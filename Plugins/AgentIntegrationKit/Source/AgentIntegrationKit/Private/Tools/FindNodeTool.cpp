// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/FindNodeTool.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/FuzzyMatchingUtils.h"
#include "Tools/NeoStackToolUtils.h"
#include "Tools/NodeSpawnerCache.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Blueprint includes
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "BlueprintActionDatabase.h"
#include "BlueprintActionFilter.h"
#include "BlueprintActionMenuBuilder.h"
#include "BlueprintActionMenuUtils.h"
#include "BlueprintActionMenuItem.h"
#include "BlueprintNodeSpawner.h"
#include "BlueprintNodeSignature.h"
#include "BlueprintVariableNodeSpawner.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "K2Node_Event.h"
#include "K2Node_MacroInstance.h"
#include "Kismet2/KismetEditorUtilities.h"

// Animation Blueprint
#include "Animation/AnimBlueprint.h"
#include "AnimationGraph.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNodeBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "AnimationTransitionGraph.h"
#include "AnimationCustomTransitionGraph.h"
#include "K2Node_Composite.h"

// Behavior Tree (only need the header for DetectGraphType cast)
#include "BehaviorTree/BehaviorTree.h"

// EQS (only need the header for DetectGraphType cast)
#include "EnvironmentQuery/EnvQuery.h"

// Material
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialExpression.h"

// Niagara
#include "NiagaraSystem.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraCommon.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"
#include "UObject/ObjectKey.h"

// EdGraph schema (needed for general graph operations)
#include "EdGraph/EdGraphSchema.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

// PCG (Procedural Content Generation) - only available in UE 5.7+
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "PCGGraph.h"
#include "PCGSettings.h"
#include "PCGPin.h"
#include "PCGEditor.h"
#include "Schema/PCGEditorGraphSchema.h"
#include "Schema/PCGEditorGraphSchemaActions.h"
#include "Nodes/PCGEditorGraphNodeBase.h"

// Global cache for PCG schema actions - shared with EditGraphTool, keyed by asset+graph
TMap<FString, TMap<FString, TSharedPtr<FEdGraphSchemaAction>>> PCGActionCacheByKey;
#endif // UE 5.7+

// MetaSound support
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphNode.h"

// Global cache for MetaSound schema actions - shared with EditGraphTool, keyed by asset+graph
TMap<FString, TMap<FString, TSharedPtr<FEdGraphSchemaAction>>> MetaSoundActionCacheByKey;

static FString MakeGraphActionCacheKey(const FString& AssetPath, const FString& GraphName)
{
	return AssetPath + TEXT("|") + GraphName;
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

TSharedPtr<FJsonObject> FFindNodeTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> AssetProp = MakeShared<FJsonObject>();
	AssetProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetProp->SetStringField(TEXT("description"), TEXT("Asset name or path (Blueprint, Material, BehaviorTree, AnimBlueprint, NiagaraSystem, PCGGraph, MetaSoundSource, MetaSoundPatch, EnvironmentQuery)"));
	Properties->SetObjectField(TEXT("asset"), AssetProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (e.g., /Game/Blueprints)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> GraphNameProp = MakeShared<FJsonObject>();
	GraphNameProp->SetStringField(TEXT("type"), TEXT("string"));
	GraphNameProp->SetStringField(TEXT("description"),
		TEXT("Specific graph to search. Accepts raw graph name (e.g., EventGraph, AnimGraph) or typed selector for AnimBPs: "
		     "animgraph:<GraphName>, statemachine:<AnimGraph>/<StateMachine>, state:<AnimGraph>/<State>, "
		     "transition:<AnimGraph>/<From->To>, custom_transition:<AnimGraph>/<From->To>, conduit:<AnimGraph>/<Conduit>, "
		     "composite:<AnimGraph>/<Composite>."));
	Properties->SetObjectField(TEXT("graph_name"), GraphNameProp);

	TSharedPtr<FJsonObject> QueryProp = MakeShared<FJsonObject>();
	QueryProp->SetStringField(TEXT("type"), TEXT("array"));
	QueryProp->SetStringField(TEXT("description"), TEXT("Search terms to find nodes (e.g., ['Print', 'SetTimer'])"));
	TSharedPtr<FJsonObject> QueryItems = MakeShared<FJsonObject>();
	QueryItems->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetObjectField(TEXT("items"), QueryItems);
	Properties->SetObjectField(TEXT("query"), QueryProp);

	TSharedPtr<FJsonObject> CategoryProp = MakeShared<FJsonObject>();
	CategoryProp->SetStringField(TEXT("type"), TEXT("string"));
	CategoryProp->SetStringField(TEXT("description"), TEXT("Filter by node category"));
	Properties->SetObjectField(TEXT("category"), CategoryProp);

	TSharedPtr<FJsonObject> InputTypeProp = MakeShared<FJsonObject>();
	InputTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	InputTypeProp->SetStringField(TEXT("description"), TEXT("Filter nodes by input pin type"));
	Properties->SetObjectField(TEXT("input_type"), InputTypeProp);

	TSharedPtr<FJsonObject> OutputTypeProp = MakeShared<FJsonObject>();
	OutputTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	OutputTypeProp->SetStringField(TEXT("description"), TEXT("Filter nodes by output pin type"));
	Properties->SetObjectField(TEXT("output_type"), OutputTypeProp);

	TSharedPtr<FJsonObject> SearchClassProp = MakeShared<FJsonObject>();
	SearchClassProp->SetStringField(TEXT("type"), TEXT("string"));
	SearchClassProp->SetStringField(TEXT("description"),
		TEXT("Search for functions on a specific UClass instead of current Blueprint context. "
		     "Use for cross-Blueprint calls: first read_file the target Blueprint to find component types, "
		     "then use search_class to find available functions on that component class. "
		     "Examples: \"CameraComponent\", \"CharacterMovementComponent\", \"SkeletalMeshComponent\". "
		     "Also works with Blueprint classes: \"BP_Character_C\" (add _C suffix for generated class)."));
	Properties->SetObjectField(TEXT("search_class"), SearchClassProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FFindNodeTool::Execute(const TSharedPtr<FJsonObject>& Args)
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

	// Parse optional parameters
	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	FString CategoryFilter;
	Args->TryGetStringField(TEXT("category"), CategoryFilter);

	// Parse pin type filters - find nodes by what they accept/output
	FString InputTypeFilter;
	Args->TryGetStringField(TEXT("input_type"), InputTypeFilter);
	InputTypeFilter = InputTypeFilter.ToLower();

	FString OutputTypeFilter;
	Args->TryGetStringField(TEXT("output_type"), OutputTypeFilter);
	OutputTypeFilter = OutputTypeFilter.ToLower();

	// Parse search_class for cross-Blueprint function discovery
	FString SearchClass;
	Args->TryGetStringField(TEXT("search_class"), SearchClass);

	// Parse limit parameter (default 15 per query)
	int32 Limit = 15;
	if (Args->HasField(TEXT("limit")))
	{
		Limit = FMath::Max(1, static_cast<int32>(Args->GetNumberField(TEXT("limit"))));
	}

	// Parse query array
	TArray<FString> Queries;
	const TArray<TSharedPtr<FJsonValue>>* QueryArray;
	if (Args->TryGetArrayField(TEXT("query"), QueryArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *QueryArray)
		{
			FString Query;
			if (Value->TryGetString(Query) && !Query.IsEmpty())
			{
				Queries.Add(Query.ToLower());
			}
		}
	}

	if (Queries.Num() == 0)
	{
		return FToolResult::Fail(TEXT("Missing required parameter: query (array of search terms)"));
	}

	// Build asset path and load
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(AssetName, Path);

	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// If search_class is specified, search for functions on that class instead of current Blueprint context
	if (!SearchClass.IsEmpty())
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (!Blueprint)
		{
			return FToolResult::Fail(TEXT("search_class requires a Blueprint asset to provide graph context"));
		}

		TArray<FNodeInfo> Results = FindNodesForClass(Blueprint, SearchClass, Queries, CategoryFilter, InputTypeFilter, OutputTypeFilter);
		FString Output = FormatClassResults(SearchClass, Queries, Results, Limit);
		return FToolResult::Ok(Output);
	}

	// Detect graph type and find nodes
	EGraphType GraphType = DetectGraphType(Asset);
	TArray<FNodeInfo> Results;

	switch (GraphType)
	{
	case EGraphType::Blueprint:
	case EGraphType::AnimBlueprint:
		Results = FindNodesInBlueprint(Cast<UBlueprint>(Asset), GraphName, Queries, CategoryFilter, InputTypeFilter, OutputTypeFilter);
		break;

	case EGraphType::BehaviorTree:
		Results = FindNodesInBehaviorTree(Asset, Queries, CategoryFilter, GraphName);
		break;

	case EGraphType::Material:
		Results = FindNodesInMaterial(Asset, Queries, CategoryFilter);
		break;

	case EGraphType::Niagara:
		Results = FindNodesInNiagara(Asset, Queries, CategoryFilter);
		break;

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	case EGraphType::PCG:
		Results = FindNodesInPCG(Asset, Queries, CategoryFilter);
		break;
#endif

	case EGraphType::MetaSound:
		Results = FindNodesInMetaSound(Asset, Queries, CategoryFilter);
		break;

	case EGraphType::EQS:
		Results = FindNodesInEQS(Asset, Queries, CategoryFilter);
		break;

	default:
		return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type: %s"), *Asset->GetClass()->GetName()));
	}

	// Format and return results
	FString Output = FormatResults(AssetName, GraphName, GraphType, Queries, Results, Limit);
	return FToolResult::Ok(Output);
}

FFindNodeTool::EGraphType FFindNodeTool::DetectGraphType(UObject* Asset) const
{
	if (!Asset)
	{
		return EGraphType::Unknown;
	}

	if (Cast<UAnimBlueprint>(Asset))
	{
		return EGraphType::AnimBlueprint;
	}
	if (Cast<UBlueprint>(Asset))
	{
		return EGraphType::Blueprint;
	}
	if (Cast<UBehaviorTree>(Asset))
	{
		return EGraphType::BehaviorTree;
	}
	if (Cast<UEnvQuery>(Asset))
	{
		return EGraphType::EQS;
	}
	if (Cast<UMaterial>(Asset) || Cast<UMaterialFunction>(Asset))
	{
		return EGraphType::Material;
	}
	if (Cast<UNiagaraSystem>(Asset))
	{
		return EGraphType::Niagara;
	}
	if (Cast<UMetaSoundSource>(Asset) || Cast<UMetaSoundPatch>(Asset))
	{
		return EGraphType::MetaSound;
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	if (Cast<UPCGGraph>(Asset) || Cast<UPCGGraphInterface>(Asset))
	{
		return EGraphType::PCG;
	}
#endif

	return EGraphType::Unknown;
}

FString FFindNodeTool::GraphTypeToString(EGraphType Type) const
{
	switch (Type)
	{
	case EGraphType::Blueprint: return TEXT("Blueprint");
	case EGraphType::AnimBlueprint: return TEXT("AnimBlueprint");
	case EGraphType::BehaviorTree: return TEXT("BehaviorTree");
	case EGraphType::Material: return TEXT("Material");
	case EGraphType::Niagara: return TEXT("Niagara");
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	case EGraphType::PCG: return TEXT("PCG");
#endif
	case EGraphType::MetaSound: return TEXT("MetaSound");
	case EGraphType::EQS: return TEXT("EQS");
	default: return TEXT("Unknown");
	}
}

UEdGraph* FFindNodeTool::GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const
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
			GraphNameLower.Contains(TEXT("composite:")))
		{
			TArray<TPair<UEdGraph*, FString>> Graphs;
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
								FString FromState = TEXT("?");
								FString ToState = TEXT("?");
								if (TransitionNode->Pins.Num() >= 2)
								{
									if (UAnimStateNodeBase* PrevState = TransitionNode->GetPreviousState())
									{
										FromState = PrevState->GetNodeTitle(ENodeTitleType::ListView).ToString();
									}
									if (UAnimStateNodeBase* NextState = TransitionNode->GetNextState())
									{
										ToState = NextState->GetNodeTitle(ENodeTitleType::ListView).ToString();
									}
								}

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

	// If no name specified, return the main event graph (UbergraphPages[0])
	if (GraphName.IsEmpty())
	{
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			return Blueprint->UbergraphPages[0];
		}
		return nullptr;
	}

	// Search UbergraphPages (EventGraph, etc.)
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

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInBlueprint(
	UBlueprint* Blueprint,
	const FString& GraphName,
	const TArray<FString>& Queries,
	const FString& CategoryFilter,
	const FString& InputTypeFilter,
	const FString& OutputTypeFilter)
{
	TArray<FNodeInfo> Results;

	if (!Blueprint)
	{
		return Results;
	}

	UEdGraph* TargetGraph = GetGraphByName(Blueprint, GraphName);
	if (!TargetGraph)
	{
		// Try to get any available graph
		if (Blueprint->UbergraphPages.Num() > 0)
		{
			TargetGraph = Blueprint->UbergraphPages[0];
		}
		else if (Blueprint->FunctionGraphs.Num() > 0)
		{
			TargetGraph = Blueprint->FunctionGraphs[0];
		}
	}

	// Must have a valid graph to query nodes
	if (!TargetGraph)
	{
		return Results;
	}

	// Match the editor context menu flow (SBlueprintActionMenu / MakeContextMenu),
	// instead of scanning the full action database and ad-hoc filtering.
	FBlueprintActionDatabase::Get().RefreshAssetActions(Blueprint);

	// Get the graph schema for compatibility checking
	const UEdGraphSchema* GraphSchema = TargetGraph->GetSchema();
	if (!GraphSchema)
	{
		return Results;
	}

	FBlueprintActionContext FilterContext;
	FilterContext.Blueprints.Add(Blueprint);
	FilterContext.Graphs.Add(TargetGraph);

	FBlueprintActionMenuBuilder MenuBuilder(FBlueprintActionMenuBuilder::DefaultConfig);
	const uint32 ClassTargetMask = EContextTargetFlags::TARGET_Blueprint |
		EContextTargetFlags::TARGET_BlueprintLibraries |
		EContextTargetFlags::TARGET_SubComponents |
		EContextTargetFlags::TARGET_NonImportedTypes;
	FBlueprintActionMenuUtils::MakeContextMenu(FilterContext, false, ClassTargetMask, MenuBuilder);

	for (int32 i = 0; i < MenuBuilder.GetNumActions(); ++i)
	{
		TSharedPtr<FEdGraphSchemaAction> Action = MenuBuilder.GetSchemaAction(i);
		if (!Action.IsValid() || Action->GetTypeId() != FBlueprintActionMenuItem::StaticGetTypeId())
		{
			continue;
		}

		FBlueprintActionMenuItem* BPMenuItem = static_cast<FBlueprintActionMenuItem*>(Action.Get());
		const UBlueprintNodeSpawner* RawSpawner = BPMenuItem ? BPMenuItem->GetRawAction() : nullptr;
		UBlueprintNodeSpawner* Spawner = const_cast<UBlueprintNodeSpawner*>(RawSpawner);
		if (!Spawner || !Spawner->NodeClass)
		{
			continue;
		}

		// Check if this node type is compatible with the graph's schema
		UEdGraphNode* NodeCDO = Spawner->NodeClass->GetDefaultObject<UEdGraphNode>();
		if (!NodeCDO || !NodeCDO->CanCreateUnderSpecifiedSchema(GraphSchema))
		{
			continue;
		}

		// Get UI spec for menu name, category, etc.
		const FBlueprintActionUiSpec& UiSpec = Spawner->PrimeDefaultUiSpec(TargetGraph);

		FString NodeName = UiSpec.MenuName.ToString();
		FString NodeCategory = UiSpec.Category.ToString();
		FString NodeKeywords = UiSpec.Keywords.ToString();
		FString NodeTooltip = UiSpec.Tooltip.ToString();

		// For variable spawners, generate fallback name from property if UiSpec is empty
		if (const UBlueprintVariableNodeSpawner* VarSpawner = Cast<UBlueprintVariableNodeSpawner>(Spawner))
		{
			// If MenuName is empty, construct it from the property name
			if (NodeName.IsEmpty())
			{
				if (FProperty const* VarProp = VarSpawner->GetVarProperty())
				{
					FString PropName = VarProp->GetName();
					FString DisplayName = FName::NameToDisplayString(PropName, false);
					bool bIsGetter = Spawner->NodeClass && Spawner->NodeClass->IsChildOf(UK2Node_VariableGet::StaticClass());
					NodeName = FString::Printf(TEXT("%s %s"), bIsGetter ? TEXT("Get") : TEXT("Set"), *DisplayName);
					// Also add the raw property name as a keyword for better matching
					if (NodeKeywords.IsEmpty())
					{
						NodeKeywords = PropName.ToLower();
					}
					else
					{
						NodeKeywords += TEXT(" ") + PropName.ToLower();
					}
				}
			}
		}

		// Skip empty names
		if (NodeName.IsEmpty())
		{
			continue;
		}

		// Check category filter
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
		{
			continue;
		}

		// Check query match
		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(NodeName, NodeKeywords, Queries, MatchedQuery, Score))
		{
			continue;
		}

		// Cache the spawner and get a semantic ID
		// Format: TYPE:BlueprintName:NodeName:UniqueId
		// This survives GC and can recreate spawners on demand
		FString SpawnerId = FNodeSpawnerCache::Get().CacheSpawner(Spawner, Blueprint, NodeName);

		// Create node info
		FNodeInfo Info;
		Info.Name = NodeName;
		Info.SpawnerId = SpawnerId;
		Info.Category = NodeCategory;
		Info.Tooltip = NodeTooltip;
		Info.Keywords = NodeKeywords;
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		// Try to get pin info and flags from template node
		if (TargetGraph)
		{
			UEdGraphNode* TemplateNode = Spawner->GetTemplateNode(TargetGraph);
			if (TemplateNode)
			{
				if (TemplateNode->Pins.Num() == 0)
				{
					TemplateNode->AllocateDefaultPins();
				}
				ExtractPinInfo(TemplateNode, Info.InputPins, Info.OutputPins);
				ExtractNodeFlags(TemplateNode, Info.Flags);
			}
		}

		// Check pin type filters - skip nodes that don't match
		if (!MatchesPinType(Info.InputPins, InputTypeFilter))
		{
			continue;
		}
		if (!MatchesPinType(Info.OutputPins, OutputTypeFilter))
		{
			continue;
		}

		Results.Add(Info);
	}

	return Results;
}

// FindNodesInBehaviorTree is implemented in FindNodeTool_BehaviorTree.cpp

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInMaterial(
	UObject* Material,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	// Iterate all MaterialExpression classes
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
		{
			continue;
		}

		if (!Class->IsChildOf(UMaterialExpression::StaticClass()))
		{
			continue;
		}

		// Skip private expressions
		if (Class->HasMetaData(TEXT("Private")))
		{
			continue;
		}

		// Get display name
		FString NodeName = Class->GetName();
		static const FString ExpressionPrefix = TEXT("MaterialExpression");
		if (NodeName.StartsWith(ExpressionPrefix))
		{
			NodeName.MidInline(ExpressionPrefix.Len(), MAX_int32, EAllowShrinking::No);
		}

		if (Class->HasMetaData(TEXT("DisplayName")))
		{
			NodeName = Class->GetDisplayNameText().ToString();
		}

		// Get category from CDO
		FString NodeCategory;
		if (UMaterialExpression* CDO = Cast<UMaterialExpression>(Class->GetDefaultObject()))
		{
			if (CDO->MenuCategories.Num() > 0)
			{
				NodeCategory = CDO->MenuCategories[0].ToString();
			}
		}

		// Check category filter
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
		{
			continue;
		}

		// Check query match
		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(NodeName, TEXT(""), Queries, MatchedQuery, Score))
		{
			continue;
		}

		FNodeInfo Info;
		Info.Name = NodeName;
		Info.SpawnerId = Class->GetPathName();
		Info.Category = NodeCategory;
		Info.Tooltip = Class->GetToolTipText().ToString();
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		Results.Add(Info);
	}

	return Results;
}

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInNiagara(
	UObject* NiagaraSystem,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	// Get filtered Niagara module scripts
	FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions FilterOptions;
	FilterOptions.ScriptUsageToInclude = ENiagaraScriptUsage::Module;
	FilterOptions.bIncludeDeprecatedScripts = false;
	FilterOptions.bIncludeNonLibraryScripts = true;  // Include project modules

	TArray<FAssetData> FilteredAssets;
	FNiagaraEditorUtilities::GetFilteredScriptAssets(FilterOptions, FilteredAssets);

	for (const FAssetData& AssetData : FilteredAssets)
	{
		// Get display name
		FString ModuleName = AssetData.AssetName.ToString();
		FString DisplayName = FNiagaraEditorUtilities::FormatScriptName(FName(*ModuleName),
			FNiagaraEditorUtilities::IsScriptAssetInLibrary(AssetData)).ToString();

		if (DisplayName.IsEmpty())
		{
			DisplayName = FName::NameToDisplayString(ModuleName, false);
		}

		// Get category from asset path
		FString ModulePath = AssetData.PackagePath.ToString();
		FString NodeCategory;

		// Determine category from path structure
		if (ModulePath.Contains(TEXT("/Spawn")))
		{
			NodeCategory = TEXT("Spawn");
		}
		else if (ModulePath.Contains(TEXT("/Update")))
		{
			NodeCategory = TEXT("Update");
		}
		else if (ModulePath.Contains(TEXT("/Forces")))
		{
			NodeCategory = TEXT("Forces");
		}
		else if (ModulePath.Contains(TEXT("/Location")))
		{
			NodeCategory = TEXT("Location");
		}
		else if (ModulePath.Contains(TEXT("/Velocity")))
		{
			NodeCategory = TEXT("Velocity");
		}
		else if (ModulePath.Contains(TEXT("/Color")))
		{
			NodeCategory = TEXT("Color");
		}
		else if (ModulePath.Contains(TEXT("/Size")))
		{
			NodeCategory = TEXT("Size");
		}
		else if (ModulePath.Contains(TEXT("/Collision")))
		{
			NodeCategory = TEXT("Collision");
		}
		else if (ModulePath.Contains(TEXT("/Emitter")))
		{
			NodeCategory = TEXT("Emitter");
		}
		else if (ModulePath.Contains(TEXT("/System")))
		{
			NodeCategory = TEXT("System");
		}
		else
		{
			// Get source info
			TTuple<EScriptSource, FText> SourceInfo = FNiagaraEditorUtilities::GetScriptSource(AssetData);
			NodeCategory = SourceInfo.Get<1>().ToString();
			if (NodeCategory.IsEmpty())
			{
				NodeCategory = TEXT("General");
			}
		}

		// Check category filter
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
		{
			continue;
		}

		// Check query match - include path in keywords for searching
		FString MatchedQuery;
		int32 Score = 0;
		FString Keywords = ModulePath.ToLower() + TEXT(" ") + ModuleName.ToLower();
		if (!MatchesQuery(DisplayName, Keywords, Queries, MatchedQuery, Score))
		{
			continue;
		}

		FNodeInfo Info;
		Info.Name = DisplayName;
		Info.SpawnerId = AssetData.GetSoftObjectPath().ToString();
		Info.Category = NodeCategory;
		Info.Keywords = Keywords;
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		// Try to get description and parameters from the script
		if (UNiagaraScript* Script = Cast<UNiagaraScript>(AssetData.GetAsset()))
		{
			// Get description from versioned script data
			if (FVersionedNiagaraScriptData* ScriptData = Script->GetLatestScriptData())
			{
				if (!ScriptData->Description.IsEmpty())
				{
					Info.Tooltip = ScriptData->Description.ToString();
				}
			}

			// Extract input parameters using exported GetAllMetaData() API
			if (UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource()))
			{
				if (UNiagaraGraph* Graph = Source->NodeGraph)
				{
					// GetAllMetaData() is exported and returns all script variables
					const TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& AllMetaData = Graph->GetAllMetaData();
					for (const auto& Pair : AllMetaData)
					{
						const FNiagaraVariable& Var = Pair.Key;
						FString TypeName = Var.GetType().GetName();
						Info.InputPins.Add(FString::Printf(TEXT("%s (%s)"),
							*Var.GetName().ToString(), *TypeName));
					}
				}
			}
		}

		Results.Add(Info);
	}

	// ===== Dynamic Input Discovery =====
	// When the category filter includes "dynamic" or queries contain "dynamic_input",
	// also search for dynamic input scripts that agents can use with set_parameters mode:"dynamic_input".
	// This is always included alongside modules (agents need both) unless category explicitly
	// restricts to only modules or only dynamic inputs.
	bool bIncludeDynamicInputs = CategoryFilter.IsEmpty()
		|| CategoryFilter.Contains(TEXT("dynamic"), ESearchCase::IgnoreCase)
		|| CategoryFilter.Contains(TEXT("DynamicInput"), ESearchCase::IgnoreCase);

	// Also check if any query explicitly asks for dynamic inputs
	if (!bIncludeDynamicInputs)
	{
		for (const FString& Q : Queries)
		{
			if (Q.Contains(TEXT("dynamic"), ESearchCase::IgnoreCase))
			{
				bIncludeDynamicInputs = true;
				break;
			}
		}
	}

	if (bIncludeDynamicInputs)
	{
		FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions DIFilterOptions;
		DIFilterOptions.ScriptUsageToInclude = ENiagaraScriptUsage::DynamicInput;
		DIFilterOptions.bIncludeDeprecatedScripts = false;
		DIFilterOptions.bIncludeNonLibraryScripts = true;

		TArray<FAssetData> DIAssets;
		FNiagaraEditorUtilities::GetFilteredScriptAssets(DIFilterOptions, DIAssets);

		for (const FAssetData& AssetData : DIAssets)
		{
			FString DIName = AssetData.AssetName.ToString();
			FString DIDisplayName = FNiagaraEditorUtilities::FormatScriptName(FName(*DIName),
				FNiagaraEditorUtilities::IsScriptAssetInLibrary(AssetData)).ToString();
			if (DIDisplayName.IsEmpty())
			{
				DIDisplayName = FName::NameToDisplayString(DIName, false);
			}

			// Determine the output type of this dynamic input
			FString OutputType = TEXT("Unknown");
			if (UNiagaraScript* DIScript = Cast<UNiagaraScript>(AssetData.GetAsset()))
			{
				UNiagaraNodeOutput* OutNode = FNiagaraEditorUtilities::GetScriptOutputNode(*DIScript);
				if (OutNode && OutNode->GetOutputs().Num() > 0)
				{
					OutputType = OutNode->GetOutputs()[0].GetType().GetName();
				}
			}

			FString DICategory = FString::Printf(TEXT("DynamicInput/%s"), *OutputType);

			// Check category filter — skip if filtering for non-dynamic categories
			if (!CategoryFilter.IsEmpty()
				&& !CategoryFilter.Contains(TEXT("dynamic"), ESearchCase::IgnoreCase)
				&& !CategoryFilter.Contains(TEXT("DynamicInput"), ESearchCase::IgnoreCase)
				&& !MatchesCategory(DICategory, CategoryFilter))
			{
				continue;
			}

			FString DIKeywords = DIName.ToLower() + TEXT(" ") + OutputType.ToLower()
				+ TEXT(" dynamic input dynamicinput");
			FString MatchedQuery;
			int32 Score = 0;
			if (!MatchesQuery(DIDisplayName, DIKeywords, Queries, MatchedQuery, Score))
			{
				continue;
			}

			FNodeInfo Info;
			Info.Name = DIDisplayName;
			Info.SpawnerId = AssetData.GetSoftObjectPath().ToString();
			Info.Category = DICategory;
			Info.Keywords = DIKeywords;
			Info.MatchedQuery = MatchedQuery;
			Info.Score = Score;
			Info.OutputPins.Add(FString::Printf(TEXT("Output (%s)"), *OutputType));

			// Get description and input parameters
			if (UNiagaraScript* DIScript = Cast<UNiagaraScript>(AssetData.GetAsset()))
			{
				if (FVersionedNiagaraScriptData* ScriptData = DIScript->GetLatestScriptData())
				{
					if (!ScriptData->Description.IsEmpty())
					{
						Info.Tooltip = ScriptData->Description.ToString();
					}
				}

				if (UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(DIScript->GetLatestSource()))
				{
					if (UNiagaraGraph* Graph = Source->NodeGraph)
					{
						const TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& AllMetaData = Graph->GetAllMetaData();
						for (const auto& Pair : AllMetaData)
						{
							const FNiagaraVariable& Var = Pair.Key;
							FString TypeName = Var.GetType().GetName();
							Info.InputPins.Add(FString::Printf(TEXT("%s (%s)"),
								*Var.GetName().ToString(), *TypeName));
						}
					}
				}
			}

			Results.Add(Info);
		}
	}

	return Results;
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInPCG(
	UObject* Asset,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	UPCGGraphInterface* PCGGraphInterface = Cast<UPCGGraphInterface>(Asset);
	if (!PCGGraphInterface)
	{
		return Results;
	}

	// Open the PCG editor to get the editor graph with proper schema
	UEdGraph* EditorGraph = nullptr;
	if (GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			if (!AssetEditorSubsystem->FindEditorForAsset(Asset, false))
			{
				AssetEditorSubsystem->OpenEditorForAsset(Asset);
			}

			WaitForCondition([&]()
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
				if (!(EditorInstance && EditorInstance->GetEditorName() == TEXT("PCGEditor")))
				{
					return false;
				}

					FPCGEditor* PCGEditor = static_cast<FPCGEditor*>(EditorInstance);
					EditorGraph = PCGEditor ? reinterpret_cast<UEdGraph*>(PCGEditor->GetPCGEditorGraph()) : nullptr;
					return EditorGraph != nullptr;
				});
		}
	}

	if (!EditorGraph)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("FindNodesInPCG: Could not get PCG editor graph. Make sure the PCG asset is open in the editor."));
		return Results;
	}

	// Get the schema and use GetGraphContextActions - the universal UEdGraph API
	const UEdGraphSchema* Schema = EditorGraph->GetSchema();
	if (!Schema)
	{
		return Results;
	}

	const FString CacheKey = MakeGraphActionCacheKey(Asset->GetPathName(), EditorGraph->GetName());
	TMap<FString, TSharedPtr<FEdGraphSchemaAction>>& PCGActionCache = PCGActionCacheByKey.FindOrAdd(CacheKey);
	PCGActionCache.Empty();

	// Build context menu to get all available actions
	FGraphContextMenuBuilder ContextMenuBuilder(EditorGraph);
	Schema->GetGraphContextActions(ContextMenuBuilder);

	int32 ActionIndex = 0;
	for (int32 i = 0; i < ContextMenuBuilder.GetNumActions(); i++)
	{
		TSharedPtr<FEdGraphSchemaAction> Action = ContextMenuBuilder.GetSchemaAction(i);
		if (!Action.IsValid())
		{
			continue;
		}

		// Get action info
		FString NodeName = Action->GetMenuDescription().ToString();
		FString NodeCategory = Action->GetCategory().ToString();
		FString Keywords = Action->GetKeywords().ToString();
		FString Tooltip = Action->GetTooltipDescription().ToString();

		if (NodeName.IsEmpty())
		{
			continue;
		}

		// Check category filter
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
		{
			continue;
		}

		// Build searchable keywords
		FString SearchKeywords = NodeName.ToLower() + TEXT(" ") + Keywords.ToLower() + TEXT(" ") + NodeCategory.ToLower();

		// Check query match
		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(NodeName, SearchKeywords, Queries, MatchedQuery, Score))
		{
			continue;
		}

		// Generate unique spawner ID and cache the action
		FString SpawnerId = FString::Printf(TEXT("PCG_ACTION:%d:%s"), ActionIndex, *NodeName.Replace(TEXT(" "), TEXT("_")));
		PCGActionCache.Add(SpawnerId, Action);

		FNodeInfo Info;
		Info.Name = NodeName;
		Info.SpawnerId = SpawnerId;
		Info.Category = NodeCategory;
		Info.Keywords = Keywords;
		Info.Tooltip = Tooltip;
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		// Try to get pin info from native element actions
		if (Action->GetTypeId() == FPCGEditorGraphSchemaAction_NewNativeElement::StaticGetTypeId())
		{
			const FPCGEditorGraphSchemaAction_NewNativeElement* NativeAction =
				static_cast<const FPCGEditorGraphSchemaAction_NewNativeElement*>(Action.Get());
			if (NativeAction->SettingsClass)
			{
				UPCGSettings* SettingsCDO = NativeAction->SettingsClass->GetDefaultObject<UPCGSettings>();
				if (SettingsCDO)
				{
					TArray<FPCGPinProperties> InputPinProps = SettingsCDO->InputPinProperties();
					TArray<FPCGPinProperties> OutputPinProps = SettingsCDO->OutputPinProperties();

					for (const FPCGPinProperties& PinProp : InputPinProps)
					{
						Info.InputPins.Add(PinProp.Label.ToString());
					}
					for (const FPCGPinProperties& PinProp : OutputPinProps)
					{
						Info.OutputPins.Add(PinProp.Label.ToString());
					}
				}
			}
		}

		Results.Add(Info);
		ActionIndex++;
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesInPCG: Found %d nodes matching queries"), Results.Num());
	return Results;
}
#endif // UE 5.7+ PCG support

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInMetaSound(
	UObject* Asset,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	// Get the editor graph from the MetaSound asset
	UEdGraph* EditorGraph = nullptr;

	if (UMetaSoundSource* MSSource = Cast<UMetaSoundSource>(Asset))
	{
		EditorGraph = MSSource->GetGraph();
	}
	else if (UMetaSoundPatch* MSPatch = Cast<UMetaSoundPatch>(Asset))
	{
		EditorGraph = MSPatch->GetGraph();
	}

	// If GetGraph() returned null, open the editor and find the graph via schema check
	if (!EditorGraph && GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			if (!AssetEditorSubsystem->FindEditorForAsset(Asset, false))
			{
				AssetEditorSubsystem->OpenEditorForAsset(Asset);
			}

			WaitForCondition([&]()
			{
				if (UMetaSoundSource* MSSource = Cast<UMetaSoundSource>(Asset))
				{
					EditorGraph = MSSource->GetGraph();
				}
				else if (UMetaSoundPatch* MSPatch = Cast<UMetaSoundPatch>(Asset))
				{
					EditorGraph = MSPatch->GetGraph();
				}
				return EditorGraph != nullptr;
			});

			// Do not scan global graph objects. Engine flow relies on asset/editor-owned graph retrieval.
		}
	}

	if (!EditorGraph)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("FindNodesInMetaSound: Could not get MetaSound editor graph."));
		return Results;
	}

	const UEdGraphSchema* Schema = EditorGraph->GetSchema();
	if (!Schema)
	{
		return Results;
	}

	const FString CacheKey = MakeGraphActionCacheKey(Asset->GetPathName(), EditorGraph->GetName());
	TMap<FString, TSharedPtr<FEdGraphSchemaAction>>& MetaSoundActionCache = MetaSoundActionCacheByKey.FindOrAdd(CacheKey);
	MetaSoundActionCache.Empty();

	FGraphContextMenuBuilder ContextMenuBuilder(EditorGraph);
	Schema->GetGraphContextActions(ContextMenuBuilder);

	int32 ActionIndex = 0;
	for (int32 i = 0; i < ContextMenuBuilder.GetNumActions(); i++)
	{
		TSharedPtr<FEdGraphSchemaAction> Action = ContextMenuBuilder.GetSchemaAction(i);
		if (!Action.IsValid())
		{
			continue;
		}

		FString NodeName = Action->GetMenuDescription().ToString();
		FString NodeCategory = Action->GetCategory().ToString();
		FString Keywords = Action->GetKeywords().ToString();
		FString Tooltip = Action->GetTooltipDescription().ToString();

		if (NodeName.IsEmpty())
		{
			continue;
		}

		if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
		{
			continue;
		}

		FString SearchKeywords = NodeName.ToLower() + TEXT(" ") + Keywords.ToLower() + TEXT(" ") + NodeCategory.ToLower();

		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(NodeName, SearchKeywords, Queries, MatchedQuery, Score))
		{
			continue;
		}

		FString SpawnerId = FString::Printf(TEXT("MS_ACTION:%d:%s"), ActionIndex, *NodeName.Replace(TEXT(" "), TEXT("_")));
		MetaSoundActionCache.Add(SpawnerId, Action);

		FNodeInfo Info;
		Info.Name = NodeName;
		Info.SpawnerId = SpawnerId;
		Info.Category = NodeCategory;
		Info.Keywords = Keywords;
		Info.Tooltip = Tooltip;
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;

		Results.Add(Info);
		ActionIndex++;
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesInMetaSound: Found %d nodes matching queries"), Results.Num());
	return Results;
}

void FFindNodeTool::ExtractPinInfo(UEdGraphNode* TemplateNode, TArray<FString>& OutInputs, TArray<FString>& OutOutputs)
{
	if (!TemplateNode)
	{
		return;
	}

	for (UEdGraphPin* Pin : TemplateNode->Pins)
	{
		if (!Pin || Pin->bHidden)
		{
			continue;
		}

		FString PinStr = FString::Printf(TEXT("%s (%s)"),
			*Pin->PinName.ToString(),
			*PinTypeToString(Pin->PinType));

		// For input pins, show default value or indicate if required
		if (Pin->Direction == EGPD_Input)
		{
			// Skip exec pins for default value display
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				// Check if pin has a default value
				bool bHasDefault = false;
				FString DefaultStr;

				if (!Pin->DefaultValue.IsEmpty())
				{
					bHasDefault = true;
					DefaultStr = Pin->DefaultValue;
				}
				else if (Pin->DefaultObject)
				{
					bHasDefault = true;
					DefaultStr = Pin->DefaultObject->GetName();
				}
				else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
				{
					bHasDefault = true;
					DefaultStr = Pin->AutogeneratedDefaultValue;
				}

				if (bHasDefault)
				{
					// Truncate long default values
					if (DefaultStr.Len() > 50)
					{
						DefaultStr = DefaultStr.Left(47) + TEXT("...");
					}
					PinStr += FString::Printf(TEXT(" = %s"), *DefaultStr);
				}
				else if (!Pin->bNotConnectable)
				{
					// Pin has no default and can be connected - might need a value
					PinStr += TEXT(" [REQUIRED]");
				}
			}

			OutInputs.Add(PinStr);
		}
		else
		{
			OutOutputs.Add(PinStr);
		}
	}
}

void FFindNodeTool::ExtractNodeFlags(UEdGraphNode* TemplateNode, TArray<FString>& OutFlags)
{
	if (!TemplateNode)
	{
		return;
	}

	// Check if it's a K2Node (Blueprint node)
	if (UK2Node* K2Node = Cast<UK2Node>(TemplateNode))
	{
		// Pure nodes have no exec pins and no side effects
		if (K2Node->IsNodePure())
		{
			OutFlags.Add(TEXT("Pure"));
		}

		// Check for function call nodes to get more info
		if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(K2Node))
		{
			if (UFunction* Function = CallNode->GetTargetFunction())
			{
				// Const function - can be called from const contexts
				if (Function->HasAnyFunctionFlags(FUNC_Const))
				{
					OutFlags.Add(TEXT("Const"));
				}

				// Thread safe
				if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable) &&
					Function->HasMetaData(TEXT("BlueprintThreadSafe")))
				{
					OutFlags.Add(TEXT("ThreadSafe"));
				}

				// Static function
				if (Function->HasAnyFunctionFlags(FUNC_Static))
				{
					OutFlags.Add(TEXT("Static"));
				}

				// Check for latent via metadata (Latent keyword in UFUNCTION)
				if (Function->HasMetaData(TEXT("Latent")))
				{
					OutFlags.Add(TEXT("Latent"));
				}

				// Deprecated - check via metadata
				if (Function->HasMetaData(TEXT("DeprecatedFunction")))
				{
					OutFlags.Add(TEXT("Deprecated"));
				}

				// Development only
				if (Function->HasMetaData(TEXT("DevelopmentOnly")))
				{
					OutFlags.Add(TEXT("DevOnly"));
				}
			}
		}

		// Check for event nodes
		if (Cast<UK2Node_Event>(K2Node))
		{
			OutFlags.Add(TEXT("Event"));
		}

		// Check for macro instance
		if (Cast<UK2Node_MacroInstance>(K2Node))
		{
			OutFlags.Add(TEXT("Macro"));
		}

		// Compact node (displayed as small operator like +, -, etc.)
		if (K2Node->ShouldDrawCompact())
		{
			OutFlags.Add(TEXT("Compact"));
		}
	}

	// Check if node is deprecated via its own flag
	if (TemplateNode->IsDeprecated())
	{
		OutFlags.Add(TEXT("Deprecated"));
	}
}

FString FFindNodeTool::PinTypeToString(const FEdGraphPinType& PinType) const
{
	// Handle exec pins
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return TEXT("exec");
	}

	// Get base type name
	FString TypeName = PinType.PinCategory.ToString();

	// For object/struct types, include the subtype
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeName = PinType.PinSubCategoryObject->GetName();
	}
	else if (!PinType.PinSubCategory.IsNone())
	{
		TypeName = PinType.PinSubCategory.ToString();
	}

	// Handle containers
	if (PinType.ContainerType == EPinContainerType::Array)
	{
		TypeName = FString::Printf(TEXT("Array<%s>"), *TypeName);
	}
	else if (PinType.ContainerType == EPinContainerType::Set)
	{
		TypeName = FString::Printf(TEXT("Set<%s>"), *TypeName);
	}
	else if (PinType.ContainerType == EPinContainerType::Map)
	{
		TypeName = FString::Printf(TEXT("Map<%s>"), *TypeName);
	}

	// Handle reference
	if (PinType.bIsReference)
	{
		TypeName += TEXT("&");
	}

	return TypeName;
}

bool FFindNodeTool::MatchesQuery(const FString& NodeName, const FString& Keywords, const TArray<FString>& Queries, FString& OutMatchedQuery, int32& OutScore) const
{
	FString LowerName = NodeName.ToLower();
	FString LowerKeywords = Keywords.ToLower();
	OutScore = 0;

	// Scoring weights:
	// 100 = Exact name match (case-insensitive)
	// 80  = Name starts with query
	// 60  = Query is a word in name (word boundary match)
	// 50  = Normalized match (spaces removed) - handles "getmyint" matching "Get My Int"
	// 40  = Name contains query as substring
	// 35  = Acronym match (e.g., "mvm" -> "Move Mouse Vertically")  [NEW]
	// 30  = Levenshtein similarity >= 70% (typo tolerance)  [NEW]
	// 20  = Keyword match
	// 15  = Normalized keyword match (spaces removed)

	// Create normalized versions (no spaces) for fuzzy matching
	// This handles cases like query "getmyint" matching "Get My Int"
	FString NormalizedName = LowerName.Replace(TEXT(" "), TEXT(""));

	// Helper to check word boundary match on original (non-lowercased) text
	auto MatchesWithWordBoundary = [](const FString& OriginalText, const FString& LowerQuery) -> bool
	{
		FString LowerText = OriginalText.ToLower();
		int32 Index = LowerText.Find(LowerQuery, ESearchCase::CaseSensitive);
		if (Index == INDEX_NONE)
		{
			return false;
		}

		// Check if it's at start or preceded by non-alpha or is CamelCase boundary
		bool bStartOk = (Index == 0) ||
			!FChar::IsAlpha(OriginalText[Index - 1]) ||
			(FChar::IsLower(OriginalText[Index - 1]) && FChar::IsUpper(OriginalText[Index]));

		// Check if it's at end or followed by non-alpha or CamelCase boundary
		int32 EndIndex = Index + LowerQuery.Len();
		bool bEndOk = (EndIndex >= OriginalText.Len()) ||
			!FChar::IsAlpha(OriginalText[EndIndex]) ||
			FChar::IsUpper(OriginalText[EndIndex]);

		return bStartOk && bEndOk;
	};

	for (const FString& Query : Queries)
	{
		int32 CurrentScore = 0;

		// Check exact name match (case-insensitive)
		if (LowerName.Equals(Query))
		{
			CurrentScore = 100;
		}
		// Check if name starts with query
		else if (LowerName.StartsWith(Query))
		{
			CurrentScore = 80;
		}
		// Check word boundary match in name
		else if (MatchesWithWordBoundary(NodeName, Query))
		{
			CurrentScore = 60;
		}
		// Check normalized match (spaces removed from both)
		// Handles "getmyint" or "get myint" matching "Get My Int"
		else if (NormalizedName.Contains(Query.Replace(TEXT(" "), TEXT(""))))
		{
			CurrentScore = 50;
		}
		// Check simple contains in name (fallback, less relevant)
		else if (LowerName.Contains(Query))
		{
			CurrentScore = 40;
		}
		// NEW: Check acronym match (e.g., "mvm" -> "Move Mouse Vertically", "sa" -> "Spawn Actor")
		else
		{
			float AcronymScore = 0.0f;
			if (FFuzzyMatchingUtils::MatchesAsAcronym(Query, NodeName, AcronymScore))
			{
				// Scale acronym score (0.5-1.0) to integer score (35-45)
				CurrentScore = 35 + (int32)((AcronymScore - 0.5f) * 20.0f);
			}
		}

		// NEW: Check Levenshtein similarity for typo tolerance (only if no match yet and query is substantial)
		if (CurrentScore == 0 && Query.Len() >= 4)
		{
			float LevenshteinScore = FFuzzyMatchingUtils::CalculateLevenshteinScore(Query, LowerName);
			if (LevenshteinScore >= 0.7f)  // 70% similarity threshold
			{
				// Scale Levenshtein score (0.7-1.0) to integer score (30-40)
				CurrentScore = 30 + (int32)((LevenshteinScore - 0.7f) * 33.0f);
			}
		}

		// Check keywords (if still no match from name-based checks)
		if (CurrentScore == 0 && !LowerKeywords.IsEmpty())
		{
			if (LowerKeywords.Contains(Query))
			{
				CurrentScore = 20;
			}
			// Check normalized keywords (spaces removed)
			else if (LowerKeywords.Replace(TEXT(" "), TEXT("")).Contains(Query.Replace(TEXT(" "), TEXT(""))))
			{
				CurrentScore = 15;
			}
		}

		if (CurrentScore > OutScore)
		{
			OutScore = CurrentScore;
			OutMatchedQuery = Query;
		}
	}

	return OutScore > 0;
}

bool FFindNodeTool::MatchesCategory(const FString& NodeCategory, const FString& CategoryFilter) const
{
	if (CategoryFilter.IsEmpty())
	{
		return true;
	}

	return NodeCategory.Contains(CategoryFilter, ESearchCase::IgnoreCase);
}

bool FFindNodeTool::MatchesPinType(const TArray<FString>& Pins, const FString& TypeFilter) const
{
	if (TypeFilter.IsEmpty())
	{
		return true;
	}

	// Check if any pin contains the type filter
	// Pin format: "PinName (TypeName) = default" or "PinName (TypeName) [REQUIRED]"
	// We want to match the type part, e.g., "Array" matches "Array<wildcard>&"
	for (const FString& Pin : Pins)
	{
		// Extract the type from parentheses
		int32 OpenParen = Pin.Find(TEXT("("));
		int32 CloseParen = Pin.Find(TEXT(")"));
		if (OpenParen != INDEX_NONE && CloseParen != INDEX_NONE && CloseParen > OpenParen)
		{
			FString PinType = Pin.Mid(OpenParen + 1, CloseParen - OpenParen - 1).ToLower();
			if (PinType.Contains(TypeFilter))
			{
				return true;
			}
		}
	}

	return false;
}

FString FFindNodeTool::FormatResults(
	const FString& AssetName,
	const FString& GraphName,
	EGraphType GraphType,
	const TArray<FString>& Queries,
	const TArray<FNodeInfo>& Results,
	int32 Limit) const
{
	FString Output;

	// Header
	Output += FString::Printf(TEXT("# FIND NODES in %s (%s)\n"),
		*AssetName, *GraphTypeToString(GraphType));

	if (!GraphName.IsEmpty())
	{
		Output += FString::Printf(TEXT("Graph: %s\n"), *GraphName);
	}

	// Query info
	FString QueryStr = FString::Join(Queries, TEXT(", "));
	Output += FString::Printf(TEXT("Query: %s\n\n"), *QueryStr);

	// Results count
	Output += FString::Printf(TEXT("## Results (%d found, showing top %d per query)\n\n"), Results.Num(), Limit);

	if (Results.Num() == 0)
	{
		Output += TEXT("No matching nodes found.\n\n");
		Output += TEXT("TIPS:\n");
		Output += TEXT("- Math operators use simple names: 'add', 'multiply', 'subtract', 'divide'\n");
		Output += TEXT("- You can also search by symbol: '+', '-', '*', '/'\n");
		Output += TEXT("- Type-specific names like 'float+float' won't work (Type Promotion merges them)\n");
		Output += TEXT("- Try broader search terms or check category filter\n");
		return Output;
	}

	// Group by matched query
	TMap<FString, TArray<const FNodeInfo*>> GroupedResults;
	for (const FNodeInfo& Info : Results)
	{
		GroupedResults.FindOrAdd(Info.MatchedQuery).Add(&Info);
	}

	// Output each group
	for (const FString& Query : Queries)
	{
		TArray<const FNodeInfo*>* Group = GroupedResults.Find(Query);
		if (!Group || Group->Num() == 0)
		{
			continue;
		}

		// Sort by score descending (best matches first)
		Group->Sort([](const FNodeInfo& A, const FNodeInfo& B)
		{
			if (A.Score != B.Score)
			{
				return A.Score > B.Score;
			}
			// Secondary sort by name length (shorter names are often more relevant)
			return A.Name.Len() < B.Name.Len();
		});

		int32 TotalCount = Group->Num();
		int32 ShownCount = FMath::Min(TotalCount, Limit);

		if (TotalCount > Limit)
		{
			Output += FString::Printf(TEXT("### \"%s\" (%d of %d, +%d more)\n"),
				*Query, ShownCount, TotalCount, TotalCount - Limit);
			Output += TEXT("    TIP: Too many results? Add input_type/output_type filter (e.g., input_type=\"array\") or category filter.\n\n");
		}
		else
		{
			Output += FString::Printf(TEXT("### \"%s\" (%d)\n\n"), *Query, TotalCount);
		}

		for (int32 i = 0; i < ShownCount; ++i)
		{
			const FNodeInfo* Info = (*Group)[i];

			Output += FString::Printf(TEXT("+ %s\n"), *Info->Name);
			Output += FString::Printf(TEXT("  ID: %s\n"), *Info->SpawnerId);

			if (!Info->Category.IsEmpty())
			{
				Output += FString::Printf(TEXT("  Category: %s\n"), *Info->Category);
			}

			// Add node flags if present
			if (Info->Flags.Num() > 0)
			{
				Output += FString::Printf(TEXT("  Flags: %s\n"), *FString::Join(Info->Flags, TEXT(", ")));
			}

			// Add tooltip/description (truncate if too long)
			if (!Info->Tooltip.IsEmpty())
			{
				FString Desc = Info->Tooltip;
				// Remove newlines and truncate for readability
				Desc.ReplaceInline(TEXT("\r\n"), TEXT(" "), ESearchCase::CaseSensitive);
				Desc.ReplaceInline(TEXT("\n"), TEXT(" "), ESearchCase::CaseSensitive);
				if (Desc.Len() > 120)
				{
					Desc = Desc.Left(117) + TEXT("...");
				}
				Output += FString::Printf(TEXT("  Desc: %s\n"), *Desc);
			}

			// Input pins
			if (Info->InputPins.Num() > 0)
			{
				Output += TEXT("  Inputs:\n");
				for (const FString& Pin : Info->InputPins)
				{
					Output += FString::Printf(TEXT("    - %s\n"), *Pin);
				}
			}

			// Output pins
			if (Info->OutputPins.Num() > 0)
			{
				Output += TEXT("  Outputs:\n");
				for (const FString& Pin : Info->OutputPins)
				{
					Output += FString::Printf(TEXT("    - %s\n"), *Pin);
				}
			}

			Output += TEXT("\n");
		}
	}

	return Output;
}

UClass* FFindNodeTool::FindClassByName(const FString& ClassName) const
{
	if (ClassName.IsEmpty())
	{
		return nullptr;
	}

	// First try direct lookup - handles full path names like "/Script/Engine.CameraComponent"
	UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
	if (FoundClass)
	{
		return FoundClass;
	}

	// Try with common engine module prefixes for short names
	static const TArray<FString> ModulePrefixes = {
		TEXT("/Script/Engine."),
		TEXT("/Script/CoreUObject."),
		TEXT("/Script/UMG."),
		TEXT("/Script/AIModule."),
		TEXT("/Script/NavigationSystem."),
		TEXT("/Script/Niagara."),
		TEXT("/Script/PhysicsCore."),
		TEXT("/Script/GameplayAbilities."),
		TEXT("/Script/EnhancedInput."),
		TEXT("/Script/AnimGraphRuntime."),
		TEXT("/Script/GameplayTasks."),
		TEXT("/Script/MovieScene."),
		TEXT("/Script/Landscape."),
		TEXT("/Script/Foliage."),
		TEXT("/Script/Paper2D."),
		TEXT("/Script/Chaos.")
	};

	for (const FString& Prefix : ModulePrefixes)
	{
		FString FullPath = Prefix + ClassName;
		FoundClass = FindObject<UClass>(nullptr, *FullPath);
		if (FoundClass)
		{
			return FoundClass;
		}
	}

	// Try case-insensitive search by iterating all classes
	FString LowerClassName = ClassName.ToLower();
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->GetName().ToLower() == LowerClassName)
		{
			return Class;
		}
	}

	// Try Blueprint generated class lookup (for "BP_Character_C" style names)
	if (ClassName.EndsWith(TEXT("_C")))
	{
		FString BlueprintName = ClassName.LeftChop(2);  // Remove "_C"

		// Search asset registry for Blueprint with this name
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

		TArray<FAssetData> BlueprintAssets;
		AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), BlueprintAssets, true);

		for (const FAssetData& AssetData : BlueprintAssets)
		{
			if (AssetData.AssetName.ToString().Equals(BlueprintName, ESearchCase::IgnoreCase))
			{
				if (UBlueprint* Blueprint = Cast<UBlueprint>(AssetData.GetAsset()))
				{
					return Blueprint->GeneratedClass;
				}
			}
		}
	}

	UE_LOG(LogAgentIntegrationKit, Warning, TEXT("FindClassByName: Could not find class '%s'"), *ClassName);
	return nullptr;
}

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesForClass(
	UBlueprint* ContextBlueprint,
	const FString& ClassName,
	const TArray<FString>& Queries,
	const FString& CategoryFilter,
	const FString& InputTypeFilter,
	const FString& OutputTypeFilter)
{
	TArray<FNodeInfo> Results;

	if (!ContextBlueprint)
	{
		return Results;
	}

	// Find the target class
	UClass* TargetClass = FindClassByName(ClassName);
	if (!TargetClass)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("FindNodesForClass: Class '%s' not found"), *ClassName);
		return Results;
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesForClass: Found class %s (%s)"),
		*TargetClass->GetName(), *TargetClass->GetPathName());

	// Get graph for context and schema compatibility checking
	UEdGraph* TargetGraph = nullptr;
	if (ContextBlueprint->UbergraphPages.Num() > 0)
	{
		TargetGraph = ContextBlueprint->UbergraphPages[0];
	}
	else if (ContextBlueprint->FunctionGraphs.Num() > 0)
	{
		TargetGraph = ContextBlueprint->FunctionGraphs[0];
	}

	if (!TargetGraph)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("FindNodesForClass: No valid graph in context Blueprint"));
		return Results;
	}

	const UEdGraphSchema* GraphSchema = TargetGraph->GetSchema();

	// For Blueprint-generated classes, refresh actions (but do NOT compile - can cause crashes)
	FBlueprintActionDatabase& ActionDatabase = FBlueprintActionDatabase::Get();

	if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(TargetClass))
	{
		if (UBlueprint* TargetBlueprint = Cast<UBlueprint>(BPGC->ClassGeneratedBy))
		{
			// Refresh actions for this Blueprint (safe, doesn't require compilation)
			ActionDatabase.RefreshAssetActions(TargetBlueprint);

			// Use SkeletonGeneratedClass for filtering if available
			if (TargetBlueprint->SkeletonGeneratedClass)
			{
				TargetClass = TargetBlueprint->SkeletonGeneratedClass;
			}
		}
	}

	FBlueprintActionFilter Filter(FBlueprintActionFilter::BPFILTER_NoFlags);
	Filter.Context.Blueprints.Add(ContextBlueprint);
	Filter.Context.Graphs.Add(TargetGraph);
	FBlueprintActionFilter::Add(Filter.TargetClasses, TargetClass);

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesForClass: Filter setup with TargetClass: %s"),
		*TargetClass->GetName());

	const FBlueprintActionDatabase::FActionRegistry& ActionRegistry = ActionDatabase.GetAllActions();

	// Track processed nodes to avoid duplicates
	TSet<FString> ProcessedNodeNames;

	// Iterate through all actions and check which pass the filter
	for (const auto& ActionPair : ActionRegistry)
	{
		UObject* ActionKey = ActionPair.Key.ResolveObjectPtr();
		const FBlueprintActionDatabase::FActionList& ActionList = ActionPair.Value;

		for (UBlueprintNodeSpawner* Spawner : ActionList)
		{
			if (!Spawner || !Spawner->NodeClass)
			{
				continue;
			}

			FBlueprintActionInfo ActionInfo(ActionKey, Spawner);
			if (Filter.IsFiltered(ActionInfo))
			{
				continue;  // Action was rejected by the filter
			}

			// Check schema compatibility
			UEdGraphNode* NodeCDO = Spawner->NodeClass->GetDefaultObject<UEdGraphNode>();
			if (!NodeCDO || !NodeCDO->CanCreateUnderSpecifiedSchema(GraphSchema))
			{
				continue;
			}

			// Get UI spec for node info
			const FBlueprintActionUiSpec& UiSpec = Spawner->PrimeDefaultUiSpec(TargetGraph);
			FString NodeName = UiSpec.MenuName.ToString();
			FString NodeCategory = UiSpec.Category.ToString();
			FString NodeKeywords = UiSpec.Keywords.ToString();
			FString NodeTooltip = UiSpec.Tooltip.ToString();

			if (NodeName.IsEmpty())
			{
				continue;
			}

			// Skip duplicates
			FString NodeKey = NodeName + TEXT("_") + NodeCategory;
			if (ProcessedNodeNames.Contains(NodeKey))
			{
				continue;
			}

			// Check query match
			FString MatchedQuery;
			int32 Score = 0;
			if (!MatchesQuery(NodeName, NodeKeywords, Queries, MatchedQuery, Score))
			{
				continue;
			}

			// Check category filter
			if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
			{
				continue;
			}

			// Cache the spawner and get ID
			FString SpawnerId = FNodeSpawnerCache::Get().CacheSpawner(Spawner, ContextBlueprint, NodeName);

			FNodeInfo Info;
			Info.Name = NodeName;
			Info.SpawnerId = SpawnerId;
			Info.Category = NodeCategory;
			Info.Tooltip = NodeTooltip;
			Info.Keywords = NodeKeywords;
			Info.MatchedQuery = MatchedQuery;
			Info.Score = Score;

			// Extract pin info from template node
			UEdGraphNode* TemplateNode = Spawner->GetTemplateNode(TargetGraph);
			if (TemplateNode)
			{
				if (TemplateNode->Pins.Num() == 0)
				{
					TemplateNode->AllocateDefaultPins();
				}
				ExtractPinInfo(TemplateNode, Info.InputPins, Info.OutputPins);
				ExtractNodeFlags(TemplateNode, Info.Flags);
			}

			// Check pin type filters
			if (!MatchesPinType(Info.InputPins, InputTypeFilter))
			{
				continue;
			}
			if (!MatchesPinType(Info.OutputPins, OutputTypeFilter))
			{
				continue;
			}

			ProcessedNodeNames.Add(NodeKey);
			Results.Add(Info);
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesForClass: Found %d total nodes for class %s"),
		Results.Num(), *ClassName);

	return Results;
}

FString FFindNodeTool::FormatClassResults(
	const FString& ClassName,
	const TArray<FString>& Queries,
	const TArray<FNodeInfo>& Results,
	int32 Limit) const
{
	FString Output;

	// Header
	Output += FString::Printf(TEXT("# FIND NODES for Class: %s\n"), *ClassName);

	// Query info
	FString QueryStr = FString::Join(Queries, TEXT(", "));
	Output += FString::Printf(TEXT("Query: %s\n\n"), *QueryStr);

	// Results count
	Output += FString::Printf(TEXT("## Results (%d found, showing top %d per query)\n\n"), Results.Num(), Limit);

	if (Results.Num() == 0)
	{
		Output += TEXT("No matching nodes found.\n");
		Output += TEXT("\nTIPS:\n");
		Output += TEXT("- Make sure the class name is correct (e.g., 'CameraComponent', 'CharacterMovementComponent')\n");
		Output += TEXT("- For Blueprint classes, add '_C' suffix (e.g., 'BP_Character_C')\n");
		Output += TEXT("- Try broader search terms\n");
		return Output;
	}

	// Group by matched query
	TMap<FString, TArray<const FNodeInfo*>> GroupedResults;
	for (const FNodeInfo& Info : Results)
	{
		GroupedResults.FindOrAdd(Info.MatchedQuery).Add(&Info);
	}

	// Output each group
	for (const FString& Query : Queries)
	{
		TArray<const FNodeInfo*>* Group = GroupedResults.Find(Query);
		if (!Group || Group->Num() == 0)
		{
			continue;
		}

		// Sort by score descending
		Group->Sort([](const FNodeInfo& A, const FNodeInfo& B)
		{
			if (A.Score != B.Score)
			{
				return A.Score > B.Score;
			}
			return A.Name.Len() < B.Name.Len();
		});

		int32 TotalCount = Group->Num();
		int32 ShownCount = FMath::Min(TotalCount, Limit);

		if (TotalCount > Limit)
		{
			Output += FString::Printf(TEXT("### \"%s\" (%d of %d, +%d more)\n"),
				*Query, ShownCount, TotalCount, TotalCount - Limit);
			Output += TEXT("    TIP: Too many results? Add input_type/output_type filter or category filter.\n\n");
		}
		else
		{
			Output += FString::Printf(TEXT("### \"%s\" (%d)\n\n"), *Query, TotalCount);
		}

		for (int32 i = 0; i < ShownCount; ++i)
		{
			const FNodeInfo* Info = (*Group)[i];

			Output += FString::Printf(TEXT("+ %s\n"), *Info->Name);
			Output += FString::Printf(TEXT("  ID: %s\n"), *Info->SpawnerId);

			if (!Info->Category.IsEmpty())
			{
				Output += FString::Printf(TEXT("  Category: %s\n"), *Info->Category);
			}

			if (Info->Flags.Num() > 0)
			{
				Output += FString::Printf(TEXT("  Flags: %s\n"), *FString::Join(Info->Flags, TEXT(", ")));
			}

			if (!Info->Tooltip.IsEmpty())
			{
				FString Desc = Info->Tooltip;
				Desc.ReplaceInline(TEXT("\r\n"), TEXT(" "), ESearchCase::CaseSensitive);
				Desc.ReplaceInline(TEXT("\n"), TEXT(" "), ESearchCase::CaseSensitive);
				if (Desc.Len() > 120)
				{
					Desc = Desc.Left(117) + TEXT("...");
				}
				Output += FString::Printf(TEXT("  Desc: %s\n"), *Desc);
			}

			if (Info->InputPins.Num() > 0)
			{
				Output += TEXT("  Inputs:\n");
				for (const FString& Pin : Info->InputPins)
				{
					Output += FString::Printf(TEXT("    - %s\n"), *Pin);
				}
			}

			if (Info->OutputPins.Num() > 0)
			{
				Output += TEXT("  Outputs:\n");
				for (const FString& Pin : Info->OutputPins)
				{
					Output += FString::Printf(TEXT("    - %s\n"), *Pin);
				}
			}

			Output += TEXT("\n");
		}
	}

	// Add usage tip
	Output += TEXT("---\n");
	Output += TEXT("USAGE: To call these functions, first get a reference to the component\n");
	Output += TEXT("(via 'Get Component by Class' node or a component variable), then call these functions on it.\n");

	return Output;
}
