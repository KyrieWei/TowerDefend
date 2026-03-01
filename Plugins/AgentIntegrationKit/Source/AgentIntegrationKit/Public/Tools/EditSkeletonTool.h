// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class USkeleton;
class UBlendProfile;
class USkeletalMeshSocket;

/**
 * Tool for editing Skeleton assets (sockets, virtual bones, retargeting, slots, curves, blend profiles)
 *
 * Skeletons are imported assets — this tool edits them, it does not create them.
 * All operations use USkeleton's public API directly.
 *
 * Parameters:
 *   - name: Skeleton asset name or path (required)
 *   - path: Asset folder path (optional, defaults to /Game)
 *
 * Socket Management:
 *   - add_sockets: [{name, bone, location?: [x,y,z], rotation?: [pitch,yaw,roll], scale?: [x,y,z], force_always_animated?}]
 *   - remove_sockets: ["socket1", "socket2"]
 *   - edit_sockets: [{name, bone?, location?, rotation?, scale?, force_always_animated?}]
 *
 * Virtual Bones:
 *   - add_virtual_bones: [{source_bone, target_bone, name?}]
 *   - remove_virtual_bones: ["VB bone1", "VB bone2"]
 *   - rename_virtual_bone: {old_name, new_name}
 *
 * Retargeting:
 *   - set_retargeting_mode: [{bone, mode: "animation"|"skeleton"|"animation_scaled"|"animation_relative"|"orient_and_scale", children_too?}]
 *
 * Slot Groups (for montage slots):
 *   - add_slots: ["SlotName1", "SlotName2"]
 *   - add_slot_groups: ["GroupName1", "GroupName2"]
 *   - set_slot_group: [{slot, group}]
 *   - remove_slots: ["SlotName1"]
 *   - remove_slot_groups: ["GroupName1"]
 *   - rename_slot: {old_name, new_name}
 *
 * Curve Metadata:
 *   - add_curves: [{name, material?, morph_target?}]
 *   - remove_curves: ["curve1", "curve2"]
 *   - rename_curve: {old_name, new_name}
 *   - set_curve_flags: [{name, material?, morph_target?}]
 *
 * Blend Profiles:
 *   - add_blend_profiles: [{name, mode?: "time_factor"|"weight_factor"|"blend_mask"}]
 *   - remove_blend_profiles: ["profile1"]
 *   - rename_blend_profile: {old_name, new_name}
 *   - set_blend_profile_bones: [{profile, bone, scale, recurse?}]
 *   - remove_blend_profile_bones: [{profile, bone}]
 *
 * Notify & Marker Names (skeleton-level registries):
 *   - add_notify_names: ["NotifyName1"]
 *   - remove_notify_names: ["NotifyName1"]
 *   - rename_notify_name: {old_name, new_name}
 *   - add_marker_names: ["MarkerName1"]
 *   - remove_marker_names: ["MarkerName1"]
 *   - rename_marker_name: {old_name, new_name}
 *
 * Compatible Skeletons:
 *   - add_compatible_skeletons: ["/Game/Path/ToSkeleton"]
 *   - remove_compatible_skeletons: ["/Game/Path/ToSkeleton"]
 */
class AGENTINTEGRATIONKIT_API FEditSkeletonTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_skeleton"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Skeleton assets: add/remove/edit sockets on bones, manage virtual bones, "
			"set per-bone retargeting modes (animation, skeleton, animation_scaled, animation_relative, orient_and_scale), "
			"create slot groups for montages, manage curve metadata with material/morph_target flags, "
			"create and configure blend profiles with per-bone blend scales, "
			"and register animation notify and sync marker names.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// Sockets
	int32 AddSockets(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveSockets(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 EditSockets(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);

	// Virtual bones
	int32 AddVirtualBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveVirtualBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	bool RenameVirtualBone(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out);

	// Retargeting
	int32 SetRetargetingModes(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);

	// Slots
	int32 AddSlots(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 AddSlotGroups(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 SetSlotGroups(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveSlots(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveSlotGroups(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	bool RenameSlot(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out);

	// Curves
	int32 AddCurves(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveCurves(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	bool RenameCurve(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out);
	int32 SetCurveFlags(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);

	// Blend profiles
	int32 AddBlendProfiles(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveBlendProfiles(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	bool RenameBlendProfile(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out);
	int32 SetBlendProfileBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveBlendProfileBones(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);

	// Notify & marker names
	int32 AddNotifyNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveNotifyNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	bool RenameNotifyName(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out);
	int32 AddMarkerNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveMarkerNames(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	bool RenameMarkerName(USkeleton* Skel, const TSharedPtr<FJsonObject>& Obj, TArray<FString>& Out);

	// Compatible skeletons
	int32 AddCompatibleSkeletons(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);
	int32 RemoveCompatibleSkeletons(USkeleton* Skel, const TArray<TSharedPtr<FJsonValue>>* Arr, TArray<FString>& Out);

	// Helpers
	static EBoneTranslationRetargetingMode::Type ParseRetargetingMode(const FString& Mode);
};
