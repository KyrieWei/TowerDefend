// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditStateTreeTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// StateTree
#include "StateTree.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "StateTreeTypes.h"
#include "StateTreeSchema.h"
#include "StateTreeEditorNode.h"
#include "StateTreeTaskBase.h"
#include "StateTreeEvaluatorBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeConsiderationBase.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeCompiler.h"
#include "StateTreeCompilerLog.h"
#include "Logging/TokenizedMessage.h"

// Asset creation
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Factories/Factory.h"
#include "StateTreeFactory.h"

// Asset utilities
#include "UObject/UObjectIterator.h"
#include "AssetRegistry/AssetRegistryModule.h"

// Property bindings
#include "StateTreeEditorPropertyBindings.h"
#include "PropertyBindingPath.h"

// Property bags (for parameters) — optional, not available in all engine builds
#if WITH_STRUCT_UTILS
#include "StructUtils/PropertyBag.h"
#endif

// Transaction support for undo/redo
#include "ScopedTransaction.h"

// Editor
#include "Editor.h"

TSharedPtr<FJsonObject> FEditStateTreeTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Asset name
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("StateTree asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// Asset creation
	TSharedPtr<FJsonObject> CreateProp = MakeShared<FJsonObject>();
	CreateProp->SetStringField(TEXT("type"), TEXT("boolean"));
	CreateProp->SetStringField(TEXT("description"), TEXT("Create the StateTree asset if it doesn't exist (default: false)"));
	Properties->SetObjectField(TEXT("create"), CreateProp);

	TSharedPtr<FJsonObject> SchemaProp = MakeShared<FJsonObject>();
	SchemaProp->SetStringField(TEXT("type"), TEXT("string"));
	SchemaProp->SetStringField(TEXT("description"), TEXT("Schema class for new assets (default: StateTreeComponentSchema). Options: StateTreeComponentSchema, StateTreeAIComponentSchema"));
	Properties->SetObjectField(TEXT("schema"), SchemaProp);

	// Discovery - list available types
	TSharedPtr<FJsonObject> ListTypesProp = MakeShared<FJsonObject>();
	ListTypesProp->SetStringField(TEXT("type"), TEXT("string"));
	ListTypesProp->SetStringField(TEXT("description"), TEXT("List available node types: 'tasks', 'evaluators', 'conditions', 'considerations', 'schemas', or 'all'. Returns types compatible with the StateTree's schema."));
	Properties->SetObjectField(TEXT("list_types"), ListTypesProp);

	// State operations
	TSharedPtr<FJsonObject> AddStateProp = MakeShared<FJsonObject>();
	AddStateProp->SetStringField(TEXT("type"), TEXT("array"));
	AddStateProp->SetStringField(TEXT("description"), TEXT("States to add: [{name, parent, type (State/Group/Subtree), selection_behavior (TryEnterState/TrySelectChildrenInOrder/etc), tag, enabled}]"));
	Properties->SetObjectField(TEXT("add_state"), AddStateProp);

	TSharedPtr<FJsonObject> RemoveStateProp = MakeShared<FJsonObject>();
	RemoveStateProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveStateProp->SetStringField(TEXT("description"), TEXT("State names to remove"));
	Properties->SetObjectField(TEXT("remove_state"), RemoveStateProp);

	// Task operations
	TSharedPtr<FJsonObject> AddTaskProp = MakeShared<FJsonObject>();
	AddTaskProp->SetStringField(TEXT("type"), TEXT("array"));
	AddTaskProp->SetStringField(TEXT("description"), TEXT("Tasks to add to states: [{state, type (e.g., StateTreeDelayTask, StateTreeDebugTextTask)}]"));
	Properties->SetObjectField(TEXT("add_task"), AddTaskProp);

	TSharedPtr<FJsonObject> RemoveTaskProp = MakeShared<FJsonObject>();
	RemoveTaskProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveTaskProp->SetStringField(TEXT("description"), TEXT("Tasks to remove: [{state, index}]"));
	Properties->SetObjectField(TEXT("remove_task"), RemoveTaskProp);

	// Evaluator operations
	TSharedPtr<FJsonObject> AddEvaluatorProp = MakeShared<FJsonObject>();
	AddEvaluatorProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEvaluatorProp->SetStringField(TEXT("description"), TEXT("Global evaluators to add: [{type}]"));
	Properties->SetObjectField(TEXT("add_evaluator"), AddEvaluatorProp);

	TSharedPtr<FJsonObject> RemoveEvaluatorProp = MakeShared<FJsonObject>();
	RemoveEvaluatorProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveEvaluatorProp->SetStringField(TEXT("description"), TEXT("Evaluator indices to remove"));
	Properties->SetObjectField(TEXT("remove_evaluator"), RemoveEvaluatorProp);

	// Global task operations
	TSharedPtr<FJsonObject> AddGlobalTaskProp = MakeShared<FJsonObject>();
	AddGlobalTaskProp->SetStringField(TEXT("type"), TEXT("array"));
	AddGlobalTaskProp->SetStringField(TEXT("description"), TEXT("Global tasks to add: [{type}]"));
	Properties->SetObjectField(TEXT("add_global_task"), AddGlobalTaskProp);

	TSharedPtr<FJsonObject> RemoveGlobalTaskProp = MakeShared<FJsonObject>();
	RemoveGlobalTaskProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveGlobalTaskProp->SetStringField(TEXT("description"), TEXT("Global task indices to remove"));
	Properties->SetObjectField(TEXT("remove_global_task"), RemoveGlobalTaskProp);

	// Transition operations
	TSharedPtr<FJsonObject> AddTransitionProp = MakeShared<FJsonObject>();
	AddTransitionProp->SetStringField(TEXT("type"), TEXT("array"));
	AddTransitionProp->SetStringField(TEXT("description"), TEXT("Transitions to add: [{state, trigger (OnStateCompleted/OnTick/OnEvent), target (state name or Succeeded/Failed/NextState), priority (Low/Normal/High/Critical), event_tag, delay}]"));
	Properties->SetObjectField(TEXT("add_transition"), AddTransitionProp);

	TSharedPtr<FJsonObject> RemoveTransitionProp = MakeShared<FJsonObject>();
	RemoveTransitionProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveTransitionProp->SetStringField(TEXT("description"), TEXT("Transitions to remove: [{state, index}]"));
	Properties->SetObjectField(TEXT("remove_transition"), RemoveTransitionProp);

	// Enter condition operations
	TSharedPtr<FJsonObject> AddEnterConditionProp = MakeShared<FJsonObject>();
	AddEnterConditionProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEnterConditionProp->SetStringField(TEXT("description"), TEXT("Enter conditions to add: [{state, type (e.g., StateTreeCompareBoolCondition)}]"));
	Properties->SetObjectField(TEXT("add_enter_condition"), AddEnterConditionProp);

	TSharedPtr<FJsonObject> RemoveEnterConditionProp = MakeShared<FJsonObject>();
	RemoveEnterConditionProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveEnterConditionProp->SetStringField(TEXT("description"), TEXT("Remove enter conditions: [{state, index}]"));
	Properties->SetObjectField(TEXT("remove_enter_condition"), RemoveEnterConditionProp);

	// Transition condition operations
	TSharedPtr<FJsonObject> AddTransitionConditionProp = MakeShared<FJsonObject>();
	AddTransitionConditionProp->SetStringField(TEXT("type"), TEXT("array"));
	AddTransitionConditionProp->SetStringField(TEXT("description"), TEXT("Add conditions to transitions: [{state, transition_index, type (condition struct name)}]"));
	Properties->SetObjectField(TEXT("add_transition_condition"), AddTransitionConditionProp);

	TSharedPtr<FJsonObject> RemoveTransitionConditionProp = MakeShared<FJsonObject>();
	RemoveTransitionConditionProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveTransitionConditionProp->SetStringField(TEXT("description"), TEXT("Remove transition conditions: [{state, transition_index, condition_index}]"));
	Properties->SetObjectField(TEXT("remove_transition_condition"), RemoveTransitionConditionProp);

	// Consideration operations
	TSharedPtr<FJsonObject> AddConsiderationProp = MakeShared<FJsonObject>();
	AddConsiderationProp->SetStringField(TEXT("type"), TEXT("array"));
	AddConsiderationProp->SetStringField(TEXT("description"), TEXT("Add utility considerations to states: [{state, type (consideration struct name)}]"));
	Properties->SetObjectField(TEXT("add_consideration"), AddConsiderationProp);

	TSharedPtr<FJsonObject> RemoveConsiderationProp = MakeShared<FJsonObject>();
	RemoveConsiderationProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveConsiderationProp->SetStringField(TEXT("description"), TEXT("Remove considerations: [{state, index}]"));
	Properties->SetObjectField(TEXT("remove_consideration"), RemoveConsiderationProp);

	// Property binding operations
	TSharedPtr<FJsonObject> AddBindingProp = MakeShared<FJsonObject>();
	AddBindingProp->SetStringField(TEXT("type"), TEXT("array"));
	AddBindingProp->SetStringField(TEXT("description"),
		TEXT("Add property bindings between nodes: [{source_state, source_node_type (task/evaluator/condition/global_task/consideration), source_index, source_property (dot path e.g. 'Duration'), target_state, target_node_type, target_index, target_property}]. "
			 "Bindings wire data from source node output to target node input. Use read_asset to see available properties on nodes."));
	Properties->SetObjectField(TEXT("add_property_binding"), AddBindingProp);

	TSharedPtr<FJsonObject> RemoveBindingProp = MakeShared<FJsonObject>();
	RemoveBindingProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveBindingProp->SetStringField(TEXT("description"), TEXT("Remove property bindings: [{state, node_type, index, property (target property path to unbind)}]"));
	Properties->SetObjectField(TEXT("remove_property_binding"), RemoveBindingProp);

	// Parameter operations
	TSharedPtr<FJsonObject> AddParamProp = MakeShared<FJsonObject>();
	AddParamProp->SetStringField(TEXT("type"), TEXT("array"));
	AddParamProp->SetStringField(TEXT("description"),
		TEXT("Add parameters: [{state (state name, empty for global), name, type (Bool/Int32/Float/Double/String/Name/Byte/Int64/UInt32/UInt64/Text), value (optional initial value as string)}]"));
	Properties->SetObjectField(TEXT("add_parameter"), AddParamProp);

	TSharedPtr<FJsonObject> RemoveParamProp = MakeShared<FJsonObject>();
	RemoveParamProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveParamProp->SetStringField(TEXT("description"), TEXT("Remove parameters: [{state (state name, empty for global), name}]"));
	Properties->SetObjectField(TEXT("remove_parameter"), RemoveParamProp);

	// Property operations
	TSharedPtr<FJsonObject> SetPropertiesProp = MakeShared<FJsonObject>();
	SetPropertiesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetPropertiesProp->SetStringField(TEXT("description"), TEXT("Set properties on StateTree nodes: [{state (state name), node_type (task/condition/evaluator/global_task/consideration), index (0-based), properties: {name: value, ...}}]. Properties can be on the node itself or its instance data. Use read_asset to see available properties."));
	Properties->SetObjectField(TEXT("set_properties"), SetPropertiesProp);

	// Schema
	TSharedPtr<FJsonObject> SetSchemaProp = MakeShared<FJsonObject>();
	SetSchemaProp->SetStringField(TEXT("type"), TEXT("string"));
	SetSchemaProp->SetStringField(TEXT("description"), TEXT("Schema class name to set (e.g., StateTreeComponentSchema, StateTreeAIComponentSchema)"));
	Properties->SetObjectField(TEXT("set_schema"), SetSchemaProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	// No required fields - name is only required for edit operations, not for list_types
	TArray<TSharedPtr<FJsonValue>> Required;
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditStateTreeTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path;
	Args->TryGetStringField(TEXT("name"), Name);
	Args->TryGetStringField(TEXT("path"), Path);

	// Handle list_types request (may not need a StateTree)
	FString ListTypes;
	if (Args->TryGetStringField(TEXT("list_types"), ListTypes) && !ListTypes.IsEmpty())
	{
		// For 'schemas', we don't need a StateTree
		if (ListTypes.Equals(TEXT("schemas"), ESearchCase::IgnoreCase))
		{
			return FToolResult::Ok(ListAvailableSchemas());
		}

		// For other types, we may want to filter by schema if a StateTree is specified
		const UStateTreeSchema* Schema = nullptr;
		if (!Name.IsEmpty())
		{
			FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
			if (UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *FullAssetPath))
			{
				if (UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get()))
				{
					Schema = EditorData->Schema;
				}
			}
		}

		FString Output;
		if (ListTypes.Equals(TEXT("tasks"), ESearchCase::IgnoreCase))
		{
			Output = ListAvailableTaskTypes(Schema);
		}
		else if (ListTypes.Equals(TEXT("evaluators"), ESearchCase::IgnoreCase))
		{
			Output = ListAvailableEvaluatorTypes(Schema);
		}
		else if (ListTypes.Equals(TEXT("conditions"), ESearchCase::IgnoreCase))
		{
			Output = ListAvailableConditionTypes(Schema);
		}
		else if (ListTypes.Equals(TEXT("considerations"), ESearchCase::IgnoreCase))
		{
			Output = ListAvailableConsiderationTypes(Schema);
		}
		else if (ListTypes.Equals(TEXT("all"), ESearchCase::IgnoreCase))
		{
			Output = TEXT("# AVAILABLE STATETREE NODE TYPES\n\n");
			Output += TEXT("## TASKS\n") + ListAvailableTaskTypes(Schema) + TEXT("\n");
			Output += TEXT("## EVALUATORS\n") + ListAvailableEvaluatorTypes(Schema) + TEXT("\n");
			Output += TEXT("## CONDITIONS\n") + ListAvailableConditionTypes(Schema) + TEXT("\n");
			Output += TEXT("## CONSIDERATIONS\n") + ListAvailableConsiderationTypes(Schema) + TEXT("\n");
			Output += TEXT("## SCHEMAS\n") + ListAvailableSchemas();
		}
		else
		{
			return FToolResult::Fail(FString::Printf(TEXT("Unknown list_types value: %s. Use 'tasks', 'evaluators', 'conditions', 'considerations', 'schemas', or 'all'."), *ListTypes));
		}

		return FToolResult::Ok(Output);
	}

	// For edit operations, name is required
	if (Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	bool bCreate = false;
	Args->TryGetBoolField(TEXT("create"), bCreate);

	FString SchemaName;
	Args->TryGetStringField(TEXT("schema"), SchemaName);

	// Build asset path and load
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UStateTree* StateTree = LoadObject<UStateTree>(nullptr, *FullAssetPath);

	bool bWasCreated = false;

	// Create asset if it doesn't exist and create flag is set
	if (!StateTree && bCreate)
	{
		// Determine package path and asset name
		FString PackagePath = Path;
		if (PackagePath.IsEmpty())
		{
			PackagePath = TEXT("/Game");
		}
		else if (!PackagePath.StartsWith(TEXT("/")))
		{
			PackagePath = TEXT("/Game/") + PackagePath;
		}

		FString AssetName = Name;
		// Remove path components from name if present
		int32 LastSlash;
		if (AssetName.FindLastChar('/', LastSlash))
		{
			AssetName = AssetName.RightChop(LastSlash + 1);
		}

		// Create the StateTree asset using AssetTools
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		// Find or create the StateTree factory
		UStateTreeFactory* Factory = NewObject<UStateTreeFactory>();

		// Set schema class on factory if specified
		if (!SchemaName.IsEmpty())
		{
			FString SearchName = SchemaName;
			if (!SearchName.StartsWith(TEXT("U")))
			{
				SearchName = TEXT("U") + SearchName;
			}

			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UStateTreeSchema::StaticClass()) &&
					!It->HasAnyClassFlags(CLASS_Abstract))
				{
					if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
						It->GetName().Equals(SchemaName, ESearchCase::IgnoreCase))
					{
						Factory->SetSchemaClass(*It);
						break;
					}
				}
			}
		}
		else
		{
			// Default to StateTreeComponentSchema if available (from GameplayStateTree plugin)
			for (TObjectIterator<UClass> It; It; ++It)
			{
				if (It->IsChildOf(UStateTreeSchema::StaticClass()) &&
					!It->HasAnyClassFlags(CLASS_Abstract) &&
					It->GetName().Equals(TEXT("UStateTreeComponentSchema"), ESearchCase::IgnoreCase))
				{
					Factory->SetSchemaClass(*It);
					break;
				}
			}
		}

		UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UStateTree::StaticClass(), Factory);
		StateTree = Cast<UStateTree>(NewAsset);

		if (StateTree)
		{
			bWasCreated = true;
		}
		else
		{
			return FToolResult::Fail(FString::Printf(TEXT("Failed to create StateTree asset at %s/%s"), *PackagePath, *AssetName));
		}
	}

	if (!StateTree)
	{
		return FToolResult::Fail(FString::Printf(TEXT("StateTree asset not found: %s (use create:true to create it)"), *FullAssetPath));
	}

	// Get editor data
	UStateTreeEditorData* EditorData = Cast<UStateTreeEditorData>(StateTree->EditorData.Get());
	if (!EditorData)
	{
		return FToolResult::Fail(TEXT("StateTree has no editor data. Asset may need to be opened in editor first."));
	}

	// Create transaction for undo/redo support
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditStateTree", "AI Edit StateTree: {0}"),
		FText::FromString(Name)));

	TArray<FString> Results;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;

	// ========== State Operations ==========

	// Process add_state
	const TArray<TSharedPtr<FJsonValue>>* AddStates;
	if (Args->TryGetArrayField(TEXT("add_state"), AddStates))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddStates)
		{
			const TSharedPtr<FJsonObject>* StateObj;
			if (Value->TryGetObject(StateObj))
			{
				FStateDefinition StateDef;
				(*StateObj)->TryGetStringField(TEXT("name"), StateDef.Name);
				(*StateObj)->TryGetStringField(TEXT("parent"), StateDef.Parent);
				(*StateObj)->TryGetStringField(TEXT("type"), StateDef.Type);
				(*StateObj)->TryGetStringField(TEXT("selection_behavior"), StateDef.SelectionBehavior);
				(*StateObj)->TryGetStringField(TEXT("tag"), StateDef.Tag);
				(*StateObj)->TryGetBoolField(TEXT("enabled"), StateDef.bEnabled);

				FString Result = AddState(StateTree, EditorData, StateDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_state
	const TArray<TSharedPtr<FJsonValue>>* RemoveStates;
	if (Args->TryGetArrayField(TEXT("remove_state"), RemoveStates))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveStates)
		{
			FString StateName;
			if (Value->TryGetString(StateName))
			{
				FString Result = RemoveState(StateTree, EditorData, StateName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Task Operations ==========

	// Process add_task
	const TArray<TSharedPtr<FJsonValue>>* AddTasks;
	if (Args->TryGetArrayField(TEXT("add_task"), AddTasks))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddTasks)
		{
			const TSharedPtr<FJsonObject>* TaskObj;
			if (Value->TryGetObject(TaskObj))
			{
				FTaskDefinition TaskDef;
				(*TaskObj)->TryGetStringField(TEXT("state"), TaskDef.State);
				(*TaskObj)->TryGetStringField(TEXT("type"), TaskDef.Type);

				FString Result = AddTask(StateTree, EditorData, TaskDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_task
	const TArray<TSharedPtr<FJsonValue>>* RemoveTasks;
	if (Args->TryGetArrayField(TEXT("remove_task"), RemoveTasks))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveTasks)
		{
			const TSharedPtr<FJsonObject>* TaskObj;
			if (Value->TryGetObject(TaskObj))
			{
				FString StateName;
				int32 TaskIndex = -1;
				(*TaskObj)->TryGetStringField(TEXT("state"), StateName);
				(*TaskObj)->TryGetNumberField(TEXT("index"), TaskIndex);

				FString Result = RemoveTask(StateTree, EditorData, StateName, TaskIndex);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Evaluator Operations ==========

	// Process add_evaluator
	const TArray<TSharedPtr<FJsonValue>>* AddEvaluators;
	if (Args->TryGetArrayField(TEXT("add_evaluator"), AddEvaluators))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddEvaluators)
		{
			const TSharedPtr<FJsonObject>* EvalObj;
			if (Value->TryGetObject(EvalObj))
			{
				FEvaluatorDefinition EvalDef;
				(*EvalObj)->TryGetStringField(TEXT("type"), EvalDef.Type);

				FString Result = AddEvaluator(StateTree, EditorData, EvalDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_evaluator
	const TArray<TSharedPtr<FJsonValue>>* RemoveEvaluators;
	if (Args->TryGetArrayField(TEXT("remove_evaluator"), RemoveEvaluators))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveEvaluators)
		{
			int32 Index = -1;
			if (Value->TryGetNumber(Index))
			{
				FString Result = RemoveEvaluator(StateTree, EditorData, Index);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Global Task Operations ==========

	// Process add_global_task
	const TArray<TSharedPtr<FJsonValue>>* AddGlobalTasks;
	if (Args->TryGetArrayField(TEXT("add_global_task"), AddGlobalTasks))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddGlobalTasks)
		{
			const TSharedPtr<FJsonObject>* TaskObj;
			if (Value->TryGetObject(TaskObj))
			{
				FGlobalTaskDefinition TaskDef;
				(*TaskObj)->TryGetStringField(TEXT("type"), TaskDef.Type);

				FString Result = AddGlobalTask(StateTree, EditorData, TaskDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_global_task
	const TArray<TSharedPtr<FJsonValue>>* RemoveGlobalTasks;
	if (Args->TryGetArrayField(TEXT("remove_global_task"), RemoveGlobalTasks))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveGlobalTasks)
		{
			int32 Index = -1;
			if (Value->TryGetNumber(Index))
			{
				FString Result = RemoveGlobalTask(StateTree, EditorData, Index);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Transition Operations ==========

	// Process add_transition
	const TArray<TSharedPtr<FJsonValue>>* AddTransitions;
	if (Args->TryGetArrayField(TEXT("add_transition"), AddTransitions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddTransitions)
		{
			const TSharedPtr<FJsonObject>* TransObj;
			if (Value->TryGetObject(TransObj))
			{
				FTransitionDefinition TransDef;
				(*TransObj)->TryGetStringField(TEXT("state"), TransDef.State);
				(*TransObj)->TryGetStringField(TEXT("trigger"), TransDef.Trigger);
				(*TransObj)->TryGetStringField(TEXT("target"), TransDef.Target);
				(*TransObj)->TryGetStringField(TEXT("priority"), TransDef.Priority);
				(*TransObj)->TryGetStringField(TEXT("event_tag"), TransDef.EventTag);
				(*TransObj)->TryGetNumberField(TEXT("delay"), TransDef.DelayDuration);

				FString Result = AddTransition(StateTree, EditorData, TransDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_transition
	const TArray<TSharedPtr<FJsonValue>>* RemoveTransitions;
	if (Args->TryGetArrayField(TEXT("remove_transition"), RemoveTransitions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveTransitions)
		{
			const TSharedPtr<FJsonObject>* TransObj;
			if (Value->TryGetObject(TransObj))
			{
				FString StateName;
				int32 TransIndex = -1;
				(*TransObj)->TryGetStringField(TEXT("state"), StateName);
				(*TransObj)->TryGetNumberField(TEXT("index"), TransIndex);

				FString Result = RemoveTransition(StateTree, EditorData, StateName, TransIndex);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Enter Condition Operations ==========

	// Process add_enter_condition
	const TArray<TSharedPtr<FJsonValue>>* AddConditions;
	if (Args->TryGetArrayField(TEXT("add_enter_condition"), AddConditions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddConditions)
		{
			const TSharedPtr<FJsonObject>* CondObj;
			if (Value->TryGetObject(CondObj))
			{
				FEnterConditionDefinition CondDef;
				(*CondObj)->TryGetStringField(TEXT("state"), CondDef.State);
				(*CondObj)->TryGetStringField(TEXT("type"), CondDef.Type);

				FString Result = AddEnterCondition(StateTree, EditorData, CondDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// ========== Remove Enter Condition Operations ==========

	// Process remove_enter_condition
	const TArray<TSharedPtr<FJsonValue>>* RemoveEnterConditions;
	if (Args->TryGetArrayField(TEXT("remove_enter_condition"), RemoveEnterConditions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveEnterConditions)
		{
			const TSharedPtr<FJsonObject>* CondObj;
			if (Value->TryGetObject(CondObj))
			{
				FString StateName;
				int32 CondIndex = -1;
				(*CondObj)->TryGetStringField(TEXT("state"), StateName);
				(*CondObj)->TryGetNumberField(TEXT("index"), CondIndex);

				FString Result = RemoveEnterCondition(StateTree, EditorData, StateName, CondIndex);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Transition Condition Operations ==========

	// Process add_transition_condition
	const TArray<TSharedPtr<FJsonValue>>* AddTransConditions;
	if (Args->TryGetArrayField(TEXT("add_transition_condition"), AddTransConditions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddTransConditions)
		{
			const TSharedPtr<FJsonObject>* CondObj;
			if (Value->TryGetObject(CondObj))
			{
				FString StateName;
				int32 TransIndex = -1;
				FString CondType;
				(*CondObj)->TryGetStringField(TEXT("state"), StateName);
				(*CondObj)->TryGetNumberField(TEXT("transition_index"), TransIndex);
				(*CondObj)->TryGetStringField(TEXT("type"), CondType);

				FString Result = AddTransitionCondition(StateTree, EditorData, StateName, TransIndex, CondType);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_transition_condition
	const TArray<TSharedPtr<FJsonValue>>* RemoveTransConditions;
	if (Args->TryGetArrayField(TEXT("remove_transition_condition"), RemoveTransConditions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveTransConditions)
		{
			const TSharedPtr<FJsonObject>* CondObj;
			if (Value->TryGetObject(CondObj))
			{
				FString StateName;
				int32 TransIndex = -1;
				int32 CondIndex = -1;
				(*CondObj)->TryGetStringField(TEXT("state"), StateName);
				(*CondObj)->TryGetNumberField(TEXT("transition_index"), TransIndex);
				(*CondObj)->TryGetNumberField(TEXT("condition_index"), CondIndex);

				FString Result = RemoveTransitionCondition(StateTree, EditorData, StateName, TransIndex, CondIndex);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Consideration Operations ==========

	// Process add_consideration
	const TArray<TSharedPtr<FJsonValue>>* AddConsiderations;
	if (Args->TryGetArrayField(TEXT("add_consideration"), AddConsiderations))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddConsiderations)
		{
			const TSharedPtr<FJsonObject>* ConsObj;
			if (Value->TryGetObject(ConsObj))
			{
				FString StateName;
				FString ConsType;
				(*ConsObj)->TryGetStringField(TEXT("state"), StateName);
				(*ConsObj)->TryGetStringField(TEXT("type"), ConsType);

				FString Result = AddConsideration(StateTree, EditorData, StateName, ConsType);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_consideration
	const TArray<TSharedPtr<FJsonValue>>* RemoveConsiderations;
	if (Args->TryGetArrayField(TEXT("remove_consideration"), RemoveConsiderations))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveConsiderations)
		{
			const TSharedPtr<FJsonObject>* ConsObj;
			if (Value->TryGetObject(ConsObj))
			{
				FString StateName;
				int32 ConsIndex = -1;
				(*ConsObj)->TryGetStringField(TEXT("state"), StateName);
				(*ConsObj)->TryGetNumberField(TEXT("index"), ConsIndex);

				FString Result = RemoveConsideration(StateTree, EditorData, StateName, ConsIndex);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Property Binding Operations ==========

	// Process add_property_binding
	const TArray<TSharedPtr<FJsonValue>>* AddBindings;
	if (Args->TryGetArrayField(TEXT("add_property_binding"), AddBindings))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddBindings)
		{
			const TSharedPtr<FJsonObject>* BindObj;
			if (Value->TryGetObject(BindObj))
			{
				FString SrcState, SrcType, SrcProp, TgtState, TgtType, TgtProp;
				int32 SrcIdx = 0, TgtIdx = 0;
				(*BindObj)->TryGetStringField(TEXT("source_state"), SrcState);
				(*BindObj)->TryGetStringField(TEXT("source_node_type"), SrcType);
				(*BindObj)->TryGetNumberField(TEXT("source_index"), SrcIdx);
				(*BindObj)->TryGetStringField(TEXT("source_property"), SrcProp);
				(*BindObj)->TryGetStringField(TEXT("target_state"), TgtState);
				(*BindObj)->TryGetStringField(TEXT("target_node_type"), TgtType);
				(*BindObj)->TryGetNumberField(TEXT("target_index"), TgtIdx);
				(*BindObj)->TryGetStringField(TEXT("target_property"), TgtProp);

				FString Result = AddPropertyBinding(StateTree, EditorData, SrcState, SrcType, SrcIdx, SrcProp, TgtState, TgtType, TgtIdx, TgtProp);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_property_binding
	const TArray<TSharedPtr<FJsonValue>>* RemoveBindings;
	if (Args->TryGetArrayField(TEXT("remove_property_binding"), RemoveBindings))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveBindings)
		{
			const TSharedPtr<FJsonObject>* BindObj;
			if (Value->TryGetObject(BindObj))
			{
				FString StateName, NodeType, PropPath;
				int32 NodeIndex = 0;
				(*BindObj)->TryGetStringField(TEXT("state"), StateName);
				(*BindObj)->TryGetStringField(TEXT("node_type"), NodeType);
				(*BindObj)->TryGetNumberField(TEXT("index"), NodeIndex);
				(*BindObj)->TryGetStringField(TEXT("property"), PropPath);

				FString Result = RemovePropertyBinding(StateTree, EditorData, StateName, NodeType, NodeIndex, PropPath);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// ========== Parameter Operations ==========

#if WITH_STRUCT_UTILS
	// Process add_parameter
	const TArray<TSharedPtr<FJsonValue>>* AddParams;
	if (Args->TryGetArrayField(TEXT("add_parameter"), AddParams))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddParams)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (Value->TryGetObject(ParamObj))
			{
				FString StateName, ParamName, TypeStr, ParamValue;
				(*ParamObj)->TryGetStringField(TEXT("state"), StateName);
				(*ParamObj)->TryGetStringField(TEXT("name"), ParamName);
				(*ParamObj)->TryGetStringField(TEXT("type"), TypeStr);
				(*ParamObj)->TryGetStringField(TEXT("value"), ParamValue);

				FString Result = AddParameter(StateTree, EditorData, StateName, ParamName, TypeStr, ParamValue);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_parameter
	const TArray<TSharedPtr<FJsonValue>>* RemoveParams;
	if (Args->TryGetArrayField(TEXT("remove_parameter"), RemoveParams))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveParams)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (Value->TryGetObject(ParamObj))
			{
				FString StateName, ParamName;
				(*ParamObj)->TryGetStringField(TEXT("state"), StateName);
				(*ParamObj)->TryGetStringField(TEXT("name"), ParamName);

				FString Result = RemoveParameter(StateTree, EditorData, StateName, ParamName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}
#else
	// StructUtils not available — parameter operations disabled
	if (Args->HasField(TEXT("add_parameter")) || Args->HasField(TEXT("remove_parameter")))
	{
		Results.Add(TEXT("! Parameter operations (add_parameter/remove_parameter) require the StructUtils plugin which is not available in this engine build"));
	}
#endif // WITH_STRUCT_UTILS

	// ========== Property Operations ==========

	// Process set_properties (LAST before compilation so nodes exist)
	const TArray<TSharedPtr<FJsonValue>>* SetProperties;
	if (Args->TryGetArrayField(TEXT("set_properties"), SetProperties))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetProperties)
		{
			const TSharedPtr<FJsonObject>* PropObj;
			if (Value->TryGetObject(PropObj))
			{
				FString StateName;
				FString NodeType;
				int32 NodeIndex = -1;
				(*PropObj)->TryGetStringField(TEXT("state"), StateName);
				(*PropObj)->TryGetStringField(TEXT("node_type"), NodeType);
				(*PropObj)->TryGetNumberField(TEXT("index"), NodeIndex);

				const TSharedPtr<FJsonObject>* PropertiesObj;
				if ((*PropObj)->TryGetObjectField(TEXT("properties"), PropertiesObj))
				{
					FString Result = SetNodeProperties(StateTree, EditorData, StateName, NodeType, NodeIndex, *PropertiesObj);
					Results.Add(Result);
				}
				else
				{
					Results.Add(TEXT("! set_properties: Missing 'properties' object"));
				}
			}
		}
	}

	// ========== Schema Operation ==========

	FString SchemaClassName;
	if (Args->TryGetStringField(TEXT("set_schema"), SchemaClassName) && !SchemaClassName.IsEmpty())
	{
		FString Result = SetSchema(StateTree, EditorData, SchemaClassName);
		Results.Add(Result);
		if (Result.StartsWith(TEXT("+"))) AddedCount++;
	}

	// ========== Finalize ==========

	// Mark assets dirty
	StateTree->Modify();
	EditorData->Modify();

	// Compile the StateTree and capture errors
	TArray<FString> CompileErrors;
	bool bCompileSuccess = CompileStateTree(StateTree, CompileErrors);

	// Add compile errors/warnings to results
	for (const FString& Error : CompileErrors)
	{
		Results.Add(FString::Printf(TEXT("  %s"), *Error));
	}

	// Build output
	FString Output = FString::Printf(TEXT("# EDIT STATE_TREE %s\n"), *Name);
	for (const FString& R : Results)
	{
		Output += R + TEXT("\n");
	}
	Output += FString::Printf(TEXT("= %d added, %d removed, compiled=%s\n"),
		AddedCount, RemovedCount, bCompileSuccess ? TEXT("OK") : TEXT("FAIL"));

	return FToolResult::Ok(Output);
}

// ========== Find Helpers ==========

UStateTreeState* FEditStateTreeTool::FindStateByName(UStateTreeEditorData* EditorData, const FString& Name)
{
	if (!EditorData || Name.IsEmpty())
	{
		return nullptr;
	}

	for (UStateTreeState* SubTree : EditorData->SubTrees)
	{
		if (SubTree && SubTree->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return SubTree;
		}

		UStateTreeState* Found = FindStateByNameRecursive(SubTree, Name);
		if (Found)
		{
			return Found;
		}
	}

	return nullptr;
}

UStateTreeState* FEditStateTreeTool::FindStateByNameRecursive(UStateTreeState* State, const FString& Name)
{
	if (!State)
	{
		return nullptr;
	}

	for (UStateTreeState* Child : State->Children)
	{
		if (Child && Child->Name.ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Child;
		}

		UStateTreeState* Found = FindStateByNameRecursive(Child, Name);
		if (Found)
		{
			return Found;
		}
	}

	return nullptr;
}

const UScriptStruct* FEditStateTreeTool::FindNodeStructByName(const FString& TypeName, const FString& BaseStructName)
{
	// Build list of possible names to search for (following engine naming conventions)
	TArray<FString> SearchNames;

	// Original name as-is
	SearchNames.Add(TypeName);

	// With F prefix (UE struct naming convention)
	if (!TypeName.StartsWith(TEXT("F")))
	{
		SearchNames.Add(TEXT("F") + TypeName);
	}

	// Common StateTree naming patterns
	// e.g., "MoveTo" -> "FStateTreeMoveToTask"
	// e.g., "Delay" -> "FStateTreeDelayTask"
	if (!TypeName.Contains(TEXT("StateTree")))
	{
		SearchNames.Add(TEXT("FStateTree") + TypeName);
		SearchNames.Add(TEXT("FStateTree") + TypeName + TEXT("Task"));
		SearchNames.Add(TEXT("FStateTree") + TypeName + TEXT("Evaluator"));
		SearchNames.Add(TEXT("FStateTree") + TypeName + TEXT("Condition"));
		SearchNames.Add(TEXT("FStateTree") + TypeName + TEXT("Consideration"));
	}

	// Handle partial names like "MoveToTask" -> "FStateTreeMoveToTask"
	if (!TypeName.StartsWith(TEXT("StateTree")) && !TypeName.StartsWith(TEXT("FStateTree")))
	{
		SearchNames.Add(TEXT("FStateTree") + TypeName);
	}

	// Get base structs for type checking (using StaticStruct like the engine does)
	const UScriptStruct* TaskBaseStruct = FStateTreeTaskBase::StaticStruct();
	const UScriptStruct* EvaluatorBaseStruct = FStateTreeEvaluatorBase::StaticStruct();
	const UScriptStruct* ConditionBaseStruct = FStateTreeConditionBase::StaticStruct();
	const UScriptStruct* ConsiderationBaseStruct = FStateTreeConsiderationBase::StaticStruct();
	const UScriptStruct* NodeBaseStruct = FStateTreeNodeBase::StaticStruct();

	const UScriptStruct* FallbackMatch = nullptr;

	// Iterate all script structs (same as FStateTreeNodeClassCache does)
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;

		// Skip if it has "Hidden" metadata (engine pattern from SStateTreeNodeTypePicker)
		if (Struct->HasMetaData(TEXT("Hidden")))
		{
			continue;
		}

		// Check if it's a StateTree node type using proper IsChildOf (engine pattern)
		bool bIsStateTreeNode = Struct->IsChildOf(TaskBaseStruct) ||
								Struct->IsChildOf(EvaluatorBaseStruct) ||
								Struct->IsChildOf(ConditionBaseStruct) ||
								Struct->IsChildOf(ConsiderationBaseStruct);

		// Skip non-StateTree structs unless we're doing a general search
		if (!bIsStateTreeNode && !BaseStructName.IsEmpty())
		{
			continue;
		}

		// Skip base structs themselves (engine filters these out)
		if (Struct == TaskBaseStruct || Struct == EvaluatorBaseStruct ||
			Struct == ConditionBaseStruct || Struct == ConsiderationBaseStruct ||
			Struct == NodeBaseStruct)
		{
			continue;
		}

		// Check against all search name variants
		const FString StructName = Struct->GetName();
		for (const FString& SearchName : SearchNames)
		{
			if (StructName.Equals(SearchName, ESearchCase::IgnoreCase))
			{
				return Struct;
			}
		}

		// Also try partial matching for convenience
		// e.g., user types "MoveTo" and we find "FStateTreeMoveToTask"
		if (bIsStateTreeNode && !FallbackMatch)
		{
			if (StructName.Contains(TypeName, ESearchCase::IgnoreCase))
			{
				FallbackMatch = Struct;
			}
		}
	}

	// Return fallback if no exact match found
	return FallbackMatch;
}

EStateTreeStateType FEditStateTreeTool::ParseStateType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("Group"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateType::Group;
	}
	if (TypeStr.Equals(TEXT("Linked"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateType::Linked;
	}
	if (TypeStr.Equals(TEXT("LinkedAsset"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateType::LinkedAsset;
	}
	if (TypeStr.Equals(TEXT("Subtree"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateType::Subtree;
	}
	return EStateTreeStateType::State;
}

EStateTreeStateSelectionBehavior FEditStateTreeTool::ParseSelectionBehavior(const FString& BehaviorStr)
{
	if (BehaviorStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateSelectionBehavior::None;
	}
	if (BehaviorStr.Equals(TEXT("TryEnterState"), ESearchCase::IgnoreCase) ||
		BehaviorStr.Equals(TEXT("TryEnter"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateSelectionBehavior::TryEnterState;
	}
	if (BehaviorStr.Equals(TEXT("TrySelectChildrenAtRandom"), ESearchCase::IgnoreCase) ||
		BehaviorStr.Equals(TEXT("Random"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateSelectionBehavior::TrySelectChildrenAtRandom;
	}
	if (BehaviorStr.Equals(TEXT("TrySelectChildrenWithHighestUtility"), ESearchCase::IgnoreCase) ||
		BehaviorStr.Equals(TEXT("HighestUtility"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateSelectionBehavior::TrySelectChildrenWithHighestUtility;
	}
	if (BehaviorStr.Equals(TEXT("TryFollowTransitions"), ESearchCase::IgnoreCase) ||
		BehaviorStr.Equals(TEXT("FollowTransitions"), ESearchCase::IgnoreCase))
	{
		return EStateTreeStateSelectionBehavior::TryFollowTransitions;
	}
	// Default
	return EStateTreeStateSelectionBehavior::TrySelectChildrenInOrder;
}

EStateTreeTransitionTrigger FEditStateTreeTool::ParseTransitionTrigger(const FString& TriggerStr)
{
	if (TriggerStr.Equals(TEXT("OnStateSucceeded"), ESearchCase::IgnoreCase) ||
		TriggerStr.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionTrigger::OnStateSucceeded;
	}
	if (TriggerStr.Equals(TEXT("OnStateFailed"), ESearchCase::IgnoreCase) ||
		TriggerStr.Equals(TEXT("Failed"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionTrigger::OnStateFailed;
	}
	if (TriggerStr.Equals(TEXT("OnTick"), ESearchCase::IgnoreCase) ||
		TriggerStr.Equals(TEXT("Tick"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionTrigger::OnTick;
	}
	if (TriggerStr.Equals(TEXT("OnEvent"), ESearchCase::IgnoreCase) ||
		TriggerStr.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionTrigger::OnEvent;
	}
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
	if (TriggerStr.Equals(TEXT("OnDelegate"), ESearchCase::IgnoreCase) ||
		TriggerStr.Equals(TEXT("Delegate"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionTrigger::OnDelegate;
	}
#endif
	// Default
	return EStateTreeTransitionTrigger::OnStateCompleted;
}

EStateTreeTransitionType FEditStateTreeTool::ParseTransitionType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("Succeeded"), ESearchCase::IgnoreCase) ||
		TypeStr.Equals(TEXT("Success"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionType::Succeeded;
	}
	if (TypeStr.Equals(TEXT("Failed"), ESearchCase::IgnoreCase) ||
		TypeStr.Equals(TEXT("Fail"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionType::Failed;
	}
	if (TypeStr.Equals(TEXT("NextState"), ESearchCase::IgnoreCase) ||
		TypeStr.Equals(TEXT("Next"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionType::NextState;
	}
	if (TypeStr.Equals(TEXT("NextSelectableState"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionType::NextSelectableState;
	}
	if (TypeStr.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionType::None;
	}
	// Default - assume it's a state name, so GotoState
	return EStateTreeTransitionType::GotoState;
}

EStateTreeTransitionPriority FEditStateTreeTool::ParseTransitionPriority(const FString& PriorityStr)
{
	if (PriorityStr.Equals(TEXT("Low"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionPriority::Low;
	}
	if (PriorityStr.Equals(TEXT("Medium"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionPriority::Medium;
	}
	if (PriorityStr.Equals(TEXT("High"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionPriority::High;
	}
	if (PriorityStr.Equals(TEXT("Critical"), ESearchCase::IgnoreCase))
	{
		return EStateTreeTransitionPriority::Critical;
	}
	return EStateTreeTransitionPriority::Normal;
}

// ========== State Operations ==========

FString FEditStateTreeTool::AddState(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FStateDefinition& StateDef)
{
	if (StateDef.Name.IsEmpty())
	{
		return TEXT("! State: Missing name");
	}

	// Check if state already exists
	if (FindStateByName(EditorData, StateDef.Name))
	{
		return FString::Printf(TEXT("! State: '%s' already exists"), *StateDef.Name);
	}

	// Create new state
	UStateTreeState* NewState = NewObject<UStateTreeState>(EditorData, NAME_None, RF_Transactional);
	NewState->Name = FName(*StateDef.Name);
	NewState->Type = ParseStateType(StateDef.Type);
	NewState->SelectionBehavior = ParseSelectionBehavior(StateDef.SelectionBehavior);
	NewState->bEnabled = StateDef.bEnabled;
	NewState->ID = FGuid::NewGuid();

	if (!StateDef.Tag.IsEmpty())
	{
		NewState->Tag = FGameplayTag::RequestGameplayTag(FName(*StateDef.Tag), false);
	}

	// Add to parent or as root
	if (StateDef.Parent.IsEmpty())
	{
		// Add as root subtree
		EditorData->SubTrees.Add(NewState);
	}
	else
	{
		UStateTreeState* ParentState = FindStateByName(EditorData, StateDef.Parent);
		if (!ParentState)
		{
			return FString::Printf(TEXT("! State: Parent '%s' not found"), *StateDef.Parent);
		}

		NewState->Parent = ParentState;
		ParentState->Children.Add(NewState);
	}

	FString TypeStr = StateDef.Type.IsEmpty() ? TEXT("State") : StateDef.Type;
	FString ParentStr = StateDef.Parent.IsEmpty() ? TEXT("(root)") : StateDef.Parent;
	return FString::Printf(TEXT("+ State: %s (%s) -> %s"), *StateDef.Name, *TypeStr, *ParentStr);
}

FString FEditStateTreeTool::RemoveState(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName)
{
	// Check root subtrees first
	for (int32 i = EditorData->SubTrees.Num() - 1; i >= 0; i--)
	{
		if (EditorData->SubTrees[i] && EditorData->SubTrees[i]->Name.ToString().Equals(StateName, ESearchCase::IgnoreCase))
		{
			EditorData->SubTrees.RemoveAt(i);
			return FString::Printf(TEXT("- State: %s (was root)"), *StateName);
		}
	}

	// Search in hierarchy
	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! State: '%s' not found"), *StateName);
	}

	// Remove from parent
	if (State->Parent)
	{
		State->Parent->Children.Remove(State);
		return FString::Printf(TEXT("- State: %s from parent %s"), *StateName, *State->Parent->Name.ToString());
	}

	return FString::Printf(TEXT("! State: '%s' could not be removed"), *StateName);
}

// ========== Task Operations ==========

FString FEditStateTreeTool::AddTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FTaskDefinition& TaskDef)
{
	if (TaskDef.State.IsEmpty())
	{
		return TEXT("! Task: Missing state name");
	}
	if (TaskDef.Type.IsEmpty())
	{
		return TEXT("! Task: Missing type");
	}

	UStateTreeState* State = FindStateByName(EditorData, TaskDef.State);
	if (!State)
	{
		return FString::Printf(TEXT("! Task: State '%s' not found"), *TaskDef.State);
	}

	// Find task struct
	const UScriptStruct* TaskStruct = FindNodeStructByName(TaskDef.Type, TEXT("StateTreeTask"));
	if (!TaskStruct)
	{
		return FString::Printf(TEXT("! Task: Unknown type '%s'"), *TaskDef.Type);
	}

	// Create the task node
	FStateTreeEditorNode& NewNode = State->Tasks.AddDefaulted_GetRef();
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(TaskStruct);

	// Initialize instance data if the task defines it
	if (const FStateTreeNodeBase* NodeBase = NewNode.Node.GetPtr<FStateTreeNodeBase>())
	{
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
		{
			NewNode.Instance.InitializeAs(InstanceType);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
		{
			NewNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
#endif
	}

	return FString::Printf(TEXT("+ Task: %s -> state %s"), *TaskDef.Type, *TaskDef.State);
}

FString FEditStateTreeTool::RemoveTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TaskIndex)
{
	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! Task: State '%s' not found"), *StateName);
	}

	if (TaskIndex < 0 || TaskIndex >= State->Tasks.Num())
	{
		return FString::Printf(TEXT("! Task: Index %d out of range (state has %d tasks)"), TaskIndex, State->Tasks.Num());
	}

	State->Tasks.RemoveAt(TaskIndex);
	return FString::Printf(TEXT("- Task: index %d from state %s"), TaskIndex, *StateName);
}

// ========== Evaluator Operations ==========

FString FEditStateTreeTool::AddEvaluator(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FEvaluatorDefinition& EvalDef)
{
	if (EvalDef.Type.IsEmpty())
	{
		return TEXT("! Evaluator: Missing type");
	}

	const UScriptStruct* EvalStruct = FindNodeStructByName(EvalDef.Type, TEXT("StateTreeEvaluator"));
	if (!EvalStruct)
	{
		return FString::Printf(TEXT("! Evaluator: Unknown type '%s'"), *EvalDef.Type);
	}

	FStateTreeEditorNode& NewNode = EditorData->Evaluators.AddDefaulted_GetRef();
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(EvalStruct);

	if (const FStateTreeNodeBase* NodeBase = NewNode.Node.GetPtr<FStateTreeNodeBase>())
	{
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
		{
			NewNode.Instance.InitializeAs(InstanceType);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
		{
			NewNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
#endif
	}

	return FString::Printf(TEXT("+ Evaluator: %s (index %d)"), *EvalDef.Type, EditorData->Evaluators.Num() - 1);
}

FString FEditStateTreeTool::RemoveEvaluator(UStateTree* StateTree, UStateTreeEditorData* EditorData, int32 Index)
{
	if (Index < 0 || Index >= EditorData->Evaluators.Num())
	{
		return FString::Printf(TEXT("! Evaluator: Index %d out of range (has %d evaluators)"), Index, EditorData->Evaluators.Num());
	}

	EditorData->Evaluators.RemoveAt(Index);
	return FString::Printf(TEXT("- Evaluator: index %d"), Index);
}

// ========== Global Task Operations ==========

FString FEditStateTreeTool::AddGlobalTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FGlobalTaskDefinition& TaskDef)
{
	if (TaskDef.Type.IsEmpty())
	{
		return TEXT("! GlobalTask: Missing type");
	}

	const UScriptStruct* TaskStruct = FindNodeStructByName(TaskDef.Type, TEXT("StateTreeTask"));
	if (!TaskStruct)
	{
		return FString::Printf(TEXT("! GlobalTask: Unknown type '%s'"), *TaskDef.Type);
	}

	FStateTreeEditorNode& NewNode = EditorData->GlobalTasks.AddDefaulted_GetRef();
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(TaskStruct);

	if (const FStateTreeNodeBase* NodeBase = NewNode.Node.GetPtr<FStateTreeNodeBase>())
	{
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
		{
			NewNode.Instance.InitializeAs(InstanceType);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
		{
			NewNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
#endif
	}

	return FString::Printf(TEXT("+ GlobalTask: %s (index %d)"), *TaskDef.Type, EditorData->GlobalTasks.Num() - 1);
}

FString FEditStateTreeTool::RemoveGlobalTask(UStateTree* StateTree, UStateTreeEditorData* EditorData, int32 Index)
{
	if (Index < 0 || Index >= EditorData->GlobalTasks.Num())
	{
		return FString::Printf(TEXT("! GlobalTask: Index %d out of range (has %d global tasks)"), Index, EditorData->GlobalTasks.Num());
	}

	EditorData->GlobalTasks.RemoveAt(Index);
	return FString::Printf(TEXT("- GlobalTask: index %d"), Index);
}

// ========== Transition Operations ==========

FString FEditStateTreeTool::AddTransition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FTransitionDefinition& TransDef)
{
	if (TransDef.State.IsEmpty())
	{
		return TEXT("! Transition: Missing state name");
	}

	UStateTreeState* State = FindStateByName(EditorData, TransDef.State);
	if (!State)
	{
		return FString::Printf(TEXT("! Transition: State '%s' not found"), *TransDef.State);
	}

	// Parse trigger and transition type
	EStateTreeTransitionTrigger Trigger = ParseTransitionTrigger(TransDef.Trigger);
	EStateTreeTransitionType TransType = ParseTransitionType(TransDef.Target);
	EStateTreeTransitionPriority Priority = ParseTransitionPriority(TransDef.Priority);

	// Create the transition
	FStateTreeTransition& NewTrans = State->Transitions.AddDefaulted_GetRef();
	NewTrans.ID = FGuid::NewGuid();
	NewTrans.Trigger = Trigger;
	NewTrans.Priority = Priority;
	NewTrans.State.LinkType = TransType;

	// If GotoState, find the target state
	if (TransType == EStateTreeTransitionType::GotoState && !TransDef.Target.IsEmpty())
	{
		UStateTreeState* TargetState = FindStateByName(EditorData, TransDef.Target);
		if (TargetState)
		{
			NewTrans.State.ID = TargetState->ID;
			NewTrans.State.Name = TargetState->Name;
		}
		else
		{
			return FString::Printf(TEXT("! Transition: Target state '%s' not found"), *TransDef.Target);
		}
	}

	// Handle event tag - for OnEvent triggers, we MUST set a tag or payload
	if (Trigger == EStateTreeTransitionTrigger::OnEvent && !TransDef.EventTag.IsEmpty())
	{
		// Request the tag - if it doesn't exist in the project, this will return invalid
		// but we still need to set SOMETHING for the transition to be valid
		FGameplayTag Tag = FGameplayTag::RequestGameplayTag(FName(*TransDef.EventTag), false);
		if (!Tag.IsValid())
		{
			// Tag doesn't exist in project - warn but still try to use it
			// The tag will need to be added to the project's GameplayTags for this to work at runtime
			UE_LOG(LogTemp, Warning, TEXT("StateTree: GameplayTag '%s' not found in project. Add it to your GameplayTags settings."), *TransDef.EventTag);
		}
		NewTrans.RequiredEvent.Tag = Tag;
	}
	else if (Trigger == EStateTreeTransitionTrigger::OnEvent && TransDef.EventTag.IsEmpty())
	{
		// OnEvent trigger requires a tag - return error
		return FString::Printf(TEXT("! Transition: OnEvent trigger requires event_tag parameter"));
	}

	// Handle delay
	if (TransDef.DelayDuration > 0.0f)
	{
		NewTrans.bDelayTransition = true;
		NewTrans.DelayDuration = TransDef.DelayDuration;
	}

	FString TriggerStr = TransDef.Trigger.IsEmpty() ? TEXT("OnStateCompleted") : TransDef.Trigger;
	FString TargetStr = TransDef.Target.IsEmpty() ? TEXT("(none)") : TransDef.Target;
	return FString::Printf(TEXT("+ Transition: %s -> %s (%s) [%s]"),
		*TransDef.State, *TargetStr, *TriggerStr, *TransDef.Priority);
}

FString FEditStateTreeTool::RemoveTransition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TransitionIndex)
{
	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! Transition: State '%s' not found"), *StateName);
	}

	if (TransitionIndex < 0 || TransitionIndex >= State->Transitions.Num())
	{
		return FString::Printf(TEXT("! Transition: Index %d out of range (state has %d transitions)"), TransitionIndex, State->Transitions.Num());
	}

	State->Transitions.RemoveAt(TransitionIndex);
	return FString::Printf(TEXT("- Transition: index %d from state %s"), TransitionIndex, *StateName);
}

// ========== Condition Operations ==========

FString FEditStateTreeTool::AddEnterCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FEnterConditionDefinition& CondDef)
{
	if (CondDef.State.IsEmpty())
	{
		return TEXT("! EnterCondition: Missing state name");
	}
	if (CondDef.Type.IsEmpty())
	{
		return TEXT("! EnterCondition: Missing type");
	}

	UStateTreeState* State = FindStateByName(EditorData, CondDef.State);
	if (!State)
	{
		return FString::Printf(TEXT("! EnterCondition: State '%s' not found"), *CondDef.State);
	}

	const UScriptStruct* CondStruct = FindNodeStructByName(CondDef.Type, TEXT("StateTreeCondition"));
	if (!CondStruct)
	{
		return FString::Printf(TEXT("! EnterCondition: Unknown type '%s'"), *CondDef.Type);
	}

	FStateTreeEditorNode& NewNode = State->EnterConditions.AddDefaulted_GetRef();
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(CondStruct);

	if (const FStateTreeNodeBase* NodeBase = NewNode.Node.GetPtr<FStateTreeNodeBase>())
	{
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
		{
			NewNode.Instance.InitializeAs(InstanceType);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
		{
			NewNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
#endif
	}

	return FString::Printf(TEXT("+ EnterCondition: %s -> state %s"), *CondDef.Type, *CondDef.State);
}

FString FEditStateTreeTool::RemoveEnterCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 Index)
{
	if (StateName.IsEmpty())
	{
		return TEXT("! RemoveEnterCondition: Missing state name");
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! RemoveEnterCondition: State '%s' not found"), *StateName);
	}

	if (Index < 0 || Index >= State->EnterConditions.Num())
	{
		return FString::Printf(TEXT("! RemoveEnterCondition: Index %d out of range (state has %d enter conditions)"), Index, State->EnterConditions.Num());
	}

	State->EnterConditions.RemoveAt(Index);
	return FString::Printf(TEXT("- EnterCondition: index %d from state %s"), Index, *StateName);
}

// ========== Transition Condition Operations ==========

FString FEditStateTreeTool::AddTransitionCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TransitionIndex, const FString& CondType)
{
	if (StateName.IsEmpty())
	{
		return TEXT("! TransitionCondition: Missing state name");
	}
	if (CondType.IsEmpty())
	{
		return TEXT("! TransitionCondition: Missing type");
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! TransitionCondition: State '%s' not found"), *StateName);
	}

	if (TransitionIndex < 0 || TransitionIndex >= State->Transitions.Num())
	{
		return FString::Printf(TEXT("! TransitionCondition: Transition index %d out of range (state has %d transitions)"), TransitionIndex, State->Transitions.Num());
	}

	const UScriptStruct* CondStruct = FindNodeStructByName(CondType, TEXT("StateTreeCondition"));
	if (!CondStruct)
	{
		return FString::Printf(TEXT("! TransitionCondition: Unknown type '%s'"), *CondType);
	}

	FStateTreeTransition& Trans = State->Transitions[TransitionIndex];
	FStateTreeEditorNode& NewNode = Trans.Conditions.AddDefaulted_GetRef();
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(CondStruct);

	// Init instance data
	if (const FStateTreeNodeBase* NodeBase = NewNode.Node.GetPtr<FStateTreeNodeBase>())
	{
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
		{
			NewNode.Instance.InitializeAs(InstanceType);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
		{
			NewNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
#endif
	}

	return FString::Printf(TEXT("+ TransitionCondition: %s -> state %s transition %d"), *CondType, *StateName, TransitionIndex);
}

FString FEditStateTreeTool::RemoveTransitionCondition(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 TransitionIndex, int32 ConditionIndex)
{
	if (StateName.IsEmpty())
	{
		return TEXT("! RemoveTransitionCondition: Missing state name");
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! RemoveTransitionCondition: State '%s' not found"), *StateName);
	}

	if (TransitionIndex < 0 || TransitionIndex >= State->Transitions.Num())
	{
		return FString::Printf(TEXT("! RemoveTransitionCondition: Transition index %d out of range (state has %d transitions)"), TransitionIndex, State->Transitions.Num());
	}

	FStateTreeTransition& Trans = State->Transitions[TransitionIndex];

	if (ConditionIndex < 0 || ConditionIndex >= Trans.Conditions.Num())
	{
		return FString::Printf(TEXT("! RemoveTransitionCondition: Condition index %d out of range (transition has %d conditions)"), ConditionIndex, Trans.Conditions.Num());
	}

	Trans.Conditions.RemoveAt(ConditionIndex);
	return FString::Printf(TEXT("- TransitionCondition: index %d from state %s transition %d"), ConditionIndex, *StateName, TransitionIndex);
}

// ========== Consideration Operations ==========

FString FEditStateTreeTool::AddConsideration(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, const FString& Type)
{
	if (StateName.IsEmpty())
	{
		return TEXT("! Consideration: Missing state name");
	}
	if (Type.IsEmpty())
	{
		return TEXT("! Consideration: Missing type");
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! Consideration: State '%s' not found"), *StateName);
	}

	const UScriptStruct* ConsStruct = FindNodeStructByName(Type, TEXT("StateTreeConsideration"));
	if (!ConsStruct)
	{
		return FString::Printf(TEXT("! Consideration: Unknown type '%s'"), *Type);
	}

	FStateTreeEditorNode& NewNode = State->Considerations.AddDefaulted_GetRef();
	NewNode.ID = FGuid::NewGuid();
	NewNode.Node.InitializeAs(ConsStruct);

	// Init instance data
	if (const FStateTreeNodeBase* NodeBase = NewNode.Node.GetPtr<FStateTreeNodeBase>())
	{
		if (const UScriptStruct* InstanceType = Cast<const UScriptStruct>(NodeBase->GetInstanceDataType()))
		{
			NewNode.Instance.InitializeAs(InstanceType);
		}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		if (const UScriptStruct* RuntimeType = Cast<const UScriptStruct>(NodeBase->GetExecutionRuntimeDataType()))
		{
			NewNode.ExecutionRuntimeData.InitializeAs(RuntimeType);
		}
#endif
	}

	return FString::Printf(TEXT("+ Consideration: %s -> state %s"), *Type, *StateName);
}

FString FEditStateTreeTool::RemoveConsideration(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, int32 Index)
{
	if (StateName.IsEmpty())
	{
		return TEXT("! RemoveConsideration: Missing state name");
	}

	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return FString::Printf(TEXT("! RemoveConsideration: State '%s' not found"), *StateName);
	}

	if (Index < 0 || Index >= State->Considerations.Num())
	{
		return FString::Printf(TEXT("! RemoveConsideration: Index %d out of range (state has %d considerations)"), Index, State->Considerations.Num());
	}

	State->Considerations.RemoveAt(Index);
	return FString::Printf(TEXT("- Consideration: index %d from state %s"), Index, *StateName);
}

// ========== Find Editor Node Helper ==========

FStateTreeEditorNode* FEditStateTreeTool::FindEditorNode(UStateTreeEditorData* EditorData, const FString& StateName, const FString& NodeType, int32 Index)
{
	if (!EditorData || NodeType.IsEmpty())
	{
		return nullptr;
	}

	// Global node types (evaluator, global_task): look in EditorData directly
	if (NodeType.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
	{
		if (Index >= 0 && Index < EditorData->Evaluators.Num())
		{
			return &EditorData->Evaluators[Index];
		}
		return nullptr;
	}
	if (NodeType.Equals(TEXT("global_task"), ESearchCase::IgnoreCase))
	{
		if (Index >= 0 && Index < EditorData->GlobalTasks.Num())
		{
			return &EditorData->GlobalTasks[Index];
		}
		return nullptr;
	}

	// For state-scoped types: find state first
	UStateTreeState* State = FindStateByName(EditorData, StateName);
	if (!State)
	{
		return nullptr;
	}

	if (NodeType.Equals(TEXT("task"), ESearchCase::IgnoreCase))
	{
		if (Index >= 0 && Index < State->Tasks.Num())
		{
			return &State->Tasks[Index];
		}
	}
	else if (NodeType.Equals(TEXT("condition"), ESearchCase::IgnoreCase) || NodeType.Equals(TEXT("enter_condition"), ESearchCase::IgnoreCase))
	{
		if (Index >= 0 && Index < State->EnterConditions.Num())
		{
			return &State->EnterConditions[Index];
		}
	}
	else if (NodeType.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
	{
		if (Index >= 0 && Index < State->Considerations.Num())
		{
			return &State->Considerations[Index];
		}
	}

	return nullptr;
}

// ========== Property Binding Operations ==========

FString FEditStateTreeTool::AddPropertyBinding(UStateTree* StateTree, UStateTreeEditorData* EditorData,
	const FString& SourceState, const FString& SourceNodeType, int32 SourceIndex, const FString& SourceProperty,
	const FString& TargetState, const FString& TargetNodeType, int32 TargetIndex, const FString& TargetProperty)
{
	if (SourceProperty.IsEmpty())
	{
		return TEXT("! add_property_binding: Missing 'source_property'");
	}
	if (TargetProperty.IsEmpty())
	{
		return TEXT("! add_property_binding: Missing 'target_property'");
	}
	if (SourceNodeType.IsEmpty())
	{
		return TEXT("! add_property_binding: Missing 'source_node_type'");
	}
	if (TargetNodeType.IsEmpty())
	{
		return TEXT("! add_property_binding: Missing 'target_node_type'");
	}

	FStateTreeEditorNode* SourceNode = FindEditorNode(EditorData, SourceState, SourceNodeType, SourceIndex);
	if (!SourceNode)
	{
		return FString::Printf(TEXT("! add_property_binding: Source node not found (%s %s[%d])"),
			*SourceState, *SourceNodeType, SourceIndex);
	}

	FStateTreeEditorNode* TargetNode = FindEditorNode(EditorData, TargetState, TargetNodeType, TargetIndex);
	if (!TargetNode)
	{
		return FString::Printf(TEXT("! add_property_binding: Target node not found (%s %s[%d])"),
			*TargetState, *TargetNodeType, TargetIndex);
	}

	// Use the convenience method on UStateTreeEditorData that handles GUID resolution
	bool bSuccess = EditorData->AddPropertyBinding(*SourceNode, SourceProperty, *TargetNode, TargetProperty);

	if (bSuccess)
	{
		return FString::Printf(TEXT("+ Binding: %s[%d].%s -> %s[%d].%s"),
			*SourceNodeType, SourceIndex, *SourceProperty, *TargetNodeType, TargetIndex, *TargetProperty);
	}
	else
	{
		return FString::Printf(TEXT("! add_property_binding: Failed to bind %s.%s -> %s.%s (check property paths are valid)"),
			*SourceNodeType, *SourceProperty, *TargetNodeType, *TargetProperty);
	}
}

FString FEditStateTreeTool::RemovePropertyBinding(UStateTree* StateTree, UStateTreeEditorData* EditorData,
	const FString& StateName, const FString& NodeType, int32 Index, const FString& PropertyPath)
{
	if (PropertyPath.IsEmpty())
	{
		return TEXT("! remove_property_binding: Missing 'property' (target property path to unbind)");
	}
	if (NodeType.IsEmpty())
	{
		return TEXT("! remove_property_binding: Missing 'node_type'");
	}

	FStateTreeEditorNode* Node = FindEditorNode(EditorData, StateName, NodeType, Index);
	if (!Node)
	{
		return FString::Printf(TEXT("! remove_property_binding: Node not found (%s %s[%d])"),
			*StateName, *NodeType, Index);
	}

	FStateTreeEditorPropertyBindings* Bindings = EditorData->GetPropertyEditorBindings();
	if (!Bindings)
	{
		return TEXT("! remove_property_binding: No property bindings available on this StateTree");
	}

	// Build the target path from the node's ID and property path string
	FPropertyBindingPath TargetPath;
	TargetPath.SetStructID(Node->ID);
	if (!TargetPath.FromString(PropertyPath))
	{
		return FString::Printf(TEXT("! remove_property_binding: Invalid property path '%s'"), *PropertyPath);
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	// Check if binding exists before removing
	bool bHadBinding = Bindings->HasBinding(TargetPath);
	Bindings->RemoveBindings(TargetPath);

	if (bHadBinding)
	{
		return FString::Printf(TEXT("- Binding: %s[%d].%s"), *NodeType, Index, *PropertyPath);
	}
	else
	{
		return FString::Printf(TEXT("! remove_property_binding: No binding found for %s[%d].%s"), *NodeType, Index, *PropertyPath);
	}
#else
	// HasBinding/RemoveBindings not available in 5.5
	return TEXT("! remove_property_binding: Not supported in UE 5.5 (requires 5.6+)");
#endif
}

// ========== Parameter Helper Functions ==========

#if WITH_STRUCT_UTILS

EPropertyBagPropertyType FEditStateTreeTool::ParsePropertyBagType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Bool;
	if (TypeStr.Equals(TEXT("Byte"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Byte;
	if (TypeStr.Equals(TEXT("Int32"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Int"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Integer"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Int32;
	if (TypeStr.Equals(TEXT("Int64"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Int64;
	if (TypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Float;
	if (TypeStr.Equals(TEXT("Double"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Double;
	if (TypeStr.Equals(TEXT("Name"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("FName"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Name;
	if (TypeStr.Equals(TEXT("String"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("FString"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::String;
	if (TypeStr.Equals(TEXT("Text"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("FText"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::Text;
	if (TypeStr.Equals(TEXT("UInt32"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::UInt32;
	if (TypeStr.Equals(TEXT("UInt64"), ESearchCase::IgnoreCase)) return EPropertyBagPropertyType::UInt64;
	// Struct, Object, Enum, etc. cannot be created from string type name alone
	return EPropertyBagPropertyType::None;
}

void FEditStateTreeTool::SetParameterValue(FInstancedPropertyBag* Bag, const FString& ParamName, EPropertyBagPropertyType Type, const FString& Value)
{
	if (!Bag || Value.IsEmpty())
	{
		return;
	}

	FName PropName(*ParamName);

	switch (Type)
	{
	case EPropertyBagPropertyType::Bool:
		Bag->SetValueBool(PropName, Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value.Equals(TEXT("1")));
		break;
	case EPropertyBagPropertyType::Byte:
		Bag->SetValueByte(PropName, static_cast<uint8>(FCString::Atoi(*Value)));
		break;
	case EPropertyBagPropertyType::Int32:
		Bag->SetValueInt32(PropName, FCString::Atoi(*Value));
		break;
	case EPropertyBagPropertyType::Int64:
		Bag->SetValueInt64(PropName, FCString::Atoi64(*Value));
		break;
	case EPropertyBagPropertyType::Float:
		Bag->SetValueFloat(PropName, FCString::Atof(*Value));
		break;
	case EPropertyBagPropertyType::Double:
		Bag->SetValueDouble(PropName, FCString::Atod(*Value));
		break;
	case EPropertyBagPropertyType::Name:
		Bag->SetValueName(PropName, FName(*Value));
		break;
	case EPropertyBagPropertyType::String:
		Bag->SetValueString(PropName, Value);
		break;
	case EPropertyBagPropertyType::Text:
		Bag->SetValueText(PropName, FText::FromString(Value));
		break;
	case EPropertyBagPropertyType::UInt32:
		Bag->SetValueUInt32(PropName, static_cast<uint32>(FCString::Strtoui64(*Value, nullptr, 10)));
		break;
	case EPropertyBagPropertyType::UInt64:
		Bag->SetValueUInt64(PropName, FCString::Strtoui64(*Value, nullptr, 10));
		break;
	default:
		break; // Complex types (Struct, Object, Enum) cannot be set from string
	}
}

FInstancedPropertyBag* FEditStateTreeTool::GetParameterBag(UStateTreeEditorData* EditorData, const FString& StateName, FString& OutError)
{
	if (!EditorData)
	{
		OutError = TEXT("EditorData is null");
		return nullptr;
	}

	if (StateName.IsEmpty())
	{
		// Global parameters - RootParameterPropertyBag is private, access via UPROPERTY reflection
		FProperty* Prop = EditorData->GetClass()->FindPropertyByName(TEXT("RootParameterPropertyBag"));
		if (!Prop)
		{
			OutError = TEXT("Could not find RootParameterPropertyBag property on editor data");
			return nullptr;
		}
		void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(EditorData);
		if (!ValuePtr)
		{
			OutError = TEXT("Could not get RootParameterPropertyBag value pointer");
			return nullptr;
		}
		return static_cast<FInstancedPropertyBag*>(ValuePtr);
	}
	else
	{
		// State parameters
		UStateTreeState* State = FindStateByName(EditorData, StateName);
		if (!State)
		{
			OutError = FString::Printf(TEXT("State '%s' not found"), *StateName);
			return nullptr;
		}
		return &State->Parameters.Parameters;
	}
}

// ========== Parameter Operations ==========

FString FEditStateTreeTool::AddParameter(UStateTree* StateTree, UStateTreeEditorData* EditorData,
	const FString& StateName, const FString& ParamName, const FString& TypeStr, const FString& Value)
{
	if (ParamName.IsEmpty())
	{
		return TEXT("! add_parameter: Missing 'name'");
	}
	if (TypeStr.IsEmpty())
	{
		return TEXT("! add_parameter: Missing 'type'");
	}

	EPropertyBagPropertyType PropType = ParsePropertyBagType(TypeStr);
	if (PropType == EPropertyBagPropertyType::None)
	{
		return FString::Printf(TEXT("! add_parameter: Unknown type '%s'. Use: Bool, Byte, Int32, Int64, Float, Double, Name, String, Text, UInt32, UInt64"), *TypeStr);
	}

	FString BagError;
	FInstancedPropertyBag* Bag = GetParameterBag(EditorData, StateName, BagError);
	if (!Bag)
	{
		return FString::Printf(TEXT("! add_parameter: %s"), *BagError);
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	EPropertyBagAlterationResult Result = Bag->AddProperty(FName(*ParamName), PropType);
	if (Result != EPropertyBagAlterationResult::Success)
	{
		FString ReasonStr;
		switch (Result)
		{
		case EPropertyBagAlterationResult::PropertyNameEmpty: ReasonStr = TEXT("property name is empty"); break;
		case EPropertyBagAlterationResult::PropertyNameInvalidCharacters: ReasonStr = TEXT("property name has invalid characters"); break;
		case EPropertyBagAlterationResult::TargetPropertyAlreadyExists: ReasonStr = TEXT("property already exists"); break;
		default: ReasonStr = TEXT("internal error"); break;
		}
		return FString::Printf(TEXT("! add_parameter: Failed to add '%s' (%s)"), *ParamName, *ReasonStr);
	}
#else
	Bag->AddProperty(FName(*ParamName), PropType);
#endif

	// Optionally set initial value
	if (!Value.IsEmpty())
	{
		SetParameterValue(Bag, ParamName, PropType, Value);
	}

	FString Location = StateName.IsEmpty() ? TEXT("global") : FString::Printf(TEXT("state %s"), *StateName);
	return FString::Printf(TEXT("+ Parameter: %s (%s) -> %s"), *ParamName, *TypeStr, *Location);
}

FString FEditStateTreeTool::RemoveParameter(UStateTree* StateTree, UStateTreeEditorData* EditorData,
	const FString& StateName, const FString& ParamName)
{
	if (ParamName.IsEmpty())
	{
		return TEXT("! remove_parameter: Missing 'name'");
	}

	FString BagError;
	FInstancedPropertyBag* Bag = GetParameterBag(EditorData, StateName, BagError);
	if (!Bag)
	{
		return FString::Printf(TEXT("! remove_parameter: %s"), *BagError);
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	EPropertyBagAlterationResult Result = Bag->RemovePropertyByName(FName(*ParamName));
	if (Result == EPropertyBagAlterationResult::Success)
	{
		FString Location = StateName.IsEmpty() ? TEXT("global") : FString::Printf(TEXT("state %s"), *StateName);
		return FString::Printf(TEXT("- Parameter: %s from %s"), *ParamName, *Location);
	}
	else
	{
		FString ReasonStr;
		switch (Result)
		{
		case EPropertyBagAlterationResult::SourcePropertyNotFound:
		case EPropertyBagAlterationResult::TargetPropertyNotFound:
			ReasonStr = TEXT("property not found");
			break;
		default:
			ReasonStr = TEXT("internal error");
			break;
		}
		return FString::Printf(TEXT("! remove_parameter: Failed to remove '%s' (%s)"), *ParamName, *ReasonStr);
	}
#else
	Bag->RemovePropertyByName(FName(*ParamName));
	FString Location = StateName.IsEmpty() ? TEXT("global") : FString::Printf(TEXT("state %s"), *StateName);
	return FString::Printf(TEXT("- Parameter: %s from %s"), *ParamName, *Location);
#endif
}

#endif // WITH_STRUCT_UTILS

// ========== Property Operations ==========

FString FEditStateTreeTool::SetNodeProperties(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& StateName, const FString& NodeType, int32 Index, const TSharedPtr<FJsonObject>& Properties)
{
	if (NodeType.IsEmpty())
	{
		return TEXT("! set_properties: Missing 'node_type'");
	}
	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		return TEXT("! set_properties: Missing or empty 'properties'");
	}

	// Locate the FStateTreeEditorNode based on node_type and index
	FStateTreeEditorNode* EditorNode = nullptr;
	FString NodeLocation;

	if (NodeType.Equals(TEXT("evaluator"), ESearchCase::IgnoreCase))
	{
		if (Index < 0 || Index >= EditorData->Evaluators.Num())
		{
			return FString::Printf(TEXT("! set_properties: Evaluator index %d out of range (has %d evaluators)"), Index, EditorData->Evaluators.Num());
		}
		EditorNode = &EditorData->Evaluators[Index];
		NodeLocation = FString::Printf(TEXT("evaluator[%d]"), Index);
	}
	else if (NodeType.Equals(TEXT("global_task"), ESearchCase::IgnoreCase))
	{
		if (Index < 0 || Index >= EditorData->GlobalTasks.Num())
		{
			return FString::Printf(TEXT("! set_properties: GlobalTask index %d out of range (has %d global tasks)"), Index, EditorData->GlobalTasks.Num());
		}
		EditorNode = &EditorData->GlobalTasks[Index];
		NodeLocation = FString::Printf(TEXT("global_task[%d]"), Index);
	}
	else
	{
		// All other types require a state
		if (StateName.IsEmpty())
		{
			return FString::Printf(TEXT("! set_properties: '%s' requires a 'state' name"), *NodeType);
		}

		UStateTreeState* State = FindStateByName(EditorData, StateName);
		if (!State)
		{
			return FString::Printf(TEXT("! set_properties: State '%s' not found"), *StateName);
		}

		if (NodeType.Equals(TEXT("task"), ESearchCase::IgnoreCase))
		{
			if (Index < 0 || Index >= State->Tasks.Num())
			{
				return FString::Printf(TEXT("! set_properties: Task index %d out of range (state '%s' has %d tasks)"), Index, *StateName, State->Tasks.Num());
			}
			EditorNode = &State->Tasks[Index];
			NodeLocation = FString::Printf(TEXT("task[%d] in state %s"), Index, *StateName);
		}
		else if (NodeType.Equals(TEXT("condition"), ESearchCase::IgnoreCase))
		{
			if (Index < 0 || Index >= State->EnterConditions.Num())
			{
				return FString::Printf(TEXT("! set_properties: Condition index %d out of range (state '%s' has %d enter conditions)"), Index, *StateName, State->EnterConditions.Num());
			}
			EditorNode = &State->EnterConditions[Index];
			NodeLocation = FString::Printf(TEXT("condition[%d] in state %s"), Index, *StateName);
		}
		else if (NodeType.Equals(TEXT("consideration"), ESearchCase::IgnoreCase))
		{
			if (Index < 0 || Index >= State->Considerations.Num())
			{
				return FString::Printf(TEXT("! set_properties: Consideration index %d out of range (state '%s' has %d considerations)"), Index, *StateName, State->Considerations.Num());
			}
			EditorNode = &State->Considerations[Index];
			NodeLocation = FString::Printf(TEXT("consideration[%d] in state %s"), Index, *StateName);
		}
		else
		{
			return FString::Printf(TEXT("! set_properties: Unknown node_type '%s'. Use: task, condition, evaluator, global_task, consideration"), *NodeType);
		}
	}

	if (!EditorNode)
	{
		return TEXT("! set_properties: Could not locate editor node");
	}

	int32 SetCount = 0;
	TArray<FString> PropResults;

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		if (!JsonValue.IsValid())
		{
			PropResults.Add(FString::Printf(TEXT("  ! %s: null value"), *PropName));
			continue;
		}

		// Special handling for ExpressionOperand on the FStateTreeEditorNode itself
		if (PropName.Equals(TEXT("operand"), ESearchCase::IgnoreCase) ||
			PropName.Equals(TEXT("expression_operand"), ESearchCase::IgnoreCase) ||
			PropName.Equals(TEXT("ExpressionOperand"), ESearchCase::IgnoreCase))
		{
			FString OperandStr;
			if (JsonValue->TryGetString(OperandStr))
			{
				if (OperandStr.Equals(TEXT("And"), ESearchCase::IgnoreCase))
				{
					EditorNode->ExpressionOperand = EStateTreeExpressionOperand::And;
					PropResults.Add(FString::Printf(TEXT("  = ExpressionOperand -> And")));
					SetCount++;
				}
				else if (OperandStr.Equals(TEXT("Or"), ESearchCase::IgnoreCase))
				{
					EditorNode->ExpressionOperand = EStateTreeExpressionOperand::Or;
					PropResults.Add(FString::Printf(TEXT("  = ExpressionOperand -> Or")));
					SetCount++;
				}
				else if (OperandStr.Equals(TEXT("Copy"), ESearchCase::IgnoreCase))
				{
					EditorNode->ExpressionOperand = EStateTreeExpressionOperand::Copy;
					PropResults.Add(FString::Printf(TEXT("  = ExpressionOperand -> Copy")));
					SetCount++;
				}
				else
				{
					PropResults.Add(FString::Printf(TEXT("  ! ExpressionOperand: invalid value '%s' (use And, Or, Copy)"), *OperandStr));
				}
			}
			else
			{
				PropResults.Add(TEXT("  ! ExpressionOperand: expected string value"));
			}
			continue;
		}

		// Try Node struct first, then Instance struct
		bool bSet = false;

		// Helper lambda to find and set a property on a given struct/memory pair
		auto TrySetPropertyOnStruct = [&](const UScriptStruct* ScriptStruct, uint8* Memory) -> bool
		{
			if (!ScriptStruct || !Memory)
			{
				return false;
			}

			// Find property - case insensitive
			FProperty* Property = nullptr;
			for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
			{
				if ((*It)->GetName().Equals(PropName, ESearchCase::IgnoreCase))
				{
					Property = *It;
					break;
				}
			}

			if (!Property)
			{
				return false;
			}

			void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Memory);
			if (!ValuePtr)
			{
				PropResults.Add(FString::Printf(TEXT("  ! %s: could not get value pointer"), *PropName));
				return true; // Found but failed
			}

			// Handle enum properties with string values
			if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
			{
				FString StringVal;
				if (JsonValue->TryGetString(StringVal))
				{
					UEnum* Enum = EnumProp->GetEnum();
					if (Enum)
					{
						int64 EnumVal = Enum->GetValueByNameString(StringVal);
						if (EnumVal == INDEX_NONE)
						{
							// Try with enum prefix
							EnumVal = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + StringVal);
						}
						if (EnumVal != INDEX_NONE)
						{
							EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
							PropResults.Add(FString::Printf(TEXT("  = %s -> %s"), *PropName, *StringVal));
							SetCount++;
						}
						else
						{
							PropResults.Add(FString::Printf(TEXT("  ! %s: invalid enum value '%s'"), *PropName, *StringVal));
						}
					}
					return true;
				}
			}

			// Handle TEnumAsByte
			if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
			{
				if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
				{
					FString StringVal;
					if (JsonValue->TryGetString(StringVal))
					{
						int64 EnumVal = Enum->GetValueByNameString(StringVal);
						if (EnumVal == INDEX_NONE)
						{
							EnumVal = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + StringVal);
						}
						if (EnumVal != INDEX_NONE)
						{
							ByteProp->SetIntPropertyValue(ValuePtr, EnumVal);
							PropResults.Add(FString::Printf(TEXT("  = %s -> %s"), *PropName, *StringVal));
							SetCount++;
						}
						else
						{
							PropResults.Add(FString::Printf(TEXT("  ! %s: invalid enum value '%s'"), *PropName, *StringVal));
						}
						return true;
					}
				}
			}

			// For all other types, convert JSON value to string and use ImportText
			FString TextValue;
			if (JsonValue->TryGetString(TextValue))
			{
				// String value - use directly
			}
			else
			{
				double NumValue;
				if (JsonValue->TryGetNumber(NumValue))
				{
					// Format without unnecessary trailing zeros
					if (FMath::IsNearlyEqual(NumValue, FMath::RoundToDouble(NumValue)))
					{
						TextValue = FString::Printf(TEXT("%d"), static_cast<int64>(NumValue));
					}
					else
					{
						TextValue = FString::SanitizeFloat(NumValue);
					}
				}
				else
				{
					bool BoolValue;
					if (JsonValue->TryGetBool(BoolValue))
					{
						TextValue = BoolValue ? TEXT("true") : TEXT("false");
					}
					else
					{
						PropResults.Add(FString::Printf(TEXT("  ! %s: unsupported value type"), *PropName));
						return true;
					}
				}
			}

			const TCHAR* TextPtr = *TextValue;
			if (Property->ImportText_Direct(TextPtr, ValuePtr, nullptr, PPF_None))
			{
				PropResults.Add(FString::Printf(TEXT("  = %s -> %s"), *PropName, *TextValue));
				SetCount++;
			}
			else
			{
				PropResults.Add(FString::Printf(TEXT("  ! %s: failed to set '%s'"), *PropName, *TextValue));
			}
			return true;
		};

		// Try Node struct first
		if (EditorNode->Node.IsValid())
		{
			bSet = TrySetPropertyOnStruct(EditorNode->Node.GetScriptStruct(), EditorNode->Node.GetMutableMemory());
		}

		// If not found on Node, try Instance struct
		if (!bSet && EditorNode->Instance.IsValid())
		{
			bSet = TrySetPropertyOnStruct(EditorNode->Instance.GetScriptStruct(), EditorNode->Instance.GetMutableMemory());
		}

		// If not found on Instance, try InstanceObject (UObject instance data)
		if (!bSet && EditorNode->InstanceObject)
		{
			// Find property on the UObject
			FProperty* Property = nullptr;
			for (TFieldIterator<FProperty> It(EditorNode->InstanceObject->GetClass()); It; ++It)
			{
				if ((*It)->GetName().Equals(PropName, ESearchCase::IgnoreCase))
				{
					Property = *It;
					break;
				}
			}

			if (Property)
			{
				void* ValuePtr = Property->ContainerPtrToValuePtr<void>(EditorNode->InstanceObject);
				if (ValuePtr)
				{
					FString TextValue;
					if (JsonValue->TryGetString(TextValue))
					{
						// String value - use directly
					}
					else
					{
						double NumValue;
						if (JsonValue->TryGetNumber(NumValue))
						{
							if (FMath::IsNearlyEqual(NumValue, FMath::RoundToDouble(NumValue)))
							{
								TextValue = FString::Printf(TEXT("%d"), static_cast<int64>(NumValue));
							}
							else
							{
								TextValue = FString::SanitizeFloat(NumValue);
							}
						}
						else
						{
							bool BoolValue;
							if (JsonValue->TryGetBool(BoolValue))
							{
								TextValue = BoolValue ? TEXT("true") : TEXT("false");
							}
							else
							{
								PropResults.Add(FString::Printf(TEXT("  ! %s: unsupported value type"), *PropName));
								bSet = true;
							}
						}
					}

					if (!bSet)
					{
						const TCHAR* TextPtr = *TextValue;
						if (Property->ImportText_Direct(TextPtr, ValuePtr, EditorNode->InstanceObject, PPF_None))
						{
							PropResults.Add(FString::Printf(TEXT("  = %s -> %s"), *PropName, *TextValue));
							SetCount++;
						}
						else
						{
							PropResults.Add(FString::Printf(TEXT("  ! %s: failed to set '%s'"), *PropName, *TextValue));
						}
						bSet = true;
					}
				}
				else
				{
					bSet = true;
				}
			}
		}

		if (!bSet)
		{
			PropResults.Add(FString::Printf(TEXT("  ! %s: property not found on node or instance"), *PropName));
		}
	}

	FString Output = FString::Printf(TEXT("= Properties on %s: %d set"), *NodeLocation, SetCount);
	for (const FString& R : PropResults)
	{
		Output += TEXT("\n") + R;
	}
	return Output;
}

// ========== Schema Operations ==========

FString FEditStateTreeTool::SetSchema(UStateTree* StateTree, UStateTreeEditorData* EditorData, const FString& SchemaClassName)
{
	// Find schema class
	FString SearchName = SchemaClassName;
	if (!SearchName.StartsWith(TEXT("U")))
	{
		SearchName = TEXT("U") + SearchName;
	}

	UClass* SchemaClass = nullptr;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UStateTreeSchema::StaticClass()) &&
			!It->HasAnyClassFlags(CLASS_Abstract))
		{
			if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(SchemaClassName, ESearchCase::IgnoreCase))
			{
				SchemaClass = *It;
				break;
			}
		}
	}

	if (!SchemaClass)
	{
		return FString::Printf(TEXT("! Schema: Unknown schema class '%s'"), *SchemaClassName);
	}

	// Create and assign schema
	UStateTreeSchema* NewSchema = NewObject<UStateTreeSchema>(EditorData, SchemaClass);
	EditorData->Schema = NewSchema;

	return FString::Printf(TEXT("+ Schema: %s"), *SchemaClass->GetName());
}

// ========== Compilation ==========

bool FEditStateTreeTool::CompileStateTree(UStateTree* StateTree, TArray<FString>& OutErrors)
{
	if (!StateTree)
	{
		OutErrors.Add(TEXT("StateTree is null"));
		return false;
	}

	// Use the editing subsystem to compile
	FStateTreeCompilerLog Log;
	bool bSuccess = UStateTreeEditingSubsystem::CompileStateTree(StateTree, Log);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 6
	// Extract error messages from the compiler log (UE 5.6+)
	TArray<TSharedRef<FTokenizedMessage>> TokenizedMessages = Log.ToTokenizedMessages();
	for (const TSharedRef<FTokenizedMessage>& Msg : TokenizedMessages)
	{
		FString Severity;
		switch (Msg->GetSeverity())
		{
		case EMessageSeverity::Error:
			Severity = TEXT("ERROR");
			break;
		case EMessageSeverity::Warning:
			Severity = TEXT("WARN");
			break;
		case EMessageSeverity::Info:
			Severity = TEXT("INFO");
			break;
		default:
			Severity = TEXT("NOTE");
			break;
		}
		OutErrors.Add(FString::Printf(TEXT("[%s] %s"), *Severity, *Msg->ToText().ToString()));
	}

	// Also validate
	UStateTreeEditingSubsystem::ValidateStateTree(StateTree);
#else
	// UE 5.5: Basic success/failure reporting only
	if (!bSuccess)
	{
		OutErrors.Add(TEXT("[ERROR] StateTree compilation failed. Open in editor for details."));
	}
#endif

	return bSuccess;
}

// ========== Discovery ==========

FString FEditStateTreeTool::ListAvailableTaskTypes(const UStateTreeSchema* Schema)
{
	FString Output;
	const UScriptStruct* TaskBaseStruct = FStateTreeTaskBase::StaticStruct();

	TArray<const UScriptStruct*> Tasks;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;

		// Must inherit from FStateTreeTaskBase
		if (!Struct->IsChildOf(TaskBaseStruct))
		{
			continue;
		}

		// Skip base struct
		if (Struct == TaskBaseStruct)
		{
			continue;
		}

		// Skip hidden structs (engine pattern)
		if (Struct->HasMetaData(TEXT("Hidden")))
		{
			continue;
		}

		// Schema filtering (if provided)
		if (Schema && !Schema->IsStructAllowed(Struct))
		{
			continue;
		}

		Tasks.Add(Struct);
	}

	// Sort alphabetically
	Tasks.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetName() < B.GetName();
	});

	for (const UScriptStruct* Task : Tasks)
	{
		FString Category = Task->GetMetaData(TEXT("Category"));
		FString DisplayName = Task->GetDisplayNameText().ToString();
		Output += FString::Printf(TEXT("- %s"), *Task->GetName());
		if (!DisplayName.IsEmpty() && !DisplayName.Equals(Task->GetName()))
		{
			Output += FString::Printf(TEXT(" (%s)"), *DisplayName);
		}
		if (!Category.IsEmpty())
		{
			Output += FString::Printf(TEXT(" [%s]"), *Category);
		}
		Output += TEXT("\n");
	}

	if (Tasks.Num() == 0)
	{
		Output = TEXT("No task types found.\n");
	}
	else
	{
		Output = FString::Printf(TEXT("Found %d task types:\n"), Tasks.Num()) + Output;
	}

	return Output;
}

FString FEditStateTreeTool::ListAvailableEvaluatorTypes(const UStateTreeSchema* Schema)
{
	FString Output;
	const UScriptStruct* EvaluatorBaseStruct = FStateTreeEvaluatorBase::StaticStruct();

	TArray<const UScriptStruct*> Evaluators;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;

		if (!Struct->IsChildOf(EvaluatorBaseStruct))
		{
			continue;
		}

		if (Struct == EvaluatorBaseStruct)
		{
			continue;
		}

		if (Struct->HasMetaData(TEXT("Hidden")))
		{
			continue;
		}

		if (Schema && !Schema->IsStructAllowed(Struct))
		{
			continue;
		}

		Evaluators.Add(Struct);
	}

	Evaluators.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetName() < B.GetName();
	});

	for (const UScriptStruct* Eval : Evaluators)
	{
		FString Category = Eval->GetMetaData(TEXT("Category"));
		FString DisplayName = Eval->GetDisplayNameText().ToString();
		Output += FString::Printf(TEXT("- %s"), *Eval->GetName());
		if (!DisplayName.IsEmpty() && !DisplayName.Equals(Eval->GetName()))
		{
			Output += FString::Printf(TEXT(" (%s)"), *DisplayName);
		}
		if (!Category.IsEmpty())
		{
			Output += FString::Printf(TEXT(" [%s]"), *Category);
		}
		Output += TEXT("\n");
	}

	if (Evaluators.Num() == 0)
	{
		Output = TEXT("No evaluator types found.\n");
	}
	else
	{
		Output = FString::Printf(TEXT("Found %d evaluator types:\n"), Evaluators.Num()) + Output;
	}

	return Output;
}

FString FEditStateTreeTool::ListAvailableConditionTypes(const UStateTreeSchema* Schema)
{
	FString Output;
	const UScriptStruct* ConditionBaseStruct = FStateTreeConditionBase::StaticStruct();

	TArray<const UScriptStruct*> Conditions;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;

		if (!Struct->IsChildOf(ConditionBaseStruct))
		{
			continue;
		}

		if (Struct == ConditionBaseStruct)
		{
			continue;
		}

		if (Struct->HasMetaData(TEXT("Hidden")))
		{
			continue;
		}

		if (Schema && !Schema->IsStructAllowed(Struct))
		{
			continue;
		}

		Conditions.Add(Struct);
	}

	Conditions.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetName() < B.GetName();
	});

	for (const UScriptStruct* Cond : Conditions)
	{
		FString Category = Cond->GetMetaData(TEXT("Category"));
		FString DisplayName = Cond->GetDisplayNameText().ToString();
		Output += FString::Printf(TEXT("- %s"), *Cond->GetName());
		if (!DisplayName.IsEmpty() && !DisplayName.Equals(Cond->GetName()))
		{
			Output += FString::Printf(TEXT(" (%s)"), *DisplayName);
		}
		if (!Category.IsEmpty())
		{
			Output += FString::Printf(TEXT(" [%s]"), *Category);
		}
		Output += TEXT("\n");
	}

	if (Conditions.Num() == 0)
	{
		Output = TEXT("No condition types found.\n");
	}
	else
	{
		Output = FString::Printf(TEXT("Found %d condition types:\n"), Conditions.Num()) + Output;
	}

	return Output;
}

FString FEditStateTreeTool::ListAvailableConsiderationTypes(const UStateTreeSchema* Schema)
{
	FString Output;
	const UScriptStruct* ConsiderationBaseStruct = FStateTreeConsiderationBase::StaticStruct();

	TArray<const UScriptStruct*> Considerations;

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;

		if (!Struct->IsChildOf(ConsiderationBaseStruct))
		{
			continue;
		}

		if (Struct == ConsiderationBaseStruct)
		{
			continue;
		}

		// Skip hidden structs (engine pattern)
		if (Struct->HasMetaData(TEXT("Hidden")))
		{
			continue;
		}

		// Schema filtering (if provided)
		if (Schema && !Schema->IsStructAllowed(Struct))
		{
			continue;
		}

		Considerations.Add(Struct);
	}

	Considerations.Sort([](const UScriptStruct& A, const UScriptStruct& B)
	{
		return A.GetName() < B.GetName();
	});

	for (const UScriptStruct* Cons : Considerations)
	{
		FString Category = Cons->GetMetaData(TEXT("Category"));
		FString DisplayName = Cons->GetDisplayNameText().ToString();
		Output += FString::Printf(TEXT("- %s"), *Cons->GetName());
		if (!DisplayName.IsEmpty() && !DisplayName.Equals(Cons->GetName()))
		{
			Output += FString::Printf(TEXT(" (%s)"), *DisplayName);
		}
		if (!Category.IsEmpty())
		{
			Output += FString::Printf(TEXT(" [%s]"), *Category);
		}
		Output += TEXT("\n");
	}

	if (Considerations.Num() == 0)
	{
		Output = TEXT("No consideration types found.\n");
	}
	else
	{
		Output = FString::Printf(TEXT("Found %d consideration types:\n"), Considerations.Num()) + Output;
	}

	return Output;
}

FString FEditStateTreeTool::ListAvailableSchemas()
{
	FString Output;
	TArray<UClass*> Schemas;

	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;

		if (!Class->IsChildOf(UStateTreeSchema::StaticClass()))
		{
			continue;
		}

		// Skip abstract classes
		if (Class->HasAnyClassFlags(CLASS_Abstract))
		{
			continue;
		}

		// Skip the base class
		if (Class == UStateTreeSchema::StaticClass())
		{
			continue;
		}

		Schemas.Add(Class);
	}

	Schemas.Sort([](const UClass& A, const UClass& B)
	{
		return A.GetName() < B.GetName();
	});

	for (const UClass* SchemaClass : Schemas)
	{
		FString DisplayName = SchemaClass->GetDisplayNameText().ToString();
		Output += FString::Printf(TEXT("- %s"), *SchemaClass->GetName());
		if (!DisplayName.IsEmpty() && !DisplayName.Equals(SchemaClass->GetName()))
		{
			Output += FString::Printf(TEXT(" (%s)"), *DisplayName);
		}
		Output += TEXT("\n");
	}

	if (Schemas.Num() == 0)
	{
		Output = TEXT("No schema types found.\n");
	}
	else
	{
		Output = FString::Printf(TEXT("Found %d schema types:\n"), Schemas.Num()) + Output;
	}

	return Output;
}
