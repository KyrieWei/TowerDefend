// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"
#include "StateTreeTypes.h"

class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
class UStateTreeSchema;
struct FStateTreeEditorNode;
#if WITH_STRUCT_UTILS
struct FInstancedPropertyBag;
enum class EPropertyBagPropertyType : uint8;
#endif

/**
 * Tool for editing StateTree assets:
 * - Add/remove states (hierarchical state machine nodes)
 * - Add/remove tasks (state behavior)
 * - Add/remove evaluators (global data calculators)
 * - Add/remove global tasks
 * - Add/remove transitions
 * - Add/remove enter conditions and transition conditions
 * - Add/remove considerations (utility AI)
 * - Set properties on nodes
 * - Add/remove property bindings (wire data between nodes)
 * - Add/remove parameters (state or global)
 * - Set schema
 */
class AGENTINTEGRATIONKIT_API FEditStateTreeTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_state_tree"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit StateTree assets: add/remove states, tasks, evaluators, transitions, conditions, considerations, property bindings, and parameters. "
			"States form a hierarchical state machine. Tasks define behavior when a state is active. "
			"Transitions define how states change based on triggers (OnStateCompleted, OnTick, OnEvent). "
			"Property bindings wire data between nodes. Parameters define state or global data. "
			"Use set_properties to configure node values after adding them.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// ========== Definitions ==========

	/** State definition */
	struct FStateDefinition
	{
		FString Name;                      // State name (required)
		FString Parent;                    // Parent state name (empty = root/subtree)
		FString Type;                      // State, Group, Linked, LinkedAsset, Subtree
		FString SelectionBehavior;         // TryEnterState, TrySelectChildrenInOrder, etc.
		FString Tag;                       // Optional GameplayTag
		bool bEnabled = true;
	};

	/** Task definition */
	struct FTaskDefinition
	{
		FString State;                     // State to add task to (required)
		FString Type;                      // Task struct type (e.g., StateTreeDelayTask)
	};

	/** Evaluator definition */
	struct FEvaluatorDefinition
	{
		FString Type;                      // Evaluator struct type
	};

	/** Global task definition */
	struct FGlobalTaskDefinition
	{
		FString Type;                      // Task struct type
	};

	/** Transition definition */
	struct FTransitionDefinition
	{
		FString State;                     // State the transition is from (required)
		FString Trigger;                   // OnStateCompleted, OnStateSucceeded, OnStateFailed, OnTick, OnEvent
		FString Target;                    // Target state name, or "Succeeded", "Failed", "NextState"
		FString Priority;                  // Low, Normal, Medium, High, Critical
		FString EventTag;                  // GameplayTag for OnEvent trigger
		float DelayDuration = 0.0f;        // Optional delay
	};

	/** Enter condition definition */
	struct FEnterConditionDefinition
	{
		FString State;                     // State to add condition to
		FString Type;                      // Condition struct type
	};

	// ========== Find Helpers ==========

	/** Find a state by name in the hierarchy */
	UStateTreeState* FindStateByName(UStateTreeEditorData* EditorData, const FString& Name);

	/** Find a state by name recursively */
	UStateTreeState* FindStateByNameRecursive(UStateTreeState* State, const FString& Name);

	/** Find a task/evaluator/condition struct by type name */
	const UScriptStruct* FindNodeStructByName(const FString& TypeName, const FString& BaseStructName);

	/** Parse state type from string */
	EStateTreeStateType ParseStateType(const FString& TypeStr);

	/** Parse selection behavior from string */
	EStateTreeStateSelectionBehavior ParseSelectionBehavior(const FString& BehaviorStr);

	/** Parse transition trigger from string */
	EStateTreeTransitionTrigger ParseTransitionTrigger(const FString& TriggerStr);

	/** Parse transition type from string */
	EStateTreeTransitionType ParseTransitionType(const FString& TypeStr);

	/** Parse transition priority from string */
	EStateTreeTransitionPriority ParseTransitionPriority(const FString& PriorityStr);

	/** Find an editor node by state name, node type, and index */
	FStateTreeEditorNode* FindEditorNode(UStateTreeEditorData* EditorData, const FString& StateName, const FString& NodeType, int32 Index);

#if WITH_STRUCT_UTILS
	/** Parse property bag type from string */
	EPropertyBagPropertyType ParsePropertyBagType(const FString& TypeStr);

	/** Set a value on a property bag from a string */
	void SetParameterValue(FInstancedPropertyBag* Bag, const FString& ParamName, EPropertyBagPropertyType Type, const FString& Value);

	/** Get a mutable reference to a property bag for the given state (or global if state empty) */
	FInstancedPropertyBag* GetParameterBag(UStateTreeEditorData* EditorData, const FString& StateName, FString& OutError);
#endif // WITH_STRUCT_UTILS

	// ========== State Operations ==========

	/** Add a state to the StateTree */
	FString AddState(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FStateDefinition& StateDef);

	/** Remove a state from the StateTree */
	FString RemoveState(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName);

	// ========== Task Operations ==========

	/** Add a task to a state */
	FString AddTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FTaskDefinition& TaskDef);

	/** Remove a task from a state (by index) */
	FString RemoveTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TaskIndex);

	// ========== Evaluator Operations ==========

	/** Add a global evaluator */
	FString AddEvaluator(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FEvaluatorDefinition& EvalDef);

	/** Remove a global evaluator by index */
	FString RemoveEvaluator(UStateTree* StateTree, UStateTreeEditorData* EditorData, int32 Index);

	// ========== Global Task Operations ==========

	/** Add a global task */
	FString AddGlobalTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FGlobalTaskDefinition& TaskDef);

	/** Remove a global task by index */
	FString RemoveGlobalTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, int32 Index);

	// ========== Transition Operations ==========

	/** Add a transition to a state */
	FString AddTransition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FTransitionDefinition& TransDef);

	/** Remove a transition from a state by index */
	FString RemoveTransition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TransitionIndex);

	// ========== Condition Operations ==========

	/** Add an enter condition to a state */
	FString AddEnterCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FEnterConditionDefinition& CondDef);

	/** Remove an enter condition from a state by index */
	FString RemoveEnterCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 Index);

	/** Add a condition to a transition */
	FString AddTransitionCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TransitionIndex, const FString& CondType);

	/** Remove a condition from a transition */
	FString RemoveTransitionCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TransitionIndex, int32 ConditionIndex);

	// ========== Consideration Operations ==========

	/** Add a utility consideration to a state */
	FString AddConsideration(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, const FString& Type);

	/** Remove a consideration from a state by index */
	FString RemoveConsideration(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 Index);

	// ========== Property Binding Operations ==========

	/** Add a property binding between two nodes */
	FString AddPropertyBinding(UStateTree* StateTree, UStateTreeEditorData* EditorData,
		const FString& SourceState, const FString& SourceNodeType, int32 SourceIndex, const FString& SourceProperty,
		const FString& TargetState, const FString& TargetNodeType, int32 TargetIndex, const FString& TargetProperty);

	/** Remove a property binding by target node and property path */
	FString RemovePropertyBinding(UStateTree* StateTree, UStateTreeEditorData* EditorData,
		const FString& StateName, const FString& NodeType, int32 Index, const FString& PropertyPath);

	// ========== Parameter Operations ==========
#if WITH_STRUCT_UTILS
	/** Add a parameter to a state or globally */
	FString AddParameter(UStateTree* StateTree, UStateTreeEditorData* EditorData,
		const FString& StateName, const FString& ParamName, const FString& TypeStr, const FString& Value);

	/** Remove a parameter from a state or globally */
	FString RemoveParameter(UStateTree* StateTree, UStateTreeEditorData* EditorData,
		const FString& StateName, const FString& ParamName);
#endif // WITH_STRUCT_UTILS

	// ========== Property Operations ==========

	/** Set properties on a StateTree editor node (task, condition, evaluator, global_task, consideration) */
	FString SetNodeProperties(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, const FString& NodeType, int32 Index, const TSharedPtr<FJsonObject>& Properties);

	// ========== Schema Operations ==========

	/** Set the StateTree schema */
	FString SetSchema(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& SchemaClassName);

	// ========== Compilation ==========

	/** Compile the StateTree after modifications, returns error messages */
	bool CompileStateTree(UStateTree* StateTree, TArray<FString>& OutErrors);

	// ========== Discovery ==========

	/** List all available task types */
	FString ListAvailableTaskTypes(const UStateTreeSchema* Schema);

	/** List all available evaluator types */
	FString ListAvailableEvaluatorTypes(const UStateTreeSchema* Schema);

	/** List all available condition types */
	FString ListAvailableConditionTypes(const UStateTreeSchema* Schema);

	/** List all available consideration types */
	FString ListAvailableConsiderationTypes(const UStateTreeSchema* Schema);

	/** List all available schema types */
	FString ListAvailableSchemas();
};
