// Copyright 2026 Betide Studio. All Rights Reserved.
// EQS (Environment Query System) node discovery for FindNodeTool

#include "Tools/FindNodeTool.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "AgentIntegrationKitModule.h"
#include "Tools/FuzzyMatchingUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// EQS runtime
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

// EQS editor graph
#include "EnvironmentQueryGraph.h"
#include "EnvironmentQueryGraphNode.h"
#include "EnvironmentQueryGraphNode_Root.h"
#include "EnvironmentQueryGraphNode_Option.h"
#include "EnvironmentQueryGraphNode_Test.h"
#include "EdGraphSchema_EnvironmentQuery.h"
#include "EnvironmentQueryEditorModule.h"

// AI Graph base
#include "AIGraphSchema.h"
#include "AIGraphTypes.h"
#include "AIGraphNode.h"

// Editor
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphSchema.h"

// ---------------------------------------------------------------------------
// Global action caches — shared with EditGraphTool_EQS.cpp via extern
// ---------------------------------------------------------------------------
TMap<FString, TSharedPtr<FEdGraphSchemaAction>> EQSOptionActionCache;  // EQS_OPTION: generators
TMap<FString, TSharedPtr<FEdGraphSchemaAction>> EQSTestActionCache;    // EQS_TEST: tests

// ---------------------------------------------------------------------------
// Cache entry metadata — stored parallel to action caches for find_node results
// ---------------------------------------------------------------------------
struct FEQSCachedNodeInfo
{
	FString SpawnerId;
	FString DisplayName;
	FString Category;    // "Generators" or "Tests"
	FString Tooltip;
	FString Keywords;
};

static TArray<FEQSCachedNodeInfo> EQSCachedNodeInfos;

// ---------------------------------------------------------------------------
// Helper: get or create the EQS editor graph
// ---------------------------------------------------------------------------
static UEnvironmentQueryGraph* EnsureEQSGraph(UEnvQuery* Query)
{
	if (!Query)
	{
		return nullptr;
	}

	UEnvironmentQueryGraph* EQSGraph = Cast<UEnvironmentQueryGraph>(Query->EdGraph);
	if (!EQSGraph)
	{
		const TSubclassOf<UEdGraphSchema> SchemaClass = UEdGraphSchema_EnvironmentQuery::StaticClass();
		Query->EdGraph = FBlueprintEditorUtils::CreateNewGraph(
			Query, TEXT("EnvironmentQuery"), UEnvironmentQueryGraph::StaticClass(), SchemaClass);
		EQSGraph = Cast<UEnvironmentQueryGraph>(Query->EdGraph);
		if (EQSGraph)
		{
			const UEdGraphSchema* Schema = EQSGraph->GetSchema();
			if (Schema)
			{
				Schema->CreateDefaultNodesForGraph(*EQSGraph);
			}
			EQSGraph->OnCreated();
		}
	}
	return EQSGraph;
}

// ---------------------------------------------------------------------------
// Helper: ensure the EQS editor is open (needed for FGraphNodeClassHelper)
// ---------------------------------------------------------------------------
static void EnsureEQSEditorOpen(UEnvQuery* Query)
{
	if (!GEditor || !Query)
	{
		return;
	}
	UAssetEditorSubsystem* Sub = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (Sub && !Sub->FindEditorForAsset(Query, false))
	{
		Sub->OpenEditorForAsset(Query);
		FPlatformProcess::Sleep(0.2f);
	}
}

// ---------------------------------------------------------------------------
// EnsureEQSCachesPopulated — populates ALL EQS option + test caches
// No-op if already populated (unless bForceRebuild=true)
// ---------------------------------------------------------------------------
bool EnsureEQSCachesPopulated(UEnvQuery* Query, bool bForceRebuild)
{
	// Skip if already populated and no rebuild requested
	if (!bForceRebuild && EQSOptionActionCache.Num() > 0)
	{
		return true;
	}

	if (!Query)
	{
		return false;
	}

	// Ensure graph + editor are up for class cache
	UEnvironmentQueryGraph* EQSGraph = EnsureEQSGraph(Query);
	if (!EQSGraph)
	{
		return false;
	}
	EnsureEQSEditorOpen(Query);

	// Get class cache from EnvironmentQueryEditorModule
	FEnvironmentQueryEditorModule& EditorModule = FModuleManager::GetModuleChecked<FEnvironmentQueryEditorModule>(TEXT("EnvironmentQueryEditor"));
	TSharedPtr<FGraphNodeClassHelper> ClassCachePtr = EditorModule.GetClassCache();
	if (!ClassCachePtr.IsValid())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("EnsureEQSCachesPopulated: ClassCache not available — is EQS editor open?"));
		return false;
	}
	FGraphNodeClassHelper& ClassCache = *ClassCachePtr;

	// Clear old caches
	EQSOptionActionCache.Empty();
	EQSTestActionCache.Empty();
	EQSCachedNodeInfos.Empty();

	// ----- Generators (Options) -----
	int32 OptionIdx = 0;
	{
		TArray<FGraphNodeClassData> GeneratorClasses;
		ClassCache.GatherClasses(UEnvQueryGenerator::StaticClass(), GeneratorClasses);

		for (const FGraphNodeClassData& ClassData : GeneratorClasses)
		{
			FString RawName = ClassData.ToString();
			FString DisplayName = FName::NameToDisplayString(RawName, false);

			UEnvironmentQueryGraphNode_Option* TemplateNode = NewObject<UEnvironmentQueryGraphNode_Option>(
				GetTransientPackage());
			TemplateNode->ClassData = ClassData;

			TSharedPtr<FAISchemaAction_NewNode> NewAction = MakeShared<FAISchemaAction_NewNode>();
			NewAction->UpdateSearchData(
				FText::FromString(DisplayName),
				FText::FromString(ClassData.GetCategory().ToString()),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = TemplateNode;

			FString SpawnerId = FString::Printf(TEXT("EQS_OPTION:%d:%s"),
				OptionIdx, *DisplayName.Replace(TEXT(" "), TEXT("_")));
			EQSOptionActionCache.Add(SpawnerId, NewAction);

			FEQSCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = DisplayName;
			Info.Category = TEXT("Generators");
			Info.Tooltip = ClassData.GetTooltip().ToString();
			Info.Keywords = ClassData.GetCategory().ToString().ToLower() + TEXT(" ") + RawName.ToLower();
			EQSCachedNodeInfos.Add(MoveTemp(Info));
			OptionIdx++;
		}
	}

	// ----- Tests -----
	int32 TestIdx = 0;
	{
		TArray<FGraphNodeClassData> TestClasses;
		ClassCache.GatherClasses(UEnvQueryTest::StaticClass(), TestClasses);

		for (const FGraphNodeClassData& ClassData : TestClasses)
		{
			FString RawName = ClassData.ToString();
			FString DisplayName = FName::NameToDisplayString(RawName, false);

			UEnvironmentQueryGraphNode_Test* TemplateNode = NewObject<UEnvironmentQueryGraphNode_Test>(
				GetTransientPackage());
			TemplateNode->ClassData = ClassData;

			TSharedPtr<FAISchemaAction_NewSubNode> NewAction = MakeShared<FAISchemaAction_NewSubNode>();
			NewAction->UpdateSearchData(
				FText::FromString(DisplayName),
				FText::FromString(ClassData.GetCategory().ToString()),
				FText::GetEmpty(), FText::GetEmpty());
			NewAction->NodeTemplate = Cast<UAIGraphNode>(TemplateNode);

			FString SpawnerId = FString::Printf(TEXT("EQS_TEST:%d:%s"),
				TestIdx, *DisplayName.Replace(TEXT(" "), TEXT("_")));
			EQSTestActionCache.Add(SpawnerId, NewAction);

			FEQSCachedNodeInfo Info;
			Info.SpawnerId = SpawnerId;
			Info.DisplayName = DisplayName;
			Info.Category = TEXT("Tests");
			Info.Tooltip = ClassData.GetTooltip().ToString();
			Info.Keywords = ClassData.GetCategory().ToString().ToLower() + TEXT(" ") + RawName.ToLower();
			EQSCachedNodeInfos.Add(MoveTemp(Info));
			TestIdx++;
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("EnsureEQSCachesPopulated: Cached %d generators, %d tests, %d total entries"),
		EQSOptionActionCache.Num(), EQSTestActionCache.Num(), EQSCachedNodeInfos.Num());
	return true;
}

// ---------------------------------------------------------------------------
// FindNodesInEQS — schema-action-based discovery
// ---------------------------------------------------------------------------
TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInEQS(
	UObject* Asset,
	const TArray<FString>& Queries,
	const FString& CategoryFilter)
{
	TArray<FNodeInfo> Results;

	UEnvQuery* Query = Cast<UEnvQuery>(Asset);
	if (!Query)
	{
		return Results;
	}

	// Populate caches (force rebuild for discovery)
	if (!EnsureEQSCachesPopulated(Query, true))
	{
		return Results;
	}

	// Filter cached entries by query
	for (const FEQSCachedNodeInfo& Cached : EQSCachedNodeInfos)
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

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("FindNodesInEQS: Found %d nodes matching queries "
		"(cached %d generators, %d tests)"), Results.Num(), EQSOptionActionCache.Num(), EQSTestActionCache.Num());
	return Results;
}

#else // ENGINE_MINOR_VERSION < 6

TArray<FFindNodeTool::FNodeInfo> FFindNodeTool::FindNodesInEQS(
	UObject*, const TArray<FString>&, const FString&)
{ return {}; }

#endif
