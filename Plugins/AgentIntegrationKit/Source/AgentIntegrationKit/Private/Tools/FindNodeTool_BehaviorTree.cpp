// Copyright 2025 Betide Studio. All Rights Reserved.
// BehaviorTree node discovery for FindNodeTool — uses schema actions + FGraphNodeClassHelper

#include "Tools/FindNodeTool.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/FuzzyMatchingUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Behavior Tree graph editor
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/Tasks/BTTask_RunBehavior.h"
#include "BehaviorTree/Composites/BTComposite_SimpleParallel.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "BehaviorTreeGraphNode_SimpleParallel.h"
#endif
#include "BehaviorTreeGraphNode_Task.h"
#include "BehaviorTreeGraphNode_SubtreeTask.h"
#include "BehaviorTreeGraphNode_Decorator.h"
#include "BehaviorTreeGraphNode_Service.h"
#include "BehaviorTreeGraphNode_CompositeDecorator.h"
#include "BehaviorTreeDecoratorGraph.h"
#include "BehaviorTreeDecoratorGraphNode_Decorator.h"
#include "BehaviorTreeDecoratorGraphNode_Logic.h"
#include "AIGraphSchema.h"
#include "AIGraphTypes.h"
#include "BehaviorTreeEditorModule.h"
#include "Kismet2/BlueprintEditorUtils.h"

// Editor
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EdGraph/EdGraphSchema.h"

// ---------------------------------------------------------------------------
// Global action caches — shared with EditGraphTool_BehaviorTree.cpp via extern
// ---------------------------------------------------------------------------
TMap<FString, TSharedPtr<FEdGraphSchemaAction>> BTActionCache;        // BT_ACTION:  composites/tasks
TMap<FString, TSharedPtr<FEdGraphSchemaAction>> BTSubNodeActionCache;  // BT_SUBNODE: decorators/services
TMap<FString, TSharedPtr<FEdGraphSchemaAction>> BTDecGraphActionCache; // BT_DECGRAPH: decorator logic

// ---------------------------------------------------------------------------
// Cache entry metadata — stored parallel to action caches for find_node results
// ---------------------------------------------------------------------------
struct FBTCachedNodeInfo
{
	FString SpawnerId;
	FString DisplayName;
	FString Category;    // "Composites", "Tasks", "Decorators", "Services"
	FString Tooltip;
	FString Keywords;
};

static TArray<FBTCachedNodeInfo> BTCachedNodeInfos;

// ---------------------------------------------------------------------------
// Helper: get or create the BT editor graph
// ---------------------------------------------------------------------------
static UBehaviorTreeGraph* EnsureBTGraph(UBehaviorTree* BT)
{
	if (!BT)
	{
		return nullptr;
	}

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
	if (!BTGraph)
	{
		const TSubclassOf<UEdGraphSchema> SchemaClass = GetDefault<UBehaviorTreeGraph>()->Schema;
		BT->BTGraph = FBlueprintEditorUtils::CreateNewGraph(
			BT, TEXT("Behavior Tree"), UBehaviorTreeGraph::StaticClass(), SchemaClass);
		BTGraph = Cast<UBehaviorTreeGraph>(BT->BTGraph);
		if (BTGraph)
		{
			const UEdGraphSchema* Schema = BTGraph->GetSchema();
			if (Schema)
			{
				Schema->CreateDefaultNodesForGraph(*BTGraph);
			}
			BTGraph->OnCreated();
		}
	}
	return BTGraph;
}

// ---------------------------------------------------------------------------
// Helper: ensure the BT editor is open (needed to populate FGraphNodeClassHelper)
// ---------------------------------------------------------------------------
static void EnsureBTEditorOpen(UBehaviorTree* BT)
{
	if (!GEditor || !BT)
	{
		return;
	}
	UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (Sub && !Sub->FindEditorForAsset(BT, false))
	{
		Sub->OpenEditorForAsset(BT);
		FPlatformProcess::Sleep(0.2f);
	}
}

// ---------------------------------------------------------------------------
// Helper: find a CompositeDecorator's bound graph by name
// ---------------------------------------------------------------------------
static UEdGraph* FindDecoratorSubGraph(UBehaviorTreeGraph* BTGraph, const FString& DecoratorName)
{
	if (!BTGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : BTGraph->Nodes)
	{
		UBehaviorTreeGraphNode* BTGNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (!BTGNode)
		{
			continue;
		}

		// Check decorators on this node
		for (UBehaviorTreeGraphNode* DecNode : BTGNode->Decorators)
		{
			if (UBehaviorTreeGraphNode_CompositeDecorator* CompDec = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(DecNode))
			{
				FString CompName = CompDec->GetNodeTitle(ENodeTitleType::ListView).ToString();
				if (CompName.Equals(DecoratorName, ESearchCase::IgnoreCase))
				{
					return CompDec->GetBoundGraph();
				}
			}
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// EnsureBTCachesPopulated — populates ALL BT action + sub-node caches
// No-op if already populated (unless bForceRebuild=true)
// Called from both FindNodesInBehaviorTree and EditGraphTool spawn paths
// ---------------------------------------------------------------------------
bool EnsureBTCachesPopulated(UBehaviorTree* BT, bool bForceRebuild)
{
	// Skip if already populated and no rebuild requested
	if (!bForceRebuild && BTActionCache.Num() > 0)
	{
		return true;
	}

	if (!BT)
	{
		return false;
	}

	// Ensure graph + editor are up for class cache
	UBehaviorTreeGraph* BTGraph = EnsureBTGraph(BT);
	if (!BTGraph)
	{
		return false;
	}
	EnsureBTEditorOpen(BT);

	// Get class cache from BehaviorTreeEditorModule
	FBehaviorTreeEditorModule& EditorModule = FModuleManager::GetModuleChecked<FBehaviorTreeEditorModule>(TEXT("BehaviorTreeEditor"));
	TSharedPtr<FGraphNodeClassHelper> ClassCachePtr = EditorModule.GetClassCache();
	if (!ClassCachePtr.IsValid())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("EnsureBTCachesPopulated: ClassCache not available — is BT editor open?"));
		return false;
	}
	FGraphNodeClassHelper& ClassCache = *ClassCachePtr;

	// Clear old caches
	BTActionCache.Empty();
	BTSubNodeActionCache.Empty();
	BTCachedNodeInfos.Empty();

	int32 ActionIdx = 0;

	// ----- Composites -----
	{
		TArray<FGraphNodeClassData> CompositeClasses;
		ClassCache.GatherClasses(UBTCompositeNode::StaticClass(), CompositeClasses);

		const FString ParallelClassName = UBTComposite_SimpleParallel::StaticClass()->GetName();

		for (const FGraphNodeClassData& ClassData : CompositeClasses)
		{
			FString RawName = ClassData.ToString();
			FString DisplayName = FName::NameToDisplayString(RawName, false);

			UClass* GraphNodeClass = UBehaviorTreeGraphNode_Composite::StaticClass();
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			if (ClassData.GetClassName() == ParallelClassName)
			{
				GraphNodeClass = UBehaviorTreeGraphNode_SimpleParallel::StaticClass();
			}
#endif

			UBehaviorTreeGraphNode* TemplateNode = NewObject<UBehaviorTreeGraphNode>(
				GetTransientPackage(), GraphNodeClass);
			TemplateNode->ClassData = ClassData;

			TSharedPtr<FAISchemaAction_NewNode> NewAction = MakeShared<FAISchemaAction_NewNode>();
			NewAction->UpdateSearchData(
				FText::FromString(DisplayName),
				FText::FromString(ClassData.GetCategory().ToString()),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = TemplateNode;

			FString SpawnerId = FString::Printf(TEXT("BT_ACTION:%d:%s"),
				ActionIdx, *DisplayName.Replace(TEXT(" "), TEXT("_")));
			BTActionCache.Add(SpawnerId, NewAction);

			FBTCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = DisplayName;
			Info.Category = TEXT("Composites");
			Info.Tooltip = ClassData.GetTooltip().ToString();
			Info.Keywords = ClassData.GetCategory().ToString().ToLower();
			BTCachedNodeInfos.Add(MoveTemp(Info));
			ActionIdx++;
		}
	}

	// ----- Tasks -----
	{
		TArray<FGraphNodeClassData> TaskClasses;
		ClassCache.GatherClasses(UBTTaskNode::StaticClass(), TaskClasses);

		for (const FGraphNodeClassData& ClassData : TaskClasses)
		{
			FString RawName = ClassData.ToString();
			FString DisplayName = FName::NameToDisplayString(RawName, false);

			UClass* GraphNodeClass = UBehaviorTreeGraphNode_Task::StaticClass();
			if (ClassData.GetClassName() == UBTTask_RunBehavior::StaticClass()->GetName())
			{
				GraphNodeClass = UBehaviorTreeGraphNode_SubtreeTask::StaticClass();
			}

			UBehaviorTreeGraphNode* TemplateNode = NewObject<UBehaviorTreeGraphNode>(
				GetTransientPackage(), GraphNodeClass);
			TemplateNode->ClassData = ClassData;

			TSharedPtr<FAISchemaAction_NewNode> NewAction = MakeShared<FAISchemaAction_NewNode>();
			NewAction->UpdateSearchData(
				FText::FromString(DisplayName),
				FText::FromString(ClassData.GetCategory().ToString()),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = TemplateNode;

			FString SpawnerId = FString::Printf(TEXT("BT_ACTION:%d:%s"),
				ActionIdx, *DisplayName.Replace(TEXT(" "), TEXT("_")));
			BTActionCache.Add(SpawnerId, NewAction);

			FBTCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = DisplayName;
			Info.Category = TEXT("Tasks");
			Info.Tooltip = ClassData.GetTooltip().ToString();
			Info.Keywords = ClassData.GetCategory().ToString().ToLower();
			BTCachedNodeInfos.Add(MoveTemp(Info));
			ActionIdx++;
		}
	}

	// ----- Decorators (BT_SUBNODE) -----
	int32 SubIdx = 0;
	{
		TArray<FGraphNodeClassData> DecoratorClasses;
		ClassCache.GatherClasses(UBTDecorator::StaticClass(), DecoratorClasses);

		for (const FGraphNodeClassData& ClassData : DecoratorClasses)
		{
			FString RawName = ClassData.ToString();
			FString DisplayName = FName::NameToDisplayString(RawName, false);

			UBehaviorTreeGraphNode* TemplateNode = NewObject<UBehaviorTreeGraphNode>(
				GetTransientPackage(), UBehaviorTreeGraphNode_Decorator::StaticClass());
			TemplateNode->ClassData = ClassData;

			TSharedPtr<FAISchemaAction_NewSubNode> NewAction = MakeShared<FAISchemaAction_NewSubNode>();
			NewAction->UpdateSearchData(
				FText::FromString(DisplayName),
				FText::FromString(ClassData.GetCategory().ToString()),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = Cast<UAIGraphNode>(TemplateNode);

			FString SpawnerId = FString::Printf(TEXT("BT_SUBNODE:%d:Decorators:%s"),
				SubIdx, *DisplayName.Replace(TEXT(" "), TEXT("_")));
			BTSubNodeActionCache.Add(SpawnerId, NewAction);

			FBTCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = DisplayName;
			Info.Category = TEXT("Decorators");
			Info.Tooltip = ClassData.GetTooltip().ToString();
			Info.Keywords = ClassData.GetCategory().ToString().ToLower();
			BTCachedNodeInfos.Add(MoveTemp(Info));
			SubIdx++;
		}

		// Composite Decorator — special decorator option
		{
			UBehaviorTreeGraphNode_CompositeDecorator* CompDecTemplate =
				NewObject<UBehaviorTreeGraphNode_CompositeDecorator>(GetTransientPackage());

			TSharedPtr<FAISchemaAction_NewSubNode> NewAction = MakeShared<FAISchemaAction_NewSubNode>();
			NewAction->UpdateSearchData(
				FText::FromString(TEXT("Composite Decorator")),
				FText::FromString(TEXT("Decorators")),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = CompDecTemplate;

			FString SpawnerId = FString::Printf(TEXT("BT_SUBNODE:%d:Decorators:Composite_Decorator"), SubIdx);
			BTSubNodeActionCache.Add(SpawnerId, NewAction);

			FBTCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = TEXT("Composite Decorator");
			Info.Category = TEXT("Decorators");
			Info.Tooltip = TEXT("Combines multiple decorators with logic operations (AND/OR/NOT)");
			Info.Keywords = TEXT("composite logic and or not");
			BTCachedNodeInfos.Add(MoveTemp(Info));
			SubIdx++;
		}
	}

	// ----- Services (BT_SUBNODE) -----
	{
		TArray<FGraphNodeClassData> ServiceClasses;
		ClassCache.GatherClasses(UBTService::StaticClass(), ServiceClasses);

		for (const FGraphNodeClassData& ClassData : ServiceClasses)
		{
			FString RawName = ClassData.ToString();
			FString DisplayName = FName::NameToDisplayString(RawName, false);

			UBehaviorTreeGraphNode* TemplateNode = NewObject<UBehaviorTreeGraphNode>(
				GetTransientPackage(), UBehaviorTreeGraphNode_Service::StaticClass());
			TemplateNode->ClassData = ClassData;

			TSharedPtr<FAISchemaAction_NewSubNode> NewAction = MakeShared<FAISchemaAction_NewSubNode>();
			NewAction->UpdateSearchData(
				FText::FromString(DisplayName),
				FText::FromString(ClassData.GetCategory().ToString()),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = Cast<UAIGraphNode>(TemplateNode);

			FString SpawnerId = FString::Printf(TEXT("BT_SUBNODE:%d:Services:%s"),
				SubIdx, *DisplayName.Replace(TEXT(" "), TEXT("_")));
			BTSubNodeActionCache.Add(SpawnerId, NewAction);

			FBTCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = DisplayName;
			Info.Category = TEXT("Services");
			Info.Tooltip = ClassData.GetTooltip().ToString();
			Info.Keywords = ClassData.GetCategory().ToString().ToLower();
			BTCachedNodeInfos.Add(MoveTemp(Info));
			SubIdx++;
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("EnsureBTCachesPopulated: Cached %d actions, %d sub-nodes, %d total entries"),
		BTActionCache.Num(), BTSubNodeActionCache.Num(), BTCachedNodeInfos.Num());
	return true;
}

// ---------------------------------------------------------------------------
// FindNodesInBehaviorTree — schema-action-based discovery
// Uses EnsureBTCachesPopulated for cache, then filters by query
// ---------------------------------------------------------------------------
TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInBehaviorTree(
	UObject* Asset,
	const TArray<FString>& Queries,
	const FString& CategoryFilter,
	const FString& GraphName)
{
	TArray<FNodeInfo> Results;

	UBehaviorTree* BT = Cast<UBehaviorTree>(Asset);
	if (!BT)
	{
		return Results;
	}

	// ---------- Decorator sub-graph path ----------
	// graph_name = "decorator:MyCompositeDecoratorName"
	if (GraphName.StartsWith(TEXT("decorator:"), ESearchCase::IgnoreCase))
	{
		UBehaviorTreeGraph* BTGraph = EnsureBTGraph(BT);
		if (!BTGraph)
		{
			return Results;
		}
		EnsureBTEditorOpen(BT);

		FString DecoratorName = GraphName.Mid(10);
		UEdGraph* DecGraph = FindDecoratorSubGraph(BTGraph, DecoratorName);
		if (!DecGraph)
		{
			return Results;
		}

		const UEdGraphSchema* DecSchema = DecGraph->GetSchema();
		if (!DecSchema)
		{
			return Results;
		}

		BTDecGraphActionCache.Empty();

		FGraphContextMenuBuilder CtxBuilder(DecGraph);
		DecSchema->GetGraphContextActions(CtxBuilder);

		int32 ActionIndex = 0;
		for (int32 i = 0; i < CtxBuilder.GetNumActions(); i++)
		{
			TSharedPtr<FEdGraphSchemaAction> Action = CtxBuilder.GetSchemaAction(i);
			if (!Action.IsValid())
			{
				continue;
			}

			FString NodeName = Action->GetMenuDescription().ToString();
			FString NodeCategory = Action->GetCategory().ToString();
			FString Keywords = Action->GetKeywords().ToString();

			if (NodeName.IsEmpty())
			{
				continue;
			}

			if (!CategoryFilter.IsEmpty() && !MatchesCategory(NodeCategory, CategoryFilter))
			{
				continue;
			}

			FString SearchKw = NodeName.ToLower() + TEXT(" ") + Keywords.ToLower();
			FString MatchedQuery;
			int32 Score = 0;
			if (!MatchesQuery(NodeName, SearchKw, Queries, MatchedQuery, Score))
			{
				continue;
			}

			FString SpawnerId = FString::Printf(TEXT("BT_DECGRAPH:%d:%s"),
				ActionIndex, *NodeName.Replace(TEXT(" "), TEXT("_")));
			BTDecGraphActionCache.Add(SpawnerId, Action);

			FNodeInfo Info;
			Info.Name = NodeName;
			Info.SpawnerId = SpawnerId;
			Info.Category = NodeCategory.IsEmpty() ? TEXT("Decorator Logic") : NodeCategory;
			Info.Keywords = Keywords;
			Info.MatchedQuery = MatchedQuery;
			Info.Score = Score;
			Results.Add(Info);
			ActionIndex++;
		}

		return Results;
	}

	// ---------- Main graph: populate caches (force rebuild for discovery) ----------
	if (!EnsureBTCachesPopulated(BT, true))
	{
		return Results;
	}

	// ---------- Filter cached entries by query ----------
	for (const FBTCachedNodeInfo& Cached : BTCachedNodeInfos)
	{
		if (!CategoryFilter.IsEmpty() && !MatchesCategory(Cached.Category, CategoryFilter))
		{
			continue;
		}

		FString MatchedQuery;
		int32 Score = 0;
		FString SearchKw = Cached.DisplayName.ToLower() + TEXT(" ") + Cached.Keywords;
		if (!MatchesQuery(Cached.DisplayName, SearchKw, Queries, MatchedQuery, Score))
		{
			continue;
		}

		FNodeInfo Info;
		Info.Name = Cached.DisplayName;
		Info.SpawnerId = Cached.SpawnerId;
		Info.Category = Cached.Category;
		Info.Tooltip = Cached.Tooltip;
		Info.MatchedQuery = MatchedQuery;
		Info.Score = Score;
		Results.Add(Info);
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesInBehaviorTree: Found %d nodes matching queries "
		"(cached %d actions, %d sub-nodes)"), Results.Num(), BTActionCache.Num(), BTSubNodeActionCache.Num());
	return Results;
}
