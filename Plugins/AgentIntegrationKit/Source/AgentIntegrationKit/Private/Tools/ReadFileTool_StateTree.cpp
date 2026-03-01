// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeTypes.h"
#include "StateTreeSchema.h"
#include "GameplayTagContainer.h"

namespace
{
	FString GetStateTypeString(EStateTreeStateType Type)
	{
		switch (Type)
		{
		case EStateTreeStateType::State: return TEXT("State");
		case EStateTreeStateType::Group: return TEXT("Group");
		case EStateTreeStateType::Linked: return TEXT("Linked");
		case EStateTreeStateType::LinkedAsset: return TEXT("LinkedAsset");
		case EStateTreeStateType::Subtree: return TEXT("Subtree");
		default: return TEXT("Unknown");
		}
	}

	FString GetSelectionBehaviorString(EStateTreeStateSelectionBehavior Behavior)
	{
		switch (Behavior)
		{
		case EStateTreeStateSelectionBehavior::None: return TEXT("None");
		case EStateTreeStateSelectionBehavior::TryEnterState: return TEXT("TryEnterState");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder: return TEXT("TrySelectChildrenInOrder");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom: return TEXT("TrySelectChildrenAtRandom");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility: return TEXT("TrySelectChildrenWithHighestUtility");
		case EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandomWeightedByUtility: return TEXT("TrySelectChildrenAtRandomWeightedByUtility");
		case EStateTreeStateSelectionBehavior::TryFollowTransitions: return TEXT("TryFollowTransitions");
		default: return TEXT("Unknown");
		}
	}

	FString GetTransitionTriggerString(EStateTreeTransitionTrigger Trigger)
	{
		TArray<FString> Triggers;
		if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnStateSucceeded))
		{
			Triggers.Add(TEXT("OnStateSucceeded"));
		}
		if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnStateFailed))
		{
			Triggers.Add(TEXT("OnStateFailed"));
		}
		if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnTick))
		{
			Triggers.Add(TEXT("OnTick"));
		}
		if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnEvent))
		{
			Triggers.Add(TEXT("OnEvent"));
		}
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
		if (EnumHasAnyFlags(Trigger, EStateTreeTransitionTrigger::OnDelegate))
		{
			Triggers.Add(TEXT("OnDelegate"));
		}
#endif

		if (Triggers.Num() == 0)
		{
			return TEXT("None");
		}
		return FString::Join(Triggers, TEXT("|"));
	}

	FString GetTransitionTypeString(EStateTreeTransitionType Type)
	{
		switch (Type)
		{
		case EStateTreeTransitionType::None: return TEXT("None");
		case EStateTreeTransitionType::Succeeded: return TEXT("Succeeded");
		case EStateTreeTransitionType::Failed: return TEXT("Failed");
		case EStateTreeTransitionType::GotoState: return TEXT("GotoState");
		case EStateTreeTransitionType::NextState: return TEXT("NextState");
		case EStateTreeTransitionType::NextSelectableState: return TEXT("NextSelectableState");
		default: return TEXT("Unknown");
		}
	}

	FString GetTransitionPriorityString(EStateTreeTransitionPriority Priority)
	{
		switch (Priority)
		{
		case EStateTreeTransitionPriority::None: return TEXT("None");
		case EStateTreeTransitionPriority::Low: return TEXT("Low");
		case EStateTreeTransitionPriority::Normal: return TEXT("Normal");
		case EStateTreeTransitionPriority::Medium: return TEXT("Medium");
		case EStateTreeTransitionPriority::High: return TEXT("High");
		case EStateTreeTransitionPriority::Critical: return TEXT("Critical");
		default: return TEXT("Unknown");
		}
	}

	FString GetNodeTypeName(const FStateTreeEditorNode& EditorNode)
	{
		if (!EditorNode.Node.IsValid())
		{
			return TEXT("(empty)");
		}

		const UScriptStruct* Struct = EditorNode.Node.GetScriptStruct();
		if (!Struct)
		{
			return TEXT("(unknown)");
		}

		FString TypeName = Struct->GetName();
		// Remove common prefixes for readability
		TypeName.RemoveFromStart(TEXT("StateTree"));
		TypeName.RemoveFromStart(TEXT("F"));
		return TypeName;
	}
}

FString FReadFileTool::GetStateTreeSummary(UStateTree* StateTree)
{
	if (!StateTree)
	{
		return TEXT("# STATE_TREE (null)\n");
	}

	FString Output = FString::Printf(TEXT("# STATE_TREE %s\n"), *StateTree->GetName());

	// Get editor data
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData)
	{
		Output += TEXT("(no editor data - asset may need recompilation)\n");
		return Output;
	}

	// Schema info
	if (EditorData->Schema)
	{
		Output += FString::Printf(TEXT("Schema: %s\n"), *EditorData->Schema->GetClass()->GetName());
	}
	else
	{
		Output += TEXT("Schema: None\n");
	}

	// Count states recursively
	int32 StateCount = 0;
	int32 TaskCount = 0;
	int32 TransitionCount = 0;
	int32 ConditionCount = 0;

	TFunction<void(const UStateTreeState*)> CountStates = [&](const UStateTreeState* State)
	{
		if (!State) return;

		StateCount++;
		TaskCount += State->Tasks.Num();
		if (State->SingleTask.Node.IsValid())
		{
			TaskCount++;
		}
		TransitionCount += State->Transitions.Num();
		ConditionCount += State->EnterConditions.Num();

		for (const FStateTreeTransition& Trans : State->Transitions)
		{
			ConditionCount += Trans.Conditions.Num();
		}

		for (const UStateTreeState* Child : State->Children)
		{
			CountStates(Child);
		}
	};

	for (const UStateTreeState* SubTree : EditorData->SubTrees)
	{
		CountStates(SubTree);
	}

	Output += FString::Printf(TEXT("States: %d\n"), StateCount);
	Output += FString::Printf(TEXT("Tasks: %d\n"), TaskCount);
	Output += FString::Printf(TEXT("Transitions: %d\n"), TransitionCount);
	Output += FString::Printf(TEXT("Conditions: %d\n"), ConditionCount);
	Output += FString::Printf(TEXT("Evaluators: %d\n"), EditorData->Evaluators.Num());
	Output += FString::Printf(TEXT("GlobalTasks: %d\n"), EditorData->GlobalTasks.Num());

	return Output;
}

FString FReadFileTool::GetStateTreeHierarchy(UStateTree* StateTree)
{
	if (!StateTree)
	{
		return TEXT("# STATES 0\n");
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData)
	{
		return TEXT("# STATES 0\n(no editor data)\n");
	}

	FString Output = TEXT("# STATES\n");

	for (const UStateTreeState* SubTree : EditorData->SubTrees)
	{
		Output += GetStateTreeStateHierarchy(SubTree, 0, EditorData);
	}

	return Output;
}

FString FReadFileTool::GetStateTreeStateHierarchy(const UStateTreeState* State, int32 Depth, UStateTreeEditorData* EditorData)
{
	if (!State)
	{
		return TEXT("");
	}

	FString Indent;
	for (int32 i = 0; i < Depth; i++)
	{
		Indent += TEXT("  ");
	}

	// State header: [Type] Name (SelectionBehavior)
	FString TypeStr = GetStateTypeString(State->Type);
	FString BehaviorStr = GetSelectionBehaviorString(State->SelectionBehavior);

	FString Output = FString::Printf(TEXT("%s[%s] %s (%s)"),
		*Indent, *TypeStr, *State->Name.ToString(), *BehaviorStr);

	// Add enabled status if disabled
	if (!State->bEnabled)
	{
		Output += TEXT(" [DISABLED]");
	}

	// Add tag if present
	if (State->Tag.IsValid())
	{
		Output += FString::Printf(TEXT(" tag=%s"), *State->Tag.ToString());
	}

	Output += TEXT("\n");

	// Enter conditions
	for (const FStateTreeEditorNode& Cond : State->EnterConditions)
	{
		FString CondType = GetNodeTypeName(Cond);
		Output += FString::Printf(TEXT("%s  ?%s\n"), *Indent, *CondType);
	}

	// Tasks
	for (const FStateTreeEditorNode& Task : State->Tasks)
	{
		FString TaskType = GetNodeTypeName(Task);
		Output += FString::Printf(TEXT("%s  <%s>\n"), *Indent, *TaskType);
	}

	// Single task (if schema uses single task mode)
	if (State->SingleTask.Node.IsValid())
	{
		FString TaskType = GetNodeTypeName(State->SingleTask);
		Output += FString::Printf(TEXT("%s  <%s>\n"), *Indent, *TaskType);
	}

	// Transitions
	for (const FStateTreeTransition& Trans : State->Transitions)
	{
		FString TriggerStr = GetTransitionTriggerString(Trans.Trigger);
		FString PriorityStr = GetTransitionPriorityString(Trans.Priority);

		FString TargetStr;
		if (Trans.State.LinkType == EStateTreeTransitionType::GotoState)
		{
			// Try to resolve target state name
			if (EditorData)
			{
				if (const UStateTreeState* TargetState = EditorData->GetStateByID(Trans.State.ID))
				{
					TargetStr = FString::Printf(TEXT("-> %s"), *TargetState->Name.ToString());
				}
				else
				{
					TargetStr = TEXT("-> (unresolved)");
				}
			}
			else
			{
				TargetStr = TEXT("-> (state)");
			}
		}
		else
		{
			TargetStr = FString::Printf(TEXT("-> %s"), *GetTransitionTypeString(Trans.State.LinkType));
		}

		Output += FString::Printf(TEXT("%s  ~%s %s [%s]\n"),
			*Indent, *TriggerStr, *TargetStr, *PriorityStr);

		// Transition conditions
		for (const FStateTreeEditorNode& Cond : Trans.Conditions)
		{
			FString CondType = GetNodeTypeName(Cond);
			Output += FString::Printf(TEXT("%s    ?%s\n"), *Indent, *CondType);
		}

		// Required event
		if (Trans.RequiredEvent.IsValid())
		{
			Output += FString::Printf(TEXT("%s    event=%s\n"), *Indent, *Trans.RequiredEvent.Tag.ToString());
		}
	}

	// Considerations (for utility-based selection)
	for (const FStateTreeEditorNode& Consideration : State->Considerations)
	{
		FString ConsType = GetNodeTypeName(Consideration);
		Output += FString::Printf(TEXT("%s  *%s\n"), *Indent, *ConsType);
	}

	// Recurse into children
	for (const UStateTreeState* Child : State->Children)
	{
		Output += GetStateTreeStateHierarchy(Child, Depth + 1, EditorData);
	}

	return Output;
}

FString FReadFileTool::GetStateTreeEvaluators(UStateTree* StateTree)
{
	if (!StateTree)
	{
		return TEXT("# EVALUATORS 0\n");
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData)
	{
		return TEXT("# EVALUATORS 0\n(no editor data)\n");
	}

	if (EditorData->Evaluators.Num() == 0)
	{
		return TEXT("# EVALUATORS 0\n");
	}

	FString Output = FString::Printf(TEXT("# EVALUATORS %d\n"), EditorData->Evaluators.Num());

	for (const FStateTreeEditorNode& Eval : EditorData->Evaluators)
	{
		FString EvalType = GetNodeTypeName(Eval);
		Output += FString::Printf(TEXT("  %s\n"), *EvalType);

		// Show instance data struct if available
		if (Eval.Instance.IsValid())
		{
			const UScriptStruct* InstanceStruct = Eval.Instance.GetScriptStruct();
			if (InstanceStruct)
			{
				Output += FString::Printf(TEXT("    instance: %s\n"), *InstanceStruct->GetName());
			}
		}
	}

	return Output;
}

FString FReadFileTool::GetStateTreeGlobalTasks(UStateTree* StateTree)
{
	if (!StateTree)
	{
		return TEXT("# GLOBAL_TASKS 0\n");
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData)
	{
		return TEXT("# GLOBAL_TASKS 0\n(no editor data)\n");
	}

	if (EditorData->GlobalTasks.Num() == 0)
	{
		return TEXT("# GLOBAL_TASKS 0\n");
	}

	FString Output = FString::Printf(TEXT("# GLOBAL_TASKS %d\n"), EditorData->GlobalTasks.Num());

	for (const FStateTreeEditorNode& Task : EditorData->GlobalTasks)
	{
		FString TaskType = GetNodeTypeName(Task);
		Output += FString::Printf(TEXT("  <%s>\n"), *TaskType);

		// Show instance data struct if available
		if (Task.Instance.IsValid())
		{
			const UScriptStruct* InstanceStruct = Task.Instance.GetScriptStruct();
			if (InstanceStruct)
			{
				Output += FString::Printf(TEXT("    instance: %s\n"), *InstanceStruct->GetName());
			}
		}
	}

	return Output;
}

FString FReadFileTool::GetStateTreeTransitions(UStateTree* StateTree)
{
	if (!StateTree)
	{
		return TEXT("# TRANSITIONS 0\n");
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData)
	{
		return TEXT("# TRANSITIONS 0\n(no editor data)\n");
	}

	FString Output = TEXT("# TRANSITIONS\n");

	TFunction<void(const UStateTreeState*, const FString&)> CollectTransitions = [&](const UStateTreeState* State, const FString& ParentPath)
	{
		if (!State) return;

		FString StatePath = ParentPath.IsEmpty() ? State->Name.ToString() : FString::Printf(TEXT("%s/%s"), *ParentPath, *State->Name.ToString());

		for (const FStateTreeTransition& Trans : State->Transitions)
		{
			FString TriggerStr = GetTransitionTriggerString(Trans.Trigger);
			FString PriorityStr = GetTransitionPriorityString(Trans.Priority);

			FString TargetStr;
			if (Trans.State.LinkType == EStateTreeTransitionType::GotoState)
			{
				if (const UStateTreeState* TargetState = EditorData->GetStateByID(Trans.State.ID))
				{
					TargetStr = TargetState->Name.ToString();
				}
				else
				{
					TargetStr = TEXT("(unresolved)");
				}
			}
			else
			{
				TargetStr = GetTransitionTypeString(Trans.State.LinkType);
			}

			Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\tconditions=%d"),
				*StatePath, *TriggerStr, *TargetStr, *PriorityStr, Trans.Conditions.Num());

			if (Trans.bDelayTransition)
			{
				Output += FString::Printf(TEXT("\tdelay=%.2fs"), Trans.DelayDuration);
			}

			if (!Trans.bTransitionEnabled)
			{
				Output += TEXT("\t[DISABLED]");
			}

			Output += TEXT("\n");
		}

		for (const UStateTreeState* Child : State->Children)
		{
			CollectTransitions(Child, StatePath);
		}
	};

	for (const UStateTreeState* SubTree : EditorData->SubTrees)
	{
		CollectTransitions(SubTree, TEXT(""));
	}

	return Output;
}

FString FReadFileTool::GetStateTreeSchema(UStateTree* StateTree)
{
	if (!StateTree)
	{
		return TEXT("# SCHEMA (none)\n");
	}

	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData || !EditorData->Schema)
	{
		return TEXT("# SCHEMA (none)\n");
	}

	UStateTreeSchema* Schema = EditorData->Schema;
	FString Output = FString::Printf(TEXT("# SCHEMA %s\n"), *Schema->GetClass()->GetName());

	// Get context data descriptors
	TConstArrayView<FStateTreeExternalDataDesc> ContextData = Schema->GetContextDataDescs();
	if (ContextData.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Context Data (%d)\n"), ContextData.Num());
		for (const FStateTreeExternalDataDesc& Desc : ContextData)
		{
			FString TypeName = Desc.Struct ? Desc.Struct->GetName() : TEXT("(unknown)");
			FString Requirement = Desc.Requirement == EStateTreeExternalDataRequirement::Required ? TEXT("Required") : TEXT("Optional");
			Output += FString::Printf(TEXT("  %s\t%s\t%s\n"), *Desc.Name.ToString(), *TypeName, *Requirement);
		}
	}

	return Output;
}

FToolResult FReadFileTool::ReadStateTree(UStateTree* StateTree, const TArray<FString>& Include)
{
	if (!StateTree)
	{
		return FToolResult::Fail(TEXT("StateTree is null"));
	}

	FString Output;

	// Default to summary if no include specified
	TArray<FString> Sections = Include;
	if (Sections.Num() == 0)
	{
		Sections.Add(TEXT("summary"));
	}

	// Build output based on requested sections
	if (Sections.Contains(TEXT("summary")))
	{
		Output += GetStateTreeSummary(StateTree);
	}

	if (Sections.Contains(TEXT("states")) || Sections.Contains(TEXT("hierarchy")) || Sections.Contains(TEXT("tree")))
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		Output += GetStateTreeHierarchy(StateTree);
	}

	if (Sections.Contains(TEXT("evaluators")))
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		Output += GetStateTreeEvaluators(StateTree);
	}

	if (Sections.Contains(TEXT("globaltasks")) || Sections.Contains(TEXT("global_tasks")))
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		Output += GetStateTreeGlobalTasks(StateTree);
	}

	if (Sections.Contains(TEXT("transitions")))
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		Output += GetStateTreeTransitions(StateTree);
	}

	if (Sections.Contains(TEXT("schema")))
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		Output += GetStateTreeSchema(StateTree);
	}

	if (Output.IsEmpty())
	{
		Output = FString::Printf(TEXT("# STATE_TREE %s (no data for requested sections)\n"), *StateTree->GetName());
	}

	return FToolResult::Ok(Output);
}
