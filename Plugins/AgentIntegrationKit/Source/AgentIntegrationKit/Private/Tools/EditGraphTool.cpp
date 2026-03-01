// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/EditGraphTool.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/NodeNameRegistry.h"
#include "Tools/NodeSpawnerCache.h"
#include "Tools/NeoStackToolUtils.h"
#include "Tools/FindNodeTool.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Blueprint includes
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintNodeSpawner.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_PromotableOperator.h"
#include "BlueprintTypePromotion.h"
#include "EdGraphNode_Comment.h"
#include "K2Node_AddPinInterface.h"

// Animation Blueprint
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNodeBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "AnimationTransitionGraph.h"
#include "AnimationCustomTransitionGraph.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "Animation/AnimLayerInterface.h"
#include "K2Node_Composite.h"

// Material
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MaterialEditorUtilities.h"
#include "IMaterialEditor.h"
#include "MaterialShared.h"
#include "UObject/UnrealType.h"
#include "RHI.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UObjectIterator.h"

// Universal schema action system
#include "EdGraph/EdGraphSchema.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintVariableNodeSpawner.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"

// Transaction support for undo/redo
#include "ScopedTransaction.h"

// PCG (Procedural Content Generation) support - only available in UE 5.7+
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGSettings.h"
#include "PCGEditor.h"
#include "Schema/PCGEditorGraphSchema.h"
#include "Schema/PCGEditorGraphSchemaActions.h"
#include "Nodes/PCGEditorGraphNodeBase.h"

// PCG action cache - shared with FindNodeTool, keyed by asset+graph
extern TMap<FString, TMap<FString, TSharedPtr<FEdGraphSchemaAction>>> PCGActionCacheByKey;
#endif // UE 5.7+

// MetaSound support
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphNode.h"

// MetaSound action cache - shared with FindNodeTool, keyed by asset+graph
extern TMap<FString, TMap<FString, TSharedPtr<FEdGraphSchemaAction>>> MetaSoundActionCacheByKey;

static FString MakeGraphActionCacheKey(const FString& AssetPath, const FString& GraphName)
{
	return AssetPath + TEXT("|") + GraphName;
}

static bool JsonValueToImportText(const TSharedPtr<FJsonValue>& Value, FString& OutText)
{
	if (!Value.IsValid())
	{
		return false;
	}

	if (Value->Type == EJson::String)
	{
		OutText = Value->AsString();
		return true;
	}
	if (Value->Type == EJson::Number)
	{
		OutText = FString::SanitizeFloat(Value->AsNumber());
		return true;
	}
	if (Value->Type == EJson::Boolean)
	{
		OutText = Value->AsBool() ? TEXT("True") : TEXT("False");
		return true;
	}
	if (Value->Type == EJson::Array)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayVal = nullptr;
		if (!Value->TryGetArray(ArrayVal) || !ArrayVal)
		{
			return false;
		}

		TArray<FString> Parts;
		for (const TSharedPtr<FJsonValue>& Elem : *ArrayVal)
		{
			FString ElemText;
			if (!JsonValueToImportText(Elem, ElemText))
			{
				return false;
			}
			Parts.Add(ElemText);
		}
		OutText = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
		return true;
	}
	if (Value->Type == EJson::Object)
	{
		const TSharedPtr<FJsonObject>* ObjVal = nullptr;
		if (!Value->TryGetObject(ObjVal) || !ObjVal)
		{
			return false;
		}

		TArray<FString> Parts;
		for (const auto& Pair : (*ObjVal)->Values)
		{
			FString FieldText;
			if (!JsonValueToImportText(Pair.Value, FieldText))
			{
				return false;
			}
			Parts.Add(FString::Printf(TEXT("%s=%s"), *Pair.Key, *FieldText));
		}
		OutText = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
		return true;
	}
	return false;
}

static bool WaitForCondition(TFunctionRef<bool()> Predicate, float TimeoutSeconds = 1.0f, float StepSeconds = 0.05f)
{
	const int32 MaxSteps = FMath::Max(1, FMath::CeilToInt(TimeoutSeconds / StepSeconds));
	for (int32 Step = 0; Step < MaxSteps; ++Step)
	{
		if (Predicate())
		{
			return true;
		}
		FPlatformProcess::Sleep(StepSeconds);
	}
	return Predicate();
}

/**
 * Validate that a newly-spawned node is compatible with its target graph.
 * Catches latent nodes in function graphs, async tasks in non-event graphs, etc.
 * Returns true if the node is valid; returns false and fills OutError if not.
 * Caller is responsible for destroying the node on failure.
 */
static bool ValidateNodeGraphCompatibility(UEdGraphNode* Node, UEdGraph* Graph, FString& OutError)
{
	if (!Node || !Graph)
	{
		return true; // Nothing to validate
	}

	// General compatibility check (covers UK2Node_BaseAsyncTask and other overrides)
	if (!Node->IsCompatibleWithGraph(Graph))
	{
		OutError = FString::Printf(TEXT("Node '%s' is not compatible with this graph. Latent/async nodes can only be placed in Event Graphs, not function graphs."),
			*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		return false;
	}

	// Additional check for UK2Node_CallFunction latent functions
	// IsCompatibleWithGraph is not overridden on UK2Node_CallFunction,
	// so we need to check IsLatentFunction() + graph type explicitly
	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		if (CallNode->IsLatentFunction())
		{
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(Graph->GetSchema());
			if (K2Schema)
			{
				EGraphType GraphType = K2Schema->GetGraphType(Graph);
				if (GraphType != GT_Ubergraph && GraphType != GT_Macro)
				{
					OutError = FString::Printf(TEXT("Latent node '%s' cannot be used in function graphs. Latent nodes (Delay, etc.) can only be placed in Event Graphs."),
						*Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
					return false;
				}
			}
		}
	}

	return true;
}

// BehaviorTree support - functions defined in EditGraphTool_BehaviorTree.cpp
#include "BehaviorTree/BehaviorTree.h"
UEdGraph* EditGraphTool_DetectBTGraph(UObject* Asset, const FString& GraphName, const FString& FullAssetPath, FString& OutActualGraphName, TArray<FString>& Errors);
UEdGraphNode* EditGraphTool_SpawnBTAction(const FString& SpawnerId, UEdGraph* Graph, const FVector2D& Position, FString& OutError);
UEdGraphNode* EditGraphTool_SpawnBTSubNode(const FString& SpawnerId, UEdGraph* Graph, UEdGraphNode* ParentGraphNode, FString& OutError);
UEdGraphNode* EditGraphTool_SpawnBTDecGraphNode(const FString& SpawnerId, UEdGraph* Graph, const FVector2D& Position, FString& OutError);
TArray<FString> EditGraphTool_SetBTNodeValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Values, UBehaviorTree* BT);
void EditGraphTool_FinalizeBTGraph(UEdGraph* Graph, UObject* Asset);
bool EditGraphTool_IsBehaviorTree(UObject* Asset);
bool EditGraphTool_IsBTGraph(UEdGraph* Graph);
UBehaviorTree* EditGraphTool_GetBehaviorTree(UObject* Asset);
UEdGraphNode* EditGraphTool_FindBTSubNode(UEdGraph* Graph, const FString& NodeRef, const FString& AssetPath, const FString& GraphName);

// EQS (Environment Query System) support - functions defined in EditGraphTool_EQS.cpp
#include "EnvironmentQuery/EnvQuery.h"
UEdGraph* EditGraphTool_DetectEQSGraph(UObject* Asset, const FString& GraphName, const FString& FullAssetPath, FString& OutActualGraphName, TArray<FString>& Errors);
UEdGraphNode* EditGraphTool_SpawnEQSOption(const FString& SpawnerId, UEdGraph* Graph, const FVector2D& Position, FString& OutError);
UEdGraphNode* EditGraphTool_SpawnEQSTest(const FString& SpawnerId, UEdGraph* Graph, UEdGraphNode* ParentOptionNode, FString& OutError);
TArray<FString> EditGraphTool_SetEQSNodeValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Values);
void EditGraphTool_FinalizeEQSGraph(UEdGraph* Graph, UObject* Asset);
bool EditGraphTool_IsEnvQuery(UObject* Asset);
bool EditGraphTool_IsEQSGraph(UEdGraph* Graph);
UEnvQuery* EditGraphTool_GetEnvQuery(UObject* Asset);
UEdGraphNode* EditGraphTool_FindEQSSubNode(UEdGraph* Graph, const FString& NodeRef, const FString& AssetPath, const FString& GraphName);

// Control Rig graph support - functions defined in EditGraphTool_ControlRig.cpp
bool EditGraphTool_IsControlRig(UObject* Asset);
FToolResult EditGraphTool_HandleControlRigGraph(const TSharedPtr<FJsonObject>& Args, const FString& AssetName, const FString& Path);

TSharedPtr<FJsonObject> FEditGraphTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> AssetProp = MakeShared<FJsonObject>();
	AssetProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetProp->SetStringField(TEXT("description"), TEXT("Asset name or path (Blueprint, Material, AnimBP, PCGGraph, MetaSoundSource, MetaSoundPatch, BehaviorTree, EnvironmentQuery, ControlRig)"));
	Properties->SetObjectField(TEXT("asset"), AssetProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> GraphNameProp = MakeShared<FJsonObject>();
	GraphNameProp->SetStringField(TEXT("type"), TEXT("string"));
	GraphNameProp->SetStringField(TEXT("description"),
		TEXT("Target graph name. Accepts raw graph name (e.g., EventGraph, AnimGraph) or typed selector for AnimBPs: "
		     "animgraph:<GraphName>, statemachine:<AnimGraph>/<StateMachine>, state:<AnimGraph>/<State>, "
		     "transition:<AnimGraph>/<From->To>, custom_transition:<AnimGraph>/<From->To>, conduit:<AnimGraph>/<Conduit>, "
		     "composite:<AnimGraph>/<Composite>, animlayer:<LayerFunctionName> (targets layer function graph from AnimLayerInterface). "
		     "For BehaviorTrees: decorator:<CompositeDecoratorName> to target a decorator sub-graph."));
	Properties->SetObjectField(TEXT("graph_name"), GraphNameProp);

	TSharedPtr<FJsonObject> OperationProp = MakeShared<FJsonObject>();
	OperationProp->SetStringField(TEXT("type"), TEXT("string"));
	OperationProp->SetStringField(TEXT("description"), TEXT("Optional mode. Use 'find_nodes' or 'search_nodes' to run integrated node discovery."));
	Properties->SetObjectField(TEXT("operation"), OperationProp);

	TSharedPtr<FJsonObject> QueryProp = MakeShared<FJsonObject>();
	QueryProp->SetStringField(TEXT("type"), TEXT("array"));
	QueryProp->SetStringField(TEXT("description"), TEXT("Node search queries for discovery mode. Example: ['Print String', 'SetTimerByFunctionName']."));
	TSharedPtr<FJsonObject> QueryItems = MakeShared<FJsonObject>();
	QueryItems->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetObjectField(TEXT("items"), QueryItems);
	Properties->SetObjectField(TEXT("query"), QueryProp);

	TSharedPtr<FJsonObject> CategoryProp = MakeShared<FJsonObject>();
	CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	CategoryProp->SetStringField(TEXT("description"), TEXT("Category filter for discovery mode."));
	Properties->SetObjectField(TEXT("category"), CategoryProp);

	TSharedPtr<FJsonObject> InputTypeProp = MakeShared<FJsonObject>();
	InputTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	InputTypeProp->SetStringField(TEXT("description"), TEXT("Input pin type filter for discovery mode."));
	Properties->SetObjectField(TEXT("input_type"), InputTypeProp);

	TSharedPtr<FJsonObject> OutputTypeProp = MakeShared<FJsonObject>();
	OutputTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	OutputTypeProp->SetStringField(TEXT("description"), TEXT("Output pin type filter for discovery mode."));
	Properties->SetObjectField(TEXT("output_type"), OutputTypeProp);

	TSharedPtr<FJsonObject> SearchClassProp = MakeShared<FJsonObject>();
	SearchClassProp->SetStringField(TEXT("type"), TEXT("string"));
	SearchClassProp->SetStringField(TEXT("description"), TEXT("Class-scoped discovery target (same behavior as legacy find_node search_class)."));
	Properties->SetObjectField(TEXT("search_class"), SearchClassProp);

	TSharedPtr<FJsonObject> AddNodesProp = MakeShared<FJsonObject>();
	AddNodesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddNodesProp->SetStringField(TEXT("description"), TEXT("Nodes to add: [{id (spawner ID from edit_graph discovery), name (friendly ref), position (required {x, y} — graph coordinates for node placement; use read_asset to see existing node positions), bind_to (optional component/object property for member calls), parent (required for BT_SUBNODE: decorators/services — node ref of parent), pins:{pin_name: value}}]"));
	Properties->SetObjectField(TEXT("add_nodes"), AddNodesProp);

	TSharedPtr<FJsonObject> ConnectionsProp = MakeShared<FJsonObject>();
	ConnectionsProp->SetStringField(TEXT("type"), TEXT("array"));
	ConnectionsProp->SetStringField(TEXT("description"), TEXT("Connections to create: ['NodeRef:PinName->NodeRef:PinName', ...]"));
	TSharedPtr<FJsonObject> ConnItems = MakeShared<FJsonObject>();
	ConnItems->SetStringField(TEXT("type"), TEXT("string"));
	ConnectionsProp->SetObjectField(TEXT("items"), ConnItems);
	Properties->SetObjectField(TEXT("connections"), ConnectionsProp);

	TSharedPtr<FJsonObject> DisconnectProp = MakeShared<FJsonObject>();
	DisconnectProp->SetStringField(TEXT("type"), TEXT("array"));
	DisconnectProp->SetStringField(TEXT("description"), TEXT("Connections to break: ['NodeRef:PinName->NodeRef:PinName', ...]"));
	TSharedPtr<FJsonObject> DiscItems = MakeShared<FJsonObject>();
	DiscItems->SetStringField(TEXT("type"), TEXT("string"));
	DisconnectProp->SetObjectField(TEXT("items"), DiscItems);
	Properties->SetObjectField(TEXT("disconnect"), DisconnectProp);

	TSharedPtr<FJsonObject> SetPinsProp = MakeShared<FJsonObject>();
	SetPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetPinsProp->SetStringField(TEXT("description"), TEXT("Set values on existing nodes: [{node (name or GUID), values:{pin_name: value}}]"));
	Properties->SetObjectField(TEXT("set_pins"), SetPinsProp);

	TSharedPtr<FJsonObject> MoveNodesProp = MakeShared<FJsonObject>();
	MoveNodesProp->SetStringField(TEXT("type"), TEXT("array"));
	MoveNodesProp->SetStringField(TEXT("description"), TEXT("Move existing nodes: [{node, x, y}] for absolute placement or [{node, dx, dy}] for relative move."));
	Properties->SetObjectField(TEXT("move_nodes"), MoveNodesProp);

	TSharedPtr<FJsonObject> AlignNodesProp = MakeShared<FJsonObject>();
	AlignNodesProp->SetStringField(TEXT("type"), TEXT("array"));
	AlignNodesProp->SetStringField(TEXT("description"), TEXT("Align existing nodes: [{nodes:[...], axis:'x'|'y', mode:'min'|'max'|'center'}]."));
	Properties->SetObjectField(TEXT("align_nodes"), AlignNodesProp);

	TSharedPtr<FJsonObject> LayoutNodesProp = MakeShared<FJsonObject>();
	LayoutNodesProp->SetStringField(TEXT("type"), TEXT("array"));
	LayoutNodesProp->SetStringField(TEXT("description"), TEXT("Auto-layout node groups on a grid: [{nodes:[...], start:{x,y}?, spacing:{x,y}?, columns?}]."));
	Properties->SetObjectField(TEXT("layout_nodes"), LayoutNodesProp);

	TSharedPtr<FJsonObject> DeleteNodesProp = MakeShared<FJsonObject>();
	DeleteNodesProp->SetStringField(TEXT("type"), TEXT("array"));
	DeleteNodesProp->SetStringField(TEXT("description"), TEXT("Nodes to delete by name or GUID: ['NodeName', 'node_guid', ...]"));
	TSharedPtr<FJsonObject> DeleteItems = MakeShared<FJsonObject>();
	DeleteItems->SetStringField(TEXT("type"), TEXT("string"));
	DeleteNodesProp->SetObjectField(TEXT("items"), DeleteItems);
	Properties->SetObjectField(TEXT("delete_nodes"), DeleteNodesProp);

	TSharedPtr<FJsonObject> AddCommentsProp = MakeShared<FJsonObject>();
	AddCommentsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddCommentsProp->SetStringField(TEXT("description"),
		TEXT("Comment boxes to add: [{text, nodes?:[\"NodeRef\",...], position?:{x,y}, size?:{w,h}, color?:\"#RRGGBB\"}]. "
		     "If 'nodes' provided, auto-wraps those nodes. If 'position' provided, places at exact coordinates. "
		     "Both can be omitted (smart positioning). 'nodes' takes priority over 'position'."));
	Properties->SetObjectField(TEXT("add_comments"), AddCommentsProp);

	TSharedPtr<FJsonObject> SplitPinsProp = MakeShared<FJsonObject>();
	SplitPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	SplitPinsProp->SetStringField(TEXT("description"),
		TEXT("Pins to split into sub-pins: [\"NodeRef:PinName\",...]. "
		     "Splits struct pins (Vector->X/Y/Z, Rotator->Pitch/Yaw/Roll, Color->R/G/B/A, etc.). "
		     "Only works on struct-type pins that aren't already split."));
	Properties->SetObjectField(TEXT("split_pins"), SplitPinsProp);

	TSharedPtr<FJsonObject> RecombinePinsProp = MakeShared<FJsonObject>();
	RecombinePinsProp->SetStringField(TEXT("type"), TEXT("array"));
	RecombinePinsProp->SetStringField(TEXT("description"),
		TEXT("Split pins to recombine back: [\"NodeRef:PinName\",...]. "
		     "Pass either the parent pin name or any sub-pin name (e.g. NodeRef:Location or NodeRef:Location_X)."));
	Properties->SetObjectField(TEXT("recombine_pins"), RecombinePinsProp);

	TSharedPtr<FJsonObject> AddExecPinsProp = MakeShared<FJsonObject>();
	AddExecPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddExecPinsProp->SetStringField(TEXT("description"),
		TEXT("Add input/exec pins to nodes that support dynamic pins: [\"NodeRef\",...]. "
		     "Works with Sequence, MakeArray, MakeSet, MakeMap, Select, and similar nodes."));
	Properties->SetObjectField(TEXT("add_exec_pins"), AddExecPinsProp);

	TSharedPtr<FJsonObject> RemoveExecPinsProp = MakeShared<FJsonObject>();
	RemoveExecPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveExecPinsProp->SetStringField(TEXT("description"),
		TEXT("Remove the last dynamic pin from nodes: [\"NodeRef\",...]. "
		     "Removes the last removable pin from Sequence, MakeArray, etc."));
	Properties->SetObjectField(TEXT("remove_exec_pins"), RemoveExecPinsProp);

	TSharedPtr<FJsonObject> ConvertPinTypeProp = MakeShared<FJsonObject>();
	ConvertPinTypeProp->SetStringField(TEXT("type"), TEXT("array"));
	ConvertPinTypeProp->SetStringField(TEXT("description"),
		TEXT("Convert pin types on promotable operator nodes (Add, Multiply, Greater, etc.): "
		     "[{\"node\": \"NodeRef\", \"pin\": \"PinName\", \"type\": \"Integer\"}]. "
		     "Rarely needed — pin types auto-resolve on first connection. Only use when you need a specific type before connecting. "
		     "Omit 'type' to list available conversions for that pin. "
		     "Common types: Integer, Integer64, Float (single-precision), Float (double-precision), Byte, Vector, Rotator, Quat, LinearColor, Timespan. "
		     "Use 'Wildcard' to reset the pin to its default untyped state."));
	Properties->SetObjectField(TEXT("convert_pin_type"), ConvertPinTypeProp);

	TSharedPtr<FJsonObject> AddArrayPinsProp = MakeShared<FJsonObject>();
	AddArrayPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddArrayPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Add array entries to RigVM array pins: [{pin_path, default_value?}]"));
	Properties->SetObjectField(TEXT("add_array_pins"), AddArrayPinsProp);

	TSharedPtr<FJsonObject> InsertArrayPinsProp = MakeShared<FJsonObject>();
	InsertArrayPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	InsertArrayPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Insert array entries: [{pin_path, index, default_value?}]"));
	Properties->SetObjectField(TEXT("insert_array_pins"), InsertArrayPinsProp);

	TSharedPtr<FJsonObject> RemoveArrayPinsProp = MakeShared<FJsonObject>();
	RemoveArrayPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveArrayPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Remove array entries: [\"Node.Pin.Array.0\"] or [{pin_path}]"));
	Properties->SetObjectField(TEXT("remove_array_pins"), RemoveArrayPinsProp);

	TSharedPtr<FJsonObject> BindPinVariablesProp = MakeShared<FJsonObject>();
	BindPinVariablesProp->SetStringField(TEXT("type"), TEXT("array"));
	BindPinVariablesProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Bind pins to variables: [{pin_path, variable}]"));
	Properties->SetObjectField(TEXT("bind_pin_variables"), BindPinVariablesProp);

	TSharedPtr<FJsonObject> PromotePinsProp = MakeShared<FJsonObject>();
	PromotePinsProp->SetStringField(TEXT("type"), TEXT("array"));
	PromotePinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Promote pins to variables: [{pin_path, create_variable_node?, node_position?}]"));
	Properties->SetObjectField(TEXT("promote_pins"), PromotePinsProp);

	TSharedPtr<FJsonObject> SetPinExpansionProp = MakeShared<FJsonObject>();
	SetPinExpansionProp->SetStringField(TEXT("type"), TEXT("array"));
	SetPinExpansionProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Expand/collapse pins: [{pin_path, expanded}]"));
	Properties->SetObjectField(TEXT("set_pin_expansion"), SetPinExpansionProp);

	TSharedPtr<FJsonObject> AddExposedPinsProp = MakeShared<FJsonObject>();
	AddExposedPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddExposedPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Add graph exposed pins: [{name, direction?, cpp_type, cpp_type_object_path?, default_value?}]"));
	Properties->SetObjectField(TEXT("add_exposed_pins"), AddExposedPinsProp);

	TSharedPtr<FJsonObject> RemoveExposedPinsProp = MakeShared<FJsonObject>();
	RemoveExposedPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveExposedPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Remove exposed pins: [\"PinName\"] or [{name}]"));
	Properties->SetObjectField(TEXT("remove_exposed_pins"), RemoveExposedPinsProp);

	TSharedPtr<FJsonObject> RenameExposedPinsProp = MakeShared<FJsonObject>();
	RenameExposedPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	RenameExposedPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Rename exposed pins: [{name, new_name}]"));
	Properties->SetObjectField(TEXT("rename_exposed_pins"), RenameExposedPinsProp);

	TSharedPtr<FJsonObject> ChangeExposedPinTypesProp = MakeShared<FJsonObject>();
	ChangeExposedPinTypesProp->SetStringField(TEXT("type"), TEXT("array"));
	ChangeExposedPinTypesProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Change exposed pin types: [{name, cpp_type, cpp_type_object_path?}]"));
	Properties->SetObjectField(TEXT("change_exposed_pin_types"), ChangeExposedPinTypesProp);

	TSharedPtr<FJsonObject> ReorderExposedPinsProp = MakeShared<FJsonObject>();
	ReorderExposedPinsProp->SetStringField(TEXT("type"), TEXT("array"));
	ReorderExposedPinsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Reorder exposed pins: [{name, index}]"));
	Properties->SetObjectField(TEXT("reorder_exposed_pins"), ReorderExposedPinsProp);

	TSharedPtr<FJsonObject> SetNodeCategoriesProp = MakeShared<FJsonObject>();
	SetNodeCategoriesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetNodeCategoriesProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Set collapse/function node categories: [{node, category}]"));
	Properties->SetObjectField(TEXT("set_node_categories"), SetNodeCategoriesProp);

	TSharedPtr<FJsonObject> SetNodeKeywordsProp = MakeShared<FJsonObject>();
	SetNodeKeywordsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetNodeKeywordsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Set collapse/function node keywords: [{node, keywords}]"));
	Properties->SetObjectField(TEXT("set_node_keywords"), SetNodeKeywordsProp);

	TSharedPtr<FJsonObject> SetNodeDescriptionsProp = MakeShared<FJsonObject>();
	SetNodeDescriptionsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetNodeDescriptionsProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Set collapse/function node descriptions: [{node, description}]"));
	Properties->SetObjectField(TEXT("set_node_descriptions"), SetNodeDescriptionsProp);

	TSharedPtr<FJsonObject> SetPinCategoriesProp = MakeShared<FJsonObject>();
	SetPinCategoriesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetPinCategoriesProp->SetStringField(TEXT("description"), TEXT("Control Rig only. Set/clear RigVM pin categories: [{pin_path, category?}]"));
	Properties->SetObjectField(TEXT("set_pin_categories"), SetPinCategoriesProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditGraphTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// Parse required parameters
	FString AssetName;
	if (!Args->TryGetStringField(TEXT("asset"), AssetName) || AssetName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: asset"));
	}

	// Parse optional path
	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	// Parse graph name
	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	// Integrated node discovery mode (replaces standalone find_node exposure).
	FString Operation;
	Args->TryGetStringField(TEXT("operation"), Operation);
	Operation = Operation.ToLower();

	const bool bHasQuery = Args->HasField(TEXT("query"));
	const bool bHasEditOps =
		Args->HasField(TEXT("add_nodes")) ||
		Args->HasField(TEXT("delete_nodes")) ||
		Args->HasField(TEXT("set_pins")) ||
		Args->HasField(TEXT("move_nodes")) ||
		Args->HasField(TEXT("align_nodes")) ||
		Args->HasField(TEXT("layout_nodes")) ||
		Args->HasField(TEXT("connections")) ||
		Args->HasField(TEXT("disconnect")) ||
		Args->HasField(TEXT("break_links")) ||
		Args->HasField(TEXT("add_comments")) ||
		Args->HasField(TEXT("split_pins")) ||
		Args->HasField(TEXT("recombine_pins")) ||
		Args->HasField(TEXT("add_exec_pins")) ||
		Args->HasField(TEXT("remove_exec_pins")) ||
		Args->HasField(TEXT("convert_pin_type")) ||
		Args->HasField(TEXT("add_array_pins")) ||
		Args->HasField(TEXT("insert_array_pins")) ||
		Args->HasField(TEXT("remove_array_pins")) ||
		Args->HasField(TEXT("bind_pin_variables")) ||
		Args->HasField(TEXT("promote_pins")) ||
		Args->HasField(TEXT("set_pin_expansion")) ||
		Args->HasField(TEXT("add_exposed_pins")) ||
		Args->HasField(TEXT("remove_exposed_pins")) ||
		Args->HasField(TEXT("rename_exposed_pins")) ||
		Args->HasField(TEXT("change_exposed_pin_types")) ||
		Args->HasField(TEXT("reorder_exposed_pins")) ||
		Args->HasField(TEXT("set_node_categories")) ||
		Args->HasField(TEXT("set_node_keywords")) ||
		Args->HasField(TEXT("set_node_descriptions")) ||
		Args->HasField(TEXT("set_pin_categories"));

	if (Operation == TEXT("find_nodes") || Operation == TEXT("search_nodes") || (bHasQuery && !bHasEditOps))
	{
		FString ControlRigAssetPath = NeoStackToolUtils::BuildAssetPath(AssetName, Path);
		if (UObject* MaybeControlRig = LoadObject<UObject>(nullptr, *ControlRigAssetPath))
		{
			if (EditGraphTool_IsControlRig(MaybeControlRig))
			{
				return EditGraphTool_HandleControlRigGraph(Args, AssetName, Path);
			}
		}

		FFindNodeTool FindNodeTool;
		return FindNodeTool.Execute(Args);
	}

	// Build asset path and load
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(AssetName, Path);

	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Create transaction for undo/redo support
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditGraph", "AI Edit Graph: {0}"),
		FText::FromString(AssetName)));

	// Ensure asset editor is open for proper schema initialization
	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			bool bIsAlreadyOpen = AssetEditorSubsystem->FindEditorForAsset(Asset, false) != nullptr;
			if (!bIsAlreadyOpen)
			{
				AssetEditorSubsystem->OpenEditorForAsset(Asset);
				FPlatformProcess::Sleep(0.1f); // Give editor time to initialize
			}
		}
	}

	// Get target graph based on asset type
	UEdGraph* Graph = nullptr;
	UBlueprint* Blueprint = nullptr;
	UMaterial* WorkingMaterial = nullptr; // Track which material we're actually working with

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		// CRITICAL: When the Material Editor is open, it works on a PREVIEW COPY of the material.
		// We must modify the preview material's graph, not the original, otherwise our changes
		// will be lost when the user clicks "Apply" (the editor overwrites original with preview).
		WorkingMaterial = Material;

		if (GEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSubsystem)
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
				if (EditorInstance)
				{
					// The Material Editor implements IMaterialEditor - verify type before static_cast
					if (EditorInstance->GetEditorName() == TEXT("MaterialEditor"))
					{
						IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
						if (MaterialEditor)
						{
							// GetMaterialInterface returns the PREVIEW material that the editor is working on
							UMaterialInterface* PreviewMaterial = MaterialEditor->GetMaterialInterface();
							if (UMaterial* PreviewMat = Cast<UMaterial>(PreviewMaterial))
							{
								WorkingMaterial = PreviewMat;
								UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Using preview material from Material Editor"));
							}
						}
					}
				}
			}
		}

		// Material graph - create if needed
		if (!WorkingMaterial->MaterialGraph)
		{
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(WorkingMaterial, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass());
			UMaterialGraph* MatGraph = Cast<UMaterialGraph>(NewGraph);
			if (!MatGraph)
			{
				return FToolResult::Fail(TEXT("Failed to create material graph"));
			}
			WorkingMaterial->MaterialGraph = MatGraph;
			WorkingMaterial->MaterialGraph->Material = WorkingMaterial;
			WorkingMaterial->MaterialGraph->RebuildGraph();
		}
		Graph = WorkingMaterial->MaterialGraph;
	}
	else if (UMaterialFunction* MaterialFunc = Cast<UMaterialFunction>(Asset))
	{
		// MaterialFunction graph - create if needed
		if (!MaterialFunc->MaterialGraph)
		{
			UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(MaterialFunc, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass());
			UMaterialGraph* MatGraph = Cast<UMaterialGraph>(NewGraph);
			if (!MatGraph)
			{
				return FToolResult::Fail(TEXT("Failed to create material function graph"));
			}
			MaterialFunc->MaterialGraph = MatGraph;
			MaterialFunc->MaterialGraph->MaterialFunction = MaterialFunc;
			MaterialFunc->MaterialGraph->RebuildGraph();
		}
		Graph = MaterialFunc->MaterialGraph;
	}
	else if ((Blueprint = Cast<UBlueprint>(Asset)) != nullptr)
	{
		// Blueprint graph
		Graph = GetGraphByName(Blueprint, GraphName);
		if (!Graph)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
		}
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (Cast<UPCGGraphInterface>(Asset))
	{
		// PCG graph - resolve editor graph from the PCG editor instance.
		if (GEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSubsystem)
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
				WaitForCondition([&]()
				{
					IAssetEditorInstance* CurrentEditor = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
					if (!(CurrentEditor && CurrentEditor->GetEditorName() == TEXT("PCGEditor")))
					{
						return false;
					}

						FPCGEditor* PCGEditor = static_cast<FPCGEditor*>(CurrentEditor);
						Graph = PCGEditor ? reinterpret_cast<UEdGraph*>(PCGEditor->GetPCGEditorGraph()) : nullptr;
						return Graph != nullptr;
					});
			}
		}
		if (!Graph)
		{
			return FToolResult::Fail(TEXT("Could not get PCG editor graph. Make sure the PCG asset is open in the editor."));
		}
	}
#endif // UE 5.7+ PCG support
	else if (Cast<UMetaSoundSource>(Asset) || Cast<UMetaSoundPatch>(Asset))
	{
		if (UMetaSoundSource* MSSource = Cast<UMetaSoundSource>(Asset))
		{
			Graph = MSSource->GetGraph();
		}
		else if (UMetaSoundPatch* MSPatch = Cast<UMetaSoundPatch>(Asset))
		{
			Graph = MSPatch->GetGraph();
		}

		if (!Graph)
		{
			// Graph may be null if editor hasn't been opened yet - try opening it
			if (GEditor)
			{
				UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
				if (AssetEditorSubsystem)
				{
					AssetEditorSubsystem->OpenEditorForAsset(Asset);
					WaitForCondition([&]()
					{
						if (UMetaSoundSource* MSSource = Cast<UMetaSoundSource>(Asset))
						{
							Graph = MSSource->GetGraph();
						}
						else if (UMetaSoundPatch* MSPatch = Cast<UMetaSoundPatch>(Asset))
						{
							Graph = MSPatch->GetGraph();
						}
						return Graph != nullptr;
					});

					// Do not scan all editor graphs globally; rely on asset/editor graph retrieval.
				}
			}
		}

		if (!Graph)
		{
			return FToolResult::Fail(TEXT("Could not get MetaSound editor graph. Make sure the MetaSound asset editor is open."));
		}
	}
	else if (EditGraphTool_IsBehaviorTree(Asset))
	{
		// BehaviorTree — detection and graph setup in EditGraphTool_BehaviorTree.cpp
		TArray<FString> BTDetectErrors;
		FString BTGraphName;
		Graph = EditGraphTool_DetectBTGraph(Asset, GraphName, FullAssetPath, BTGraphName, BTDetectErrors);
		if (!Graph)
		{
			FString ErrorMsg = TEXT("Could not get BehaviorTree editor graph.");
			for (const FString& E : BTDetectErrors)
			{
				ErrorMsg += TEXT("\n") + E;
			}
			return FToolResult::Fail(ErrorMsg);
		}
	}
	else if (EditGraphTool_IsEnvQuery(Asset))
	{
		// EQS — detection and graph setup in EditGraphTool_EQS.cpp
		TArray<FString> EQSDetectErrors;
		FString EQSGraphName;
		Graph = EditGraphTool_DetectEQSGraph(Asset, GraphName, FullAssetPath, EQSGraphName, EQSDetectErrors);
		if (!Graph)
		{
			FString ErrorMsg = TEXT("Could not get EQS editor graph.");
			for (const FString& E : EQSDetectErrors)
			{
				ErrorMsg += TEXT("\n") + E;
			}
			return FToolResult::Fail(ErrorMsg);
		}
	}
	else if (EditGraphTool_IsControlRig(Asset))
	{
		return EditGraphTool_HandleControlRigGraph(Args, AssetName, Path);
	}
	else
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName()));
	}

	// Use actual graph name for registry
	FString ActualGraphName = Graph->GetName();

	// Track results
	TArray<FAddedNode> AddedNodes;
	TArray<FString> DeletedNodes;
	TArray<FString> ConnectionResults;
	TArray<FString> DisconnectResults;
	TArray<FString> MoveResults;
	TArray<FString> AlignResults;
	TArray<FString> LayoutResults;
	TArray<FString> SetPinsResults;
	TArray<FString> Errors;

	// Map of new node names to their instances (for connection resolution within this call)
	TMap<FString, UEdGraphNode*> NewNodeMap;

	// Process add_nodes
	const TArray<TSharedPtr<FJsonValue>>* AddNodesArray;
	if (Args->TryGetArrayField(TEXT("add_nodes"), AddNodesArray))
	{
		for (const TSharedPtr<FJsonValue>& NodeValue : *AddNodesArray)
		{
			const TSharedPtr<FJsonObject>* NodeObj;
			if (!NodeValue->TryGetObject(NodeObj))
			{
				Errors.Add(TEXT("Invalid node definition (not an object)"));
				continue;
			}

			FNodeDefinition NodeDef;
			FString ParseError;
			if (!ParseNodeDefinition(*NodeObj, NodeDef, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			IBlueprintNodeBinder::FBindingSet NodeBindings;
			FString BindingError;
			if (!BuildBindingsForNode(NodeDef, Blueprint, NodeBindings, BindingError))
			{
				Errors.Add(BindingError);
				continue;
			}

			// PRIMARY PATH: Check if this is a cached spawner ID (new system)
			// Format: TYPE:BlueprintName:NodeName:UniqueId (e.g., "VAR_GET:BP_Player:Get_Health:42")
			if (FNodeSpawnerCache::IsCacheId(NodeDef.SpawnerId))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached spawner for %s"), *NodeDef.SpawnerId);

				// Invoke the spawner directly via cache at agent-specified position
				UEdGraphNode* NewNode = FNodeSpawnerCache::Get().InvokeSpawner(NodeDef.SpawnerId, Graph, NodeDef.Position, NodeBindings);

				if (NewNode)
				{
					// Validate node is compatible with this graph (e.g., no latent nodes in function graphs)
					FString CompatError;
					if (!ValidateNodeGraphCompatibility(NewNode, Graph, CompatError))
					{
						Graph->GetSchema()->BreakNodeLinks(*NewNode);
						NewNode->DestroyNode();
						Errors.Add(CompatError);
						continue;
					}

					// Set pin values
					TArray<FString> PinValueResults;
					if (NodeDef.Pins.IsValid())
					{
						PinValueResults = SetPinValues(NewNode, NodeDef.Pins);
					}

					// Generate name if not provided
					FString NodeName = NodeDef.Name;
					if (NodeName.IsEmpty())
					{
						NodeName = FString::Printf(TEXT("%s_%s"),
							*GetNodeTypeName(NewNode),
							*NewNode->NodeGuid.ToString().Left(8));
					}

					// Register in session registry
					FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);

					// Track in local map for this call's connections
					NewNodeMap.Add(NodeName, NewNode);

					// Track result
					FAddedNode Added;
					Added.Name = NodeName;
					Added.NodeType = GetNodeTypeName(NewNode);
					Added.Guid = NewNode->NodeGuid;
					Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
					Added.PinValues = PinValueResults;

					for (UEdGraphPin* Pin : NewNode->Pins)
					{
						if (Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin)
						{
							if (Pin->Direction == EGPD_Input)
							{
								Added.InputPins.Add(Pin->PinName.ToString());
							}
							else if (Pin->Direction == EGPD_Output)
							{
								Added.OutputPins.Add(Pin->PinName.ToString());
							}
						}
					}

					AddedNodes.Add(Added);
					continue;
				}
				else
				{
					// Cache lookup failed - could be expired or invalid
					UE_LOG(LogAgentIntegrationKit, Warning, TEXT("[AIK] EditGraph: Cache lookup failed for %s, trying legacy path"),
						*NodeDef.SpawnerId);
					// Fall through to legacy logic
				}
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			// PCG ACTION PATH: Check if this is a cached PCG schema action
			// Format: PCG_ACTION:Index:NodeName (from edit_graph discovery)
			if (NodeDef.SpawnerId.StartsWith(TEXT("PCG_ACTION:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached PCG action for %s"), *NodeDef.SpawnerId);

				const FString CacheKey = MakeGraphActionCacheKey(FullAssetPath, Graph->GetName());
				const TMap<FString, TSharedPtr<FEdGraphSchemaAction>>* PCGActionCache = PCGActionCacheByKey.Find(CacheKey);
				const TSharedPtr<FEdGraphSchemaAction>* CachedAction = PCGActionCache ? PCGActionCache->Find(NodeDef.SpawnerId) : nullptr;
				if (CachedAction && CachedAction->IsValid())
				{
					// Spawn node using PerformAction at agent-specified position
					TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
					UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, NodeDef.Position, true);
#else
					UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2f(NodeDef.Position.X, NodeDef.Position.Y), true);
#endif

					if (NewNode)
					{
						// Keep transactional for undo/redo; schema action already handled placement lifecycle.
						NewNode->SetFlags(RF_Transactional);

						// Set pin values
						TArray<FString> PinValueResults;
						if (NodeDef.Pins.IsValid())
						{
							PinValueResults = SetPinValues(NewNode, NodeDef.Pins);
						}

						// Generate name if not provided
						FString NodeName = NodeDef.Name;
						if (NodeName.IsEmpty())
						{
							NodeName = FString::Printf(TEXT("%s_%s"),
								*GetNodeTypeName(NewNode),
								*NewNode->NodeGuid.ToString().Left(8));
						}

						// Register in session registry
						FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);

						// Track in local map for this call's connections
						NewNodeMap.Add(NodeName, NewNode);

						// Track result
						FAddedNode Added;
						Added.Name = NodeName;
						Added.NodeType = GetNodeTypeName(NewNode);
						Added.Guid = NewNode->NodeGuid;
						Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
						Added.PinValues = PinValueResults;

						for (UEdGraphPin* Pin : NewNode->Pins)
						{
							if (Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin)
							{
								if (Pin->Direction == EGPD_Input)
								{
									Added.InputPins.Add(Pin->PinName.ToString());
								}
								else if (Pin->Direction == EGPD_Output)
								{
									Added.OutputPins.Add(Pin->PinName.ToString());
								}
							}
						}

						AddedNodes.Add(Added);
						continue;
					}
					else
					{
						Errors.Add(FString::Printf(TEXT("PCG action PerformAction failed: %s"), *NodeDef.SpawnerId));
						continue;
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("PCG action not found in cache: %s. Run edit_graph with operation='find_nodes' on this exact asset/graph first."), *NodeDef.SpawnerId));
					continue;
				}
			}
#endif // UE 5.7+ PCG support

			/// METASOUND ACTION PATH: Check if this is a cached MetaSound schema action
			// Format: MS_ACTION:Index:NodeName (from edit_graph discovery)
			if (NodeDef.SpawnerId.StartsWith(TEXT("MS_ACTION:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached MetaSound action for %s"), *NodeDef.SpawnerId);

				const FString CacheKey = MakeGraphActionCacheKey(FullAssetPath, Graph->GetName());
				const TMap<FString, TSharedPtr<FEdGraphSchemaAction>>* MetaSoundActionCache = MetaSoundActionCacheByKey.Find(CacheKey);
				const TSharedPtr<FEdGraphSchemaAction>* CachedAction = MetaSoundActionCache ? MetaSoundActionCache->Find(NodeDef.SpawnerId) : nullptr;
				if (CachedAction && CachedAction->IsValid())
				{
					// Spawn MetaSound node at agent-specified position
					TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
					UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, NodeDef.Position, true);
#else
					UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2f(NodeDef.Position.X, NodeDef.Position.Y), true);
#endif

					if (NewNode)
					{
						// Keep transactional for undo/redo; schema action already handled placement lifecycle.
						NewNode->SetFlags(RF_Transactional);

						TArray<FString> PinValueResults;
						if (NodeDef.Pins.IsValid())
						{
							PinValueResults = SetPinValues(NewNode, NodeDef.Pins);
						}

						FString NodeName = NodeDef.Name;
						if (NodeName.IsEmpty())
						{
							NodeName = FString::Printf(TEXT("%s_%s"),
								*GetNodeTypeName(NewNode),
								*NewNode->NodeGuid.ToString().Left(8));
						}

						FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);
						NewNodeMap.Add(NodeName, NewNode);

						FAddedNode Added;
						Added.Name = NodeName;
						Added.NodeType = GetNodeTypeName(NewNode);
						Added.Guid = NewNode->NodeGuid;
						Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
						Added.PinValues = PinValueResults;

						for (UEdGraphPin* Pin : NewNode->Pins)
						{
							if (Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin)
							{
								if (Pin->Direction == EGPD_Input)
								{
									Added.InputPins.Add(Pin->PinName.ToString());
								}
								else if (Pin->Direction == EGPD_Output)
								{
									Added.OutputPins.Add(Pin->PinName.ToString());
								}
							}
						}

						AddedNodes.Add(Added);
						continue;
					}
					else
					{
						Errors.Add(FString::Printf(TEXT("MetaSound action PerformAction failed: %s"), *NodeDef.SpawnerId));
						continue;
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("MetaSound action not found in cache: %s. Run edit_graph with operation='find_nodes' on this exact asset/graph first."), *NodeDef.SpawnerId));
					continue;
				}
			}

			// BT_ACTION PATH: Cached BehaviorTree composites/tasks
			if (NodeDef.SpawnerId.StartsWith(TEXT("BT_ACTION:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached BT action for %s"), *NodeDef.SpawnerId);

				FString SpawnError;
				UEdGraphNode* NewNode = EditGraphTool_SpawnBTAction(NodeDef.SpawnerId, Graph, NodeDef.Position, SpawnError);
				if (NewNode)
				{
					TArray<FString> PinValueResults;
					if (NodeDef.Pins.IsValid())
					{
						UBehaviorTree* BT = EditGraphTool_GetBehaviorTree(Asset);
						PinValueResults = EditGraphTool_SetBTNodeValues(NewNode, NodeDef.Pins, BT);
					}

					FString NodeName = NodeDef.Name;
					if (NodeName.IsEmpty())
					{
						NodeName = FString::Printf(TEXT("%s_%s"), *GetNodeTypeName(NewNode), *NewNode->NodeGuid.ToString().Left(8));
					}

					FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);
					NewNodeMap.Add(NodeName, NewNode);

					FAddedNode Added;
					Added.Name = NodeName;
					Added.NodeType = GetNodeTypeName(NewNode);
					Added.Guid = NewNode->NodeGuid;
					Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
					Added.PinValues = PinValueResults;

					for (UEdGraphPin* Pin : NewNode->Pins)
					{
						if (Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin)
						{
							if (Pin->Direction == EGPD_Input)
							{
								Added.InputPins.Add(Pin->PinName.ToString());
							}
							else if (Pin->Direction == EGPD_Output)
							{
								Added.OutputPins.Add(Pin->PinName.ToString());
							}
						}
					}

					AddedNodes.Add(Added);
					continue;
				}
				else
				{
					Errors.Add(SpawnError);
					continue;
				}
			}

			// BT_SUBNODE PATH: Cached BehaviorTree decorators/services
			if (NodeDef.SpawnerId.StartsWith(TEXT("BT_SUBNODE:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached BT sub-node action for %s"), *NodeDef.SpawnerId);

				if (NodeDef.Parent.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("BT_SUBNODE requires 'parent' field: %s"), *NodeDef.SpawnerId));
					continue;
				}

				UEdGraphNode* ParentNode = ResolveNodeRef(NodeDef.Parent, Graph, FullAssetPath, NewNodeMap);
				if (!ParentNode)
				{
					Errors.Add(FString::Printf(TEXT("BT_SUBNODE: parent '%s' not found"), *NodeDef.Parent));
					continue;
				}

				FString SpawnError;
				UEdGraphNode* NewNode = EditGraphTool_SpawnBTSubNode(NodeDef.SpawnerId, Graph, ParentNode, SpawnError);
				if (NewNode)
				{
					TArray<FString> PinValueResults;
					if (NodeDef.Pins.IsValid())
					{
						UBehaviorTree* BT = EditGraphTool_GetBehaviorTree(Asset);
						PinValueResults = EditGraphTool_SetBTNodeValues(NewNode, NodeDef.Pins, BT);
					}

					FString NodeName = NodeDef.Name;
					if (NodeName.IsEmpty())
					{
						NodeName = FString::Printf(TEXT("%s_%s"), *GetNodeTypeName(NewNode), *NewNode->NodeGuid.ToString().Left(8));
					}

					FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);
					NewNodeMap.Add(NodeName, NewNode);

					FAddedNode Added;
					Added.Name = NodeName;
					Added.NodeType = GetNodeTypeName(NewNode);
					Added.Guid = NewNode->NodeGuid;
					Added.Position = FVector2D(0, 0); // Sub-nodes don't have independent positions
					Added.PinValues = PinValueResults;
					AddedNodes.Add(Added);
					continue;
				}
				else
				{
					Errors.Add(SpawnError);
					continue;
				}
			}

			// BT_DECGRAPH PATH: Cached BehaviorTree decorator sub-graph logic nodes
			if (NodeDef.SpawnerId.StartsWith(TEXT("BT_DECGRAPH:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached BT decorator graph action for %s"), *NodeDef.SpawnerId);

				FString SpawnError;
				UEdGraphNode* NewNode = EditGraphTool_SpawnBTDecGraphNode(NodeDef.SpawnerId, Graph, NodeDef.Position, SpawnError);
				if (NewNode)
				{
					FString NodeName = NodeDef.Name;
					if (NodeName.IsEmpty())
					{
						NodeName = FString::Printf(TEXT("%s_%s"), *GetNodeTypeName(NewNode), *NewNode->NodeGuid.ToString().Left(8));
					}

					FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);
					NewNodeMap.Add(NodeName, NewNode);

					FAddedNode Added;
					Added.Name = NodeName;
					Added.NodeType = GetNodeTypeName(NewNode);
					Added.Guid = NewNode->NodeGuid;
					Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);

					for (UEdGraphPin* Pin : NewNode->Pins)
					{
						if (Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin)
						{
							if (Pin->Direction == EGPD_Input)
							{
								Added.InputPins.Add(Pin->PinName.ToString());
							}
							else if (Pin->Direction == EGPD_Output)
							{
								Added.OutputPins.Add(Pin->PinName.ToString());
							}
						}
					}

					AddedNodes.Add(Added);
					continue;
				}
				else
				{
					Errors.Add(SpawnError);
					continue;
				}
			}

			// EQS_OPTION PATH: Cached EQS generators (main graph nodes)
			if (NodeDef.SpawnerId.StartsWith(TEXT("EQS_OPTION:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached EQS option for %s"), *NodeDef.SpawnerId);

				FString SpawnError;
				UEdGraphNode* NewNode = EditGraphTool_SpawnEQSOption(NodeDef.SpawnerId, Graph, NodeDef.Position, SpawnError);
				if (NewNode)
				{
					TArray<FString> PinValueResults;
					if (NodeDef.Pins.IsValid())
					{
						PinValueResults = EditGraphTool_SetEQSNodeValues(NewNode, NodeDef.Pins);
					}

					FString NodeName = NodeDef.Name;
					if (NodeName.IsEmpty())
					{
						NodeName = FString::Printf(TEXT("%s_%s"), *GetNodeTypeName(NewNode), *NewNode->NodeGuid.ToString().Left(8));
					}

					FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);
					NewNodeMap.Add(NodeName, NewNode);

					FAddedNode Added;
					Added.Name = NodeName;
					Added.NodeType = GetNodeTypeName(NewNode);
					Added.Guid = NewNode->NodeGuid;
					Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
					Added.PinValues = PinValueResults;

					for (UEdGraphPin* Pin : NewNode->Pins)
					{
						if (Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin)
						{
							if (Pin->Direction == EGPD_Input)
							{
								Added.InputPins.Add(Pin->PinName.ToString());
							}
							else if (Pin->Direction == EGPD_Output)
							{
								Added.OutputPins.Add(Pin->PinName.ToString());
							}
						}
					}

					AddedNodes.Add(Added);
					continue;
				}
				else
				{
					Errors.Add(SpawnError);
					continue;
				}
			}

			// EQS_TEST PATH: Cached EQS tests (sub-nodes on option nodes)
			if (NodeDef.SpawnerId.StartsWith(TEXT("EQS_TEST:")))
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] EditGraph: Using cached EQS test for %s"), *NodeDef.SpawnerId);

				if (NodeDef.Parent.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("EQS_TEST requires 'parent' field: %s"), *NodeDef.SpawnerId));
					continue;
				}

				UEdGraphNode* ParentNode = ResolveNodeRef(NodeDef.Parent, Graph, FullAssetPath, NewNodeMap);
				if (!ParentNode)
				{
					Errors.Add(FString::Printf(TEXT("EQS_TEST: parent '%s' not found"), *NodeDef.Parent));
					continue;
				}

				FString SpawnError;
				UEdGraphNode* NewNode = EditGraphTool_SpawnEQSTest(NodeDef.SpawnerId, Graph, ParentNode, SpawnError);
				if (NewNode)
				{
					TArray<FString> PinValueResults;
					if (NodeDef.Pins.IsValid())
					{
						PinValueResults = EditGraphTool_SetEQSNodeValues(NewNode, NodeDef.Pins);
					}

					FString NodeName = NodeDef.Name;
					if (NodeName.IsEmpty())
					{
						NodeName = FString::Printf(TEXT("%s_%s"), *GetNodeTypeName(NewNode), *NewNode->NodeGuid.ToString().Left(8));
					}

					FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);
					NewNodeMap.Add(NodeName, NewNode);

					FAddedNode Added;
					Added.Name = NodeName;
					Added.NodeType = GetNodeTypeName(NewNode);
					Added.Guid = NewNode->NodeGuid;
					Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
					Added.PinValues = PinValueResults;
					AddedNodes.Add(Added);
					continue;
				}
				else
				{
					Errors.Add(SpawnError);
					continue;
				}
			}

			// FALLBACK PATH: Legacy VARGET:/VARSET:, GUID, and string matching
			// Find action using UNIVERSAL schema approach
			TSharedPtr<FEdGraphSchemaAction> FoundAction;
			const UEdGraphSchema* Schema = Graph->GetSchema();

			if (Blueprint)
			{
				// Blueprint-specific action database
				FBlueprintActionContext FilterContext;
				FilterContext.Blueprints.Add(Blueprint);
				FilterContext.Graphs.Add(Graph);

				FBlueprintActionMenuBuilder MenuBuilder(FBlueprintActionMenuBuilder::DefaultConfig);
				uint32 ClassTargetMask = EContextTargetFlags::TARGET_Blueprint |
				                         EContextTargetFlags::TARGET_BlueprintLibraries |
				                         EContextTargetFlags::TARGET_SubComponents |
				                         EContextTargetFlags::TARGET_NonImportedTypes;

				FBlueprintActionMenuUtils::MakeContextMenu(FilterContext, false, ClassTargetMask, MenuBuilder);

				// Check if this is a variable getter/setter ID (VARGET: or VARSET:)
				// These have special handling because UE's spawner signature doesn't distinguish member variables
				bool bIsVarGetterOrSetter = NodeDef.SpawnerId.StartsWith(TEXT("VARGET:")) || NodeDef.SpawnerId.StartsWith(TEXT("VARSET:"));
				FString TargetPropertyPath;
				bool bIsGetter = false;

				if (bIsVarGetterOrSetter)
				{
					bIsGetter = NodeDef.SpawnerId.StartsWith(TEXT("VARGET:"));
					TargetPropertyPath = NodeDef.SpawnerId.Mid(bIsGetter ? 7 : 7); // Skip "VARGET:" or "VARSET:"
					UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Searching for variable %s with property path '%s'"),
						bIsGetter ? TEXT("getter") : TEXT("setter"), *TargetPropertyPath);
				}

				// Try to parse SpawnerId as a GUID (the canonical unique identifier)
				FGuid TargetGuid;
				bool bHasTargetGuid = !bIsVarGetterOrSetter && FGuid::Parse(NodeDef.SpawnerId, TargetGuid);

				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Searching for action with SpawnerId='%s' (IsGuid=%d, IsVar=%d)"),
					*NodeDef.SpawnerId, bHasTargetGuid ? 1 : 0, bIsVarGetterOrSetter ? 1 : 0);

				for (int32 i = 0; i < MenuBuilder.GetNumActions(); i++)
				{
					TSharedPtr<FEdGraphSchemaAction> Action = MenuBuilder.GetSchemaAction(i);
					if (!Action.IsValid())
					{
						continue;
					}

					// Check if this is a FBlueprintActionMenuItem (which wraps UBlueprintNodeSpawner)
					if (Action->GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
					{
						FBlueprintActionMenuItem* BPMenuItem = static_cast<FBlueprintActionMenuItem*>(Action.Get());
						if (const UBlueprintNodeSpawner* Spawner = BPMenuItem->GetRawAction())
						{
							// Special handling for variable getters/setters
							if (bIsVarGetterOrSetter)
							{
								if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
								{
									// Check if it's the right type (getter vs setter)
									bool bSpawnerIsGetter = Spawner->NodeClass && Spawner->NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass());
									if (bSpawnerIsGetter != bIsGetter)
									{
										continue;
									}

									// Match by property path
									if (FProperty const* VarProp = VarSpawner->GetVarProperty())
									{
										FString PropPath = VarProp->GetPathName();
										if (PropPath.Equals(TargetPropertyPath, ESearchCase::IgnoreCase))
										{
											UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: MATCHED variable by property path: %s"), *PropPath);
											FoundAction = Action;
											break;
										}
									}
								}
								continue; // Only check variable spawners for VARGET/VARSET
							}

							// Get the spawner's unique signature GUID
							FGuid SpawnerGuid = Spawner->GetSpawnerSignature().AsGuid();

							if (bHasTargetGuid)
							{
								// Direct GUID match - most reliable
								if (SpawnerGuid == TargetGuid)
								{
									UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: MATCHED by GUID: %s"), *SpawnerGuid.ToString());
									FoundAction = Action;
									break;
								}
							}
							else
							{
								// Legacy non-GUID IDs: strict signature string match only.
								FString SignatureStr = Spawner->GetSpawnerSignature().ToString();
								if (SignatureStr.Equals(NodeDef.SpawnerId, ESearchCase::IgnoreCase))
								{
									UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: MATCHED by Signature: %s"), *SignatureStr);
									FoundAction = Action;
									break;
								}
							}
						}
					}
				}
			}
			else if (Schema)
			{
				// UNIVERSAL schema-based discovery for Materials, etc.
				FGraphContextMenuBuilder ContextMenuBuilder(Graph);
				Schema->GetGraphContextActions(ContextMenuBuilder);

				// For material expressions, SpawnerId is like "/Script/Engine.MaterialExpressionConstant3Vector"
				// We need to find the action that creates this expression class
				FString TargetClassName = NodeDef.SpawnerId;
				// Extract just the class name
				int32 LastDot = INDEX_NONE;
				if (TargetClassName.FindLastChar('.', LastDot))
				{
					TargetClassName = TargetClassName.Mid(LastDot + 1);
				}

				// Find action by matching MaterialExpressionClass
				for (int32 i = 0; i < ContextMenuBuilder.GetNumActions(); i++)
				{
					TSharedPtr<FEdGraphSchemaAction> Action = ContextMenuBuilder.GetSchemaAction(i);
					if (Action.IsValid())
					{
						FString TypeId = Action->GetTypeId().ToString();

						// For material expressions, the action stores the class directly
						if (TypeId == TEXT("FMaterialGraphSchemaAction_NewNode"))
						{
							// Cast to get the MaterialExpressionClass
							FMaterialGraphSchemaAction_NewNode* MaterialAction =
								static_cast<FMaterialGraphSchemaAction_NewNode*>(Action.Get());

							if (MaterialAction && MaterialAction->MaterialExpressionClass)
							{
								FString ActionClassName = MaterialAction->MaterialExpressionClass->GetName();

								// Direct class name match
								if (ActionClassName.Equals(TargetClassName, ESearchCase::IgnoreCase))
								{
									FoundAction = Action;
									break;
								}

								// Also try full path match
								FString ActionClassPath = MaterialAction->MaterialExpressionClass->GetPathName();
								if (ActionClassPath.Equals(NodeDef.SpawnerId, ESearchCase::IgnoreCase))
								{
									FoundAction = Action;
									break;
								}
							}
						}
						else
						{
							// For other non-Blueprint graph types, use strict menu description match.
							FString MenuDesc = Action->GetMenuDescription().ToString();
							if (MenuDesc.Equals(NodeDef.SpawnerId, ESearchCase::IgnoreCase))
							{
								FoundAction = Action;
								break;
							}
						}
					}
				}
			}

			if (!FoundAction.IsValid())
			{
				Errors.Add(FString::Printf(TEXT("Action not found: %s"), *NodeDef.SpawnerId));
				continue;
			}

			// Prefer direct spawner invoke for Blueprint actions when bindings are requested.
			// This follows the editor path for call-on-member nodes.
			UEdGraphNode* NewNode = nullptr;
			if (Blueprint && NodeBindings.Num() > 0 && FoundAction->GetTypeId() == FBlueprintActionMenuItem::StaticGetTypeId())
			{
				if (FBlueprintActionMenuItem* BPMenuItem = static_cast<FBlueprintActionMenuItem*>(FoundAction.Get()))
				{
					if (UBlueprintNodeSpawner* RawSpawner = const_cast<UBlueprintNodeSpawner*>(BPMenuItem->GetRawAction()))
					{
						NewNode = RawSpawner->Invoke(Graph, NodeBindings, NodeDef.Position);
					}
				}
			}

			// UNIVERSAL node creation using PerformAction fallback
			if (!NewNode)
			{
				TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION < 6
				NewNode = FoundAction->PerformAction(Graph, EmptyPins, NodeDef.Position, true);
#else
				NewNode = FoundAction->PerformAction(Graph, EmptyPins, FVector2f(NodeDef.Position.X, NodeDef.Position.Y), true);
#endif
			}
			if (!NewNode)
			{
				Errors.Add(FString::Printf(TEXT("Failed to create node: %s"), *NodeDef.SpawnerId));
				continue;
			}

			// Match engine post-creation flow
			// PerformAction/Invoke already ran placement lifecycle for action-spawned nodes.
			NewNode->SetFlags(RF_Transactional);

			// Validate node is compatible with this graph (e.g., no latent nodes in function graphs)
			{
				FString CompatError;
				if (!ValidateNodeGraphCompatibility(NewNode, Graph, CompatError))
				{
					const UEdGraphSchema* NodeSchema = Graph->GetSchema();
					if (NodeSchema)
					{
						NodeSchema->BreakNodeLinks(*NewNode);
					}
					NewNode->DestroyNode();
					Errors.Add(CompatError);
					continue;
				}
			}

			// Set pin values
			TArray<FString> PinValueResults;
			if (NodeDef.Pins.IsValid())
			{
				PinValueResults = SetPinValues(NewNode, NodeDef.Pins);
			}

			// Generate name if not provided
			FString NodeName = NodeDef.Name;
			if (NodeName.IsEmpty())
			{
				NodeName = FString::Printf(TEXT("%s_%s"),
					*GetNodeTypeName(NewNode),
					*NewNode->NodeGuid.ToString().Left(8));
			}

			// Register in session registry (replaces if exists)
			FNodeNameRegistry::Get().Register(FullAssetPath, ActualGraphName, NodeName, NewNode->NodeGuid);

			// Track in local map for this call's connections
			NewNodeMap.Add(NodeName, NewNode);

			// Track result
			FAddedNode Added;
			Added.Name = NodeName;
			Added.NodeType = GetNodeTypeName(NewNode);
			Added.Guid = NewNode->NodeGuid;
			Added.Position = FVector2D(NewNode->NodePosX, NewNode->NodePosY);
			Added.PinValues = PinValueResults;

			// Capture available pins for AI to know what connections are possible
			// Skip hidden, not-connectable, and orphaned pins
			for (UEdGraphPin* Pin : NewNode->Pins)
			{
				if (Pin->bHidden || Pin->bNotConnectable || Pin->bOrphanedPin) continue;
				if (Pin->Direction == EGPD_Input)
				{
					Added.InputPins.Add(Pin->PinName.ToString());
				}
				else if (Pin->Direction == EGPD_Output)
				{
					Added.OutputPins.Add(Pin->PinName.ToString());
				}
			}

			AddedNodes.Add(Added);
		}
	}

	// Process delete_nodes - remove nodes from graph
	const TArray<TSharedPtr<FJsonValue>>* DeleteNodesArray;
	if (Args->TryGetArrayField(TEXT("delete_nodes"), DeleteNodesArray))
	{
		for (const TSharedPtr<FJsonValue>& DeleteValue : *DeleteNodesArray)
		{
			FString NodeRef;
			if (!DeleteValue->TryGetString(NodeRef))
			{
				Errors.Add(TEXT("Invalid delete_nodes entry (not a string)"));
				continue;
			}

			// Resolve the node reference (name or GUID)
			UEdGraphNode* TargetNode = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!TargetNode && EditGraphTool_IsBTGraph(Graph))
			{
				// BT sub-nodes (decorators/services) aren't in Graph->Nodes — search parent SubNodes
				TargetNode = EditGraphTool_FindBTSubNode(Graph, NodeRef, FullAssetPath, ActualGraphName);
			}
			if (!TargetNode && EditGraphTool_IsEQSGraph(Graph))
			{
				// EQS tests aren't in Graph->Nodes — search Option SubNodes
				TargetNode = EditGraphTool_FindEQSSubNode(Graph, NodeRef, FullAssetPath, ActualGraphName);
			}
			if (!TargetNode)
			{
				Errors.Add(FString::Printf(TEXT("Node not found for deletion: %s"), *NodeRef));
				continue;
			}

			// Get node info before deletion for reporting
			FString NodeName = TargetNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			FString NodeGuid = TargetNode->NodeGuid.ToString();

			// Check if node can be deleted (some nodes like entry points cannot be deleted)
			if (!TargetNode->CanUserDeleteNode())
			{
				Errors.Add(FString::Printf(TEXT("Cannot delete node (protected): %s"), *NodeName));
				continue;
			}

			// Follow engine's node removal flow (FBlueprintEditorUtils::RemoveNode)
			// 1. Mark graph modified for undo/redo
			Graph->Modify();

			// 2. Clean up debugger state if this is a Blueprint graph
			UBlueprint* OwnerBP = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
			if (OwnerBP)
			{
				FKismetDebugUtilities::RemoveBreakpointFromNode(TargetNode, OwnerBP);
				for (int32 PinIdx = 0; PinIdx < TargetNode->Pins.Num(); ++PinIdx)
				{
					FKismetDebugUtilities::RemovePinWatch(OwnerBP, TargetNode->Pins[PinIdx]);
				}
			}

			// 3. Mark node modified
			TargetNode->Modify();

			// 4. Break links using Schema (notifies connected nodes via PinConnectionListChanged)
			const UEdGraphSchema* NodeSchema = Graph->GetSchema();
			if (NodeSchema)
			{
				NodeSchema->BreakNodeLinks(*TargetNode);
			}

			// 5. Destroy the node (engine pattern - handles cleanup properly)
			TargetNode->DestroyNode();

			// Unregister from our name registry
			FNodeNameRegistry::Get().Unregister(FullAssetPath, ActualGraphName, NodeRef);

			// Remove from NewNodeMap if it was just added
			NewNodeMap.Remove(NodeRef);

			DeletedNodes.Add(FString::Printf(TEXT("%s (GUID: %s)"), *NodeName, *NodeGuid.Left(8)));
		}
	}

	// Process disconnect BEFORE connections - exec output pins only allow one outgoing
	// connection, so disconnecting first ensures new connections can be made in the same call
	const TArray<TSharedPtr<FJsonValue>>* DisconnectArray;
	if (Args->TryGetArrayField(TEXT("disconnect"), DisconnectArray))
	{
		for (const TSharedPtr<FJsonValue>& DisconnValue : *DisconnectArray)
		{
			FString DisconnStr;
			if (!DisconnValue->TryGetString(DisconnStr))
			{
				Errors.Add(TEXT("Invalid disconnect entry (not a string)"));
				continue;
			}

			// Check if it's a specific connection "NodeA:PinA -> NodeB:PinB" or just a pin "NodeA:PinA"
			if (DisconnStr.Contains(TEXT("->")))
			{
				// Specific connection to break
				FConnectionDef ConnDef;
				FString ParseError;
				if (!ParseConnection(DisconnStr, ConnDef, ParseError))
				{
					Errors.Add(ParseError);
					continue;
				}

				// Resolve nodes
				UEdGraphNode* FromNode = ResolveNodeRef(ConnDef.FromNodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!FromNode)
				{
					Errors.Add(FString::Printf(TEXT("Cannot resolve 'from' node for disconnect: %s"), *ConnDef.FromNodeRef));
					continue;
				}

				UEdGraphNode* ToNode = ResolveNodeRef(ConnDef.ToNodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!ToNode)
				{
					Errors.Add(FString::Printf(TEXT("Cannot resolve 'to' node for disconnect: %s"), *ConnDef.ToNodeRef));
					continue;
				}

				// Find pins
				UEdGraphPin* FromPin = FindPinByName(FromNode, ConnDef.FromPinName, EGPD_Output);
				if (!FromPin)
				{
					FString AvailablePins = ListAvailablePins(FromNode, EGPD_Output);
					Errors.Add(FString::Printf(TEXT("Output pin '%s' not found on %s for disconnect. Available: %s"),
						*ConnDef.FromPinName, *ConnDef.FromNodeRef, *AvailablePins));
					continue;
				}

				UEdGraphPin* ToPin = FindPinByName(ToNode, ConnDef.ToPinName, EGPD_Input);
				if (!ToPin)
				{
					FString AvailablePins = ListAvailablePins(ToNode, EGPD_Input);
					Errors.Add(FString::Printf(TEXT("Input pin '%s' not found on %s for disconnect. Available: %s"),
						*ConnDef.ToPinName, *ConnDef.ToNodeRef, *AvailablePins));
					continue;
				}

				// Break specific connection
				FString BreakError;
				if (BreakConnection(FromPin, ToPin, BreakError))
				{
					DisconnectResults.Add(FString::Printf(TEXT("%s:%s -x- %s:%s"),
						*ConnDef.FromNodeRef, *ConnDef.FromPinName,
						*ConnDef.ToNodeRef, *ConnDef.ToPinName));
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("Disconnect failed %s:%s -> %s:%s: %s"),
						*ConnDef.FromNodeRef, *ConnDef.FromPinName,
						*ConnDef.ToNodeRef, *ConnDef.ToPinName,
						*BreakError));
				}
			}
			else
			{
				// Break all connections on a pin "NodeRef:PinName"
				FString NodeRef, PinName;
				if (!DisconnStr.Split(TEXT(":"), &NodeRef, &PinName))
				{
					Errors.Add(FString::Printf(TEXT("Invalid disconnect format (missing :): %s"), *DisconnStr));
					continue;
				}

				UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!Node)
				{
					Errors.Add(FString::Printf(TEXT("Cannot resolve node for disconnect: %s"), *NodeRef));
					continue;
				}

				// Try to find as output first, then input
				UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Output);
				if (!Pin)
				{
					Pin = FindPinByName(Node, PinName, EGPD_Input);
				}

				if (!Pin)
				{
					FString AvailableOutputs = ListAvailablePins(Node, EGPD_Output);
					FString AvailableInputs = ListAvailablePins(Node, EGPD_Input);
					Errors.Add(FString::Printf(TEXT("Pin '%s' not found on %s. Outputs: %s | Inputs: %s"),
						*PinName, *NodeRef, *AvailableOutputs, *AvailableInputs));
					continue;
				}

				int32 BrokenCount = Pin->LinkedTo.Num();
				FString BreakError;
				if (BreakAllConnections(Pin, BreakError))
				{
					DisconnectResults.Add(FString::Printf(TEXT("%s:%s -x- (all %d connections)"),
						*NodeRef, *PinName, BrokenCount));
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("Disconnect all failed %s:%s: %s"),
						*NodeRef, *PinName, *BreakError));
				}
			}
		}
	}

	// Process connections (after disconnects, so freed pins can accept new connections)
	const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray;
	if (Args->TryGetArrayField(TEXT("connections"), ConnectionsArray))
	{
		for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
		{
			FString ConnectionStr;
			if (!ConnValue->TryGetString(ConnectionStr))
			{
				Errors.Add(TEXT("Invalid connection (not a string)"));
				continue;
			}

			FConnectionDef ConnDef;
			FString ParseError;
			if (!ParseConnection(ConnectionStr, ConnDef, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			// Resolve 'from' node
			UEdGraphNode* FromNode = ResolveNodeRef(ConnDef.FromNodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!FromNode)
			{
				Errors.Add(FString::Printf(TEXT("Cannot resolve 'from' node: %s"), *ConnDef.FromNodeRef));
				continue;
			}

			// Resolve 'to' node
			UEdGraphNode* ToNode = ResolveNodeRef(ConnDef.ToNodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!ToNode)
			{
				Errors.Add(FString::Printf(TEXT("Cannot resolve 'to' node: %s"), *ConnDef.ToNodeRef));
				continue;
			}

			// Find pins
			UEdGraphPin* FromPin = FindPinByName(FromNode, ConnDef.FromPinName, EGPD_Output);
			if (!FromPin)
			{
				FString AvailablePins = ListAvailablePins(FromNode, EGPD_Output);
				Errors.Add(FString::Printf(TEXT("Output pin '%s' not found on %s. Available outputs: %s"),
					*ConnDef.FromPinName, *ConnDef.FromNodeRef, *AvailablePins));
				continue;
			}

			UEdGraphPin* ToPin = FindPinByName(ToNode, ConnDef.ToPinName, EGPD_Input);
			if (!ToPin)
			{
				FString AvailablePins = ListAvailablePins(ToNode, EGPD_Input);
				Errors.Add(FString::Printf(TEXT("Input pin '%s' not found on %s. Available inputs: %s"),
					*ConnDef.ToPinName, *ConnDef.ToNodeRef, *AvailablePins));
				continue;
			}

			// Create connection using three-tier fallback strategy
			FConnectionResult ConnResult = CreateConnectionWithFallback(FromPin, ToPin);
			if (ConnResult.bSuccess)
			{
				FString ConnStr = FString::Printf(TEXT("%s:%s -> %s:%s"),
					*ConnDef.FromNodeRef, *ConnDef.FromPinName,
					*ConnDef.ToNodeRef, *ConnDef.ToPinName);

				// Add connection type suffix for non-direct connections
				if (ConnResult.Type == EConnectionResultType::Promoted)
				{
					ConnStr += FString::Printf(TEXT(" [promoted: %s]"), *ConnResult.Details);
				}
				else if (ConnResult.Type == EConnectionResultType::Converted)
				{
					ConnStr += FString::Printf(TEXT(" [converted: %s]"), *ConnResult.Details);
				}
				else if (ConnResult.Details == TEXT("already connected"))
				{
					ConnStr += TEXT(" [already connected]");
				}

				ConnectionResults.Add(ConnStr);
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("Connection failed %s:%s -> %s:%s: %s"),
					*ConnDef.FromNodeRef, *ConnDef.FromPinName,
					*ConnDef.ToNodeRef, *ConnDef.ToPinName,
					*ConnResult.Error));
			}
		}
	}

	// Process set_pins - set values on existing nodes
	const TArray<TSharedPtr<FJsonValue>>* SetPinsArray;
	if (Args->TryGetArrayField(TEXT("set_pins"), SetPinsArray))
	{
		for (const TSharedPtr<FJsonValue>& SetPinValue : *SetPinsArray)
		{
			const TSharedPtr<FJsonObject>* SetPinObj;
			if (!SetPinValue->TryGetObject(SetPinObj))
			{
				Errors.Add(TEXT("Invalid set_pins entry (not an object)"));
				continue;
			}

			FSetPinsOp SetOp;
			FString ParseError;
			if (!ParseSetPinsOp(*SetPinObj, SetOp, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			// Resolve the node
			UEdGraphNode* TargetNode = ResolveNodeRef(SetOp.NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!TargetNode && EditGraphTool_IsBTGraph(Graph))
			{
				TargetNode = EditGraphTool_FindBTSubNode(Graph, SetOp.NodeRef, FullAssetPath, ActualGraphName);
			}
			if (!TargetNode && EditGraphTool_IsEQSGraph(Graph))
			{
				TargetNode = EditGraphTool_FindEQSSubNode(Graph, SetOp.NodeRef, FullAssetPath, ActualGraphName);
			}
			if (!TargetNode)
			{
				Errors.Add(FString::Printf(TEXT("Node not found for set_pins: %s"), *SetOp.NodeRef));
				continue;
			}

			// Set values on the node
			TArray<FString> Results = SetNodeValues(TargetNode, SetOp.Values, Graph);
			for (const FString& Result : Results)
			{
				SetPinsResults.Add(FString::Printf(TEXT("%s: %s"), *SetOp.NodeRef, *Result));
			}
		}
	}

	// Process move_nodes
	const TArray<TSharedPtr<FJsonValue>>* MoveNodesArray;
	if (Args->TryGetArrayField(TEXT("move_nodes"), MoveNodesArray))
	{
		for (const TSharedPtr<FJsonValue>& MoveValue : *MoveNodesArray)
		{
			const TSharedPtr<FJsonObject>* MoveObj;
			if (!MoveValue->TryGetObject(MoveObj))
			{
				Errors.Add(TEXT("Invalid move_nodes entry (not an object)"));
				continue;
			}

			FMoveNodeOp MoveOp;
			FString ParseError;
			if (!ParseMoveNodeOp(*MoveObj, MoveOp, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			UEdGraphNode* TargetNode = ResolveNodeRef(MoveOp.NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!TargetNode)
			{
				Errors.Add(FString::Printf(TEXT("Node not found for move_nodes: %s"), *MoveOp.NodeRef));
				continue;
			}

			TargetNode->Modify();
			const int32 OldX = TargetNode->NodePosX;
			const int32 OldY = TargetNode->NodePosY;

			if (MoveOp.bHasAbsolute)
			{
				TargetNode->NodePosX = MoveOp.X;
				TargetNode->NodePosY = MoveOp.Y;
			}
			else
			{
				TargetNode->NodePosX += MoveOp.DeltaX;
				TargetNode->NodePosY += MoveOp.DeltaY;
			}

			MoveResults.Add(FString::Printf(TEXT("%s: (%d,%d) -> (%d,%d)"),
				*MoveOp.NodeRef, OldX, OldY, TargetNode->NodePosX, TargetNode->NodePosY));
		}
	}

	// Process align_nodes
	const TArray<TSharedPtr<FJsonValue>>* AlignNodesArray;
	if (Args->TryGetArrayField(TEXT("align_nodes"), AlignNodesArray))
	{
		for (const TSharedPtr<FJsonValue>& AlignValue : *AlignNodesArray)
		{
			const TSharedPtr<FJsonObject>* AlignObj;
			if (!AlignValue->TryGetObject(AlignObj))
			{
				Errors.Add(TEXT("Invalid align_nodes entry (not an object)"));
				continue;
			}

			FAlignNodesOp AlignOp;
			FString ParseError;
			if (!ParseAlignNodesOp(*AlignObj, AlignOp, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			TArray<UEdGraphNode*> NodesToAlign;
			for (const FString& NodeRef : AlignOp.NodeRefs)
			{
				if (UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap))
				{
					NodesToAlign.Add(Node);
				}
			}

			if (NodesToAlign.Num() < 2)
			{
				Errors.Add(TEXT("align_nodes: fewer than 2 target nodes were resolved"));
				continue;
			}

			auto GetCoord = [&AlignOp](UEdGraphNode* Node) -> int32
			{
				return AlignOp.Axis == TEXT("x") ? Node->NodePosX : Node->NodePosY;
			};

			int32 TargetCoord = 0;
			if (AlignOp.Mode == TEXT("min"))
			{
				TargetCoord = TNumericLimits<int32>::Max();
				for (UEdGraphNode* Node : NodesToAlign)
				{
					TargetCoord = FMath::Min(TargetCoord, GetCoord(Node));
				}
			}
			else if (AlignOp.Mode == TEXT("max"))
			{
				TargetCoord = TNumericLimits<int32>::Lowest();
				for (UEdGraphNode* Node : NodesToAlign)
				{
					TargetCoord = FMath::Max(TargetCoord, GetCoord(Node));
				}
			}
			else
			{
				int64 Sum = 0;
				for (UEdGraphNode* Node : NodesToAlign)
				{
					Sum += GetCoord(Node);
				}
				TargetCoord = FMath::RoundToInt(static_cast<double>(Sum) / static_cast<double>(NodesToAlign.Num()));
			}

			for (UEdGraphNode* Node : NodesToAlign)
			{
				Node->Modify();
				if (AlignOp.Axis == TEXT("x"))
				{
					Node->NodePosX = TargetCoord;
				}
				else
				{
					Node->NodePosY = TargetCoord;
				}
			}

			AlignResults.Add(FString::Printf(TEXT("Aligned %d node(s): axis=%s mode=%s target=%d"),
				NodesToAlign.Num(), *AlignOp.Axis, *AlignOp.Mode, TargetCoord));
		}
	}

	// Process layout_nodes
	const TArray<TSharedPtr<FJsonValue>>* LayoutNodesArray;
	if (Args->TryGetArrayField(TEXT("layout_nodes"), LayoutNodesArray))
	{
		for (const TSharedPtr<FJsonValue>& LayoutValue : *LayoutNodesArray)
		{
			const TSharedPtr<FJsonObject>* LayoutObj;
			if (!LayoutValue->TryGetObject(LayoutObj))
			{
				Errors.Add(TEXT("Invalid layout_nodes entry (not an object)"));
				continue;
			}

			FLayoutNodesOp LayoutOp;
			FString ParseError;
			if (!ParseLayoutNodesOp(*LayoutObj, LayoutOp, ParseError))
			{
				Errors.Add(ParseError);
				continue;
			}

			TArray<UEdGraphNode*> NodesToLayout;
			for (const FString& NodeRef : LayoutOp.NodeRefs)
			{
				if (UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap))
				{
					NodesToLayout.Add(Node);
				}
			}
			if (NodesToLayout.Num() == 0)
			{
				Errors.Add(TEXT("layout_nodes: no target nodes were resolved"));
				continue;
			}

			for (int32 Index = 0; Index < NodesToLayout.Num(); ++Index)
			{
				const int32 Row = Index / LayoutOp.Columns;
				const int32 Col = Index % LayoutOp.Columns;
				UEdGraphNode* Node = NodesToLayout[Index];
				Node->Modify();
				Node->NodePosX = LayoutOp.StartX + Col * LayoutOp.SpacingX;
				Node->NodePosY = LayoutOp.StartY + Row * LayoutOp.SpacingY;
			}

			LayoutResults.Add(FString::Printf(TEXT("Laid out %d node(s): start=(%d,%d) spacing=(%d,%d) columns=%d"),
				NodesToLayout.Num(), LayoutOp.StartX, LayoutOp.StartY, LayoutOp.SpacingX, LayoutOp.SpacingY, LayoutOp.Columns));
		}
	}

	// Process add_comments
	const TArray<TSharedPtr<FJsonValue>>* AddCommentsArray;
	if (Args->TryGetArrayField(TEXT("add_comments"), AddCommentsArray))
	{
		for (const TSharedPtr<FJsonValue>& CommentValue : *AddCommentsArray)
		{
			const TSharedPtr<FJsonObject>* CommentObj;
			if (!CommentValue->TryGetObject(CommentObj))
			{
				Errors.Add(TEXT("Invalid add_comments entry (not an object)"));
				continue;
			}

			FString CommentText;
			if (!(*CommentObj)->TryGetStringField(TEXT("text"), CommentText) || CommentText.IsEmpty())
			{
				Errors.Add(TEXT("Comment missing required 'text' field"));
				continue;
			}

			// Determine position and size
			float PosX = 0.0f, PosY = 0.0f;
			float Width = 400.0f, Height = 100.0f;
			bool bHasPosition = false;

			// Check if wrapping nodes
			const TArray<TSharedPtr<FJsonValue>>* NodeRefs;
			if ((*CommentObj)->TryGetArrayField(TEXT("nodes"), NodeRefs) && NodeRefs->Num() > 0)
			{
				float MinX = MAX_FLT, MinY = MAX_FLT, MaxX = -MAX_FLT, MaxY = -MAX_FLT;
				int32 FoundNodes = 0;

				for (const TSharedPtr<FJsonValue>& NodeRefVal : *NodeRefs)
				{
					FString NodeRef;
					if (!NodeRefVal->TryGetString(NodeRef)) continue;

					UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
					if (Node)
					{
						MinX = FMath::Min(MinX, (float)Node->NodePosX);
						MinY = FMath::Min(MinY, (float)Node->NodePosY);
						// Estimate node size since actual rendered size isn't available
						MaxX = FMath::Max(MaxX, (float)(Node->NodePosX + 300));
						MaxY = FMath::Max(MaxY, (float)(Node->NodePosY + 150));
						FoundNodes++;
					}
					else
					{
						Errors.Add(FString::Printf(TEXT("Comment: Node not found: %s"), *NodeRef));
					}
				}

				if (FoundNodes > 0)
				{
					const float Padding = 50.0f;
					const float TitleHeight = 40.0f;
					PosX = MinX - Padding;
					PosY = MinY - Padding - TitleHeight;
					Width = (MaxX - MinX) + Padding * 2.0f;
					Height = (MaxY - MinY) + Padding * 2.0f + TitleHeight;
					bHasPosition = true;
				}
			}

			// Explicit position overrides (but nodes takes priority)
			if (!bHasPosition)
			{
				const TSharedPtr<FJsonObject>* PosObj;
				if ((*CommentObj)->TryGetObjectField(TEXT("position"), PosObj))
				{
					double X = 0, Y = 0;
					if ((*PosObj)->TryGetNumberField(TEXT("x"), X)) PosX = (float)X;
					if ((*PosObj)->TryGetNumberField(TEXT("y"), Y)) PosY = (float)Y;
					bHasPosition = true;
				}
			}

			// Explicit size
			const TSharedPtr<FJsonObject>* SizeObj;
			if ((*CommentObj)->TryGetObjectField(TEXT("size"), SizeObj))
			{
				double W = 0, H = 0;
				if ((*SizeObj)->TryGetNumberField(TEXT("w"), W)) Width = (float)W;
				if ((*SizeObj)->TryGetNumberField(TEXT("h"), H)) Height = (float)H;
			}

			// Default position if nothing specified — placed at origin
			if (!bHasPosition)
			{
				PosX = 0.0f;
				PosY = 0.0f;
			}

			// Parse optional color (#RRGGBB or #RRGGBBAA)
			FLinearColor CommentColor = FLinearColor::White;
			bool bHasColor = false;
			FString ColorStr;
			if ((*CommentObj)->TryGetStringField(TEXT("color"), ColorStr) && !ColorStr.IsEmpty())
			{
				FColor ParsedColor;
				if (ColorStr.StartsWith(TEXT("#")))
				{
					ParsedColor = FColor::FromHex(ColorStr);
					CommentColor = ParsedColor.ReinterpretAsLinear();
					bHasColor = true;
				}
			}

			// Create comment node
			Graph->Modify();
			UEdGraphNode_Comment* CommentNode = NewObject<UEdGraphNode_Comment>(Graph);
			CommentNode->CreateNewGuid();
			CommentNode->SetFlags(RF_Transactional);
			CommentNode->PostPlacedNewNode();

			// Set properties after PostPlacedNewNode (which sets defaults)
			CommentNode->NodeComment = CommentText;
			CommentNode->NodePosX = (int32)PosX;
			CommentNode->NodePosY = (int32)PosY;
			CommentNode->NodeWidth = (int32)Width;
			CommentNode->NodeHeight = (int32)Height;
			if (bHasColor)
			{
				CommentNode->CommentColor = CommentColor;
			}

			Graph->AddNode(CommentNode, false, false);

			// Track as added node
			FAddedNode AddedNode;
			AddedNode.Name = CommentText.Left(40);
			AddedNode.NodeType = TEXT("Comment");
			AddedNode.Guid = CommentNode->NodeGuid;
			AddedNode.Position = FVector2D(PosX, PosY);
			AddedNode.PinValues.Add(FString::Printf(TEXT("Size: %dx%d"), (int32)Width, (int32)Height));
			AddedNodes.Add(AddedNode);
		}
	}

	// Process split_pins
	const TArray<TSharedPtr<FJsonValue>>* SplitPinsArray;
	if (Args->TryGetArrayField(TEXT("split_pins"), SplitPinsArray))
	{
		const UEdGraphSchema* GraphSchema = Graph ? Graph->GetSchema() : nullptr;
		if (!GraphSchema)
		{
			Errors.Add(TEXT("split_pins failed: graph has no schema"));
		}
		else
		{
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GraphSchema);
			for (const TSharedPtr<FJsonValue>& SplitValue : *SplitPinsArray)
			{
				FString PinRef;
				if (!SplitValue->TryGetString(PinRef) || PinRef.IsEmpty())
				{
					Errors.Add(TEXT("Invalid split_pins entry (not a string)"));
					continue;
				}

				// Parse "NodeRef:PinName"
				FString NodeRef, PinName;
				if (!PinRef.Split(TEXT(":"), &NodeRef, &PinName) || NodeRef.IsEmpty() || PinName.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("Invalid pin reference format: '%s' (expected 'NodeRef:PinName')"), *PinRef));
					continue;
				}

				UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!Node)
				{
					Errors.Add(FString::Printf(TEXT("Split: Node not found: %s"), *NodeRef));
					continue;
				}

				// Find pin (try both directions)
				UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
				if (!Pin)
				{
					Pin = FindPinByName(Node, PinName, EGPD_Output);
				}
				if (!Pin)
				{
					Errors.Add(FString::Printf(TEXT("Split: Pin '%s' not found on node '%s'"), *PinName, *NodeRef));
					continue;
				}

				// Check if already split
				if (Pin->SubPins.Num() > 0)
				{
					Errors.Add(FString::Printf(TEXT("Split: Pin '%s' is already split"), *PinRef));
					continue;
				}

				// Keep K2's strong validation. Other schemas may support split through their own rules.
				if (K2Schema && !K2Schema->PinHasSplittableStructType(Pin))
				{
					Errors.Add(FString::Printf(TEXT("Split: Pin '%s' cannot be split (not a struct type)"), *PinRef));
					continue;
				}

				GraphSchema->SplitPin(Pin, true);

				if (Pin->SubPins.Num() == 0)
				{
					Errors.Add(FString::Printf(TEXT("Split: Schema did not split '%s' (operation not supported for this graph/pin)"), *PinRef));
					continue;
				}

				// Collect sub-pin names for result
				TArray<FString> SubPinNames;
				for (UEdGraphPin* SubPin : Pin->SubPins)
				{
					SubPinNames.Add(SubPin->PinName.ToString());
				}

				SetPinsResults.Add(FString::Printf(TEXT("Split %s -> %s"), *PinRef, *FString::Join(SubPinNames, TEXT(", "))));
			}
		}
	}

	// Process recombine_pins
	const TArray<TSharedPtr<FJsonValue>>* RecombinePinsArray;
	if (Args->TryGetArrayField(TEXT("recombine_pins"), RecombinePinsArray))
	{
		const UEdGraphSchema* GraphSchema = Graph ? Graph->GetSchema() : nullptr;
		if (!GraphSchema)
		{
			Errors.Add(TEXT("recombine_pins failed: graph has no schema"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& RecombineValue : *RecombinePinsArray)
			{
				FString PinRef;
				if (!RecombineValue->TryGetString(PinRef) || PinRef.IsEmpty())
				{
					Errors.Add(TEXT("Invalid recombine_pins entry (not a string)"));
					continue;
				}

				// Parse "NodeRef:PinName"
				FString NodeRef, PinName;
				if (!PinRef.Split(TEXT(":"), &NodeRef, &PinName) || NodeRef.IsEmpty() || PinName.IsEmpty())
				{
					Errors.Add(FString::Printf(TEXT("Invalid pin reference format: '%s' (expected 'NodeRef:PinName')"), *PinRef));
					continue;
				}

				UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
				if (!Node)
				{
					Errors.Add(FString::Printf(TEXT("Recombine: Node not found: %s"), *NodeRef));
					continue;
				}

				// Search ALL pins including hidden ones (split parent pins are hidden)
				UEdGraphPin* Pin = nullptr;
				for (UEdGraphPin* P : Node->Pins)
				{
					if (P && P->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
					{
						Pin = P;
						break;
					}
				}
				if (!Pin)
				{
					Errors.Add(FString::Printf(TEXT("Recombine: Pin '%s' not found on node '%s'"), *PinName, *NodeRef));
					continue;
				}

				// Find the parent pin if this is a sub-pin
				UEdGraphPin* ParentPin = Pin->ParentPin ? Pin->ParentPin : Pin;

				// Verify it's actually split
				if (ParentPin->SubPins.Num() == 0)
				{
					Errors.Add(FString::Printf(TEXT("Recombine: Pin '%s' is not split"), *PinRef));
					continue;
				}

				FString ParentPinName = ParentPin->PinName.ToString();
				GraphSchema->RecombinePin(ParentPin);

				if (ParentPin->SubPins.Num() > 0)
				{
					Errors.Add(FString::Printf(TEXT("Recombine: Schema did not recombine '%s:%s' (operation not supported or blocked by links)"),
						*NodeRef, *ParentPinName));
					continue;
				}

				SetPinsResults.Add(FString::Printf(TEXT("Recombined %s:%s"), *NodeRef, *ParentPinName));
			}
		}
	}

	// Process add_exec_pins
	const TArray<TSharedPtr<FJsonValue>>* AddExecPinsArray;
	if (Args->TryGetArrayField(TEXT("add_exec_pins"), AddExecPinsArray))
	{
		for (const TSharedPtr<FJsonValue>& AddPinValue : *AddExecPinsArray)
		{
			FString NodeRef;
			if (!AddPinValue->TryGetString(NodeRef) || NodeRef.IsEmpty())
			{
				Errors.Add(TEXT("Invalid add_exec_pins entry (not a string)"));
				continue;
			}

			UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!Node)
			{
				Errors.Add(FString::Printf(TEXT("AddPin: Node not found: %s"), *NodeRef));
				continue;
			}

			// Check if node implements IK2Node_AddPinInterface
			IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(Node);
			if (!AddPinNode)
			{
				Errors.Add(FString::Printf(TEXT("AddPin: Node '%s' does not support dynamic pins"), *NodeRef));
				continue;
			}

			if (!AddPinNode->CanAddPin())
			{
				Errors.Add(FString::Printf(TEXT("AddPin: Node '%s' cannot add more pins (at maximum)"), *NodeRef));
				continue;
			}

			int32 PinCountBefore = Node->Pins.Num();
			AddPinNode->AddInputPin();
			int32 PinCountAfter = Node->Pins.Num();

			if (PinCountAfter > PinCountBefore)
			{
				// Find the new pin name
				UEdGraphPin* NewPin = Node->Pins.Last();
				SetPinsResults.Add(FString::Printf(TEXT("AddPin %s: added '%s'"), *NodeRef, *NewPin->PinName.ToString()));
			}
			else
			{
				Errors.Add(FString::Printf(TEXT("AddPin: Failed to add pin to '%s'"), *NodeRef));
			}
		}
	}

	// Process remove_exec_pins
	const TArray<TSharedPtr<FJsonValue>>* RemoveExecPinsArray;
	if (Args->TryGetArrayField(TEXT("remove_exec_pins"), RemoveExecPinsArray))
	{
		for (const TSharedPtr<FJsonValue>& RemovePinValue : *RemoveExecPinsArray)
		{
			FString NodeRef;
			if (!RemovePinValue->TryGetString(NodeRef) || NodeRef.IsEmpty())
			{
				Errors.Add(TEXT("Invalid remove_exec_pins entry (not a string)"));
				continue;
			}

			UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!Node)
			{
				Errors.Add(FString::Printf(TEXT("RemovePin: Node not found: %s"), *NodeRef));
				continue;
			}

			IK2Node_AddPinInterface* AddPinNode = Cast<IK2Node_AddPinInterface>(Node);
			if (!AddPinNode)
			{
				Errors.Add(FString::Printf(TEXT("RemovePin: Node '%s' does not support dynamic pins"), *NodeRef));
				continue;
			}

			// Find the last removable pin
			UEdGraphPin* PinToRemove = nullptr;
			for (int32 i = Node->Pins.Num() - 1; i >= 0; i--)
			{
				if (AddPinNode->CanRemovePin(Node->Pins[i]))
				{
					PinToRemove = Node->Pins[i];
					break;
				}
			}

			if (!PinToRemove)
			{
				Errors.Add(FString::Printf(TEXT("RemovePin: No removable pins on '%s'"), *NodeRef));
				continue;
			}

			FString RemovedPinName = PinToRemove->PinName.ToString();
			AddPinNode->RemoveInputPin(PinToRemove);

			SetPinsResults.Add(FString::Printf(TEXT("RemovePin %s: removed '%s'"), *NodeRef, *RemovedPinName));
		}
	}

	// Process convert_pin_type — change pin types on promotable operator nodes
	const TArray<TSharedPtr<FJsonValue>>* ConvertPinTypeArray;
	if (Args->TryGetArrayField(TEXT("convert_pin_type"), ConvertPinTypeArray))
	{
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

		for (const TSharedPtr<FJsonValue>& ConvertValue : *ConvertPinTypeArray)
		{
			const TSharedPtr<FJsonObject>* ConvertObj;
			if (!ConvertValue->TryGetObject(ConvertObj))
			{
				Errors.Add(TEXT("Invalid convert_pin_type entry (not an object)"));
				continue;
			}

			FString NodeRef;
			if (!(*ConvertObj)->TryGetStringField(TEXT("node"), NodeRef) || NodeRef.IsEmpty())
			{
				Errors.Add(TEXT("convert_pin_type: missing 'node' field"));
				continue;
			}

			FString PinName;
			if (!(*ConvertObj)->TryGetStringField(TEXT("pin"), PinName) || PinName.IsEmpty())
			{
				Errors.Add(TEXT("convert_pin_type: missing 'pin' field"));
				continue;
			}

			UEdGraphNode* Node = ResolveNodeRef(NodeRef, Graph, FullAssetPath, NewNodeMap);
			if (!Node)
			{
				Errors.Add(FString::Printf(TEXT("convert_pin_type: Node not found: %s"), *NodeRef));
				continue;
			}

			UK2Node_PromotableOperator* PromotableNode = Cast<UK2Node_PromotableOperator>(Node);
			if (!PromotableNode)
			{
				Errors.Add(FString::Printf(TEXT("convert_pin_type: Node '%s' is not a promotable operator (Add, Multiply, Greater, etc.)"), *NodeRef));
				continue;
			}

			// Find the pin
			UEdGraphPin* TargetPin = FindPinByName(Node, PinName, EGPD_Input);
			if (!TargetPin)
			{
				TargetPin = FindPinByName(Node, PinName, EGPD_Output);
			}
			if (!TargetPin)
			{
				FString AvailablePins = ListAvailablePins(Node, EGPD_Input);
				Errors.Add(FString::Printf(TEXT("convert_pin_type: Pin '%s' not found on '%s'. Available: %s"),
					*PinName, *NodeRef, *AvailablePins));
				continue;
			}

			// Handle split pins — convert on parent
			if (TargetPin->ParentPin)
			{
				TargetPin = TargetPin->ParentPin;
			}

			if (!PromotableNode->CanConvertPinType(TargetPin))
			{
				Errors.Add(FString::Printf(TEXT("convert_pin_type: Pin '%s' on '%s' cannot be converted (may be a bool output on a comparison operator)"),
					*PinName, *NodeRef));
				continue;
			}

			// Gather available conversion types
			TArray<UFunction*> AvailableFunctions;
			FTypePromotion::GetAllFuncsForOp(PromotableNode->GetOperationName(), AvailableFunctions);

			TArray<FEdGraphPinType> PossibleTypes;
			for (const UFunction* Func : AvailableFunctions)
			{
				for (TFieldIterator<FProperty> PropIt(Func); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
				{
					FEdGraphPinType ParamType;
					if (K2Schema->ConvertPropertyToPinType(*PropIt, ParamType))
					{
						PossibleTypes.AddUnique(ParamType);
					}
				}
			}

			// Check if 'type' is specified — if not, list available conversions
			FString TargetType;
			(*ConvertObj)->TryGetStringField(TEXT("type"), TargetType);

			if (TargetType.IsEmpty())
			{
				// List available conversions
				TArray<FString> TypeNames;
				for (const FEdGraphPinType& PinType : PossibleTypes)
				{
					TypeNames.Add(K2Schema->TypeToText(PinType).ToString());
				}
				TypeNames.Add(TEXT("Wildcard"));

				FString CurrentType = K2Schema->TypeToText(TargetPin->PinType).ToString();
				SetPinsResults.Add(FString::Printf(TEXT("convert_pin_type %s:%s — current: %s, available: [%s]"),
					*NodeRef, *PinName, *CurrentType, *FString::Join(TypeNames, TEXT(", "))));
				continue;
			}

			// Handle Wildcard reset
			if (TargetType.Equals(TEXT("Wildcard"), ESearchCase::IgnoreCase) ||
				TargetType.Equals(TEXT("Reset"), ESearchCase::IgnoreCase))
			{
				FEdGraphPinType WildcardType;
				WildcardType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
				PromotableNode->ConvertPinType(TargetPin, WildcardType);
				SetPinsResults.Add(FString::Printf(TEXT("convert_pin_type %s:%s -> Wildcard"), *NodeRef, *PinName));
				continue;
			}

			// Match the target type string against available types
			FString LowerTarget = TargetType.ToLower();
			const FEdGraphPinType* MatchedType = nullptr;
			for (const FEdGraphPinType& PinType : PossibleTypes)
			{
				FString TypeText = K2Schema->TypeToText(PinType).ToString();
				if (TypeText.Equals(TargetType, ESearchCase::IgnoreCase))
				{
					MatchedType = &PinType;
					break;
				}
			}

			// Fuzzy fallback: substring match
			if (!MatchedType)
			{
				for (const FEdGraphPinType& PinType : PossibleTypes)
				{
					FString TypeText = K2Schema->TypeToText(PinType).ToString().ToLower();
					if (TypeText.Contains(LowerTarget) || LowerTarget.Contains(TypeText))
					{
						MatchedType = &PinType;
						break;
					}
				}
			}

			if (!MatchedType)
			{
				// Build error with available options
				TArray<FString> TypeNames;
				for (const FEdGraphPinType& PinType : PossibleTypes)
				{
					TypeNames.Add(K2Schema->TypeToText(PinType).ToString());
				}
				Errors.Add(FString::Printf(TEXT("convert_pin_type: Type '%s' not available for '%s:%s'. Available: [%s, Wildcard]"),
					*TargetType, *NodeRef, *PinName, *FString::Join(TypeNames, TEXT(", "))));
				continue;
			}

			FString OldType = K2Schema->TypeToText(TargetPin->PinType).ToString();
			FString NewType = K2Schema->TypeToText(*MatchedType).ToString();
			PromotableNode->ConvertPinType(TargetPin, *MatchedType);
			SetPinsResults.Add(FString::Printf(TEXT("convert_pin_type %s:%s: %s -> %s"), *NodeRef, *PinName, *OldType, *NewType));
		}
	}

	// Mark asset dirty and trigger updates
	Asset->Modify();
	if (Blueprint)
	{
		// During PIE, use MarkBlueprintAsModified (dirty-only, no compile) to avoid
		// crashing when FRepLayout holds stale FProperty pointers from active UNetDriver.
		// The blueprint will be recompiled when PIE ends or when the user explicitly compiles.
		if (GEditor && GEditor->IsPlayingSessionInEditor())
		{
			FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
		}
		else
		{
			FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		}
	}
	else if (UMaterialGraph* MatGraph = Cast<UMaterialGraph>(Graph))
	{
		// Critical: TryCreateConnection only creates pin links (visual/transient),
		// but FExpressionInput is the persistent storage that saves to .uasset.
		// LinkMaterialExpressionsFromGraph syncs graph pins TO FExpressionInput.
		// Order matters: Modify -> Link -> MarkDirty -> UpdatePinTypes -> Recompile

		UObject* MaterialOrFunction = nullptr;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		MaterialOrFunction = MatGraph->GetMaterialOrFunction();
#else
		MaterialOrFunction = MatGraph->MaterialFunction ? static_cast<UObject*>(MatGraph->MaterialFunction) : static_cast<UObject*>(MatGraph->Material);
#endif
		if (UMaterial* Mat = Cast<UMaterial>(MaterialOrFunction))
		{
			// Step 1: Mark material as being modified BEFORE sync
			Mat->Modify();

			// Step 2: Sync graph connections to FExpressionInput
			MatGraph->LinkMaterialExpressionsFromGraph();

			// Step 3: Notify Material Editor of changes
			FMaterialEditorUtilities::UpdateMaterialAfterGraphChange(MatGraph);

			// Step 4: Mark the Material Editor as dirty so user knows to save
			// This is CRITICAL when working with preview materials
			if (GEditor)
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
							MaterialEditor->MarkMaterialDirty();
							UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Marked Material Editor as dirty"));
						}
					}
				}
			}

				// Step 5: Mark package dirty and force recompile
				Mat->MarkPackageDirty();
				Mat->ForceRecompileForRendering();

				// Surface compile diagnostics to caller (same principle as Niagara compile events)
				TSet<FString> UniqueCompileErrors;
				for (int32 QualityIdx = 0; QualityIdx < static_cast<int32>(EMaterialQualityLevel::Num); ++QualityIdx)
				{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
					const FMaterialResource* Resource = Mat->GetMaterialResource(
						GMaxRHIShaderPlatform, static_cast<EMaterialQualityLevel::Type>(QualityIdx));
#else
					const FMaterialResource* Resource = Mat->GetMaterialResource(
						GMaxRHIFeatureLevel, static_cast<EMaterialQualityLevel::Type>(QualityIdx));
#endif
					if (!Resource)
					{
						continue;
					}

					for (const FString& CompileErr : Resource->GetCompileErrors())
					{
						if (!CompileErr.IsEmpty() && !UniqueCompileErrors.Contains(CompileErr))
						{
							UniqueCompileErrors.Add(CompileErr);
							Errors.Add(FString::Printf(TEXT("COMPILE ERROR [Material/QL%d]: %s"), QualityIdx, *CompileErr));
						}
					}
				}

			// Debug: Log RootNode connection state AND FExpressionInput state
			if (MatGraph->RootNode)
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: RootNode has %d pins"), MatGraph->RootNode->Pins.Num());
				for (UEdGraphPin* Pin : MatGraph->RootNode->Pins)
				{
					if (Pin->Direction == EGPD_Input)
					{
						UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("  Pin '%s' SourceIndex=%d LinkedTo=%d"),
							*Pin->PinName.ToString(), Pin->SourceIndex, Pin->LinkedTo.Num());

						// Check if the FExpressionInput was actually set (the PERSISTENT storage)
						if (Pin->SourceIndex >= 0 && Pin->SourceIndex < MatGraph->MaterialInputs.Num())
						{
							FExpressionInput& MatInput = MatGraph->MaterialInputs[Pin->SourceIndex].GetExpressionInput(Mat);
							if (MatInput.Expression)
							{
								UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: FExpressionInput CONNECTED to %s (OutputIndex=%d)"),
									*MatInput.Expression->GetName(), MatInput.OutputIndex);
							}
							else if (Pin->LinkedTo.Num() > 0)
							{
								UE_LOG(LogAgentIntegrationKit, Warning, TEXT("NeoStack: FExpressionInput is NULL but Pin has %d links! NOT PERSISTED!"),
									Pin->LinkedTo.Num());
							}
						}
					}
				}
			}
		}
		else if (UMaterialFunction* MatFunc = Cast<UMaterialFunction>(MaterialOrFunction))
		{
			MatFunc->Modify();
			MatGraph->LinkMaterialExpressionsFromGraph();
			FMaterialEditorUtilities::UpdateMaterialAfterGraphChange(MatGraph);
			MatFunc->MarkPackageDirty();
		}
	}
	else if (Cast<UMetaSoundSource>(Asset) || Cast<UMetaSoundPatch>(Asset))
	{
		Graph->NotifyGraphChanged();
		Asset->MarkPackageDirty();
	}
	else if (EditGraphTool_IsBTGraph(Graph))
	{
		// BehaviorTree: rebuild runtime tree from graph
		EditGraphTool_FinalizeBTGraph(Graph, Asset);
	}
	else if (EditGraphTool_IsEQSGraph(Graph))
	{
		// EQS: sync graph → runtime options/tests
		EditGraphTool_FinalizeEQSGraph(Graph, Asset);
	}

	// Format and return results
	FString Output = FormatResults(AssetName, ActualGraphName, AddedNodes, DeletedNodes, ConnectionResults, DisconnectResults, MoveResults, AlignResults, LayoutResults, SetPinsResults, Errors);

	if (Errors.Num() > 0 && AddedNodes.Num() == 0 && ConnectionResults.Num() == 0 && DisconnectResults.Num() == 0 &&
		MoveResults.Num() == 0 && AlignResults.Num() == 0 && LayoutResults.Num() == 0 && SetPinsResults.Num() == 0)
	{
		return FToolResult::Fail(Output);
	}

	return FToolResult::Ok(Output);
}

bool FEditGraphTool::ParseNodeDefinition(const TSharedPtr<FJsonObject>& NodeObj, FNodeDefinition& OutDef, FString& OutError)
{
	// Required: id (spawner ID)
	if (!NodeObj->TryGetStringField(TEXT("id"), OutDef.SpawnerId) || OutDef.SpawnerId.IsEmpty())
	{
		OutError = TEXT("Node missing required 'id' field");
		return false;
	}

	// Optional: name
	NodeObj->TryGetStringField(TEXT("name"), OutDef.Name);
	NodeObj->TryGetStringField(TEXT("bind_to"), OutDef.BindTo);
	NodeObj->TryGetStringField(TEXT("parent"), OutDef.Parent);

	// Required: position {x, y} — agent must specify where to place the node
	// Exception: BT_SUBNODE and EQS_TEST spawner IDs don't need position (they attach to parent)
	bool bNeedsPosition = !OutDef.SpawnerId.StartsWith(TEXT("BT_SUBNODE:")) &&
	                      !OutDef.SpawnerId.StartsWith(TEXT("EQS_TEST:"));
	if (bNeedsPosition)
	{
		const TSharedPtr<FJsonObject>* PosObj = nullptr;
		if (!NodeObj->TryGetObjectField(TEXT("position"), PosObj) || !PosObj || !(*PosObj).IsValid())
		{
			OutError = FString::Printf(TEXT("Node '%s': missing required 'position' field ({x, y}). Use read_asset to see existing node positions."),
				OutDef.Name.IsEmpty() ? *OutDef.SpawnerId : *OutDef.Name);
			return false;
		}

		double PosX = 0.0, PosY = 0.0;
		bool bHasX = (*PosObj)->TryGetNumberField(TEXT("x"), PosX);
		bool bHasY = (*PosObj)->TryGetNumberField(TEXT("y"), PosY);
		if (!bHasX || !bHasY)
		{
			OutError = FString::Printf(TEXT("Node '%s': 'position' must have both 'x' and 'y' number fields"),
				OutDef.Name.IsEmpty() ? *OutDef.SpawnerId : *OutDef.Name);
			return false;
		}

		OutDef.Position = FVector2D(PosX, PosY);
	}

	// Optional: pins (object with pin values)
	const TSharedPtr<FJsonObject>* PinsObj;
	if (NodeObj->TryGetObjectField(TEXT("pins"), PinsObj))
	{
		OutDef.Pins = *PinsObj;
	}

	return true;
}

bool FEditGraphTool::BuildBindingsForNode(const FNodeDefinition& NodeDef, UBlueprint* Blueprint,
                                          IBlueprintNodeBinder::FBindingSet& OutBindings, FString& OutError) const
{
	OutBindings.Reset();

	if (NodeDef.BindTo.IsEmpty())
	{
		return true;
	}

	if (!Blueprint)
	{
		OutError = FString::Printf(TEXT("Node '%s': bind_to requires a Blueprint graph context"),
			NodeDef.Name.IsEmpty() ? *NodeDef.SpawnerId : *NodeDef.Name);
		return false;
	}

	FString BindingName = NodeDef.BindTo;
	BindingName.TrimStartAndEndInline();
	BindingName.ReplaceInline(TEXT("\""), TEXT(""));

	auto FindObjectProperty = [&BindingName](UClass* SearchClass) -> FObjectProperty*
	{
		if (!SearchClass)
		{
			return nullptr;
		}

		if (FObjectProperty* Exact = FindFProperty<FObjectProperty>(SearchClass, *BindingName))
		{
			return Exact;
		}

		for (TFieldIterator<FObjectProperty> It(SearchClass, EFieldIterationFlags::IncludeSuper); It; ++It)
		{
			FObjectProperty* Property = *It;
			if (!Property)
			{
				continue;
			}

			if (Property->GetName().Equals(BindingName, ESearchCase::IgnoreCase))
			{
				return Property;
			}
		}

		return nullptr;
	};

	FObjectProperty* BindingProperty = FindObjectProperty(Blueprint->SkeletonGeneratedClass);
	if (!BindingProperty)
	{
		BindingProperty = FindObjectProperty(Blueprint->GeneratedClass);
	}

	if (!BindingProperty)
	{
		OutError = FString::Printf(
			TEXT("Node '%s': bind_to='%s' not found on Blueprint class. Use read_asset components to get the component/property name."),
			NodeDef.Name.IsEmpty() ? *NodeDef.SpawnerId : *NodeDef.Name,
			*NodeDef.BindTo);
		return false;
	}

	OutBindings.Add(FBindingObject(BindingProperty));
	return true;
}

bool FEditGraphTool::ParseConnection(const FString& ConnectionStr, FConnectionDef& OutDef, FString& OutError)
{
	// Format: "NodeRef:PinName->NodeRef:PinName"
	FString FromPart, ToPart;
	if (!ConnectionStr.Split(TEXT("->"), &FromPart, &ToPart))
	{
		OutError = FString::Printf(TEXT("Invalid connection format (missing ->): %s"), *ConnectionStr);
		return false;
	}

	FromPart.TrimStartAndEndInline();
	ToPart.TrimStartAndEndInline();

	// Parse from part
	if (!FromPart.Split(TEXT(":"), &OutDef.FromNodeRef, &OutDef.FromPinName))
	{
		OutError = FString::Printf(TEXT("Invalid 'from' format (missing :): %s"), *FromPart);
		return false;
	}

	// Parse to part
	if (!ToPart.Split(TEXT(":"), &OutDef.ToNodeRef, &OutDef.ToPinName))
	{
		OutError = FString::Printf(TEXT("Invalid 'to' format (missing :): %s"), *ToPart);
		return false;
	}

	return true;
}

bool FEditGraphTool::ParseSetPinsOp(const TSharedPtr<FJsonObject>& OpObj, FSetPinsOp& OutOp, FString& OutError)
{
	// Required: node reference
	if (!OpObj->TryGetStringField(TEXT("node"), OutOp.NodeRef) || OutOp.NodeRef.IsEmpty())
	{
		OutError = TEXT("set_pins entry missing required 'node' field");
		return false;
	}

	// Required: values object
	const TSharedPtr<FJsonObject>* ValuesObj;
	if (!OpObj->TryGetObjectField(TEXT("values"), ValuesObj))
	{
		OutError = TEXT("set_pins entry missing required 'values' field");
		return false;
	}
	OutOp.Values = *ValuesObj;

	return true;
}

bool FEditGraphTool::ParseMoveNodeOp(const TSharedPtr<FJsonObject>& OpObj, FMoveNodeOp& OutOp, FString& OutError)
{
	if (!OpObj->TryGetStringField(TEXT("node"), OutOp.NodeRef) || OutOp.NodeRef.IsEmpty())
	{
		OutError = TEXT("move_nodes entry missing required 'node' field");
		return false;
	}

	double X = 0.0;
	double Y = 0.0;
	const bool bHasX = OpObj->TryGetNumberField(TEXT("x"), X);
	const bool bHasY = OpObj->TryGetNumberField(TEXT("y"), Y);
	if (bHasX || bHasY)
	{
		if (!bHasX || !bHasY)
		{
			OutError = TEXT("move_nodes absolute move requires both 'x' and 'y'");
			return false;
		}
		OutOp.bHasAbsolute = true;
		OutOp.X = FMath::RoundToInt(X);
		OutOp.Y = FMath::RoundToInt(Y);
	}

	double DX = 0.0;
	double DY = 0.0;
	const bool bHasDX = OpObj->TryGetNumberField(TEXT("dx"), DX);
	const bool bHasDY = OpObj->TryGetNumberField(TEXT("dy"), DY);
	if (bHasDX || bHasDY)
	{
		if (!bHasDX || !bHasDY)
		{
			OutError = TEXT("move_nodes relative move requires both 'dx' and 'dy'");
			return false;
		}
		OutOp.DeltaX = FMath::RoundToInt(DX);
		OutOp.DeltaY = FMath::RoundToInt(DY);
	}

	if (!OutOp.bHasAbsolute && OutOp.DeltaX == 0 && OutOp.DeltaY == 0)
	{
		OutError = TEXT("move_nodes entry must specify either {x,y} or {dx,dy}");
		return false;
	}

	return true;
}

bool FEditGraphTool::ParseAlignNodesOp(const TSharedPtr<FJsonObject>& OpObj, FAlignNodesOp& OutOp, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (!OpObj->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray || NodesArray->Num() < 2)
	{
		OutError = TEXT("align_nodes entry requires 'nodes' array with at least 2 node references");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
	{
		FString NodeRef;
		if (NodeVal->TryGetString(NodeRef) && !NodeRef.IsEmpty())
		{
			OutOp.NodeRefs.Add(NodeRef);
		}
	}
	if (OutOp.NodeRefs.Num() < 2)
	{
		OutError = TEXT("align_nodes entry has no valid node references");
		return false;
	}

	if (!OpObj->TryGetStringField(TEXT("axis"), OutOp.Axis) || OutOp.Axis.IsEmpty())
	{
		OutError = TEXT("align_nodes entry missing required 'axis' field");
		return false;
	}
	OutOp.Axis = OutOp.Axis.ToLower();
	if (OutOp.Axis != TEXT("x") && OutOp.Axis != TEXT("y"))
	{
		OutError = TEXT("align_nodes axis must be 'x' or 'y'");
		return false;
	}

	if (!OpObj->TryGetStringField(TEXT("mode"), OutOp.Mode) || OutOp.Mode.IsEmpty())
	{
		OutOp.Mode = TEXT("min");
	}
	OutOp.Mode = OutOp.Mode.ToLower();
	if (OutOp.Mode != TEXT("min") && OutOp.Mode != TEXT("max") && OutOp.Mode != TEXT("center"))
	{
		OutError = TEXT("align_nodes mode must be 'min', 'max', or 'center'");
		return false;
	}

	return true;
}

bool FEditGraphTool::ParseLayoutNodesOp(const TSharedPtr<FJsonObject>& OpObj, FLayoutNodesOp& OutOp, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* NodesArray = nullptr;
	if (!OpObj->TryGetArrayField(TEXT("nodes"), NodesArray) || !NodesArray || NodesArray->Num() == 0)
	{
		OutError = TEXT("layout_nodes entry requires non-empty 'nodes' array");
		return false;
	}

	for (const TSharedPtr<FJsonValue>& NodeVal : *NodesArray)
	{
		FString NodeRef;
		if (NodeVal->TryGetString(NodeRef) && !NodeRef.IsEmpty())
		{
			OutOp.NodeRefs.Add(NodeRef);
		}
	}
	if (OutOp.NodeRefs.Num() == 0)
	{
		OutError = TEXT("layout_nodes entry has no valid node references");
		return false;
	}

	const TSharedPtr<FJsonObject>* StartObj = nullptr;
	if (OpObj->TryGetObjectField(TEXT("start"), StartObj) && StartObj)
	{
		double X = OutOp.StartX;
		double Y = OutOp.StartY;
		(*StartObj)->TryGetNumberField(TEXT("x"), X);
		(*StartObj)->TryGetNumberField(TEXT("y"), Y);
		OutOp.StartX = FMath::RoundToInt(X);
		OutOp.StartY = FMath::RoundToInt(Y);
	}

	const TSharedPtr<FJsonObject>* SpacingObj = nullptr;
	if (OpObj->TryGetObjectField(TEXT("spacing"), SpacingObj) && SpacingObj)
	{
		double SX = OutOp.SpacingX;
		double SY = OutOp.SpacingY;
		(*SpacingObj)->TryGetNumberField(TEXT("x"), SX);
		(*SpacingObj)->TryGetNumberField(TEXT("y"), SY);
		OutOp.SpacingX = FMath::Max(1, FMath::RoundToInt(SX));
		OutOp.SpacingY = FMath::Max(1, FMath::RoundToInt(SY));
	}

	double Columns = static_cast<double>(OutOp.Columns);
	if (OpObj->TryGetNumberField(TEXT("columns"), Columns))
	{
		OutOp.Columns = FMath::Max(1, FMath::RoundToInt(Columns));
	}

	return true;
}

TArray<FString> FEditGraphTool::SetNodeValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Values, UEdGraph* Graph)
{
	TArray<FString> Results;

	if (!Node || !Values.IsValid())
	{
		return Results;
	}

	// Check if this is a Material expression node
	UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
	if (MatNode && MatNode->MaterialExpression)
	{
		// Material expression - use reflection to set properties
		UMaterialExpression* Expression = MatNode->MaterialExpression;

		for (const auto& Pair : Values->Values)
		{
			const FString& PropertyName = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			FString ValueStr;
			if (!JsonValueToImportText(Value, ValueStr))
			{
				Results.Add(FString::Printf(TEXT("! %s: unsupported value type for material property"), *PropertyName));
				continue;
			}

			// Find property using reflection
			FProperty* Property = Expression->GetClass()->FindPropertyByName(*PropertyName);
			if (!Property)
			{
				Results.Add(FString::Printf(TEXT("! %s: property not found"), *PropertyName));
				continue;
			}

			// Set property using reflection (same pattern as configure_asset)
			Expression->Modify();
			Expression->PreEditChange(Property);

			// Import the value
			const TCHAR* ImportResult = Property->ImportText_InContainer(*ValueStr, Expression, Expression, PPF_None);

			if (!ImportResult)
			{
				Results.Add(FString::Printf(TEXT("! %s: failed to set value '%s'"), *PropertyName, *ValueStr));
				continue;
			}

			// Post-edit change
			Expression->MarkPackageDirty();
			FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
			Expression->PostEditChangeProperty(PropertyEvent);

			// Mark for preview update
			Expression->bNeedToUpdatePreview = true;

			Results.Add(FString::Printf(TEXT("%s = %s"), *PropertyName, *ValueStr));
		}
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (UPCGEditorGraphNodeBase* PCGNode = Cast<UPCGEditorGraphNodeBase>(Node))
	{
		// PCG node - use reflection to set properties on UPCGSettings
		UPCGNode* PCGInnerNode = PCGNode ? PCGNode->GetPCGNode() : nullptr;
		UPCGSettings* PCGSettings = PCGInnerNode ? PCGInnerNode->GetSettings() : nullptr;
		if (!PCGSettings)
		{
			Results.Add(TEXT("! PCG node has no settings"));
			return Results;
		}

		for (const auto& Pair : Values->Values)
		{
			const FString& PropertyName = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			// Convert JSON value to string
			FString ValueStr;
			if (Value->Type == EJson::String)
			{
				Value->TryGetString(ValueStr);
			}
			else if (Value->Type == EJson::Number)
			{
				double NumVal;
				Value->TryGetNumber(NumVal);
				ValueStr = FString::SanitizeFloat(NumVal);
			}
			else if (Value->Type == EJson::Boolean)
			{
				bool BoolVal;
				Value->TryGetBool(BoolVal);
				ValueStr = BoolVal ? TEXT("True") : TEXT("False");
			}
			else if (Value->Type == EJson::Array)
			{
				// Handle array for vectors
				const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
				if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() >= 3)
				{
					double X = 0, Y = 0, Z = 0, W = 1.0;
					(*ArrayVal)[0]->TryGetNumber(X);
					(*ArrayVal)[1]->TryGetNumber(Y);
					(*ArrayVal)[2]->TryGetNumber(Z);
					if (ArrayVal->Num() >= 4)
					{
						(*ArrayVal)[3]->TryGetNumber(W);
					}
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"),
						*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y),
						*FString::SanitizeFloat(Z), *FString::SanitizeFloat(W));
				}
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! %s: unsupported value type"), *PropertyName));
				continue;
			}

			// Find property using reflection
			FProperty* Property = PCGSettings->GetClass()->FindPropertyByName(*PropertyName);
			if (!Property)
			{
				Results.Add(FString::Printf(TEXT("! %s: property not found on PCG settings"), *PropertyName));
				continue;
			}

			// Set property using reflection
			PCGSettings->Modify();
			PCGSettings->PreEditChange(Property);

			// Import the value
			const TCHAR* ImportResult = Property->ImportText_InContainer(*ValueStr, PCGSettings, PCGSettings, PPF_None);

			if (!ImportResult)
			{
				Results.Add(FString::Printf(TEXT("! %s: failed to set value '%s'"), *PropertyName, *ValueStr));
				continue;
			}

			// Post-edit change
			PCGSettings->MarkPackageDirty();
			FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
			PCGSettings->PostEditChange();

			Results.Add(FString::Printf(TEXT("%s = %s (PCG)"), *PropertyName, *ValueStr));
		}
	}
#endif // UE 5.7+ PCG support
	else if (EditGraphTool_IsBTGraph(Graph))
	{
		// BehaviorTree node - use reflection on NodeInstance
		UBehaviorTree* BT = Graph->GetTypedOuter<UBehaviorTree>();
		return EditGraphTool_SetBTNodeValues(Node, Values, BT);
	}
	else if (EditGraphTool_IsEQSGraph(Graph))
	{
		// EQS node - use reflection on generator/test NodeInstance
		return EditGraphTool_SetEQSNodeValues(Node, Values);
	}
	else
	{
		// Blueprint node - use pin default values
		const UEdGraphSchema* Schema = Graph ? Graph->GetSchema() : nullptr;

		for (const auto& Pair : Values->Values)
		{
			const FString& PinName = Pair.Key;
			const TSharedPtr<FJsonValue>& Value = Pair.Value;

			// Find the pin
			UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
			if (!Pin)
			{
				// Special case: AnimGraph Sequence Player "Sequence" asset (pin may be hidden)
				if (UAnimGraphNode_SequencePlayer* SequencePlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
				{
					if (PinName.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
					{
						FString RawValue;
						if (Value->Type == EJson::String)
						{
							Value->TryGetString(RawValue);
						}
						if (!RawValue.IsEmpty())
						{
							// Extract /Game/... path if embedded in markup
							int32 GameIdx = RawValue.Find(TEXT("/Game/"), ESearchCase::IgnoreCase);
							if (GameIdx != INDEX_NONE)
							{
								FString Sub = RawValue.Mid(GameIdx);
								int32 EndIdx = Sub.Find(TEXT("\""), ESearchCase::IgnoreCase);
								if (EndIdx != INDEX_NONE)
								{
									Sub = Sub.Left(EndIdx);
								}
								RawValue = Sub;
							}

							FString AssetPath = NeoStackToolUtils::BuildAssetPath(RawValue, TEXT(""));
							if (UAnimationAsset* AnimAsset = LoadObject<UAnimationAsset>(nullptr, *AssetPath))
							{
								SequencePlayer->Modify();
								SequencePlayer->SetAnimationAsset(AnimAsset);
								Results.Add(FString::Printf(TEXT("Sequence = %s (asset)"), *AssetPath));
								continue;
							}
							Results.Add(FString::Printf(TEXT("! Sequence: asset not found: %s"), *AssetPath));
							continue;
						}
					}
				}

				// K2Node_CreateDelegate: bind to a function by name
				if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
				{
					if (PinName.Equals(TEXT("SelectedFunctionName"), ESearchCase::IgnoreCase) ||
						PinName.Equals(TEXT("function"), ESearchCase::IgnoreCase) ||
						PinName.Equals(TEXT("FunctionName"), ESearchCase::IgnoreCase))
					{
						FString FuncName;
						if (Value->TryGetString(FuncName) && !FuncName.IsEmpty())
						{
							CreateDelegateNode->SetFunction(FName(*FuncName));

							// Only call HandleAnyChange if the delegate output is connected,
							// otherwise it clears the name when LinkedTo.Num() == 0
							UEdGraphPin* DelegateOut = CreateDelegateNode->GetDelegateOutPin();
							if (DelegateOut && DelegateOut->LinkedTo.Num() > 0)
							{
								CreateDelegateNode->HandleAnyChange(true);
								Results.Add(FString::Printf(TEXT("SelectedFunctionName = %s (delegate binding)"), *FuncName));
							}
							else
							{
								Results.Add(FString::Printf(TEXT("SelectedFunctionName = %s (delegate binding — connect delegate output pin to finalize)"), *FuncName));
							}
							continue;
						}
						Results.Add(TEXT("! SelectedFunctionName: value must be a non-empty string"));
						continue;
					}
				}

				Results.Add(FString::Printf(TEXT("! %s: pin not found"), *PinName));
				continue;
			}

			// Convert JSON value to string
			FString ValueStr;
			if (Value->Type == EJson::String)
			{
				Value->TryGetString(ValueStr);
			}
			else if (Value->Type == EJson::Number)
			{
				double NumVal;
				Value->TryGetNumber(NumVal);
				ValueStr = FString::SanitizeFloat(NumVal);
			}
			else if (Value->Type == EJson::Boolean)
			{
				bool BoolVal;
				Value->TryGetBool(BoolVal);
				ValueStr = BoolVal ? TEXT("true") : TEXT("false");
			}
			else if (Value->Type == EJson::Array)
			{
				// Handle arrays - convert to UE struct format like "(X=1.0,Y=2.0,Z=3.0)" for vectors
				const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
				if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() > 0)
				{
					if (ArrayVal->Num() == 2)
					{
						double X = 0, Y = 0;
						(*ArrayVal)[0]->TryGetNumber(X);
						(*ArrayVal)[1]->TryGetNumber(Y);
						ValueStr = FString::Printf(TEXT("(X=%s,Y=%s)"),
							*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y));
					}
					else if (ArrayVal->Num() == 3)
					{
						double X = 0, Y = 0, Z = 0;
						(*ArrayVal)[0]->TryGetNumber(X);
						(*ArrayVal)[1]->TryGetNumber(Y);
						(*ArrayVal)[2]->TryGetNumber(Z);
						ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
							*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y), *FString::SanitizeFloat(Z));
					}
					else if (ArrayVal->Num() == 4)
					{
						double A = 0, B = 0, C = 0, D = 0;
						(*ArrayVal)[0]->TryGetNumber(A);
						(*ArrayVal)[1]->TryGetNumber(B);
						(*ArrayVal)[2]->TryGetNumber(C);
						(*ArrayVal)[3]->TryGetNumber(D);
						ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"),
							*FString::SanitizeFloat(A), *FString::SanitizeFloat(B),
							*FString::SanitizeFloat(C), *FString::SanitizeFloat(D));
					}
					else
					{
						TArray<FString> Elements;
						for (const TSharedPtr<FJsonValue>& Elem : *ArrayVal)
						{
							double NumVal;
							FString StrVal;
							if (Elem->TryGetNumber(NumVal))
							{
								Elements.Add(FString::SanitizeFloat(NumVal));
							}
							else if (Elem->TryGetString(StrVal))
							{
								Elements.Add(StrVal);
							}
						}
						ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
					}
				}
			}
			else if (Value->Type == EJson::Object)
			{
				// Handle objects - convert to UE struct format like "(X=1.0,Y=2.0,Z=3.0)"
				const TSharedPtr<FJsonObject>* ObjVal;
				if (Value->TryGetObject(ObjVal))
				{
					TArray<FString> Parts;
					for (const auto& Field : (*ObjVal)->Values)
					{
						FString FieldValue;
						double NumVal;
						bool BoolVal;
						if (Field.Value->TryGetNumber(NumVal))
						{
							FieldValue = FString::SanitizeFloat(NumVal);
						}
						else if (Field.Value->TryGetBool(BoolVal))
						{
							FieldValue = BoolVal ? TEXT("True") : TEXT("False");
						}
						else if (Field.Value->TryGetString(FieldValue))
						{
							// Already assigned
						}
						Parts.Add(FString::Printf(TEXT("%s=%s"), *Field.Key, *FieldValue));
					}
					ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
				}
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! %s: unsupported value type"), *PinName));
				continue;
			}

			// Check if we got a value
			if (ValueStr.IsEmpty())
			{
				Results.Add(FString::Printf(TEXT("! %s: could not parse value"), *PinName));
				continue;
			}

			// Set the default value
			if (Schema)
			{
				Schema->TrySetDefaultValue(*Pin, ValueStr);
			}
			else
			{
				Pin->DefaultValue = ValueStr;
				// Engine always calls PinDefaultValueChanged after setting pin values
				Node->PinDefaultValueChanged(Pin);
			}

			Results.Add(FString::Printf(TEXT("%s = %s"), *PinName, *ValueStr));
		}
	}

	return Results;
}

UBlueprintNodeSpawner* FEditGraphTool::FindSpawnerById(const FString& SpawnerId, UEdGraph* Graph)
{
	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();
	const FBlueprintActionDatabase::FActionRegistry& AllActions = ActionDatabase.GetAllActions();

	for (const auto& ActionPair : AllActions)
	{
		for (UBlueprintNodeSpawner* Spawner : ActionPair.Value)
		{
			if (!Spawner)
			{
				continue;
			}

			FString Signature = Spawner->GetSpawnerSignature().ToString();
			if (Signature.Equals(SpawnerId, ESearchCase::IgnoreCase))
			{
				return Spawner;
			}
		}
	}

	return nullptr;
}

UEdGraphNode* FEditGraphTool::SpawnNode(UBlueprintNodeSpawner* Spawner, UEdGraph* Graph, const FVector2D& Position)
{
	if (!Spawner || !Graph)
	{
		return nullptr;
	}

	IBlueprintNodeBinder::FBindingSet Bindings;
	UEdGraphNode* NewNode = Spawner->Invoke(Graph, Bindings, Position);

	if (NewNode)
	{
		// Keep transactional for undo support; spawner handles node placement lifecycle.
		NewNode->SetFlags(RF_Transactional);

		// Ensure pins are allocated for node types that may defer pin setup.
		if (NewNode->Pins.Num() == 0)
		{
			NewNode->AllocateDefaultPins();
		}
	}

	return NewNode;
}

TArray<FString> FEditGraphTool::SetPinValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& PinValues)
{
	TArray<FString> Results;

	if (!Node || !PinValues.IsValid())
	{
		return Results;
	}

	UEdGraph* NodeGraph = Node->GetGraph();
	if (!NodeGraph)
	{
		Results.Add(TEXT("! Node has no parent graph"));
		return Results;
	}
	const UEdGraphSchema* Schema = NodeGraph->GetSchema();

	// Check if this is a material expression node (for property fallback)
	UMaterialGraphNode* MatNode = Cast<UMaterialGraphNode>(Node);
	UMaterialExpression* Expression = MatNode ? MatNode->MaterialExpression : nullptr;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	// Check if this is a PCG node (for property fallback on UPCGSettings)
	UPCGEditorGraphNodeBase* PCGNode = Cast<UPCGEditorGraphNodeBase>(Node);
	UPCGNode* PCGInnerNode = PCGNode ? PCGNode->GetPCGNode() : nullptr;
	UPCGSettings* PCGSettings = PCGInnerNode ? PCGInnerNode->GetSettings() : nullptr;
#endif

	for (const auto& Pair : PinValues->Values)
	{
		const FString& PinName = Pair.Key;
		const TSharedPtr<FJsonValue>& Value = Pair.Value;

		// Find the pin
		UEdGraphPin* Pin = FindPinByName(Node, PinName, EGPD_Input);
		if (!Pin)
		{
			// Special case: AnimGraph Sequence Player "Sequence" asset (pin may be hidden)
			if (UAnimGraphNode_SequencePlayer* SequencePlayer = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				if (PinName.Equals(TEXT("Sequence"), ESearchCase::IgnoreCase))
				{
					FString RawValue;
					if (Value->Type == EJson::String)
					{
						Value->TryGetString(RawValue);
					}
					if (!RawValue.IsEmpty())
					{
						// Extract /Game/... path if embedded in markup
						int32 GameIdx = RawValue.Find(TEXT("/Game/"), ESearchCase::IgnoreCase);
						if (GameIdx != INDEX_NONE)
						{
							FString Sub = RawValue.Mid(GameIdx);
							int32 EndIdx = Sub.Find(TEXT("\""), ESearchCase::IgnoreCase);
							if (EndIdx != INDEX_NONE)
							{
								Sub = Sub.Left(EndIdx);
							}
							RawValue = Sub;
						}

						FString AssetPath = NeoStackToolUtils::BuildAssetPath(RawValue, TEXT(""));
						if (UAnimationAsset* AnimAsset = LoadObject<UAnimationAsset>(nullptr, *AssetPath))
						{
							SequencePlayer->Modify();
							SequencePlayer->SetAnimationAsset(AnimAsset);
							Results.Add(FString::Printf(TEXT("Sequence = %s (asset)"), *AssetPath));
							continue;
						}
						Results.Add(FString::Printf(TEXT("! Sequence: asset not found: %s"), *AssetPath));
						continue;
					}
				}
			}

			// For material expressions, try setting as a property instead
			if (Expression)
			{
				FProperty* Property = Expression->GetClass()->FindPropertyByName(*PinName);
				if (Property)
				{
					// Convert JSON value to string for property
					FString ValueStr;
					if (Value->Type == EJson::String)
					{
						Value->TryGetString(ValueStr);
					}
					else if (Value->Type == EJson::Number)
					{
						double NumVal;
						Value->TryGetNumber(NumVal);
						ValueStr = FString::SanitizeFloat(NumVal);
					}
					else if (Value->Type == EJson::Boolean)
					{
						bool BoolVal;
						Value->TryGetBool(BoolVal);
						ValueStr = BoolVal ? TEXT("True") : TEXT("False");
					}
					else if (Value->Type == EJson::Array)
					{
						// Handle array for vectors/colors
						const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
						if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() >= 3)
						{
							double R = 0, G = 0, B = 0, A = 1.0;
							(*ArrayVal)[0]->TryGetNumber(R);
							(*ArrayVal)[1]->TryGetNumber(G);
							(*ArrayVal)[2]->TryGetNumber(B);
							if (ArrayVal->Num() >= 4)
							{
								(*ArrayVal)[3]->TryGetNumber(A);
							}
							ValueStr = FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"),
								*FString::SanitizeFloat(R), *FString::SanitizeFloat(G),
								*FString::SanitizeFloat(B), *FString::SanitizeFloat(A));
						}
					}

					if (!ValueStr.IsEmpty())
					{
						Expression->Modify();
						Expression->PreEditChange(Property);
						const TCHAR* ImportResult = Property->ImportText_InContainer(*ValueStr, Expression, Expression, PPF_None);
						if (ImportResult)
						{
							Expression->MarkPackageDirty();
							FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
							Expression->PostEditChangeProperty(PropertyEvent);
							Expression->bNeedToUpdatePreview = true;
							Results.Add(FString::Printf(TEXT("%s = %s (property)"), *PinName, *ValueStr));
							continue;
						}
					}
					Results.Add(FString::Printf(TEXT("! Failed to set property: %s"), *PinName));
					continue;
				}
			}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			// For PCG nodes, try setting as a property on UPCGSettings
			if (PCGSettings)
			{
				FProperty* Property = PCGSettings->GetClass()->FindPropertyByName(*PinName);
				if (Property)
				{
					// Convert JSON value to string for property
					FString ValueStr;
					if (Value->Type == EJson::String)
					{
						Value->TryGetString(ValueStr);
					}
					else if (Value->Type == EJson::Number)
					{
						double NumVal;
						Value->TryGetNumber(NumVal);
						ValueStr = FString::SanitizeFloat(NumVal);
					}
					else if (Value->Type == EJson::Boolean)
					{
						bool BoolVal;
						Value->TryGetBool(BoolVal);
						ValueStr = BoolVal ? TEXT("True") : TEXT("False");
					}
					else if (Value->Type == EJson::Array)
					{
						// Handle array for vectors/colors
						const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
						if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() >= 3)
						{
							double X = 0, Y = 0, Z = 0, W = 1.0;
							(*ArrayVal)[0]->TryGetNumber(X);
							(*ArrayVal)[1]->TryGetNumber(Y);
							(*ArrayVal)[2]->TryGetNumber(Z);
							if (ArrayVal->Num() >= 4)
							{
								(*ArrayVal)[3]->TryGetNumber(W);
							}
							ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"),
								*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y),
								*FString::SanitizeFloat(Z), *FString::SanitizeFloat(W));
						}
					}

					if (!ValueStr.IsEmpty())
					{
						PCGSettings->Modify();
						PCGSettings->PreEditChange(Property);
						const TCHAR* ImportResult = Property->ImportText_InContainer(*ValueStr, PCGSettings, PCGSettings, PPF_None);
						if (ImportResult)
						{
							PCGSettings->MarkPackageDirty();
							FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
							PCGSettings->PostEditChange();
							Results.Add(FString::Printf(TEXT("%s = %s (PCG property)"), *PinName, *ValueStr));
							continue;
						}
					}
					Results.Add(FString::Printf(TEXT("! Failed to set PCG property: %s"), *PinName));
					continue;
				}
			}
#endif // UE 5.7+ PCG support

			// K2Node_CreateDelegate: bind to a function by name
			// During add_nodes, the delegate output pin isn't connected yet, so we ONLY
			// call SetFunction — NOT HandleAnyChange. HandleAnyChange would clear the name
			// when the delegate pin has no connections. The subsequent connection step
			// triggers PinConnectionListChanged → HandleAnyChange which resolves properly.
			if (UK2Node_CreateDelegate* CreateDelegateNode = Cast<UK2Node_CreateDelegate>(Node))
			{
				if (PinName.Equals(TEXT("SelectedFunctionName"), ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("function"), ESearchCase::IgnoreCase) ||
					PinName.Equals(TEXT("FunctionName"), ESearchCase::IgnoreCase))
				{
					FString FuncName;
					if (Value->TryGetString(FuncName) && !FuncName.IsEmpty())
					{
						CreateDelegateNode->SetFunction(FName(*FuncName));
						Results.Add(FString::Printf(TEXT("SelectedFunctionName = %s (delegate binding — connect delegate output pin to finalize)"), *FuncName));
						continue;
					}
					Results.Add(TEXT("! SelectedFunctionName: value must be a non-empty string"));
					continue;
				}
			}

			Results.Add(FString::Printf(TEXT("! Pin not found: %s"), *PinName));
			continue;
		}

		// Convert JSON value to string
		FString ValueStr;
		if (Value->Type == EJson::String)
		{
			Value->TryGetString(ValueStr);
		}
		else if (Value->Type == EJson::Number)
		{
			double NumVal;
			Value->TryGetNumber(NumVal);
			ValueStr = FString::SanitizeFloat(NumVal);
		}
		else if (Value->Type == EJson::Boolean)
		{
			bool BoolVal;
			Value->TryGetBool(BoolVal);
			ValueStr = BoolVal ? TEXT("true") : TEXT("false");
		}
		else if (Value->Type == EJson::Array)
		{
			// Handle arrays - convert to UE struct format like "(X=1.0,Y=2.0,Z=3.0)" for vectors
			const TArray<TSharedPtr<FJsonValue>>* ArrayVal;
			if (Value->TryGetArray(ArrayVal) && ArrayVal->Num() > 0)
			{
				// Check array size to determine struct type
				if (ArrayVal->Num() == 2)
				{
					// Vector2D format: (X=val,Y=val)
					double X = 0, Y = 0;
					(*ArrayVal)[0]->TryGetNumber(X);
					(*ArrayVal)[1]->TryGetNumber(Y);
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s)"),
						*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y));
				}
				else if (ArrayVal->Num() == 3)
				{
					// Vector format: (X=val,Y=val,Z=val)
					double X = 0, Y = 0, Z = 0;
					(*ArrayVal)[0]->TryGetNumber(X);
					(*ArrayVal)[1]->TryGetNumber(Y);
					(*ArrayVal)[2]->TryGetNumber(Z);
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
						*FString::SanitizeFloat(X), *FString::SanitizeFloat(Y), *FString::SanitizeFloat(Z));
				}
				else if (ArrayVal->Num() == 4)
				{
					// Vector4/Rotator/Color format: (X=val,Y=val,Z=val,W=val) or (R=val,G=val,B=val,A=val)
					double A = 0, B = 0, C = 0, D = 0;
					(*ArrayVal)[0]->TryGetNumber(A);
					(*ArrayVal)[1]->TryGetNumber(B);
					(*ArrayVal)[2]->TryGetNumber(C);
					(*ArrayVal)[3]->TryGetNumber(D);
					// Use XYZW for Vector4, could also be RGBA for colors
					ValueStr = FString::Printf(TEXT("(X=%s,Y=%s,Z=%s,W=%s)"),
						*FString::SanitizeFloat(A), *FString::SanitizeFloat(B),
						*FString::SanitizeFloat(C), *FString::SanitizeFloat(D));
				}
				else
				{
					// Generic array - just join with commas
					TArray<FString> Elements;
					for (const TSharedPtr<FJsonValue>& Elem : *ArrayVal)
					{
						double NumVal;
						FString StrVal;
						if (Elem->TryGetNumber(NumVal))
						{
							Elements.Add(FString::SanitizeFloat(NumVal));
						}
						else if (Elem->TryGetString(StrVal))
						{
							Elements.Add(StrVal);
						}
					}
					ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
				}
			}
		}
		else if (Value->Type == EJson::Object)
		{
			// Handle objects - convert to UE struct format
			const TSharedPtr<FJsonObject>* ObjVal;
			if (Value->TryGetObject(ObjVal))
			{
				TArray<FString> Parts;
				for (const auto& Field : (*ObjVal)->Values)
				{
					FString FieldValue;
					double NumVal;
					bool BoolVal;
					if (Field.Value->TryGetNumber(NumVal))
					{
						FieldValue = FString::SanitizeFloat(NumVal);
					}
					else if (Field.Value->TryGetBool(BoolVal))
					{
						FieldValue = BoolVal ? TEXT("True") : TEXT("False");
					}
					else if (Field.Value->TryGetString(FieldValue))
					{
						// Already assigned
					}
					Parts.Add(FString::Printf(TEXT("%s=%s"), *Field.Key, *FieldValue));
				}
				ValueStr = FString::Printf(TEXT("(%s)"), *FString::Join(Parts, TEXT(",")));
			}
		}
		else
		{
			Results.Add(FString::Printf(TEXT("! Unsupported value type for pin: %s"), *PinName));
			continue;
		}

		// Check if we actually got a value
		if (ValueStr.IsEmpty())
		{
			Results.Add(FString::Printf(TEXT("! Could not parse value for pin: %s"), *PinName));
			continue;
		}

		// Special handling for Class and Object pins
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		    Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
		{
			// For Class pins, we need to load the UClass and set DefaultObject
			// ValueStr can be:
			// - Short name like "BP_Enemy" (we'll search for it)
			// - Full path like "/Game/Blueprints/BP_Enemy.BP_Enemy_C"
			// - Blueprint path like "/Game/Blueprints/BP_Enemy" (we append _C)

			UClass* FoundClass = nullptr;

			// Try loading as full path first
			if (ValueStr.StartsWith(TEXT("/")))
			{
				// Check if it's a Blueprint path without _C suffix
				FString ClassPath = ValueStr;
				if (!ClassPath.EndsWith(TEXT("_C")))
				{
					// Append the class suffix for Blueprints
					int32 LastDot = ClassPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
					if (LastDot != INDEX_NONE)
					{
						ClassPath = ClassPath + TEXT("_C");
					}
					else
					{
						// No dot - add one with the asset name + _C
						int32 LastSlash = ClassPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
						FString AssetName = ClassPath.Mid(LastSlash + 1);
						ClassPath = ClassPath + TEXT(".") + AssetName + TEXT("_C");
					}
				}
				FoundClass = LoadClass<UObject>(nullptr, *ClassPath);

				// If that failed, try the original path
				if (!FoundClass)
				{
					FoundClass = LoadClass<UObject>(nullptr, *ValueStr);
				}
			}
			else
			{
				// Short name - search in /Game/ for a matching Blueprint
				FString SearchPath = FString::Printf(TEXT("/Game/%s.%s_C"), *ValueStr, *ValueStr);
				FoundClass = LoadClass<UObject>(nullptr, *SearchPath);

				// Try common Blueprint paths
				if (!FoundClass)
				{
					TArray<FString> SearchPaths = {
						FString::Printf(TEXT("/Game/Blueprints/%s.%s_C"), *ValueStr, *ValueStr),
						FString::Printf(TEXT("/Game/AI/%s.%s_C"), *ValueStr, *ValueStr),
						FString::Printf(TEXT("/Game/Characters/%s.%s_C"), *ValueStr, *ValueStr),
					};

					for (const FString& Path : SearchPaths)
					{
						FoundClass = LoadClass<UObject>(nullptr, *Path);
						if (FoundClass)
						{
							break;
						}
					}
				}

				// Also try loading as an engine class
				if (!FoundClass)
				{
					FString EngineClassPath = FString::Printf(TEXT("/Script/Engine.%s"), *ValueStr);
					FoundClass = LoadClass<UObject>(nullptr, *EngineClassPath);
				}
			}

			if (FoundClass)
			{
				// Check if the class is compatible with the pin's expected base class
				UClass* PinBaseClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
				if (PinBaseClass && !FoundClass->IsChildOf(PinBaseClass))
				{
					Results.Add(FString::Printf(TEXT("! Class %s is not a subclass of %s for pin: %s"),
						*FoundClass->GetName(), *PinBaseClass->GetName(), *PinName));
					continue;
				}

				Pin->DefaultObject = FoundClass;
				// Engine always calls PinDefaultValueChanged after setting DefaultObject
				Node->PinDefaultValueChanged(Pin);
				Results.Add(FString::Printf(TEXT("%s = %s (class)"), *PinName, *FoundClass->GetPathName()));
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! Could not find class for pin %s: %s"), *PinName, *ValueStr));
			}
			continue;
		}
		else if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		         Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject)
		{
			// For Object pins, we need to load the UObject and set DefaultObject
			// This is typically used for asset references
			UObject* FoundObject = LoadObject<UObject>(nullptr, *ValueStr);
			if (FoundObject)
			{
				Pin->DefaultObject = FoundObject;
				// Engine always calls PinDefaultValueChanged after setting DefaultObject
				Node->PinDefaultValueChanged(Pin);
				Results.Add(FString::Printf(TEXT("%s = %s (object)"), *PinName, *FoundObject->GetPathName()));
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! Could not find object for pin %s: %s"), *PinName, *ValueStr));
			}
			continue;
		}

		// Try to set the default value for other pin types
		if (Schema)
		{
			Schema->TrySetDefaultValue(*Pin, ValueStr);
		}
		else
		{
			Pin->DefaultValue = ValueStr;
			// Engine always calls PinDefaultValueChanged after setting pin values
			Node->PinDefaultValueChanged(Pin);
		}

		Results.Add(FString::Printf(TEXT("%s = %s"), *PinName, *ValueStr));
	}

	return Results;
}

UEdGraphNode* FEditGraphTool::ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph, const FString& AssetPath,
                                              const TMap<FString, UEdGraphNode*>& NewNodes)
{
	if (NodeRef.IsEmpty() || !Graph)
	{
		return nullptr;
	}

	// 0. Special keywords: "Output" or "Root" resolves to the material output node
	if (NodeRef.Equals(TEXT("Output"), ESearchCase::IgnoreCase) ||
		NodeRef.Equals(TEXT("Root"), ESearchCase::IgnoreCase))
	{
		if (UMaterialGraph* MatGraph = Cast<UMaterialGraph>(Graph))
		{
			if (MatGraph->RootNode)
			{
				return MatGraph->RootNode;
			}
		}
	}

	// 1. Check new nodes from this call
	if (const UEdGraphNode* const* Found = NewNodes.Find(NodeRef))
	{
		return const_cast<UEdGraphNode*>(*Found);
	}

	// 2. Check session registry
	FGuid RegisteredGuid = FNodeNameRegistry::Get().Resolve(AssetPath, Graph->GetName(), NodeRef);
	if (RegisteredGuid.IsValid())
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == RegisteredGuid)
			{
				return Node;
			}
		}
	}

	// 3. Try parsing as raw GUID (full 32-hex format)
	FGuid DirectGuid;
	if (FGuid::Parse(NodeRef, DirectGuid))
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid == DirectGuid)
			{
				return Node;
			}
		}
	}

	// 4. Try partial GUID prefix match (for short GUIDs from read_asset, e.g., "ABCDEF12")
	if (NodeRef.Len() >= 8 && NodeRef.Len() < 32)
	{
		FString NormRef = NodeRef.ToUpper();
		UEdGraphNode* Match = nullptr;
		int32 MatchCount = 0;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node->NodeGuid.ToString().Left(NormRef.Len()).ToUpper() == NormRef)
			{
				Match = Node;
				MatchCount++;
			}
		}
		// Only return if exactly one node matches (avoid ambiguity)
		if (MatchCount == 1)
		{
			return Match;
		}
	}

	return nullptr;
}

UEdGraphPin* FEditGraphTool::FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return nullptr;
	}

	// Helper lambda to check if pin is connectable
	auto IsPinConnectable = [](UEdGraphPin* Pin) -> bool
	{
		return Pin && !Pin->bHidden && !Pin->bNotConnectable && !Pin->bOrphanedPin;
	};

	// First try exact match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
			Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	// Try friendly name match
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
			Pin->PinFriendlyName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
		{
			return Pin;
		}
	}

	// For exec pins, try common aliases
	if (PinName.Equals(TEXT("exec"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("execute"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("in"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == Direction &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
	}

	if (PinName.Equals(TEXT("then"), ESearchCase::IgnoreCase) ||
		PinName.Equals(TEXT("out"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (IsPinConnectable(Pin) && Pin->Direction == EGPD_Output &&
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
	}

	return nullptr;
}

FString FEditGraphTool::ListAvailablePins(UEdGraphNode* Node, EEdGraphPinDirection Direction)
{
	if (!Node)
	{
		return TEXT("(node is null)");
	}

	TArray<FString> PinNames;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden || Pin->bNotConnectable || Pin->bOrphanedPin)
		{
			continue;
		}
		if (Pin->Direction != Direction)
		{
			continue;
		}

		// Build pin description: "PinName (Type)"
		FString TypeStr = Pin->PinType.PinCategory.ToString();
		if (Pin->PinType.PinSubCategoryObject.IsValid())
		{
			TypeStr = Pin->PinType.PinSubCategoryObject->GetName();
		}
		if (Pin->PinType.ContainerType == EPinContainerType::Array)
		{
			TypeStr = FString::Printf(TEXT("Array<%s>"), *TypeStr);
		}

		PinNames.Add(FString::Printf(TEXT("%s (%s)"), *Pin->PinName.ToString(), *TypeStr));
	}

	if (PinNames.Num() == 0)
	{
		return TEXT("(no connectable pins)");
	}

	return FString::Join(PinNames, TEXT(", "));
}

bool FEditGraphTool::ValidateConnectionPrerequisites(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError)
{
	if (!FromPin || !ToPin)
	{
		OutError = TEXT("Invalid pins");
		return false;
	}

	// Check pin directions
	if (FromPin->Direction != EGPD_Output)
	{
		OutError = TEXT("Source pin must be an output pin");
		return false;
	}
	if (ToPin->Direction != EGPD_Input)
	{
		OutError = TEXT("Target pin must be an input pin");
		return false;
	}

	// Check if nodes are in the same graph
	UEdGraphNode* FromNode = FromPin->GetOwningNode();
	UEdGraphNode* ToNode = ToPin->GetOwningNode();
	if (!FromNode || !ToNode)
	{
		OutError = TEXT("Could not get owning nodes");
		return false;
	}
	if (FromNode->GetGraph() != ToNode->GetGraph())
	{
		OutError = TEXT("Cannot connect nodes from different graphs");
		return false;
	}

	// Check if already connected
	if (FromPin->LinkedTo.Contains(ToPin))
	{
		// Already connected - not an error, but caller should know
		OutError = TEXT("Already connected");
		return true;
	}

	// Check execution pin uniqueness - exec output pins can only have ONE connection
	if (FromPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
		FromPin->Direction == EGPD_Output &&
		FromPin->LinkedTo.Num() > 0)
	{
		OutError = FString::Printf(TEXT("Exec output pin '%s' already has a connection (exec pins can only have one outgoing connection)"),
			*FromPin->PinName.ToString());
		return false;
	}

	return true;
}

FEditGraphTool::FConnectionResult FEditGraphTool::CreateConnectionWithFallback(UEdGraphPin* FromPin, UEdGraphPin* ToPin)
{
	FConnectionResult Result;

	// Validate prerequisites
	FString ValidationError;
	if (!ValidateConnectionPrerequisites(FromPin, ToPin, ValidationError))
	{
		// Check if it's the "already connected" case
		if (ValidationError == TEXT("Already connected"))
		{
			Result.bSuccess = true;
			Result.Type = EConnectionResultType::Direct;
			Result.Details = TEXT("already connected");
			return Result;
		}
		Result.Error = ValidationError;
		return Result;
	}

	// Get schema from graph - validate chain to avoid null dereference
	UEdGraphNode* FromNode = FromPin->GetOwningNode();
	if (!FromNode)
	{
		Result.Error = TEXT("Could not get owning node from pin");
		return Result;
	}
	UEdGraph* Graph = FromNode->GetGraph();
	if (!Graph)
	{
		Result.Error = TEXT("Could not get graph from node");
		return Result;
	}

	const UEdGraphSchema* Schema = Graph->GetSchema();
	if (!Schema)
	{
		Result.Error = TEXT("Could not get schema from graph");
		return Result;
	}

	// Check what type of connection is possible
	FPinConnectionResponse Response = Schema->CanCreateConnection(FromPin, ToPin);

	switch (Response.Response.GetValue())
	{
		case CONNECT_RESPONSE_MAKE:
		{
			// Direct connection - types are compatible
			if (Schema->TryCreateConnection(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Direct;
				Result.Details = TEXT("direct");
			}
			else
			{
				Result.Error = TEXT("TryCreateConnection failed unexpectedly");
			}
			break;
		}

		case CONNECT_RESPONSE_BREAK_OTHERS_A:
		case CONNECT_RESPONSE_BREAK_OTHERS_B:
		{
			// Schema will break existing links as needed (used by AnimStateMachineSchema for Entry/state)
			if (Schema->TryCreateConnection(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Direct;
				Result.Details = TEXT("broke existing links");
			}
			else
			{
				Result.Error = TEXT("TryCreateConnection failed for break-others response");
			}
			break;
		}

		case CONNECT_RESPONSE_MAKE_WITH_PROMOTION:
		{
			// Type promotion needed (e.g., float to double, int to int64)
			// The schema will handle the promotion automatically
			if (Schema->CreatePromotedConnection(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Promoted;
				Result.Details = FString::Printf(TEXT("promoted %s to %s"),
					*FromPin->PinType.PinCategory.ToString(),
					*ToPin->PinType.PinCategory.ToString());
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Connection with promotion: %s.%s -> %s.%s (%s)"),
					*FromPin->GetOwningNode()->GetName(), *FromPin->PinName.ToString(),
					*ToPin->GetOwningNode()->GetName(), *ToPin->PinName.ToString(),
					*Result.Details);
			}
			else
			{
				Result.Error = FString::Printf(TEXT("Type promotion failed: %s"), *Response.Message.ToString());
			}
			break;
		}

		case CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE:
		{
			// Need to insert a conversion node (e.g., int to string, vector to text)
			if (Schema->CreateAutomaticConversionNodeAndConnections(FromPin, ToPin))
			{
				Result.bSuccess = true;
				Result.Type = EConnectionResultType::Converted;
				Result.Details = FString::Printf(TEXT("auto-inserted conversion node for %s to %s"),
					*FromPin->PinType.PinCategory.ToString(),
					*ToPin->PinType.PinCategory.ToString());
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Connection with conversion node: %s.%s -> %s.%s (%s)"),
					*FromPin->GetOwningNode()->GetName(), *FromPin->PinName.ToString(),
					*ToPin->GetOwningNode()->GetName(), *ToPin->PinName.ToString(),
					*Result.Details);
			}
			else
			{
				Result.Error = FString::Printf(TEXT("Failed to create conversion node: %s"), *Response.Message.ToString());
			}
			break;
		}

		case CONNECT_RESPONSE_DISALLOW:
		default:
		{
			// Build detailed type information for debugging
			FString FromTypeStr = FromPin->PinType.PinCategory.ToString();
			FString ToTypeStr = ToPin->PinType.PinCategory.ToString();

			// Add subtype info for object/struct types
			if (FromPin->PinType.PinSubCategoryObject.IsValid())
			{
				FromTypeStr = FromPin->PinType.PinSubCategoryObject->GetName();
			}
			if (ToPin->PinType.PinSubCategoryObject.IsValid())
			{
				ToTypeStr = ToPin->PinType.PinSubCategoryObject->GetName();
			}

			// Add container type if present
			if (FromPin->PinType.ContainerType == EPinContainerType::Array)
			{
				FromTypeStr = FString::Printf(TEXT("Array<%s>"), *FromTypeStr);
			}
			if (ToPin->PinType.ContainerType == EPinContainerType::Array)
			{
				ToTypeStr = FString::Printf(TEXT("Array<%s>"), *ToTypeStr);
			}

			Result.Error = FString::Printf(
				TEXT("Cannot connect %s:%s (%s) -> %s:%s (%s). %s"),
				*FromPin->GetOwningNode()->GetName(), *FromPin->PinName.ToString(), *FromTypeStr,
				*ToPin->GetOwningNode()->GetName(), *ToPin->PinName.ToString(), *ToTypeStr,
				*Response.Message.ToString());
			break;
		}
	}

	// Engine post-connection flow: notify both nodes of connection changes
	// TryCreateConnection() calls PinConnectionListChanged() internally for direct connections,
	// but promoted/conversion connections may not, so we ensure it for all success cases.
	if (Result.bSuccess)
	{
		// Notify nodes of pin connectivity change (triggers pin regeneration in K2 nodes,
		// timeline track updates, variable reference validation, etc.)
		if (UEdGraphNode* FromOwner = FromPin->GetOwningNode())
		{
			FromOwner->PinConnectionListChanged(FromPin);
		}
		if (UEdGraphNode* ToOwner = ToPin->GetOwningNode())
		{
			ToOwner->PinConnectionListChanged(ToPin);
		}
	}

	return Result;
}

bool FEditGraphTool::CreateConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError)
{
	// Use the new fallback system
	FConnectionResult Result = CreateConnectionWithFallback(FromPin, ToPin);
	if (!Result.bSuccess)
	{
		OutError = Result.Error;
		return false;
	}
	return true;
}

bool FEditGraphTool::BreakConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError)
{
	if (!FromPin || !ToPin)
	{
		OutError = TEXT("Invalid pins");
		return false;
	}

	// Check if they're actually connected
	if (!FromPin->LinkedTo.Contains(ToPin))
	{
		OutError = TEXT("Pins are not connected");
		return false;
	}

	// CRITICAL: Mark both owning nodes as modified BEFORE breaking link (engine pattern)
	// This ensures undo/redo history captures the change
	UEdGraphNode* FromNode = FromPin->GetOwningNode();
	UEdGraphNode* ToNode = ToPin->GetOwningNode();

	if (FromNode)
	{
		FromNode->Modify();
	}
	if (ToNode)
	{
		ToNode->Modify();
	}

	// Break the link
	FromPin->BreakLinkTo(ToPin);

	return true;
}

bool FEditGraphTool::BreakAllConnections(UEdGraphPin* Pin, FString& OutError)
{
	if (!Pin)
	{
		OutError = TEXT("Invalid pin");
		return false;
	}

	if (Pin->LinkedTo.Num() == 0)
	{
		// Not an error, just nothing to break
		return true;
	}

	// CRITICAL: Mark owning node as modified BEFORE breaking all links (engine pattern)
	UEdGraphNode* OwningNode = Pin->GetOwningNode();
	if (OwningNode)
	{
		OwningNode->Modify();
	}

	// Also mark all connected nodes as modified (make a copy to iterate safely)
	TArray<UEdGraphPin*> LinkedToCopy = Pin->LinkedTo;
	for (UEdGraphPin* LinkedPin : LinkedToCopy)
	{
		if (LinkedPin)
		{
			UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
			if (LinkedNode)
			{
				LinkedNode->Modify();
			}
		}
	}

	// Break all links
	Pin->BreakAllPinLinks(true);

	return true;
}

UEdGraph* FEditGraphTool::GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const
{
	if (!Blueprint)
	{
		return nullptr;
	}

	// Support AnimBlueprint subgraph selectors (state machines, transitions, conduits, etc.)
	if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint))
	{
		FString GraphNameLower = GraphName.ToLower();
		if (GraphNameLower.Contains(TEXT("animgraph:")) ||
			GraphNameLower.Contains(TEXT("statemachine:")) ||
			GraphNameLower.Contains(TEXT("state:")) ||
			GraphNameLower.Contains(TEXT("transition:")) ||
			GraphNameLower.Contains(TEXT("custom_transition:")) ||
			GraphNameLower.Contains(TEXT("conduit:")) ||
			GraphNameLower.Contains(TEXT("composite:")) ||
			GraphNameLower.Contains(TEXT("animlayer:")))
		{
			TArray<TPair<UEdGraph*, FString>> Graphs;
			TSet<UEdGraph*> Visited;

			auto SafeGetTransitionStateName = [](UAnimStateTransitionNode* TransitionNode, bool bPrev) -> FString
			{
				if (!TransitionNode || TransitionNode->Pins.Num() < 2)
				{
					return TEXT("?");
				}
				UAnimStateNodeBase* StateNode = bPrev ? TransitionNode->GetPreviousState() : TransitionNode->GetNextState();
				if (!StateNode)
				{
					return TEXT("?");
				}
				return StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			};

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
							Graphs.Add(TPair<UEdGraph*, FString>(StateMachine->EditorStateMachineGraph,
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
								FString FromState = SafeGetTransitionStateName(TransitionNode, true);
								FString ToState = SafeGetTransitionStateName(TransitionNode, false);

								Graphs.Add(TPair<UEdGraph*, FString>(BoundGraph,
									FString::Printf(TEXT("transition:%s/%s->%s"), *RootName, *FromState, *ToState)));
								CollectRef(BoundGraph, RootName, CollectRef);

								if (TransitionNode->CustomTransitionGraph)
								{
									Graphs.Add(TPair<UEdGraph*, FString>(TransitionNode->CustomTransitionGraph,
										FString::Printf(TEXT("custom_transition:%s/%s->%s"), *RootName, *FromState, *ToState)));
									CollectRef(TransitionNode->CustomTransitionGraph, RootName, CollectRef);
								}
							}
							else
							{
								FString NodeName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
								FString TypePrefix = StateNode->IsA<UAnimStateConduitNode>() ? TEXT("conduit") : TEXT("state");
								Graphs.Add(TPair<UEdGraph*, FString>(BoundGraph,
									FString::Printf(TEXT("%s:%s/%s"), *TypePrefix, *RootName, *NodeName)));
								CollectRef(BoundGraph, RootName, CollectRef);
							}
						}
					}
					else if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(CurrentNode))
					{
						if (CompositeNode->BoundGraph)
						{
							Graphs.Add(TPair<UEdGraph*, FString>(CompositeNode->BoundGraph,
								FString::Printf(TEXT("composite:%s/%s"), *RootName, *CompositeNode->GetNodeTitle(ENodeTitleType::ListView).ToString())));
							CollectRef(CompositeNode->BoundGraph, RootName, CollectRef);
						}
					}
				}
			};

			for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
			{
				if (UAnimationGraph* AnimGraph = Cast<UAnimationGraph>(Graph))
				{
					Graphs.Add(TPair<UEdGraph*, FString>(AnimGraph,
						FString::Printf(TEXT("animgraph:%s"), *AnimGraph->GetName())));
					CollectChildGraphsFromNodes(AnimGraph, AnimGraph->GetName(), CollectChildGraphsFromNodes);
				}
			}

			// Collect animation layer function graphs from implemented interfaces
			// Animation layer interfaces produce UAnimationGraph instances for their functions
			for (const FBPInterfaceDescription& InterfaceDesc : AnimBlueprint->ImplementedInterfaces)
			{
				if (!InterfaceDesc.Interface)
				{
					continue;
				}
				for (UEdGraph* LayerGraph : InterfaceDesc.Graphs)
				{
					if (LayerGraph && LayerGraph->IsA<UAnimationGraph>())
					{
						Graphs.Add(TPair<UEdGraph*, FString>(LayerGraph,
							FString::Printf(TEXT("animlayer:%s"), *LayerGraph->GetName())));
						// Also collect child graphs within the layer function graph
						CollectChildGraphsFromNodes(LayerGraph, LayerGraph->GetName(), CollectChildGraphsFromNodes);
					}
				}
			}

			for (const auto& GraphPair : Graphs)
			{
				if (GraphPair.Value.Equals(GraphName, ESearchCase::IgnoreCase) ||
					GraphPair.Value.ToLower().Equals(GraphNameLower))
				{
					return GraphPair.Key;
				}
			}
		}
	}

	// Support composite: selector for any Blueprint type (not just AnimBP)
	{
		FString GraphNameLower = GraphName.ToLower();
		if (GraphNameLower.Contains(TEXT("composite:")))
		{
			TArray<TPair<UEdGraph*, FString>> CompositeGraphs;
			TSet<UEdGraph*> Visited;

			auto CollectComposites = [&](UEdGraph* GraphToSearch, const FString& ParentName, auto&& Self) -> void
			{
				if (!GraphToSearch || Visited.Contains(GraphToSearch)) return;
				Visited.Add(GraphToSearch);

				for (UEdGraphNode* Node : GraphToSearch->Nodes)
				{
					if (!Node) continue;
					if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
					{
						if (CompositeNode->BoundGraph)
						{
							FString CompName = CompositeNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
							if (CompName.IsEmpty())
							{
								CompName = CompositeNode->BoundGraph->GetName();
							}
							CompositeGraphs.Add(TPair<UEdGraph*, FString>(CompositeNode->BoundGraph,
								FString::Printf(TEXT("composite:%s/%s"), *ParentName, *CompName)));
							Self(CompositeNode->BoundGraph, ParentName, Self);
						}
					}
				}
			};

			for (UEdGraph* Graph : Blueprint->UbergraphPages)
			{
				if (Graph) CollectComposites(Graph, Graph->GetName(), CollectComposites);
			}
			for (UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (Graph) CollectComposites(Graph, Graph->GetName(), CollectComposites);
			}
			for (UEdGraph* Graph : Blueprint->MacroGraphs)
			{
				if (Graph) CollectComposites(Graph, Graph->GetName(), CollectComposites);
			}

			for (const auto& GraphPair : CompositeGraphs)
			{
				if (GraphPair.Value.Equals(GraphName, ESearchCase::IgnoreCase) ||
					GraphPair.Value.ToLower().Equals(GraphNameLower))
				{
					return GraphPair.Key;
				}
			}
		}
	}

	// If no name specified, return the main event graph
	if (GraphName.IsEmpty())
	{
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	// Search UbergraphPages
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Search FunctionGraphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	// Search MacroGraphs
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	return nullptr;
}

FString FEditGraphTool::GetNodeTypeName(UEdGraphNode* Node) const
{
	if (!Node)
	{
		return TEXT("Unknown");
	}

	// Try to get a nice title
	FText Title = Node->GetNodeTitle(ENodeTitleType::MenuTitle);
	if (!Title.IsEmpty())
	{
		return Title.ToString();
	}

	// Fall back to class name (with safety check)
	if (Node->GetClass())
	{
		return Node->GetClass()->GetName();
	}

	return TEXT("UnknownNode");
}

FString FEditGraphTool::FormatResults(const FString& AssetName, const FString& GraphName,
                                       const TArray<FAddedNode>& AddedNodes,
                                       const TArray<FString>& DeletedNodes,
                                       const TArray<FString>& Connections,
                                       const TArray<FString>& Disconnections,
                                       const TArray<FString>& MoveResults,
                                       const TArray<FString>& AlignResults,
                                       const TArray<FString>& LayoutResults,
                                       const TArray<FString>& SetPinsResults,
                                       const TArray<FString>& Errors) const
{
	FString Output;

	// Header
	Output += FString::Printf(TEXT("# EDIT GRAPH: %s\n"), *AssetName);
	Output += FString::Printf(TEXT("Graph: %s\n\n"), *GraphName);

	// Added nodes
	if (AddedNodes.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Added Nodes (%d)\n\n"), AddedNodes.Num());

		for (const FAddedNode& Node : AddedNodes)
		{
			Output += FString::Printf(TEXT("+ %s (%s) at (%.0f, %.0f)\n"),
				*Node.Name, *Node.NodeType, Node.Position.X, Node.Position.Y);
			Output += FString::Printf(TEXT("  GUID: %s\n"), *Node.Guid.ToString());

			// Show available pins for connections
			if (Node.OutputPins.Num() > 0)
			{
				Output += FString::Printf(TEXT("  Out: %s\n"), *FString::Join(Node.OutputPins, TEXT(", ")));
			}
			if (Node.InputPins.Num() > 0)
			{
				Output += FString::Printf(TEXT("  In: %s\n"), *FString::Join(Node.InputPins, TEXT(", ")));
			}

			for (const FString& PinVal : Node.PinValues)
			{
				Output += FString::Printf(TEXT("  - %s\n"), *PinVal);
			}
		}
	}

	// Deleted Nodes
	if (DeletedNodes.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Deleted Nodes (%d)\n\n"), DeletedNodes.Num());

		for (const FString& Node : DeletedNodes)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Node);
		}
		Output += TEXT("\n");
	}

	// Connections
	if (Connections.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Connections (%d)\n\n"), Connections.Num());

		for (const FString& Conn : Connections)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Conn);
		}
		Output += TEXT("\n");
	}

	// Disconnections
	if (Disconnections.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Disconnections (%d)\n\n"), Disconnections.Num());

		for (const FString& Disconn : Disconnections)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Disconn);
		}
		Output += TEXT("\n");
	}

	if (MoveResults.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Moved Nodes (%d)\n\n"), MoveResults.Num());
		for (const FString& Move : MoveResults)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Move);
		}
		Output += TEXT("\n");
	}

	if (AlignResults.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Alignments (%d)\n\n"), AlignResults.Num());
		for (const FString& Align : AlignResults)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Align);
		}
		Output += TEXT("\n");
	}

	if (LayoutResults.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Layout Operations (%d)\n\n"), LayoutResults.Num());
		for (const FString& Layout : LayoutResults)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Layout);
		}
		Output += TEXT("\n");
	}

	// Set pins results
	if (SetPinsResults.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Values Set (%d)\n\n"), SetPinsResults.Num());

		for (const FString& Result : SetPinsResults)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Result);
		}
		Output += TEXT("\n");
	}

	// Errors
	if (Errors.Num() > 0)
	{
		Output += FString::Printf(TEXT("## Errors (%d)\n\n"), Errors.Num());

		for (const FString& Err : Errors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *Err);
		}
		Output += TEXT("\n");
	}

	// Summary
	Output += FString::Printf(TEXT("= %d nodes added, %d deleted, %d connections, %d disconnections, %d moved, %d alignments, %d layouts, %d values set"),
		AddedNodes.Num(), DeletedNodes.Num(), Connections.Num(), Disconnections.Num(), MoveResults.Num(), AlignResults.Num(), LayoutResults.Num(), SetPinsResults.Num());

	if (Errors.Num() > 0)
	{
		Output += FString::Printf(TEXT(", %d errors"), Errors.Num());
	}

	Output += TEXT("\n");

	return Output;
}
