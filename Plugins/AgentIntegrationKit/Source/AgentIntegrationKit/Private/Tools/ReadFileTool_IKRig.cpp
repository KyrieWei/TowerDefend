// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6

#include "Rig/IKRigDefinition.h"
#include "Rig/Solvers/IKRigSolverBase.h"
#include "RigEditor/IKRigController.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/IKRetargetChainMapping.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Retargeter/RetargetOps/IKChainsOp.h"
#include "Engine/SkeletalMesh.h"

#if WITH_POSE_SEARCH
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchFeatureChannel_Velocity.h"
#include "PoseSearch/PoseSearchFeatureChannel_Position.h"
#include "PoseSearch/PoseSearchFeatureChannel_Heading.h"
#include "PoseSearch/PoseSearchFeatureChannel_Phase.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"
#endif // WITH_POSE_SEARCH

FToolResult FReadFileTool::ReadIKRig(UIKRigDefinition* IKRig)
{
	FString RigSummary;
	RigSummary += FString::Printf(TEXT("IK Rig: %s\n"), *IKRig->GetName());
	RigSummary += TEXT("================\n\n");

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		return FToolResult::Fail(TEXT("Failed to get IK Rig controller"));
	}

	USkeletalMesh* IKRigSkelMesh = Controller->GetSkeletalMesh();
	RigSummary += FString::Printf(TEXT("Skeletal Mesh: %s\n\n"),
		IKRigSkelMesh ? *IKRigSkelMesh->GetPathName() : TEXT("None"));

	int32 NumSolvers = Controller->GetNumSolvers();
	RigSummary += FString::Printf(TEXT("Solvers (%d):\n"), NumSolvers);
	for (int32 i = 0; i < NumSolvers; i++)
	{
		FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(i);
		if (Solver)
		{
			FString SolverName = Controller->GetSolverUniqueName(i);
			bool bEnabled = Controller->GetSolverEnabled(i);
			FName StartBone = Controller->GetStartBone(i);
			FName EndBone = Controller->GetEndBone(i);

			RigSummary += FString::Printf(TEXT("  [%d] %s (%s)\n"),
				i, *SolverName, bEnabled ? TEXT("enabled") : TEXT("disabled"));

			if (!StartBone.IsNone())
			{
				RigSummary += FString::Printf(TEXT("      Start: %s\n"), *StartBone.ToString());
			}
			if (!EndBone.IsNone())
			{
				RigSummary += FString::Printf(TEXT("      End: %s\n"), *EndBone.ToString());
			}
		}
	}
	RigSummary += TEXT("\n");

	const TArray<UIKRigEffectorGoal*>& Goals = Controller->GetAllGoals();
	RigSummary += FString::Printf(TEXT("Goals (%d):\n"), Goals.Num());
	for (const UIKRigEffectorGoal* Goal : Goals)
	{
		if (Goal)
		{
			FName BoneName = Controller->GetBoneForGoal(Goal->GoalName);
			RigSummary += FString::Printf(TEXT("  - %s -> bone: %s\n"),
				*Goal->GoalName.ToString(), *BoneName.ToString());

			TArray<int32> ConnectedSolvers;
			for (int32 i = 0; i < NumSolvers; i++)
			{
				if (Controller->IsGoalConnectedToSolver(Goal->GoalName, i))
				{
					ConnectedSolvers.Add(i);
				}
			}
			if (ConnectedSolvers.Num() > 0)
			{
				FString SolverList;
				for (int32 SolverIdx : ConnectedSolvers)
				{
					if (!SolverList.IsEmpty()) SolverList += TEXT(", ");
					SolverList += FString::Printf(TEXT("%d"), SolverIdx);
				}
				RigSummary += FString::Printf(TEXT("    Connected to solvers: [%s]\n"), *SolverList);
			}
		}
	}
	RigSummary += TEXT("\n");

	const TArray<FBoneChain>& Chains = Controller->GetRetargetChains();
	RigSummary += FString::Printf(TEXT("Retarget Chains (%d):\n"), Chains.Num());
	for (const FBoneChain& Chain : Chains)
	{
		RigSummary += FString::Printf(TEXT("  - %s: %s -> %s\n"),
			*Chain.ChainName.ToString(),
			*Chain.StartBone.BoneName.ToString(),
			*Chain.EndBone.BoneName.ToString());
		if (!Chain.IKGoalName.IsNone())
		{
			RigSummary += FString::Printf(TEXT("    Goal: %s\n"), *Chain.IKGoalName.ToString());
		}
	}
	RigSummary += TEXT("\n");

	FName RetargetRoot = Controller->GetRetargetRoot();
	RigSummary += FString::Printf(TEXT("Retarget Root: %s\n"),
		RetargetRoot.IsNone() ? TEXT("Not set") : *RetargetRoot.ToString());

	return FToolResult::Ok(RigSummary);
}

FToolResult FReadFileTool::ReadIKRetargeter(UIKRetargeter* Retargeter)
{
	FString RetargetSummary;
	RetargetSummary += FString::Printf(TEXT("IK Retargeter: %s\n"), *Retargeter->GetName());
	RetargetSummary += TEXT("================\n\n");

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		return FToolResult::Fail(TEXT("Failed to get IK Retargeter controller"));
	}

	const UIKRigDefinition* SourceRig = Controller->GetIKRig(ERetargetSourceOrTarget::Source);
	RetargetSummary += FString::Printf(TEXT("Source IK Rig: %s\n"),
		SourceRig ? *SourceRig->GetPathName() : TEXT("None"));

	const UIKRigDefinition* TargetRig = Controller->GetIKRig(ERetargetSourceOrTarget::Target);
	RetargetSummary += FString::Printf(TEXT("Target IK Rig: %s\n\n"),
		TargetRig ? *TargetRig->GetPathName() : TEXT("None"));

	int32 NumOps = Controller->GetNumRetargetOps();
	RetargetSummary += FString::Printf(TEXT("Retarget Ops (%d):\n"), NumOps);
	for (int32 i = 0; i < NumOps; i++)
	{
		FName OpName = Controller->GetOpName(i);
		bool bEnabled = Controller->GetRetargetOpEnabled(i);
		FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(i);

		FString TypeName = TEXT("Unknown");
		if (Op)
		{
			const UScriptStruct* Type = Op->GetType();
			if (Type) TypeName = Type->GetAuthoredName();
		}

		int32 ParentIndex = Controller->GetParentOpIndex(i);
		FString ParentStr;
		if (ParentIndex != INDEX_NONE)
		{
			ParentStr = FString::Printf(TEXT("\tParent:%s"), *Controller->GetOpName(ParentIndex).ToString());
		}

		RetargetSummary += FString::Printf(TEXT("  [%d] %s\t%s\t%s%s\n"),
			i, *OpName.ToString(), *TypeName, bEnabled ? TEXT("enabled") : TEXT("disabled"), *ParentStr);
	}
	RetargetSummary += TEXT("\n");

	// Per-op chain mappings with FK/IK settings
	RetargetSummary += TEXT("Chain Mappings (per-op):\n");
	bool bFoundChainMappings = false;
	for (int32 i = 0; i < NumOps; i++)
	{
		FIKRetargetOpBase* Op = Controller->GetRetargetOpByIndex(i);
		if (!Op) continue;
		FRetargetChainMapping* Mapping = Op->GetChainMapping();
		if (!Mapping) continue;

		FName OpName = Controller->GetOpName(i);
		const UScriptStruct* OpType = Op->GetType();
		RetargetSummary += FString::Printf(TEXT("  Op: %s\n"), *OpName.ToString());
		bFoundChainMappings = true;

		bool bIsFKOp = OpType && OpType->IsChildOf(FIKRetargetFKChainsOp::StaticStruct());
		bool bIsIKOp = OpType && OpType->IsChildOf(FIKRetargetIKChainsOp::StaticStruct());

		TMap<FName, const FRetargetFKChainSettings*> FKSettingsMap;
		TMap<FName, const FRetargetIKChainSettings*> IKSettingsMap;
		if (bIsFKOp)
		{
			FIKRetargetFKChainsOp* FKOp = static_cast<FIKRetargetFKChainsOp*>(Op);
			FIKRetargetFKChainsOpSettings* FKSettings = static_cast<FIKRetargetFKChainsOpSettings*>(FKOp->GetSettings());
			if (FKSettings)
			{
				for (const FRetargetFKChainSettings& CS : FKSettings->ChainsToRetarget)
					FKSettingsMap.Add(CS.TargetChainName, &CS);
			}
		}
		else if (bIsIKOp)
		{
			FIKRetargetIKChainsOp* IKOp = static_cast<FIKRetargetIKChainsOp*>(Op);
			FIKRetargetIKChainsOpSettings* IKSettings = static_cast<FIKRetargetIKChainsOpSettings*>(IKOp->GetSettings());
			if (IKSettings)
			{
				for (const FRetargetIKChainSettings& CS : IKSettings->ChainsToRetarget)
					IKSettingsMap.Add(CS.TargetChainName, &CS);
			}
		}

		const TArray<FRetargetChainPair>& ChainPairs = Mapping->GetChainPairs();
		for (const FRetargetChainPair& Pair : ChainPairs)
		{
			FString SourceName = Pair.SourceChainName.IsNone() ? TEXT("(unmapped)") : Pair.SourceChainName.ToString();
			FString SettingsStr;
			if (const FRetargetFKChainSettings* const* FKS = FKSettingsMap.Find(Pair.TargetChainName))
			{
				SettingsStr = FString::Printf(TEXT("\tFK:RotAlpha=%.2f,TransAlpha=%.2f,%s"),
					(*FKS)->RotationAlpha, (*FKS)->TranslationAlpha, (*FKS)->EnableFK ? TEXT("On") : TEXT("Off"));
			}
			else if (const FRetargetIKChainSettings* const* IKS = IKSettingsMap.Find(Pair.TargetChainName))
			{
				SettingsStr = FString::Printf(TEXT("\tIK:Blend=%.2f,Ext=%.2f,%s"),
					(*IKS)->BlendToSource, (*IKS)->Extension, (*IKS)->EnableIK ? TEXT("On") : TEXT("Off"));
			}
			RetargetSummary += FString::Printf(TEXT("    %s <- %s%s\n"), *Pair.TargetChainName.ToString(), *SourceName, *SettingsStr);
		}
	}
	if (!bFoundChainMappings)
	{
		RetargetSummary += TEXT("  (no ops with chain mappings)\n");
	}
	RetargetSummary += TEXT("\n");

	FName SourcePose = Controller->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Source);
	FName TargetPose = Controller->GetCurrentRetargetPoseName(ERetargetSourceOrTarget::Target);
	RetargetSummary += FString::Printf(TEXT("Current Source Pose: %s\n"), *SourcePose.ToString());
	RetargetSummary += FString::Printf(TEXT("Current Target Pose: %s\n"), *TargetPose.ToString());

	return FToolResult::Ok(RetargetSummary);
}

#if WITH_POSE_SEARCH

FToolResult FReadFileTool::ReadPoseSearchSchema(UPoseSearchSchema* PoseSchema)
{
	FString Out;
	Out += FString::Printf(TEXT("Pose Search Schema: %s\n"), *PoseSchema->GetName());
	Out += TEXT("================\n\n");

	const auto& Skeletons = PoseSchema->GetRoledSkeletons();
	Out += FString::Printf(TEXT("Skeletons (%d):\n"), Skeletons.Num());
	for (const auto& RoledSkel : Skeletons)
	{
		USkeleton* Skel = RoledSkel.Skeleton.Get();
		Out += FString::Printf(TEXT("  - %s\n"), Skel ? *Skel->GetPathName() : TEXT("None"));
	}
	Out += TEXT("\n");

	Out += FString::Printf(TEXT("Sample Rate: %d\n"), PoseSchema->SampleRate);

	FString PreprocessorStr;
	switch (PoseSchema->DataPreprocessor)
	{
	case EPoseSearchDataPreprocessor::None: PreprocessorStr = TEXT("None"); break;
	case EPoseSearchDataPreprocessor::Normalize: PreprocessorStr = TEXT("Normalize"); break;
	case EPoseSearchDataPreprocessor::NormalizeOnlyByDeviation: PreprocessorStr = TEXT("NormalizeOnlyByDeviation"); break;
	case EPoseSearchDataPreprocessor::NormalizeWithCommonSchema: PreprocessorStr = TEXT("NormalizeWithCommonSchema"); break;
	default: PreprocessorStr = TEXT("Unknown"); break;
	}
	Out += FString::Printf(TEXT("Data Preprocessor: %s\n\n"), *PreprocessorStr);

	TConstArrayView<TObjectPtr<UPoseSearchFeatureChannel>> Channels = PoseSchema->GetChannels();
	Out += FString::Printf(TEXT("Channels (%d):\n"), Channels.Num());
	for (int32 i = 0; i < Channels.Num(); i++)
	{
		UPoseSearchFeatureChannel* Channel = Channels[i];
		if (!Channel) continue;

		FString ChannelType = Channel->GetClass()->GetName();
		ChannelType.RemoveFromStart(TEXT("PoseSearchFeatureChannel_"));

		Out += FString::Printf(TEXT("  [%d] %s"), i, *ChannelType);

		if (UPoseSearchFeatureChannel_Pose* PoseCh = Cast<UPoseSearchFeatureChannel_Pose>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, bones=%d)\n"), PoseCh->Weight, PoseCh->SampledBones.Num());
			for (const FPoseSearchBone& Bone : PoseCh->SampledBones)
			{
				FString Flags;
				if (Bone.Flags & int32(EPoseSearchBoneFlags::Position)) Flags += TEXT("Position ");
				if (Bone.Flags & int32(EPoseSearchBoneFlags::Velocity)) Flags += TEXT("Velocity ");
				if (Bone.Flags & int32(EPoseSearchBoneFlags::Rotation)) Flags += TEXT("Rotation ");
				if (Bone.Flags & int32(EPoseSearchBoneFlags::Phase)) Flags += TEXT("Phase ");
				Out += FString::Printf(TEXT("      %s [%s] weight=%.2f\n"),
					*Bone.Reference.BoneName.ToString(), *Flags.TrimEnd(), Bone.Weight);
			}
		}
		else if (UPoseSearchFeatureChannel_Trajectory* TrajCh = Cast<UPoseSearchFeatureChannel_Trajectory>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, samples=%d)\n"), TrajCh->Weight, TrajCh->Samples.Num());
			for (const FPoseSearchTrajectorySample& Sample : TrajCh->Samples)
			{
				FString Flags;
				if (Sample.Flags & int32(EPoseSearchTrajectoryFlags::Position)) Flags += TEXT("Position ");
				if (Sample.Flags & int32(EPoseSearchTrajectoryFlags::Velocity)) Flags += TEXT("Velocity ");
				if (Sample.Flags & int32(EPoseSearchTrajectoryFlags::VelocityDirection)) Flags += TEXT("VelocityDir ");
				if (Sample.Flags & int32(EPoseSearchTrajectoryFlags::FacingDirection)) Flags += TEXT("FacingDir ");
				if (Sample.Flags & int32(EPoseSearchTrajectoryFlags::PositionXY)) Flags += TEXT("PositionXY ");
				if (Sample.Flags & int32(EPoseSearchTrajectoryFlags::VelocityXY)) Flags += TEXT("VelocityXY ");
				Out += FString::Printf(TEXT("      offset=%.2fs [%s] weight=%.2f\n"),
					Sample.Offset, *Flags.TrimEnd(), Sample.Weight);
			}
		}
		else if (UPoseSearchFeatureChannel_Velocity* VelCh = Cast<UPoseSearchFeatureChannel_Velocity>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, bone=%s)\n"),
				VelCh->Weight, *VelCh->Bone.BoneName.ToString());
		}
		else if (UPoseSearchFeatureChannel_Position* PosCh = Cast<UPoseSearchFeatureChannel_Position>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, bone=%s)\n"),
				PosCh->Weight, *PosCh->Bone.BoneName.ToString());
		}
		else if (UPoseSearchFeatureChannel_Heading* HeadCh = Cast<UPoseSearchFeatureChannel_Heading>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, bone=%s)\n"),
				HeadCh->Weight, *HeadCh->Bone.BoneName.ToString());
		}
		else if (UPoseSearchFeatureChannel_Phase* PhaseCh = Cast<UPoseSearchFeatureChannel_Phase>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, bone=%s)\n"),
				PhaseCh->Weight, *PhaseCh->Bone.BoneName.ToString());
		}
		else if (UPoseSearchFeatureChannel_Curve* CurveCh = Cast<UPoseSearchFeatureChannel_Curve>(Channel))
		{
			Out += FString::Printf(TEXT(" (weight=%.2f, curve=%s)\n"),
				CurveCh->Weight, *CurveCh->CurveName.ToString());
		}
		else
		{
			Out += TEXT("\n");
		}
	}

	return FToolResult::Ok(Out);
}

FToolResult FReadFileTool::ReadPoseSearchDatabase(UPoseSearchDatabase* PoseDB)
{
	FString Out;
	Out += FString::Printf(TEXT("Pose Search Database: %s\n"), *PoseDB->GetName());
	Out += TEXT("================\n\n");

	Out += FString::Printf(TEXT("Schema: %s\n"),
		PoseDB->Schema ? *PoseDB->Schema->GetPathName() : TEXT("None"));

	FString ModeStr;
	switch (PoseDB->PoseSearchMode)
	{
	case EPoseSearchMode::BruteForce: ModeStr = TEXT("BruteForce"); break;
	case EPoseSearchMode::PCAKDTree: ModeStr = TEXT("PCAKDTree"); break;
	case EPoseSearchMode::VPTree: ModeStr = TEXT("VPTree"); break;
	default: ModeStr = TEXT("Other"); break;
	}
	Out += FString::Printf(TEXT("Search Mode: %s\n"), *ModeStr);

	Out += FString::Printf(TEXT("Continuing Pose Cost Bias: %.4f\n"), PoseDB->ContinuingPoseCostBias);
	Out += FString::Printf(TEXT("Base Cost Bias: %.4f\n"), PoseDB->BaseCostBias);
	Out += FString::Printf(TEXT("Looping Cost Bias: %.4f\n"), PoseDB->LoopingCostBias);

	Out += FString::Printf(TEXT("Normalization Set: %s\n\n"),
		PoseDB->NormalizationSet ? *PoseDB->NormalizationSet->GetPathName() : TEXT("None"));

	int32 NumAnims = PoseDB->GetNumAnimationAssets();
	Out += FString::Printf(TEXT("Animations (%d):\n"), NumAnims);
	for (int32 i = 0; i < NumAnims; i++)
	{
		UObject* AnimAsset = PoseDB->GetAnimationAsset(i);
		if (!AnimAsset) continue;

#if ENGINE_MINOR_VERSION >= 7
		const auto* Entry = PoseDB->GetDatabaseAnimationAsset(i);
		if (Entry)
		{
			FString MirrorStr;
			switch (Entry->GetMirrorOption())
			{
			case EPoseSearchMirrorOption::UnmirroredOnly: MirrorStr = TEXT("Original"); break;
			case EPoseSearchMirrorOption::MirroredOnly: MirrorStr = TEXT("Mirrored"); break;
			case EPoseSearchMirrorOption::UnmirroredAndMirrored: MirrorStr = TEXT("Both"); break;
			default: MirrorStr = TEXT("Unknown"); break;
			}
			Out += FString::Printf(TEXT("  [%d] %s (%s) %s mirror=%s\n"),
				i, *AnimAsset->GetName(), *AnimAsset->GetClass()->GetName(),
				Entry->IsEnabled() ? TEXT("enabled") : TEXT("disabled"),
				*MirrorStr);
		}
		else
#endif
		{
			Out += FString::Printf(TEXT("  [%d] %s (%s)\n"), i, *AnimAsset->GetName(), *AnimAsset->GetClass()->GetName());
		}
	}

	return FToolResult::Ok(Out);
}

FToolResult FReadFileTool::ReadPoseSearchNormalizationSet(UPoseSearchNormalizationSet* NormSet)
{
	FString Out;
	Out += FString::Printf(TEXT("Pose Search Normalization Set: %s\n"), *NormSet->GetName());
	Out += TEXT("================\n\n");

	Out += FString::Printf(TEXT("Databases (%d):\n"), NormSet->Databases.Num());
	for (int32 i = 0; i < NormSet->Databases.Num(); i++)
	{
		const UPoseSearchDatabase* DB = NormSet->Databases[i];
		Out += FString::Printf(TEXT("  [%d] %s\n"), i,
			DB ? *DB->GetPathName() : TEXT("None"));
	}

	return FToolResult::Ok(Out);
}

#endif // WITH_POSE_SEARCH

#endif // ENGINE_MINOR_VERSION >= 6
