// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditAnimSequenceTool.h"
#include "Tools/NeoStackToolBase.h"
#include "Tools/NeoStackToolUtils.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Curves/RichCurve.h"
#include "Animation/AnimBoneCompressionSettings.h"
#include "Animation/AnimCurveCompressionSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"
#include "Misc/FrameRate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Animation/Skeleton.h"

// ============================================================================
// Schema
// ============================================================================

TSharedPtr<FJsonObject> FEditAnimSequenceTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Required: asset name
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("AnimSequence asset name or full path (required). For new assets, this is the name to create."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (defaults to /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// Create new AnimSequence
	TSharedPtr<FJsonObject> CreateProp = MakeShared<FJsonObject>();
	CreateProp->SetStringField(TEXT("type"), TEXT("object"));
	CreateProp->SetStringField(TEXT("description"),
		TEXT("Create a new blank AnimSequence instead of loading an existing one. "
			"{skeleton (path to USkeleton asset, required), frame_rate? (default 30), num_frames? (default 1)}"));
	Properties->SetObjectField(TEXT("create"), CreateProp);

	// Notify operations
	TSharedPtr<FJsonObject> AddNotifiesProp = MakeShared<FJsonObject>();
	AddNotifiesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddNotifiesProp->SetStringField(TEXT("description"),
		TEXT("Add animation notifies. Each: {name, time, type? (class name like 'PlaySound' or 'AnimNotify_PlaySound'), "
			"duration? (for state notifies), track_index? (default 0), trigger_chance? (0-1), branching_point? (bool)}"));
	Properties->SetObjectField(TEXT("add_notifies"), AddNotifiesProp);

	TSharedPtr<FJsonObject> RemoveNotifiesProp = MakeShared<FJsonObject>();
	RemoveNotifiesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveNotifiesProp->SetStringField(TEXT("description"),
		TEXT("Remove notifies by name (string) or index (int). Indices processed in reverse order."));
	Properties->SetObjectField(TEXT("remove_notifies"), RemoveNotifiesProp);

	TSharedPtr<FJsonObject> EditNotifiesProp = MakeShared<FJsonObject>();
	EditNotifiesProp->SetStringField(TEXT("type"), TEXT("array"));
	EditNotifiesProp->SetStringField(TEXT("description"),
		TEXT("Edit existing notifies. Each: {index, time?, duration?, track_index?, trigger_chance?}"));
	Properties->SetObjectField(TEXT("edit_notifies"), EditNotifiesProp);

	// Sync markers
	TSharedPtr<FJsonObject> AddMarkersProp = MakeShared<FJsonObject>();
	AddMarkersProp->SetStringField(TEXT("type"), TEXT("array"));
	AddMarkersProp->SetStringField(TEXT("description"),
		TEXT("Add sync markers. Each: {name, time, track_index? (default 0)}"));
	Properties->SetObjectField(TEXT("add_sync_markers"), AddMarkersProp);

	TSharedPtr<FJsonObject> RemoveMarkersProp = MakeShared<FJsonObject>();
	RemoveMarkersProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveMarkersProp->SetStringField(TEXT("description"),
		TEXT("Remove sync markers by name (string) or index (int)"));
	Properties->SetObjectField(TEXT("remove_sync_markers"), RemoveMarkersProp);

	TSharedPtr<FJsonObject> RenameMarkerProp = MakeShared<FJsonObject>();
	RenameMarkerProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameMarkerProp->SetStringField(TEXT("description"),
		TEXT("Rename a sync marker: {old_name, new_name}"));
	Properties->SetObjectField(TEXT("rename_sync_marker"), RenameMarkerProp);

	// Curve operations
	TSharedPtr<FJsonObject> AddCurvesProp = MakeShared<FJsonObject>();
	AddCurvesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddCurvesProp->SetStringField(TEXT("description"),
		TEXT("Add curves. Each: {name, type: 'float'|'transform' (default 'float'), "
			"keys?: [{time, value, interp_mode?, tangent_mode?, arrive_tangent?, leave_tangent?}]}"));
	Properties->SetObjectField(TEXT("add_curves"), AddCurvesProp);

	TSharedPtr<FJsonObject> RemoveCurvesProp = MakeShared<FJsonObject>();
	RemoveCurvesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveCurvesProp->SetStringField(TEXT("description"),
		TEXT("Remove curves by name. Each element: string name, or {name, type?: 'float'|'transform'}"));
	Properties->SetObjectField(TEXT("remove_curves"), RemoveCurvesProp);

	TSharedPtr<FJsonObject> SetCurveKeysProp = MakeShared<FJsonObject>();
	SetCurveKeysProp->SetStringField(TEXT("type"), TEXT("array"));
	SetCurveKeysProp->SetStringField(TEXT("description"),
		TEXT("Set float curve keyframes (replaces all keys). Each: {curve (name), type?: 'float', "
			"keys: [{time, value, interp_mode?: 'linear'|'constant'|'cubic', "
			"tangent_mode?: 'auto'|'user'|'break'|'smart_auto', arrive_tangent?, leave_tangent?}]}"));
	Properties->SetObjectField(TEXT("set_curve_keys"), SetCurveKeysProp);

	TSharedPtr<FJsonObject> SetTransformKeysProp = MakeShared<FJsonObject>();
	SetTransformKeysProp->SetStringField(TEXT("type"), TEXT("array"));
	SetTransformKeysProp->SetStringField(TEXT("description"),
		TEXT("Set transform curve keyframes. Each: {curve (name), "
			"keys: [{time, location?: [x,y,z], rotation?: [pitch,yaw,roll], scale?: [x,y,z]}]}"));
	Properties->SetObjectField(TEXT("set_transform_curve_keys"), SetTransformKeysProp);

	TSharedPtr<FJsonObject> RenameCurveProp = MakeShared<FJsonObject>();
	RenameCurveProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameCurveProp->SetStringField(TEXT("description"),
		TEXT("Rename a curve: {old_name, new_name, type?: 'float'|'transform'}"));
	Properties->SetObjectField(TEXT("rename_curve"), RenameCurveProp);

	TSharedPtr<FJsonObject> ScaleCurveProp = MakeShared<FJsonObject>();
	ScaleCurveProp->SetStringField(TEXT("type"), TEXT("object"));
	ScaleCurveProp->SetStringField(TEXT("description"),
		TEXT("Scale a curve: {name, origin (float), factor (float), type?: 'float'|'transform'}"));
	Properties->SetObjectField(TEXT("scale_curve"), ScaleCurveProp);

	TSharedPtr<FJsonObject> SetCurveColorProp = MakeShared<FJsonObject>();
	SetCurveColorProp->SetStringField(TEXT("type"), TEXT("object"));
	SetCurveColorProp->SetStringField(TEXT("description"),
		TEXT("Set curve editor color: {name, color: [r,g,b,a] (0-1 floats), type?: 'float'|'transform'}"));
	Properties->SetObjectField(TEXT("set_curve_color"), SetCurveColorProp);

	// Frame operations
	TSharedPtr<FJsonObject> SetFrameRateProp = MakeShared<FJsonObject>();
	SetFrameRateProp->SetStringField(TEXT("type"), TEXT("object"));
	SetFrameRateProp->SetStringField(TEXT("description"),
		TEXT("Set frame rate: {fps: 30} or {numerator: 30, denominator: 1}"));
	Properties->SetObjectField(TEXT("set_frame_rate"), SetFrameRateProp);

	TSharedPtr<FJsonObject> SetFramesProp = MakeShared<FJsonObject>();
	SetFramesProp->SetStringField(TEXT("type"), TEXT("number"));
	SetFramesProp->SetStringField(TEXT("description"),
		TEXT("Set total number of frames (int). Adjusts animation length."));
	Properties->SetObjectField(TEXT("set_number_of_frames"), SetFramesProp);

	TSharedPtr<FJsonObject> ResizeProp = MakeShared<FJsonObject>();
	ResizeProp->SetStringField(TEXT("type"), TEXT("object"));
	ResizeProp->SetStringField(TEXT("description"),
		TEXT("Resize with range: {new_frame_count, t0 (start frame), t1 (end frame)}"));
	Properties->SetObjectField(TEXT("resize"), ResizeProp);

	// Bone tracks
	TSharedPtr<FJsonObject> AddBoneTracksProp = MakeShared<FJsonObject>();
	AddBoneTracksProp->SetStringField(TEXT("type"), TEXT("array"));
	AddBoneTracksProp->SetStringField(TEXT("description"),
		TEXT("Add bone animation tracks: [\"bone1\", \"bone2\"]. Must match skeleton bone names. "
			"Tracks must be added before setting keyframes."));
	Properties->SetObjectField(TEXT("add_bone_tracks"), AddBoneTracksProp);

	TSharedPtr<FJsonObject> RemoveBonesProp = MakeShared<FJsonObject>();
	RemoveBonesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveBonesProp->SetStringField(TEXT("description"),
		TEXT("Remove bone animation tracks by name: [\"bone1\", \"bone2\"]"));
	Properties->SetObjectField(TEXT("remove_bone_tracks"), RemoveBonesProp);

	TSharedPtr<FJsonObject> SetBoneKeysProp = MakeShared<FJsonObject>();
	SetBoneKeysProp->SetStringField(TEXT("type"), TEXT("array"));
	SetBoneKeysProp->SetStringField(TEXT("description"),
		TEXT("Set all keyframes for bone tracks (replaces existing keys). Each: "
			"{bone, positions: [[x,y,z],...], rotations: [[pitch,yaw,roll],...] (3=euler) or "
			"[[x,y,z,w],...] (4=quaternion), scales?: [[x,y,z],...] (defaults to [1,1,1])}. "
			"All arrays must have equal length matching the animation's frame count."));
	Properties->SetObjectField(TEXT("set_bone_track_keys"), SetBoneKeysProp);

	TSharedPtr<FJsonObject> UpdateBoneKeysProp = MakeShared<FJsonObject>();
	UpdateBoneKeysProp->SetStringField(TEXT("type"), TEXT("array"));
	UpdateBoneKeysProp->SetStringField(TEXT("description"),
		TEXT("Update a range of bone track keyframes. Each: "
			"{bone, start_frame, positions: [[x,y,z],...], rotations: [[p,y,r],...] or [[x,y,z,w],...], "
			"scales?: [[x,y,z],...]}. Array lengths = number of frames to update."));
	Properties->SetObjectField(TEXT("update_bone_track_keys"), UpdateBoneKeysProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ============================================================================
// Helpers
// ============================================================================

UClass* FEditAnimSequenceTool::FindAnimNotifyClass(const FString& TypeName)
{
	if (TypeName.IsEmpty()) return nullptr;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UAnimNotify::StaticClass()) && !Class->IsChildOf(UAnimNotifyState::StaticClass()))
		{
			continue;
		}
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;

		FString ClassName = Class->GetName();
		if (ClassName.Equals(TypeName, ESearchCase::IgnoreCase) ||
			ClassName.Equals(TEXT("AnimNotify_") + TypeName, ESearchCase::IgnoreCase) ||
			ClassName.Equals(TEXT("AnimNotifyState_") + TypeName, ESearchCase::IgnoreCase))
		{
			return Class;
		}
	}
	return nullptr;
}

FAnimationCurveIdentifier FEditAnimSequenceTool::MakeCurveId(const FString& Name, const FString& Type) const
{
	ERawCurveTrackTypes CurveType = ERawCurveTrackTypes::RCT_Float;
	if (Type.Equals(TEXT("transform"), ESearchCase::IgnoreCase))
	{
		CurveType = ERawCurveTrackTypes::RCT_Transform;
	}
	return FAnimationCurveIdentifier(FName(*Name), CurveType);
}

static ERichCurveInterpMode ParseInterpMode(const FString& Mode)
{
	if (Mode.Equals(TEXT("constant"), ESearchCase::IgnoreCase)) return RCIM_Constant;
	if (Mode.Equals(TEXT("cubic"), ESearchCase::IgnoreCase)) return RCIM_Cubic;
	return RCIM_Linear; // default
}

static ERichCurveTangentMode ParseTangentMode(const FString& Mode)
{
	if (Mode.Equals(TEXT("user"), ESearchCase::IgnoreCase)) return RCTM_User;
	if (Mode.Equals(TEXT("break"), ESearchCase::IgnoreCase)) return RCTM_Break;
	if (Mode.Equals(TEXT("smart_auto"), ESearchCase::IgnoreCase)) return RCTM_SmartAuto;
	return RCTM_Auto; // default
}

// ============================================================================
// Execute
// ============================================================================

FToolResult FEditAnimSequenceTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// --- 1. Extract required parameters ---
	FString Name;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);

	// --- 2. Create or Load AnimSequence ---
	UAnimSequence* Seq = nullptr;
	TArray<FString> Results;

	const TSharedPtr<FJsonObject>* CreateObj;
	if (Args->TryGetObjectField(TEXT("create"), CreateObj))
	{
		Seq = CreateAnimSequence(Name, Path, *CreateObj, Results);
		if (!Seq)
		{
			return FToolResult::Fail(FString::Join(Results, TEXT("\n")));
		}
	}
	else
	{
		FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
		Seq = LoadObject<UAnimSequence>(nullptr, *FullAssetPath);
		if (!Seq)
		{
			Seq = NeoStackToolUtils::LoadAssetWithFallback<UAnimSequence>(FullAssetPath);
		}
		if (!Seq)
		{
			return FToolResult::Fail(FString::Printf(TEXT("AnimSequence not found: %s"), *FullAssetPath));
		}
	}

	// --- 3. Begin transaction ---
	Results.Add(FString::Printf(TEXT("Editing AnimSequence: %s"), *Seq->GetPathName()));

	const FScopedTransaction Transaction(FText::FromString(TEXT("AI Edit AnimSequence")));
	Seq->Modify();

	bool bNotifiesChanged = false;
	bool bSyncMarkersChanged = false;

	// --- 4-5. Direct manipulation zone (notifies, sync markers, properties) ---

	// add_notifies
	const TArray<TSharedPtr<FJsonValue>>* AddNotifiesArray;
	if (Args->TryGetArrayField(TEXT("add_notifies"), AddNotifiesArray))
	{
		if (AddNotifies(Seq, AddNotifiesArray, Results) > 0)
		{
			bNotifiesChanged = true;
		}
	}

	// remove_notifies
	const TArray<TSharedPtr<FJsonValue>>* RemoveNotifiesArray;
	if (Args->TryGetArrayField(TEXT("remove_notifies"), RemoveNotifiesArray))
	{
		if (RemoveNotifies(Seq, RemoveNotifiesArray, Results) > 0)
		{
			bNotifiesChanged = true;
		}
	}

	// edit_notifies
	const TArray<TSharedPtr<FJsonValue>>* EditNotifiesArray;
	if (Args->TryGetArrayField(TEXT("edit_notifies"), EditNotifiesArray))
	{
		if (EditNotifies(Seq, EditNotifiesArray, Results) > 0)
		{
			bNotifiesChanged = true;
		}
	}

	// add_sync_markers
	const TArray<TSharedPtr<FJsonValue>>* AddMarkersArray;
	if (Args->TryGetArrayField(TEXT("add_sync_markers"), AddMarkersArray))
	{
		if (AddSyncMarkers(Seq, AddMarkersArray, Results) > 0)
		{
			bSyncMarkersChanged = true;
		}
	}

	// remove_sync_markers
	const TArray<TSharedPtr<FJsonValue>>* RemoveMarkersArray;
	if (Args->TryGetArrayField(TEXT("remove_sync_markers"), RemoveMarkersArray))
	{
		if (RemoveSyncMarkers(Seq, RemoveMarkersArray, Results) > 0)
		{
			bSyncMarkersChanged = true;
		}
	}

	// rename_sync_marker
	const TSharedPtr<FJsonObject>* RenameMarkerObj;
	if (Args->TryGetObjectField(TEXT("rename_sync_marker"), RenameMarkerObj))
	{
		if (RenameSyncMarker(Seq, *RenameMarkerObj, Results))
		{
			bSyncMarkersChanged = true;
		}
	}

	// --- 6. Controller zone (curves, frames, bone tracks) ---
	bool bHasControllerOps = false;
	{
		// Check if any controller operations are requested
		const TArray<TSharedPtr<FJsonValue>>* Dummy;
		const TSharedPtr<FJsonObject>* DummyObj;
		double DummyNum;
		bHasControllerOps =
			Args->TryGetArrayField(TEXT("add_curves"), Dummy) ||
			Args->TryGetArrayField(TEXT("remove_curves"), Dummy) ||
			Args->TryGetArrayField(TEXT("set_curve_keys"), Dummy) ||
			Args->TryGetArrayField(TEXT("set_transform_curve_keys"), Dummy) ||
			Args->TryGetObjectField(TEXT("rename_curve"), DummyObj) ||
			Args->TryGetObjectField(TEXT("scale_curve"), DummyObj) ||
			Args->TryGetObjectField(TEXT("set_curve_color"), DummyObj) ||
			Args->TryGetObjectField(TEXT("set_frame_rate"), DummyObj) ||
			Args->TryGetNumberField(TEXT("set_number_of_frames"), DummyNum) ||
			Args->TryGetObjectField(TEXT("resize"), DummyObj) ||
			Args->TryGetArrayField(TEXT("add_bone_tracks"), Dummy) ||
			Args->TryGetArrayField(TEXT("remove_bone_tracks"), Dummy) ||
			Args->TryGetArrayField(TEXT("set_bone_track_keys"), Dummy) ||
			Args->TryGetArrayField(TEXT("update_bone_track_keys"), Dummy);
	}

	if (bHasControllerOps)
	{
		IAnimationDataController& Controller = Seq->GetController();
		IAnimationDataController::FScopedBracket Bracket(Controller,
			FText::FromString(TEXT("AI AnimSequence Controller Edits")));

		// add_curves
		const TArray<TSharedPtr<FJsonValue>>* AddCurvesArray;
		if (Args->TryGetArrayField(TEXT("add_curves"), AddCurvesArray))
		{
			AddCurves(Controller, AddCurvesArray, Results);
		}

		// remove_curves
		const TArray<TSharedPtr<FJsonValue>>* RemoveCurvesArray;
		if (Args->TryGetArrayField(TEXT("remove_curves"), RemoveCurvesArray))
		{
			RemoveCurves(Controller, RemoveCurvesArray, Results);
		}

		// set_curve_keys
		const TArray<TSharedPtr<FJsonValue>>* SetCurveKeysArray;
		if (Args->TryGetArrayField(TEXT("set_curve_keys"), SetCurveKeysArray))
		{
			SetCurveKeys(Controller, SetCurveKeysArray, Results);
		}

		// set_transform_curve_keys
		const TArray<TSharedPtr<FJsonValue>>* SetTransformKeysArray;
		if (Args->TryGetArrayField(TEXT("set_transform_curve_keys"), SetTransformKeysArray))
		{
			SetTransformCurveKeys(Controller, SetTransformKeysArray, Results);
		}

		// rename_curve
		const TSharedPtr<FJsonObject>* RenameCurveObj;
		if (Args->TryGetObjectField(TEXT("rename_curve"), RenameCurveObj))
		{
			RenameCurve(Controller, *RenameCurveObj, Results);
		}

		// scale_curve
		const TSharedPtr<FJsonObject>* ScaleCurveObj;
		if (Args->TryGetObjectField(TEXT("scale_curve"), ScaleCurveObj))
		{
			ScaleCurve(Controller, *ScaleCurveObj, Results);
		}

		// set_curve_color
		const TSharedPtr<FJsonObject>* SetCurveColorObj;
		if (Args->TryGetObjectField(TEXT("set_curve_color"), SetCurveColorObj))
		{
			SetCurveColor(Controller, *SetCurveColorObj, Results);
		}

		// set_frame_rate
		const TSharedPtr<FJsonObject>* FrameRateObj;
		if (Args->TryGetObjectField(TEXT("set_frame_rate"), FrameRateObj))
		{
			SetFrameRate(Controller, *FrameRateObj, Results);
		}

		// set_number_of_frames
		double FrameCount;
		if (Args->TryGetNumberField(TEXT("set_number_of_frames"), FrameCount))
		{
			SetNumberOfFrames(Controller, FrameCount, Results);
		}

		// resize
		const TSharedPtr<FJsonObject>* ResizeObj;
		if (Args->TryGetObjectField(TEXT("resize"), ResizeObj))
		{
			Resize(Controller, *ResizeObj, Results);
		}

		// add_bone_tracks (before set/update — tracks must exist first)
		const TArray<TSharedPtr<FJsonValue>>* AddBonesArray;
		if (Args->TryGetArrayField(TEXT("add_bone_tracks"), AddBonesArray))
		{
			AddBoneTracks(Controller, AddBonesArray, Results);
		}

		// remove_bone_tracks
		const TArray<TSharedPtr<FJsonValue>>* RemoveBonesArray;
		if (Args->TryGetArrayField(TEXT("remove_bone_tracks"), RemoveBonesArray))
		{
			RemoveBoneTracks(Controller, RemoveBonesArray, Results);
		}

		// set_bone_track_keys
		const TArray<TSharedPtr<FJsonValue>>* SetBoneKeysArray;
		if (Args->TryGetArrayField(TEXT("set_bone_track_keys"), SetBoneKeysArray))
		{
			SetBoneTrackKeysOp(Controller, SetBoneKeysArray, Results);
		}

		// update_bone_track_keys
		const TArray<TSharedPtr<FJsonValue>>* UpdateBoneKeysArray;
		if (Args->TryGetArrayField(TEXT("update_bone_track_keys"), UpdateBoneKeysArray))
		{
			UpdateBoneTrackKeysOp(Controller, UpdateBoneKeysArray, Results);
		}
	}

	// --- 7. Finalize ---
	if (bNotifiesChanged)
	{
		Seq->SortNotifies();
	}
	if (bSyncMarkersChanged)
	{
		Seq->SortSyncMarkers();
		Seq->RefreshSyncMarkerDataFromAuthored();
	}

	Seq->PostEditChange();
	Seq->MarkPackageDirty();

	return FToolResult::Ok(FString::Join(Results, TEXT("\n")));
}

// ============================================================================
// Notify Operations
// ============================================================================

int32 FEditAnimSequenceTool::AddNotifies(UAnimSequence* Seq,
                                          const TArray<TSharedPtr<FJsonValue>>* NotifiesArray,
                                          TArray<FString>& OutResults)
{
	if (!NotifiesArray) return 0;

	int32 Added = 0;
	float PlayLength = Seq->GetPlayLength();

	for (const TSharedPtr<FJsonValue>& Value : *NotifiesArray)
	{
		const TSharedPtr<FJsonObject>* NotifyObj;
		if (!Value->TryGetObject(NotifyObj)) continue;

		FString NotifyName;
		if (!(*NotifyObj)->TryGetStringField(TEXT("name"), NotifyName) || NotifyName.IsEmpty())
		{
			OutResults.Add(TEXT("! add_notifies: missing 'name'"));
			continue;
		}

		double Time = 0.0;
		if (!(*NotifyObj)->TryGetNumberField(TEXT("time"), Time))
		{
			OutResults.Add(FString::Printf(TEXT("! add_notifies: '%s' missing 'time'"), *NotifyName));
			continue;
		}

		if (Time < 0.0 || Time > PlayLength)
		{
			OutResults.Add(FString::Printf(TEXT("! add_notifies: time %.2f outside duration (0 - %.2f)"),
				Time, PlayLength));
			continue;
		}

		FString TypeName;
		(*NotifyObj)->TryGetStringField(TEXT("type"), TypeName);

		double Duration = 0.0;
		(*NotifyObj)->TryGetNumberField(TEXT("duration"), Duration);

		double TrackIndex = 0.0;
		(*NotifyObj)->TryGetNumberField(TEXT("track_index"), TrackIndex);

		double TriggerChance = 1.0;
		(*NotifyObj)->TryGetNumberField(TEXT("trigger_chance"), TriggerChance);

		bool bBranchingPoint = false;
		(*NotifyObj)->TryGetBoolField(TEXT("branching_point"), bBranchingPoint);

		// Create the notify event
		FAnimNotifyEvent NewEvent;
		NewEvent.NotifyName = FName(*NotifyName);
		NewEvent.Link(Seq, static_cast<float>(Time));
		NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(
			Seq->CalculateOffsetForNotify(static_cast<float>(Time)));
		NewEvent.TrackIndex = static_cast<int32>(TrackIndex);
		NewEvent.NotifyTriggerChance = FMath::Clamp(static_cast<float>(TriggerChance), 0.0f, 1.0f);

		if (bBranchingPoint)
		{
			NewEvent.MontageTickType = EMontageNotifyTickType::BranchingPoint;
		}

		// Create typed notify instance if specified
		if (!TypeName.IsEmpty())
		{
			UClass* NotifyClass = FindAnimNotifyClass(TypeName);
			if (NotifyClass)
			{
				if (NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))
				{
					UAnimNotifyState* NotifyState = NewObject<UAnimNotifyState>(Seq, NotifyClass);
					NewEvent.NotifyStateClass = NotifyState;
					NewEvent.Duration = static_cast<float>(Duration > 0.0 ? Duration : 0.5);
					NewEvent.EndLink.Link(Seq, static_cast<float>(Time + NewEvent.Duration));
				}
				else
				{
					UAnimNotify* Notify = NewObject<UAnimNotify>(Seq, NotifyClass);
					NewEvent.Notify = Notify;
				}
			}
			else
			{
				OutResults.Add(FString::Printf(
					TEXT("! add_notifies: class '%s' not found, adding as named notify"), *TypeName));
			}
		}

		Seq->Notifies.Add(NewEvent);

		FString Extra;
		if (!TypeName.IsEmpty()) Extra += FString::Printf(TEXT(" type=%s"), *TypeName);
		if (Duration > 0.0) Extra += FString::Printf(TEXT(" duration=%.2fs"), Duration);
		if (bBranchingPoint) Extra += TEXT(" [BranchingPoint]");
		OutResults.Add(FString::Printf(TEXT("+ Notify: %s at %.3fs%s"), *NotifyName, Time, *Extra));
		Added++;
	}

	return Added;
}

int32 FEditAnimSequenceTool::RemoveNotifies(UAnimSequence* Seq,
                                             const TArray<TSharedPtr<FJsonValue>>* NotifiesArray,
                                             TArray<FString>& OutResults)
{
	if (!NotifiesArray) return 0;

	TArray<int32> IndicesToRemove;

	for (const TSharedPtr<FJsonValue>& Value : *NotifiesArray)
	{
		FString NotifyName;
		double NotifyIndex;

		if (Value->TryGetString(NotifyName))
		{
			// Find by name
			bool bFound = false;
			for (int32 i = 0; i < Seq->Notifies.Num(); i++)
			{
				const FAnimNotifyEvent& Evt = Seq->Notifies[i];
				FString EvtName;
				if (Evt.Notify) EvtName = Evt.Notify->GetNotifyName();
				else if (Evt.NotifyStateClass) EvtName = Evt.NotifyStateClass->GetNotifyName();
				else EvtName = Evt.NotifyName.ToString();

				if (EvtName.Equals(NotifyName, ESearchCase::IgnoreCase))
				{
					IndicesToRemove.AddUnique(i);
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				OutResults.Add(FString::Printf(TEXT("! remove_notifies: '%s' not found"), *NotifyName));
			}
		}
		else if (Value->TryGetNumber(NotifyIndex))
		{
			int32 Idx = static_cast<int32>(NotifyIndex);
			if (Idx >= 0 && Idx < Seq->Notifies.Num())
			{
				IndicesToRemove.AddUnique(Idx);
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("! remove_notifies: index %d out of range (0-%d)"),
					Idx, Seq->Notifies.Num() - 1));
			}
		}
	}

	// Remove in reverse order
	IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Idx : IndicesToRemove)
	{
		FString RemovedName = Seq->Notifies[Idx].NotifyName.ToString();
		Seq->Notifies.RemoveAt(Idx);
		OutResults.Add(FString::Printf(TEXT("- Notify: %s (index %d)"), *RemovedName, Idx));
	}

	return IndicesToRemove.Num();
}

int32 FEditAnimSequenceTool::EditNotifies(UAnimSequence* Seq,
                                           const TArray<TSharedPtr<FJsonValue>>* NotifiesArray,
                                           TArray<FString>& OutResults)
{
	if (!NotifiesArray) return 0;

	int32 Edited = 0;
	float PlayLength = Seq->GetPlayLength();

	for (const TSharedPtr<FJsonValue>& Value : *NotifiesArray)
	{
		const TSharedPtr<FJsonObject>* NotifyObj;
		if (!Value->TryGetObject(NotifyObj)) continue;

		double IndexDouble;
		if (!(*NotifyObj)->TryGetNumberField(TEXT("index"), IndexDouble))
		{
			OutResults.Add(TEXT("! edit_notifies: missing 'index'"));
			continue;
		}

		int32 Index = static_cast<int32>(IndexDouble);
		if (Index < 0 || Index >= Seq->Notifies.Num())
		{
			OutResults.Add(FString::Printf(TEXT("! edit_notifies: index %d out of range (0-%d)"),
				Index, Seq->Notifies.Num() - 1));
			continue;
		}

		FAnimNotifyEvent& Evt = Seq->Notifies[Index];
		TArray<FString> Changes;

		// Edit time
		double NewTime;
		if ((*NotifyObj)->TryGetNumberField(TEXT("time"), NewTime))
		{
			if (NewTime >= 0.0 && NewTime <= PlayLength)
			{
				Evt.Link(Seq, static_cast<float>(NewTime));
				Evt.TriggerTimeOffset = GetTriggerTimeOffsetForType(
					Seq->CalculateOffsetForNotify(static_cast<float>(NewTime)));
				Changes.Add(FString::Printf(TEXT("time=%.3f"), NewTime));
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("! edit_notifies[%d]: time %.2f outside duration"),
					Index, NewTime));
			}
		}

		// Edit duration (state notifies only)
		double NewDuration;
		if ((*NotifyObj)->TryGetNumberField(TEXT("duration"), NewDuration))
		{
			if (Evt.NotifyStateClass)
			{
				Evt.Duration = static_cast<float>(NewDuration);
				// Re-link end position
				float CurrentTime = Evt.GetTime();
				Evt.EndLink.Link(Seq, CurrentTime + Evt.Duration);
				Changes.Add(FString::Printf(TEXT("duration=%.3f"), NewDuration));
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("! edit_notifies[%d]: duration only applies to state notifies"),
					Index));
			}
		}

		// Edit track index
		double NewTrackIndex;
		if ((*NotifyObj)->TryGetNumberField(TEXT("track_index"), NewTrackIndex))
		{
			Evt.TrackIndex = static_cast<int32>(NewTrackIndex);
			Changes.Add(FString::Printf(TEXT("track=%d"), Evt.TrackIndex));
		}

		// Edit trigger chance
		double NewChance;
		if ((*NotifyObj)->TryGetNumberField(TEXT("trigger_chance"), NewChance))
		{
			Evt.NotifyTriggerChance = FMath::Clamp(static_cast<float>(NewChance), 0.0f, 1.0f);
			Changes.Add(FString::Printf(TEXT("chance=%.2f"), Evt.NotifyTriggerChance));
		}

		if (Changes.Num() > 0)
		{
			OutResults.Add(FString::Printf(TEXT("= Notify[%d] %s: %s"),
				Index, *Evt.NotifyName.ToString(), *FString::Join(Changes, TEXT(", "))));
			Edited++;
		}
	}

	return Edited;
}

// ============================================================================
// Sync Markers
// ============================================================================

int32 FEditAnimSequenceTool::AddSyncMarkers(UAnimSequence* Seq,
                                             const TArray<TSharedPtr<FJsonValue>>* MarkersArray,
                                             TArray<FString>& OutResults)
{
	if (!MarkersArray) return 0;

	int32 Added = 0;
	float PlayLength = Seq->GetPlayLength();

	for (const TSharedPtr<FJsonValue>& Value : *MarkersArray)
	{
		const TSharedPtr<FJsonObject>* MarkerObj;
		if (!Value->TryGetObject(MarkerObj)) continue;

		FString MarkerName;
		if (!(*MarkerObj)->TryGetStringField(TEXT("name"), MarkerName) || MarkerName.IsEmpty())
		{
			OutResults.Add(TEXT("! add_sync_markers: missing 'name'"));
			continue;
		}

		double Time = 0.0;
		if (!(*MarkerObj)->TryGetNumberField(TEXT("time"), Time))
		{
			OutResults.Add(FString::Printf(TEXT("! add_sync_markers: '%s' missing 'time'"), *MarkerName));
			continue;
		}

		if (Time < 0.0 || Time > PlayLength)
		{
			OutResults.Add(FString::Printf(TEXT("! add_sync_markers: time %.2f outside duration (0 - %.2f)"),
				Time, PlayLength));
			continue;
		}

		FAnimSyncMarker NewMarker;
		NewMarker.MarkerName = FName(*MarkerName);
		NewMarker.Time = static_cast<float>(Time);

#if WITH_EDITORONLY_DATA
		double TrackIndex = 0.0;
		(*MarkerObj)->TryGetNumberField(TEXT("track_index"), TrackIndex);
		NewMarker.TrackIndex = static_cast<int32>(TrackIndex);
#endif

		Seq->AuthoredSyncMarkers.Add(NewMarker);
		OutResults.Add(FString::Printf(TEXT("+ SyncMarker: %s at %.3fs"), *MarkerName, Time));
		Added++;
	}

	return Added;
}

int32 FEditAnimSequenceTool::RemoveSyncMarkers(UAnimSequence* Seq,
                                                const TArray<TSharedPtr<FJsonValue>>* MarkersArray,
                                                TArray<FString>& OutResults)
{
	if (!MarkersArray) return 0;

	int32 Removed = 0;

	// Separate name-based and index-based removals
	TArray<FName> NamesToRemove;
	TArray<int32> IndicesToRemove;

	for (const TSharedPtr<FJsonValue>& Value : *MarkersArray)
	{
		FString MarkerName;
		double MarkerIndex;

		if (Value->TryGetString(MarkerName))
		{
			NamesToRemove.AddUnique(FName(*MarkerName));
		}
		else if (Value->TryGetNumber(MarkerIndex))
		{
			int32 Idx = static_cast<int32>(MarkerIndex);
			if (Idx >= 0 && Idx < Seq->AuthoredSyncMarkers.Num())
			{
				IndicesToRemove.AddUnique(Idx);
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("! remove_sync_markers: index %d out of range (0-%d)"),
					Idx, Seq->AuthoredSyncMarkers.Num() - 1));
			}
		}
	}

	// Remove by name using engine method
	if (NamesToRemove.Num() > 0)
	{
		for (const FName& Name : NamesToRemove)
		{
			// Count before removal
			int32 CountBefore = Seq->AuthoredSyncMarkers.Num();
			TArray<FName> SingleName = { Name };
			Seq->RemoveSyncMarkers(SingleName);
			int32 CountAfter = Seq->AuthoredSyncMarkers.Num();
			int32 RemovedCount = CountBefore - CountAfter;

			if (RemovedCount > 0)
			{
				OutResults.Add(FString::Printf(TEXT("- SyncMarker: %s (%d removed)"),
					*Name.ToString(), RemovedCount));
				Removed += RemovedCount;
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("! remove_sync_markers: '%s' not found"), *Name.ToString()));
			}
		}
	}

	// Remove by index (reverse order)
	if (IndicesToRemove.Num() > 0)
	{
		IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 Idx : IndicesToRemove)
		{
			if (Idx < Seq->AuthoredSyncMarkers.Num())
			{
				FString RemovedName = Seq->AuthoredSyncMarkers[Idx].MarkerName.ToString();
				Seq->AuthoredSyncMarkers.RemoveAt(Idx);
				OutResults.Add(FString::Printf(TEXT("- SyncMarker: %s (index %d)"), *RemovedName, Idx));
				Removed++;
			}
		}
	}

	return Removed;
}

bool FEditAnimSequenceTool::RenameSyncMarker(UAnimSequence* Seq,
                                              const TSharedPtr<FJsonObject>& RenameObj,
                                              TArray<FString>& OutResults)
{
	if (!RenameObj.IsValid()) return false;

	FString OldName, NewName;
	if (!RenameObj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		OutResults.Add(TEXT("! rename_sync_marker: missing 'old_name'"));
		return false;
	}
	if (!RenameObj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		OutResults.Add(TEXT("! rename_sync_marker: missing 'new_name'"));
		return false;
	}

	bool bRenamed = Seq->RenameSyncMarkers(FName(*OldName), FName(*NewName));
	if (bRenamed)
	{
		OutResults.Add(FString::Printf(TEXT("= SyncMarker: '%s' -> '%s'"), *OldName, *NewName));
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("! rename_sync_marker: '%s' not found"), *OldName));
	}
	return bRenamed;
}

// ============================================================================
// Curve Operations (via IAnimationDataController)
// ============================================================================

int32 FEditAnimSequenceTool::AddCurves(IAnimationDataController& Controller,
                                        const TArray<TSharedPtr<FJsonValue>>* CurvesArray,
                                        TArray<FString>& OutResults)
{
	if (!CurvesArray) return 0;

	int32 Added = 0;

	for (const TSharedPtr<FJsonValue>& Value : *CurvesArray)
	{
		const TSharedPtr<FJsonObject>* CurveObj;
		if (!Value->TryGetObject(CurveObj)) continue;

		FString CurveName;
		if (!(*CurveObj)->TryGetStringField(TEXT("name"), CurveName) || CurveName.IsEmpty())
		{
			OutResults.Add(TEXT("! add_curves: missing 'name'"));
			continue;
		}

		FString CurveType;
		(*CurveObj)->TryGetStringField(TEXT("type"), CurveType);
		if (CurveType.IsEmpty()) CurveType = TEXT("float");

		FAnimationCurveIdentifier CurveId = MakeCurveId(CurveName, CurveType);

		if (Controller.AddCurve(CurveId))
		{
			OutResults.Add(FString::Printf(TEXT("+ Curve: %s (%s)"), *CurveName, *CurveType));
			Added++;

			// If keys are provided inline, set them immediately
			const TArray<TSharedPtr<FJsonValue>>* InlineKeys;
			if ((*CurveObj)->TryGetArrayField(TEXT("keys"), InlineKeys) && InlineKeys->Num() > 0)
			{
				if (CurveType.Equals(TEXT("float"), ESearchCase::IgnoreCase))
				{
					TArray<FRichCurveKey> Keys;
					for (const TSharedPtr<FJsonValue>& KeyValue : *InlineKeys)
					{
						const TSharedPtr<FJsonObject>* KeyObj;
						if (!KeyValue->TryGetObject(KeyObj)) continue;

						double KeyTime = 0.0, KeyVal = 0.0;
						(*KeyObj)->TryGetNumberField(TEXT("time"), KeyTime);
						(*KeyObj)->TryGetNumberField(TEXT("value"), KeyVal);

						FRichCurveKey Key(static_cast<float>(KeyTime), static_cast<float>(KeyVal));

						FString InterpStr;
						if ((*KeyObj)->TryGetStringField(TEXT("interp_mode"), InterpStr))
						{
							Key.InterpMode = ParseInterpMode(InterpStr);
						}
						FString TangentStr;
						if ((*KeyObj)->TryGetStringField(TEXT("tangent_mode"), TangentStr))
						{
							Key.TangentMode = ParseTangentMode(TangentStr);
						}

						Keys.Add(Key);
					}
					if (Keys.Num() > 0)
					{
						Controller.SetCurveKeys(CurveId, Keys);
						OutResults.Add(FString::Printf(TEXT("  Set %d inline keys on %s"), Keys.Num(), *CurveName));
					}
				}
			}
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! add_curves: failed to add '%s' (may already exist)"), *CurveName));
		}
	}

	return Added;
}

int32 FEditAnimSequenceTool::RemoveCurves(IAnimationDataController& Controller,
                                           const TArray<TSharedPtr<FJsonValue>>* CurvesArray,
                                           TArray<FString>& OutResults)
{
	if (!CurvesArray) return 0;

	int32 Removed = 0;

	for (const TSharedPtr<FJsonValue>& Value : *CurvesArray)
	{
		FString CurveName;
		FString CurveType = TEXT("float");

		// Support both string and object format
		if (Value->TryGetString(CurveName))
		{
			// Simple string — assume float
		}
		else
		{
			const TSharedPtr<FJsonObject>* CurveObj;
			if (Value->TryGetObject(CurveObj))
			{
				(*CurveObj)->TryGetStringField(TEXT("name"), CurveName);
				(*CurveObj)->TryGetStringField(TEXT("type"), CurveType);
			}
		}

		if (CurveName.IsEmpty())
		{
			OutResults.Add(TEXT("! remove_curves: empty curve name"));
			continue;
		}

		FAnimationCurveIdentifier CurveId = MakeCurveId(CurveName, CurveType);
		if (Controller.RemoveCurve(CurveId))
		{
			OutResults.Add(FString::Printf(TEXT("- Curve: %s"), *CurveName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! remove_curves: '%s' not found"), *CurveName));
		}
	}

	return Removed;
}

int32 FEditAnimSequenceTool::SetCurveKeys(IAnimationDataController& Controller,
                                           const TArray<TSharedPtr<FJsonValue>>* KeysArray,
                                           TArray<FString>& OutResults)
{
	if (!KeysArray) return 0;

	int32 Set = 0;

	for (const TSharedPtr<FJsonValue>& Value : *KeysArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Value->TryGetObject(EntryObj)) continue;

		FString CurveName;
		if (!(*EntryObj)->TryGetStringField(TEXT("curve"), CurveName) || CurveName.IsEmpty())
		{
			OutResults.Add(TEXT("! set_curve_keys: missing 'curve' name"));
			continue;
		}

		FString CurveType;
		(*EntryObj)->TryGetStringField(TEXT("type"), CurveType);
		if (CurveType.IsEmpty()) CurveType = TEXT("float");

		FAnimationCurveIdentifier CurveId = MakeCurveId(CurveName, CurveType);

		const TArray<TSharedPtr<FJsonValue>>* KeysData;
		if (!(*EntryObj)->TryGetArrayField(TEXT("keys"), KeysData) || KeysData->Num() == 0)
		{
			OutResults.Add(FString::Printf(TEXT("! set_curve_keys: '%s' missing or empty 'keys' array"), *CurveName));
			continue;
		}

		TArray<FRichCurveKey> Keys;
		for (const TSharedPtr<FJsonValue>& KeyValue : *KeysData)
		{
			const TSharedPtr<FJsonObject>* KeyObj;
			if (!KeyValue->TryGetObject(KeyObj)) continue;

			double KeyTime = 0.0, KeyVal = 0.0;
			(*KeyObj)->TryGetNumberField(TEXT("time"), KeyTime);
			(*KeyObj)->TryGetNumberField(TEXT("value"), KeyVal);

			FRichCurveKey Key(static_cast<float>(KeyTime), static_cast<float>(KeyVal));

			FString InterpStr;
			if ((*KeyObj)->TryGetStringField(TEXT("interp_mode"), InterpStr))
			{
				Key.InterpMode = ParseInterpMode(InterpStr);
			}
			FString TangentStr;
			if ((*KeyObj)->TryGetStringField(TEXT("tangent_mode"), TangentStr))
			{
				Key.TangentMode = ParseTangentMode(TangentStr);
			}

			double ArriveTangent, LeaveTangent;
			if ((*KeyObj)->TryGetNumberField(TEXT("arrive_tangent"), ArriveTangent))
			{
				Key.ArriveTangent = static_cast<float>(ArriveTangent);
			}
			if ((*KeyObj)->TryGetNumberField(TEXT("leave_tangent"), LeaveTangent))
			{
				Key.LeaveTangent = static_cast<float>(LeaveTangent);
			}

			Keys.Add(Key);
		}

		if (Keys.Num() > 0 && Controller.SetCurveKeys(CurveId, Keys))
		{
			OutResults.Add(FString::Printf(TEXT("= Curve %s: set %d keys"), *CurveName, Keys.Num()));
			Set++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! set_curve_keys: failed to set keys on '%s' (curve may not exist)"),
				*CurveName));
		}
	}

	return Set;
}

int32 FEditAnimSequenceTool::SetTransformCurveKeys(IAnimationDataController& Controller,
                                                    const TArray<TSharedPtr<FJsonValue>>* KeysArray,
                                                    TArray<FString>& OutResults)
{
	if (!KeysArray) return 0;

	int32 Set = 0;

	for (const TSharedPtr<FJsonValue>& Value : *KeysArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Value->TryGetObject(EntryObj)) continue;

		FString CurveName;
		if (!(*EntryObj)->TryGetStringField(TEXT("curve"), CurveName) || CurveName.IsEmpty())
		{
			OutResults.Add(TEXT("! set_transform_curve_keys: missing 'curve' name"));
			continue;
		}

		FAnimationCurveIdentifier CurveId = MakeCurveId(CurveName, TEXT("transform"));

		const TArray<TSharedPtr<FJsonValue>>* KeysData;
		if (!(*EntryObj)->TryGetArrayField(TEXT("keys"), KeysData) || KeysData->Num() == 0)
		{
			OutResults.Add(FString::Printf(TEXT("! set_transform_curve_keys: '%s' missing or empty 'keys'"), *CurveName));
			continue;
		}

		TArray<FTransform> Transforms;
		TArray<float> Times;

		for (const TSharedPtr<FJsonValue>& KeyValue : *KeysData)
		{
			const TSharedPtr<FJsonObject>* KeyObj;
			if (!KeyValue->TryGetObject(KeyObj)) continue;

			double KeyTime = 0.0;
			(*KeyObj)->TryGetNumberField(TEXT("time"), KeyTime);
			Times.Add(static_cast<float>(KeyTime));

			FVector Location = FVector::ZeroVector;
			FRotator Rotation = FRotator::ZeroRotator;
			FVector Scale = FVector::OneVector;

			// Parse location [x,y,z]
			const TArray<TSharedPtr<FJsonValue>>* LocArray;
			if ((*KeyObj)->TryGetArrayField(TEXT("location"), LocArray) && LocArray->Num() >= 3)
			{
				Location.X = (*LocArray)[0]->AsNumber();
				Location.Y = (*LocArray)[1]->AsNumber();
				Location.Z = (*LocArray)[2]->AsNumber();
			}

			// Parse rotation [pitch,yaw,roll]
			const TArray<TSharedPtr<FJsonValue>>* RotArray;
			if ((*KeyObj)->TryGetArrayField(TEXT("rotation"), RotArray) && RotArray->Num() >= 3)
			{
				Rotation.Pitch = (*RotArray)[0]->AsNumber();
				Rotation.Yaw = (*RotArray)[1]->AsNumber();
				Rotation.Roll = (*RotArray)[2]->AsNumber();
			}

			// Parse scale [x,y,z]
			const TArray<TSharedPtr<FJsonValue>>* ScaleArray;
			if ((*KeyObj)->TryGetArrayField(TEXT("scale"), ScaleArray) && ScaleArray->Num() >= 3)
			{
				Scale.X = (*ScaleArray)[0]->AsNumber();
				Scale.Y = (*ScaleArray)[1]->AsNumber();
				Scale.Z = (*ScaleArray)[2]->AsNumber();
			}

			Transforms.Add(FTransform(Rotation.Quaternion(), Location, Scale));
		}

		if (Transforms.Num() > 0 && Controller.SetTransformCurveKeys(CurveId, Transforms, Times))
		{
			OutResults.Add(FString::Printf(TEXT("= TransformCurve %s: set %d keys"), *CurveName, Transforms.Num()));
			Set++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! set_transform_curve_keys: failed on '%s'"), *CurveName));
		}
	}

	return Set;
}

bool FEditAnimSequenceTool::RenameCurve(IAnimationDataController& Controller,
                                         const TSharedPtr<FJsonObject>& RenameObj,
                                         TArray<FString>& OutResults)
{
	if (!RenameObj.IsValid()) return false;

	FString OldName, NewName, CurveType;
	if (!RenameObj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		OutResults.Add(TEXT("! rename_curve: missing 'old_name'"));
		return false;
	}
	if (!RenameObj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		OutResults.Add(TEXT("! rename_curve: missing 'new_name'"));
		return false;
	}
	RenameObj->TryGetStringField(TEXT("type"), CurveType);
	if (CurveType.IsEmpty()) CurveType = TEXT("float");

	FAnimationCurveIdentifier OldId = MakeCurveId(OldName, CurveType);
	FAnimationCurveIdentifier NewId = MakeCurveId(NewName, CurveType);

	if (Controller.RenameCurve(OldId, NewId))
	{
		OutResults.Add(FString::Printf(TEXT("= Curve: '%s' -> '%s'"), *OldName, *NewName));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("! rename_curve: '%s' not found"), *OldName));
		return false;
	}
}

bool FEditAnimSequenceTool::ScaleCurve(IAnimationDataController& Controller,
                                        const TSharedPtr<FJsonObject>& ScaleObj,
                                        TArray<FString>& OutResults)
{
	if (!ScaleObj.IsValid()) return false;

	FString CurveName, CurveType;
	double Origin = 0.0, Factor = 1.0;

	if (!ScaleObj->TryGetStringField(TEXT("name"), CurveName) || CurveName.IsEmpty())
	{
		OutResults.Add(TEXT("! scale_curve: missing 'name'"));
		return false;
	}
	if (!ScaleObj->TryGetNumberField(TEXT("factor"), Factor))
	{
		OutResults.Add(TEXT("! scale_curve: missing 'factor'"));
		return false;
	}
	ScaleObj->TryGetNumberField(TEXT("origin"), Origin);
	ScaleObj->TryGetStringField(TEXT("type"), CurveType);
	if (CurveType.IsEmpty()) CurveType = TEXT("float");

	FAnimationCurveIdentifier CurveId = MakeCurveId(CurveName, CurveType);

	if (Controller.ScaleCurve(CurveId, static_cast<float>(Origin), static_cast<float>(Factor)))
	{
		OutResults.Add(FString::Printf(TEXT("= Curve %s: scaled by %.2f (origin=%.2f)"), *CurveName, Factor, Origin));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("! scale_curve: failed on '%s'"), *CurveName));
		return false;
	}
}

bool FEditAnimSequenceTool::SetCurveColor(IAnimationDataController& Controller,
                                           const TSharedPtr<FJsonObject>& ColorObj,
                                           TArray<FString>& OutResults)
{
	if (!ColorObj.IsValid()) return false;

	FString CurveName, CurveType;
	if (!ColorObj->TryGetStringField(TEXT("name"), CurveName) || CurveName.IsEmpty())
	{
		OutResults.Add(TEXT("! set_curve_color: missing 'name'"));
		return false;
	}
	ColorObj->TryGetStringField(TEXT("type"), CurveType);
	if (CurveType.IsEmpty()) CurveType = TEXT("float");

	const TArray<TSharedPtr<FJsonValue>>* ColorArray;
	if (!ColorObj->TryGetArrayField(TEXT("color"), ColorArray) || ColorArray->Num() < 3)
	{
		OutResults.Add(TEXT("! set_curve_color: 'color' must be [r,g,b] or [r,g,b,a]"));
		return false;
	}

	float R = static_cast<float>((*ColorArray)[0]->AsNumber());
	float G = static_cast<float>((*ColorArray)[1]->AsNumber());
	float B = static_cast<float>((*ColorArray)[2]->AsNumber());
	float A = ColorArray->Num() >= 4 ? static_cast<float>((*ColorArray)[3]->AsNumber()) : 1.0f;

	FAnimationCurveIdentifier CurveId = MakeCurveId(CurveName, CurveType);

	if (Controller.SetCurveColor(CurveId, FLinearColor(R, G, B, A)))
	{
		OutResults.Add(FString::Printf(TEXT("= Curve %s: color (%.2f, %.2f, %.2f, %.2f)"),
			*CurveName, R, G, B, A));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("! set_curve_color: failed on '%s'"), *CurveName));
		return false;
	}
}

// ============================================================================
// Properties
// ============================================================================

// ============================================================================
// Frame Operations (via IAnimationDataController)
// ============================================================================

bool FEditAnimSequenceTool::SetFrameRate(IAnimationDataController& Controller,
                                          const TSharedPtr<FJsonObject>& FRObj,
                                          TArray<FString>& OutResults)
{
	if (!FRObj.IsValid()) return false;

	int32 Numerator = 30, Denominator = 1;

	double FPS;
	if (FRObj->TryGetNumberField(TEXT("fps"), FPS))
	{
		Numerator = static_cast<int32>(FPS);
		Denominator = 1;
	}
	else
	{
		double Num, Den;
		if (FRObj->TryGetNumberField(TEXT("numerator"), Num))
		{
			Numerator = static_cast<int32>(Num);
		}
		if (FRObj->TryGetNumberField(TEXT("denominator"), Den))
		{
			Denominator = FMath::Max(1, static_cast<int32>(Den));
		}
	}

	Controller.SetFrameRate(FFrameRate(Numerator, Denominator));
	OutResults.Add(FString::Printf(TEXT("= FrameRate: %d/%d (%.1f fps)"),
		Numerator, Denominator, static_cast<float>(Numerator) / static_cast<float>(Denominator)));
	return true;
}

bool FEditAnimSequenceTool::SetNumberOfFrames(IAnimationDataController& Controller, double FrameCount,
                                               TArray<FString>& OutResults)
{
	int32 Frames = FMath::Max(1, static_cast<int32>(FrameCount));
	Controller.SetNumberOfFrames(FFrameNumber(Frames));
	OutResults.Add(FString::Printf(TEXT("= NumberOfFrames: %d"), Frames));
	return true;
}

bool FEditAnimSequenceTool::Resize(IAnimationDataController& Controller,
                                    const TSharedPtr<FJsonObject>& ResizeObj,
                                    TArray<FString>& OutResults)
{
	if (!ResizeObj.IsValid()) return false;

	double NewFrameCount, T0, T1;
	if (!ResizeObj->TryGetNumberField(TEXT("new_frame_count"), NewFrameCount))
	{
		OutResults.Add(TEXT("! resize: missing 'new_frame_count'"));
		return false;
	}
	if (!ResizeObj->TryGetNumberField(TEXT("t0"), T0))
	{
		OutResults.Add(TEXT("! resize: missing 't0'"));
		return false;
	}
	if (!ResizeObj->TryGetNumberField(TEXT("t1"), T1))
	{
		OutResults.Add(TEXT("! resize: missing 't1'"));
		return false;
	}

	Controller.ResizeNumberOfFrames(
		FFrameNumber(static_cast<int32>(NewFrameCount)),
		FFrameNumber(static_cast<int32>(T0)),
		FFrameNumber(static_cast<int32>(T1)));

	OutResults.Add(FString::Printf(TEXT("= Resized: %d frames (range %d-%d)"),
		static_cast<int32>(NewFrameCount), static_cast<int32>(T0), static_cast<int32>(T1)));
	return true;
}

// ============================================================================
// Bone Tracks (via IAnimationDataController)
// ============================================================================

int32 FEditAnimSequenceTool::RemoveBoneTracks(IAnimationDataController& Controller,
                                               const TArray<TSharedPtr<FJsonValue>>* TracksArray,
                                               TArray<FString>& OutResults)
{
	if (!TracksArray) return 0;

	int32 Removed = 0;

	for (const TSharedPtr<FJsonValue>& Value : *TracksArray)
	{
		FString BoneName;
		if (!Value->TryGetString(BoneName) || BoneName.IsEmpty()) continue;

		if (Controller.RemoveBoneTrack(FName(*BoneName)))
		{
			OutResults.Add(FString::Printf(TEXT("- BoneTrack: %s"), *BoneName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! remove_bone_tracks: '%s' not found"), *BoneName));
		}
	}

	return Removed;
}

// ============================================================================
// Asset Creation
// ============================================================================

UAnimSequence* FEditAnimSequenceTool::CreateAnimSequence(const FString& Name, const FString& Path,
                                                          const TSharedPtr<FJsonObject>& CreateObj,
                                                          TArray<FString>& OutResults)
{
	if (!CreateObj.IsValid())
	{
		OutResults.Add(TEXT("! create: invalid create object"));
		return nullptr;
	}

	// Load skeleton (required)
	FString SkeletonPath;
	if (!CreateObj->TryGetStringField(TEXT("skeleton"), SkeletonPath) || SkeletonPath.IsEmpty())
	{
		OutResults.Add(TEXT("! create: missing required 'skeleton' field"));
		return nullptr;
	}

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
	if (!Skeleton)
	{
		Skeleton = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(SkeletonPath);
	}
	if (!Skeleton)
	{
		OutResults.Add(FString::Printf(TEXT("! create: skeleton not found: '%s'"), *SkeletonPath));
		return nullptr;
	}

	// Parse optional parameters
	double FrameRateVal = 30.0;
	CreateObj->TryGetNumberField(TEXT("frame_rate"), FrameRateVal);
	int32 FrameRate = FMath::Max(1, static_cast<int32>(FrameRateVal));

	double NumFramesVal = 1.0;
	CreateObj->TryGetNumberField(TEXT("num_frames"), NumFramesVal);
	int32 NumFrames = FMath::Max(1, static_cast<int32>(NumFramesVal));

	// Build asset path and create package
	FString AssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);

	// Extract package path and asset name from the full path
	FString PackagePath, AssetName;
	AssetPath.Split(TEXT("."), &PackagePath, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
	if (AssetName.IsEmpty())
	{
		// No dot separator — the whole thing is the package path, use Name as asset name
		PackagePath = AssetPath;
		AssetName = FPackageName::GetShortName(PackagePath);
	}

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		OutResults.Add(FString::Printf(TEXT("! create: failed to create package '%s'"), *PackagePath));
		return nullptr;
	}

	UAnimSequence* Seq = NewObject<UAnimSequence>(Package, UAnimSequence::StaticClass(),
		FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!Seq)
	{
		OutResults.Add(TEXT("! create: failed to create AnimSequence object"));
		return nullptr;
	}

	// Associate with skeleton
	Seq->SetSkeleton(Skeleton);

	// Initialize the data model via controller
	IAnimationDataController& Controller = Seq->GetController();
	Controller.InitializeModel();

	// Set frame rate if not default 30
	if (FrameRate != 30)
	{
		Controller.SetFrameRate(FFrameRate(FrameRate, 1));
	}

	// Set number of frames
	Controller.SetNumberOfFrames(FFrameNumber(NumFrames));

	// Notify the model is populated
	Controller.NotifyPopulated();

	// Register with asset registry
	FAssetRegistryModule::AssetCreated(Seq);
	Seq->MarkPackageDirty();

	OutResults.Add(FString::Printf(TEXT("+ Created AnimSequence '%s' (skeleton=%s, %dfps, %d frames)"),
		*AssetName, *Skeleton->GetName(), FrameRate, NumFrames));

	return Seq;
}

// ============================================================================
// Bone Track Operations (via IAnimationDataController)
// ============================================================================

int32 FEditAnimSequenceTool::AddBoneTracks(IAnimationDataController& Controller,
                                            const TArray<TSharedPtr<FJsonValue>>* TracksArray,
                                            TArray<FString>& OutResults)
{
	if (!TracksArray) return 0;

	int32 Added = 0;

	for (const TSharedPtr<FJsonValue>& Value : *TracksArray)
	{
		FString BoneName;
		if (!Value->TryGetString(BoneName) || BoneName.IsEmpty()) continue;

		if (Controller.AddBoneCurve(FName(*BoneName)))
		{
			OutResults.Add(FString::Printf(TEXT("+ BoneTrack: %s"), *BoneName));
			Added++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! add_bone_tracks: failed to add '%s' (may already exist or bone not in skeleton)"), *BoneName));
		}
	}

	return Added;
}

// Parse rotation from JSON array — 3 elements = euler [pitch,yaw,roll], 4 elements = quaternion [x,y,z,w]
static FQuat ParseRotationFromArray(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	if (Arr.Num() >= 4)
	{
		// Quaternion [x, y, z, w]
		return FQuat(
			Arr[0]->AsNumber(),
			Arr[1]->AsNumber(),
			Arr[2]->AsNumber(),
			Arr[3]->AsNumber()
		);
	}
	else if (Arr.Num() >= 3)
	{
		// Euler [pitch, yaw, roll] → FRotator → Quaternion
		FRotator Rot(
			Arr[0]->AsNumber(),  // Pitch
			Arr[1]->AsNumber(),  // Yaw
			Arr[2]->AsNumber()   // Roll
		);
		return Rot.Quaternion();
	}
	return FQuat::Identity;
}

static FVector ParseBoneVector(const TArray<TSharedPtr<FJsonValue>>& Arr, const FVector& Default = FVector::ZeroVector)
{
	if (Arr.Num() < 3) return Default;
	return FVector(
		Arr[0]->AsNumber(),
		Arr[1]->AsNumber(),
		Arr[2]->AsNumber()
	);
}

// Parse bone track key arrays from a JSON object
static bool ParseBoneKeyArrays(const TSharedPtr<FJsonObject>& Obj,
                                TArray<FVector>& OutPositions,
                                TArray<FQuat>& OutRotations,
                                TArray<FVector>& OutScales,
                                FString& OutBoneName,
                                TArray<FString>& OutResults,
                                const FString& OpName)
{
	if (!Obj->TryGetStringField(TEXT("bone"), OutBoneName) || OutBoneName.IsEmpty())
	{
		OutResults.Add(FString::Printf(TEXT("! %s: missing 'bone'"), *OpName));
		return false;
	}

	// Parse positions (required)
	const TArray<TSharedPtr<FJsonValue>>* PosArray;
	if (!Obj->TryGetArrayField(TEXT("positions"), PosArray) || PosArray->Num() == 0)
	{
		OutResults.Add(FString::Printf(TEXT("! %s: '%s' missing or empty 'positions'"), *OpName, *OutBoneName));
		return false;
	}

	for (const auto& PosVal : *PosArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* Vec;
		if (PosVal->TryGetArray(Vec))
		{
			OutPositions.Add(ParseBoneVector(*Vec));
		}
		else
		{
			OutPositions.Add(FVector::ZeroVector);
		}
	}

	// Parse rotations (required)
	const TArray<TSharedPtr<FJsonValue>>* RotArray;
	if (!Obj->TryGetArrayField(TEXT("rotations"), RotArray) || RotArray->Num() == 0)
	{
		OutResults.Add(FString::Printf(TEXT("! %s: '%s' missing or empty 'rotations'"), *OpName, *OutBoneName));
		return false;
	}

	for (const auto& RotVal : *RotArray)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr;
		if (RotVal->TryGetArray(Arr))
		{
			OutRotations.Add(ParseRotationFromArray(*Arr));
		}
		else
		{
			OutRotations.Add(FQuat::Identity);
		}
	}

	// Parse scales (optional — defaults to [1,1,1])
	const TArray<TSharedPtr<FJsonValue>>* ScaleArray;
	if (Obj->TryGetArrayField(TEXT("scales"), ScaleArray) && ScaleArray->Num() > 0)
	{
		for (const auto& ScaleVal : *ScaleArray)
		{
			const TArray<TSharedPtr<FJsonValue>>* Vec;
			if (ScaleVal->TryGetArray(Vec))
			{
				OutScales.Add(ParseBoneVector(*Vec, FVector::OneVector));
			}
			else
			{
				OutScales.Add(FVector::OneVector);
			}
		}
	}
	else
	{
		// Default scales to [1,1,1] for each position key
		for (int32 i = 0; i < OutPositions.Num(); i++)
		{
			OutScales.Add(FVector::OneVector);
		}
	}

	// Validate equal lengths
	if (OutPositions.Num() != OutRotations.Num() || OutPositions.Num() != OutScales.Num())
	{
		OutResults.Add(FString::Printf(
			TEXT("! %s: '%s' array length mismatch (positions=%d, rotations=%d, scales=%d)"),
			*OpName, *OutBoneName, OutPositions.Num(), OutRotations.Num(), OutScales.Num()));
		return false;
	}

	return true;
}

int32 FEditAnimSequenceTool::SetBoneTrackKeysOp(IAnimationDataController& Controller,
                                                  const TArray<TSharedPtr<FJsonValue>>* KeysArray,
                                                  TArray<FString>& OutResults)
{
	if (!KeysArray) return 0;

	int32 Set = 0;

	for (const TSharedPtr<FJsonValue>& Value : *KeysArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Value->TryGetObject(EntryObj)) continue;

		FString BoneName;
		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;

		if (!ParseBoneKeyArrays(*EntryObj, Positions, Rotations, Scales, BoneName, OutResults,
			TEXT("set_bone_track_keys")))
		{
			continue;
		}

		if (Controller.SetBoneTrackKeys(FName(*BoneName), Positions, Rotations, Scales))
		{
			OutResults.Add(FString::Printf(TEXT("= BoneTrack '%s': set %d keys"), *BoneName, Positions.Num()));
			Set++;
		}
		else
		{
			OutResults.Add(FString::Printf(
				TEXT("! set_bone_track_keys: failed on '%s' (track may not exist — use add_bone_tracks first)"),
				*BoneName));
		}
	}

	return Set;
}

int32 FEditAnimSequenceTool::UpdateBoneTrackKeysOp(IAnimationDataController& Controller,
                                                     const TArray<TSharedPtr<FJsonValue>>* KeysArray,
                                                     TArray<FString>& OutResults)
{
	if (!KeysArray) return 0;

	int32 Updated = 0;

	for (const TSharedPtr<FJsonValue>& Value : *KeysArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Value->TryGetObject(EntryObj)) continue;

		double StartFrameVal = 0.0;
		if (!(*EntryObj)->TryGetNumberField(TEXT("start_frame"), StartFrameVal))
		{
			OutResults.Add(TEXT("! update_bone_track_keys: missing 'start_frame'"));
			continue;
		}
		int32 StartFrame = static_cast<int32>(StartFrameVal);

		FString BoneName;
		TArray<FVector> Positions;
		TArray<FQuat> Rotations;
		TArray<FVector> Scales;

		if (!ParseBoneKeyArrays(*EntryObj, Positions, Rotations, Scales, BoneName, OutResults,
			TEXT("update_bone_track_keys")))
		{
			continue;
		}

		// FInt32Range: inclusive lower, exclusive upper
		FInt32Range KeyRange(StartFrame, StartFrame + Positions.Num());

		if (Controller.UpdateBoneTrackKeys(FName(*BoneName), KeyRange, Positions, Rotations, Scales))
		{
			OutResults.Add(FString::Printf(TEXT("= BoneTrack '%s': updated frames %d-%d (%d keys)"),
				*BoneName, StartFrame, StartFrame + Positions.Num() - 1, Positions.Num()));
			Updated++;
		}
		else
		{
			OutResults.Add(FString::Printf(
				TEXT("! update_bone_track_keys: failed on '%s' (check track exists and range is valid)"),
				*BoneName));
		}
	}

	return Updated;
}
