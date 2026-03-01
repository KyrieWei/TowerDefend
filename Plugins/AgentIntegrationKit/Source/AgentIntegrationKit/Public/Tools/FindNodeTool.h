// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UBlueprintNodeSpawner;

/**
 * Tool for finding available nodes in graph-based assets:
 * - Blueprints (EventGraph, functions, etc.)
 * - Behavior Trees (tasks, composites, decorators, services)
 * - Animation Blueprints (AnimGraph, EventGraph)
 * - Materials (material expressions)
 * - Niagara Systems (modules + dynamic input scripts with output type info)
 * - PCG Graphs (procedural content generation nodes)
 * - MetaSounds (audio synthesis nodes)
 * - Environment Queries (EQS generators and tests)
 *
 * Returns node info including spawner ID, category, and pin signatures.
 */
class AGENTINTEGRATIONKIT_API FFindNodeTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("find_node"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Find available nodes in Blueprint/BehaviorTree/Material/AnimBP/Niagara/MetaSound/EQS graphs by name or keyword. "
		            "UE5 uses type-promoted wildcard operators — search by operator name (Greater, Less, Add, Subtract, Multiply, Divide, EqualEqual, NotEqual, GreaterEqual, LessEqual), NOT by type (e.g., search 'Greater' not 'integer > integer'). Pins auto-promote to the correct type when connected. "
		            "For AnimBPs, graph_name can target subgraphs: animgraph:<Graph>, statemachine:<AnimGraph>/<SM>, "
		            "state:<AnimGraph>/<State>, transition:<AnimGraph>/<From->To>, custom_transition:<AnimGraph>/<From->To>, "
		            "conduit:<AnimGraph>/<Conduit>, composite:<AnimGraph>/<Composite>.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Result entry for a found node */
	struct FNodeInfo
	{
		FString Name;
		FString SpawnerId;
		FString Category;
		FString Tooltip;
		FString Keywords;
		TArray<FString> InputPins;
		TArray<FString> OutputPins;
		TArray<FString> Flags;     // Node flags: Pure, Latent, Const, Deprecated, ThreadSafe, etc.
		FString MatchedQuery;      // Which query term matched this node
		int32 Score = 0;           // Relevance score for ranking (higher is better)
	};

	/** Graph type enumeration */
	enum class EGraphType
	{
		Blueprint,
		BehaviorTree,
		Material,
		AnimBlueprint,
		Niagara,
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		PCG,
#endif
		MetaSound,
		EQS,
		Unknown
	};

	/** Detect graph type from loaded asset */
	EGraphType DetectGraphType(UObject* Asset) const;

	/** Get graph type as string */
	FString GraphTypeToString(EGraphType Type) const;

	/** Find nodes in a Blueprint graph */
	TArray<FNodeInfo> FindNodesInBlueprint(UBlueprint* Blueprint, const FString& GraphName,
		const TArray<FString>& Queries, const FString& CategoryFilter,
		const FString& InputTypeFilter, const FString& OutputTypeFilter);

	/** Find nodes in a Behavior Tree (composites, tasks, decorators, services, decorator sub-graph logic) */
	TArray<FNodeInfo> FindNodesInBehaviorTree(UObject* BehaviorTree,
		const TArray<FString>& Queries, const FString& CategoryFilter,
		const FString& GraphName);

	/** Find nodes in a Material */
	TArray<FNodeInfo> FindNodesInMaterial(UObject* Material,
		const TArray<FString>& Queries, const FString& CategoryFilter);

	/** Find modules in a Niagara System */
	TArray<FNodeInfo> FindNodesInNiagara(UObject* NiagaraSystem,
		const TArray<FString>& Queries, const FString& CategoryFilter);

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	/** Find nodes in a PCG Graph */
	TArray<FNodeInfo> FindNodesInPCG(UObject* PCGGraph,
		const TArray<FString>& Queries, const FString& CategoryFilter);
#endif

	/** Find nodes in a MetaSound graph */
	TArray<FNodeInfo> FindNodesInMetaSound(UObject* MetaSoundAsset,
		const TArray<FString>& Queries, const FString& CategoryFilter);

	/** Find nodes in an Environment Query (EQS generators and tests) */
	TArray<FNodeInfo> FindNodesInEQS(UObject* EnvQueryAsset,
		const TArray<FString>& Queries, const FString& CategoryFilter);

	/** Extract pin info from a template node */
	void ExtractPinInfo(UEdGraphNode* TemplateNode, TArray<FString>& OutInputs, TArray<FString>& OutOutputs);

	/** Extract node flags (Pure, Latent, Const, Deprecated, etc.) */
	void ExtractNodeFlags(UEdGraphNode* TemplateNode, TArray<FString>& OutFlags);

	/** Convert pin type to readable string */
	FString PinTypeToString(const struct FEdGraphPinType& PinType) const;

	/** Check if node matches any query and compute relevance score */
	bool MatchesQuery(const FString& NodeName, const FString& Keywords, const TArray<FString>& Queries, FString& OutMatchedQuery, int32& OutScore) const;

	/** Check if node matches category filter */
	bool MatchesCategory(const FString& NodeCategory, const FString& CategoryFilter) const;

	/** Check if any pin in the array matches the type filter */
	bool MatchesPinType(const TArray<FString>& Pins, const FString& TypeFilter) const;

	/** Format results to output string with limit per query */
	FString FormatResults(const FString& AssetName, const FString& GraphName, EGraphType GraphType,
		const TArray<FString>& Queries, const TArray<FNodeInfo>& Results, int32 Limit) const;

	/** Get the target graph from a Blueprint */
	UEdGraph* GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const;

	/** Find nodes for a specific UClass (cross-Blueprint function discovery) */
	TArray<FNodeInfo> FindNodesForClass(UBlueprint* ContextBlueprint, const FString& ClassName,
		const TArray<FString>& Queries, const FString& CategoryFilter,
		const FString& InputTypeFilter, const FString& OutputTypeFilter);

	/** Find a UClass by name (supports short names like "CameraComponent" or full paths) */
	UClass* FindClassByName(const FString& ClassName) const;

	/** Format results for class-based search */
	FString FormatClassResults(const FString& ClassName, const TArray<FString>& Queries,
		const TArray<FNodeInfo>& Results, int32 Limit) const;
};
