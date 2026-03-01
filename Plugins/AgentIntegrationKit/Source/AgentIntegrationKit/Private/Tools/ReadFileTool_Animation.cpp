// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimStateConduitNode.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimationCustomTransitionGraph.h"
#include "K2Node_Composite.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"

// Helper to safely get previous/next state from transition nodes
// GetInputPin/GetOutputPin crash if Pins array is empty
// GetPreviousState/GetNextState crash if pins have no connections (array out of bounds)
namespace
{
	UAnimStateNodeBase* SafeGetPreviousState(UAnimStateTransitionNode* TransNode)
	{
		if (!TransNode || TransNode->Pins.Num() < 2) return nullptr;
		UEdGraphPin* InputPin = TransNode->GetInputPin();
		if (!InputPin || InputPin->LinkedTo.Num() == 0) return nullptr;
		return TransNode->GetPreviousState();
	}

	UAnimStateNodeBase* SafeGetNextState(UAnimStateTransitionNode* TransNode)
	{
		if (!TransNode || TransNode->Pins.Num() < 2) return nullptr;
		UEdGraphPin* OutputPin = TransNode->GetOutputPin();
		if (!OutputPin || OutputPin->LinkedTo.Num() == 0) return nullptr;
		return TransNode->GetNextState();
	}
}

FString FReadFileTool::GetAnimBlueprintSummary(UAnimBlueprint* AnimBlueprint)
{
	FString ParentName = AnimBlueprint->ParentClass ? AnimBlueprint->ParentClass->GetName() : TEXT("AnimInstance");

	// Get skeleton info
	FString SkeletonName = TEXT("None");
	if (AnimBlueprint->TargetSkeleton)
	{
		SkeletonName = AnimBlueprint->TargetSkeleton->GetName();
	}

	int32 VarCount = AnimBlueprint->NewVariables.Num();
	int32 GraphCount = AnimBlueprint->UbergraphPages.Num() + AnimBlueprint->FunctionGraphs.Num();

	// Count state machines across all animation graphs
	int32 StateMachineCount = 0;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Cast<UAnimationGraph>(Graph))
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Cast<UAnimGraphNode_StateMachine>(Node))
				{
					StateMachineCount++;
				}
			}
		}
	}

	FString Output = FString::Printf(TEXT("# ANIM_BLUEPRINT %s parent=%s skeleton=%s\nvariables=%d graphs=%d state_machines=%d\n"),
		*AnimBlueprint->GetName(), *ParentName, *SkeletonName, VarCount, GraphCount, StateMachineCount);

	// Add graph list
	Output += FString::Printf(TEXT("\n# GRAPHS %d\n"), GraphCount);

	for (UEdGraph* Graph : AnimBlueprint->UbergraphPages)
	{
		Output += FString::Printf(TEXT("%s\tubergraph\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		Output += FString::Printf(TEXT("%s\tfunction\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}

	return Output;
}

FString FReadFileTool::GetAnimBlueprintStateMachines(UAnimBlueprint* AnimBlueprint)
{
	FString Output;

	// Collect all animation graphs (AnimGraph + animation layers)
	TArray<UAnimationGraph*> AnimGraphs;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph))
		{
			AnimGraphs.Add(AnimGraph);
		}
	}

	if (AnimGraphs.Num() == 0)
	{
		return TEXT("# STATE_MACHINES 0\n(no animation graphs found)\n");
	}

	// Pre-count state machines
	int32 TotalStateMachines = 0;
	for (UAnimationGraph* AnimGraph : AnimGraphs)
	{
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (Cast<UAnimGraphNode_StateMachine>(Node))
			{
				TotalStateMachines++;
			}
		}
	}

	Output = FString::Printf(TEXT("# STATE_MACHINES %d\n"), TotalStateMachines);

	for (UAnimationGraph* AnimGraph : AnimGraphs)
	{
		// Collect state machines for this graph
		TArray<UAnimGraphNode_StateMachine*> StateMachines;
		for (UEdGraphNode* Node : AnimGraph->Nodes)
		{
			if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
			{
				StateMachines.Add(SMNode);
			}
		}

		Output += FString::Printf(TEXT("\n# ANIM_GRAPH %s state_machines=%d\n"),
			*AnimGraph->GetName(), StateMachines.Num());

		// Output each state machine
		for (UAnimGraphNode_StateMachine* SMNode : StateMachines)
		{
			FString SMName = SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			FString SMGuid = NeoStackToolUtils::GetNodeGuid(SMNode);

			// Get the state machine graph
			UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
			if (!SMGraph)
			{
				Output += FString::Printf(TEXT("\n## STATE_MACHINE %s guid=%s\n(no graph)\n"), *SMName, *SMGuid);
				continue;
			}

			// Count states and transitions
			int32 StateCount = 0;
			int32 TransitionCount = 0;
			for (UEdGraphNode* GraphNode : SMGraph->Nodes)
			{
				if (Cast<UAnimStateNode>(GraphNode))
				{
					StateCount++;
				}
				else if (Cast<UAnimStateTransitionNode>(GraphNode))
				{
					TransitionCount++;
				}
			}

			Output += FString::Printf(TEXT("\n## STATE_MACHINE %s guid=%s states=%d transitions=%d\n"),
				*SMName, *SMGuid, StateCount, TransitionCount);

			// List states
			Output += TEXT("# STATES\n");
			for (UEdGraphNode* GraphNode : SMGraph->Nodes)
			{
				if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(GraphNode))
				{
					FString StateName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
					FString StateGuid = NeoStackToolUtils::GetNodeGuid(StateNode);

					// Check if this state has a bound graph (the state's animation logic)
					FString HasGraph = StateNode->BoundGraph ? TEXT("has_graph") : TEXT("no_graph");

					Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *StateGuid, *StateName, *HasGraph);
				}
				else if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(GraphNode))
				{
					FString EntryGuid = NeoStackToolUtils::GetNodeGuid(EntryNode);
					Output += FString::Printf(TEXT("%s\t[Entry]\tentry_point\n"), *EntryGuid);
				}
			}

			// List transitions (including custom transition graphs)
			Output += TEXT("# TRANSITIONS\n");
			for (UEdGraphNode* GraphNode : SMGraph->Nodes)
			{
				if (UAnimStateTransitionNode* TransNode = Cast<UAnimStateTransitionNode>(GraphNode))
				{
					FString TransGuid = NeoStackToolUtils::GetNodeGuid(TransNode);

					// Get source and destination states
					FString FromState = TEXT("Unknown");
					FString ToState = TEXT("Unknown");

					if (UAnimStateNodeBase* PrevState = SafeGetPreviousState(TransNode))
					{
						FromState = PrevState->GetNodeTitle(ENodeTitleType::ListView).ToString();
					}
					if (UAnimStateNodeBase* NextState = SafeGetNextState(TransNode))
					{
						ToState = NextState->GetNodeTitle(ENodeTitleType::ListView).ToString();
					}

					// Check if transition has a graph (condition logic)
					FString HasConditionGraph = TEXT("no_condition");
					if (UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransNode->BoundGraph))
					{
						HasConditionGraph = FString::Printf(TEXT("condition_graph=%s"), *TransGraph->GetName());
					}

					// Check for custom transition graph
					if (TransNode->CustomTransitionGraph)
					{
						HasConditionGraph += FString::Printf(TEXT(" custom_graph=%s"), *TransNode->CustomTransitionGraph->GetName());
					}

					Output += FString::Printf(TEXT("%s\t%s -> %s\t%s\n"),
						*TransGuid, *FromState, *ToState, *HasConditionGraph);
				}
			}
		}
	}

	return Output;
}

void FReadFileTool::CollectAnimBlueprintGraphs(UAnimBlueprint* AnimBlueprint, TArray<TPair<UEdGraph*, FString>>& OutGraphs)
{
	// Collect all animation graphs (AnimGraph + layers)
	TArray<UAnimationGraph*> AnimGraphs;
	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph))
		{
			AnimGraphs.Add(AnimGraph);
		}
	}

	if (AnimGraphs.Num() == 0)
	{
		return;
	}

	TSet<UEdGraph*> Visited;

	auto CollectChildGraphsFromNodes = [&](UEdGraph* GraphToSearch, const FString& RootName, auto&& CollectRef) -> void
	{
		if (!GraphToSearch || Visited.Contains(GraphToSearch))
		{
			return;
		}
		Visited.Add(GraphToSearch);

		for (UEdGraphNode* CurrentNode : GraphToSearch->Nodes)
		{
			if (UAnimGraphNode_StateMachineBase* StateMachine = Cast<UAnimGraphNode_StateMachineBase>(CurrentNode))
			{
				if (StateMachine->EditorStateMachineGraph)
				{
					FString SMName = StateMachine->GetNodeTitle(ENodeTitleType::ListView).ToString();
					OutGraphs.Add(TPair<UEdGraph*, FString>(StateMachine->EditorStateMachineGraph,
						FString::Printf(TEXT("statemachine:%s/%s"), *RootName, *SMName)));
					CollectRef(StateMachine->EditorStateMachineGraph, RootName, CollectRef);
				}
			}
			else if (UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(CurrentNode))
			{
				UEdGraph* BoundGraph = StateNode->GetBoundGraph();
				if (BoundGraph)
				{
					if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(StateNode))
					{
						FString FromState = TEXT("?");
						FString ToState = TEXT("?");
						if (UAnimStateNodeBase* PrevState = SafeGetPreviousState(TransitionNode))
						{
							FromState = PrevState->GetNodeTitle(ENodeTitleType::ListView).ToString();
						}
						if (UAnimStateNodeBase* NextState = SafeGetNextState(TransitionNode))
						{
							ToState = NextState->GetNodeTitle(ENodeTitleType::ListView).ToString();
						}

						OutGraphs.Add(TPair<UEdGraph*, FString>(BoundGraph,
							FString::Printf(TEXT("transition:%s/%s->%s"), *RootName, *FromState, *ToState)));
						CollectRef(BoundGraph, RootName, CollectRef);

						if (TransitionNode->CustomTransitionGraph)
						{
							OutGraphs.Add(TPair<UEdGraph*, FString>(TransitionNode->CustomTransitionGraph,
								FString::Printf(TEXT("custom_transition:%s/%s->%s"), *RootName, *FromState, *ToState)));
							CollectRef(TransitionNode->CustomTransitionGraph, RootName, CollectRef);
						}
					}
					else
					{
						FString NodeName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
						FString TypePrefix = StateNode->IsA<UAnimStateConduitNode>() ? TEXT("conduit") : TEXT("state");
						OutGraphs.Add(TPair<UEdGraph*, FString>(BoundGraph,
							FString::Printf(TEXT("%s:%s/%s"), *TypePrefix, *RootName, *NodeName)));
						CollectRef(BoundGraph, RootName, CollectRef);
					}
				}
			}
			else if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(CurrentNode))
			{
				if (CompositeNode->BoundGraph)
				{
					OutGraphs.Add(TPair<UEdGraph*, FString>(CompositeNode->BoundGraph,
						FString::Printf(TEXT("composite:%s/%s"), *RootName, *CompositeNode->GetNodeTitle(ENodeTitleType::ListView).ToString())));
					CollectRef(CompositeNode->BoundGraph, RootName, CollectRef);
				}
			}
		}
	};

	// Add anim graphs and collect their subgraphs
	for (UAnimationGraph* AnimGraph : AnimGraphs)
	{
		OutGraphs.Add(TPair<UEdGraph*, FString>(AnimGraph,
			FString::Printf(TEXT("animgraph:%s"), *AnimGraph->GetName())));

		CollectChildGraphsFromNodes(AnimGraph, AnimGraph->GetName(), CollectChildGraphsFromNodes);
	}
}

FToolResult FReadFileTool::ReadBlendSpace(UBlendSpace* BlendSpace)
{
	bool bIs1D = BlendSpace->IsA<UBlendSpace1D>();

	FString Output = FString::Printf(TEXT("# BLEND_SPACE: %s (%s)\n"),
		*BlendSpace->GetName(), bIs1D ? TEXT("1D") : TEXT("2D"));
	Output += FString::Printf(TEXT("Path: %s\n"), *BlendSpace->GetPathName());

	USkeleton* Skeleton = BlendSpace->GetSkeleton();
	if (Skeleton)
	{
		Output += FString::Printf(TEXT("Skeleton: %s\n"), *Skeleton->GetPathName());
	}

	Output += TEXT("\n## Axes\n");
	int32 NumAxes = bIs1D ? 1 : 2;
	for (int32 i = 0; i < NumAxes; i++)
	{
		const FBlendParameter& Param = BlendSpace->GetBlendParameter(i);
		FString AxisLabel = (i == 0) ? TEXT("X") : TEXT("Y");
		Output += FString::Printf(TEXT("%s: %s\tMin=%.1f\tMax=%.1f\tGridDivisions=%d"),
			*AxisLabel, *Param.DisplayName, Param.Min, Param.Max, Param.GridNum);
		if (Param.bWrapInput)
		{
			Output += TEXT("\tWrap=true");
		}
		Output += TEXT("\n");
	}

	const TArray<FBlendSample>& Samples = BlendSpace->GetBlendSamples();
	if (Samples.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Samples (%d)\n"), Samples.Num());
		for (int32 i = 0; i < Samples.Num(); i++)
		{
			const FBlendSample& Sample = Samples[i];
			FString AnimName = Sample.Animation ? Sample.Animation->GetName() : TEXT("None");

			if (bIs1D)
			{
				Output += FString::Printf(TEXT("[%d] %s\tPosition=%.2f\tRateScale=%.2f\n"),
					i, *AnimName, Sample.SampleValue.X, Sample.RateScale);
			}
			else
			{
				Output += FString::Printf(TEXT("[%d] %s\tPosition=(%.2f, %.2f)\tRateScale=%.2f\n"),
					i, *AnimName, Sample.SampleValue.X, Sample.SampleValue.Y, Sample.RateScale);
			}
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadAnimMontage(UAnimMontage* Montage)
{
	FString Output = FString::Printf(TEXT("# ANIM_MONTAGE: %s\n"), *Montage->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *Montage->GetPathName());

	USkeleton* Skeleton = Montage->GetSkeleton();
	if (Skeleton)
	{
		Output += FString::Printf(TEXT("Skeleton: %s\n"), *Skeleton->GetPathName());
	}

	Output += FString::Printf(TEXT("Duration: %.3fs\n"), Montage->GetPlayLength());

	FString BlendInOption;
	switch (Montage->BlendIn.GetBlendOption())
	{
		case EAlphaBlendOption::Linear: BlendInOption = TEXT("Linear"); break;
		case EAlphaBlendOption::Cubic: BlendInOption = TEXT("Cubic"); break;
		case EAlphaBlendOption::HermiteCubic: BlendInOption = TEXT("HermiteCubic"); break;
		case EAlphaBlendOption::Sinusoidal: BlendInOption = TEXT("Sinusoidal"); break;
		case EAlphaBlendOption::Custom: BlendInOption = TEXT("Custom"); break;
		default: BlendInOption = TEXT("Other"); break;
	}

	FString BlendOutOption;
	switch (Montage->BlendOut.GetBlendOption())
	{
		case EAlphaBlendOption::Linear: BlendOutOption = TEXT("Linear"); break;
		case EAlphaBlendOption::Cubic: BlendOutOption = TEXT("Cubic"); break;
		case EAlphaBlendOption::HermiteCubic: BlendOutOption = TEXT("HermiteCubic"); break;
		case EAlphaBlendOption::Sinusoidal: BlendOutOption = TEXT("Sinusoidal"); break;
		case EAlphaBlendOption::Custom: BlendOutOption = TEXT("Custom"); break;
		default: BlendOutOption = TEXT("Other"); break;
	}

	Output += FString::Printf(TEXT("Blend In: %.3fs (%s)\n"), Montage->BlendIn.GetBlendTime(), *BlendInOption);
	Output += FString::Printf(TEXT("Blend Out: %.3fs (%s)\n"), Montage->BlendOut.GetBlendTime(), *BlendOutOption);
	Output += FString::Printf(TEXT("Blend Out Trigger: %.3fs\n"), Montage->BlendOutTriggerTime);
	Output += FString::Printf(TEXT("Auto Blend Out: %s\n"), Montage->bEnableAutoBlendOut ? TEXT("true") : TEXT("false"));

	if (Montage->CompositeSections.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Sections (%d)\n"), Montage->CompositeSections.Num());
		for (int32 i = 0; i < Montage->CompositeSections.Num(); i++)
		{
			const FCompositeSection& Section = Montage->CompositeSections[i];
			FString NextSection = Section.NextSectionName.IsNone() ? TEXT("(end)") : Section.NextSectionName.ToString();
			Output += FString::Printf(TEXT("[%d] %s\t%.2fs\t-> %s\n"),
				i, *Section.SectionName.ToString(), Section.GetTime(), *NextSection);
		}
	}

	if (Montage->SlotAnimTracks.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Slot Tracks (%d)\n"), Montage->SlotAnimTracks.Num());
		for (const FSlotAnimationTrack& SlotTrack : Montage->SlotAnimTracks)
		{
			Output += FString::Printf(TEXT("### %s\n"), *SlotTrack.SlotName.ToString());
			for (int32 i = 0; i < SlotTrack.AnimTrack.AnimSegments.Num(); i++)
			{
				const FAnimSegment& Seg = SlotTrack.AnimTrack.AnimSegments[i];
				FString AnimName = Seg.GetAnimReference() ? Seg.GetAnimReference()->GetName() : TEXT("None");
				Output += FString::Printf(TEXT("Segment %d: %s (%.2fs - %.2fs) Rate=%.2f Loops=%d\n"),
					i, *AnimName, Seg.StartPos, Seg.StartPos + Seg.GetLength(),
					Seg.AnimPlayRate, Seg.LoopingCount);
			}
		}
	}

	if (Montage->Notifies.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Notifies (%d)\n"), Montage->Notifies.Num());
		for (const FAnimNotifyEvent& Notify : Montage->Notifies)
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
				NotifyName += FString::Printf(TEXT(" (%.2fs duration)"), Notify.GetDuration());
			}
			else
			{
				NotifyName = Notify.NotifyName.ToString();
			}

			FString TickType = (Notify.MontageTickType == EMontageNotifyTickType::BranchingPoint)
				? TEXT(" [BranchingPoint]") : TEXT("");

			Output += FString::Printf(TEXT("- [%.2fs] %s%s\n"), Notify.GetTriggerTime(), *NotifyName, *TickType);
		}
	}

	return FToolResult::Ok(Output);
}
