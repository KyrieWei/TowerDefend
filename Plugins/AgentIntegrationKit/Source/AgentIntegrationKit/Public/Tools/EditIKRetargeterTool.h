// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
class UIKRetargeter;
class UIKRetargeterController;
class UIKRigDefinition;
class USkeletalMesh;
#endif

/**
 * Tool for creating and editing IK Retargeter assets
 *
 * IK Retargeters enable animation retargeting between different skeletons by:
 * - Linking a source IK Rig to a target IK Rig
 * - Mapping retarget chains between source and target
 * - Configuring FK/IK chain settings for each mapping
 * - Managing retarget poses for alignment
 *
 * Parameters:
 *   - name: IK Retargeter asset name (required)
 *   - path: Asset folder path (optional, defaults to /Game)
 *   - source_ikrig: Path to source IK Rig asset (required for new retargeters)
 *   - target_ikrig: Path to target IK Rig asset (required for new retargeters)
 *
 * Op Stack:
 *   - add_default_ops: Boolean - add standard op stack (Pelvis, FK, IK, RunIK, RootMotion)
 *   - add_ops: Array of op definitions [{type, name, enabled}]
 *       Types: "PelvisMotion", "FKChains", "IKChains", "RunIKRig", "RootMotion",
 *              "CurveRemap", "StrideWarping", "FloorConstraint", "PinBone", "FilterBone"
 *   - remove_ops: Array of op names to remove
 *   - enable_ops: Array of op names to enable
 *   - disable_ops: Array of op names to disable
 *
 * Chain Mapping:
 *   - auto_map_chains: String - "exact" (same name), "fuzzy" (closest match), or "clear"
 *   - map_chains: Array of manual mappings [{target_chain, source_chain}]
 *   - unmap_chains: Array of target chain names to unmap
 *
 * Retarget Poses:
 *   - create_pose: Create a pose {name, for: "source"/"target"}
 *   - delete_pose: Delete a pose {name, for: "source"/"target"}
 *   - set_current_pose: Set active pose {name, for: "source"/"target"}
 *   - auto_align: Boolean - auto-align all bones between source and target poses
 *
 * Chain Settings (per-chain configuration):
 *   - configure_fk_chain: Configure FK settings {chain, rotation_mode, rotation_alpha, translation_mode, translation_alpha}
 *   - configure_ik_chain: Configure IK settings {chain, enabled, blend_to_source, extension, scale_vertical}
 *
 * Pose Bone Editing:
 *   - edit_pose_bones: Array of bone rotation offsets [{bone, rotation: [pitch, yaw, roll], for: "source"/"target"}]
 *   - set_root_offset: Set root translation offset {offset: [x, y, z], for: "source"/"target"}
 *
 * Animation Retargeting:
 *   - retarget_animations: Batch retarget animations {animations: [...paths...], source_mesh, target_mesh, prefix, suffix, search, replace, include_referenced, overwrite}
 */
class AGENTINTEGRATIONKIT_API FEditIKRetargeterTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_ikretargeter"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create and edit IK Retargeter assets for animation retargeting between skeletons. "
			"Set source_ikrig and target_ikrig to link two IK Rigs. "
			"Use add_default_ops=true to create the standard retargeting op stack. "
			"Use auto_map_chains=\"fuzzy\" to automatically map chains by name similarity. "
			"Use configure_fk_chains to set per-chain FK settings (rotation_mode, translation_mode, alphas) — "
			"CRITICAL for finger retargeting: set rotation_mode='OneToOne' and translation_mode='StretchBoneLengthUniformly' on finger chains. "
			"Use configure_ik_chains for per-chain IK goal settings (blend, extension, offsets). "
			"Use configure_pelvis and configure_root_motion for pelvis and root motion op settings. "
			"Use list_ops=true and list_chain_mappings=true to inspect current configuration. "
			"Use retarget_animations to batch retarget animation sequences to the target skeleton.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Chain mapping from JSON */
	struct FChainMapOp
	{
		FString TargetChain;
		FString SourceChain;
	};

	/** Op definition from JSON */
	struct FOpDef
	{
		FString Type;
		FString Name;
		bool bEnabled = true;
	};

	/** Pose operation from JSON */
	struct FPoseOp
	{
		FString Name;
		bool bIsSource = false; // false = target
	};

	/** FK chain settings from JSON */
	struct FFKChainSettings
	{
		FString Chain;
		FString RotationMode;
		float RotationAlpha = 1.0f;
		FString TranslationMode;
		float TranslationAlpha = 1.0f;
		bool bEnableFK = true;
	};

	/** IK chain settings from JSON */
	struct FIKChainSettings
	{
		FString Chain;
		bool bEnableIK = true;
		float BlendToSource = 0.0f;
		float Extension = 1.0f;
		float ScaleVertical = 1.0f;
	};

	/** Bone pose edit from JSON */
	struct FBonePoseEdit
	{
		FString BoneName;
		FRotator Rotation;
		bool bIsSource = false;
	};

	/** Root offset from JSON */
	struct FRootOffset
	{
		FVector Offset;
		bool bIsSource = false;
	};

	/** Retarget animations params from JSON */
	struct FRetargetAnimParams
	{
		TArray<FString> AnimationPaths;
		FString SourceMeshPath;
		FString TargetMeshPath;
		FString Prefix;
		FString Suffix;
		FString Search;
		FString Replace;
		bool bIncludeReferenced = true;
		bool bOverwrite = false;
	};

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	// IK Rig assignment
	bool SetSourceIKRig(UIKRetargeterController* Controller, const FString& IKRigPath, TArray<FString>& OutResults);
	bool SetTargetIKRig(UIKRetargeterController* Controller, const FString& IKRigPath, TArray<FString>& OutResults);

	// Op stack operations
	bool AddDefaultOps(UIKRetargeterController* Controller, TArray<FString>& OutResults);
	int32 AddOps(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* OpsArray, TArray<FString>& OutResults);
	int32 RemoveOps(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* OpsArray, TArray<FString>& OutResults);
	int32 EnableOps(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* OpsArray, bool bEnable, TArray<FString>& OutResults);

	// Chain mapping operations
	bool AutoMapChains(UIKRetargeterController* Controller, const FString& MapType, TArray<FString>& OutResults);
	int32 MapChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* MappingsArray, TArray<FString>& OutResults);
	int32 UnmapChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults);

	// Pose operations
	bool CreatePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults);
	bool DeletePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults);
	bool SetCurrentPose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults);
	bool AutoAlignBones(UIKRetargeterController* Controller, TArray<FString>& OutResults);

	// Pose bone editing
	int32 EditPoseBones(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* BonesArray, TArray<FString>& OutResults);
	bool SetRootOffset(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& OffsetObj, TArray<FString>& OutResults);

	// Animation retargeting
	bool RetargetAnimations(UIKRetargeter* Retargeter, const TSharedPtr<FJsonObject>& RetargetObj, TArray<FString>& OutResults);

	// Per-op configuration
	int32 ConfigureFKChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults);
	int32 ConfigureIKChains(UIKRetargeterController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults);
	bool ConfigurePelvis(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PelvisObj, TArray<FString>& OutResults);
	bool ConfigureRootMotion(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& RootMotionObj, TArray<FString>& OutResults);

	// Op stack
	bool MoveOp(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& MoveObj, TArray<FString>& OutResults);

	// Preview mesh
	bool SetPreviewMeshOp(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& MeshObj, TArray<FString>& OutResults);

	// Additional pose operations
	bool DuplicatePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults);
	bool RenamePose(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& PoseObj, TArray<FString>& OutResults);
	bool ResetPoseBones(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& ResetObj, TArray<FString>& OutResults);
	bool SnapToGroundOp(UIKRetargeterController* Controller, const TSharedPtr<FJsonObject>& SnapObj, TArray<FString>& OutResults);

	// Parsing helpers
	FChainMapOp ParseChainMapOp(const TSharedPtr<FJsonObject>& MapObj);
	FOpDef ParseOpDef(const TSharedPtr<FJsonObject>& OpObj);
	FPoseOp ParsePoseOp(const TSharedPtr<FJsonObject>& PoseObj);
	FBonePoseEdit ParseBonePoseEdit(const TSharedPtr<FJsonObject>& BoneObj);
	FRootOffset ParseRootOffset(const TSharedPtr<FJsonObject>& OffsetObj);
	FRetargetAnimParams ParseRetargetAnimParams(const TSharedPtr<FJsonObject>& RetargetObj);

	/** Get op type path from type name */
	FString GetOpTypePath(const FString& TypeName);

	/** Parse source_or_target string to enum */
	bool ParseSourceOrTarget(const FString& Value, bool& bOutIsSource);

	/** Create a new IK Retargeter asset */
	UIKRetargeter* CreateRetargeterAsset(const FString& AssetName, const FString& AssetPath);

	/** Get or load an IK Retargeter asset */
	UIKRetargeter* GetOrLoadRetargeter(const FString& Name, const FString& Path);

	/** Load an IK Rig asset by path */
	UIKRigDefinition* LoadIKRig(const FString& Path);

	/** Find first op of a given struct type, or by name. Returns INDEX_NONE if not found. */
	int32 FindOpIndex(UIKRetargeterController* Controller, const FString& OpName, const UScriptStruct* ExpectedType);
#endif
};
