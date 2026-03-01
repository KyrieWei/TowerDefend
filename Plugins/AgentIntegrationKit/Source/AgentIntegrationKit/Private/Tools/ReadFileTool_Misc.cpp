// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "AgentIntegrationKitModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IMaterialEditor.h"
#include "LevelSequence.h"
#include "MovieScene.h"
#include "MovieSceneBinding.h"
#include "MovieSceneTrack.h"
#include "MovieSceneSection.h"
#include "MovieSceneSpawnable.h"
#include "MovieScenePossessable.h"
#include "Tracks/MovieScenePropertyTrack.h"
#include "Tracks/MovieSceneAudioTrack.h"
#include "Tracks/MovieSceneSkeletalAnimationTrack.h"
#include "Sections/MovieSceneAudioSection.h"
#include "Sections/MovieSceneSkeletalAnimationSection.h"
#include "Channels/MovieSceneDoubleChannel.h"
#include "Channels/MovieSceneFloatChannel.h"
#include "Channels/MovieSceneBoolChannel.h"
#include "Channels/MovieSceneIntegerChannel.h"
#include "Channels/MovieSceneByteChannel.h"
#include "Channels/MovieSceneChannelProxy.h"
#include "Sound/SoundBase.h"
#include "Animation/AnimSequenceBase.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Materials/MaterialInstance.h"
#include "StaticParameterSet.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Engine/Font.h"
#include "VT/RuntimeVirtualTexture.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchyDefines.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif

FToolResult FReadFileTool::ReadMaterial(UObject* Asset, const TArray<FString>& Include,
	const FString& GraphName, int32 Offset, int32 Limit)
{
	UMaterial* Material = Cast<UMaterial>(Asset);
	UMaterialFunction* MaterialFunction = Cast<UMaterialFunction>(Asset);
	if (!Material && !MaterialFunction)
	{
		return FToolResult::Fail(TEXT("Asset is not a Material or MaterialFunction"));
	}

	UMaterial* WorkingMaterial = Material;
	UMaterialFunction* WorkingFunction = MaterialFunction;

	if (Material && GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
			if (EditorInstance && EditorInstance->GetEditorName() == TEXT("MaterialEditor"))
			{
				IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
				if (MaterialEditor)
				{
					UMaterialInterface* PreviewMaterial = MaterialEditor->GetMaterialInterface();
					if (UMaterial* PreviewMat = Cast<UMaterial>(PreviewMaterial))
					{
						WorkingMaterial = PreviewMat;
						UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack ReadFile: Using preview material from Material Editor"));
					}
				}
			}
		}
	}

	UMaterialGraph* MaterialGraph = nullptr;

	if (WorkingMaterial)
	{
		if (!WorkingMaterial->MaterialGraph)
		{
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(WorkingMaterial, NAME_None,
				UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass());
			UMaterialGraph* MatGraph = Cast<UMaterialGraph>(NewGraph);
			if (!MatGraph)
			{
				return FToolResult::Fail(TEXT("Failed to create material graph for reading"));
			}
			WorkingMaterial->MaterialGraph = MatGraph;
			WorkingMaterial->MaterialGraph->Material = WorkingMaterial;
			WorkingMaterial->MaterialGraph->RebuildGraph();
		}
		MaterialGraph = WorkingMaterial->MaterialGraph;
	}
	else if (WorkingFunction)
	{
		if (!WorkingFunction->MaterialGraph)
		{
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(WorkingFunction, NAME_None,
			UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass());
			UMaterialGraph* MatGraph = Cast<UMaterialGraph>(NewGraph);
			if (!MatGraph)
			{
				return FToolResult::Fail(TEXT("Failed to create material function graph for reading"));
			}
			WorkingFunction->MaterialGraph = MatGraph;
			WorkingFunction->MaterialGraph->MaterialFunction = WorkingFunction;
			WorkingFunction->MaterialGraph->RebuildGraph();
		}
		MaterialGraph = WorkingFunction->MaterialGraph;
	}

	FString Summary;
	if (Include.Contains(TEXT("summary")))
	{
		if (WorkingMaterial)
		{
			Summary = FString::Printf(TEXT("# MATERIAL %s\n"), *WorkingMaterial->GetName());
			Summary += FString::Printf(TEXT("BlendMode: %d\n"), (int32)WorkingMaterial->BlendMode);
			Summary += FString::Printf(TEXT("ShadingModel: %d\n"), (int32)WorkingMaterial->GetShadingModels().GetFirstShadingModel());
			Summary += FString::Printf(TEXT("TwoSided: %s\n"), WorkingMaterial->IsTwoSided() ? TEXT("true") : TEXT("false"));
			Summary += FString::Printf(TEXT("Expressions: %d\n"), WorkingMaterial->GetExpressions().Num());
		}
		else if (WorkingFunction)
		{
			Summary = FString::Printf(TEXT("# MATERIAL_FUNCTION %s\n"), *WorkingFunction->GetName());
			Summary += FString::Printf(TEXT("Description: %s\n"), *WorkingFunction->Description);
			Summary += FString::Printf(TEXT("ExposeToLibrary: %s\n"), WorkingFunction->bExposeToLibrary ? TEXT("true") : TEXT("false"));
			Summary += FString::Printf(TEXT("Expressions: %d\n"), WorkingFunction->GetExpressions().Num());
		}
	}

	if (!GraphName.IsEmpty() && MaterialGraph)
	{
		if (MaterialGraph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			const FString GraphType = WorkingFunction ? TEXT("material_function") : TEXT("material");
			FString Output = GetGraphWithNodes(MaterialGraph, GraphType, TEXT(""), Offset, Limit);
			Output += TEXT("\n") + GetGraphConnections(MaterialGraph);
			return FToolResult::Ok(Output);
		}
		return FToolResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	FString Output = Summary;
	if ((Include.Contains(TEXT("graphs")) || Include.Contains(TEXT("graph"))) && MaterialGraph)
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		const FString GraphType = WorkingFunction ? TEXT("material_function") : TEXT("material");
		Output += GetGraphWithNodes(MaterialGraph, GraphType, TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(MaterialGraph);
	}

	if (Output.IsEmpty())
	{
		if (WorkingMaterial)
		{
			Output = FString::Printf(TEXT("# MATERIAL %s (no data)\n"), *WorkingMaterial->GetName());
		}
		else if (WorkingFunction)
		{
			Output = FString::Printf(TEXT("# MATERIAL_FUNCTION %s (no data)\n"), *WorkingFunction->GetName());
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadLevelSequence(ULevelSequence* LevelSequence)
{
	FString SeqSummary;
	UMovieScene* MovieScene = LevelSequence->GetMovieScene();
	if (!MovieScene)
	{
		return FToolResult::Fail(TEXT("Level Sequence has no MovieScene"));
	}

	FFrameRate DisplayRate = MovieScene->GetDisplayRate();
	TRange<FFrameNumber> PlaybackRange = MovieScene->GetPlaybackRange();
	FFrameRate TickResolution = MovieScene->GetTickResolution();
	double TickDecimal = TickResolution.AsDecimal();
	if (FMath::IsNearlyZero(TickDecimal))
	{
		TickDecimal = 24000.0;
	}
	double StartSeconds = PlaybackRange.GetLowerBoundValue().Value / TickDecimal;
	double EndSeconds = PlaybackRange.GetUpperBoundValue().Value / TickDecimal;

	SeqSummary += FString::Printf(TEXT("# LEVEL_SEQUENCE: %s\n"), *LevelSequence->GetName());
	SeqSummary += FString::Printf(TEXT("Frame Rate: %d FPS\n"), DisplayRate.Numerator);
	SeqSummary += FString::Printf(TEXT("Playback Range: %.2fs - %.2fs (%.2f seconds)\n\n"), StartSeconds, EndSeconds, EndSeconds - StartSeconds);

	// Helper: count channels in a section and get summary
	auto GetChannelSummary = [](UMovieSceneSection* Section) -> FString
	{
		if (!Section) return FString();

		FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
		int32 DoubleCount = Proxy.GetChannels<FMovieSceneDoubleChannel>().Num();
		int32 FloatCount = Proxy.GetChannels<FMovieSceneFloatChannel>().Num();
		int32 BoolCount = Proxy.GetChannels<FMovieSceneBoolChannel>().Num();
		int32 IntCount = Proxy.GetChannels<FMovieSceneIntegerChannel>().Num();
		int32 ByteCount = Proxy.GetChannels<FMovieSceneByteChannel>().Num();

		TArray<FString> Parts;
		if (DoubleCount > 0) Parts.Add(FString::Printf(TEXT("%d double"), DoubleCount));
		if (FloatCount > 0) Parts.Add(FString::Printf(TEXT("%d float"), FloatCount));
		if (BoolCount > 0) Parts.Add(FString::Printf(TEXT("%d bool"), BoolCount));
		if (IntCount > 0) Parts.Add(FString::Printf(TEXT("%d int"), IntCount));
		if (ByteCount > 0) Parts.Add(FString::Printf(TEXT("%d byte"), ByteCount));

		if (Parts.Num() == 0) return TEXT("no channels");
		return FString::Join(Parts, TEXT(", "));
	};

	// Helper: get key count summary for a section
	auto GetKeySummary = [](UMovieSceneSection* Section) -> FString
	{
		if (!Section) return FString();

		FMovieSceneChannelProxy& Proxy = Section->GetChannelProxy();
		int32 TotalKeys = 0;

		for (FMovieSceneDoubleChannel* Ch : Proxy.GetChannels<FMovieSceneDoubleChannel>())
		{
			if (Ch) TotalKeys += Ch->GetNumKeys();
		}
		for (FMovieSceneFloatChannel* Ch : Proxy.GetChannels<FMovieSceneFloatChannel>())
		{
			if (Ch) TotalKeys += Ch->GetNumKeys();
		}
		for (FMovieSceneBoolChannel* Ch : Proxy.GetChannels<FMovieSceneBoolChannel>())
		{
			if (Ch) TotalKeys += Ch->GetNumKeys();
		}
		for (FMovieSceneIntegerChannel* Ch : Proxy.GetChannels<FMovieSceneIntegerChannel>())
		{
			if (Ch) TotalKeys += Ch->GetNumKeys();
		}
		for (FMovieSceneByteChannel* Ch : Proxy.GetChannels<FMovieSceneByteChannel>())
		{
			if (Ch) TotalKeys += Ch->GetNumKeys();
		}

		return FString::Printf(TEXT("%d keys"), TotalKeys);
	};

	// Helper: get track details (property name, audio asset, animation asset)
	auto GetTrackDetails = [](UMovieSceneTrack* Track) -> FString
	{
		if (!Track) return FString();

		FString Details;

		// Property track - show property name
		if (UMovieScenePropertyTrack* PropTrack = Cast<UMovieScenePropertyTrack>(Track))
		{
			Details += FString::Printf(TEXT(" [property: %s]"), *PropTrack->GetPropertyName().ToString());
		}

		// Audio track - show sound assets
		if (UMovieSceneAudioTrack* AudioTrack = Cast<UMovieSceneAudioTrack>(Track))
		{
			for (UMovieSceneSection* Section : AudioTrack->GetAllSections())
			{
				if (UMovieSceneAudioSection* AudioSection = Cast<UMovieSceneAudioSection>(Section))
				{
					USoundBase* Sound = AudioSection->GetSound();
					if (Sound)
					{
						Details += FString::Printf(TEXT(" [sound: %s]"), *Sound->GetPathName());
					}
				}
			}
		}

		// Skeletal animation track - show animation assets
		if (UMovieSceneSkeletalAnimationTrack* AnimTrack = Cast<UMovieSceneSkeletalAnimationTrack>(Track))
		{
			for (UMovieSceneSection* Section : AnimTrack->GetAllSections())
			{
				if (UMovieSceneSkeletalAnimationSection* AnimSection = Cast<UMovieSceneSkeletalAnimationSection>(Section))
				{
					if (AnimSection->Params.Animation)
					{
						Details += FString::Printf(TEXT(" [anim: %s]"), *AnimSection->Params.Animation->GetPathName());
					}
				}
			}
		}

		return Details;
	};

	// Helper: format track type name
	auto FormatTrackType = [](UMovieSceneTrack* Track) -> FString
	{
		FString TrackType = Track->GetClass()->GetName();
		TrackType.RemoveFromStart(TEXT("MovieScene"));
		TrackType.RemoveFromEnd(TEXT("Track"));
		return TrackType;
	};

	// Possessables
	int32 PossessableCount = MovieScene->GetPossessableCount();
	if (PossessableCount > 0)
	{
		SeqSummary += FString::Printf(TEXT("## Possessable Bindings (%d)\n"), PossessableCount);
		for (int32 i = 0; i < PossessableCount; i++)
		{
			const FMovieScenePossessable& Possessable = MovieScene->GetPossessable(i);
			FString ClassName = Possessable.GetPossessedObjectClass() ? Possessable.GetPossessedObjectClass()->GetName() : TEXT("Unknown");
			SeqSummary += FString::Printf(TEXT("- **%s** (GUID: %s, Class: %s)\n"),
				*Possessable.GetName(), *Possessable.GetGuid().ToString(), *ClassName);

			const FMovieSceneBinding* Binding = MovieScene->FindBinding(Possessable.GetGuid());
			if (Binding)
			{
				for (UMovieSceneTrack* Track : Binding->GetTracks())
				{
					if (!Track) continue;

					FString TrackType = FormatTrackType(Track);
					FString Details = GetTrackDetails(Track);

					for (UMovieSceneSection* Section : Track->GetAllSections())
					{
						if (!Section) continue;

						TRange<FFrameNumber> Range = Section->GetRange();
						double SectionStart = Range.GetLowerBoundValue().Value / TickDecimal;
						double SectionEnd = Range.GetUpperBoundValue().Value / TickDecimal;
						FString ChannelInfo = GetChannelSummary(Section);
						FString KeyInfo = GetKeySummary(Section);

						SeqSummary += FString::Printf(TEXT("  + %s: %.2fs-%.2fs (%s, %s)%s\n"),
							*TrackType, SectionStart, SectionEnd, *ChannelInfo, *KeyInfo, *Details);
					}
				}
			}
		}
		SeqSummary += TEXT("\n");
	}

	// Spawnables
	int32 SpawnableCount = MovieScene->GetSpawnableCount();
	if (SpawnableCount > 0)
	{
		SeqSummary += FString::Printf(TEXT("## Spawnable Bindings (%d)\n"), SpawnableCount);
		for (int32 i = 0; i < SpawnableCount; i++)
		{
			const FMovieSceneSpawnable& Spawnable = MovieScene->GetSpawnable(i);
			FString TemplateClass = Spawnable.GetObjectTemplate() ? Spawnable.GetObjectTemplate()->GetClass()->GetName() : TEXT("Unknown");
			SeqSummary += FString::Printf(TEXT("- **%s** (GUID: %s, Template: %s)\n"),
				*Spawnable.GetName(), *Spawnable.GetGuid().ToString(), *TemplateClass);

			const FMovieSceneBinding* Binding = MovieScene->FindBinding(Spawnable.GetGuid());
			if (Binding)
			{
				for (UMovieSceneTrack* Track : Binding->GetTracks())
				{
					if (!Track) continue;

					FString TrackType = FormatTrackType(Track);
					FString Details = GetTrackDetails(Track);

					for (UMovieSceneSection* Section : Track->GetAllSections())
					{
						if (!Section) continue;

						TRange<FFrameNumber> Range = Section->GetRange();
						double SectionStart = Range.GetLowerBoundValue().Value / TickDecimal;
						double SectionEnd = Range.GetUpperBoundValue().Value / TickDecimal;
						FString ChannelInfo = GetChannelSummary(Section);
						FString KeyInfo = GetKeySummary(Section);

						SeqSummary += FString::Printf(TEXT("  + %s: %.2fs-%.2fs (%s, %s)%s\n"),
							*TrackType, SectionStart, SectionEnd, *ChannelInfo, *KeyInfo, *Details);
					}
				}
			}
		}
		SeqSummary += TEXT("\n");
	}

	// Master tracks
	const TArray<UMovieSceneTrack*>& MasterTracks = MovieScene->GetTracks();
	if (MasterTracks.Num() > 0)
	{
		SeqSummary += FString::Printf(TEXT("## Master Tracks (%d)\n"), MasterTracks.Num());
		for (UMovieSceneTrack* Track : MasterTracks)
		{
			if (!Track) continue;

			FString TrackType = FormatTrackType(Track);
			FString Details = GetTrackDetails(Track);

			for (UMovieSceneSection* Section : Track->GetAllSections())
			{
				if (!Section) continue;

				TRange<FFrameNumber> Range = Section->GetRange();
				double SectionStart = Range.GetLowerBoundValue().Value / TickDecimal;
				double SectionEnd = Range.GetUpperBoundValue().Value / TickDecimal;

				SeqSummary += FString::Printf(TEXT("- %s: %.2fs-%.2fs%s\n"),
					*TrackType, SectionStart, SectionEnd, *Details);
			}

			if (Track->GetAllSections().Num() == 0)
			{
				SeqSummary += FString::Printf(TEXT("- %s (no sections)%s\n"), *TrackType, *Details);
			}
		}
	}

	SeqSummary += TEXT("\nTIP: Use edit_sequencer with action='list_bindings' for detailed binding info, ");
	SeqSummary += TEXT("action='list_channels' to see keyframeable channels.\n");

	return FToolResult::Ok(SeqSummary);
}

FToolResult FReadFileTool::ReadSkeletalMesh(USkeletalMesh* SkelMesh)
{
	FString MeshSummary;
	MeshSummary += FString::Printf(TEXT("Skeletal Mesh: %s\n"), *SkelMesh->GetName());
	MeshSummary += TEXT("================\n\n");

	const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
	int32 NumBones = RefSkeleton.GetNum();

	MeshSummary += FString::Printf(TEXT("Bone Count: %d\n"), NumBones);
	MeshSummary += FString::Printf(TEXT("LOD Count: %d\n"), SkelMesh->GetLODNum());

	USkeleton* Skeleton = SkelMesh->GetSkeleton();
	if (Skeleton)
	{
		MeshSummary += FString::Printf(TEXT("Skeleton Asset: %s\n"), *Skeleton->GetPathName());
	}
	MeshSummary += TEXT("\n");

	MeshSummary += TEXT("## Bone Hierarchy\n\n");

	TFunction<void(int32, int32)> PrintBoneTree = [&](int32 BoneIndex, int32 Depth)
	{
		if (BoneIndex < 0 || BoneIndex >= NumBones) return;

		const FMeshBoneInfo& BoneInfo = RefSkeleton.GetRefBoneInfo()[BoneIndex];
		FString Indent = FString::ChrN(Depth * 2, ' ');

		MeshSummary += FString::Printf(TEXT("%s[%d] %s\n"),
			*Indent, BoneIndex, *BoneInfo.Name.ToString());

		for (int32 i = 0; i < NumBones; i++)
		{
			if (RefSkeleton.GetRefBoneInfo()[i].ParentIndex == BoneIndex)
			{
				PrintBoneTree(i, Depth + 1);
			}
		}
	};

	for (int32 i = 0; i < NumBones; i++)
	{
		if (RefSkeleton.GetRefBoneInfo()[i].ParentIndex == INDEX_NONE)
		{
			PrintBoneTree(i, 0);
		}
	}
	MeshSummary += TEXT("\n");

	MeshSummary += TEXT("## Bone List\n\n");
	MeshSummary += TEXT("| Index | Bone Name | Parent Index | Parent Name |\n");
	MeshSummary += TEXT("|-------|-----------|--------------|-------------|\n");

	for (int32 i = 0; i < NumBones; i++)
	{
		const FMeshBoneInfo& BoneInfo = RefSkeleton.GetRefBoneInfo()[i];
		FString ParentName = TEXT("(root)");
		if (BoneInfo.ParentIndex >= 0 && BoneInfo.ParentIndex < NumBones)
		{
			ParentName = RefSkeleton.GetRefBoneInfo()[BoneInfo.ParentIndex].Name.ToString();
		}

		MeshSummary += FString::Printf(TEXT("| %d | %s | %d | %s |\n"),
			i, *BoneInfo.Name.ToString(), BoneInfo.ParentIndex, *ParentName);
	}

	return FToolResult::Ok(MeshSummary);
}

FToolResult FReadFileTool::ReadAnimSequence(UAnimSequence* AnimSeq)
{
	FString AnimSummary;
	AnimSummary += FString::Printf(TEXT("Animation Sequence: %s\n"), *AnimSeq->GetName());
	AnimSummary += TEXT("================\n\n");

	float PlayLength = AnimSeq->GetPlayLength();
	int32 NumKeys = AnimSeq->GetNumberOfSampledKeys();
	FFrameRate FrameRate = AnimSeq->GetSamplingFrameRate();

	AnimSummary += TEXT("## Basic Info\n\n");
	AnimSummary += FString::Printf(TEXT("Duration: %.3f seconds\n"), PlayLength);
	AnimSummary += FString::Printf(TEXT("Frame Rate: %.2f FPS\n"), FrameRate.AsDecimal());
	AnimSummary += FString::Printf(TEXT("Sampled Keys: %d\n"), NumKeys);

	USkeleton* Skeleton = AnimSeq->GetSkeleton();
	if (Skeleton)
	{
		AnimSummary += FString::Printf(TEXT("Skeleton: %s\n"), *Skeleton->GetPathName());
	}
	AnimSummary += TEXT("\n");

	AnimSummary += TEXT("## Additive Settings\n\n");
	EAdditiveAnimationType AdditiveType = AnimSeq->AdditiveAnimType;
	FString AdditiveTypeStr;
	switch (AdditiveType)
	{
		case AAT_None: AdditiveTypeStr = TEXT("None"); break;
		case AAT_LocalSpaceBase: AdditiveTypeStr = TEXT("Local Space"); break;
		case AAT_RotationOffsetMeshSpace: AdditiveTypeStr = TEXT("Mesh Space"); break;
		default: AdditiveTypeStr = TEXT("Unknown"); break;
	}
	AnimSummary += FString::Printf(TEXT("Additive Type: %s\n"), *AdditiveTypeStr);

	if (AdditiveType != AAT_None && AnimSeq->RefPoseSeq)
	{
		AnimSummary += FString::Printf(TEXT("Reference Pose: %s\n"), *AnimSeq->RefPoseSeq->GetPathName());
	}
	AnimSummary += TEXT("\n");

	AnimSummary += TEXT("## Root Motion\n\n");
	AnimSummary += FString::Printf(TEXT("Root Motion Enabled: %s\n"), AnimSeq->bEnableRootMotion ? TEXT("Yes") : TEXT("No"));
	if (AnimSeq->bEnableRootMotion)
	{
		FString RootLockStr;
		switch (AnimSeq->RootMotionRootLock)
		{
			case ERootMotionRootLock::RefPose: RootLockStr = TEXT("Reference Pose"); break;
			case ERootMotionRootLock::AnimFirstFrame: RootLockStr = TEXT("Anim First Frame"); break;
			case ERootMotionRootLock::Zero: RootLockStr = TEXT("Zero"); break;
			default: RootLockStr = TEXT("Unknown"); break;
		}
		AnimSummary += FString::Printf(TEXT("Root Lock: %s\n"), *RootLockStr);
	}
	AnimSummary += TEXT("\n");

	const TArray<FAnimNotifyEvent>& Notifies = AnimSeq->Notifies;
	if (Notifies.Num() > 0)
	{
		AnimSummary += FString::Printf(TEXT("## Notifies (%d)\n\n"), Notifies.Num());
		for (const FAnimNotifyEvent& Notify : Notifies)
		{
			FString NotifyName;
			if (Notify.Notify)
			{
				NotifyName = Notify.Notify->GetClass()->GetName();
				NotifyName.RemoveFromStart(TEXT("AnimNotify_"));
			}
			else if (Notify.NotifyStateClass)
			{
				NotifyName = Notify.NotifyStateClass->GetClass()->GetName();
				NotifyName.RemoveFromStart(TEXT("AnimNotifyState_"));
				NotifyName += FString::Printf(TEXT(" (%.2fs)"), Notify.GetDuration());
			}
			else
			{
				NotifyName = Notify.NotifyName.ToString();
			}

			AnimSummary += FString::Printf(TEXT("- [%.2fs] %s\n"), Notify.GetTriggerTime(), *NotifyName);
		}
		AnimSummary += TEXT("\n");
	}

	const FRawCurveTracks& RawCurves = AnimSeq->GetCurveData();
	if (RawCurves.FloatCurves.Num() > 0)
	{
		AnimSummary += FString::Printf(TEXT("## Curves (%d)\n\n"), RawCurves.FloatCurves.Num());
		for (const FFloatCurve& Curve : RawCurves.FloatCurves)
		{
			AnimSummary += FString::Printf(TEXT("- %s\n"), *Curve.GetName().ToString());
		}
		AnimSummary += TEXT("\n");
	}

#if WITH_EDITORONLY_DATA
	if (Skeleton)
	{
		IAnimationDataModel* DataModel = AnimSeq->GetDataModel();
		if (DataModel)
		{
			TArray<FName> BoneTrackNames;
			DataModel->GetBoneTrackNames(BoneTrackNames);

			if (BoneTrackNames.Num() > 0)
			{
				int32 NumModelKeys = DataModel->GetNumberOfKeys();
				AnimSummary += FString::Printf(TEXT("## Bone Tracks (%d bones, %d keys)\n\n"),
					BoneTrackNames.Num(), NumModelKeys);

				// Show keyframe data per bone (cap detail at 30 bones)
				constexpr int32 MaxDetailBones = 30;
				constexpr int32 MaxInlineKeys = 10;
				constexpr int32 HeadTailKeys = 3;

				int32 DetailCount = FMath::Min(BoneTrackNames.Num(), MaxDetailBones);
				for (int32 BoneIdx = 0; BoneIdx < DetailCount; BoneIdx++)
				{
					const FName& BoneName = BoneTrackNames[BoneIdx];
					TArray<FTransform> Transforms;
					DataModel->GetBoneTrackTransforms(BoneName, Transforms);

					AnimSummary += FString::Printf(TEXT("### %s (%d keys)\n"),
						*BoneName.ToString(), Transforms.Num());

					// Check if any scale differs from (1,1,1)
					bool bHasNonUnitScale = false;
					for (const FTransform& T : Transforms)
					{
						if (!T.GetScale3D().Equals(FVector::OneVector, 0.001))
						{
							bHasNonUnitScale = true;
							break;
						}
					}

					// Lambda to format a single keyframe line
					auto FormatKey = [&](int32 KeyIdx) -> FString
					{
						const FTransform& T = Transforms[KeyIdx];
						FVector Pos = T.GetLocation();
						FRotator Rot = T.GetRotation().Rotator();
						FString Line = FString::Printf(
							TEXT("[%d] pos=(%.2f, %.2f, %.2f) rot=(%.2f, %.2f, %.2f)"),
							KeyIdx, Pos.X, Pos.Y, Pos.Z, Rot.Pitch, Rot.Yaw, Rot.Roll);
						if (bHasNonUnitScale)
						{
							FVector Scale = T.GetScale3D();
							Line += FString::Printf(TEXT(" scale=(%.2f, %.2f, %.2f)"), Scale.X, Scale.Y, Scale.Z);
						}
						return Line + TEXT("\n");
					};

					if (Transforms.Num() <= MaxInlineKeys)
					{
						// Show all keys
						for (int32 K = 0; K < Transforms.Num(); K++)
						{
							AnimSummary += FormatKey(K);
						}
					}
					else
					{
						// Show first N, ..., last N
						for (int32 K = 0; K < HeadTailKeys; K++)
						{
							AnimSummary += FormatKey(K);
						}
						AnimSummary += FString::Printf(TEXT("... (%d frames omitted)\n"),
							Transforms.Num() - HeadTailKeys * 2);
						for (int32 K = Transforms.Num() - HeadTailKeys; K < Transforms.Num(); K++)
						{
							AnimSummary += FormatKey(K);
						}
					}
					AnimSummary += TEXT("\n");
				}

				// List remaining bones without keyframe detail
				if (BoneTrackNames.Num() > MaxDetailBones)
				{
					AnimSummary += FString::Printf(TEXT("... and %d more bone tracks:\n"),
						BoneTrackNames.Num() - MaxDetailBones);
					for (int32 BoneIdx = MaxDetailBones; BoneIdx < BoneTrackNames.Num(); BoneIdx++)
					{
						AnimSummary += FString::Printf(TEXT("- %s\n"), *BoneTrackNames[BoneIdx].ToString());
					}
				}
			}
		}
	}
#endif

	return FToolResult::Ok(AnimSummary);
}

FToolResult FReadFileTool::ReadMaterialInstance(UMaterialInstance* MatInstance)
{
	FString Output = FString::Printf(TEXT("# MATERIAL_INSTANCE: %s\n"), *MatInstance->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *MatInstance->GetPathName());

	if (MatInstance->Parent)
	{
		Output += FString::Printf(TEXT("Parent: %s\n"), *MatInstance->Parent->GetPathName());

		FString ParentChain = MatInstance->GetName();
		UMaterialInterface* Current = MatInstance->Parent;
		while (Current)
		{
			ParentChain += TEXT(" -> ") + Current->GetName();
			if (UMaterialInstance* ParentMI = Cast<UMaterialInstance>(Current))
			{
				Current = ParentMI->Parent;
			}
			else
			{
				break;
			}
		}
		Output += FString::Printf(TEXT("Parent Chain: %s\n"), *ParentChain);
	}

	UMaterial* BaseMaterial = MatInstance->GetMaterial();
	if (BaseMaterial)
	{
		Output += FString::Printf(TEXT("Base Material: %s\n"), *BaseMaterial->GetPathName());
	}

	const FMaterialInstanceBasePropertyOverrides& BaseProps = MatInstance->BasePropertyOverrides;
	TArray<FString> Overrides;
	if (BaseProps.bOverride_BlendMode)
	{
		FString BlendModeStr;
		switch (BaseProps.BlendMode)
		{
			case BLEND_Opaque: BlendModeStr = TEXT("Opaque"); break;
			case BLEND_Masked: BlendModeStr = TEXT("Masked"); break;
			case BLEND_Translucent: BlendModeStr = TEXT("Translucent"); break;
			case BLEND_Additive: BlendModeStr = TEXT("Additive"); break;
			case BLEND_Modulate: BlendModeStr = TEXT("Modulate"); break;
			default: BlendModeStr = FString::Printf(TEXT("%d"), (int32)BaseProps.BlendMode); break;
		}
		Overrides.Add(FString::Printf(TEXT("BlendMode: %s"), *BlendModeStr));
	}
	if (BaseProps.bOverride_ShadingModel)
	{
		Overrides.Add(FString::Printf(TEXT("ShadingModel: %d"), (int32)BaseProps.ShadingModel));
	}
	if (BaseProps.bOverride_TwoSided)
	{
		Overrides.Add(FString::Printf(TEXT("TwoSided: %s"), BaseProps.TwoSided ? TEXT("true") : TEXT("false")));
	}
	if (BaseProps.bOverride_OpacityMaskClipValue)
	{
		Overrides.Add(FString::Printf(TEXT("OpacityMaskClipValue: %.3f"), BaseProps.OpacityMaskClipValue));
	}
	if (BaseProps.bOverride_DitheredLODTransition)
	{
		Overrides.Add(FString::Printf(TEXT("DitheredLODTransition: %s"), BaseProps.DitheredLODTransition ? TEXT("true") : TEXT("false")));
	}
	if (Overrides.Num() > 0)
	{
		Output += TEXT("\n## Base Property Overrides\n");
		for (const FString& Override : Overrides)
		{
			Output += Override + TEXT("\n");
		}
	}

	if (MatInstance->ScalarParameterValues.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Scalar Parameters (%d)\n"), MatInstance->ScalarParameterValues.Num());
		for (const FScalarParameterValue& Param : MatInstance->ScalarParameterValues)
		{
			Output += FString::Printf(TEXT("%s\t%.4f\n"), *Param.ParameterInfo.Name.ToString(), Param.ParameterValue);
		}
	}

	if (MatInstance->VectorParameterValues.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Vector Parameters (%d)\n"), MatInstance->VectorParameterValues.Num());
		for (const FVectorParameterValue& Param : MatInstance->VectorParameterValues)
		{
			const FLinearColor& C = Param.ParameterValue;
			Output += FString::Printf(TEXT("%s\t(R=%.3f, G=%.3f, B=%.3f, A=%.3f)\n"),
				*Param.ParameterInfo.Name.ToString(), C.R, C.G, C.B, C.A);
		}
	}

	if (MatInstance->TextureParameterValues.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Texture Parameters (%d)\n"), MatInstance->TextureParameterValues.Num());
		for (const FTextureParameterValue& Param : MatInstance->TextureParameterValues)
		{
			FString TexPath = Param.ParameterValue ? Param.ParameterValue->GetPathName() : TEXT("None");
			Output += FString::Printf(TEXT("%s\t%s\n"), *Param.ParameterInfo.Name.ToString(), *TexPath);
		}
	}

	if (MatInstance->FontParameterValues.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Font Parameters (%d)\n"), MatInstance->FontParameterValues.Num());
		for (const FFontParameterValue& Param : MatInstance->FontParameterValues)
		{
			FString FontPath = Param.FontValue ? Param.FontValue->GetPathName() : TEXT("None");
			Output += FString::Printf(TEXT("%s\t%s (Page %d)\n"), *Param.ParameterInfo.Name.ToString(), *FontPath, Param.FontPage);
		}
	}

	if (MatInstance->RuntimeVirtualTextureParameterValues.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Runtime Virtual Texture Parameters (%d)\n"), MatInstance->RuntimeVirtualTextureParameterValues.Num());
		for (const FRuntimeVirtualTextureParameterValue& Param : MatInstance->RuntimeVirtualTextureParameterValues)
		{
			FString RVTPath = Param.ParameterValue ? Param.ParameterValue->GetPathName() : TEXT("None");
			Output += FString::Printf(TEXT("%s\t%s\n"), *Param.ParameterInfo.Name.ToString(), *RVTPath);
		}
	}

	FStaticParameterSet StaticParams = MatInstance->GetStaticParameters();
	if (StaticParams.StaticSwitchParameters.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Static Switch Parameters (%d)\n"), StaticParams.StaticSwitchParameters.Num());
		for (const FStaticSwitchParameter& Param : StaticParams.StaticSwitchParameters)
		{
			if (Param.bOverride)
			{
				Output += FString::Printf(TEXT("%s\t%s\n"), *Param.ParameterInfo.Name.ToString(), Param.Value ? TEXT("true") : TEXT("false"));
			}
		}
	}

#if WITH_EDITORONLY_DATA
	if (StaticParams.EditorOnly.StaticComponentMaskParameters.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Static Component Mask Parameters (%d)\n"), StaticParams.EditorOnly.StaticComponentMaskParameters.Num());
		for (const FStaticComponentMaskParameter& Param : StaticParams.EditorOnly.StaticComponentMaskParameters)
		{
			if (Param.bOverride)
			{
				Output += FString::Printf(TEXT("%s\tR=%s G=%s B=%s A=%s\n"),
					*Param.ParameterInfo.Name.ToString(),
					Param.R ? TEXT("1") : TEXT("0"),
					Param.G ? TEXT("1") : TEXT("0"),
					Param.B ? TEXT("1") : TEXT("0"),
					Param.A ? TEXT("1") : TEXT("0"));
			}
		}
	}
#endif

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadStaticMesh(UStaticMesh* StaticMeshAsset)
{
	FString Output = FString::Printf(TEXT("# STATIC_MESH: %s\n"), *StaticMeshAsset->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *StaticMeshAsset->GetPathName());

	FBoxSphereBounds Bounds = StaticMeshAsset->GetBounds();
	Output += FString::Printf(TEXT("Bounds: Center=(%.1f, %.1f, %.1f) Extent=(%.1f, %.1f, %.1f) Radius=%.1f\n"),
		Bounds.Origin.X, Bounds.Origin.Y, Bounds.Origin.Z,
		Bounds.BoxExtent.X, Bounds.BoxExtent.Y, Bounds.BoxExtent.Z,
		Bounds.SphereRadius);

	int32 NumLODs = StaticMeshAsset->GetNumLODs();
	Output += FString::Printf(TEXT("LODs: %d\n"), NumLODs);
	Output += FString::Printf(TEXT("Lightmap Resolution: %d\n"), StaticMeshAsset->GetLightMapResolution());
	Output += FString::Printf(TEXT("Lightmap UV Channel: %d\n"), StaticMeshAsset->GetLightMapCoordinateIndex());
	Output += FString::Printf(TEXT("Nanite Enabled: %s\n"), StaticMeshAsset->IsNaniteEnabled() ? TEXT("true") : TEXT("false"));

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	FName LODGroup = StaticMeshAsset->GetLODGroup();
	if (!LODGroup.IsNone())
	{
		Output += FString::Printf(TEXT("LOD Group: %s\n"), *LODGroup.ToString());
	}
#endif

	Output += FString::Printf(TEXT("Allow CPU Access: %s\n"), StaticMeshAsset->bAllowCPUAccess ? TEXT("true") : TEXT("false"));
	Output += FString::Printf(TEXT("Distance Field: %s\n"), StaticMeshAsset->bGenerateMeshDistanceField ? TEXT("true") : TEXT("false"));
	Output += FString::Printf(TEXT("Ray Tracing: %s\n"), StaticMeshAsset->bSupportRayTracing ? TEXT("true") : TEXT("false"));

	const TArray<FStaticMaterial>& Materials = StaticMeshAsset->GetStaticMaterials();
	if (Materials.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Material Slots (%d)\n"), Materials.Num());
		for (int32 i = 0; i < Materials.Num(); i++)
		{
			const FStaticMaterial& Mat = Materials[i];
			FString MatPath = Mat.MaterialInterface ? Mat.MaterialInterface->GetPathName() : TEXT("None");
			Output += FString::Printf(TEXT("[%d] %s\t%s\n"), i, *Mat.MaterialSlotName.ToString(), *MatPath);
		}
	}

	const FStaticMeshRenderData* RenderData = StaticMeshAsset->GetRenderData();
	if (RenderData && RenderData->LODResources.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## LOD Details\n"));
		for (int32 LODIdx = 0; LODIdx < RenderData->LODResources.Num(); LODIdx++)
		{
			const FStaticMeshLODResources& LODRes = RenderData->LODResources[LODIdx];
			Output += FString::Printf(TEXT("LOD %d\tVertices=%d\tTriangles=%d\tUV Channels=%d\tSections=%d\n"),
				LODIdx, LODRes.GetNumVertices(), LODRes.GetNumTriangles(),
				LODRes.GetNumTexCoords(), LODRes.Sections.Num());

			for (int32 SecIdx = 0; SecIdx < LODRes.Sections.Num(); SecIdx++)
			{
				const FStaticMeshSection& Section = LODRes.Sections[SecIdx];
				Output += FString::Printf(TEXT("  Section %d: Material=%d Triangles=%d Collision=%s Shadow=%s\n"),
					SecIdx, Section.MaterialIndex, Section.NumTriangles,
					Section.bEnableCollision ? TEXT("Yes") : TEXT("No"),
					Section.bCastShadow ? TEXT("Yes") : TEXT("No"));
			}
		}
	}

	UBodySetup* BodySetup = StaticMeshAsset->GetBodySetup();
	if (BodySetup)
	{
		Output += TEXT("\n## Collision\n");
		int32 SimpleCount = BodySetup->AggGeom.GetElementCount();
		Output += FString::Printf(TEXT("Simple Shapes: %d\n"), SimpleCount);

		if (BodySetup->AggGeom.BoxElems.Num() > 0)
			Output += FString::Printf(TEXT("  Boxes: %d\n"), BodySetup->AggGeom.BoxElems.Num());
		if (BodySetup->AggGeom.SphereElems.Num() > 0)
			Output += FString::Printf(TEXT("  Spheres: %d\n"), BodySetup->AggGeom.SphereElems.Num());
		if (BodySetup->AggGeom.SphylElems.Num() > 0)
			Output += FString::Printf(TEXT("  Capsules: %d\n"), BodySetup->AggGeom.SphylElems.Num());
		if (BodySetup->AggGeom.ConvexElems.Num() > 0)
			Output += FString::Printf(TEXT("  Convex Hulls: %d\n"), BodySetup->AggGeom.ConvexElems.Num());

		FString CollisionType;
		switch (BodySetup->CollisionTraceFlag)
		{
			case CTF_UseDefault: CollisionType = TEXT("Default"); break;
			case CTF_UseSimpleAndComplex: CollisionType = TEXT("SimpleAndComplex"); break;
			case CTF_UseSimpleAsComplex: CollisionType = TEXT("SimpleAsComplex"); break;
			case CTF_UseComplexAsSimple: CollisionType = TEXT("ComplexAsSimple"); break;
			default: CollisionType = TEXT("Unknown"); break;
		}
		Output += FString::Printf(TEXT("Collision Complexity: %s\n"), *CollisionType);
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadSkeleton(USkeleton* Skeleton)
{
	const FReferenceSkeleton& RefSkeleton = Skeleton->GetReferenceSkeleton();
	int32 NumBones = RefSkeleton.GetNum();
	int32 NumRawBones = RefSkeleton.GetRawBoneNum();

	FString Output = FString::Printf(TEXT("# SKELETON: %s\n"), *Skeleton->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *Skeleton->GetPathName());
	Output += FString::Printf(TEXT("Bones: %d"), NumBones);
	if (NumBones != NumRawBones)
	{
		Output += FString::Printf(TEXT(" (%d raw + %d virtual)"), NumRawBones, NumBones - NumRawBones);
	}
	Output += TEXT("\n");
	Output += FString::Printf(TEXT("Sockets: %d\n"), Skeleton->Sockets.Num());
	Output += FString::Printf(TEXT("Virtual Bones: %d\n"), Skeleton->GetVirtualBones().Num());

	Output += TEXT("\n## Bone Hierarchy\n");
	const TArray<FMeshBoneInfo>& BoneInfo = RefSkeleton.GetRefBoneInfo();

	TFunction<void(int32, int32)> PrintBoneTree = [&](int32 BoneIndex, int32 Depth)
	{
		if (BoneIndex < 0 || BoneIndex >= NumBones) return;
		FString Indent = FString::ChrN(Depth * 2, ' ');
		Output += FString::Printf(TEXT("%s[%d] %s\n"), *Indent, BoneIndex, *BoneInfo[BoneIndex].Name.ToString());
		for (int32 i = 0; i < NumBones; i++)
		{
			if (BoneInfo[i].ParentIndex == BoneIndex)
			{
				PrintBoneTree(i, Depth + 1);
			}
		}
	};

	for (int32 i = 0; i < NumBones; i++)
	{
		if (BoneInfo[i].ParentIndex == INDEX_NONE)
		{
			PrintBoneTree(i, 0);
		}
	}

	if (Skeleton->Sockets.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Sockets (%d)\n"), Skeleton->Sockets.Num());
		for (USkeletalMeshSocket* Socket : Skeleton->Sockets)
		{
			if (Socket)
			{
				Output += FString::Printf(TEXT("%s\tBone=%s\tLoc=(%.1f, %.1f, %.1f)\tRot=(%.1f, %.1f, %.1f)\n"),
					*Socket->SocketName.ToString(),
					*Socket->BoneName.ToString(),
					Socket->RelativeLocation.X, Socket->RelativeLocation.Y, Socket->RelativeLocation.Z,
					Socket->RelativeRotation.Pitch, Socket->RelativeRotation.Yaw, Socket->RelativeRotation.Roll);
			}
		}
	}

	const TArray<FVirtualBone>& VirtualBones = Skeleton->GetVirtualBones();
	if (VirtualBones.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Virtual Bones (%d)\n"), VirtualBones.Num());
		for (const FVirtualBone& VB : VirtualBones)
		{
			Output += FString::Printf(TEXT("%s\t%s -> %s\n"),
				*VB.VirtualBoneName.ToString(), *VB.SourceBoneName.ToString(), *VB.TargetBoneName.ToString());
		}
	}

	const TArray<FAnimSlotGroup>& SlotGroups = Skeleton->GetSlotGroups();
	if (SlotGroups.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Anim Slot Groups (%d)\n"), SlotGroups.Num());
		for (const FAnimSlotGroup& Group : SlotGroups)
		{
			FString SlotNames;
			for (int32 i = 0; i < Group.SlotNames.Num(); i++)
			{
				if (i > 0) SlotNames += TEXT(", ");
				SlotNames += Group.SlotNames[i].ToString();
			}
			Output += FString::Printf(TEXT("%s: %s\n"), *Group.GroupName.ToString(), *SlotNames);
		}
	}

	TArray<FName> CurveNames;
	Skeleton->GetCurveMetaDataNames(CurveNames);
	if (CurveNames.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Curves (%d)\n"), CurveNames.Num());
		for (const FName& CurveName : CurveNames)
		{
			Output += FString::Printf(TEXT("%s\n"), *CurveName.ToString());
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadControlRig(UObject* Asset)
{
	UControlRigBlueprint* CRBlueprint = Cast<UControlRigBlueprint>(Asset);
	if (!CRBlueprint)
	{
		return ReadGenericAsset(Asset);
	}

	URigHierarchy* Hierarchy = CRBlueprint->GetHierarchy();
	if (!Hierarchy)
	{
		return FToolResult::Ok(FString::Printf(TEXT("# CONTROL_RIG: %s\n(no hierarchy data)\n"), *Asset->GetName()));
	}

	FString Output = FString::Printf(TEXT("# CONTROL_RIG: %s\n"), *Asset->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *Asset->GetPathName());

	int32 BoneCount = Hierarchy->Num(ERigElementType::Bone);
	int32 ControlCount = Hierarchy->Num(ERigElementType::Control);
	int32 NullCount = Hierarchy->Num(ERigElementType::Null);
	int32 CurveCount = Hierarchy->Num(ERigElementType::Curve);
	int32 TotalCount = Hierarchy->Num();

	Output += FString::Printf(TEXT("Elements: %d (Bones=%d Controls=%d Nulls=%d Curves=%d)\n"),
		TotalCount, BoneCount, ControlCount, NullCount, CurveCount);

	if (BoneCount > 0)
	{
		Output += FString::Printf(TEXT("\n## Bones (%d)\n"), BoneCount);
		Hierarchy->ForEach<FRigBoneElement>([&](FRigBoneElement* Bone) -> bool
		{
			FString BoneType;
			switch (Bone->BoneType)
			{
				case ERigBoneType::Imported: BoneType = TEXT("Imported"); break;
				case ERigBoneType::User: BoneType = TEXT("User"); break;
				default: BoneType = TEXT("Unknown"); break;
			}
			FRigElementKey ParentKey = Hierarchy->GetFirstParent(Bone->GetKey());
			FString ParentStr = ParentKey.IsValid() ? ParentKey.Name.ToString() : TEXT("");
			Output += FString::Printf(TEXT("[%d] %s\t%s\tParent:%s\n"), Bone->GetIndex(), *Bone->GetName(), *BoneType, *ParentStr);
			return true;
		});
	}

	if (ControlCount > 0)
	{
		Output += FString::Printf(TEXT("\n## Controls (%d)\n"), ControlCount);
		Hierarchy->ForEach<FRigControlElement>([&](FRigControlElement* Control) -> bool
		{
			const FRigControlSettings& Settings = Control->Settings;

			FString ControlType;
			switch (Settings.ControlType)
			{
				case ERigControlType::Bool: ControlType = TEXT("Bool"); break;
				case ERigControlType::Float: ControlType = TEXT("Float"); break;
				case ERigControlType::Integer: ControlType = TEXT("Integer"); break;
				case ERigControlType::Vector2D: ControlType = TEXT("Vector2D"); break;
				case ERigControlType::Position: ControlType = TEXT("Position"); break;
				case ERigControlType::Scale: ControlType = TEXT("Scale"); break;
				case ERigControlType::Rotator: ControlType = TEXT("Rotator"); break;
				case ERigControlType::Transform: ControlType = TEXT("Transform"); break;
				case ERigControlType::TransformNoScale: ControlType = TEXT("TransformNoScale"); break;
				case ERigControlType::EulerTransform: ControlType = TEXT("EulerTransform"); break;
				default: ControlType = TEXT("Unknown"); break;
			}

			FString AnimType;
			switch (Settings.AnimationType)
			{
				case ERigControlAnimationType::AnimationControl: AnimType = TEXT("Control"); break;
				case ERigControlAnimationType::AnimationChannel: AnimType = TEXT("Channel"); break;
				case ERigControlAnimationType::ProxyControl: AnimType = TEXT("Proxy"); break;
				case ERigControlAnimationType::VisualCue: AnimType = TEXT("VisualCue"); break;
				default: AnimType = TEXT("Unknown"); break;
			}

			Output += FString::Printf(TEXT("%s\t%s\tAnimType=%s"),
				*Control->GetName(), *ControlType, *AnimType);

			if (Settings.bShapeVisible && !Settings.ShapeName.IsNone())
			{
				Output += FString::Printf(TEXT("\tShape=%s"), *Settings.ShapeName.ToString());
			}

			FRigElementKey ParentKey = Hierarchy->GetFirstParent(Control->GetKey());
			if (ParentKey.IsValid())
			{
				Output += FString::Printf(TEXT("\tParent:%s"), *ParentKey.Name.ToString());
			}

			const FLinearColor& Color = Settings.ShapeColor;
			Output += FString::Printf(TEXT("\tColor=(%.1f, %.1f, %.1f)"), Color.R, Color.G, Color.B);
			Output += TEXT("\n");
			return true;
		});
	}

	if (NullCount > 0)
	{
		Output += FString::Printf(TEXT("\n## Nulls (%d)\n"), NullCount);
		Hierarchy->ForEach<FRigNullElement>([&](FRigNullElement* Null) -> bool
		{
			FRigElementKey ParentKey = Hierarchy->GetFirstParent(Null->GetKey());
			FString ParentStr = ParentKey.IsValid() ? ParentKey.Name.ToString() : TEXT("");
			Output += FString::Printf(TEXT("[%d] %s\tParent:%s\n"), Null->GetIndex(), *Null->GetName(), *ParentStr);
			return true;
		});
	}

	if (CurveCount > 0)
	{
		Output += FString::Printf(TEXT("\n## Curves (%d)\n"), CurveCount);
		Hierarchy->ForEach<FRigCurveElement>([&](FRigCurveElement* Curve) -> bool
		{
			float Value = Curve->Get();
			bool bIsSet = Curve->IsValueSet();
			Output += FString::Printf(TEXT("%s\t%s\n"), *Curve->GetName(),
				bIsSet ? *FString::Printf(TEXT("%.4f"), Value) : TEXT("(not set)"));
			return true;
		});
	}

		// RigVM Graph Models (default + function graphs)
		TArray<URigVMGraph*> RigGraphs = CRBlueprint->GetAllModels();
		if (RigGraphs.Num() == 0)
		{
			if (URigVMGraph* DefaultGraph = CRBlueprint->GetDefaultModel())
			{
				RigGraphs.Add(DefaultGraph);
			}
		}

		if (RigGraphs.Num() > 0)
		{
			Output += FString::Printf(TEXT("\n## Graph Models (%d)\n"), RigGraphs.Num());
			for (URigVMGraph* RigGraph : RigGraphs)
			{
				if (!RigGraph)
				{
					continue;
				}

				const TArray<URigVMNode*>& Nodes = RigGraph->GetNodes();
				Output += FString::Printf(TEXT("\n### %s (%s)\n"),
					*RigGraph->GetName(),
					*RigGraph->GetNodePath());
				Output += FString::Printf(TEXT("nodes=%d\n"), Nodes.Num());

				for (URigVMNode* Node : Nodes)
				{
					if (!Node) continue;
					Output += FString::Printf(TEXT("%s\t[%s]\n"), *Node->GetName(), *Node->GetNodeTitle());
					for (URigVMPin* Pin : Node->GetPins())
					{
						if (!Pin) continue;
						FString Direction = Pin->GetDirection() == ERigVMPinDirection::Input ? TEXT("In") :
							Pin->GetDirection() == ERigVMPinDirection::Output ? TEXT("Out") : TEXT("IO");
						FString DefaultVal = Pin->GetDefaultValue();
						if (DefaultVal.Len() > 80) DefaultVal = DefaultVal.Left(77) + TEXT("...");
						if (DefaultVal.IsEmpty())
						{
							Output += FString::Printf(TEXT("  %s\t%s\t%s\n"), *Pin->GetName(), *Pin->GetCPPType(), *Direction);
						}
						else
						{
							Output += FString::Printf(TEXT("  %s\t%s\t%s\t=%s\n"), *Pin->GetName(), *Pin->GetCPPType(), *Direction, *DefaultVal);
						}
					}
				}
			}
		}

	// Member Variables
	TArray<FRigVMGraphVariableDescription> Vars = CRBlueprint->GetMemberVariables();
	if (Vars.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Variables (%d)\n"), Vars.Num());
		for (const FRigVMGraphVariableDescription& V : Vars)
		{
			FString DefaultVal = V.DefaultValue;
			if (DefaultVal.Len() > 80) DefaultVal = DefaultVal.Left(77) + TEXT("...");
			Output += FString::Printf(TEXT("%s\t%s\t=%s\n"), *V.Name.ToString(), *V.CPPType, *DefaultVal);
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadPhysicsAsset(UPhysicsAsset* PhysAsset)
{
	FString Output = FString::Printf(TEXT("# PHYSICS_ASSET: %s\n"), *PhysAsset->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *PhysAsset->GetPathName());
	Output += FString::Printf(TEXT("Bodies: %d\n"), PhysAsset->SkeletalBodySetups.Num());
	Output += FString::Printf(TEXT("Constraints: %d\n"), PhysAsset->ConstraintSetup.Num());

	auto PhysTypeStr = [](EPhysicsType Type) -> FString
	{
		switch (Type)
		{
			case PhysType_Default: return TEXT("Default");
			case PhysType_Kinematic: return TEXT("Kinematic");
			case PhysType_Simulated: return TEXT("Simulated");
			default: return TEXT("Unknown");
		}
	};

	auto GetShapeSummary = [](const FKAggregateGeom& Geom) -> FString
	{
		TArray<FString> Shapes;
		for (const FKSphereElem& S : Geom.SphereElems)
		{
			Shapes.Add(FString::Printf(TEXT("Sphere(R=%.1f)"), S.Radius));
		}
		for (const FKBoxElem& B : Geom.BoxElems)
		{
			Shapes.Add(FString::Printf(TEXT("Box(%.1f x %.1f x %.1f)"), B.X, B.Y, B.Z));
		}
		for (const FKSphylElem& C : Geom.SphylElems)
		{
			Shapes.Add(FString::Printf(TEXT("Capsule(R=%.1f, L=%.1f)"), C.Radius, C.Length));
		}
		for (const FKConvexElem& V : Geom.ConvexElems)
		{
			Shapes.Add(FString::Printf(TEXT("Convex(%d verts)"), V.VertexData.Num()));
		}
		for (const FKTaperedCapsuleElem& T : Geom.TaperedCapsuleElems)
		{
			Shapes.Add(FString::Printf(TEXT("TaperedCapsule(R0=%.1f, R1=%.1f, L=%.1f)"), T.Radius0, T.Radius1, T.Length));
		}
		return Shapes.Num() > 0 ? FString::Join(Shapes, TEXT(", ")) : TEXT("None");
	};

	if (PhysAsset->SkeletalBodySetups.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Bodies (%d)\n"), PhysAsset->SkeletalBodySetups.Num());
		for (int32 i = 0; i < PhysAsset->SkeletalBodySetups.Num(); i++)
		{
			USkeletalBodySetup* Body = PhysAsset->SkeletalBodySetups[i];
			if (!Body) continue;

			Output += FString::Printf(TEXT("[%d] %s\t%s\t%s\n"),
				i, *Body->BoneName.ToString(), *PhysTypeStr(Body->PhysicsType), *GetShapeSummary(Body->AggGeom));
		}
	}

	if (PhysAsset->ConstraintSetup.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Constraints (%d)\n"), PhysAsset->ConstraintSetup.Num());
		for (int32 i = 0; i < PhysAsset->ConstraintSetup.Num(); i++)
		{
			UPhysicsConstraintTemplate* CT = PhysAsset->ConstraintSetup[i];
			if (!CT) continue;

			const FConstraintInstance& CI = CT->DefaultInstance;
			Output += FString::Printf(TEXT("[%d] %s -> %s"),
				i, *CI.ConstraintBone1.ToString(), *CI.ConstraintBone2.ToString());

			const FConeConstraint& Cone = CI.ProfileInstance.ConeLimit;
			const FTwistConstraint& Twist = CI.ProfileInstance.TwistLimit;

			TArray<FString> Limits;
			if (Cone.Swing1Motion == EAngularConstraintMotion::ACM_Limited)
			{
				Limits.Add(FString::Printf(TEXT("Swing1=%.1fdeg"), Cone.Swing1LimitDegrees));
			}
			else if (Cone.Swing1Motion == EAngularConstraintMotion::ACM_Free)
			{
				Limits.Add(TEXT("Swing1=Free"));
			}

			if (Cone.Swing2Motion == EAngularConstraintMotion::ACM_Limited)
			{
				Limits.Add(FString::Printf(TEXT("Swing2=%.1fdeg"), Cone.Swing2LimitDegrees));
			}
			else if (Cone.Swing2Motion == EAngularConstraintMotion::ACM_Free)
			{
				Limits.Add(TEXT("Swing2=Free"));
			}

			if (Twist.TwistMotion == EAngularConstraintMotion::ACM_Limited)
			{
				Limits.Add(FString::Printf(TEXT("Twist=%.1fdeg"), Twist.TwistLimitDegrees));
			}
			else if (Twist.TwistMotion == EAngularConstraintMotion::ACM_Free)
			{
				Limits.Add(TEXT("Twist=Free"));
			}

			if (Limits.Num() > 0)
			{
				Output += TEXT("\t") + FString::Join(Limits, TEXT(" "));
			}
			else
			{
				Output += TEXT("\tLocked");
			}

			if (CI.ProfileInstance.bDisableCollision)
			{
				Output += TEXT(" (no collision)");
			}

			Output += TEXT("\n");
		}
	}

	return FToolResult::Ok(Output);
}
