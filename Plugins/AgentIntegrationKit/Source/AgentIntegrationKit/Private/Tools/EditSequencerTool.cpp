// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditSequencerTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Level Sequence
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieScenePossessable.h"
#include "MovieSceneSection.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSpawnable.h"

// Track types (needed for section casts in AddTrack configuration)
#include "Tracks/MovieScene3DTransformTrack.h"
#include "Tracks/MovieSceneCameraCutTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneVisibilityTrack.h"
#include "Tracks/MovieSceneFadeTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Tracks/MovieScenePropertyTrack.h"

// Sections (for configuration in AddTrack)
#include "Sections/MovieScene3DTransformSection.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Sections/MovieSceneCameraCutSection.h"

// Channels for keyframing
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneChannelProxy.h"

// Assets
#include "Sound/SoundBase.h"
#include "Animation/AnimSequenceBase.h"
#include "MovieSceneObjectBindingID.h"
#include "MovieSceneSequenceID.h"

// Actor finding and editor
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Editor.h"
#include "LevelEditorSubsystem.h"
#include "Camera/CameraActor.h"

// Transaction
#include "ScopedTransaction.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"

// Dynamic class discovery
#include "UObject/UObjectIterator.h"

// Property iteration for list_properties
#include "UObject/UnrealType.h"

// ========== Description ==========

FString FEditSequencerTool::GetDescription() const
{
	return TEXT(
		"Edit Level Sequences with dynamic discovery. "
		"Use action='list_track_types' to discover available track types, "
		"action='list_bindings' to inspect existing bindings/tracks/timing, "
		"action='analyze_camera_cuts' to review shot pacing/coverage and get screenshot review timestamps, "
		"action='execute_shot_plan' to block shots from beat definitions (cuts + camera transforms), "
		"action='list_channels' to see channel indices for keyframing, "
		"action='list_properties' to see animatable properties on actors. "
		"Use read_asset to see sequence bindings and track details. "
		"Then use add_bindings, add_tracks, add_keyframes (by channel index), "
		"set_transforms (bulk location/rotation/scale), remove_bindings, remove_tracks. "
		"For cinematics, work in passes like a human editor: block shots, review with screenshot, then refine cuts and camera transforms."
	);
}

// ========== Schema ==========

TSharedPtr<FJsonObject> FEditSequencerTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Action
	TSharedPtr<FJsonObject> ActionProp = MakeShared<FJsonObject>();
	ActionProp->SetStringField(TEXT("type"), TEXT("string"));
	ActionProp->SetStringField(TEXT("description"),
		TEXT("Action to perform. Discovery: 'list_track_types', 'list_bindings', 'analyze_camera_cuts', 'list_channels', 'list_properties'. "
		     "High-level edit: 'execute_shot_plan'. "
		     "Edit: 'edit' (default). Use read_asset for binding details, then edit."));
	TArray<TSharedPtr<FJsonValue>> ActionEnum;
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("edit")));
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("list_track_types")));
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("list_bindings")));
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("analyze_camera_cuts")));
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("execute_shot_plan")));
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("list_channels")));
	ActionEnum.Add(MakeShared<FJsonValueString>(TEXT("list_properties")));
	ActionProp->SetArrayField(TEXT("enum"), ActionEnum);
	Properties->SetObjectField(TEXT("action"), ActionProp);

	// Asset
	TSharedPtr<FJsonObject> AssetProp = MakeShared<FJsonObject>();
	AssetProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetProp->SetStringField(TEXT("description"), TEXT("Level Sequence asset name or path (required for edit, list_channels, analyze_camera_cuts)"));
	Properties->SetObjectField(TEXT("asset"), AssetProp);

	// Path
	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (default: /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// Discovery params
	TSharedPtr<FJsonObject> BindingParam = MakeShared<FJsonObject>();
	BindingParam->SetStringField(TEXT("type"), TEXT("string"));
	BindingParam->SetStringField(TEXT("description"), TEXT("Binding name (for list_channels: which binding's track to inspect)"));
	Properties->SetObjectField(TEXT("binding"), BindingParam);

	TSharedPtr<FJsonObject> TrackTypeParam = MakeShared<FJsonObject>();
	TrackTypeParam->SetStringField(TEXT("type"), TEXT("string"));
	TrackTypeParam->SetStringField(TEXT("description"), TEXT("Track type name from list_track_types (for list_channels)"));
	Properties->SetObjectField(TEXT("track_type"), TrackTypeParam);

	TSharedPtr<FJsonObject> TrackIndexParam = MakeShared<FJsonObject>();
	TrackIndexParam->SetStringField(TEXT("type"), TEXT("integer"));
	TrackIndexParam->SetStringField(TEXT("description"), TEXT("Track index when multiple tracks of same type (for list_channels)"));
	Properties->SetObjectField(TEXT("track_index"), TrackIndexParam);

	TSharedPtr<FJsonObject> ActorNameParam = MakeShared<FJsonObject>();
	ActorNameParam->SetStringField(TEXT("type"), TEXT("string"));
	ActorNameParam->SetStringField(TEXT("description"), TEXT("Actor name in level (for list_properties)"));
	Properties->SetObjectField(TEXT("actor_name"), ActorNameParam);

	// Add bindings
	TSharedPtr<FJsonObject> BindingsProp = MakeShared<FJsonObject>();
	BindingsProp->SetStringField(TEXT("type"), TEXT("array"));
	BindingsProp->SetStringField(TEXT("description"),
		TEXT("Actors/components to bind: [{actor_name, name? (display name), component_name? (binds component under actor)}]"));
	Properties->SetObjectField(TEXT("add_bindings"), BindingsProp);

	// Add tracks
	TSharedPtr<FJsonObject> TracksProp = MakeShared<FJsonObject>();
	TracksProp->SetStringField(TEXT("type"), TEXT("array"));
	TracksProp->SetStringField(TEXT("description"),
		TEXT("Tracks to add: [{binding? (omit for master tracks), track_type (from list_track_types, e.g. '3DTransform', 'Audio', 'CameraCut'), "
		     "start_time?, end_time?, sound_asset?, sound_volume?, sound_pitch?, animation_asset?, camera_binding?, property_name?, property_path?}]. "
		     "For CameraCut, omit binding and provide camera_binding/start_time/end_time; multiple entries create multiple shot sections on one CameraCut track."));
	Properties->SetObjectField(TEXT("add_tracks"), TracksProp);

	// Execute shot plan (high-level sequence blocking)
	TSharedPtr<FJsonObject> ShotsProp = MakeShared<FJsonObject>();
	ShotsProp->SetStringField(TEXT("type"), TEXT("array"));
	ShotsProp->SetStringField(TEXT("description"),
		TEXT("For action='execute_shot_plan': shots to block and frame. "
		     "[{name?, start_time, end_time, camera_binding?, camera_actor?, target_actor?, "
		     "shot_size? (wide/medium/closeup/extreme_closeup), side? (front/back/left/right/front_left/front_right/back_left/back_right), "
		     "angle? (eye_level/high_angle/low_angle/bird_eye/worm_eye/dutch_left/dutch_right), movement? (static/push_in/pull_out/orbit_left/orbit_right), "
		     "distance_scale?, height_offset?}]."));
	Properties->SetObjectField(TEXT("shots"), ShotsProp);

	TSharedPtr<FJsonObject> ReplaceCutsProp = MakeShared<FJsonObject>();
	ReplaceCutsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ReplaceCutsProp->SetStringField(TEXT("description"), TEXT("For action='execute_shot_plan': if true, clear existing CameraCut track first."));
	Properties->SetObjectField(TEXT("replace_camera_cuts"), ReplaceCutsProp);

	TSharedPtr<FJsonObject> DefaultTargetProp = MakeShared<FJsonObject>();
	DefaultTargetProp->SetStringField(TEXT("type"), TEXT("string"));
	DefaultTargetProp->SetStringField(TEXT("description"), TEXT("For action='execute_shot_plan': default target actor if a shot omits target_actor."));
	Properties->SetObjectField(TEXT("default_target_actor"), DefaultTargetProp);

	// Add keyframes (channel-by-index)
	TSharedPtr<FJsonObject> KeyframesProp = MakeShared<FJsonObject>();
	KeyframesProp->SetStringField(TEXT("type"), TEXT("array"));
	KeyframesProp->SetStringField(TEXT("description"),
		TEXT("Keyframes by channel index: [{binding, track_type, channel_index (from list_channels), time (seconds), value (number), "
		     "bool_value? (for bool channels), interp? (linear/constant/cubic), track_index? (when multiple same-type tracks)}]"));
	Properties->SetObjectField(TEXT("add_keyframes"), KeyframesProp);

	// Set transforms (bulk convenience)
	TSharedPtr<FJsonObject> TransformsProp = MakeShared<FJsonObject>();
	TransformsProp->SetStringField(TEXT("type"), TEXT("array"));
	TransformsProp->SetStringField(TEXT("description"),
		TEXT("Bulk transform keyframes: [{binding, time (seconds), location? [x,y,z], rotation? [pitch,yaw,roll] in degrees, "
		     "scale? [x,y,z], interp? (linear/constant/cubic)}]. Sets multiple channels at once."));
	Properties->SetObjectField(TEXT("set_transforms"), TransformsProp);

	// Remove bindings
	TSharedPtr<FJsonObject> RemoveBindingsProp = MakeShared<FJsonObject>();
	RemoveBindingsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveBindingsProp->SetStringField(TEXT("description"), TEXT("Binding names to remove from the sequence"));
	Properties->SetObjectField(TEXT("remove_bindings"), RemoveBindingsProp);

	// Remove tracks
	TSharedPtr<FJsonObject> RemoveTracksProp = MakeShared<FJsonObject>();
	RemoveTracksProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveTracksProp->SetStringField(TEXT("description"),
		TEXT("Tracks to remove: [{binding, track_type, track_index? (default 0)}]"));
	Properties->SetObjectField(TEXT("remove_tracks"), RemoveTracksProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("action")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ========== Execute ==========

FToolResult FEditSequencerTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// Get action
	FString Action;
	Args->TryGetStringField(TEXT("action"), Action);
	if (Action.IsEmpty())
	{
		Action = TEXT("edit");
	}

	// list_track_types needs no sequence
	if (Action == TEXT("list_track_types"))
	{
		return ListTrackTypes();
	}

	// list_properties needs no sequence
	if (Action == TEXT("list_properties"))
	{
		return ListProperties(Args);
	}

	// All other actions need a sequence
	FString AssetName;
	if (!Args->TryGetStringField(TEXT("asset"), AssetName) || AssetName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: asset"));
	}

	// Parse path
	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	// Build asset path
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(AssetName, Path);

	// Load sequence
	ULevelSequence* Sequence = LoadObject<ULevelSequence>(nullptr, *FullAssetPath);
	if (!Sequence)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Level Sequence not found: %s"), *FullAssetPath));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FToolResult::Fail(TEXT("Level Sequence has no MovieScene"));
	}

	// Discovery actions
	if (Action == TEXT("list_bindings"))
	{
		return ListBindings(Sequence);
	}

	if (Action == TEXT("list_channels"))
	{
		return ListChannels(Sequence, Args);
	}

	if (Action == TEXT("analyze_camera_cuts"))
	{
		return AnalyzeCameraCuts(Sequence);
	}

	if (Action == TEXT("execute_shot_plan"))
	{
		return ExecuteShotPlan(Sequence, Args);
	}
	// Edit action
	if (Action != TEXT("edit"))
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unknown action '%s'. Valid: edit, list_track_types, list_bindings, analyze_camera_cuts, execute_shot_plan, list_channels, list_properties"), *Action));
	}

	// Create transaction
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditSequencer", "AI Edit Sequencer: {0}"),
		FText::FromString(AssetName)));

	// Track results
	TArray<FAddedBinding> AddedBindings;
	TArray<FAddedTrack> AddedTracks;
	TArray<FString> AddedKeyframes;
	TArray<FString> AddedTransformKeys;
	TArray<FString> RemovedBindings;
	TArray<FString> RemovedTracks;
	TArray<FString> Errors;

	// Process remove_bindings (before adds, so we can clean up first)
	const TArray<TSharedPtr<FJsonValue>>* RemoveBindingsArray;
	if (Args->TryGetArrayField(TEXT("remove_bindings"), RemoveBindingsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveBindingsArray)
		{
			FString BindingName;
			if (Value->TryGetString(BindingName) && !BindingName.IsEmpty())
			{
				FString Result = RemoveBinding(Sequence, BindingName);
				if (Result.StartsWith(TEXT("ERROR:")))
				{
					Errors.Add(Result);
				}
				else
				{
					RemovedBindings.Add(Result);
				}
			}
		}
	}

	// Process remove_tracks
	const TArray<TSharedPtr<FJsonValue>>* RemoveTracksArray;
	if (Args->TryGetArrayField(TEXT("remove_tracks"), RemoveTracksArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveTracksArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FString Result = RemoveTrack(Sequence, *Obj);
				if (Result.StartsWith(TEXT("ERROR:")))
				{
					Errors.Add(Result);
				}
				else
				{
					RemovedTracks.Add(Result);
				}
			}
		}
	}

	// Process add_bindings
	const TArray<TSharedPtr<FJsonValue>>* BindingsArray;
	if (Args->TryGetArrayField(TEXT("add_bindings"), BindingsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *BindingsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (!Value->TryGetObject(Obj))
			{
				Errors.Add(TEXT("Invalid binding definition (not an object)"));
				continue;
			}

			FBindingDef Def;
			FString ParseError;
			if (!ParseBindingDef(*Obj, Def, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			FAddedBinding Result;
			FString OpResult = AddBinding(Sequence, Def, Result);
			if (OpResult.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(OpResult);
			}
			else
			{
				AddedBindings.Add(Result);
			}
		}
	}

	// Process add_tracks
	const TArray<TSharedPtr<FJsonValue>>* TracksArray;
	if (Args->TryGetArrayField(TEXT("add_tracks"), TracksArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *TracksArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (!Value->TryGetObject(Obj))
			{
				Errors.Add(TEXT("Invalid track definition (not an object)"));
				continue;
			}

			FTrackDef Def;
			FString ParseError;
			if (!ParseTrackDef(*Obj, Def, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			FAddedTrack Result;
			FString OpResult = AddTrack(Sequence, Def, Result);
			if (OpResult.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(OpResult);
			}
			else
			{
				AddedTracks.Add(Result);
			}
		}
	}

	// Process set_transforms (bulk convenience)
	const TArray<TSharedPtr<FJsonValue>>* TransformsArray;
	if (Args->TryGetArrayField(TEXT("set_transforms"), TransformsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *TransformsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (!Value->TryGetObject(Obj))
			{
				Errors.Add(TEXT("Invalid transform definition (not an object)"));
				continue;
			}

			FTransformKeyDef Def;
			FString ParseError;
			if (!ParseTransformKeyDef(*Obj, Def, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			FString OpResult = AddTransformKeys(Sequence, Def);
			if (OpResult.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(OpResult);
			}
			else
			{
				AddedTransformKeys.Add(OpResult);
			}
		}
	}

	// Process add_keyframes
	const TArray<TSharedPtr<FJsonValue>>* KeyframesArray;
	if (Args->TryGetArrayField(TEXT("add_keyframes"), KeyframesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *KeyframesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (!Value->TryGetObject(Obj))
			{
				Errors.Add(TEXT("Invalid keyframe definition (not an object)"));
				continue;
			}

			FKeyframeDef Def;
			FString ParseError;
			if (!ParseKeyframeDef(*Obj, Def, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			FString OpResult = AddKeyframe(Sequence, Def);
			if (OpResult.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(OpResult);
			}
			else
			{
				AddedKeyframes.Add(OpResult);
			}
		}
	}

	// Mark modified
	bool bHasChanges = AddedBindings.Num() > 0 || AddedTracks.Num() > 0 || AddedKeyframes.Num() > 0 ||
		AddedTransformKeys.Num() > 0 || RemovedBindings.Num() > 0 || RemovedTracks.Num() > 0;

	if (bHasChanges)
	{
		Sequence->Modify();
		Sequence->MarkPackageDirty();
	}

	// Format output
	FString Output = FormatResults(AssetName, AddedBindings, AddedTracks, AddedKeyframes,
		AddedTransformKeys, RemovedBindings, RemovedTracks, Errors);

	if (Errors.Num() > 0 && !bHasChanges)
	{
		return FToolResult::Fail(Output);
	}

	return FToolResult::Ok(Output);
}

// ========== Discovery: List Track Types ==========

FToolResult FEditSequencerTool::ListTrackTypes()
{
	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(UMovieSceneTrack::StaticClass(), DerivedClasses);

	FString Output = TEXT("# Available Track Types\n\n");
	Output += TEXT("Use these names in add_tracks track_type field.\n\n");

	// Sort by friendly name
	DerivedClasses.Sort([](const UClass& A, const UClass& B)
	{
		return GetFriendlyTrackName(const_cast<UClass*>(&A)) < GetFriendlyTrackName(const_cast<UClass*>(&B));
	});

	int32 Count = 0;
	for (UClass* TrackClass : DerivedClasses)
	{
		if (!TrackClass || TrackClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		FString FriendlyName = GetFriendlyTrackName(TrackClass);
		FString ClassName = TrackClass->GetName();

		Output += FString::Printf(TEXT("- **%s** (class: %s)\n"), *FriendlyName, *ClassName);
		Count++;
	}

	Output += FString::Printf(TEXT("\nTotal: %d track types\n"), Count);

	return FToolResult::Ok(Output);
}

// ========== Discovery: List Bindings ==========

FToolResult FEditSequencerTool::ListBindings(ULevelSequence* Sequence)
{
	if (!Sequence)
	{
		return FToolResult::Fail(TEXT("Invalid sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FToolResult::Fail(TEXT("Level Sequence has no MovieScene"));
	}

	TMap<FGuid, FString> GuidToBindingName;
	GuidToBindingName.Reserve(MovieScene->GetPossessableCount() + MovieScene->GetSpawnableCount());

	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		GuidToBindingName.Add(Possessable.GetGuid(), Possessable.GetName());
	}
	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		GuidToBindingName.Add(Spawnable.GetGuid(), Spawnable.GetName());
	}

	FString Output = FString::Printf(TEXT("# Bindings in '%s'\n\n"), *Sequence->GetName());

	const FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	const FFrameRate TickRate = MovieScene->GetTickResolution();
	Output += FString::Printf(TEXT("Display Rate: %d/%d (%.3f fps)\n"),
		DisplayRate.Numerator, DisplayRate.Denominator, DisplayRate.AsDecimal());
	Output += FString::Printf(TEXT("Tick Resolution: %d/%d (%.3f tps)\n"),
		TickRate.Numerator, TickRate.Denominator, TickRate.AsDecimal());

	const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	FString PlaybackStart = TEXT("unbounded");
	FString PlaybackEnd = TEXT("unbounded");
	if (PlaybackRange.HasLowerBound())
	{
		PlaybackStart = FString::Printf(TEXT("%.2fs"), FrameToSeconds(MovieScene, PlaybackRange.GetLowerBoundValue()));
	}
	if (PlaybackRange.HasUpperBound())
	{
		PlaybackEnd = FString::Printf(TEXT("%.2fs"), FrameToSeconds(MovieScene, PlaybackRange.GetUpperBoundValue()));
	}
	Output += FString::Printf(TEXT("Playback Range: %s -> %s\n\n"), *PlaybackStart, *PlaybackEnd);

	auto AppendTrackDetails = [&](UMovieSceneTrack* Track, int32 TrackIndex)
	{
		if (!Track)
		{
			return FString::Printf(TEXT("- track[%d]: (invalid)\n"), TrackIndex);
		}

		const FString FriendlyTrackType = GetFriendlyTrackName(Track->GetClass());
		const TArray<UMovieSceneSection*>& Sections = Track->GetAllSections();

		FString TrackText = FString::Printf(TEXT("- track[%d]: %s (%d section%s)\n"),
			TrackIndex,
			*FriendlyTrackType,
			Sections.Num(),
			Sections.Num() == 1 ? TEXT("") : TEXT("s"));

		for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
		{
			UMovieSceneSection* Section = Sections[SectionIndex];
			if (!Section)
			{
				TrackText += FString::Printf(TEXT("  - section[%d]: (invalid)\n"), SectionIndex);
				continue;
			}

			const TRange<FFrameNumber> SectionRange = Section->GetRange();
			FString Start = TEXT("unbounded");
			FString End = TEXT("unbounded");
			if (SectionRange.HasLowerBound())
			{
				Start = FString::Printf(TEXT("%.2fs"), FrameToSeconds(MovieScene, SectionRange.GetLowerBoundValue()));
			}
			if (SectionRange.HasUpperBound())
			{
				End = FString::Printf(TEXT("%.2fs"), FrameToSeconds(MovieScene, SectionRange.GetUpperBoundValue()));
			}

			const int32 ChannelCount = CountChannels(Section);
			TrackText += FString::Printf(TEXT("  - section[%d]: %s -> %s, channels=%d\n"),
				SectionIndex, *Start, *End, ChannelCount);

			if (UMovieSceneCameraCutSection* CameraCut = Cast<UMovieSceneCameraCutSection>(Section))
			{
				const FMovieSceneObjectBindingID& CameraBindingID = CameraCut->GetCameraBindingID();
				if (!CameraBindingID.IsValid())
				{
					TrackText += TEXT("    camera_binding: (none)\n");
				}
				else
				{
					const FGuid CameraGuid = CameraBindingID.GetGuid();
					const FString* CameraBindingName = GuidToBindingName.Find(CameraGuid);
					const FString CameraName = CameraBindingName
						? *CameraBindingName
						: FString::Printf(TEXT("(unknown guid: %s)"), *CameraGuid.ToString());
					TrackText += FString::Printf(TEXT("    camera_binding: %s\n"), *CameraName);
				}
			}
		}

		return TrackText;
	};

	auto AppendBindingDetails = [&](const FGuid& BindingGuid, const FString& BindingName, const TCHAR* KindLabel)
	{
		FString BindingText = FString::Printf(TEXT("- **%s** [%s] (GUID: %s)\n"),
			*BindingName, KindLabel, *BindingGuid.ToString());

		const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
		if (!Binding)
		{
			BindingText += TEXT("  - (binding entry missing)\n");
			return BindingText;
		}

		const TArray<UMovieSceneTrack*>& Tracks = Binding->GetTracks();
		if (Tracks.Num() == 0)
		{
			BindingText += TEXT("  - (no tracks)\n");
			return BindingText;
		}

		for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); ++TrackIndex)
		{
			BindingText += FString::Printf(TEXT("  %s"), *AppendTrackDetails(Tracks[TrackIndex], TrackIndex));
		}

		return BindingText;
	};

	Output += FString::Printf(TEXT("## Possessables (%d)\n"), MovieScene->GetPossessableCount());
	if (MovieScene->GetPossessableCount() == 0)
	{
		Output += TEXT("(none)\n");
	}
	else
	{
		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
			Output += AppendBindingDetails(Possessable.GetGuid(), Possessable.GetName(), TEXT("possessable"));
		}
	}
	Output += TEXT("\n");

	Output += FString::Printf(TEXT("## Spawnables (%d)\n"), MovieScene->GetSpawnableCount());
	if (MovieScene->GetSpawnableCount() == 0)
	{
		Output += TEXT("(none)\n");
	}
	else
	{
		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
			Output += AppendBindingDetails(Spawnable.GetGuid(), Spawnable.GetName(), TEXT("spawnable"));
		}
	}
	Output += TEXT("\n");

	TArray<UMovieSceneTrack*> MasterTracks = MovieScene->GetTracks();
	if (UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack())
	{
		MasterTracks.Insert(CameraCutTrack, 0);
	}

	Output += FString::Printf(TEXT("## Master Tracks (%d)\n"), MasterTracks.Num());
	if (MasterTracks.Num() == 0)
	{
		Output += TEXT("(none)\n");
	}
	else
	{
		for (int32 TrackIndex = 0; TrackIndex < MasterTracks.Num(); ++TrackIndex)
		{
			Output += AppendTrackDetails(MasterTracks[TrackIndex], TrackIndex);
		}
	}

	Output += TEXT("\nTIP: Build or revise shots in passes:\n");
	Output += TEXT("1) inspect existing bindings/cuts with list_bindings, ");
	Output += TEXT("2) run analyze_camera_cuts for pacing/coverage issues, ");
	Output += TEXT("3) edit with add_tracks/set_transforms/add_keyframes, ");
	Output += TEXT("4) verify framing with screenshot, then iterate.\n");

	return FToolResult::Ok(Output);
}

// ========== Discovery: List Channels ==========

FToolResult FEditSequencerTool::ListChannels(ULevelSequence* Sequence, const TSharedPtr<FJsonObject>& Args)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	FString BindingName;
	if (!Args->TryGetStringField(TEXT("binding"), BindingName) || BindingName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("list_channels requires 'binding' parameter"));
	}

	FString TrackType;
	if (!Args->TryGetStringField(TEXT("track_type"), TrackType) || TrackType.IsEmpty())
	{
		return FToolResult::Fail(TEXT("list_channels requires 'track_type' parameter"));
	}

	int32 TrackIndex = 0;
	Args->TryGetNumberField(TEXT("track_index"), TrackIndex);

	// Find binding
	FGuid BindingGuid = FindBindingByName(MovieScene, BindingName);
	if (!BindingGuid.IsValid())
	{
		return FToolResult::Fail(FString::Printf(TEXT("Binding '%s' not found"), *BindingName));
	}

	// Resolve track class
	UClass* TrackClass = ResolveTrackClass(TrackType);
	if (!TrackClass)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unknown track type '%s'. Use list_track_types to see available types."), *TrackType));
	}

	// Find the track
	const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
	if (!Binding)
	{
		return FToolResult::Fail(FString::Printf(TEXT("No tracks found for binding '%s'"), *BindingName));
	}

	UMovieSceneTrack* TargetTrack = nullptr;
	int32 MatchCount = 0;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (Track && Track->IsA(TrackClass))
		{
			if (MatchCount == TrackIndex)
			{
				TargetTrack = Track;
				break;
			}
			MatchCount++;
		}
	}

	if (!TargetTrack)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Binding '%s' has no %s track (index %d)"), *BindingName, *TrackType, TrackIndex));
	}

	// Get section
	const TArray<UMovieSceneSection*>& Sections = TargetTrack->GetAllSections();
	if (Sections.Num() == 0)
	{
		return FToolResult::Fail(TEXT("Track has no sections"));
	}

	UMovieSceneSection* Section = Sections[0];
	if (!Section)
	{
		return FToolResult::Fail(TEXT("Invalid section"));
	}

	FString Output = FString::Printf(TEXT("# Channels for %s on '%s'\n\n"), *TrackType, *BindingName);
	Output += TEXT("Use channel_index in add_keyframes to target a specific channel.\n\n");

	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	int32 GlobalIdx = 0;

	// Double channels
	TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
	for (int32 i = 0; i < DoubleChannels.Num(); i++)
	{
		int32 NumKeys = DoubleChannels[i] ? DoubleChannels[i]->GetNumKeys() : 0;
		Output += FString::Printf(TEXT("- [%d] DoubleChannel[%d] (%d keys)\n"), GlobalIdx, i, NumKeys);
		GlobalIdx++;
	}

	// Float channels
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = Proxy.GetChannels<FMovieSceneFloatChannel>();
	for (int32 i = 0; i < FloatChannels.Num(); i++)
	{
		int32 NumKeys = FloatChannels[i] ? FloatChannels[i]->GetNumKeys() : 0;
		Output += FString::Printf(TEXT("- [%d] FloatChannel[%d] (%d keys)\n"), GlobalIdx, i, NumKeys);
		GlobalIdx++;
	}

	// Bool channels
	TArrayView<FMovieSceneBoolChannel*> BoolChannels = Proxy.GetChannels<FMovieSceneBoolChannel>();
	for (int32 i = 0; i < BoolChannels.Num(); i++)
	{
		int32 NumKeys = BoolChannels[i] ? BoolChannels[i]->GetNumKeys() : 0;
		Output += FString::Printf(TEXT("- [%d] BoolChannel[%d] (%d keys)\n"), GlobalIdx, i, NumKeys);
		GlobalIdx++;
	}

	// Integer channels
	TArrayView<FMovieSceneIntegerChannel*> IntChannels = Proxy.GetChannels<FMovieSceneIntegerChannel>();
	for (int32 i = 0; i < IntChannels.Num(); i++)
	{
		int32 NumKeys = IntChannels[i] ? IntChannels[i]->GetNumKeys() : 0;
		Output += FString::Printf(TEXT("- [%d] IntegerChannel[%d] (%d keys)\n"), GlobalIdx, i, NumKeys);
		GlobalIdx++;
	}

	// Byte channels
	TArrayView<FMovieSceneByteChannel*> ByteChannels = Proxy.GetChannels<FMovieSceneByteChannel>();
	for (int32 i = 0; i < ByteChannels.Num(); i++)
	{
		int32 NumKeys = ByteChannels[i] ? ByteChannels[i]->GetNumKeys() : 0;
		Output += FString::Printf(TEXT("- [%d] ByteChannel[%d] (%d keys)\n"), GlobalIdx, i, NumKeys);
		GlobalIdx++;
	}

	Output += FString::Printf(TEXT("\nTotal channels: %d\n"), GlobalIdx);

	// Hint for common track types
	if (TrackClass->IsChildOf(UMovieScene3DTransformTrack::StaticClass()) && DoubleChannels.Num() == 9)
	{
		Output += TEXT("\nTransform channel mapping: 0-2 = Location X/Y/Z, 3-5 = Rotation X/Y/Z (degrees), 6-8 = Scale X/Y/Z\n");
		Output += TEXT("TIP: Use set_transforms for bulk transform keyframing instead of individual channels.\n");
	}

	return FToolResult::Ok(Output);
}

// ========== Discovery: List Properties ==========

FToolResult FEditSequencerTool::ListProperties(const TSharedPtr<FJsonObject>& Args)
{
	FString ActorName;
	if (!Args->TryGetStringField(TEXT("actor_name"), ActorName) || ActorName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("list_properties requires 'actor_name' parameter"));
	}

	AActor* Actor = FindActorByName(ActorName);
	if (!Actor)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Actor '%s' not found in level"), *ActorName));
	}

	FString Output = FString::Printf(TEXT("# Animatable Properties for '%s' (%s)\n\n"),
		*Actor->GetActorLabel(), *Actor->GetClass()->GetName());

	// Animatable property types
	auto IsAnimatableType = [](FProperty* Prop) -> bool
	{
		if (CastField<FFloatProperty>(Prop) || CastField<FDoubleProperty>(Prop)) return true;
		if (CastField<FBoolProperty>(Prop)) return true;
		if (CastField<FIntProperty>(Prop) || CastField<FByteProperty>(Prop)) return true;
		if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
		{
			FName StructName = StructProp->Struct->GetFName();
			if (StructName == NAME_Vector || StructName == NAME_Rotator || StructName == NAME_Color ||
				StructName == NAME_LinearColor || StructName == NAME_Transform)
			{
				return true;
			}
		}
		return false;
	};

	auto GetPropertyTypeName = [](FProperty* Prop) -> FString
	{
		return NeoStackToolUtils::GetPropertyTypeName(Prop);
	};

	// Actor properties
	Output += TEXT("## Actor Properties\n");
	int32 PropCount = 0;
	for (TFieldIterator<FProperty> It(Actor->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (Prop && IsAnimatableType(Prop) && Prop->HasAnyPropertyFlags(CPF_Edit))
		{
			Output += FString::Printf(TEXT("- %s (%s)\n"), *Prop->GetName(), *GetPropertyTypeName(Prop));
			PropCount++;
		}
	}
	if (PropCount == 0) Output += TEXT("  (none found)\n");

	// Component properties
	Output += TEXT("\n## Components\n");
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;

		TArray<FString> CompProps;
		for (TFieldIterator<FProperty> It(Comp->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (Prop && IsAnimatableType(Prop) && Prop->HasAnyPropertyFlags(CPF_Edit))
			{
				CompProps.Add(FString::Printf(TEXT("  - %s (%s)"), *Prop->GetName(), *GetPropertyTypeName(Prop)));
			}
		}

		if (CompProps.Num() > 0)
		{
			Output += FString::Printf(TEXT("### %s (%s)\n"), *Comp->GetName(), *Comp->GetClass()->GetName());
			for (const FString& P : CompProps)
			{
				Output += P + TEXT("\n");
			}
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FEditSequencerTool::AnalyzeCameraCuts(ULevelSequence* Sequence)
{
	if (!Sequence)
	{
		return FToolResult::Fail(TEXT("Invalid sequence"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FToolResult::Fail(TEXT("Level Sequence has no MovieScene"));
	}

	UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(MovieScene->GetCameraCutTrack());
	if (!CameraCutTrack)
	{
		// Legacy fallback: older content may still hold a camera cut track in root tracks.
		for (UMovieSceneTrack* RootTrack : MovieScene->GetTracks())
		{
			if (UMovieSceneCameraCutTrack* LegacyCameraCutTrack = Cast<UMovieSceneCameraCutTrack>(RootTrack))
			{
				CameraCutTrack = LegacyCameraCutTrack;
				break;
			}
		}
	}

	if (!CameraCutTrack)
	{
		FString Output = FString::Printf(TEXT("# Camera Cut Analysis: '%s'\n\n"), *Sequence->GetName());
		Output += TEXT("No CameraCut track found.\n");
		Output += TEXT("Create a CameraCut master track with add_tracks, then add shot sections with camera_binding.\n");
		return FToolResult::Ok(Output);
	}

	TArray<UMovieSceneCameraCutSection*> Sections;
	for (UMovieSceneSection* Section : CameraCutTrack->GetAllSections())
	{
		if (UMovieSceneCameraCutSection* CameraSection = Cast<UMovieSceneCameraCutSection>(Section))
		{
			Sections.Add(CameraSection);
		}
	}

	if (Sections.Num() == 0)
	{
		FString Output = FString::Printf(TEXT("# Camera Cut Analysis: '%s'\n\n"), *Sequence->GetName());
		Output += TEXT("CameraCut track exists but has no sections.\n");
		Output += TEXT("Add sections with add_tracks using track_type='CameraCut' and camera_binding.\n");
		return FToolResult::Ok(Output);
	}

	Sections.Sort([](const UMovieSceneCameraCutSection& A, const UMovieSceneCameraCutSection& B)
	{
		const TRange<FFrameNumber> RangeA = A.GetRange();
		const TRange<FFrameNumber> RangeB = B.GetRange();

		const int32 StartA = RangeA.HasLowerBound() ? RangeA.GetLowerBoundValue().Value : MIN_int32;
		const int32 StartB = RangeB.HasLowerBound() ? RangeB.GetLowerBoundValue().Value : MIN_int32;
		return StartA < StartB;
	});

	TMap<FGuid, FString> GuidToBindingName;
	GuidToBindingName.Reserve(MovieScene->GetPossessableCount() + MovieScene->GetSpawnableCount());
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		GuidToBindingName.Add(Possessable.GetGuid(), Possessable.GetName());
	}
	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		GuidToBindingName.Add(Spawnable.GetGuid(), Spawnable.GetName());
	}

	const FFrameRate TickRate = MovieScene->GetTickResolution();
	const double TickRateDecimal = TickRate.AsDecimal();

	FString Output = FString::Printf(TEXT("# Camera Cut Analysis: '%s'\n\n"), *Sequence->GetName());
	Output += FString::Printf(TEXT("Shots: %d\n"), Sections.Num());
	Output += FString::Printf(TEXT("Tick Resolution: %d/%d (%.3f tps)\n\n"),
		TickRate.Numerator, TickRate.Denominator, TickRateDecimal);

	int32 IssueCount = 0;
	int32 BoundedShotCount = 0;
	double TotalDurationSeconds = 0.0;
	TSet<FGuid> UniqueCameraGuids;
	TArray<FString> ReviewPoints;

	bool bHasPrevEnd = false;
	FFrameNumber PrevEnd = 0;
	FGuid PrevCameraGuid;
	bool bHasPrevCamera = false;

	for (int32 ShotIndex = 0; ShotIndex < Sections.Num(); ++ShotIndex)
	{
		UMovieSceneCameraCutSection* Section = Sections[ShotIndex];
		if (!Section)
		{
			IssueCount++;
			Output += FString::Printf(TEXT("- Shot %d: invalid section\n"), ShotIndex + 1);
			continue;
		}

		const TRange<FFrameNumber> Range = Section->GetRange();
		const bool bHasStart = Range.HasLowerBound();
		const bool bHasEnd = Range.HasUpperBound();

		const FFrameNumber StartFrame = bHasStart ? Range.GetLowerBoundValue() : FFrameNumber(0);
		const FFrameNumber EndFrame = bHasEnd ? Range.GetUpperBoundValue() : FFrameNumber(0);

		FString StartText = bHasStart ? FString::Printf(TEXT("%.2fs"), FrameToSeconds(MovieScene, StartFrame)) : TEXT("unbounded");
		FString EndText = bHasEnd ? FString::Printf(TEXT("%.2fs"), FrameToSeconds(MovieScene, EndFrame)) : TEXT("unbounded");
		FString DurationText = TEXT("unbounded");

		double DurationSeconds = 0.0;
		if (bHasStart && bHasEnd)
		{
			DurationSeconds = FMath::Max(0.0, FrameToSeconds(MovieScene, EndFrame) - FrameToSeconds(MovieScene, StartFrame));
			DurationText = FString::Printf(TEXT("%.2fs"), DurationSeconds);
			TotalDurationSeconds += DurationSeconds;
			BoundedShotCount++;
		}

		const FMovieSceneObjectBindingID& CameraBindingID = Section->GetCameraBindingID();
		const FGuid CameraGuid = CameraBindingID.GetGuid();
		FString CameraName = TEXT("(none)");
		if (CameraGuid.IsValid())
		{
			if (const FString* FoundName = GuidToBindingName.Find(CameraGuid))
			{
				CameraName = *FoundName;
			}
			else
			{
				CameraName = FString::Printf(TEXT("(unknown guid: %s)"), *CameraGuid.ToString());
			}
			UniqueCameraGuids.Add(CameraGuid);
		}

		Output += FString::Printf(TEXT("- Shot %d: %s -> %s | duration=%s | camera=%s\n"),
			ShotIndex + 1, *StartText, *EndText, *DurationText, *CameraName);

		if (!CameraGuid.IsValid())
		{
			IssueCount++;
			Output += TEXT("  ! Missing camera_binding\n");
		}

		if (bHasStart && bHasEnd)
		{
			if (DurationSeconds < 0.75)
			{
				IssueCount++;
				Output += TEXT("  ! Very short cut (<0.75s) - may feel jumpy unless intentional\n");
			}
			else if (DurationSeconds > 8.0)
			{
				IssueCount++;
				Output += TEXT("  ! Very long cut (>8s) - verify pacing and subject motion\n");
			}

			const double StartSeconds = FrameToSeconds(MovieScene, StartFrame);
			const double EndSeconds = FrameToSeconds(MovieScene, EndFrame);
			const double EdgePad = FMath::Min(0.30, DurationSeconds * 0.2);
			const double ReviewStart = StartSeconds + EdgePad;
			const double ReviewMid = (StartSeconds + EndSeconds) * 0.5;
			const double ReviewEnd = EndSeconds - EdgePad;

			ReviewPoints.Add(FString::Printf(TEXT("- Shot %d start: %.2fs"), ShotIndex + 1, ReviewStart));
			ReviewPoints.Add(FString::Printf(TEXT("- Shot %d middle: %.2fs"), ShotIndex + 1, ReviewMid));
			if (ReviewEnd > ReviewStart + 0.05)
			{
				ReviewPoints.Add(FString::Printf(TEXT("- Shot %d end: %.2fs"), ShotIndex + 1, ReviewEnd));
			}
		}
		else
		{
			IssueCount++;
			Output += TEXT("  ! Unbounded shot range - set explicit start/end for predictable timing\n");
		}

		if (bHasPrevEnd && bHasStart)
		{
			const int32 DeltaFrames = StartFrame.Value - PrevEnd.Value;
			if (DeltaFrames > 0)
			{
				IssueCount++;
				const double GapSeconds = TickRateDecimal > 0.0 ? static_cast<double>(DeltaFrames) / TickRateDecimal : 0.0;
				Output += FString::Printf(TEXT("  ! Gap from previous shot: %.2fs\n"), GapSeconds);
			}
			else if (DeltaFrames < 0)
			{
				IssueCount++;
				const double OverlapSeconds = TickRateDecimal > 0.0 ? static_cast<double>(-DeltaFrames) / TickRateDecimal : 0.0;
				Output += FString::Printf(TEXT("  ! Overlap with previous shot: %.2fs\n"), OverlapSeconds);
			}
		}

		if (bHasPrevCamera && CameraGuid.IsValid() && CameraGuid == PrevCameraGuid)
		{
			IssueCount++;
			Output += TEXT("  ! Same camera as previous shot - check if angle/lens change is sufficient\n");
		}

		if (bHasEnd)
		{
			bHasPrevEnd = true;
			PrevEnd = EndFrame;
		}
		else
		{
			bHasPrevEnd = false;
		}

		if (CameraGuid.IsValid())
		{
			bHasPrevCamera = true;
			PrevCameraGuid = CameraGuid;
		}
		else
		{
			bHasPrevCamera = false;
		}
	}

	const double AverageDuration = BoundedShotCount > 0 ? TotalDurationSeconds / static_cast<double>(BoundedShotCount) : 0.0;
	Output += TEXT("\n## Summary\n");
	Output += FString::Printf(TEXT("- Unique cameras used: %d\n"), UniqueCameraGuids.Num());
	Output += FString::Printf(TEXT("- Average shot duration: %.2fs (%d bounded shots)\n"), AverageDuration, BoundedShotCount);
	Output += FString::Printf(TEXT("- Issues found: %d\n"), IssueCount);

	if (ReviewPoints.Num() > 0)
	{
		Output += TEXT("\n## Suggested Screenshot Review Timestamps\n");
		for (const FString& Point : ReviewPoints)
		{
			Output += Point + TEXT("\n");
		}
		Output += TEXT("\nUse these timestamps to review composition/continuity, then refine cuts/transforms and re-run analyze_camera_cuts.\n");
	}

	return FToolResult::Ok(Output);
}

FToolResult FEditSequencerTool::ExecuteShotPlan(ULevelSequence* Sequence, const TSharedPtr<FJsonObject>& Args)
{
	if (!Sequence || !Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid sequence or arguments"));
	}

	UMovieScene* MovieScene = Sequence->GetMovieScene();
	if (!MovieScene)
	{
		return FToolResult::Fail(TEXT("Level Sequence has no MovieScene"));
	}

	const TArray<TSharedPtr<FJsonValue>>* ShotsArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("shots"), ShotsArray) || !ShotsArray || ShotsArray->Num() == 0)
	{
		return FToolResult::Fail(TEXT("execute_shot_plan requires non-empty 'shots' array"));
	}

	bool bReplaceCameraCuts = false;
	Args->TryGetBoolField(TEXT("replace_camera_cuts"), bReplaceCameraCuts);

	FString DefaultTargetActor;
	Args->TryGetStringField(TEXT("default_target_actor"), DefaultTargetActor);

	struct FShotPlanShot
	{
		FString Name;
		double StartTime = 0.0;
		double EndTime = 0.0;
		FString CameraBinding;
		FString CameraActor;
		FString TargetActor;
		FString ShotSize = TEXT("medium");
		FString Side = TEXT("front");
		FString Angle = TEXT("eye_level");
		FString Movement = TEXT("static");
		double DistanceScale = 1.0;
		double HeightOffset = 0.0;
	};

	TArray<FShotPlanShot> ParsedShots;
	TArray<FString> ParseErrors;
	ParsedShots.Reserve(ShotsArray->Num());

	for (int32 ShotIndex = 0; ShotIndex < ShotsArray->Num(); ++ShotIndex)
	{
		const TSharedPtr<FJsonValue>& Value = (*ShotsArray)[ShotIndex];
		const TSharedPtr<FJsonObject>* Obj = nullptr;
		if (!Value->TryGetObject(Obj) || !Obj || !Obj->IsValid())
		{
			ParseErrors.Add(FString::Printf(TEXT("Shot %d: must be an object"), ShotIndex + 1));
			continue;
		}

		FShotPlanShot Shot;
		(*Obj)->TryGetStringField(TEXT("name"), Shot.Name);
		if (Shot.Name.IsEmpty())
		{
			Shot.Name = FString::Printf(TEXT("Shot %d"), ShotIndex + 1);
		}

		if (!(*Obj)->TryGetNumberField(TEXT("start_time"), Shot.StartTime))
		{
			ParseErrors.Add(FString::Printf(TEXT("%s: missing required start_time"), *Shot.Name));
			continue;
		}
		if (!(*Obj)->TryGetNumberField(TEXT("end_time"), Shot.EndTime))
		{
			ParseErrors.Add(FString::Printf(TEXT("%s: missing required end_time"), *Shot.Name));
			continue;
		}
		if (!FMath::IsFinite(Shot.StartTime) || !FMath::IsFinite(Shot.EndTime))
		{
			ParseErrors.Add(FString::Printf(TEXT("%s: start_time/end_time must be finite numbers"), *Shot.Name));
			continue;
		}
		if (Shot.EndTime <= Shot.StartTime)
		{
			ParseErrors.Add(FString::Printf(TEXT("%s: end_time must be greater than start_time"), *Shot.Name));
			continue;
		}

		(*Obj)->TryGetStringField(TEXT("camera_binding"), Shot.CameraBinding);
		(*Obj)->TryGetStringField(TEXT("camera_actor"), Shot.CameraActor);
		(*Obj)->TryGetStringField(TEXT("target_actor"), Shot.TargetActor);
		(*Obj)->TryGetStringField(TEXT("shot_size"), Shot.ShotSize);
		(*Obj)->TryGetStringField(TEXT("side"), Shot.Side);
		(*Obj)->TryGetStringField(TEXT("angle"), Shot.Angle);
		(*Obj)->TryGetStringField(TEXT("movement"), Shot.Movement);
		(*Obj)->TryGetNumberField(TEXT("distance_scale"), Shot.DistanceScale);
		(*Obj)->TryGetNumberField(TEXT("height_offset"), Shot.HeightOffset);

		Shot.ShotSize = Shot.ShotSize.ToLower();
		Shot.Side = Shot.Side.ToLower();
		Shot.Angle = Shot.Angle.ToLower();
		Shot.Movement = Shot.Movement.ToLower();
		Shot.DistanceScale = FMath::Max(0.1, Shot.DistanceScale);

		ParsedShots.Add(MoveTemp(Shot));
	}

	if (ParsedShots.Num() == 0)
	{
		return FToolResult::Fail(FString::Printf(TEXT("No valid shots to execute.\n%s"), *FString::Join(ParseErrors, TEXT("\n"))));
	}

	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "ExecuteShotPlan", "AI Execute Shot Plan: {0}"),
		FText::FromString(Sequence->GetName())));

	auto IsLikelyCameraClass = [](const UClass* InClass) -> bool
	{
		if (!InClass)
		{
			return false;
		}
		if (InClass->IsChildOf(ACameraActor::StaticClass()))
		{
			return true;
		}
		return InClass->GetName().Contains(TEXT("Camera"));
	};

	auto CollectCameraBindings = [&]() -> TArray<FString>
	{
		TArray<FString> Result;
		TSet<FString> Seen;
		Result.Reserve(MovieScene->GetPossessableCount() + MovieScene->GetSpawnableCount());

		for (int32 i = 0; i < MovieScene->GetPossessableCount(); ++i)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
			if (IsLikelyCameraClass(Possessable.GetPossessedObjectClass()))
			{
				if (!Seen.Contains(Possessable.GetName()))
				{
					Seen.Add(Possessable.GetName());
					Result.Add(Possessable.GetName());
				}
			}
		}

		for (int32 i = 0; i < MovieScene->GetSpawnableCount(); ++i)
		{
			const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
			const UClass* SpawnClass = nullptr;
			if (const UObject* TemplateObj = Spawnable.GetObjectTemplate())
			{
				SpawnClass = TemplateObj->GetClass();
			}
			if (IsLikelyCameraClass(SpawnClass))
			{
				if (!Seen.Contains(Spawnable.GetName()))
				{
					Seen.Add(Spawnable.GetName());
					Result.Add(Spawnable.GetName());
				}
			}
		}

		Result.Sort();
		return Result;
	};

	auto FindFirstCameraActorInLevel = [&]() -> ACameraActor*
	{
		if (!GEditor)
		{
			return nullptr;
		}
		UWorld* World = GEditor->GetEditorWorldContext().World();
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<ACameraActor> It(World); It; ++It)
		{
			ACameraActor* CameraActor = *It;
			if (CameraActor)
			{
				return CameraActor;
			}
		}
		return nullptr;
	};

	auto EnsureBindingForActor = [&](AActor* Actor, const FString& PreferredBindingName, FString& OutBindingName, FString& OutError) -> bool
	{
		OutBindingName.Reset();
		OutError.Reset();
		if (!Actor)
		{
			OutError = TEXT("Invalid actor for binding");
			return false;
		}

		FString CandidateName = PreferredBindingName;
		if (!CandidateName.IsEmpty())
		{
			const FGuid Existing = FindBindingByName(MovieScene, CandidateName);
			if (Existing.IsValid())
			{
				OutBindingName = CandidateName;
				return true;
			}
		}

		const FString LabelName = Actor->GetActorLabel();
		FGuid ExistingByLabel = FindBindingByName(MovieScene, LabelName);
		if (ExistingByLabel.IsValid())
		{
			OutBindingName = LabelName;
			return true;
		}

		const FString ObjectName = Actor->GetName();
		FGuid ExistingByObjectName = FindBindingByName(MovieScene, ObjectName);
		if (ExistingByObjectName.IsValid())
		{
			OutBindingName = ObjectName;
			return true;
		}

		if (CandidateName.IsEmpty())
		{
			CandidateName = !LabelName.IsEmpty() ? LabelName : ObjectName;
		}
		if (CandidateName.IsEmpty())
		{
			OutError = TEXT("Could not determine binding name for actor");
			return false;
		}

		const FGuid NewGuid = MovieScene->AddPossessable(CandidateName, Actor->GetClass());
		if (!NewGuid.IsValid())
		{
			OutError = FString::Printf(TEXT("Failed to create binding for actor '%s'"), *Actor->GetName());
			return false;
		}

		Sequence->BindPossessableObject(NewGuid, *Actor, Actor->GetWorld());
		OutBindingName = CandidateName;
		return true;
	};

	auto EnsureTransformTrackRange = [&](const FString& BindingName, double StartTime, double EndTime, FString& OutError) -> bool
	{
		OutError.Reset();
		FGuid BindingGuid = FindBindingByName(MovieScene, BindingName);
		if (!BindingGuid.IsValid())
		{
			OutError = FString::Printf(TEXT("Binding '%s' not found"), *BindingName);
			return false;
		}

		auto FindTransformTrack = [&]() -> UMovieScene3DTransformTrack*
		{
			const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
			if (!Binding)
			{
				return nullptr;
			}
			for (UMovieSceneTrack* Track : Binding->GetTracks())
			{
				if (UMovieScene3DTransformTrack* TransformTrack = Cast<UMovieScene3DTransformTrack>(Track))
				{
					return TransformTrack;
				}
			}
			return nullptr;
		};

		UMovieScene3DTransformTrack* TransformTrack = FindTransformTrack();
		if (!TransformTrack)
		{
			FTrackDef TrackDef;
			TrackDef.Binding = BindingName;
			TrackDef.TrackType = TEXT("3DTransform");
			TrackDef.StartTime = StartTime;
			TrackDef.EndTime = EndTime;
			FAddedTrack Added;
			FString AddResult = AddTrack(Sequence, TrackDef, Added);
			if (AddResult.StartsWith(TEXT("ERROR:")))
			{
				OutError = AddResult;
				return false;
			}
			TransformTrack = FindTransformTrack();
		}

		if (!TransformTrack)
		{
			OutError = FString::Printf(TEXT("Could not get transform track for '%s'"), *BindingName);
			return false;
		}

		UMovieSceneSection* Section = nullptr;
		const TArray<UMovieSceneSection*>& Sections = TransformTrack->GetAllSections();
		if (Sections.Num() > 0)
		{
			Section = Sections[0];
		}
		else
		{
			Section = TransformTrack->CreateNewSection();
			if (Section)
			{
				TransformTrack->AddSection(*Section);
			}
		}

		if (!Section)
		{
			OutError = TEXT("Failed to create transform section");
			return false;
		}

		FFrameNumber StartFrame = SecondsToFrame(MovieScene, StartTime);
		FFrameNumber EndFrame = SecondsToFrame(MovieScene, EndTime);
		if (EndFrame <= StartFrame)
		{
			EndFrame = StartFrame + FFrameNumber(1);
		}

		const TRange<FFrameNumber> ExistingRange = Section->GetRange();
		if (ExistingRange.HasLowerBound())
		{
			StartFrame = FMath::Min(StartFrame, ExistingRange.GetLowerBoundValue());
		}
		if (ExistingRange.HasUpperBound())
		{
			EndFrame = FMath::Max(EndFrame, ExistingRange.GetUpperBoundValue());
		}
		if (EndFrame <= StartFrame)
		{
			EndFrame = StartFrame + FFrameNumber(1);
		}

		Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
		return true;
	};

	auto GetShotDistance = [](const FString& ShotSize) -> double
	{
		if (ShotSize == TEXT("wide")) return 1200.0;
		if (ShotSize == TEXT("closeup")) return 260.0;
		if (ShotSize == TEXT("extreme_closeup")) return 140.0;
		return 700.0; // medium default
	};

	auto GetSideDirection = [](const FString& Side, const FVector& Forward, const FVector& Right) -> FVector
	{
		if (Side == TEXT("back")) return (-Forward).GetSafeNormal();
		if (Side == TEXT("left")) return (-Right).GetSafeNormal();
		if (Side == TEXT("right")) return Right.GetSafeNormal();
		if (Side == TEXT("front_left")) return (Forward - Right).GetSafeNormal();
		if (Side == TEXT("front_right")) return (Forward + Right).GetSafeNormal();
		if (Side == TEXT("back_left")) return ((-Forward) - Right).GetSafeNormal();
		if (Side == TEXT("back_right")) return ((-Forward) + Right).GetSafeNormal();
		return Forward.GetSafeNormal(); // front default
	};

	auto GetAngleOffsets = [](const FString& Angle, double& OutHeight, double& OutRoll)
	{
		OutHeight = 0.0;
		OutRoll = 0.0;
		if (Angle == TEXT("high_angle")) OutHeight = 140.0;
		else if (Angle == TEXT("low_angle")) OutHeight = -90.0;
		else if (Angle == TEXT("bird_eye")) OutHeight = 450.0;
		else if (Angle == TEXT("worm_eye")) OutHeight = -220.0;
		else if (Angle == TEXT("dutch_left")) OutRoll = -12.0;
		else if (Angle == TEXT("dutch_right")) OutRoll = 12.0;
	};

	TArray<FString> AutoCameraBindings = CollectCameraBindings();
	int32 AutoCameraBindingIndex = 0;

	if (bReplaceCameraCuts && MovieScene->GetCameraCutTrack())
	{
		MovieScene->RemoveCameraCutTrack();
	}

	TArray<FString> AppliedCuts;
	TArray<FString> AppliedTransforms;
	TArray<FString> Warnings;
	TArray<FString> Errors = ParseErrors;
	bool bAnyChanges = false;

	for (const FShotPlanShot& Shot : ParsedShots)
	{
		FString CameraBindingName = Shot.CameraBinding;

		if (!CameraBindingName.IsEmpty())
		{
			if (!FindBindingByName(MovieScene, CameraBindingName).IsValid())
			{
				Errors.Add(FString::Printf(TEXT("%s: camera_binding '%s' not found"), *Shot.Name, *CameraBindingName));
				continue;
			}
		}
		else if (!Shot.CameraActor.IsEmpty())
		{
			AActor* CameraActor = FindActorByName(Shot.CameraActor);
			if (!CameraActor)
			{
				Errors.Add(FString::Printf(TEXT("%s: camera_actor '%s' not found"), *Shot.Name, *Shot.CameraActor));
				continue;
			}
			FString BindError;
			if (!EnsureBindingForActor(CameraActor, TEXT(""), CameraBindingName, BindError))
			{
				Errors.Add(FString::Printf(TEXT("%s: %s"), *Shot.Name, *BindError));
				continue;
			}
		}
		else if (AutoCameraBindings.Num() > 0)
		{
			CameraBindingName = AutoCameraBindings[AutoCameraBindingIndex % AutoCameraBindings.Num()];
			AutoCameraBindingIndex++;
		}
		else
		{
			ACameraActor* FallbackCamera = FindFirstCameraActorInLevel();
			if (!FallbackCamera)
			{
				Errors.Add(FString::Printf(TEXT("%s: no camera_binding/camera_actor provided and no camera actors found in level"), *Shot.Name));
				continue;
			}

			FString BindError;
			if (!EnsureBindingForActor(FallbackCamera, TEXT(""), CameraBindingName, BindError))
			{
				Errors.Add(FString::Printf(TEXT("%s: %s"), *Shot.Name, *BindError));
				continue;
			}

			AutoCameraBindings = CollectCameraBindings();
		}

		FTrackDef CameraCutDef;
		CameraCutDef.TrackType = TEXT("CameraCut");
		CameraCutDef.StartTime = Shot.StartTime;
		CameraCutDef.EndTime = Shot.EndTime;
		CameraCutDef.CameraBinding = CameraBindingName;

		FAddedTrack AddedCut;
		FString CutResult = AddTrack(Sequence, CameraCutDef, AddedCut);
		if (CutResult.StartsWith(TEXT("ERROR:")))
		{
			Errors.Add(FString::Printf(TEXT("%s: %s"), *Shot.Name, *CutResult));
			continue;
		}

		AppliedCuts.Add(FString::Printf(TEXT("%s -> %s (%.2fs - %.2fs)"),
			*Shot.Name, *CameraBindingName, Shot.StartTime, Shot.EndTime));
		bAnyChanges = true;

		const FString TargetActorName = Shot.TargetActor.IsEmpty() ? DefaultTargetActor : Shot.TargetActor;
		if (TargetActorName.IsEmpty())
		{
			Warnings.Add(FString::Printf(TEXT("%s: no target_actor/default_target_actor; only camera cut was added"), *Shot.Name));
			continue;
		}

		AActor* TargetActor = FindActorByName(TargetActorName);
		if (!TargetActor)
		{
			Warnings.Add(FString::Printf(TEXT("%s: target actor '%s' not found; only camera cut was added"), *Shot.Name, *TargetActorName));
			continue;
		}

		FString EnsureTrackError;
		if (!EnsureTransformTrackRange(CameraBindingName, Shot.StartTime, Shot.EndTime, EnsureTrackError))
		{
			Warnings.Add(FString::Printf(TEXT("%s: %s"), *Shot.Name, *EnsureTrackError));
			continue;
		}

		const FVector TargetLocation = TargetActor->GetActorLocation();
		const FVector TargetForward = TargetActor->GetActorForwardVector().GetSafeNormal();
		const FVector TargetRight = TargetActor->GetActorRightVector().GetSafeNormal();
		const FVector SideDirection = GetSideDirection(Shot.Side, TargetForward, TargetRight);

		double AngleHeight = 0.0;
		double AngleRoll = 0.0;
		GetAngleOffsets(Shot.Angle, AngleHeight, AngleRoll);

		const double BaseDistance = GetShotDistance(Shot.ShotSize) * Shot.DistanceScale;
		const double HeightOffset = AngleHeight + Shot.HeightOffset;
		const FVector StartLocation = TargetLocation + (SideDirection * BaseDistance) + FVector(0.0, 0.0, HeightOffset);

		FVector EndDirection = SideDirection;
		double EndDistance = BaseDistance;
		if (Shot.Movement == TEXT("push_in"))
		{
			EndDistance = BaseDistance * 0.8;
		}
		else if (Shot.Movement == TEXT("pull_out"))
		{
			EndDistance = BaseDistance * 1.2;
		}
		else if (Shot.Movement == TEXT("orbit_left"))
		{
			EndDirection = FRotator(0.0, -20.0, 0.0).RotateVector(SideDirection).GetSafeNormal();
		}
		else if (Shot.Movement == TEXT("orbit_right"))
		{
			EndDirection = FRotator(0.0, 20.0, 0.0).RotateVector(SideDirection).GetSafeNormal();
		}

		const FVector EndLocation = TargetLocation + (EndDirection * EndDistance) + FVector(0.0, 0.0, HeightOffset);
		FRotator StartRotation = (TargetLocation - StartLocation).Rotation();
		FRotator EndRotation = (TargetLocation - EndLocation).Rotation();
		StartRotation.Roll += AngleRoll;
		EndRotation.Roll += AngleRoll;

		FTransformKeyDef StartKey;
		StartKey.Binding = CameraBindingName;
		StartKey.Time = Shot.StartTime;
		StartKey.bHasLocation = true;
		StartKey.Location = StartLocation;
		StartKey.bHasRotation = true;
		StartKey.Rotation = StartRotation;
		StartKey.Interp = TEXT("cubic");

		FTransformKeyDef EndKey;
		EndKey.Binding = CameraBindingName;
		EndKey.Time = Shot.EndTime;
		EndKey.bHasLocation = true;
		EndKey.Location = EndLocation;
		EndKey.bHasRotation = true;
		EndKey.Rotation = EndRotation;
		EndKey.Interp = TEXT("cubic");

		FString StartTransformResult = AddTransformKeys(Sequence, StartKey);
		if (StartTransformResult.StartsWith(TEXT("ERROR:")))
		{
			Warnings.Add(FString::Printf(TEXT("%s: %s"), *Shot.Name, *StartTransformResult));
			continue;
		}

		FString EndTransformResult = AddTransformKeys(Sequence, EndKey);
		if (EndTransformResult.StartsWith(TEXT("ERROR:")))
		{
			Warnings.Add(FString::Printf(TEXT("%s: %s"), *Shot.Name, *EndTransformResult));
			continue;
		}

		AppliedTransforms.Add(FString::Printf(TEXT("%s -> target '%s' (%s, %s, %s)"),
			*Shot.Name, *TargetActorName, *Shot.ShotSize, *Shot.Angle, *Shot.Movement));
	}

	if (bAnyChanges)
	{
		Sequence->Modify();
		Sequence->MarkPackageDirty();
	}

	FString Output = FString::Printf(TEXT("# EXECUTE SHOT PLAN: %s\n\n"), *Sequence->GetName());
	Output += FString::Printf(TEXT("Requested shots: %d\n"), ParsedShots.Num());
	Output += FString::Printf(TEXT("Applied camera cuts: %d\n"), AppliedCuts.Num());
	Output += FString::Printf(TEXT("Applied camera transforms: %d\n"), AppliedTransforms.Num());
	Output += TEXT("\n");

	if (AppliedCuts.Num() > 0)
	{
		Output += TEXT("## Cuts Applied\n");
		for (const FString& Cut : AppliedCuts)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Cut);
		}
		Output += TEXT("\n");
	}

	if (AppliedTransforms.Num() > 0)
	{
		Output += TEXT("## Camera Blocking Applied\n");
		for (const FString& T : AppliedTransforms)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *T);
		}
		Output += TEXT("\n");
	}

	if (Warnings.Num() > 0)
	{
		Output += TEXT("## Warnings\n");
		for (const FString& W : Warnings)
		{
			Output += FString::Printf(TEXT("! %s\n"), *W);
		}
		Output += TEXT("\n");
	}

	if (Errors.Num() > 0)
	{
		Output += TEXT("## Errors\n");
		for (const FString& E : Errors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *E);
		}
		Output += TEXT("\n");
	}

	FToolResult Analysis = AnalyzeCameraCuts(Sequence);
	if (Analysis.bSuccess && !Analysis.Output.IsEmpty())
	{
		Output += TEXT("## Camera Cut Review\n");
		Output += Analysis.Output + TEXT("\n");
	}

	Output += TEXT("Next pass: use screenshot at the suggested timestamps, then run execute_shot_plan again with refined shots or edit_sequencer for manual polish.\n");

	if (!bAnyChanges)
	{
		return FToolResult::Fail(Output);
	}
	return FToolResult::Ok(Output);
}

// ========== Dynamic Track Resolution ==========

UClass* FEditSequencerTool::ResolveTrackClass(const FString& TrackType)
{
	if (TrackType.IsEmpty())
	{
		return nullptr;
	}

	TArray<UClass*> DerivedClasses;
	GetDerivedClasses(UMovieSceneTrack::StaticClass(), DerivedClasses);

	// Try exact match against friendly name (case-insensitive)
	for (UClass* TrackClass : DerivedClasses)
	{
		if (!TrackClass || TrackClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		FString FriendlyName = GetFriendlyTrackName(TrackClass);
		if (FriendlyName.Equals(TrackType, ESearchCase::IgnoreCase))
		{
			return TrackClass;
		}
	}

	// Try matching against full class name
	for (UClass* TrackClass : DerivedClasses)
	{
		if (!TrackClass || TrackClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		if (TrackClass->GetName().Equals(TrackType, ESearchCase::IgnoreCase))
		{
			return TrackClass;
		}

		// Also try with MovieScene prefix
		FString WithPrefix = FString::Printf(TEXT("MovieScene%sTrack"), *TrackType);
		if (TrackClass->GetName().Equals(WithPrefix, ESearchCase::IgnoreCase))
		{
			return TrackClass;
		}
	}

	// Try partial/contains match as last resort
	for (UClass* TrackClass : DerivedClasses)
	{
		if (!TrackClass || TrackClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		if (TrackClass->GetName().Contains(TrackType))
		{
			return TrackClass;
		}
	}

	return nullptr;
}

FString FEditSequencerTool::GetFriendlyTrackName(UClass* TrackClass)
{
	if (!TrackClass)
	{
		return TEXT("Unknown");
	}

	FString Name = TrackClass->GetName();
	Name.RemoveFromStart(TEXT("MovieScene"));
	Name.RemoveFromEnd(TEXT("Track"));

	if (Name.IsEmpty())
	{
		return TrackClass->GetName();
	}

	return Name;
}

// ========== Parsing ==========

bool FEditSequencerTool::ParseBindingDef(const TSharedPtr<FJsonObject>& Obj, FBindingDef& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("actor_name"), OutDef.ActorName) || OutDef.ActorName.IsEmpty())
	{
		OutError = TEXT("Binding missing required 'actor_name' field");
		return false;
	}

	Obj->TryGetStringField(TEXT("component_name"), OutDef.ComponentName);
	Obj->TryGetStringField(TEXT("name"), OutDef.Name);

	if (OutDef.Name.IsEmpty())
	{
		OutDef.Name = OutDef.ComponentName.IsEmpty() ? OutDef.ActorName : OutDef.ComponentName;
	}

	return true;
}

bool FEditSequencerTool::ParseTrackDef(const TSharedPtr<FJsonObject>& Obj, FTrackDef& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("track_type"), OutDef.TrackType) || OutDef.TrackType.IsEmpty())
	{
		OutError = TEXT("Track missing required 'track_type' field. Use list_track_types to see available types.");
		return false;
	}

	if (!ResolveTrackClass(OutDef.TrackType))
	{
		OutError = FString::Printf(TEXT("Unknown track_type '%s'. Use action='list_track_types' to see available types."), *OutDef.TrackType);
		return false;
	}

	Obj->TryGetStringField(TEXT("binding"), OutDef.Binding);
	Obj->TryGetNumberField(TEXT("start_time"), OutDef.StartTime);
	Obj->TryGetNumberField(TEXT("end_time"), OutDef.EndTime);

	// Audio
	Obj->TryGetStringField(TEXT("sound_asset"), OutDef.SoundAsset);
	double Volume = 1.0;
	if (Obj->TryGetNumberField(TEXT("sound_volume"), Volume))
	{
		OutDef.SoundVolume = static_cast<float>(Volume);
	}
	double Pitch = 1.0;
	if (Obj->TryGetNumberField(TEXT("sound_pitch"), Pitch))
	{
		OutDef.SoundPitch = static_cast<float>(Pitch);
	}

	// Animation
	Obj->TryGetStringField(TEXT("animation_asset"), OutDef.AnimationAsset);

	// Camera cut
	Obj->TryGetStringField(TEXT("camera_binding"), OutDef.CameraBinding);

	// Property
	Obj->TryGetStringField(TEXT("property_name"), OutDef.PropertyName);
	Obj->TryGetStringField(TEXT("property_path"), OutDef.PropertyPath);
	if (OutDef.PropertyPath.IsEmpty())
	{
		OutDef.PropertyPath = OutDef.PropertyName;
	}

	return true;
}

bool FEditSequencerTool::ParseKeyframeDef(const TSharedPtr<FJsonObject>& Obj, FKeyframeDef& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("binding"), OutDef.Binding) || OutDef.Binding.IsEmpty())
	{
		OutError = TEXT("Keyframe missing required 'binding' field");
		return false;
	}

	if (!Obj->TryGetStringField(TEXT("track_type"), OutDef.TrackType) || OutDef.TrackType.IsEmpty())
	{
		OutError = TEXT("Keyframe missing required 'track_type' field");
		return false;
	}

	// Channel index
	double ChannelIndexD = 0;
	Obj->TryGetNumberField(TEXT("channel_index"), ChannelIndexD);
	OutDef.ChannelIndex = static_cast<int32>(ChannelIndexD);

	// Track index (optional, for when there are multiple tracks of same type)
	double TrackIndexD = -1;
	if (Obj->TryGetNumberField(TEXT("track_index"), TrackIndexD))
	{
		OutDef.TrackIndex = static_cast<int32>(TrackIndexD);
	}

	if (!Obj->TryGetNumberField(TEXT("time"), OutDef.Time))
	{
		OutError = TEXT("Keyframe missing required 'time' field (seconds)");
		return false;
	}

	Obj->TryGetNumberField(TEXT("value"), OutDef.Value);
	Obj->TryGetBoolField(TEXT("bool_value"), OutDef.BoolValue);

	Obj->TryGetStringField(TEXT("interp"), OutDef.Interp);
	if (OutDef.Interp.IsEmpty())
	{
		OutDef.Interp = TEXT("cubic");
	}

	return true;
}

bool FEditSequencerTool::ParseTransformKeyDef(const TSharedPtr<FJsonObject>& Obj, FTransformKeyDef& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("binding"), OutDef.Binding) || OutDef.Binding.IsEmpty())
	{
		OutError = TEXT("Transform key missing required 'binding' field");
		return false;
	}

	if (!Obj->TryGetNumberField(TEXT("time"), OutDef.Time))
	{
		OutError = TEXT("Transform key missing required 'time' field (seconds)");
		return false;
	}

	// Location [x, y, z]
	const TArray<TSharedPtr<FJsonValue>>* LocationArr;
	if (Obj->TryGetArrayField(TEXT("location"), LocationArr) && LocationArr->Num() >= 3)
	{
		OutDef.bHasLocation = true;
		OutDef.Location.X = (*LocationArr)[0]->AsNumber();
		OutDef.Location.Y = (*LocationArr)[1]->AsNumber();
		OutDef.Location.Z = (*LocationArr)[2]->AsNumber();
	}

	// Rotation [pitch, yaw, roll] in degrees
	const TArray<TSharedPtr<FJsonValue>>* RotationArr;
	if (Obj->TryGetArrayField(TEXT("rotation"), RotationArr) && RotationArr->Num() >= 3)
	{
		OutDef.bHasRotation = true;
		OutDef.Rotation.Pitch = (*RotationArr)[0]->AsNumber();
		OutDef.Rotation.Yaw = (*RotationArr)[1]->AsNumber();
		OutDef.Rotation.Roll = (*RotationArr)[2]->AsNumber();
	}

	// Scale [x, y, z]
	const TArray<TSharedPtr<FJsonValue>>* ScaleArr;
	if (Obj->TryGetArrayField(TEXT("scale"), ScaleArr) && ScaleArr->Num() >= 3)
	{
		OutDef.bHasScale = true;
		OutDef.Scale.X = (*ScaleArr)[0]->AsNumber();
		OutDef.Scale.Y = (*ScaleArr)[1]->AsNumber();
		OutDef.Scale.Z = (*ScaleArr)[2]->AsNumber();
	}

	if (!OutDef.bHasLocation && !OutDef.bHasRotation && !OutDef.bHasScale)
	{
		OutError = TEXT("Transform key must have at least one of: location, rotation, scale");
		return false;
	}

	Obj->TryGetStringField(TEXT("interp"), OutDef.Interp);
	if (OutDef.Interp.IsEmpty())
	{
		OutDef.Interp = TEXT("cubic");
	}

	return true;
}

// ========== Operations: AddBinding ==========

FString FEditSequencerTool::AddBinding(ULevelSequence* Sequence, const FBindingDef& Def, FAddedBinding& OutResult)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	// Check if binding already exists
	FGuid ExistingGuid = FindBindingByName(MovieScene, Def.Name);
	if (ExistingGuid.IsValid())
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' already exists"), *Def.Name);
	}

	// Find actor
	AActor* Actor = FindActorByName(Def.ActorName);
	if (!Actor)
	{
		return FString::Printf(TEXT("ERROR: Actor '%s' not found in level"), *Def.ActorName);
	}

	// Component binding
	if (!Def.ComponentName.IsEmpty())
	{
		UActorComponent* Component = nullptr;

		// Exact name match
		for (UActorComponent* Comp : Actor->GetComponents())
		{
			if (Comp && Comp->GetName().Equals(Def.ComponentName, ESearchCase::IgnoreCase))
			{
				Component = Comp;
				break;
			}
		}

		// Partial match
		if (!Component)
		{
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp && Comp->GetName().Contains(Def.ComponentName))
				{
					Component = Comp;
					break;
				}
			}
		}

		// Class name match
		if (!Component)
		{
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp && Comp->GetClass()->GetName().Contains(Def.ComponentName))
				{
					Component = Comp;
					break;
				}
			}
		}

		if (!Component)
		{
			TArray<FString> ComponentNames;
			for (UActorComponent* Comp : Actor->GetComponents())
			{
				if (Comp)
				{
					ComponentNames.Add(FString::Printf(TEXT("%s (%s)"), *Comp->GetName(), *Comp->GetClass()->GetName()));
				}
			}
			return FString::Printf(TEXT("ERROR: Component '%s' not found on actor '%s'. Available: %s"),
				*Def.ComponentName, *Actor->GetActorLabel(), *FString::Join(ComponentNames, TEXT(", ")));
		}

		// Find or create actor binding as parent
		FString ActorBindingName = Actor->GetActorLabel();
		FGuid ActorGuid = FindBindingByName(MovieScene, ActorBindingName);
		if (!ActorGuid.IsValid())
		{
			ActorGuid = FindBindingByName(MovieScene, Actor->GetName());
		}

		if (!ActorGuid.IsValid())
		{
			ActorGuid = MovieScene->AddPossessable(ActorBindingName, Actor->GetClass());
			if (!ActorGuid.IsValid())
			{
				return TEXT("ERROR: Failed to create actor binding for component parent");
			}
			Sequence->BindPossessableObject(ActorGuid, *Actor, Actor->GetWorld());
		}

		// Create component binding
		FGuid ComponentGuid = MovieScene->AddPossessable(Def.Name, Component->GetClass());
		if (!ComponentGuid.IsValid())
		{
			return TEXT("ERROR: Failed to create component binding");
		}

		FMovieScenePossessable* Possessable = MovieScene->FindPossessable(ComponentGuid);
		if (Possessable)
		{
			Possessable->SetParent(ActorGuid, MovieScene);
		}

		Sequence->BindPossessableObject(ComponentGuid, *Component, Actor);

		OutResult.Name = Def.Name;
		OutResult.ActorPath = Component->GetPathName();
		OutResult.Guid = ComponentGuid;

		return FString::Printf(TEXT("Bound '%s' to component '%s' on actor '%s'"),
			*Def.Name, *Component->GetName(), *Actor->GetActorLabel());
	}

	// Actor binding
	FGuid NewGuid = MovieScene->AddPossessable(Def.Name, Actor->GetClass());
	if (!NewGuid.IsValid())
	{
		return TEXT("ERROR: Failed to create possessable binding");
	}

	Sequence->BindPossessableObject(NewGuid, *Actor, Actor->GetWorld());

	OutResult.Name = Def.Name;
	OutResult.ActorPath = Actor->GetPathName();
	OutResult.Guid = NewGuid;

	return FString::Printf(TEXT("Bound '%s' to actor '%s'"), *Def.Name, *Actor->GetActorLabel());
}

// ========== Operations: AddTrack ==========

FString FEditSequencerTool::AddTrack(ULevelSequence* Sequence, const FTrackDef& Def, FAddedTrack& OutResult)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	UClass* TrackClass = ResolveTrackClass(Def.TrackType);
	if (!TrackClass)
	{
		return FString::Printf(TEXT("ERROR: Unknown track type '%s'"), *Def.TrackType);
	}

	const bool bIsMasterTrack = Def.Binding.IsEmpty();
	const bool bIsCameraCutTrack = TrackClass->IsChildOf(UMovieSceneCameraCutTrack::StaticClass());
	UMovieSceneTrack* NewTrack = nullptr;
	FGuid BindingGuid;
	bool bCreatedTrack = false;

	if (bIsCameraCutTrack && !bIsMasterTrack)
	{
		return TEXT("ERROR: CameraCut is a master track. Omit 'binding' and set 'camera_binding' instead.");
	}

	if (bIsMasterTrack)
	{
		if (bIsCameraCutTrack)
		{
			NewTrack = MovieScene->GetCameraCutTrack();
			if (!NewTrack)
			{
				NewTrack = MovieScene->AddCameraCutTrack(TrackClass);
				bCreatedTrack = true;
			}
			if (!NewTrack)
			{
				return FString::Printf(TEXT("ERROR: Failed to add %s master track"), *Def.TrackType);
			}
		}
		else
		{
			// Check if already exists
			for (UMovieSceneTrack* ExistingTrack : MovieScene->GetTracks())
			{
				if (ExistingTrack && ExistingTrack->GetClass() == TrackClass)
				{
					return FString::Printf(TEXT("ERROR: Sequence already has a %s master track"), *Def.TrackType);
				}
			}

			NewTrack = MovieScene->AddTrack(TrackClass);
			bCreatedTrack = true;
			if (!NewTrack)
			{
				return FString::Printf(TEXT("ERROR: Failed to add %s master track"), *Def.TrackType);
			}
		}
	}
	else
	{
		BindingGuid = FindBindingByName(MovieScene, Def.Binding);
		if (!BindingGuid.IsValid())
		{
			return FString::Printf(TEXT("ERROR: Binding '%s' not found"), *Def.Binding);
		}

		// Check if already exists on binding
		const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
		if (Binding)
		{
			for (UMovieSceneTrack* ExistingTrack : Binding->GetTracks())
			{
				if (ExistingTrack && ExistingTrack->GetClass() == TrackClass)
				{
					return FString::Printf(TEXT("ERROR: Binding '%s' already has a %s track"), *Def.Binding, *Def.TrackType);
				}
			}
		}

		NewTrack = MovieScene->AddTrack(TrackClass, BindingGuid);
		if (!NewTrack)
		{
			return FString::Printf(TEXT("ERROR: Failed to add %s track to '%s'"), *Def.TrackType, *Def.Binding);
		}
		bCreatedTrack = true;
	}

	FString ConfigResult;
	UMovieSceneSection* Section = nullptr;
	FFrameNumber StartFrame = SecondsToFrame(MovieScene, Def.StartTime);
	FFrameNumber EndFrame = StartFrame;
	const bool bHasExplicitEndTime = Def.EndTime >= 0.0;

	if (bIsCameraCutTrack)
	{
		if (!Def.CameraBinding.IsEmpty())
		{
			FGuid CameraGuid = FindBindingByName(MovieScene, Def.CameraBinding);
			if (!CameraGuid.IsValid())
			{
				return FString::Printf(TEXT("ERROR: camera_binding '%s' not found"), *Def.CameraBinding);
			}

			UMovieSceneCameraCutTrack* CameraCutTrack = Cast<UMovieSceneCameraCutTrack>(NewTrack);
			if (!CameraCutTrack)
			{
				return TEXT("ERROR: Internal error: CameraCut track cast failed");
			}

			UE::MovieScene::FRelativeObjectBindingID RelativeID(CameraGuid);
			FMovieSceneObjectBindingID CameraBindingID(RelativeID);
			UMovieSceneCameraCutSection* CameraCutSection = CameraCutTrack->AddNewCameraCut(CameraBindingID, StartFrame);
			if (!CameraCutSection)
			{
				return TEXT("ERROR: Failed to add CameraCut section");
			}

			Section = CameraCutSection;
			ConfigResult += FString::Printf(TEXT(" [camera: %s]"), *Def.CameraBinding);

			if (bHasExplicitEndTime)
			{
				EndFrame = SecondsToFrame(MovieScene, Def.EndTime);
				if (EndFrame <= StartFrame)
				{
					return TEXT("ERROR: end_time must be greater than start_time for CameraCut sections");
				}

				CameraCutSection->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
				CameraCutTrack->RearrangeAllSections();

				const TRange<FFrameNumber> AdjustedRange = CameraCutSection->GetRange();
				if (AdjustedRange.HasUpperBound())
				{
					EndFrame = AdjustedRange.GetUpperBoundValue();
				}
			}
			else
			{
				const TRange<FFrameNumber> SectionRange = CameraCutSection->GetRange();
				if (SectionRange.HasUpperBound())
				{
					EndFrame = SectionRange.GetUpperBoundValue();
				}
				else
				{
					EndFrame = StartFrame;
				}
			}
		}
		else
		{
			return TEXT("ERROR: CameraCut requires 'camera_binding'");
		}
	}
	else
	{
		if (bHasExplicitEndTime)
		{
			EndFrame = SecondsToFrame(MovieScene, Def.EndTime);
		}
		else
		{
			const TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
			if (PlaybackRange.HasUpperBound())
			{
				EndFrame = PlaybackRange.GetUpperBoundValue();
			}
			else
			{
				const int32 OneSecondFrames = FMath::Max(1, static_cast<int32>(FMath::RoundToDouble(MovieScene->GetTickResolution().AsDecimal())));
				EndFrame = StartFrame + FFrameNumber(OneSecondFrames);
			}
		}

		if (EndFrame <= StartFrame)
		{
			return TEXT("ERROR: end_time must be greater than start_time");
		}

		Section = NewTrack->CreateNewSection();
		if (Section)
		{
			Section->SetRange(TRange<FFrameNumber>(StartFrame, EndFrame));
			NewTrack->AddSection(*Section);

			// Configure audio section
			if (UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section))
			{
				if (!Def.SoundAsset.IsEmpty())
				{
					USoundBase* Sound = LoadObject<USoundBase>(nullptr, *Def.SoundAsset);
					if (Sound)
					{
						AudioSection->SetSound(Sound);
						ConfigResult += FString::Printf(TEXT(" [sound: %s]"), *Sound->GetName());
					}
					else
					{
						ConfigResult += FString::Printf(TEXT(" [WARNING: sound '%s' not found]"), *Def.SoundAsset);
					}
				}

				FMovieSceneChannelProxy& ChannelProxy = AudioSection->GetChannelProxy();
				TArrayView<FMovieSceneFloatChannel*> FloatChannels = ChannelProxy.GetChannels<FMovieSceneFloatChannel>();
				if (FloatChannels.Num() > 0 && Def.SoundVolume != 1.0f)
				{
					FloatChannels[0]->SetDefault(Def.SoundVolume);
					ConfigResult += FString::Printf(TEXT(" [volume: %.2f]"), Def.SoundVolume);
				}
				if (FloatChannels.Num() > 1 && Def.SoundPitch != 1.0f)
				{
					FloatChannels[1]->SetDefault(Def.SoundPitch);
					ConfigResult += FString::Printf(TEXT(" [pitch: %.2f]"), Def.SoundPitch);
				}
			}

			// Configure skeletal animation section
			if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
			{
				if (!Def.AnimationAsset.IsEmpty())
				{
					UAnimSequenceBase* Anim = LoadObject<UAnimSequenceBase>(nullptr, *Def.AnimationAsset);
					if (Anim)
					{
						AnimSection->Params.Animation = Anim;
						ConfigResult += FString::Printf(TEXT(" [animation: %s]"), *Anim->GetName());
					}
					else
					{
						ConfigResult += FString::Printf(TEXT(" [WARNING: animation '%s' not found]"), *Def.AnimationAsset);
					}
				}
			}

			// Configure property track
			if (UMovieScenePropertyTrack* PropertyTrack = Cast<UMovieScenePropertyTrack>(NewTrack))
			{
				if (!Def.PropertyName.IsEmpty())
				{
					PropertyTrack->SetPropertyNameAndPath(FName(*Def.PropertyName), Def.PropertyPath);
					ConfigResult += FString::Printf(TEXT(" [property: %s]"), *Def.PropertyPath);
				}
			}
		}
	}

	OutResult.Binding = bIsMasterTrack ? TEXT("(master)") : Def.Binding;
	OutResult.TrackType = Def.TrackType;
	OutResult.StartTime = FrameToSeconds(MovieScene, StartFrame);
	OutResult.EndTime = FrameToSeconds(MovieScene, EndFrame);

	const FString FriendlyName = GetFriendlyTrackName(TrackClass);
	if (Section)
	{
		if (bIsCameraCutTrack)
		{
			return FString::Printf(TEXT("Added %s shot %.2fs -> %.2fs%s"),
				*FriendlyName, OutResult.StartTime, OutResult.EndTime, *ConfigResult);
		}
		if (bIsMasterTrack)
		{
			return FString::Printf(TEXT("Added %s master track%s"), *FriendlyName, *ConfigResult);
		}
		return FString::Printf(TEXT("Added %s track to '%s'%s"), *FriendlyName, *Def.Binding, *ConfigResult);
	}

	if (bCreatedTrack && bIsMasterTrack)
	{
		return FString::Printf(TEXT("Added %s master track"), *FriendlyName);
	}

	return FString::Printf(TEXT("Added %s track (no section created)"), *FriendlyName);
}

// ========== Operations: AddKeyframe (generic, channel-by-index) ==========

FString FEditSequencerTool::AddKeyframe(ULevelSequence* Sequence, const FKeyframeDef& Def)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	// Find binding
	FGuid BindingGuid = FindBindingByName(MovieScene, Def.Binding);
	if (!BindingGuid.IsValid())
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' not found"), *Def.Binding);
	}

	const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
	if (!Binding)
	{
		return FString::Printf(TEXT("ERROR: No tracks for binding '%s'"), *Def.Binding);
	}

	// Resolve track class
	UClass* TrackClass = ResolveTrackClass(Def.TrackType);
	if (!TrackClass)
	{
		return FString::Printf(TEXT("ERROR: Unknown track type '%s'"), *Def.TrackType);
	}

	// Find the track
	UMovieSceneTrack* TargetTrack = nullptr;
	int32 MatchCount = 0;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (Track && Track->IsA(TrackClass))
		{
			if (Def.TrackIndex < 0 || MatchCount == Def.TrackIndex)
			{
				TargetTrack = Track;
				break;
			}
			MatchCount++;
		}
	}

	if (!TargetTrack)
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' has no %s track. Add one first."), *Def.Binding, *Def.TrackType);
	}

	// Get first section
	const TArray<UMovieSceneSection*>& Sections = TargetTrack->GetAllSections();
	if (Sections.Num() == 0)
	{
		return TEXT("ERROR: Track has no sections");
	}

	UMovieSceneSection* Section = Sections[0];
	if (!Section)
	{
		return TEXT("ERROR: Invalid section");
	}

	FFrameNumber Frame = SecondsToFrame(MovieScene, Def.Time);
	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	int32 GlobalIdx = 0;

	// Double channels
	TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = Proxy.GetChannels<FMovieSceneDoubleChannel>();
	if (Def.ChannelIndex >= GlobalIdx && Def.ChannelIndex < GlobalIdx + DoubleChannels.Num())
	{
		int32 LocalIdx = Def.ChannelIndex - GlobalIdx;
		FMovieSceneDoubleChannel* Ch = DoubleChannels[LocalIdx];
		if (!Ch) return TEXT("ERROR: Null double channel");

		if (Def.Interp == TEXT("linear"))
			Ch->AddLinearKey(Frame, Def.Value);
		else if (Def.Interp == TEXT("constant"))
			Ch->AddConstantKey(Frame, Def.Value);
		else
			Ch->AddCubicKey(Frame, Def.Value, RCTM_Auto);

		return FString::Printf(TEXT("Added key: channel %d @ %.2fs = %.4f (%s)"),
			Def.ChannelIndex, Def.Time, Def.Value, *Def.Interp);
	}
	GlobalIdx += DoubleChannels.Num();

	// Float channels
	TArrayView<FMovieSceneFloatChannel*> FloatChannels = Proxy.GetChannels<FMovieSceneFloatChannel>();
	if (Def.ChannelIndex >= GlobalIdx && Def.ChannelIndex < GlobalIdx + FloatChannels.Num())
	{
		int32 LocalIdx = Def.ChannelIndex - GlobalIdx;
		FMovieSceneFloatChannel* Ch = FloatChannels[LocalIdx];
		if (!Ch) return TEXT("ERROR: Null float channel");

		FMovieSceneFloatValue FloatVal(static_cast<float>(Def.Value));
		if (Def.Interp == TEXT("linear"))
			FloatVal.InterpMode = RCIM_Linear;
		else if (Def.Interp == TEXT("constant"))
			FloatVal.InterpMode = RCIM_Constant;
		else
			FloatVal.InterpMode = RCIM_Cubic;

		Ch->GetData().AddKey(Frame, FloatVal);

		return FString::Printf(TEXT("Added key: channel %d @ %.2fs = %.4f (%s)"),
			Def.ChannelIndex, Def.Time, Def.Value, *Def.Interp);
	}
	GlobalIdx += FloatChannels.Num();

	// Bool channels
	TArrayView<FMovieSceneBoolChannel*> BoolChannels = Proxy.GetChannels<FMovieSceneBoolChannel>();
	if (Def.ChannelIndex >= GlobalIdx && Def.ChannelIndex < GlobalIdx + BoolChannels.Num())
	{
		int32 LocalIdx = Def.ChannelIndex - GlobalIdx;
		FMovieSceneBoolChannel* Ch = BoolChannels[LocalIdx];
		if (!Ch) return TEXT("ERROR: Null bool channel");

		Ch->GetData().AddKey(Frame, Def.BoolValue);

		return FString::Printf(TEXT("Added key: channel %d @ %.2fs = %s"),
			Def.ChannelIndex, Def.Time, Def.BoolValue ? TEXT("true") : TEXT("false"));
	}
	GlobalIdx += BoolChannels.Num();

	// Integer channels
	TArrayView<FMovieSceneIntegerChannel*> IntChannels = Proxy.GetChannels<FMovieSceneIntegerChannel>();
	if (Def.ChannelIndex >= GlobalIdx && Def.ChannelIndex < GlobalIdx + IntChannels.Num())
	{
		int32 LocalIdx = Def.ChannelIndex - GlobalIdx;
		FMovieSceneIntegerChannel* Ch = IntChannels[LocalIdx];
		if (!Ch) return TEXT("ERROR: Null integer channel");

		Ch->GetData().AddKey(Frame, static_cast<int32>(Def.Value));

		return FString::Printf(TEXT("Added key: channel %d @ %.2fs = %d (%s)"),
			Def.ChannelIndex, Def.Time, static_cast<int32>(Def.Value), *Def.Interp);
	}
	GlobalIdx += IntChannels.Num();

	// Byte channels
	TArrayView<FMovieSceneByteChannel*> ByteChannels = Proxy.GetChannels<FMovieSceneByteChannel>();
	if (Def.ChannelIndex >= GlobalIdx && Def.ChannelIndex < GlobalIdx + ByteChannels.Num())
	{
		int32 LocalIdx = Def.ChannelIndex - GlobalIdx;
		FMovieSceneByteChannel* Ch = ByteChannels[LocalIdx];
		if (!Ch) return TEXT("ERROR: Null byte channel");

		Ch->GetData().AddKey(Frame, static_cast<uint8>(Def.Value));

		return FString::Printf(TEXT("Added key: channel %d @ %.2fs = %d"),
			Def.ChannelIndex, Def.Time, static_cast<uint8>(Def.Value));
	}
	GlobalIdx += ByteChannels.Num();

	return FString::Printf(TEXT("ERROR: Channel index %d out of range (total channels: %d)"),
		Def.ChannelIndex, GlobalIdx);
}

// ========== Operations: AddTransformKeys (bulk convenience) ==========

FString FEditSequencerTool::AddTransformKeys(ULevelSequence* Sequence, const FTransformKeyDef& Def)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	// Find binding
	FGuid BindingGuid = FindBindingByName(MovieScene, Def.Binding);
	if (!BindingGuid.IsValid())
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' not found"), *Def.Binding);
	}

	const FMovieSceneBinding* Binding = MovieScene->FindBinding(BindingGuid);
	if (!Binding)
	{
		return FString::Printf(TEXT("ERROR: No tracks for binding '%s'"), *Def.Binding);
	}

	// Find transform track
	UMovieScene3DTransformTrack* TransformTrack = nullptr;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		TransformTrack = Cast<UMovieScene3DTransformTrack>(Track);
		if (TransformTrack) break;
	}

	if (!TransformTrack)
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' has no 3DTransform track. Add one first."), *Def.Binding);
	}

	const TArray<UMovieSceneSection*>& Sections = TransformTrack->GetAllSections();
	if (Sections.Num() == 0)
	{
		return TEXT("ERROR: Transform track has no sections");
	}

	UMovieScene3DTransformSection* TransformSection = Cast<UMovieScene3DTransformSection>(Sections[0]);
	if (!TransformSection)
	{
		return TEXT("ERROR: Could not get transform section");
	}

	FMovieSceneChannelProxy& ChannelProxy = TransformSection->GetChannelProxy();
	TArrayView<FMovieSceneDoubleChannel*> DoubleChannels = ChannelProxy.GetChannels<FMovieSceneDoubleChannel>();

	if (DoubleChannels.Num() < 9)
	{
		return FString::Printf(TEXT("ERROR: Transform section has %d channels (expected 9)"), DoubleChannels.Num());
	}

	FFrameNumber Frame = SecondsToFrame(MovieScene, Def.Time);
	int32 KeysSet = 0;

	auto AddKey = [&](int32 ChannelIdx, double Value)
	{
		FMovieSceneDoubleChannel* Ch = DoubleChannels[ChannelIdx];
		if (!Ch) return;

		if (Def.Interp == TEXT("linear"))
			Ch->AddLinearKey(Frame, Value);
		else if (Def.Interp == TEXT("constant"))
			Ch->AddConstantKey(Frame, Value);
		else
			Ch->AddCubicKey(Frame, Value, RCTM_Auto);
		KeysSet++;
	};

	// Location: channels 0, 1, 2
	if (Def.bHasLocation)
	{
		AddKey(0, Def.Location.X);
		AddKey(1, Def.Location.Y);
		AddKey(2, Def.Location.Z);
	}

	// Rotation: channels 3, 4, 5
	if (Def.bHasRotation)
	{
		AddKey(3, Def.Rotation.Pitch);
		AddKey(4, Def.Rotation.Yaw);
		AddKey(5, Def.Rotation.Roll);
	}

	// Scale: channels 6, 7, 8
	if (Def.bHasScale)
	{
		AddKey(6, Def.Scale.X);
		AddKey(7, Def.Scale.Y);
		AddKey(8, Def.Scale.Z);
	}

	FString Parts;
	if (Def.bHasLocation) Parts += FString::Printf(TEXT("loc=(%.1f, %.1f, %.1f)"), Def.Location.X, Def.Location.Y, Def.Location.Z);
	if (Def.bHasRotation)
	{
		if (!Parts.IsEmpty()) Parts += TEXT(", ");
		Parts += FString::Printf(TEXT("rot=(%.1f, %.1f, %.1f)"), Def.Rotation.Pitch, Def.Rotation.Yaw, Def.Rotation.Roll);
	}
	if (Def.bHasScale)
	{
		if (!Parts.IsEmpty()) Parts += TEXT(", ");
		Parts += FString::Printf(TEXT("scale=(%.1f, %.1f, %.1f)"), Def.Scale.X, Def.Scale.Y, Def.Scale.Z);
	}

	return FString::Printf(TEXT("Transform '%s' @ %.2fs: %s (%d keys, %s)"),
		*Def.Binding, Def.Time, *Parts, KeysSet, *Def.Interp);
}

// ========== Operations: RemoveBinding ==========

FString FEditSequencerTool::RemoveBinding(ULevelSequence* Sequence, const FString& BindingName)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	FGuid Guid = FindBindingByName(MovieScene, BindingName);
	if (!Guid.IsValid())
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' not found"), *BindingName);
	}

	// Check if it's a possessable
	FMovieScenePossessable* Possessable = MovieScene->FindPossessable(Guid);
	if (Possessable)
	{
		if (MovieScene->RemovePossessable(Guid))
		{
			return FString::Printf(TEXT("Removed possessable binding '%s'"), *BindingName);
		}
	}

	// Check if it's a spawnable
	FMovieSceneSpawnable* Spawnable = MovieScene->FindSpawnable(Guid);
	if (Spawnable)
	{
		if (MovieScene->RemoveSpawnable(Guid))
		{
			return FString::Printf(TEXT("Removed spawnable binding '%s'"), *BindingName);
		}
	}

	return FString::Printf(TEXT("ERROR: Failed to remove binding '%s'"), *BindingName);
}

// ========== Operations: RemoveTrack ==========

FString FEditSequencerTool::RemoveTrack(ULevelSequence* Sequence, const TSharedPtr<FJsonObject>& Def)
{
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	FString BindingName;
	Def->TryGetStringField(TEXT("binding"), BindingName);

	FString TrackType;
	if (!Def->TryGetStringField(TEXT("track_type"), TrackType) || TrackType.IsEmpty())
	{
		return TEXT("ERROR: remove_tracks requires 'track_type' field");
	}

	int32 TrackIndex = 0;
	double TrackIndexD = 0;
	if (Def->TryGetNumberField(TEXT("track_index"), TrackIndexD))
	{
		TrackIndex = static_cast<int32>(TrackIndexD);
	}

	UClass* TrackClass = ResolveTrackClass(TrackType);
	if (!TrackClass)
	{
		return FString::Printf(TEXT("ERROR: Unknown track type '%s'"), *TrackType);
	}
	const bool bIsCameraCutTrack = TrackClass->IsChildOf(UMovieSceneCameraCutTrack::StaticClass());

	if (BindingName.IsEmpty())
	{
		if (bIsCameraCutTrack)
		{
			if (TrackIndex > 0)
			{
				return TEXT("ERROR: CameraCut master track supports only index 0");
			}

			UMovieSceneTrack* CameraCutTrack = MovieScene->GetCameraCutTrack();
			if (CameraCutTrack)
			{
				MovieScene->RemoveCameraCutTrack();
				return FString::Printf(TEXT("Removed %s master track"), *TrackType);
			}

			for (UMovieSceneTrack* RootTrack : MovieScene->GetTracks())
			{
				if (RootTrack && RootTrack->IsA(TrackClass))
				{
					if (MovieScene->RemoveTrack(*RootTrack))
					{
						return FString::Printf(TEXT("Removed %s master track"), *TrackType);
					}
					return FString::Printf(TEXT("ERROR: Failed to remove %s master track"), *TrackType);
				}
			}

			return FString::Printf(TEXT("ERROR: No %s master track found"), *TrackType);
		}

		// Remove master track
		int32 MatchCount = 0;
		for (UMovieSceneTrack* Track : MovieScene->GetTracks())
		{
			if (Track && Track->IsA(TrackClass))
			{
				if (MatchCount == TrackIndex)
				{
					if (MovieScene->RemoveTrack(*Track))
					{
						return FString::Printf(TEXT("Removed %s master track"), *TrackType);
					}
					return FString::Printf(TEXT("ERROR: Failed to remove %s master track"), *TrackType);
				}
				MatchCount++;
			}
		}
		return FString::Printf(TEXT("ERROR: No %s master track found"), *TrackType);
	}

	if (bIsCameraCutTrack)
	{
		return TEXT("ERROR: CameraCut is a master track. Omit 'binding' when removing it.");
	}

	// Remove binding track
	FGuid Guid = FindBindingByName(MovieScene, BindingName);
	if (!Guid.IsValid())
	{
		return FString::Printf(TEXT("ERROR: Binding '%s' not found"), *BindingName);
	}

	const FMovieSceneBinding* Binding = MovieScene->FindBinding(Guid);
	if (!Binding)
	{
		return FString::Printf(TEXT("ERROR: No tracks for binding '%s'"), *BindingName);
	}

	int32 MatchCount = 0;
	for (UMovieSceneTrack* Track : Binding->GetTracks())
	{
		if (Track && Track->IsA(TrackClass))
		{
			if (MatchCount == TrackIndex)
			{
				if (MovieScene->RemoveTrack(*Track))
				{
					return FString::Printf(TEXT("Removed %s track from '%s'"), *TrackType, *BindingName);
				}
				return FString::Printf(TEXT("ERROR: Failed to remove %s track from '%s'"), *TrackType, *BindingName);
			}
			MatchCount++;
		}
	}

	return FString::Printf(TEXT("ERROR: Binding '%s' has no %s track (index %d)"), *BindingName, *TrackType, TrackIndex);
}

// ========== Helpers ==========

FGuid FEditSequencerTool::FindBindingByName(UMovieScene* MovieScene, const FString& Name)
{
	if (!MovieScene)
	{
		return FGuid();
	}

	// Check possessables
	for (int32 i = 0; i < MovieScene->GetPossessableCount(); i++)
	{
		const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
		if (Possessable.GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Possessable.GetGuid();
		}
	}

	// Check spawnables
	for (int32 i = 0; i < MovieScene->GetSpawnableCount(); i++)
	{
		const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
		if (Spawnable.GetName().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Spawnable.GetGuid();
		}
	}

	return FGuid();
}

AActor* FEditSequencerTool::FindActorByName(const FString& ActorName)
{
	if (!GEditor)
	{
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return nullptr;
	}

	// Exact label match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetActorLabel().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	// Exact name match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && Actor->GetName().Equals(ActorName, ESearchCase::IgnoreCase))
		{
			return Actor;
		}
	}

	// Partial match
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor && (Actor->GetActorLabel().Contains(ActorName) || Actor->GetName().Contains(ActorName)))
		{
			return Actor;
		}
	}

	return nullptr;
}

FFrameNumber FEditSequencerTool::SecondsToFrame(UMovieScene* MovieScene, double Seconds) const
{
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	return FFrameNumber(static_cast<int32>(FMath::RoundToDouble(Seconds * TickResolution.AsDecimal())));
}

double FEditSequencerTool::FrameToSeconds(UMovieScene* MovieScene, FFrameNumber Frame) const
{
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	double TickDecimal = TickResolution.AsDecimal();
	if (FMath::IsNearlyZero(TickDecimal))
	{
		return 0.0;
	}
	return Frame.Value / TickDecimal;
}

int32 FEditSequencerTool::CountChannels(UMovieSceneSection* Section) const
{
	if (!Section)
	{
		return 0;
	}

	FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
	int32 Count = 0;

	Count += Proxy.GetChannels<FMovieSceneDoubleChannel>().Num();
	Count += Proxy.GetChannels<FMovieSceneFloatChannel>().Num();
	Count += Proxy.GetChannels<FMovieSceneBoolChannel>().Num();
	Count += Proxy.GetChannels<FMovieSceneIntegerChannel>().Num();
	Count += Proxy.GetChannels<FMovieSceneByteChannel>().Num();

	return Count;
}

// ========== Format Results ==========

FString FEditSequencerTool::FormatResults(
	const FString& SequenceName,
	const TArray<FAddedBinding>& Bindings,
	const TArray<FAddedTrack>& Tracks,
	const TArray<FString>& Keyframes,
	const TArray<FString>& TransformKeys,
	const TArray<FString>& RemovedBindings,
	const TArray<FString>& RemovedTracks,
	const TArray<FString>& Errors) const
{
	FString Output;

	Output += FString::Printf(TEXT("# EDIT SEQUENCER: %s\n\n"), *SequenceName);

	if (RemovedBindings.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Bindings Removed (%d)\n"), RemovedBindings.Num());
		for (const FString& R : RemovedBindings)
		{
			Output += FString::Printf(TEXT("- %s\n"), *R);
		}
		Output += TEXT("\n");
	}

	if (RemovedTracks.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Tracks Removed (%d)\n"), RemovedTracks.Num());
		for (const FString& R : RemovedTracks)
		{
			Output += FString::Printf(TEXT("- %s\n"), *R);
		}
		Output += TEXT("\n");
	}

	if (Bindings.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Bindings Added (%d)\n"), Bindings.Num());
		for (const FAddedBinding& B : Bindings)
		{
			Output += FString::Printf(TEXT("+ %s -> %s\n"), *B.Name, *B.ActorPath);
			Output += FString::Printf(TEXT("  GUID: %s\n"), *B.Guid.ToString());
		}
		Output += TEXT("\n");
	}

	if (Tracks.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Tracks Added (%d)\n"), Tracks.Num());
		for (const FAddedTrack& T : Tracks)
		{
			Output += FString::Printf(TEXT("+ %s on %s (%.2fs - %.2fs)\n"),
				*T.TrackType, *T.Binding, T.StartTime, T.EndTime);
		}
		Output += TEXT("\n");
	}

	if (TransformKeys.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Transform Keys (%d)\n"), TransformKeys.Num());
		for (const FString& K : TransformKeys)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *K);
		}
		Output += TEXT("\n");
	}

	if (Keyframes.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Keyframes Added (%d)\n"), Keyframes.Num());
		for (const FString& K : Keyframes)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *K);
		}
		Output += TEXT("\n");
	}

	if (Errors.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Errors (%d)\n"), Errors.Num());
		for (const FString& E : Errors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *E);
		}
	}

	return Output;
}
