// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditSkeletonTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/BlendProfile.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static FVector ParseVector(const TArray<TSharedPtr<FJsonValue>>* Arr, const FVector& Default = FVector::ZeroVector)
{
	if (!Arr || Arr->Num() < 3) return Default;
	return FVector(
		(*Arr)[0]->AsNumber(),
		(*Arr)[1]->AsNumber(),
		(*Arr)[2]->AsNumber()
	);
}

static FRotator ParseRotator(const TArray<TSharedPtr<FJsonValue>>* Arr, const FRotator& Default = FRotator::ZeroRotator)
{
	if (!Arr || Arr->Num() < 3) return Default;
	return FRotator(
		(*Arr)[0]->AsNumber(),  // Pitch
		(*Arr)[1]->AsNumber(),  // Yaw
		(*Arr)[2]->AsNumber()   // Roll
	);
}

EBoneTranslationRetargetingMode::Type FEditSkeletonTool::ParseRetargetingMode(const FString& Mode)
{
	if (Mode.Equals(TEXT("skeleton"), ESearchCase::IgnoreCase))
		return EBoneTranslationRetargetingMode::Skeleton;
	if (Mode.Equals(TEXT("animation_scaled"), ESearchCase::IgnoreCase))
		return EBoneTranslationRetargetingMode::AnimationScaled;
	if (Mode.Equals(TEXT("animation_relative"), ESearchCase::IgnoreCase))
		return EBoneTranslationRetargetingMode::AnimationRelative;
	if (Mode.Equals(TEXT("orient_and_scale"), ESearchCase::IgnoreCase))
		return EBoneTranslationRetargetingMode::OrientAndScale;
	return EBoneTranslationRetargetingMode::Animation;
}

static EBlendProfileMode ParseBlendProfileMode(const FString& Mode)
{
	if (Mode.Equals(TEXT("weight_factor"), ESearchCase::IgnoreCase))
		return EBlendProfileMode::WeightFactor;
	if (Mode.Equals(TEXT("blend_mask"), ESearchCase::IgnoreCase))
		return EBlendProfileMode::BlendMask;
	return EBlendProfileMode::TimeFactor;
}

// ─────────────────────────────────────────────────────────────────────────────
// Schema
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FEditSkeletonTool::GetInputSchema() const
{
	auto Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	auto Props = MakeShared<FJsonObject>();

	// Asset identification
	auto NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Skeleton asset name or full path"));
	Props->SetObjectField(TEXT("name"), NameProp);

	auto PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (default: /Game)"));
	Props->SetObjectField(TEXT("path"), PathProp);

	// Socket operations
	auto AddSocketsProp = MakeShared<FJsonObject>();
	AddSocketsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSocketsProp->SetStringField(TEXT("description"),
		TEXT("[{name, bone, location?: [x,y,z], rotation?: [pitch,yaw,roll], scale?: [x,y,z], force_always_animated?}]"));
	Props->SetObjectField(TEXT("add_sockets"), AddSocketsProp);

	auto RemoveSocketsProp = MakeShared<FJsonObject>();
	RemoveSocketsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSocketsProp->SetStringField(TEXT("description"), TEXT("[\"socket1\", \"socket2\"]"));
	Props->SetObjectField(TEXT("remove_sockets"), RemoveSocketsProp);

	auto EditSocketsProp = MakeShared<FJsonObject>();
	EditSocketsProp->SetStringField(TEXT("type"), TEXT("array"));
	EditSocketsProp->SetStringField(TEXT("description"),
		TEXT("[{name, bone?, location?, rotation?, scale?, force_always_animated?}]"));
	Props->SetObjectField(TEXT("edit_sockets"), EditSocketsProp);

	// Virtual bones
	auto AddVBProp = MakeShared<FJsonObject>();
	AddVBProp->SetStringField(TEXT("type"), TEXT("array"));
	AddVBProp->SetStringField(TEXT("description"),
		TEXT("[{source_bone, target_bone, name?}] — name must start with 'VB ' prefix (space, not underscore). "
			"If name is omitted, one is auto-generated as 'VB source_target'. Use rename_virtual_bone to rename after."));
	Props->SetObjectField(TEXT("add_virtual_bones"), AddVBProp);

	auto RemoveVBProp = MakeShared<FJsonObject>();
	RemoveVBProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveVBProp->SetStringField(TEXT("description"), TEXT("[\"VB bone1\", \"VB bone2\"] — names include the 'VB ' prefix (with space)"));
	Props->SetObjectField(TEXT("remove_virtual_bones"), RemoveVBProp);

	auto RenameVBProp = MakeShared<FJsonObject>();
	RenameVBProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameVBProp->SetStringField(TEXT("description"), TEXT("{old_name, new_name} — new_name must also start with 'VB ' prefix (with space)"));
	Props->SetObjectField(TEXT("rename_virtual_bone"), RenameVBProp);

	// Retargeting
	auto RetargetProp = MakeShared<FJsonObject>();
	RetargetProp->SetStringField(TEXT("type"), TEXT("array"));
	RetargetProp->SetStringField(TEXT("description"),
		TEXT("[{bone, mode: animation|skeleton|animation_scaled|animation_relative|orient_and_scale, children_too?}]"));
	Props->SetObjectField(TEXT("set_retargeting_mode"), RetargetProp);

	// Slots
	auto AddSlotsProp = MakeShared<FJsonObject>();
	AddSlotsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSlotsProp->SetStringField(TEXT("description"), TEXT("[\"SlotName1\", \"SlotName2\"]"));
	Props->SetObjectField(TEXT("add_slots"), AddSlotsProp);

	auto AddSlotGroupsProp = MakeShared<FJsonObject>();
	AddSlotGroupsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSlotGroupsProp->SetStringField(TEXT("description"), TEXT("[\"GroupName1\"]"));
	Props->SetObjectField(TEXT("add_slot_groups"), AddSlotGroupsProp);

	auto SetSlotGroupProp = MakeShared<FJsonObject>();
	SetSlotGroupProp->SetStringField(TEXT("type"), TEXT("array"));
	SetSlotGroupProp->SetStringField(TEXT("description"), TEXT("[{slot, group}]"));
	Props->SetObjectField(TEXT("set_slot_group"), SetSlotGroupProp);

	auto RemoveSlotsProp = MakeShared<FJsonObject>();
	RemoveSlotsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSlotsProp->SetStringField(TEXT("description"), TEXT("[\"SlotName1\"]"));
	Props->SetObjectField(TEXT("remove_slots"), RemoveSlotsProp);

	auto RemoveSlotGroupsProp = MakeShared<FJsonObject>();
	RemoveSlotGroupsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSlotGroupsProp->SetStringField(TEXT("description"), TEXT("[\"GroupName1\"]"));
	Props->SetObjectField(TEXT("remove_slot_groups"), RemoveSlotGroupsProp);

	auto RenameSlotProp = MakeShared<FJsonObject>();
	RenameSlotProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameSlotProp->SetStringField(TEXT("description"), TEXT("{old_name, new_name}"));
	Props->SetObjectField(TEXT("rename_slot"), RenameSlotProp);

	// Curves
	auto AddCurvesProp = MakeShared<FJsonObject>();
	AddCurvesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddCurvesProp->SetStringField(TEXT("description"), TEXT("[{name, material?, morph_target?}]"));
	Props->SetObjectField(TEXT("add_curves"), AddCurvesProp);

	auto RemoveCurvesProp = MakeShared<FJsonObject>();
	RemoveCurvesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveCurvesProp->SetStringField(TEXT("description"), TEXT("[\"curve1\", \"curve2\"]"));
	Props->SetObjectField(TEXT("remove_curves"), RemoveCurvesProp);

	auto RenameCurveProp = MakeShared<FJsonObject>();
	RenameCurveProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameCurveProp->SetStringField(TEXT("description"), TEXT("{old_name, new_name}"));
	Props->SetObjectField(TEXT("rename_curve"), RenameCurveProp);

	auto SetCurveFlagsProp = MakeShared<FJsonObject>();
	SetCurveFlagsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetCurveFlagsProp->SetStringField(TEXT("description"), TEXT("[{name, material?, morph_target?}]"));
	Props->SetObjectField(TEXT("set_curve_flags"), SetCurveFlagsProp);

	// Blend profiles
	auto AddBPProp = MakeShared<FJsonObject>();
	AddBPProp->SetStringField(TEXT("type"), TEXT("array"));
	AddBPProp->SetStringField(TEXT("description"), TEXT("[{name, mode?: time_factor|weight_factor|blend_mask}]"));
	Props->SetObjectField(TEXT("add_blend_profiles"), AddBPProp);

	auto RemoveBPProp = MakeShared<FJsonObject>();
	RemoveBPProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveBPProp->SetStringField(TEXT("description"), TEXT("[\"profile1\"]"));
	Props->SetObjectField(TEXT("remove_blend_profiles"), RemoveBPProp);

	auto RenameBPProp = MakeShared<FJsonObject>();
	RenameBPProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameBPProp->SetStringField(TEXT("description"), TEXT("{old_name, new_name}"));
	Props->SetObjectField(TEXT("rename_blend_profile"), RenameBPProp);

	auto SetBPBonesProp = MakeShared<FJsonObject>();
	SetBPBonesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetBPBonesProp->SetStringField(TEXT("description"), TEXT("[{profile, bone, scale, recurse?}]"));
	Props->SetObjectField(TEXT("set_blend_profile_bones"), SetBPBonesProp);

	auto RemoveBPBonesProp = MakeShared<FJsonObject>();
	RemoveBPBonesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveBPBonesProp->SetStringField(TEXT("description"), TEXT("[{profile, bone}]"));
	Props->SetObjectField(TEXT("remove_blend_profile_bones"), RemoveBPBonesProp);

	// Notify names
	auto AddNotifyProp = MakeShared<FJsonObject>();
	AddNotifyProp->SetStringField(TEXT("type"), TEXT("array"));
	AddNotifyProp->SetStringField(TEXT("description"), TEXT("[\"NotifyName1\"]"));
	Props->SetObjectField(TEXT("add_notify_names"), AddNotifyProp);

	auto RemoveNotifyProp = MakeShared<FJsonObject>();
	RemoveNotifyProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveNotifyProp->SetStringField(TEXT("description"), TEXT("[\"NotifyName1\"]"));
	Props->SetObjectField(TEXT("remove_notify_names"), RemoveNotifyProp);

	auto RenameNotifyProp = MakeShared<FJsonObject>();
	RenameNotifyProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameNotifyProp->SetStringField(TEXT("description"), TEXT("{old_name, new_name}"));
	Props->SetObjectField(TEXT("rename_notify_name"), RenameNotifyProp);

	// Marker names
	auto AddMarkerProp = MakeShared<FJsonObject>();
	AddMarkerProp->SetStringField(TEXT("type"), TEXT("array"));
	AddMarkerProp->SetStringField(TEXT("description"), TEXT("[\"MarkerName1\"]"));
	Props->SetObjectField(TEXT("add_marker_names"), AddMarkerProp);

	auto RemoveMarkerProp = MakeShared<FJsonObject>();
	RemoveMarkerProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveMarkerProp->SetStringField(TEXT("description"), TEXT("[\"MarkerName1\"]"));
	Props->SetObjectField(TEXT("remove_marker_names"), RemoveMarkerProp);

	auto RenameMarkerProp = MakeShared<FJsonObject>();
	RenameMarkerProp->SetStringField(TEXT("type"), TEXT("object"));
	RenameMarkerProp->SetStringField(TEXT("description"), TEXT("{old_name, new_name}"));
	Props->SetObjectField(TEXT("rename_marker_name"), RenameMarkerProp);

	// Compatible skeletons
	auto AddCompatProp = MakeShared<FJsonObject>();
	AddCompatProp->SetStringField(TEXT("type"), TEXT("array"));
	AddCompatProp->SetStringField(TEXT("description"), TEXT("[\"/Game/Path/ToSkeleton\"]"));
	Props->SetObjectField(TEXT("add_compatible_skeletons"), AddCompatProp);

	auto RemoveCompatProp = MakeShared<FJsonObject>();
	RemoveCompatProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveCompatProp->SetStringField(TEXT("description"), TEXT("[\"/Game/Path/ToSkeleton\"]"));
	Props->SetObjectField(TEXT("remove_compatible_skeletons"), RemoveCompatProp);

	Schema->SetObjectField(TEXT("properties"), Props);

	auto Required = TArray<TSharedPtr<FJsonValue>>();
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

FToolResult FEditSkeletonTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
		return FToolResult::Fail(TEXT("Invalid arguments"));

	// --- Load asset ---
	FString Name;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
		return FToolResult::Fail(TEXT("Missing required 'name' field"));

	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);

	FString AssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	USkeleton* Skel = LoadObject<USkeleton>(nullptr, *AssetPath);
	if (!Skel)
	{
		Skel = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(Name);
	}
	if (!Skel)
		return FToolResult::Fail(FString::Printf(TEXT("Skeleton not found: %s"), *AssetPath));

	// --- Transaction + Modify ---
	FScopedTransaction Transaction(FText::FromString(TEXT("AI Edit Skeleton")));
	Skel->Modify();

	TArray<FString> Results;
	int32 TotalOps = 0;

	// ── Socket Operations ────────────────────────────────────────────────
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;

	if (Args->TryGetArrayField(TEXT("add_sockets"), Arr))
		TotalOps += AddSockets(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_sockets"), Arr))
		TotalOps += RemoveSockets(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("edit_sockets"), Arr))
		TotalOps += EditSockets(Skel, Arr, Results);

	// ── Virtual Bone Operations ──────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_virtual_bones"), Arr))
		TotalOps += AddVirtualBones(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_virtual_bones"), Arr))
		TotalOps += RemoveVirtualBones(Skel, Arr, Results);

	const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
	if (Args->TryGetObjectField(TEXT("rename_virtual_bone"), ObjPtr))
	{
		if (RenameVirtualBone(Skel, *ObjPtr, Results)) TotalOps++;
	}

	// ── Retargeting ──────────────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("set_retargeting_mode"), Arr))
		TotalOps += SetRetargetingModes(Skel, Arr, Results);

	// ── Slot Operations ──────────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_slots"), Arr))
		TotalOps += AddSlots(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("add_slot_groups"), Arr))
		TotalOps += AddSlotGroups(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("set_slot_group"), Arr))
		TotalOps += SetSlotGroups(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_slots"), Arr))
		TotalOps += RemoveSlots(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_slot_groups"), Arr))
		TotalOps += RemoveSlotGroups(Skel, Arr, Results);

	if (Args->TryGetObjectField(TEXT("rename_slot"), ObjPtr))
	{
		if (RenameSlot(Skel, *ObjPtr, Results)) TotalOps++;
	}

	// ── Curve Metadata ───────────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_curves"), Arr))
		TotalOps += AddCurves(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_curves"), Arr))
		TotalOps += RemoveCurves(Skel, Arr, Results);

	if (Args->TryGetObjectField(TEXT("rename_curve"), ObjPtr))
	{
		if (RenameCurve(Skel, *ObjPtr, Results)) TotalOps++;
	}

	if (Args->TryGetArrayField(TEXT("set_curve_flags"), Arr))
		TotalOps += SetCurveFlags(Skel, Arr, Results);

	// ── Blend Profiles ───────────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_blend_profiles"), Arr))
		TotalOps += AddBlendProfiles(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_blend_profiles"), Arr))
		TotalOps += RemoveBlendProfiles(Skel, Arr, Results);

	if (Args->TryGetObjectField(TEXT("rename_blend_profile"), ObjPtr))
	{
		if (RenameBlendProfile(Skel, *ObjPtr, Results)) TotalOps++;
	}

	if (Args->TryGetArrayField(TEXT("set_blend_profile_bones"), Arr))
		TotalOps += SetBlendProfileBones(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_blend_profile_bones"), Arr))
		TotalOps += RemoveBlendProfileBones(Skel, Arr, Results);

	// ── Notify Names ─────────────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_notify_names"), Arr))
		TotalOps += AddNotifyNames(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_notify_names"), Arr))
		TotalOps += RemoveNotifyNames(Skel, Arr, Results);

	if (Args->TryGetObjectField(TEXT("rename_notify_name"), ObjPtr))
	{
		if (RenameNotifyName(Skel, *ObjPtr, Results)) TotalOps++;
	}

	// ── Marker Names ─────────────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_marker_names"), Arr))
		TotalOps += AddMarkerNames(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_marker_names"), Arr))
		TotalOps += RemoveMarkerNames(Skel, Arr, Results);

	if (Args->TryGetObjectField(TEXT("rename_marker_name"), ObjPtr))
	{
		if (RenameMarkerName(Skel, *ObjPtr, Results)) TotalOps++;
	}

	// ── Compatible Skeletons ─────────────────────────────────────────────
	if (Args->TryGetArrayField(TEXT("add_compatible_skeletons"), Arr))
		TotalOps += AddCompatibleSkeletons(Skel, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_compatible_skeletons"), Arr))
		TotalOps += RemoveCompatibleSkeletons(Skel, Arr, Results);

	// ── Finalize ─────────────────────────────────────────────────────────
	if (TotalOps == 0)
		return FToolResult::Fail(TEXT("No valid operations specified"));

	Skel->PostEditChange();
	Skel->MarkPackageDirty();

	FString Summary = FString::Printf(TEXT("Skeleton '%s': %d operations completed.\n"),
		*Skel->GetName(), TotalOps);
	for (const FString& R : Results)
	{
		Summary += R + TEXT("\n");
	}

	return FToolResult::Ok(Summary);
}

// ─────────────────────────────────────────────────────────────────────────────
// Socket Operations
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddSockets(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	const FReferenceSkeleton& RefSkel = Skel->GetReferenceSkeleton();

	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString SocketName, BoneName;
		if (!Obj->TryGetStringField(TEXT("name"), SocketName) || SocketName.IsEmpty())
		{
			Out.Add(TEXT("- Skipped socket: missing 'name'"));
			continue;
		}
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			Out.Add(FString::Printf(TEXT("- Skipped socket '%s': missing 'bone'"), *SocketName));
			continue;
		}

		// Validate bone exists
		if (RefSkel.FindBoneIndex(FName(*BoneName)) == INDEX_NONE)
		{
			Out.Add(FString::Printf(TEXT("- Skipped socket '%s': bone '%s' not found"), *SocketName, *BoneName));
			continue;
		}

		// Check duplicate
		if (Skel->FindSocket(FName(*SocketName)) != nullptr)
		{
			Out.Add(FString::Printf(TEXT("- Skipped socket '%s': already exists"), *SocketName));
			continue;
		}

		USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skel);
		Socket->SocketName = FName(*SocketName);
		Socket->BoneName = FName(*BoneName);

		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("location"), VecArr))
			Socket->RelativeLocation = ParseVector(VecArr);
		if (Obj->TryGetArrayField(TEXT("rotation"), VecArr))
			Socket->RelativeRotation = ParseRotator(VecArr);
		if (Obj->TryGetArrayField(TEXT("scale"), VecArr))
			Socket->RelativeScale = ParseVector(VecArr, FVector::OneVector);

		bool bForceAnimated = true;
		if (Obj->TryGetBoolField(TEXT("force_always_animated"), bForceAnimated))
			Socket->bForceAlwaysAnimated = bForceAnimated;

		Skel->Sockets.Add(Socket);
		Out.Add(FString::Printf(TEXT("- Added socket '%s' on bone '%s'"), *SocketName, *BoneName));
		Count++;
	}

	return Count;
}

int32 FEditSkeletonTool::RemoveSockets(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	// Collect indices in reverse order to avoid invalidation
	TArray<int32> IndicesToRemove;

	for (const auto& Val : *Arr)
	{
		FString SocketName = Val->AsString();
		if (SocketName.IsEmpty()) continue;

		int32 Index = INDEX_NONE;
		USkeletalMeshSocket* Found = Skel->FindSocketAndIndex(FName(*SocketName), Index);
		if (!Found || Index == INDEX_NONE)
		{
			Out.Add(FString::Printf(TEXT("- Socket '%s' not found"), *SocketName));
			continue;
		}

		IndicesToRemove.AddUnique(Index);
		Out.Add(FString::Printf(TEXT("- Removed socket '%s'"), *SocketName));
	}

	// Sort descending so removal doesn't shift indices
	IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });
	for (int32 Idx : IndicesToRemove)
	{
		Skel->Sockets.RemoveAt(Idx);
	}

	return IndicesToRemove.Num();
}

int32 FEditSkeletonTool::EditSockets(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	const FReferenceSkeleton& RefSkel = Skel->GetReferenceSkeleton();

	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString SocketName;
		if (!Obj->TryGetStringField(TEXT("name"), SocketName) || SocketName.IsEmpty()) continue;

		USkeletalMeshSocket* Socket = Skel->FindSocket(FName(*SocketName));
		if (!Socket)
		{
			Out.Add(FString::Printf(TEXT("- Socket '%s' not found for editing"), *SocketName));
			continue;
		}

		FString NewBone;
		if (Obj->TryGetStringField(TEXT("bone"), NewBone) && !NewBone.IsEmpty())
		{
			if (RefSkel.FindBoneIndex(FName(*NewBone)) == INDEX_NONE)
			{
				Out.Add(FString::Printf(TEXT("- Socket '%s': bone '%s' not found, skipping bone change"), *SocketName, *NewBone));
			}
			else
			{
				Socket->BoneName = FName(*NewBone);
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* VecArr = nullptr;
		if (Obj->TryGetArrayField(TEXT("location"), VecArr))
			Socket->RelativeLocation = ParseVector(VecArr);
		if (Obj->TryGetArrayField(TEXT("rotation"), VecArr))
			Socket->RelativeRotation = ParseRotator(VecArr);
		if (Obj->TryGetArrayField(TEXT("scale"), VecArr))
			Socket->RelativeScale = ParseVector(VecArr, FVector::OneVector);

		bool bForceAnimated;
		if (Obj->TryGetBoolField(TEXT("force_always_animated"), bForceAnimated))
			Socket->bForceAlwaysAnimated = bForceAnimated;

		Out.Add(FString::Printf(TEXT("- Edited socket '%s'"), *SocketName));
		Count++;
	}

	return Count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual Bones
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddVirtualBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;

	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString Source, Target;
		if (!Obj->TryGetStringField(TEXT("source_bone"), Source) || Source.IsEmpty())
		{
			Out.Add(TEXT("- Skipped virtual bone: missing 'source_bone'"));
			continue;
		}
		if (!Obj->TryGetStringField(TEXT("target_bone"), Target) || Target.IsEmpty())
		{
			Out.Add(TEXT("- Skipped virtual bone: missing 'target_bone'"));
			continue;
		}

		FString VBName;
		if (Obj->TryGetStringField(TEXT("name"), VBName) && !VBName.IsEmpty())
		{
			if (Skel->AddNewNamedVirtualBone(FName(*Source), FName(*Target), FName(*VBName)))
			{
				Out.Add(FString::Printf(TEXT("- Added virtual bone '%s' (%s -> %s)"), *VBName, *Source, *Target));
				Count++;
			}
			else
			{
				Out.Add(FString::Printf(TEXT("- Failed to add virtual bone '%s' (check prefix/bones exist)"), *VBName));
			}
		}
		else
		{
			FName GeneratedName;
			if (Skel->AddNewVirtualBone(FName(*Source), FName(*Target), GeneratedName))
			{
				Out.Add(FString::Printf(TEXT("- Added virtual bone '%s' (%s -> %s)"),
					*GeneratedName.ToString(), *Source, *Target));
				Count++;
			}
			else
			{
				Out.Add(FString::Printf(TEXT("- Failed to add virtual bone (%s -> %s)"), *Source, *Target));
			}
		}
	}

	return Count;
}

int32 FEditSkeletonTool::RemoveVirtualBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	TArray<FName> NamesToRemove;
	for (const auto& Val : *Arr)
	{
		FString BoneName = Val->AsString();
		if (!BoneName.IsEmpty())
		{
			NamesToRemove.Add(FName(*BoneName));
		}
	}

	if (NamesToRemove.Num() > 0)
	{
		Skel->RemoveVirtualBones(NamesToRemove);
		for (const FName& N : NamesToRemove)
		{
			Out.Add(FString::Printf(TEXT("- Removed virtual bone '%s'"), *N.ToString()));
		}
	}

	return NamesToRemove.Num();
}

bool FEditSkeletonTool::RenameVirtualBone(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out)
{
	FString OldName, NewName;
	if (!Obj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		Out.Add(TEXT("- rename_virtual_bone: missing 'old_name'"));
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		Out.Add(TEXT("- rename_virtual_bone: missing 'new_name'"));
		return false;
	}

	Skel->RenameVirtualBone(FName(*OldName), FName(*NewName));
	Out.Add(FString::Printf(TEXT("- Renamed virtual bone '%s' -> '%s'"), *OldName, *NewName));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Retargeting
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::SetRetargetingModes(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	const FReferenceSkeleton& RefSkel = Skel->GetReferenceSkeleton();

	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString BoneName, ModeStr;
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			Out.Add(TEXT("- Skipped retargeting: missing 'bone'"));
			continue;
		}
		if (!Obj->TryGetStringField(TEXT("mode"), ModeStr) || ModeStr.IsEmpty())
		{
			Out.Add(FString::Printf(TEXT("- Skipped retargeting for '%s': missing 'mode'"), *BoneName));
			continue;
		}

		int32 BoneIndex = RefSkel.FindBoneIndex(FName(*BoneName));
		if (BoneIndex == INDEX_NONE)
		{
			Out.Add(FString::Printf(TEXT("- Bone '%s' not found for retargeting"), *BoneName));
			continue;
		}

		EBoneTranslationRetargetingMode::Type Mode = ParseRetargetingMode(ModeStr);
		bool bChildrenToo = false;
		Obj->TryGetBoolField(TEXT("children_too"), bChildrenToo);

		Skel->SetBoneTranslationRetargetingMode(BoneIndex, Mode, bChildrenToo);
		Out.Add(FString::Printf(TEXT("- Set retargeting mode '%s' on bone '%s'%s"),
			*ModeStr, *BoneName, bChildrenToo ? TEXT(" (+ children)") : TEXT("")));
		Count++;
	}

	return Count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Slot Operations
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddSlots(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString SlotName = Val->AsString();
		if (SlotName.IsEmpty()) continue;

		if (Skel->RegisterSlotNode(FName(*SlotName)))
		{
			Out.Add(FString::Printf(TEXT("- Registered slot '%s'"), *SlotName));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Slot '%s' already registered"), *SlotName));
		}
	}
	return Count;
}

int32 FEditSkeletonTool::AddSlotGroups(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString GroupName = Val->AsString();
		if (GroupName.IsEmpty()) continue;

		if (Skel->AddSlotGroupName(FName(*GroupName)))
		{
			Out.Add(FString::Printf(TEXT("- Added slot group '%s'"), *GroupName));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Slot group '%s' already exists"), *GroupName));
		}
	}
	return Count;
}

int32 FEditSkeletonTool::SetSlotGroups(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString SlotName, GroupName;
		if (!Obj->TryGetStringField(TEXT("slot"), SlotName) || SlotName.IsEmpty()) continue;
		if (!Obj->TryGetStringField(TEXT("group"), GroupName) || GroupName.IsEmpty()) continue;

		Skel->SetSlotGroupName(FName(*SlotName), FName(*GroupName));
		Out.Add(FString::Printf(TEXT("- Moved slot '%s' to group '%s'"), *SlotName, *GroupName));
		Count++;
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveSlots(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString SlotName = Val->AsString();
		if (SlotName.IsEmpty()) continue;

		Skel->RemoveSlotName(FName(*SlotName));
		Out.Add(FString::Printf(TEXT("- Removed slot '%s'"), *SlotName));
		Count++;
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveSlotGroups(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString GroupName = Val->AsString();
		if (GroupName.IsEmpty()) continue;

		Skel->RemoveSlotGroup(FName(*GroupName));
		Out.Add(FString::Printf(TEXT("- Removed slot group '%s'"), *GroupName));
		Count++;
	}
	return Count;
}

bool FEditSkeletonTool::RenameSlot(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out)
{
	FString OldName, NewName;
	if (!Obj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		Out.Add(TEXT("- rename_slot: missing 'old_name'"));
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		Out.Add(TEXT("- rename_slot: missing 'new_name'"));
		return false;
	}

	// RenameSlotName has a check() assertion that the old slot exists — validate first
	bool bSlotExists = false;
	const TArray<FAnimSlotGroup>& SlotGroups = Skel->GetSlotGroups();
	for (const FAnimSlotGroup& Group : SlotGroups)
	{
		if (Group.SlotNames.Contains(FName(*OldName)))
		{
			bSlotExists = true;
			break;
		}
	}

	if (!bSlotExists)
	{
		Out.Add(FString::Printf(TEXT("- rename_slot: slot '%s' not found"), *OldName));
		return false;
	}

	Skel->RenameSlotName(FName(*OldName), FName(*NewName));
	Out.Add(FString::Printf(TEXT("- Renamed slot '%s' -> '%s'"), *OldName, *NewName));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Curve Metadata
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddCurves(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString CurveName;
		if (!Obj->TryGetStringField(TEXT("name"), CurveName) || CurveName.IsEmpty()) continue;

		FName CurveFName(*CurveName);
		if (Skel->AddCurveMetaData(CurveFName))
		{
			bool bMaterial = false, bMorphTarget = false;
			Obj->TryGetBoolField(TEXT("material"), bMaterial);
			Obj->TryGetBoolField(TEXT("morph_target"), bMorphTarget);


#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			if (bMaterial) Skel->SetCurveMetaDataMaterial(CurveFName, true);
			if (bMorphTarget) Skel->SetCurveMetaDataMorphTarget(CurveFName, true);
#endif

			Out.Add(FString::Printf(TEXT("- Added curve '%s'%s%s"), *CurveName,
				bMaterial ? TEXT(" [material]") : TEXT(""),
				bMorphTarget ? TEXT(" [morph_target]") : TEXT("")));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Curve '%s' already exists"), *CurveName));
		}
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveCurves(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString CurveName = Val->AsString();
		if (CurveName.IsEmpty()) continue;

		if (Skel->RemoveCurveMetaData(FName(*CurveName)))
		{
			Out.Add(FString::Printf(TEXT("- Removed curve '%s'"), *CurveName));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Curve '%s' not found"), *CurveName));
		}
	}
	return Count;
}

bool FEditSkeletonTool::RenameCurve(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out)
{
	FString OldName, NewName;
	if (!Obj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		Out.Add(TEXT("- rename_curve: missing 'old_name'"));
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		Out.Add(TEXT("- rename_curve: missing 'new_name'"));
		return false;
	}

	if (Skel->RenameCurveMetaData(FName(*OldName), FName(*NewName)))
	{
		Out.Add(FString::Printf(TEXT("- Renamed curve '%s' -> '%s'"), *OldName, *NewName));
		return true;
	}

	Out.Add(FString::Printf(TEXT("- Failed to rename curve '%s'"), *OldName));
	return false;
}

int32 FEditSkeletonTool::SetCurveFlags(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString CurveName;
		if (!Obj->TryGetStringField(TEXT("name"), CurveName) || CurveName.IsEmpty()) continue;

		FName CurveFName(*CurveName);


#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		bool bMaterial;
		if (Obj->TryGetBoolField(TEXT("material"), bMaterial))
			Skel->SetCurveMetaDataMaterial(CurveFName, bMaterial);

		bool bMorphTarget;
		if (Obj->TryGetBoolField(TEXT("morph_target"), bMorphTarget))
			Skel->SetCurveMetaDataMorphTarget(CurveFName, bMorphTarget);
#endif

		Out.Add(FString::Printf(TEXT("- Updated curve flags for '%s'"), *CurveName));
		Count++;
	}
	return Count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Blend Profiles
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddBlendProfiles(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString ProfileName;
		if (!Obj->TryGetStringField(TEXT("name"), ProfileName) || ProfileName.IsEmpty()) continue;

		// Check if already exists
		if (Skel->GetBlendProfile(FName(*ProfileName)) != nullptr)
		{
			Out.Add(FString::Printf(TEXT("- Blend profile '%s' already exists"), *ProfileName));
			continue;
		}

		UBlendProfile* Profile = Skel->CreateNewBlendProfile(FName(*ProfileName));
		if (Profile)
		{
			FString ModeStr;
			if (Obj->TryGetStringField(TEXT("mode"), ModeStr) && !ModeStr.IsEmpty())
			{
				Profile->Mode = ParseBlendProfileMode(ModeStr);
			}

			Out.Add(FString::Printf(TEXT("- Created blend profile '%s'"), *ProfileName));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Failed to create blend profile '%s'"), *ProfileName));
		}
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveBlendProfiles(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString ProfileName = Val->AsString();
		if (ProfileName.IsEmpty()) continue;

		FName ProfFName(*ProfileName);
		int32 RemovedIdx = INDEX_NONE;

		for (int32 i = 0; i < Skel->BlendProfiles.Num(); ++i)
		{
			UBlendProfile* Profile = Skel->BlendProfiles[i];
			if (Profile && Profile->GetFName() == ProfFName)
			{
				RemovedIdx = i;
				break;
			}
		}

		if (RemovedIdx != INDEX_NONE)
		{
			Skel->BlendProfiles.RemoveAt(RemovedIdx);
			Out.Add(FString::Printf(TEXT("- Removed blend profile '%s'"), *ProfileName));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Blend profile '%s' not found"), *ProfileName));
		}
	}
	return Count;
}

bool FEditSkeletonTool::RenameBlendProfile(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out)
{
	FString OldName, NewName;
	if (!Obj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		Out.Add(TEXT("- rename_blend_profile: missing 'old_name'"));
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		Out.Add(TEXT("- rename_blend_profile: missing 'new_name'"));
		return false;
	}

	UBlendProfile* Result = Skel->RenameBlendProfile(FName(*OldName), FName(*NewName));
	if (Result)
	{
		Out.Add(FString::Printf(TEXT("- Renamed blend profile '%s' -> '%s'"), *OldName, *NewName));
		return true;
	}

	Out.Add(FString::Printf(TEXT("- Failed to rename blend profile '%s'"), *OldName));
	return false;
}

int32 FEditSkeletonTool::SetBlendProfileBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString ProfileName, BoneName;
		if (!Obj->TryGetStringField(TEXT("profile"), ProfileName) || ProfileName.IsEmpty()) continue;
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty()) continue;

		double Scale = 1.0;
		Obj->TryGetNumberField(TEXT("scale"), Scale);

		bool bRecurse = false;
		Obj->TryGetBoolField(TEXT("recurse"), bRecurse);

		UBlendProfile* Profile = Skel->GetBlendProfile(FName(*ProfileName));
		if (!Profile)
		{
			Out.Add(FString::Printf(TEXT("- Blend profile '%s' not found"), *ProfileName));
			continue;
		}

		// Use the FName overload — it handles bone lookup internally
		Profile->SetBoneBlendScale(FName(*BoneName), static_cast<float>(Scale), bRecurse, /*bCreate=*/true);
		Out.Add(FString::Printf(TEXT("- Set bone '%s' scale=%.2f in profile '%s'%s"),
			*BoneName, Scale, *ProfileName, bRecurse ? TEXT(" (recursive)") : TEXT("")));
		Count++;
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveBlendProfileBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	const FReferenceSkeleton& RefSkel = Skel->GetReferenceSkeleton();

	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
		if (!Val->TryGetObject(ObjPtr) || !ObjPtr || !(*ObjPtr).IsValid()) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString ProfileName, BoneName;
		if (!Obj->TryGetStringField(TEXT("profile"), ProfileName) || ProfileName.IsEmpty()) continue;
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty()) continue;

		UBlendProfile* Profile = Skel->GetBlendProfile(FName(*ProfileName));
		if (!Profile)
		{
			Out.Add(FString::Printf(TEXT("- Blend profile '%s' not found"), *ProfileName));
			continue;
		}

		int32 BoneIndex = RefSkel.FindBoneIndex(FName(*BoneName));
		if (BoneIndex == INDEX_NONE)
		{
			Out.Add(FString::Printf(TEXT("- Bone '%s' not found for blend profile entry removal"), *BoneName));
			continue;
		}

		Profile->RemoveEntry(BoneIndex);
		Out.Add(FString::Printf(TEXT("- Removed bone '%s' from profile '%s'"), *BoneName, *ProfileName));
		Count++;
	}
	return Count;
}

// ─────────────────────────────────────────────────────────────────────────────
// Notify Names (skeleton-level registry)
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddNotifyNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString NotifyName = Val->AsString();
		if (NotifyName.IsEmpty()) continue;

		Skel->AddNewAnimationNotify(FName(*NotifyName));
		Out.Add(FString::Printf(TEXT("- Registered notify name '%s'"), *NotifyName));
		Count++;
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveNotifyNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString NotifyName = Val->AsString();
		if (NotifyName.IsEmpty()) continue;

		Skel->RemoveAnimationNotify(FName(*NotifyName));
		Out.Add(FString::Printf(TEXT("- Removed notify name '%s'"), *NotifyName));
		Count++;
	}
	return Count;
}

bool FEditSkeletonTool::RenameNotifyName(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out)
{
	FString OldName, NewName;
	if (!Obj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		Out.Add(TEXT("- rename_notify_name: missing 'old_name'"));
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		Out.Add(TEXT("- rename_notify_name: missing 'new_name'"));
		return false;
	}

	Skel->RenameAnimationNotify(FName(*OldName), FName(*NewName));
	Out.Add(FString::Printf(TEXT("- Renamed notify '%s' -> '%s'"), *OldName, *NewName));
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Marker Names (skeleton-level registry)
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddMarkerNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString MarkerName = Val->AsString();
		if (MarkerName.IsEmpty()) continue;

		Skel->RegisterMarkerName(FName(*MarkerName));
		Out.Add(FString::Printf(TEXT("- Registered marker name '%s'"), *MarkerName));
		Count++;
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveMarkerNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString MarkerName = Val->AsString();
		if (MarkerName.IsEmpty()) continue;

		if (Skel->RemoveMarkerName(FName(*MarkerName)))
		{
			Out.Add(FString::Printf(TEXT("- Removed marker name '%s'"), *MarkerName));
			Count++;
		}
		else
		{
			Out.Add(FString::Printf(TEXT("- Marker name '%s' not found"), *MarkerName));
		}
	}
	return Count;
}

bool FEditSkeletonTool::RenameMarkerName(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out)
{
	FString OldName, NewName;
	if (!Obj->TryGetStringField(TEXT("old_name"), OldName) || OldName.IsEmpty())
	{
		Out.Add(TEXT("- rename_marker_name: missing 'old_name'"));
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), NewName) || NewName.IsEmpty())
	{
		Out.Add(TEXT("- rename_marker_name: missing 'new_name'"));
		return false;
	}

	if (Skel->RenameMarkerName(FName(*OldName), FName(*NewName)))
	{
		Out.Add(FString::Printf(TEXT("- Renamed marker '%s' -> '%s'"), *OldName, *NewName));
		return true;
	}

	Out.Add(FString::Printf(TEXT("- Failed to rename marker '%s'"), *OldName));
	return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Compatible Skeletons
// ─────────────────────────────────────────────────────────────────────────────

int32 FEditSkeletonTool::AddCompatibleSkeletons(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString SkelPath = Val->AsString();
		if (SkelPath.IsEmpty()) continue;

		USkeleton* OtherSkel = LoadObject<USkeleton>(nullptr, *SkelPath);
		if (!OtherSkel)
		{
			// Try fallback
			OtherSkel = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(SkelPath);
		}
		if (!OtherSkel)
		{
			Out.Add(FString::Printf(TEXT("- Compatible skeleton not found: '%s'"), *SkelPath));
			continue;
		}

		Skel->AddCompatibleSkeleton(OtherSkel);
		Out.Add(FString::Printf(TEXT("- Added compatible skeleton '%s'"), *OtherSkel->GetName()));
		Count++;
	}
	return Count;
}

int32 FEditSkeletonTool::RemoveCompatibleSkeletons(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out)
{
	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		FString SkelPath = Val->AsString();
		if (SkelPath.IsEmpty()) continue;

		USkeleton* OtherSkel = LoadObject<USkeleton>(nullptr, *SkelPath);
		if (!OtherSkel)
		{
			OtherSkel = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(SkelPath);
		}
		if (!OtherSkel)
		{
			Out.Add(FString::Printf(TEXT("- Compatible skeleton not found: '%s'"), *SkelPath));
			continue;
		}

		Skel->RemoveCompatibleSkeleton(OtherSkel);
		Out.Add(FString::Printf(TEXT("- Removed compatible skeleton '%s'"), *OtherSkel->GetName()));
		Count++;
	}
	return Count;
}
