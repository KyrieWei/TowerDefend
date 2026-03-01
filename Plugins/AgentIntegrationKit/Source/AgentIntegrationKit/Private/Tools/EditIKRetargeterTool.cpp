// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditIKRetargeterTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// IK Retargeter struct-based API is only available in UE 5.6+
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6

// IK Retargeter includes
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetOps.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "RetargetEditor/IKRetargetFactory.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "Rig/IKRigDefinition.h"
#include "Engine/SkeletalMesh.h"

// Per-op configuration includes
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Retargeter/RetargetOps/PelvisMotionOp.h"
#include "Retargeter/RetargetOps/RootMotionGeneratorOp.h"

// Asset creation
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

// Transaction support
#include "ScopedTransaction.h"

// UE 5.7 workaround: PostLoad() on these op settings structs is declared virtual but not exported
// with IKRIG_API (Epic bug). Provide empty stubs to satisfy the linker. These are only needed for
// vtable resolution when we create local copies via GetSettings(); the real PostLoad is only called
// during asset deserialization which doesn't go through our vtable.
#if ENGINE_MINOR_VERSION >= 7
void FIKRetargetIKChainsOpSettings::PostLoad(const FIKRigObjectVersion::Type) {}
void FIKRetargetPelvisMotionOpSettings::PostLoad(const FIKRigObjectVersion::Type) {}
void FIKRetargetRootMotionOpSettings::PostLoad(const FIKRigObjectVersion::Type) {}
#endif

TSharedPtr<FJsonObject> FEditIKRetargeterTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Basic parameters
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("IK Retargeter asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (defaults to /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> SourceRigProp = MakeShared<FJsonObject>();
	SourceRigProp->SetStringField(TEXT("type"), TEXT("string"));
	SourceRigProp->SetStringField(TEXT("description"), TEXT("Path to source IK Rig asset (the skeleton to copy animation FROM)"));
	Properties->SetObjectField(TEXT("source_ikrig"), SourceRigProp);

	TSharedPtr<FJsonObject> TargetRigProp = MakeShared<FJsonObject>();
	TargetRigProp->SetStringField(TEXT("type"), TEXT("string"));
	TargetRigProp->SetStringField(TEXT("description"), TEXT("Path to target IK Rig asset (the skeleton to copy animation TO)"));
	Properties->SetObjectField(TEXT("target_ikrig"), TargetRigProp);

	// Op stack operations
	TSharedPtr<FJsonObject> AddDefaultOpsProp = MakeShared<FJsonObject>();
	AddDefaultOpsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	AddDefaultOpsProp->SetStringField(TEXT("description"), TEXT("Add the standard op stack (PelvisMotion, FKChains, IKChains, RunIKRig, RootMotionGenerator). Recommended for most retargeting setups."));
	Properties->SetObjectField(TEXT("add_default_ops"), AddDefaultOpsProp);

	TSharedPtr<FJsonObject> AddOpsProp = MakeShared<FJsonObject>();
	AddOpsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddOpsProp->SetStringField(TEXT("description"), TEXT("Ops to add: [{type, name, enabled}]. Types: PelvisMotion, FKChains, IKChains, RunIKRig, RootMotionGenerator, CurveRemap, StrideWarping, FloorConstraint, PinBone, FilterBone"));
	Properties->SetObjectField(TEXT("add_ops"), AddOpsProp);

	TSharedPtr<FJsonObject> RemoveOpsProp = MakeShared<FJsonObject>();
	RemoveOpsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveOpsProp->SetStringField(TEXT("description"), TEXT("Op names to remove from the stack"));
	Properties->SetObjectField(TEXT("remove_ops"), RemoveOpsProp);

	TSharedPtr<FJsonObject> EnableOpsProp = MakeShared<FJsonObject>();
	EnableOpsProp->SetStringField(TEXT("type"), TEXT("array"));
	EnableOpsProp->SetStringField(TEXT("description"), TEXT("Op names to enable"));
	Properties->SetObjectField(TEXT("enable_ops"), EnableOpsProp);

	TSharedPtr<FJsonObject> DisableOpsProp = MakeShared<FJsonObject>();
	DisableOpsProp->SetStringField(TEXT("type"), TEXT("array"));
	DisableOpsProp->SetStringField(TEXT("description"), TEXT("Op names to disable"));
	Properties->SetObjectField(TEXT("disable_ops"), DisableOpsProp);

	// Chain mapping
	TSharedPtr<FJsonObject> AutoMapProp = MakeShared<FJsonObject>();
	AutoMapProp->SetStringField(TEXT("type"), TEXT("string"));
	AutoMapProp->SetStringField(TEXT("description"), TEXT("Auto-map chains: 'exact' (same name), 'fuzzy' (closest match by name similarity), or 'clear' (remove all mappings)"));
	Properties->SetObjectField(TEXT("auto_map_chains"), AutoMapProp);

	TSharedPtr<FJsonObject> MapChainsProp = MakeShared<FJsonObject>();
	MapChainsProp->SetStringField(TEXT("type"), TEXT("array"));
	MapChainsProp->SetStringField(TEXT("description"), TEXT("Manual chain mappings: [{target_chain, source_chain}]. Maps source chain to target chain for retargeting."));
	Properties->SetObjectField(TEXT("map_chains"), MapChainsProp);

	TSharedPtr<FJsonObject> UnmapChainsProp = MakeShared<FJsonObject>();
	UnmapChainsProp->SetStringField(TEXT("type"), TEXT("array"));
	UnmapChainsProp->SetStringField(TEXT("description"), TEXT("Target chain names to unmap (clear their source mapping)"));
	Properties->SetObjectField(TEXT("unmap_chains"), UnmapChainsProp);

	// Pose operations
	TSharedPtr<FJsonObject> CreatePoseProp = MakeShared<FJsonObject>();
	CreatePoseProp->SetStringField(TEXT("type"), TEXT("object"));
	CreatePoseProp->SetStringField(TEXT("description"), TEXT("Create a retarget pose: {name, for: 'source' or 'target'}"));
	Properties->SetObjectField(TEXT("create_pose"), CreatePoseProp);

	TSharedPtr<FJsonObject> DeletePoseProp = MakeShared<FJsonObject>();
	DeletePoseProp->SetStringField(TEXT("type"), TEXT("object"));
	DeletePoseProp->SetStringField(TEXT("description"), TEXT("Delete a retarget pose: {name, for: 'source' or 'target'}"));
	Properties->SetObjectField(TEXT("delete_pose"), DeletePoseProp);

	TSharedPtr<FJsonObject> SetCurrentPoseProp = MakeShared<FJsonObject>();
	SetCurrentPoseProp->SetStringField(TEXT("type"), TEXT("object"));
	SetCurrentPoseProp->SetStringField(TEXT("description"), TEXT("Set active retarget pose: {name, for: 'source' or 'target'}"));
	Properties->SetObjectField(TEXT("set_current_pose"), SetCurrentPoseProp);

	TSharedPtr<FJsonObject> AutoAlignProp = MakeShared<FJsonObject>();
	AutoAlignProp->SetStringField(TEXT("type"), TEXT("boolean"));
	AutoAlignProp->SetStringField(TEXT("description"), TEXT("Auto-align all bones between source and target poses for better retargeting quality"));
	Properties->SetObjectField(TEXT("auto_align"), AutoAlignProp);

	// Pose bone editing
	TSharedPtr<FJsonObject> EditPoseBonesProp = MakeShared<FJsonObject>();
	EditPoseBonesProp->SetStringField(TEXT("type"), TEXT("array"));
	EditPoseBonesProp->SetStringField(TEXT("description"), TEXT("Edit bone rotations in retarget pose: [{bone, rotation: [pitch, yaw, roll], for: 'source'/'target'}]. Adjusts individual bone alignments for better retargeting."));
	Properties->SetObjectField(TEXT("edit_pose_bones"), EditPoseBonesProp);

	TSharedPtr<FJsonObject> SetRootOffsetProp = MakeShared<FJsonObject>();
	SetRootOffsetProp->SetStringField(TEXT("type"), TEXT("object"));
	SetRootOffsetProp->SetStringField(TEXT("description"), TEXT("Set root translation offset: {offset: [x, y, z], for: 'source'/'target'}. Adjusts root bone position in the retarget pose."));
	Properties->SetObjectField(TEXT("set_root_offset"), SetRootOffsetProp);

	// Animation retargeting
	TSharedPtr<FJsonObject> RetargetAnimsProp = MakeShared<FJsonObject>();
	RetargetAnimsProp->SetStringField(TEXT("type"), TEXT("object"));
	RetargetAnimsProp->SetStringField(TEXT("description"), TEXT("Batch retarget animations: {animations: ['/Game/Anim1', ...], source_mesh: '/Game/SourceSkel', target_mesh: '/Game/TargetSkel', prefix: '', suffix: '_Retargeted', search: '', replace: '', include_referenced: true, overwrite: false}. Duplicates and retargets animation sequences to the target skeleton."));
	Properties->SetObjectField(TEXT("retarget_animations"), RetargetAnimsProp);

	// Per-op configuration
	TSharedPtr<FJsonObject> ConfigFKProp = MakeShared<FJsonObject>();
	ConfigFKProp->SetStringField(TEXT("type"), TEXT("array"));
	ConfigFKProp->SetStringField(TEXT("description"), TEXT("Configure per-chain FK settings: [{chain: 'ChainName', op_name: 'FK Chains' (optional), enable_fk: true, rotation_mode: 'Interpolated'|'OneToOne'|'OneToOneReversed'|'MatchChain'|'MatchScaledChain'|'CopyLocal'|'None', rotation_alpha: 1.0, translation_mode: 'None'|'GloballyScaled'|'Absolute'|'StretchBoneLengthUniformly'|'StretchBoneLengthNonUniformly'|'OrientAndScale', translation_alpha: 1.0}]. CRITICAL for finger retargeting: use rotation_mode='OneToOne' + translation_mode='StretchBoneLengthUniformly' on finger chains."));
	Properties->SetObjectField(TEXT("configure_fk_chains"), ConfigFKProp);

	TSharedPtr<FJsonObject> ConfigIKProp = MakeShared<FJsonObject>();
	ConfigIKProp->SetStringField(TEXT("type"), TEXT("array"));
	ConfigIKProp->SetStringField(TEXT("description"), TEXT("Configure per-chain IK settings: [{chain: 'ChainName', op_name: 'IK Goals' (optional), enable_ik: true, blend_to_source: 0.0, blend_to_source_translation: 1.0, blend_to_source_rotation: 0.0, static_offset: [x,y,z], static_local_offset: [x,y,z], static_rotation_offset: [pitch,yaw,roll], scale_vertical: 1.0, extension: 1.0}]"));
	Properties->SetObjectField(TEXT("configure_ik_chains"), ConfigIKProp);

	TSharedPtr<FJsonObject> ConfigPelvisProp = MakeShared<FJsonObject>();
	ConfigPelvisProp->SetStringField(TEXT("type"), TEXT("object"));
	ConfigPelvisProp->SetStringField(TEXT("description"), TEXT("Configure pelvis motion op: {op_name (optional), source_pelvis_bone, target_pelvis_bone, floor_constraint_weight, rotation_alpha, translation_alpha, scale_horizontal, scale_vertical, affect_ik_horizontal, affect_ik_vertical, blend_to_source_translation, rotation_offset_local: [p,y,r], rotation_offset_global: [p,y,r], translation_offset_local: [x,y,z], translation_offset_global: [x,y,z]}"));
	Properties->SetObjectField(TEXT("configure_pelvis"), ConfigPelvisProp);

	TSharedPtr<FJsonObject> ConfigRootMotionProp = MakeShared<FJsonObject>();
	ConfigRootMotionProp->SetStringField(TEXT("type"), TEXT("object"));
	ConfigRootMotionProp->SetStringField(TEXT("description"), TEXT("Configure root motion op: {op_name (optional), source_root_bone, target_root_bone, target_pelvis_bone, root_motion_source: 'CopyFromSourceRoot'|'GenerateFromTargetPelvis', root_height_source: 'CopyHeightFromSource'|'SnapToGround', rotate_with_pelvis: false, maintain_offset_from_pelvis: true, propagate_to_children: true, global_offset: {location: [x,y,z], rotation: [p,y,r]}}"));
	Properties->SetObjectField(TEXT("configure_root_motion"), ConfigRootMotionProp);

	// Op stack operations
	TSharedPtr<FJsonObject> MoveOpProp = MakeShared<FJsonObject>();
	MoveOpProp->SetStringField(TEXT("type"), TEXT("object"));
	MoveOpProp->SetStringField(TEXT("description"), TEXT("Move an op in the stack: {name: 'opName', to_index: 2}. Use read_asset to see current op indices."));
	Properties->SetObjectField(TEXT("move_op"), MoveOpProp);

	// Preview mesh
	TSharedPtr<FJsonObject> PreviewMeshProp = MakeShared<FJsonObject>();
	PreviewMeshProp->SetStringField(TEXT("type"), TEXT("object"));
	PreviewMeshProp->SetStringField(TEXT("description"), TEXT("Set preview skeletal mesh: {mesh: '/Game/Path/Mesh', for: 'source'|'target'}"));
	Properties->SetObjectField(TEXT("set_preview_mesh"), PreviewMeshProp);

	// Additional pose operations
	TSharedPtr<FJsonObject> DupPoseProp = MakeShared<FJsonObject>();
	DupPoseProp->SetStringField(TEXT("type"), TEXT("object"));
	DupPoseProp->SetStringField(TEXT("description"), TEXT("Duplicate a retarget pose: {name: 'PoseName', new_name: 'NewPose', for: 'source'|'target'}"));
	Properties->SetObjectField(TEXT("duplicate_pose"), DupPoseProp);

	TSharedPtr<FJsonObject> RenamePoseProp = MakeShared<FJsonObject>();
	RenamePoseProp->SetStringField(TEXT("type"), TEXT("object"));
	RenamePoseProp->SetStringField(TEXT("description"), TEXT("Rename a retarget pose: {old_name: 'OldName', new_name: 'NewName', for: 'source'|'target'}"));
	Properties->SetObjectField(TEXT("rename_pose"), RenamePoseProp);

	TSharedPtr<FJsonObject> ResetPoseProp = MakeShared<FJsonObject>();
	ResetPoseProp->SetStringField(TEXT("type"), TEXT("object"));
	ResetPoseProp->SetStringField(TEXT("description"), TEXT("Reset bones in a retarget pose to reference: {pose_name: 'Default', bones: ['bone1', 'bone2'] (empty=reset all), for: 'source'|'target'}"));
	Properties->SetObjectField(TEXT("reset_pose_bones"), ResetPoseProp);

	TSharedPtr<FJsonObject> SnapGroundProp = MakeShared<FJsonObject>();
	SnapGroundProp->SetStringField(TEXT("type"), TEXT("object"));
	SnapGroundProp->SetStringField(TEXT("description"), TEXT("Snap skeleton to ground plane using a reference bone: {bone: 'foot_l', for: 'source'|'target'}"));
	Properties->SetObjectField(TEXT("snap_to_ground"), SnapGroundProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditIKRetargeterTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}
	Args->TryGetStringField(TEXT("path"), Path);

	// Get or create the retargeter asset
	UIKRetargeter* Retargeter = GetOrLoadRetargeter(Name, Path);
	bool bCreatedNew = false;

	if (!Retargeter)
	{
		Retargeter = CreateRetargeterAsset(Name, Path);
		if (!Retargeter)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Failed to create IK Retargeter: %s"), *Name));
		}
		bCreatedNew = true;
	}

	// Get the controller
	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		return FToolResult::Fail(TEXT("Failed to get IK Retargeter controller"));
	}

	TArray<FString> Results;
	if (bCreatedNew)
	{
		Results.Add(FString::Printf(TEXT("Created IK Retargeter: %s"), *Retargeter->GetPathName()));
	}
	else
	{
		Results.Add(FString::Printf(TEXT("Editing IK Retargeter: %s"), *Retargeter->GetPathName()));
	}

	// Begin transaction for undo support
	FScopedTransaction Transaction(FText::FromString(TEXT("Edit IK Retargeter")));
	Retargeter->Modify();

	// Set source IK Rig
	FString SourceRigPath;
	if (Args->TryGetStringField(TEXT("source_ikrig"), SourceRigPath) && !SourceRigPath.IsEmpty())
	{
		SetSourceIKRig(Controller, SourceRigPath, Results);
	}

	// Set target IK Rig
	FString TargetRigPath;
	if (Args->TryGetStringField(TEXT("target_ikrig"), TargetRigPath) && !TargetRigPath.IsEmpty())
	{
		SetTargetIKRig(Controller, TargetRigPath, Results);
	}

	// Set preview mesh
	const TSharedPtr<FJsonObject>* PreviewMeshObj;
	if (Args->TryGetObjectField(TEXT("set_preview_mesh"), PreviewMeshObj))
	{
		SetPreviewMeshOp(Controller, *PreviewMeshObj, Results);
	}

	// Add default ops
	bool bAddDefaultOps = false;
	if (Args->TryGetBoolField(TEXT("add_default_ops"), bAddDefaultOps) && bAddDefaultOps)
	{
		AddDefaultOps(Controller, Results);
	}

	// Add custom ops
	const TArray<TSharedPtr<FJsonValue>>* AddOpsArray;
	if (Args->TryGetArrayField(TEXT("add_ops"), AddOpsArray))
	{
		AddOps(Controller, AddOpsArray, Results);
	}

	// Remove ops
	const TArray<TSharedPtr<FJsonValue>>* RemoveOpsArray;
	if (Args->TryGetArrayField(TEXT("remove_ops"), RemoveOpsArray))
	{
		RemoveOps(Controller, RemoveOpsArray, Results);
	}

	// Move op (after add/remove so indices are stable)
	const TSharedPtr<FJsonObject>* MoveOpObj;
	if (Args->TryGetObjectField(TEXT("move_op"), MoveOpObj))
	{
		MoveOp(Controller, *MoveOpObj, Results);
	}

	// Enable ops
	const TArray<TSharedPtr<FJsonValue>>* EnableOpsArray;
	if (Args->TryGetArrayField(TEXT("enable_ops"), EnableOpsArray))
	{
		EnableOps(Controller, EnableOpsArray, true, Results);
	}

	// Disable ops
	const TArray<TSharedPtr<FJsonValue>>* DisableOpsArray;
	if (Args->TryGetArrayField(TEXT("disable_ops"), DisableOpsArray))
	{
		EnableOps(Controller, DisableOpsArray, false, Results);
	}

	// Assign IK rigs to all ops (ensures ops have proper IK rig references and chain mappings)
#if ENGINE_MINOR_VERSION >= 7
	if (Controller->GetNumRetargetOps() > 0)
	{
		const UIKRigDefinition* SourceRig = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
		const UIKRigDefinition* TargetRig = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
		if (SourceRig)
		{
			Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Source, SourceRig);
		}
		if (TargetRig)
		{
			Controller->AssignIKRigToAllOps(ERetargetSourceOrTarget::Target, TargetRig);
		}
	}
#endif

	// Auto-map chains
	FString AutoMapType;
	if (Args->TryGetStringField(TEXT("auto_map_chains"), AutoMapType) && !AutoMapType.IsEmpty())
	{
		AutoMapChains(Controller, AutoMapType, Results);
	}

	// Manual chain mappings
	const TArray<TSharedPtr<FJsonValue>>* MapChainsArray;
	if (Args->TryGetArrayField(TEXT("map_chains"), MapChainsArray))
	{
		MapChains(Controller, MapChainsArray, Results);
	}

	// Unmap chains
	const TArray<TSharedPtr<FJsonValue>>* UnmapChainsArray;
	if (Args->TryGetArrayField(TEXT("unmap_chains"), UnmapChainsArray))
	{
		UnmapChains(Controller, UnmapChainsArray, Results);
	}

	// Configure FK chains (per-chain rotation/translation modes — fixes finger retargeting)
	const TArray<TSharedPtr<FJsonValue>>* ConfigFKArray;
	if (Args->TryGetArrayField(TEXT("configure_fk_chains"), ConfigFKArray))
	{
		ConfigureFKChains(Controller, ConfigFKArray, Results);
	}

	// Configure IK chains (per-chain IK goal settings)
	const TArray<TSharedPtr<FJsonValue>>* ConfigIKArray;
	if (Args->TryGetArrayField(TEXT("configure_ik_chains"), ConfigIKArray))
	{
		ConfigureIKChains(Controller, ConfigIKArray, Results);
	}

	// Configure pelvis motion op
	const TSharedPtr<FJsonObject>* ConfigPelvisObj;
	if (Args->TryGetObjectField(TEXT("configure_pelvis"), ConfigPelvisObj))
	{
		ConfigurePelvis(Controller, *ConfigPelvisObj, Results);
	}

	// Configure root motion op
	const TSharedPtr<FJsonObject>* ConfigRootMotionObj;
	if (Args->TryGetObjectField(TEXT("configure_root_motion"), ConfigRootMotionObj))
	{
		ConfigureRootMotion(Controller, *ConfigRootMotionObj, Results);
	}

	// Pose operations
	const TSharedPtr<FJsonObject>* CreatePoseObj;
	if (Args->TryGetObjectField(TEXT("create_pose"), CreatePoseObj))
	{
		CreatePose(Controller, *CreatePoseObj, Results);
	}

	const TSharedPtr<FJsonObject>* DeletePoseObj;
	if (Args->TryGetObjectField(TEXT("delete_pose"), DeletePoseObj))
	{
		DeletePose(Controller, *DeletePoseObj, Results);
	}

	const TSharedPtr<FJsonObject>* SetCurrentPoseObj;
	if (Args->TryGetObjectField(TEXT("set_current_pose"), SetCurrentPoseObj))
	{
		SetCurrentPose(Controller, *SetCurrentPoseObj, Results);
	}

	// Duplicate pose
	const TSharedPtr<FJsonObject>* DupPoseObj;
	if (Args->TryGetObjectField(TEXT("duplicate_pose"), DupPoseObj))
	{
		DuplicatePose(Controller, *DupPoseObj, Results);
	}

	// Rename pose
	const TSharedPtr<FJsonObject>* RenamePoseObj;
	if (Args->TryGetObjectField(TEXT("rename_pose"), RenamePoseObj))
	{
		RenamePose(Controller, *RenamePoseObj, Results);
	}

	// Auto-align
	bool bAutoAlign = false;
	if (Args->TryGetBoolField(TEXT("auto_align"), bAutoAlign) && bAutoAlign)
	{
		AutoAlignBones(Controller, Results);
	}

	// Edit pose bones
	const TArray<TSharedPtr<FJsonValue>>* EditPoseBonesArray;
	if (Args->TryGetArrayField(TEXT("edit_pose_bones"), EditPoseBonesArray))
	{
		EditPoseBones(Controller, EditPoseBonesArray, Results);
	}

	// Set root offset
	const TSharedPtr<FJsonObject>* SetRootOffsetObj;
	if (Args->TryGetObjectField(TEXT("set_root_offset"), SetRootOffsetObj))
	{
		SetRootOffset(Controller, *SetRootOffsetObj, Results);
	}

	// Reset pose bones
	const TSharedPtr<FJsonObject>* ResetPoseObj;
	if (Args->TryGetObjectField(TEXT("reset_pose_bones"), ResetPoseObj))
	{
		ResetPoseBones(Controller, *ResetPoseObj, Results);
	}

	// Snap to ground
	const TSharedPtr<FJsonObject>* SnapGroundObj;
	if (Args->TryGetObjectField(TEXT("snap_to_ground"), SnapGroundObj))
	{
		SnapToGroundOp(Controller, *SnapGroundObj, Results);
	}

	// Retarget animations
	const TSharedPtr<FJsonObject>* RetargetAnimsObj;
	if (Args->TryGetObjectField(TEXT("retarget_animations"), RetargetAnimsObj))
	{
		RetargetAnimations(Retargeter, *RetargetAnimsObj, Results);
	}

	// Mark package dirty and save
	Retargeter->MarkPackageDirty();

	// Notify asset registry
	FAssetRegistryModule::AssetCreated(Retargeter);

	return FToolResult::Ok(FString::Join(Results, TEXT("\n")));
}

// ============================================================================
// IK RIG ASSIGNMENT
// ============================================================================

bool FEditIKRetargeterTool::SetSourceIKRig(UIKRetargeterController* Controller, const FString& IKRigPath, TArray<FString>& OutResults)
{
	UIKRigDefinition* IKRig = LoadIKRig(IKRigPath);
	if (!IKRig)
	{
		OutResults.Add(FString::Printf(TEXT("Failed to load source IK Rig: %s"), *IKRigPath));
		return false;
	}

	Controller->SetIKRig(ERetargetSourceOrTarget::Source, IKRig);
	OutResults.Add(FString::Printf(TEXT("Set source IK Rig: %s"), *IKRig->GetName()));
	return true;
}

bool FEditIKRetargeterTool::SetTargetIKRig(UIKRetargeterController* Controller, const FString& IKRigPath, TArray<FString>& OutResults)
{
	UIKRigDefinition* IKRig = LoadIKRig(IKRigPath);
	if (!IKRig)
	{
		OutResults.Add(FString::Printf(TEXT("Failed to load target IK Rig: %s"), *IKRigPath));
		return false;
	}

	Controller->SetIKRig(ERetargetSourceOrTarget::Target, IKRig);
	OutResults.Add(FString::Printf(TEXT("Set target IK Rig: %s"), *IKRig->GetName()));
	return true;
}

// ============================================================================
// OP STACK OPERATIONS
// ============================================================================

bool FEditIKRetargeterTool::AddDefaultOps(UIKRetargeterController* Controller, TArray<FString>& OutResults)
{
	if (Controller->GetNumRetargetOps() > 0)
	{
		OutResults.Add(FString::Printf(TEXT("Skipped add_default_ops: op stack already has %d ops (use remove_ops first or add_ops for specific ops)"), Controller->GetNumRetargetOps()));
		return false;
	}

	Controller->AddDefaultOps();
	OutResults.Add(FString::Printf(TEXT("Added default ops (%d ops)"), Controller->GetNumRetargetOps()));
	return true;
}

int32 FEditIKRetargeterTool::AddOps(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* OpsArray, TArray<FString>& OutResults)
{
	if (!OpsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& OpValue : *OpsArray)
	{
		const TSharedPtr<FJsonObject>* OpObj;
		if (!OpValue->TryGetObject(OpObj))
		{
			continue;
		}

		FOpDef Op = ParseOpDef(*OpObj);
		if (Op.Type.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped op with no type"));
			continue;
		}

		FString OpTypePath = GetOpTypePath(Op.Type);
		if (OpTypePath.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("Unknown op type: %s"), *Op.Type));
			continue;
		}

		int32 OpIndex = Controller->AddRetargetOp(OpTypePath);
		if (OpIndex >= 0)
		{
			// Set custom name if provided
			if (!Op.Name.IsEmpty())
			{
				Controller->SetOpName(FName(*Op.Name), OpIndex);
			}

			// Set enabled state
			Controller->SetRetargetOpEnabled(OpIndex, Op.bEnabled);

			FName ActualName = Controller->GetOpName(OpIndex);
			OutResults.Add(FString::Printf(TEXT("Added op: %s (index %d)"), *ActualName.ToString(), OpIndex));
			Added++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add op: %s"), *Op.Type));
		}
	}

	return Added;
}

int32 FEditIKRetargeterTool::RemoveOps(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* OpsArray, TArray<FString>& OutResults)
{
	if (!OpsArray) return 0;

	int32 Removed = 0;
	// Process in reverse order to handle index shifts
	TArray<int32> IndicesToRemove;

	for (const TSharedPtr<FJsonValue>& OpValue : *OpsArray)
	{
		FString OpName = OpValue->AsString();
		if (OpName.IsEmpty()) continue;

		int32 Index = Controller->GetIndexOfOpByName(FName(*OpName));
		if (Index >= 0)
		{
			IndicesToRemove.Add(Index);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Op not found: %s"), *OpName));
		}
	}

	// Sort descending and remove
	IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Index : IndicesToRemove)
	{
		FName OpName = Controller->GetOpName(Index);
		if (Controller->RemoveRetargetOp(Index))
		{
			OutResults.Add(FString::Printf(TEXT("Removed op: %s"), *OpName.ToString()));
			Removed++;
		}
	}

	return Removed;
}

int32 FEditIKRetargeterTool::EnableOps(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* OpsArray, bool bEnable, TArray<FString>& OutResults)
{
	if (!OpsArray) return 0;

	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& OpValue : *OpsArray)
	{
		FString OpName = OpValue->AsString();
		if (OpName.IsEmpty()) continue;

		int32 Index = Controller->GetIndexOfOpByName(FName(*OpName));
		if (Index >= 0)
		{
			Controller->SetRetargetOpEnabled(Index, bEnable);
			OutResults.Add(FString::Printf(TEXT("%s op: %s"), bEnable ? TEXT("Enabled") : TEXT("Disabled"), *OpName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Op not found: %s"), *OpName));
		}
	}

	return Count;
}

// ============================================================================
// CHAIN MAPPING OPERATIONS
// ============================================================================

bool FEditIKRetargeterTool::AutoMapChains(UIKRetargeterController* Controller, const FString& MapType, TArray<FString>& OutResults)
{
	EAutoMapChainType AutoMapType;

	if (MapType.Equals(TEXT("exact"), ESearchCase::IgnoreCase))
	{
		AutoMapType = EAutoMapChainType::Exact;
	}
	else if (MapType.Equals(TEXT("fuzzy"), ESearchCase::IgnoreCase))
	{
		AutoMapType = EAutoMapChainType::Fuzzy;
	}
	else if (MapType.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
	{
		AutoMapType = EAutoMapChainType::Clear;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Unknown auto_map_chains type: %s (use 'exact', 'fuzzy', or 'clear')"), *MapType));
		return false;
	}

	Controller->AutoMapChains(AutoMapType, true);
	OutResults.Add(FString::Printf(TEXT("Auto-mapped chains using '%s' mode"), *MapType));
	return true;
}

int32 FEditIKRetargeterTool::MapChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* MappingsArray, TArray<FString>& OutResults)
{
	if (!MappingsArray) return 0;

	int32 Mapped = 0;
	for (const TSharedPtr<FJsonValue>& MapValue : *MappingsArray)
	{
		const TSharedPtr<FJsonObject>* MapObj;
		if (!MapValue->TryGetObject(MapObj))
		{
			continue;
		}

		FChainMapOp Op = ParseChainMapOp(*MapObj);
		if (Op.TargetChain.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped mapping with no target_chain"));
			continue;
		}

		if (Controller->SetSourceChain(FName(*Op.SourceChain), FName(*Op.TargetChain)))
		{
			OutResults.Add(FString::Printf(TEXT("Mapped: %s -> %s"), *Op.SourceChain, *Op.TargetChain));
			Mapped++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to map: %s -> %s"), *Op.SourceChain, *Op.TargetChain));
		}
	}

	return Mapped;
}

int32 FEditIKRetargeterTool::UnmapChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults)
{
	if (!ChainsArray) return 0;

	int32 Unmapped = 0;
	for (const TSharedPtr<FJsonValue>& ChainValue : *ChainsArray)
	{
		FString TargetChain = ChainValue->AsString();
		if (TargetChain.IsEmpty()) continue;

		// Set source to NAME_None to unmap
		if (Controller->SetSourceChain(NAME_None, FName(*TargetChain)))
		{
			OutResults.Add(FString::Printf(TEXT("Unmapped target chain: %s"), *TargetChain));
			Unmapped++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to unmap: %s"), *TargetChain));
		}
	}

	return Unmapped;
}

// ============================================================================
// POSE OPERATIONS
// ============================================================================

bool FEditIKRetargeterTool::CreatePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults)
{
	FPoseOp Op = ParsePoseOp(PoseObj);
	if (Op.Name.IsEmpty())
	{
		OutResults.Add(TEXT("create_pose requires 'name' field"));
		return false;
	}

	ERetargetSourceOrTarget SourceOrTarget = Op.bIsSource ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;
	FName NewPoseName = Controller->CreateRetargetPose(FName(*Op.Name), SourceOrTarget);

	if (!NewPoseName.IsNone())
	{
		OutResults.Add(FString::Printf(TEXT("Created %s pose: %s"),
			Op.bIsSource ? TEXT("source") : TEXT("target"), *NewPoseName.ToString()));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Failed to create pose: %s"), *Op.Name));
		return false;
	}
}

bool FEditIKRetargeterTool::DeletePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults)
{
	FPoseOp Op = ParsePoseOp(PoseObj);
	if (Op.Name.IsEmpty())
	{
		OutResults.Add(TEXT("delete_pose requires 'name' field"));
		return false;
	}

	ERetargetSourceOrTarget SourceOrTarget = Op.bIsSource ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	if (Controller->RemoveRetargetPose(FName(*Op.Name), SourceOrTarget))
	{
		OutResults.Add(FString::Printf(TEXT("Deleted %s pose: %s"),
			Op.bIsSource ? TEXT("source") : TEXT("target"), *Op.Name));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Failed to delete pose: %s"), *Op.Name));
		return false;
	}
}

bool FEditIKRetargeterTool::SetCurrentPose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults)
{
	FPoseOp Op = ParsePoseOp(PoseObj);
	if (Op.Name.IsEmpty())
	{
		OutResults.Add(TEXT("set_current_pose requires 'name' field"));
		return false;
	}

	ERetargetSourceOrTarget SourceOrTarget = Op.bIsSource ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	if (Controller->SetCurrentRetargetPose(FName(*Op.Name), SourceOrTarget))
	{
		OutResults.Add(FString::Printf(TEXT("Set current %s pose: %s"),
			Op.bIsSource ? TEXT("source") : TEXT("target"), *Op.Name));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Failed to set current pose: %s"), *Op.Name));
		return false;
	}
}

bool FEditIKRetargeterTool::AutoAlignBones(UIKRetargeterController* Controller, TArray<FString>& OutResults)
{
	Controller->AutoAlignAllBones(ERetargetSourceOrTarget::Target);
	OutResults.Add(TEXT("Auto-aligned all bones for target pose"));
	return true;
}

// ============================================================================
// PARSING HELPERS
// ============================================================================

FEditIKRetargeterTool::FChainMapOp FEditIKRetargeterTool::ParseChainMapOp(const TSharedPtr<FJsonObject>& MapObj)
{
	FChainMapOp Op;
	MapObj->TryGetStringField(TEXT("target_chain"), Op.TargetChain);
	MapObj->TryGetStringField(TEXT("source_chain"), Op.SourceChain);
	return Op;
}

FEditIKRetargeterTool::FOpDef FEditIKRetargeterTool::ParseOpDef(const TSharedPtr<FJsonObject>& OpObj)
{
	FOpDef Op;
	OpObj->TryGetStringField(TEXT("type"), Op.Type);
	OpObj->TryGetStringField(TEXT("name"), Op.Name);
	if (!OpObj->TryGetBoolField(TEXT("enabled"), Op.bEnabled))
	{
		Op.bEnabled = true;
	}
	return Op;
}

FEditIKRetargeterTool::FPoseOp FEditIKRetargeterTool::ParsePoseOp(const TSharedPtr<FJsonObject>& PoseObj)
{
	FPoseOp Op;
	PoseObj->TryGetStringField(TEXT("name"), Op.Name);

	FString ForValue;
	if (PoseObj->TryGetStringField(TEXT("for"), ForValue))
	{
		Op.bIsSource = ForValue.Equals(TEXT("source"), ESearchCase::IgnoreCase);
	}

	return Op;
}

FString FEditIKRetargeterTool::GetOpTypePath(const FString& TypeName)
{
	if (TypeName.Equals(TEXT("PelvisMotion"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Pelvis"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetPelvisMotionOp");
	}
	else if (TypeName.Equals(TEXT("FKChains"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("FK"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetFKChainsOp");
	}
	else if (TypeName.Equals(TEXT("IKChains"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("IK"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetIKChainsOp");
	}
	else if (TypeName.Equals(TEXT("RunIKRig"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("RunIK"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetRunIKRigOp");
	}
	else if (TypeName.Equals(TEXT("RootMotion"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetRootMotionOp");
	}
	else if (TypeName.Equals(TEXT("CurveRemap"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Curve"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetCurveRemapOp");
	}
	else if (TypeName.Equals(TEXT("StrideWarping"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Stride"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetStrideWarpingOp");
	}
	else if (TypeName.Equals(TEXT("FloorConstraint"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Floor"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetFloorConstraintOp");
	}
	else if (TypeName.Equals(TEXT("PinBone"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Pin"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetPinBoneOp");
	}
	else if (TypeName.Equals(TEXT("FilterBone"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Filter"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRetargetFilterBoneOp");
	}

	return FString();
}

bool FEditIKRetargeterTool::ParseSourceOrTarget(const FString& Value, bool& bOutIsSource)
{
	if (Value.Equals(TEXT("source"), ESearchCase::IgnoreCase))
	{
		bOutIsSource = true;
		return true;
	}
	else if (Value.Equals(TEXT("target"), ESearchCase::IgnoreCase))
	{
		bOutIsSource = false;
		return true;
	}
	return false;
}

FEditIKRetargeterTool::FBonePoseEdit FEditIKRetargeterTool::ParseBonePoseEdit(const TSharedPtr<FJsonObject>& BoneObj)
{
	FBonePoseEdit Edit;
	BoneObj->TryGetStringField(TEXT("bone"), Edit.BoneName);

	FString ForValue;
	if (BoneObj->TryGetStringField(TEXT("for"), ForValue))
	{
		Edit.bIsSource = ForValue.Equals(TEXT("source"), ESearchCase::IgnoreCase);
	}

	const TArray<TSharedPtr<FJsonValue>>* RotArray;
	if (BoneObj->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray->Num() >= 3)
	{
		Edit.Rotation = FRotator(
			(*RotArray)[0]->AsNumber(),
			(*RotArray)[1]->AsNumber(),
			(*RotArray)[2]->AsNumber()
		);
	}

	return Edit;
}

FEditIKRetargeterTool::FRootOffset FEditIKRetargeterTool::ParseRootOffset(const TSharedPtr<FJsonObject>& OffsetObj)
{
	FRootOffset Offset;

	FString ForValue;
	if (OffsetObj->TryGetStringField(TEXT("for"), ForValue))
	{
		Offset.bIsSource = ForValue.Equals(TEXT("source"), ESearchCase::IgnoreCase);
	}

	const TArray<TSharedPtr<FJsonValue>>* OffsetArray;
	if (OffsetObj->TryGetArrayField(TEXT("offset"), OffsetArray) && OffsetArray->Num() >= 3)
	{
		Offset.Offset = FVector(
			(*OffsetArray)[0]->AsNumber(),
			(*OffsetArray)[1]->AsNumber(),
			(*OffsetArray)[2]->AsNumber()
		);
	}

	return Offset;
}

FEditIKRetargeterTool::FRetargetAnimParams FEditIKRetargeterTool::ParseRetargetAnimParams(const TSharedPtr<FJsonObject>& RetargetObj)
{
	FRetargetAnimParams Params;

	const TArray<TSharedPtr<FJsonValue>>* AnimsArray;
	if (RetargetObj->TryGetArrayField(TEXT("animations"), AnimsArray))
	{
		for (const TSharedPtr<FJsonValue>& AnimValue : *AnimsArray)
		{
			FString AnimPath = AnimValue->AsString();
			if (!AnimPath.IsEmpty())
			{
				Params.AnimationPaths.Add(AnimPath);
			}
		}
	}

	RetargetObj->TryGetStringField(TEXT("source_mesh"), Params.SourceMeshPath);
	RetargetObj->TryGetStringField(TEXT("target_mesh"), Params.TargetMeshPath);
	RetargetObj->TryGetStringField(TEXT("prefix"), Params.Prefix);
	RetargetObj->TryGetStringField(TEXT("suffix"), Params.Suffix);
	RetargetObj->TryGetStringField(TEXT("search"), Params.Search);
	RetargetObj->TryGetStringField(TEXT("replace"), Params.Replace);

	if (!RetargetObj->TryGetBoolField(TEXT("include_referenced"), Params.bIncludeReferenced))
	{
		Params.bIncludeReferenced = true;
	}
	RetargetObj->TryGetBoolField(TEXT("overwrite"), Params.bOverwrite);

	return Params;
}

// ============================================================================
// POSE BONE EDITING
// ============================================================================

int32 FEditIKRetargeterTool::EditPoseBones(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* BonesArray, TArray<FString>& OutResults)
{
	if (!BonesArray) return 0;

	int32 Edited = 0;
	for (const TSharedPtr<FJsonValue>& BoneValue : *BonesArray)
	{
		const TSharedPtr<FJsonObject>* BoneObj;
		if (!BoneValue->TryGetObject(BoneObj))
		{
			continue;
		}

		FBonePoseEdit Edit = ParseBonePoseEdit(*BoneObj);
		if (Edit.BoneName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped bone edit with no bone name"));
			continue;
		}

		ERetargetSourceOrTarget SourceOrTarget = Edit.bIsSource ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;
		FQuat RotationQuat = Edit.Rotation.Quaternion();

		Controller->SetRotationOffsetForRetargetPoseBone(FName(*Edit.BoneName), RotationQuat, SourceOrTarget);
		OutResults.Add(FString::Printf(TEXT("Set %s pose bone '%s' rotation: (P=%.1f, Y=%.1f, R=%.1f)"),
			Edit.bIsSource ? TEXT("source") : TEXT("target"),
			*Edit.BoneName,
			Edit.Rotation.Pitch, Edit.Rotation.Yaw, Edit.Rotation.Roll));
		Edited++;
	}

	return Edited;
}

bool FEditIKRetargeterTool::SetRootOffset(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& OffsetObj, TArray<FString>& OutResults)
{
	FRootOffset Offset = ParseRootOffset(OffsetObj);
	ERetargetSourceOrTarget SourceOrTarget = Offset.bIsSource ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	Controller->SetRootOffsetInRetargetPose(Offset.Offset, SourceOrTarget);
	OutResults.Add(FString::Printf(TEXT("Set %s pose root offset: (X=%.1f, Y=%.1f, Z=%.1f)"),
		Offset.bIsSource ? TEXT("source") : TEXT("target"),
		Offset.Offset.X, Offset.Offset.Y, Offset.Offset.Z));

	return true;
}

// ============================================================================
// ANIMATION RETARGETING
// ============================================================================

bool FEditIKRetargeterTool::RetargetAnimations(UIKRetargeter* Retargeter, const TSharedPtr<FJsonObject>& RetargetObj, TArray<FString>& OutResults)
{
	FRetargetAnimParams Params = ParseRetargetAnimParams(RetargetObj);

	if (Params.AnimationPaths.Num() == 0)
	{
		OutResults.Add(TEXT("retarget_animations requires 'animations' array with at least one animation path"));
		return false;
	}

	// Load source mesh
	USkeletalMesh* SourceMesh = nullptr;
	if (!Params.SourceMeshPath.IsEmpty())
	{
		FString FullPath = Params.SourceMeshPath;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/Game/") + FullPath;
		}
		SourceMesh = LoadObject<USkeletalMesh>(nullptr, *FullPath);
		if (!SourceMesh)
		{
			FString FullPathWithSuffix = FullPath + TEXT(".") + FPaths::GetBaseFilename(FullPath);
			SourceMesh = LoadObject<USkeletalMesh>(nullptr, *FullPathWithSuffix);
		}
	}

	// Try to get source mesh from retargeter's source IK Rig
	if (!SourceMesh)
	{
		const UIKRigDefinition* SourceRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Source);
		if (SourceRig)
		{
			SourceMesh = SourceRig->GetPreviewMesh();
		}
	}

	if (!SourceMesh)
	{
		OutResults.Add(TEXT("retarget_animations requires 'source_mesh' path or source IK Rig must have a preview mesh set"));
		return false;
	}

	// Load target mesh
	USkeletalMesh* TargetMesh = nullptr;
	if (!Params.TargetMeshPath.IsEmpty())
	{
		FString FullPath = Params.TargetMeshPath;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/Game/") + FullPath;
		}
		TargetMesh = LoadObject<USkeletalMesh>(nullptr, *FullPath);
		if (!TargetMesh)
		{
			FString FullPathWithSuffix = FullPath + TEXT(".") + FPaths::GetBaseFilename(FullPath);
			TargetMesh = LoadObject<USkeletalMesh>(nullptr, *FullPathWithSuffix);
		}
	}

	// Try to get target mesh from retargeter's target IK Rig
	if (!TargetMesh)
	{
		const UIKRigDefinition* TargetRig = Retargeter->GetIKRig(ERetargetSourceOrTarget::Target);
		if (TargetRig)
		{
			TargetMesh = TargetRig->GetPreviewMesh();
		}
	}

	if (!TargetMesh)
	{
		OutResults.Add(TEXT("retarget_animations requires 'target_mesh' path or target IK Rig must have a preview mesh set"));
		return false;
	}

	// Build asset data array
	TArray<FAssetData> AssetsToRetarget;
	for (const FString& AnimPath : Params.AnimationPaths)
	{
		FString FullPath = AnimPath;
		if (!FullPath.StartsWith(TEXT("/")))
		{
			FullPath = TEXT("/Game/") + FullPath;
		}

		UObject* AnimAsset = LoadObject<UObject>(nullptr, *FullPath);
		if (!AnimAsset)
		{
			FString FullPathWithSuffix = FullPath + TEXT(".") + FPaths::GetBaseFilename(FullPath);
			AnimAsset = LoadObject<UObject>(nullptr, *FullPathWithSuffix);
		}

		if (AnimAsset)
		{
			AssetsToRetarget.Add(FAssetData(AnimAsset));
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to load animation: %s"), *AnimPath));
		}
	}

	if (AssetsToRetarget.Num() == 0)
	{
		OutResults.Add(TEXT("No valid animation assets found to retarget"));
		return false;
	}

	// Run batch retarget - API differs between UE versions
#if ENGINE_MINOR_VERSION >= 7
	// UE 5.7+ uses 10-parameter version
	TArray<FAssetData> RetargetedAssets = UIKRetargetBatchOperation::DuplicateAndRetarget(
		AssetsToRetarget,
		SourceMesh,
		TargetMesh,
		Retargeter,
		Params.Search,
		Params.Replace,
		Params.Prefix,
		Params.Suffix,
		Params.bIncludeReferenced,
		Params.bOverwrite
	);
#else
	// UE 5.6 uses 8-parameter version (no bIncludeReferenced, bOverwrite)
	TArray<FAssetData> RetargetedAssets = UIKRetargetBatchOperation::DuplicateAndRetarget(
		AssetsToRetarget,
		SourceMesh,
		TargetMesh,
		Retargeter,
		Params.Search,
		Params.Replace,
		Params.Prefix,
		Params.Suffix
	);
#endif

	OutResults.Add(FString::Printf(TEXT("Retargeted %d animations:"), RetargetedAssets.Num()));
	for (const FAssetData& Asset : RetargetedAssets)
	{
		OutResults.Add(FString::Printf(TEXT("  - %s"), *Asset.GetObjectPathString()));
	}

	return RetargetedAssets.Num() > 0;
}

// ============================================================================
// HELPER: FIND OP BY NAME OR TYPE
// ============================================================================

int32 FEditIKRetargeterTool::FindOpIndex(UIKRetargeterController* Controller, const FString& OpName, const UScriptStruct* ExpectedType)
{
	if (!OpName.IsEmpty())
	{
		return Controller->GetIndexOfOpByName(FName(*OpName));
	}

	// Find first op of the expected type
	for (int32 i = 0; i < Controller->GetNumRetargetOps(); ++i)
	{
		FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(i);
		if (Op && Op->GetType()->IsChildOf(ExpectedType))
		{
			return i;
		}
	}

	return INDEX_NONE;
}

// ============================================================================
// ENUM PARSING
// ============================================================================

static EFKChainRotationMode ParseFKRotationMode(const FString& ModeStr)
{
	if (ModeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::None;
	if (ModeStr.Equals(TEXT("Interpolated"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::Interpolated;
	if (ModeStr.Equals(TEXT("OneToOne"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::OneToOne;
	if (ModeStr.Equals(TEXT("OneToOneReversed"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::OneToOneReversed;
	if (ModeStr.Equals(TEXT("MatchChain"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::MatchChain;
	if (ModeStr.Equals(TEXT("MatchScaledChain"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::MatchScaledChain;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (ModeStr.Equals(TEXT("CopyLocal"), ESearchCase::IgnoreCase)) return EFKChainRotationMode::CopyLocal;
#endif
	return EFKChainRotationMode::Interpolated; // default
}

static EFKChainTranslationMode ParseFKTranslationMode(const FString& ModeStr)
{
	if (ModeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase)) return EFKChainTranslationMode::None;
	if (ModeStr.Equals(TEXT("GloballyScaled"), ESearchCase::IgnoreCase)) return EFKChainTranslationMode::GloballyScaled;
	if (ModeStr.Equals(TEXT("Absolute"), ESearchCase::IgnoreCase)) return EFKChainTranslationMode::Absolute;
	if (ModeStr.Equals(TEXT("StretchBoneLengthUniformly"), ESearchCase::IgnoreCase)) return EFKChainTranslationMode::StretchBoneLengthUniformly;
	if (ModeStr.Equals(TEXT("StretchBoneLengthNonUniformly"), ESearchCase::IgnoreCase)) return EFKChainTranslationMode::StretchBoneLengthNonUniformly;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (ModeStr.Equals(TEXT("OrientAndScale"), ESearchCase::IgnoreCase)) return EFKChainTranslationMode::OrientAndScale;
#endif
	return EFKChainTranslationMode::None; // default
}

static FString FKRotationModeToString(EFKChainRotationMode Mode)
{
	switch (Mode)
	{
	case EFKChainRotationMode::None: return TEXT("None");
	case EFKChainRotationMode::Interpolated: return TEXT("Interpolated");
	case EFKChainRotationMode::OneToOne: return TEXT("OneToOne");
	case EFKChainRotationMode::OneToOneReversed: return TEXT("OneToOneReversed");
	case EFKChainRotationMode::MatchChain: return TEXT("MatchChain");
	case EFKChainRotationMode::MatchScaledChain: return TEXT("MatchScaledChain");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	case EFKChainRotationMode::CopyLocal: return TEXT("CopyLocal");
#endif
	default: return TEXT("Unknown");
	}
}

static FString FKTranslationModeToString(EFKChainTranslationMode Mode)
{
	switch (Mode)
	{
	case EFKChainTranslationMode::None: return TEXT("None");
	case EFKChainTranslationMode::GloballyScaled: return TEXT("GloballyScaled");
	case EFKChainTranslationMode::Absolute: return TEXT("Absolute");
	case EFKChainTranslationMode::StretchBoneLengthUniformly: return TEXT("StretchBoneLengthUniformly");
	case EFKChainTranslationMode::StretchBoneLengthNonUniformly: return TEXT("StretchBoneLengthNonUniformly");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	case EFKChainTranslationMode::OrientAndScale: return TEXT("OrientAndScale");
#endif
	default: return TEXT("Unknown");
	}
}

// ============================================================================
// CONFIGURE FK CHAINS
// ============================================================================

int32 FEditIKRetargeterTool::ConfigureFKChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults)
{
	if (!ChainsArray || ChainsArray->Num() == 0) return 0;

	int32 Configured = 0;

	// Group by op_name to batch settings changes per op
	TMap<FString, TArray<const TSharedPtr<FJsonObject>*>> OpGroups;
	for (const TSharedPtr<FJsonValue>& ChainValue : *ChainsArray)
	{
		const TSharedPtr<FJsonObject>* ChainObj;
		if (!ChainValue->TryGetObject(ChainObj)) continue;

		FString OpName;
		(*ChainObj)->TryGetStringField(TEXT("op_name"), OpName);
		OpGroups.FindOrAdd(OpName).Add(ChainObj);
	}

	for (const auto& [OpName, ChainConfigs] : OpGroups)
	{
		int32 OpIndex = FindOpIndex(Controller, OpName, FIKRetargetFKChainsOp::StaticStruct());
		if (OpIndex == INDEX_NONE)
		{
			OutResults.Add(OpName.IsEmpty()
				? TEXT("No FK Chains op found in stack. Add one with add_ops or add_default_ops first.")
				: FString::Printf(TEXT("FK op not found: %s"), *OpName));
			continue;
		}

		UIKRetargetOpControllerBase* BaseCtrl = Controller->GetOpController(OpIndex);
		UIKRetargetFKChainsController* FKCtrl = Cast<UIKRetargetFKChainsController>(BaseCtrl);
		if (!FKCtrl)
		{
			OutResults.Add(FString::Printf(TEXT("Op at index %d is not an FK Chains op"), OpIndex));
			continue;
		}

		FIKRetargetFKChainsOpSettings Settings = FKCtrl->GetSettings();

		for (const TSharedPtr<FJsonObject>* ChainObjPtr : ChainConfigs)
		{
			const TSharedPtr<FJsonObject>& ChainObj = *ChainObjPtr;

			FString ChainName;
			if (!ChainObj->TryGetStringField(TEXT("chain"), ChainName) || ChainName.IsEmpty())
			{
				OutResults.Add(TEXT("configure_fk_chains: skipped entry with no 'chain' field"));
				continue;
			}

			// Find the chain entry
			FRetargetFKChainSettings* ChainSettings = nullptr;
			for (FRetargetFKChainSettings& CS : Settings.ChainsToRetarget)
			{
				if (CS.TargetChainName == FName(*ChainName))
				{
					ChainSettings = &CS;
					break;
				}
			}

			if (!ChainSettings)
			{
				OutResults.Add(FString::Printf(TEXT("FK chain not found in op: '%s'. Available chains: "), *ChainName) +
					[&]() {
						FString Available;
						for (const FRetargetFKChainSettings& CS : Settings.ChainsToRetarget)
						{
							if (!Available.IsEmpty()) Available += TEXT(", ");
							Available += CS.TargetChainName.ToString();
						}
						return Available.IsEmpty() ? TEXT("(none)") : Available;
					}());
				continue;
			}

			// Apply settings (only fields that are present in JSON)
			bool bEnableFK;
			if (ChainObj->TryGetBoolField(TEXT("enable_fk"), bEnableFK))
			{
				ChainSettings->EnableFK = bEnableFK;
			}

			FString RotMode;
			if (ChainObj->TryGetStringField(TEXT("rotation_mode"), RotMode) && !RotMode.IsEmpty())
			{
				ChainSettings->RotationMode = ParseFKRotationMode(RotMode);
			}

			double RotAlpha;
			if (ChainObj->TryGetNumberField(TEXT("rotation_alpha"), RotAlpha))
			{
				ChainSettings->RotationAlpha = RotAlpha;
			}

			FString TransMode;
			if (ChainObj->TryGetStringField(TEXT("translation_mode"), TransMode) && !TransMode.IsEmpty())
			{
				ChainSettings->TranslationMode = ParseFKTranslationMode(TransMode);
			}

			double TransAlpha;
			if (ChainObj->TryGetNumberField(TEXT("translation_alpha"), TransAlpha))
			{
				ChainSettings->TranslationAlpha = TransAlpha;
			}

			OutResults.Add(FString::Printf(TEXT("Configured FK chain '%s': Rot=%s (%.2f), Trans=%s (%.2f), Enabled=%s"),
				*ChainName,
				*FKRotationModeToString(ChainSettings->RotationMode), ChainSettings->RotationAlpha,
				*FKTranslationModeToString(ChainSettings->TranslationMode), ChainSettings->TranslationAlpha,
				ChainSettings->EnableFK ? TEXT("true") : TEXT("false")));
			Configured++;
		}

		FKCtrl->SetSettings(Settings);
	}

	return Configured;
}

// ============================================================================
// CONFIGURE IK CHAINS
// ============================================================================

int32 FEditIKRetargeterTool::ConfigureIKChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults)
{
	if (!ChainsArray || ChainsArray->Num() == 0) return 0;

	int32 Configured = 0;

	// Group by op_name
	TMap<FString, TArray<const TSharedPtr<FJsonObject>*>> OpGroups;
	for (const TSharedPtr<FJsonValue>& ChainValue : *ChainsArray)
	{
		const TSharedPtr<FJsonObject>* ChainObj;
		if (!ChainValue->TryGetObject(ChainObj)) continue;

		FString OpName;
		(*ChainObj)->TryGetStringField(TEXT("op_name"), OpName);
		OpGroups.FindOrAdd(OpName).Add(ChainObj);
	}

	for (const auto& [OpName, ChainConfigs] : OpGroups)
	{
		int32 OpIndex = FindOpIndex(Controller, OpName, FIKRetargetIKChainsOp::StaticStruct());
		if (OpIndex == INDEX_NONE)
		{
			OutResults.Add(OpName.IsEmpty()
				? TEXT("No IK Chains op found in stack. Add one with add_ops or add_default_ops first.")
				: FString::Printf(TEXT("IK op not found: %s"), *OpName));
			continue;
		}

		UIKRetargetOpControllerBase* BaseCtrl = Controller->GetOpController(OpIndex);
		UIKRetargetIKChainsController* IKCtrl = Cast<UIKRetargetIKChainsController>(BaseCtrl);
		if (!IKCtrl)
		{
			OutResults.Add(FString::Printf(TEXT("Op at index %d is not an IK Chains op"), OpIndex));
			continue;
		}

		FIKRetargetIKChainsOpSettings Settings = IKCtrl->GetSettings();

		for (const TSharedPtr<FJsonObject>* ChainObjPtr : ChainConfigs)
		{
			const TSharedPtr<FJsonObject>& ChainObj = *ChainObjPtr;

			FString ChainName;
			if (!ChainObj->TryGetStringField(TEXT("chain"), ChainName) || ChainName.IsEmpty())
			{
				OutResults.Add(TEXT("configure_ik_chains: skipped entry with no 'chain' field"));
				continue;
			}

			FRetargetIKChainSettings* ChainSettings = nullptr;
			for (FRetargetIKChainSettings& CS : Settings.ChainsToRetarget)
			{
				if (CS.TargetChainName == FName(*ChainName))
				{
					ChainSettings = &CS;
					break;
				}
			}

			if (!ChainSettings)
			{
				OutResults.Add(FString::Printf(TEXT("IK chain not found in op: '%s'"), *ChainName));
				continue;
			}

			// Apply settings
			bool bEnableIK;
			if (ChainObj->TryGetBoolField(TEXT("enable_ik"), bEnableIK))
			{
				ChainSettings->EnableIK = bEnableIK;
			}

			double BlendToSource;
			if (ChainObj->TryGetNumberField(TEXT("blend_to_source"), BlendToSource))
			{
				ChainSettings->BlendToSource = BlendToSource;
			}

			double BlendToSourceTrans;
			if (ChainObj->TryGetNumberField(TEXT("blend_to_source_translation"), BlendToSourceTrans))
			{
				ChainSettings->BlendToSourceTranslation = BlendToSourceTrans;
			}

			double BlendToSourceRot;
			if (ChainObj->TryGetNumberField(TEXT("blend_to_source_rotation"), BlendToSourceRot))
			{
				ChainSettings->BlendToSourceRotation = BlendToSourceRot;
			}

			double ScaleVert;
			if (ChainObj->TryGetNumberField(TEXT("scale_vertical"), ScaleVert))
			{
				ChainSettings->ScaleVertical = ScaleVert;
			}

			double Extension;
			if (ChainObj->TryGetNumberField(TEXT("extension"), Extension))
			{
				ChainSettings->Extension = Extension;
			}

			// Vector fields
			const TArray<TSharedPtr<FJsonValue>>* OffsetArray;
			if (ChainObj->TryGetArrayField(TEXT("static_offset"), OffsetArray) && OffsetArray->Num() >= 3)
			{
				ChainSettings->StaticOffset = FVector((*OffsetArray)[0]->AsNumber(), (*OffsetArray)[1]->AsNumber(), (*OffsetArray)[2]->AsNumber());
			}
			if (ChainObj->TryGetArrayField(TEXT("static_local_offset"), OffsetArray) && OffsetArray->Num() >= 3)
			{
				ChainSettings->StaticLocalOffset = FVector((*OffsetArray)[0]->AsNumber(), (*OffsetArray)[1]->AsNumber(), (*OffsetArray)[2]->AsNumber());
			}
			if (ChainObj->TryGetArrayField(TEXT("static_rotation_offset"), OffsetArray) && OffsetArray->Num() >= 3)
			{
				ChainSettings->StaticRotationOffset = FRotator((*OffsetArray)[0]->AsNumber(), (*OffsetArray)[1]->AsNumber(), (*OffsetArray)[2]->AsNumber());
			}

	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			bool bApplyPelvis;
			if (ChainObj->TryGetBoolField(TEXT("apply_pelvis_offset"), bApplyPelvis))
			{
				ChainSettings->ApplyPelvisOffsetToSourceGoals = bApplyPelvis;
			}
#endif

			OutResults.Add(FString::Printf(TEXT("Configured IK chain '%s': EnableIK=%s, BlendToSource=%.2f, Extension=%.2f, ScaleVert=%.2f"),
				*ChainName,
				ChainSettings->EnableIK ? TEXT("true") : TEXT("false"),
				ChainSettings->BlendToSource, ChainSettings->Extension, ChainSettings->ScaleVertical));
			Configured++;
		}

		IKCtrl->SetSettings(Settings);
	}

	return Configured;
}

// ============================================================================
// CONFIGURE PELVIS MOTION OP
// ============================================================================

bool FEditIKRetargeterTool::ConfigurePelvis(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PelvisObj, TArray<FString>& OutResults)
{
	FString OpName;
	PelvisObj->TryGetStringField(TEXT("op_name"), OpName);

	int32 OpIndex = FindOpIndex(Controller, OpName, FIKRetargetPelvisMotionOp::StaticStruct());
	if (OpIndex == INDEX_NONE)
	{
		OutResults.Add(TEXT("No Pelvis Motion op found in stack"));
		return false;
	}

	UIKRetargetOpControllerBase* BaseCtrl = Controller->GetOpController(OpIndex);
	UIKRetargetPelvisMotionController* PelvisCtrl = Cast<UIKRetargetPelvisMotionController>(BaseCtrl);
	if (!PelvisCtrl)
	{
		OutResults.Add(TEXT("Failed to get Pelvis Motion controller"));
		return false;
	}

	// Handle bone references via dedicated methods (FBoneReference needs special handling)
	FString SourcePelvisBone;
	if (PelvisObj->TryGetStringField(TEXT("source_pelvis_bone"), SourcePelvisBone) && !SourcePelvisBone.IsEmpty())
	{
		PelvisCtrl->SetSourcePelvisBone(FName(*SourcePelvisBone));
	}

	FString TargetPelvisBone;
	if (PelvisObj->TryGetStringField(TEXT("target_pelvis_bone"), TargetPelvisBone) && !TargetPelvisBone.IsEmpty())
	{
		PelvisCtrl->SetTargetPelvisBone(FName(*TargetPelvisBone));
	}

	// Get settings, modify non-bone fields, set back
	FIKRetargetPelvisMotionOpSettings Settings = PelvisCtrl->GetSettings();

	double Val;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (PelvisObj->TryGetNumberField(TEXT("floor_constraint_weight"), Val)) Settings.FloorConstraintWeight = Val;
	if (PelvisObj->TryGetNumberField(TEXT("source_crotch_offset"), Val)) Settings.SourceCrotchOffset = Val;
#endif
	if (PelvisObj->TryGetNumberField(TEXT("rotation_alpha"), Val)) Settings.RotationAlpha = Val;
	if (PelvisObj->TryGetNumberField(TEXT("translation_alpha"), Val)) Settings.TranslationAlpha = Val;
	if (PelvisObj->TryGetNumberField(TEXT("blend_to_source_translation"), Val)) Settings.BlendToSourceTranslation = Val;
	if (PelvisObj->TryGetNumberField(TEXT("scale_horizontal"), Val)) Settings.ScaleHorizontal = Val;
	if (PelvisObj->TryGetNumberField(TEXT("scale_vertical"), Val)) Settings.ScaleVertical = Val;
	if (PelvisObj->TryGetNumberField(TEXT("affect_ik_horizontal"), Val)) Settings.AffectIKHorizontal = Val;
	if (PelvisObj->TryGetNumberField(TEXT("affect_ik_vertical"), Val)) Settings.AffectIKVertical = Val;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (PelvisObj->TryGetNumberField(TEXT("target_crotch_offset"), Val)) Settings.TargetCrotchOffset = Val;

	const TArray<TSharedPtr<FJsonValue>>* RotArray;
	if (PelvisObj->TryGetArrayField(TEXT("rotation_offset_local"), RotArray) && RotArray->Num() >= 3)
	{
		Settings.RotationOffsetLocal = FRotator((*RotArray)[0]->AsNumber(), (*RotArray)[1]->AsNumber(), (*RotArray)[2]->AsNumber());
	}
	if (PelvisObj->TryGetArrayField(TEXT("rotation_offset_global"), RotArray) && RotArray->Num() >= 3)
	{
		Settings.RotationOffsetGlobal = FRotator((*RotArray)[0]->AsNumber(), (*RotArray)[1]->AsNumber(), (*RotArray)[2]->AsNumber());
	}

	const TArray<TSharedPtr<FJsonValue>>* TransArray;
	if (PelvisObj->TryGetArrayField(TEXT("translation_offset_local"), TransArray) && TransArray->Num() >= 3)
	{
		Settings.TranslationOffsetLocal = FVector((*TransArray)[0]->AsNumber(), (*TransArray)[1]->AsNumber(), (*TransArray)[2]->AsNumber());
	}
	if (PelvisObj->TryGetArrayField(TEXT("translation_offset_global"), TransArray) && TransArray->Num() >= 3)
	{
		Settings.TranslationOffsetGlobal = FVector((*TransArray)[0]->AsNumber(), (*TransArray)[1]->AsNumber(), (*TransArray)[2]->AsNumber());
	}
#endif

	PelvisCtrl->SetSettings(Settings);

	OutResults.Add(FString::Printf(TEXT("Configured Pelvis Motion op: RotAlpha=%.2f, TransAlpha=%.2f, ScaleH=%.2f, ScaleV=%.2f"),
		Settings.RotationAlpha, Settings.TranslationAlpha, Settings.ScaleHorizontal, Settings.ScaleVertical));
	return true;
}

// ============================================================================
// CONFIGURE ROOT MOTION OP
// ============================================================================

bool FEditIKRetargeterTool::ConfigureRootMotion(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& RootMotionObj, TArray<FString>& OutResults)
{
	FString OpName;
	RootMotionObj->TryGetStringField(TEXT("op_name"), OpName);

	int32 OpIndex = FindOpIndex(Controller, OpName, FIKRetargetRootMotionOp::StaticStruct());
	if (OpIndex == INDEX_NONE)
	{
		OutResults.Add(TEXT("No Root Motion op found in stack"));
		return false;
	}

	UIKRetargetOpControllerBase* BaseCtrl = Controller->GetOpController(OpIndex);
	UIKRetargetRootMotionController* RootCtrl = Cast<UIKRetargetRootMotionController>(BaseCtrl);
	if (!RootCtrl)
	{
		OutResults.Add(TEXT("Failed to get Root Motion controller"));
		return false;
	}

	// Handle bone references via dedicated methods
	FString BoneName;
	if (RootMotionObj->TryGetStringField(TEXT("source_root_bone"), BoneName) && !BoneName.IsEmpty())
	{
		RootCtrl->SetSourceRootBone(FName(*BoneName));
	}
	if (RootMotionObj->TryGetStringField(TEXT("target_root_bone"), BoneName) && !BoneName.IsEmpty())
	{
		RootCtrl->SetTargetRootBone(FName(*BoneName));
	}
	if (RootMotionObj->TryGetStringField(TEXT("target_pelvis_bone"), BoneName) && !BoneName.IsEmpty())
	{
		RootCtrl->SetTargetPelvisBone(FName(*BoneName));
	}

	// Get settings, modify non-bone fields
	FIKRetargetRootMotionOpSettings Settings = RootCtrl->GetSettings();

	FString MotionSource;
	if (RootMotionObj->TryGetStringField(TEXT("root_motion_source"), MotionSource) && !MotionSource.IsEmpty())
	{
		if (MotionSource.Equals(TEXT("CopyFromSourceRoot"), ESearchCase::IgnoreCase))
			Settings.RootMotionSource = ERootMotionSource::CopyFromSourceRoot;
		else if (MotionSource.Equals(TEXT("GenerateFromTargetPelvis"), ESearchCase::IgnoreCase))
			Settings.RootMotionSource = ERootMotionSource::GenerateFromTargetPelvis;
		else
			OutResults.Add(FString::Printf(TEXT("Unknown root_motion_source: '%s' (use CopyFromSourceRoot or GenerateFromTargetPelvis)"), *MotionSource));
	}

	FString HeightSource;
	if (RootMotionObj->TryGetStringField(TEXT("root_height_source"), HeightSource) && !HeightSource.IsEmpty())
	{
		if (HeightSource.Equals(TEXT("CopyHeightFromSource"), ESearchCase::IgnoreCase))
			Settings.RootHeightSource = ERootMotionHeightSource::CopyHeightFromSource;
		else if (HeightSource.Equals(TEXT("SnapToGround"), ESearchCase::IgnoreCase))
			Settings.RootHeightSource = ERootMotionHeightSource::SnapToGround;
		else
			OutResults.Add(FString::Printf(TEXT("Unknown root_height_source: '%s' (use CopyHeightFromSource or SnapToGround)"), *HeightSource));
	}

	bool bVal;
	if (RootMotionObj->TryGetBoolField(TEXT("rotate_with_pelvis"), bVal)) Settings.bRotateWithPelvis = bVal;
	if (RootMotionObj->TryGetBoolField(TEXT("maintain_offset_from_pelvis"), bVal)) Settings.bMaintainOffsetFromPelvis = bVal;
	if (RootMotionObj->TryGetBoolField(TEXT("propagate_to_children"), bVal)) Settings.bPropagateToNonRetargetedChildren = bVal;

	// Global offset
	const TSharedPtr<FJsonObject>* OffsetObj;
	if (RootMotionObj->TryGetObjectField(TEXT("global_offset"), OffsetObj))
	{
		const TArray<TSharedPtr<FJsonValue>>* LocArray;
		if ((*OffsetObj)->TryGetArrayField(TEXT("location"), LocArray) && LocArray->Num() >= 3)
		{
			Settings.GlobalOffset.SetLocation(FVector((*LocArray)[0]->AsNumber(), (*LocArray)[1]->AsNumber(), (*LocArray)[2]->AsNumber()));
		}
		const TArray<TSharedPtr<FJsonValue>>* RotArray;
		if ((*OffsetObj)->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray->Num() >= 3)
		{
			Settings.GlobalOffset.SetRotation(FRotator((*RotArray)[0]->AsNumber(), (*RotArray)[1]->AsNumber(), (*RotArray)[2]->AsNumber()).Quaternion());
		}
	}

	RootCtrl->SetSettings(Settings);

	OutResults.Add(FString::Printf(TEXT("Configured Root Motion op: Source=%s, HeightSource=%s"),
		Settings.RootMotionSource == ERootMotionSource::CopyFromSourceRoot ? TEXT("CopyFromSourceRoot") : TEXT("GenerateFromTargetPelvis"),
		Settings.RootHeightSource == ERootMotionHeightSource::CopyHeightFromSource ? TEXT("CopyHeightFromSource") : TEXT("SnapToGround")));
	return true;
}

// ============================================================================
// MOVE OP
// ============================================================================

bool FEditIKRetargeterTool::MoveOp(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& MoveObj, TArray<FString>& OutResults)
{
	FString OpName;
	if (!MoveObj->TryGetStringField(TEXT("name"), OpName) || OpName.IsEmpty())
	{
		OutResults.Add(TEXT("move_op requires 'name' field"));
		return false;
	}

	double ToIndexD;
	if (!MoveObj->TryGetNumberField(TEXT("to_index"), ToIndexD))
	{
		OutResults.Add(TEXT("move_op requires 'to_index' field"));
		return false;
	}
	int32 ToIndex = static_cast<int32>(ToIndexD);

	int32 FromIndex = Controller->GetIndexOfOpByName(FName(*OpName));
	if (FromIndex < 0)
	{
		OutResults.Add(FString::Printf(TEXT("Op not found: %s"), *OpName));
		return false;
	}

	if (ToIndex < 0 || ToIndex >= Controller->GetNumRetargetOps())
	{
		OutResults.Add(FString::Printf(TEXT("Invalid to_index: %d (stack has %d ops)"), ToIndex, Controller->GetNumRetargetOps()));
		return false;
	}

	if (Controller->MoveRetargetOpInStack(FromIndex, ToIndex))
	{
		OutResults.Add(FString::Printf(TEXT("Moved op '%s' from index %d to %d"), *OpName, FromIndex, ToIndex));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Failed to move op '%s' (may violate parent-child constraints)"), *OpName));
		return false;
	}
}

// ============================================================================
// SET PREVIEW MESH
// ============================================================================

bool FEditIKRetargeterTool::SetPreviewMeshOp(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& MeshObj, TArray<FString>& OutResults)
{
	FString MeshPath;
	if (!MeshObj->TryGetStringField(TEXT("mesh"), MeshPath) || MeshPath.IsEmpty())
	{
		OutResults.Add(TEXT("set_preview_mesh requires 'mesh' field"));
		return false;
	}

	FString ForStr;
	MeshObj->TryGetStringField(TEXT("for"), ForStr);
	bool bIsSource = ForStr.Equals(TEXT("source"), ESearchCase::IgnoreCase);
	ERetargetSourceOrTarget SOT = bIsSource ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	if (!MeshPath.StartsWith(TEXT("/")))
	{
		MeshPath = TEXT("/Game/") + MeshPath;
	}

	USkeletalMesh* Mesh = NeoStackToolUtils::LoadAssetWithFallback<USkeletalMesh>(MeshPath);
	if (!Mesh)
	{
		OutResults.Add(FString::Printf(TEXT("Failed to load skeletal mesh: %s"), *MeshPath));
		return false;
	}

	Controller->SetPreviewMesh(SOT, Mesh);
	OutResults.Add(FString::Printf(TEXT("Set %s preview mesh: %s"), bIsSource ? TEXT("source") : TEXT("target"), *Mesh->GetName()));
	return true;
}

// ============================================================================
// ADDITIONAL POSE OPERATIONS
// ============================================================================

bool FEditIKRetargeterTool::DuplicatePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults)
{
	FString Name, NewName, ForStr;
	if (!PoseObj->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		OutResults.Add(TEXT("duplicate_pose requires 'name' field"));
		return false;
	}
	if (!PoseObj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		OutResults.Add(TEXT("duplicate_pose requires 'new_name' field"));
		return false;
	}
	PoseObj->TryGetStringField(TEXT("for"), ForStr);
	ERetargetSourceOrTarget SOT = ForStr.Equals(TEXT("source"), ESearchCase::IgnoreCase) ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	FName Result = Controller->DuplicateRetargetPose(FName(*Name), FName(*NewName), SOT);
	if (!Result.IsNone())
	{
		OutResults.Add(FString::Printf(TEXT("Duplicated %s pose '%s' -> '%s'"),
			SOT == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"), *Name, *Result.ToString()));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Failed to duplicate pose: %s"), *Name));
		return false;
	}
}

bool FEditIKRetargeterTool::RenamePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults)
{
	FString OldName, NewName, ForStr;
	if (!PoseObj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		OutResults.Add(TEXT("rename_pose requires 'old_name' field"));
		return false;
	}
	if (!PoseObj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		OutResults.Add(TEXT("rename_pose requires 'new_name' field"));
		return false;
	}
	PoseObj->TryGetStringField(TEXT("for"), ForStr);
	ERetargetSourceOrTarget SOT = ForStr.Equals(TEXT("source"), ESearchCase::IgnoreCase) ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	if (Controller->RenameRetargetPose(FName(*OldName), FName(*NewName), SOT))
	{
		OutResults.Add(FString::Printf(TEXT("Renamed %s pose: '%s' -> '%s'"),
			SOT == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"), *OldName, *NewName));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("Failed to rename pose: %s"), *OldName));
		return false;
	}
}

bool FEditIKRetargeterTool::ResetPoseBones(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& ResetObj, TArray<FString>& OutResults)
{
	FString PoseName, ForStr;
	if (!ResetObj->TryGetStringField(TEXT("pose_name"), PoseName) || PoseName.IsEmpty())
	{
		OutResults.Add(TEXT("reset_pose_bones requires 'pose_name' field"));
		return false;
	}
	ResetObj->TryGetStringField(TEXT("for"), ForStr);
	ERetargetSourceOrTarget SOT = ForStr.Equals(TEXT("source"), ESearchCase::IgnoreCase) ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	TArray<FName> BonesToReset;
	const TArray<TSharedPtr<FJsonValue>>* BonesArray;
	if (ResetObj->TryGetArrayField(TEXT("bones"), BonesArray))
	{
		for (const TSharedPtr<FJsonValue>& BoneValue : *BonesArray)
		{
			FString BoneName = BoneValue->AsString();
			if (!BoneName.IsEmpty())
			{
				BonesToReset.Add(FName(*BoneName));
			}
		}
	}

	Controller->ResetRetargetPose(FName(*PoseName), BonesToReset, SOT);
	OutResults.Add(FString::Printf(TEXT("Reset %s pose '%s': %s"),
		SOT == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"),
		*PoseName,
		BonesToReset.Num() > 0 ? *FString::Printf(TEXT("%d bones"), BonesToReset.Num()) : TEXT("all bones")));
	return true;
}

bool FEditIKRetargeterTool::SnapToGroundOp(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& SnapObj, TArray<FString>& OutResults)
{
	FString BoneName, ForStr;
	if (!SnapObj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
	{
		OutResults.Add(TEXT("snap_to_ground requires 'bone' field"));
		return false;
	}
	SnapObj->TryGetStringField(TEXT("for"), ForStr);
	ERetargetSourceOrTarget SOT = ForStr.Equals(TEXT("source"), ESearchCase::IgnoreCase) ? ERetargetSourceOrTarget::Source : ERetargetSourceOrTarget::Target;

	Controller->SnapBoneToGround(FName(*BoneName), SOT);
	OutResults.Add(FString::Printf(TEXT("Snapped %s skeleton to ground using bone: %s"),
		SOT == ERetargetSourceOrTarget::Source ? TEXT("source") : TEXT("target"), *BoneName));
	return true;
}

// ============================================================================
// ============================================================================
// ASSET MANAGEMENT
// ============================================================================

UIKRetargeter* FEditIKRetargeterTool::CreateRetargeterAsset(const FString& AssetName, const FString& AssetPath)
{
	FString SanitizedName;
	UPackage* Package = NeoStackToolUtils::CreateAssetPackage(AssetName, AssetPath, SanitizedName);
	if (!Package)
	{
		return nullptr;
	}

	UIKRetargetFactory* Factory = NewObject<UIKRetargetFactory>();
	UIKRetargeter* NewRetargeter = Cast<UIKRetargeter>(Factory->FactoryCreateNew(
		UIKRetargeter::StaticClass(),
		Package,
		FName(*SanitizedName),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn));

	if (NewRetargeter)
	{
		FAssetRegistryModule::AssetCreated(NewRetargeter);
		Package->MarkPackageDirty();
	}

	return NewRetargeter;
}

UIKRetargeter* FEditIKRetargeterTool::GetOrLoadRetargeter(const FString& Name, const FString& Path)
{
	FString FullPath;
	if (Name.StartsWith(TEXT("/")))
	{
		FullPath = Name;
	}
	else
	{
		FString BasePath = Path.IsEmpty() ? TEXT("/Game") : Path;
		if (!BasePath.StartsWith(TEXT("/")))
		{
			BasePath = TEXT("/Game/") + BasePath;
		}
		FullPath = BasePath / Name;
	}

	return NeoStackToolUtils::LoadAssetWithFallback<UIKRetargeter>(FullPath);
}

UIKRigDefinition* FEditIKRetargeterTool::LoadIKRig(const FString& Path)
{
	if (Path.IsEmpty())
	{
		return nullptr;
	}

	FString FullPath = Path;
	if (!FullPath.StartsWith(TEXT("/")))
	{
		FullPath = TEXT("/Game/") + FullPath;
	}

	return NeoStackToolUtils::LoadAssetWithFallback<UIKRigDefinition>(FullPath);
}

#else // UE 5.5 - IK Retargeter struct-based API not available

TSharedPtr<FJsonObject> FEditIKRetargeterTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("IK Retargeter asset name"));
	Properties->SetObjectField(TEXT("name"), NameProp);
	Schema->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);
	return Schema;
}

FToolResult FEditIKRetargeterTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	return FToolResult::Fail(TEXT("edit_ikretargeter tool requires Unreal Engine 5.6 or later. The IK Retargeter struct-based API is not available in UE 5.5."));
}

#endif // ENGINE_MINOR_VERSION >= 6
