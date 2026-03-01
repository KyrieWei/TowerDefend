// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UAnimSequence;
class IAnimationDataController;
struct FAnimationCurveIdentifier;

/**
 * Tool for editing Animation Sequence assets (notifies, curves, sync markers, properties)
 *
 * AnimSequences can be created from scratch or loaded from existing assets.
 * Uses dual architecture: IAnimationDataController for curves/tracks, direct manipulation for notifies.
 *
 * Parameters:
 *   - name: AnimSequence asset name or path (required)
 *   - path: Asset folder path (optional, defaults to /Game)
 *   - create: {skeleton, frame_rate?, num_frames?} — Create new blank AnimSequence
 *
 * Bone Track Operations (via IAnimationDataController):
 *   - add_bone_tracks: ["bone1", "bone2"] — Add bone animation tracks
 *   - set_bone_track_keys: [{bone, positions: [[x,y,z],...], rotations: [[p,y,r],...] or [[x,y,z,w],...], scales?: [[x,y,z],...]}]
 *   - update_bone_track_keys: [{bone, start_frame, positions, rotations, scales?}]
 *
 * Notify Management:
 *   - add_notifies: [{name, time, type?, duration?, track_index?, trigger_chance?, branching_point?}]
 *   - remove_notifies: [name_or_index, ...] — by name (string) or index (int)
 *   - edit_notifies: [{index, time?, duration?, track_index?, trigger_chance?}]
 *
 * Sync Markers:
 *   - add_sync_markers: [{name, time, track_index?}]
 *   - remove_sync_markers: [name_or_index, ...]
 *   - rename_sync_marker: {old_name, new_name}
 *
 * Curve Editing (via IAnimationDataController):
 *   - add_curves: [{name, type: "float"|"transform", keys?: [...]}]
 *   - remove_curves: ["curve1", "curve2"]
 *   - set_curve_keys: [{curve, type?, keys: [{time, value, interp_mode?, tangent_mode?, arrive_tangent?, leave_tangent?}]}]
 *   - set_transform_curve_keys: [{curve, keys: [{time, location, rotation, scale}]}]
 *   - rename_curve: {old_name, new_name, type?}
 *   - scale_curve: {name, origin, factor, type?}
 *   - set_curve_color: {name, color: [r,g,b,a], type?}
 *
 * Properties:
 *   Use configure_asset tool to set UAnimSequence properties (rate_scale, interpolation,
 *   additive_anim_type, enable_root_motion, etc.) via reflection.
 *
 * Frame Operations (via IAnimationDataController):
 *   - set_frame_rate: {fps} or {numerator, denominator}
 *   - set_number_of_frames: int
 *   - resize: {new_frame_count, t0, t1}
 *
 * Bone Tracks:
 *   - remove_bone_tracks: ["bone1", "bone2"]
 */
class AGENTINTEGRATIONKIT_API FEditAnimSequenceTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_anim_sequence"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create and edit AnimSequence assets: create blank animations from a skeleton, "
			"add bone tracks and set per-bone transform keyframes (positions, rotations, scales) "
			"to procedurally generate skeletal animation. Also: add/remove/edit notifies and notify states, "
			"manage sync markers, edit float and transform curves, "
			"configure additive animation, root motion, compression, rate scale, and frame rate. "
			"Supports euler [pitch,yaw,roll] or quaternion [x,y,z,w] rotation formats.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// Notify operations (direct array manipulation)
	int32 AddNotifies(UAnimSequence* Seq, const TArray<TSharedPtr<FJsonValue>>* NotifiesArray,
	                  TArray<FString>& OutResults);
	int32 RemoveNotifies(UAnimSequence* Seq, const TArray<TSharedPtr<FJsonValue>>* NotifiesArray,
	                     TArray<FString>& OutResults);
	int32 EditNotifies(UAnimSequence* Seq, const TArray<TSharedPtr<FJsonValue>>* NotifiesArray,
	                   TArray<FString>& OutResults);

	// Sync markers (direct array manipulation)
	int32 AddSyncMarkers(UAnimSequence* Seq, const TArray<TSharedPtr<FJsonValue>>* MarkersArray,
	                     TArray<FString>& OutResults);
	int32 RemoveSyncMarkers(UAnimSequence* Seq, const TArray<TSharedPtr<FJsonValue>>* MarkersArray,
	                        TArray<FString>& OutResults);
	bool RenameSyncMarker(UAnimSequence* Seq, const TSharedPtr<FJsonObject>& RenameObj,
	                      TArray<FString>& OutResults);

	// Curve operations (via IAnimationDataController)
	int32 AddCurves(IAnimationDataController& Controller, const TArray<TSharedPtr<FJsonValue>>* CurvesArray,
	                TArray<FString>& OutResults);
	int32 RemoveCurves(IAnimationDataController& Controller, const TArray<TSharedPtr<FJsonValue>>* CurvesArray,
	                   TArray<FString>& OutResults);
	int32 SetCurveKeys(IAnimationDataController& Controller, const TArray<TSharedPtr<FJsonValue>>* KeysArray,
	                   TArray<FString>& OutResults);
	int32 SetTransformCurveKeys(IAnimationDataController& Controller,
	                            const TArray<TSharedPtr<FJsonValue>>* KeysArray,
	                            TArray<FString>& OutResults);
	bool RenameCurve(IAnimationDataController& Controller, const TSharedPtr<FJsonObject>& RenameObj,
	                 TArray<FString>& OutResults);
	bool ScaleCurve(IAnimationDataController& Controller, const TSharedPtr<FJsonObject>& ScaleObj,
	                TArray<FString>& OutResults);
	bool SetCurveColor(IAnimationDataController& Controller, const TSharedPtr<FJsonObject>& ColorObj,
	                   TArray<FString>& OutResults);

	// Frame operations (via IAnimationDataController)
	bool SetFrameRate(IAnimationDataController& Controller, const TSharedPtr<FJsonObject>& FRObj,
	                  TArray<FString>& OutResults);
	bool SetNumberOfFrames(IAnimationDataController& Controller, double FrameCount,
	                       TArray<FString>& OutResults);
	bool Resize(IAnimationDataController& Controller, const TSharedPtr<FJsonObject>& ResizeObj,
	            TArray<FString>& OutResults);

	// Bone tracks (via IAnimationDataController)
	int32 AddBoneTracks(IAnimationDataController& Controller,
	                    const TArray<TSharedPtr<FJsonValue>>* TracksArray,
	                    TArray<FString>& OutResults);
	int32 RemoveBoneTracks(IAnimationDataController& Controller,
	                       const TArray<TSharedPtr<FJsonValue>>* TracksArray,
	                       TArray<FString>& OutResults);
	int32 SetBoneTrackKeysOp(IAnimationDataController& Controller,
	                         const TArray<TSharedPtr<FJsonValue>>* KeysArray,
	                         TArray<FString>& OutResults);
	int32 UpdateBoneTrackKeysOp(IAnimationDataController& Controller,
	                            const TArray<TSharedPtr<FJsonValue>>* KeysArray,
	                            TArray<FString>& OutResults);

	// Asset creation
	UAnimSequence* CreateAnimSequence(const FString& Name, const FString& Path,
	                                  const TSharedPtr<FJsonObject>& CreateObj,
	                                  TArray<FString>& OutResults);

	// Helpers
	static UClass* FindAnimNotifyClass(const FString& TypeName);
	FAnimationCurveIdentifier MakeCurveId(const FString& Name, const FString& Type) const;
};
