// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"
#include "Misc/FrameNumber.h"

class ULevelSequence;
class UMovieScene;
class UMovieSceneTrack;
class UMovieSceneSection;
class AActor;

/**
 * Dynamic, discovery-based tool for editing Level Sequences.
 *
 * Discovery actions (use before editing):
 * - list_track_types: Enumerate all available track types in the engine
 * - analyze_camera_cuts: Inspect shot pacing, continuity, and review timestamps
 * - list_channels: See channels on a specific track (indices for keyframing)
 * - list_properties: See animatable properties on an actor
 *
 * Edit operations:
 * - add_bindings, add_tracks, add_keyframes, set_transforms
 * - remove_bindings, remove_tracks
 * Playback range and frame rate are set via configure_asset (use include_all to discover them).
 */
class AGENTINTEGRATIONKIT_API FEditSequencerTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_sequencer"); }
	virtual FString GetDescription() const override;
	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// ===== Structs =====

	/** Actor/Component binding definition */
	struct FBindingDef
	{
		FString ActorName;
		FString Name;
		FString ComponentName;
	};

	/** Track definition */
	struct FTrackDef
	{
		FString Binding;
		FString TrackType;     // Discovered name from list_track_types (e.g., "3DTransform", "Audio", "CameraCut")
		double StartTime = 0.0;
		double EndTime = -1.0;

		// Audio
		FString SoundAsset;
		float SoundVolume = 1.0f;
		float SoundPitch = 1.0f;

		// Skeletal animation
		FString AnimationAsset;

		// Camera cut
		FString CameraBinding;

		// Property tracks
		FString PropertyName;
		FString PropertyPath;
	};

	/** Keyframe definition - channel-by-index */
	struct FKeyframeDef
	{
		FString Binding;
		FString TrackType;      // Track type to find the right track
		int32 ChannelIndex = 0; // Channel index from list_channels
		int32 TrackIndex = -1;  // Optional: when multiple tracks of same type
		double Time = 0.0;
		double Value = 0.0;
		bool BoolValue = false;
		FString Interp;         // linear, constant, cubic
	};

	/** Bulk transform keyframe */
	struct FTransformKeyDef
	{
		FString Binding;
		double Time = 0.0;
		bool bHasLocation = false;
		FVector Location = FVector::ZeroVector;
		bool bHasRotation = false;
		FRotator Rotation = FRotator::ZeroRotator;
		bool bHasScale = false;
		FVector Scale = FVector::OneVector;
		FString Interp;
	};

	/** Added binding result */
	struct FAddedBinding
	{
		FString Name;
		FString ActorPath;
		FGuid Guid;
	};

	/** Added track result */
	struct FAddedTrack
	{
		FString Binding;
		FString TrackType;
		double StartTime;
		double EndTime;
	};

	// ===== Discovery =====

	FToolResult ListTrackTypes();
	FToolResult ListBindings(ULevelSequence* Sequence);
	FToolResult AnalyzeCameraCuts(ULevelSequence* Sequence);
	FToolResult ExecuteShotPlan(ULevelSequence* Sequence, const TSharedPtr<FJsonObject>& Args);
	FToolResult ListChannels(ULevelSequence* Sequence, const TSharedPtr<FJsonObject>& Args);
	FToolResult ListProperties(const TSharedPtr<FJsonObject>& Args);


	// ===== Dynamic Track Resolution =====

	static UClass* ResolveTrackClass(const FString& TrackType);
	static FString GetFriendlyTrackName(UClass* TrackClass);

	// ===== Parsing =====

	bool ParseBindingDef(const TSharedPtr<FJsonObject>& Obj, FBindingDef& OutDef, FString& OutError);
	bool ParseTrackDef(const TSharedPtr<FJsonObject>& Obj, FTrackDef& OutDef, FString& OutError);
	bool ParseKeyframeDef(const TSharedPtr<FJsonObject>& Obj, FKeyframeDef& OutDef, FString& OutError);
	bool ParseTransformKeyDef(const TSharedPtr<FJsonObject>& Obj, FTransformKeyDef& OutDef, FString& OutError);

	// ===== Operations =====

	FString AddBinding(ULevelSequence* Sequence, const FBindingDef& Def, FAddedBinding& OutResult);
	FString AddTrack(ULevelSequence* Sequence, const FTrackDef& Def, FAddedTrack& OutResult);
	FString AddKeyframe(ULevelSequence* Sequence, const FKeyframeDef& Def);
	FString AddTransformKeys(ULevelSequence* Sequence, const FTransformKeyDef& Def);
	FString RemoveBinding(ULevelSequence* Sequence, const FString& BindingName);
	FString RemoveTrack(ULevelSequence* Sequence, const TSharedPtr<FJsonObject>& Def);

	// ===== Helpers =====

	FGuid FindBindingByName(UMovieScene* MovieScene, const FString& Name);
	AActor* FindActorByName(const FString& ActorName);
	FFrameNumber SecondsToFrame(UMovieScene* MovieScene, double Seconds) const;
	double FrameToSeconds(UMovieScene* MovieScene, FFrameNumber Frame) const;

	/** Count total channels for a section using typed accessors (Double, Float, Bool, Int, Byte order) */
	int32 CountChannels(UMovieSceneSection* Section) const;

	FString FormatResults(
		const FString& SequenceName,
		const TArray<FAddedBinding>& Bindings,
		const TArray<FAddedTrack>& Tracks,
		const TArray<FString>& Keyframes,
		const TArray<FString>& TransformKeys,
		const TArray<FString>& RemovedBindings,
		const TArray<FString>& RemovedTracks,
		const TArray<FString>& Errors
	) const;
};
