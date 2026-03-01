// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "WidgetBlueprint.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimBlueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "Engine/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/DataTable.h"
#include "NiagaraSystem.h"
#include "LevelSequence.h"
#include "UObject/UnrealType.h"
#include "Metasound.h"
#include "MetasoundSource.h"
#include "Materials/MaterialInstance.h"
#include "Engine/StaticMesh.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Sound/SoundCue.h"
#include "Sound/SoundClass.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/CurveTable.h"
#include "K2Node_Composite.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Rig/IKRigDefinition.h"
#include "Retargeter/IKRetargeter.h"
#if WITH_POSE_SEARCH
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#endif // WITH_POSE_SEARCH
#endif

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "PCGGraph.h"
#endif

// StateTree support
#include "StateTree.h"

// Enhanced Input support
#include "InputAction.h"
#include "InputMappingContext.h"

// Gameplay Ability System support
#include "GameplayEffect.h"

// StringTable support
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

// ChooserTable support
#include "Chooser.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "OutputObjectColumn.h"

TSharedPtr<FJsonObject> FReadFileTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Asset path (e.g., /Game/Blueprints/BP_Player) or file name"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Optional folder path for text files"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> GraphProp = MakeShared<FJsonObject>();
	GraphProp->SetStringField(TEXT("type"), TEXT("string"));
	GraphProp->SetStringField(TEXT("description"),
		TEXT("Specific graph to read. Accepts raw graph name (e.g., EventGraph) or typed selector for AnimBPs: "
		     "animgraph:<GraphName>, statemachine:<AnimGraph>/<StateMachine>, state:<AnimGraph>/<State>, "
		     "transition:<AnimGraph>/<From->To>, custom_transition:<AnimGraph>/<From->To>, conduit:<AnimGraph>/<Conduit>, "
		     "composite:<AnimGraph>/<Composite>."));
	Properties->SetObjectField(TEXT("graph"), GraphProp);

	TSharedPtr<FJsonObject> ComponentProp = MakeShared<FJsonObject>();
	ComponentProp->SetStringField(TEXT("type"), TEXT("string"));
	ComponentProp->SetStringField(TEXT("description"), TEXT("Component name to inspect properties for (shows all editable properties with current values)"));
	Properties->SetObjectField(TEXT("component"), ComponentProp);

	TSharedPtr<FJsonObject> OffsetProp = MakeShared<FJsonObject>();
	OffsetProp->SetStringField(TEXT("type"), TEXT("integer"));
	OffsetProp->SetStringField(TEXT("description"), TEXT("Line offset for pagination (default: 1)"));
	Properties->SetObjectField(TEXT("offset"), OffsetProp);

	TSharedPtr<FJsonObject> LimitProp = MakeShared<FJsonObject>();
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"), TEXT("Max lines to return (default: 100, max: 1000)"));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	TSharedPtr<FJsonObject> IncludeProp = MakeShared<FJsonObject>();
	IncludeProp->SetStringField(TEXT("type"), TEXT("array"));
	IncludeProp->SetStringField(TEXT("description"),
		TEXT("Sections to include: summary, variables, components, graphs, interfaces, widgets, tree, schema. "
			 "For WidgetBlueprints: use 'schema' to see ALL editable properties with types and format hints - "
			 "shows exactly what property names to use in configure_widgets and the expected value format."));
	TSharedPtr<FJsonObject> IncludeItems = MakeShared<FJsonObject>();
	IncludeItems->SetStringField(TEXT("type"), TEXT("string"));
	IncludeProp->SetObjectField(TEXT("items"), IncludeItems);
	Properties->SetObjectField(TEXT("include"), IncludeProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FReadFileTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path, GraphName, ComponentName;
	int32 Offset = 1;
	int32 Limit = 100;
	TArray<FString> Include;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("graph"), GraphName);
	Args->TryGetStringField(TEXT("component"), ComponentName);
	Args->TryGetNumberField(TEXT("offset"), Offset);
	Args->TryGetNumberField(TEXT("limit"), Limit);

	// Parse include array
	const TArray<TSharedPtr<FJsonValue>>* IncludeArray;
	if (Args->TryGetArrayField(TEXT("include"), IncludeArray))
	{
		for (const auto& Val : *IncludeArray)
		{
			FString IncludeItem;
			if (Val->TryGetString(IncludeItem))
			{
				Include.Add(IncludeItem.ToLower());
			}
		}
	}

	// Default include if not specified
	if (Include.Num() == 0)
	{
		Include.Add(TEXT("summary"));
	}

	// Clamp values
	Offset = FMath::Max(1, Offset);
	Limit = FMath::Clamp(Limit, 1, 1000);

	// Route based on path type
	if (!NeoStackToolUtils::IsAssetPath(Name, Path))
	{
		return ReadTextFile(Name, Path, Offset, Limit);
	}

	// Load as generic UObject first (like FindNodeTool does)
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Collect graphs and metadata based on asset type
	TArray<TPair<UEdGraph*, FString>> Graphs; // Graph + Type
	FString AssetType;
	FString Summary;

	// Check for Animation Blueprint FIRST (it inherits from UBlueprint)
	if (UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset))
	{
		AssetType = TEXT("AnimBlueprint");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetAnimBlueprintSummary(AnimBlueprint);
		}
		if (Include.Contains(TEXT("variables")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintVariables(AnimBlueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("statemachines")) || Include.Contains(TEXT("states")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetAnimBlueprintStateMachines(AnimBlueprint);
		}

		// Collect standard graphs
		for (UEdGraph* Graph : AnimBlueprint->UbergraphPages)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("ubergraph")));
		}
		for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("function")));
		}

		// Collect AnimGraph and state machine graphs as subgraphs
		CollectAnimBlueprintGraphs(AnimBlueprint, Graphs);
	}
	// Check for Widget Blueprint (it inherits from UBlueprint)
	else if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Asset))
	{
		AssetType = TEXT("WidgetBlueprint");

		// Build summary with widget tree
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetWidgetBlueprintSummary(WidgetBlueprint);
		}
		if (Include.Contains(TEXT("widgets")) || Include.Contains(TEXT("tree")) || Include.Contains(TEXT("schema")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			bool bShowSchema = Include.Contains(TEXT("schema"));
			Summary += GetWidgetTree(WidgetBlueprint, bShowSchema);
		}
		if (Include.Contains(TEXT("variables")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintVariables(WidgetBlueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("interfaces")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintInterfaces(WidgetBlueprint);
		}

		// Collect graphs (Widget Blueprints have event graphs too)
		for (UEdGraph* Graph : WidgetBlueprint->UbergraphPages)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("ubergraph")));
		}
		for (UEdGraph* Graph : WidgetBlueprint->FunctionGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("function")));
		}
	}
	else if (Cast<UControlRigBlueprint>(Asset))
	{
		return ReadControlRig(Asset);
	}
	else if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		// Check for GameplayEffect Blueprint before generic handling
		if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(UGameplayEffect::StaticClass()))
		{
			return ReadGameplayEffect(Blueprint);
		}

		AssetType = TEXT("Blueprint");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetBlueprintSummary(Blueprint);
		}
		if (Include.Contains(TEXT("variables")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintVariables(Blueprint, Offset, Limit);
		}
		if (Include.Contains(TEXT("components")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintComponents(Blueprint, Offset, Limit);
		}
		// If a specific component is requested, show its properties
		if (!ComponentName.IsEmpty())
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintComponentProperties(Blueprint, ComponentName, Offset, Limit);
		}
		if (Include.Contains(TEXT("interfaces")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlueprintInterfaces(Blueprint);
		}

		// Collect graphs
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("ubergraph")));
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("function")));
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(Graph, TEXT("macro")));
		}

		// Collect composite (collapsed) graphs recursively from all graphs
		{
			TSet<UEdGraph*> VisitedGraphs;
			auto CollectCompositeGraphs = [&](UEdGraph* GraphToSearch, const FString& ParentName, auto&& Self) -> void
			{
				if (!GraphToSearch || VisitedGraphs.Contains(GraphToSearch))
				{
					return;
				}
				VisitedGraphs.Add(GraphToSearch);

				for (UEdGraphNode* Node : GraphToSearch->Nodes)
				{
					if (!Node) continue;
					if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(Node))
					{
						if (CompositeNode->BoundGraph)
						{
							FString CompositeName = CompositeNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
							if (CompositeName.IsEmpty())
							{
								CompositeName = CompositeNode->BoundGraph->GetName();
							}
							Graphs.Add(TPair<UEdGraph*, FString>(CompositeNode->BoundGraph,
								FString::Printf(TEXT("composite:%s/%s"), *ParentName, *CompositeName)));
							Self(CompositeNode->BoundGraph, ParentName, Self);
						}
					}
				}
			};

			// Iterate a snapshot since we're appending to Graphs
			TArray<TPair<UEdGraph*, FString>> InitialGraphs = Graphs;
			for (const auto& GraphPair : InitialGraphs)
			{
				if (GraphPair.Key)
				{
					CollectCompositeGraphs(GraphPair.Key, GraphPair.Key->GetName(), CollectCompositeGraphs);
				}
			}
		}
	}
	else if (USoundCue* SoundCueAsset = Cast<USoundCue>(Asset))
	{
		AssetType = TEXT("SoundCue");

		if (Include.Contains(TEXT("summary")))
		{
			Summary = FString::Printf(TEXT("# SOUND_CUE: %s\n"), *SoundCueAsset->GetName());
			Summary += FString::Printf(TEXT("Volume: %.2f\n"), SoundCueAsset->VolumeMultiplier);
			Summary += FString::Printf(TEXT("Pitch: %.2f\n"), SoundCueAsset->PitchMultiplier);

			if (SoundCueAsset->SoundClassObject)
			{
				Summary += FString::Printf(TEXT("Sound Class: %s\n"), *SoundCueAsset->SoundClassObject->GetName());
			}

			if (SoundCueAsset->IsLooping())
			{
				Summary += TEXT("Duration: Looping\n");
			}
			else
			{
				Summary += FString::Printf(TEXT("Duration: %.2fs\n"), SoundCueAsset->GetDuration());
			}

			Summary += FString::Printf(TEXT("Nodes: %d\n"), SoundCueAsset->AllNodes.Num());
		}

		if (SoundCueAsset->SoundCueGraph)
		{
			Graphs.Add(TPair<UEdGraph*, FString>(SoundCueAsset->SoundCueGraph, TEXT("sound")));
		}
	}
	else if (Cast<UMaterial>(Asset))
	{
		return ReadMaterial(Asset, Include, GraphName, Offset, Limit);
	}
	else if (Cast<UMaterialFunction>(Asset))
	{
		return ReadMaterial(Asset, Include, GraphName, Offset, Limit);
	}
	else if (UMaterialInstance* MatInstance = Cast<UMaterialInstance>(Asset))
	{
		return ReadMaterialInstance(MatInstance);
	}
	else if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Asset))
	{
		AssetType = TEXT("BehaviorTree");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetBehaviorTreeSummary(BehaviorTree);
		}

		// Always include full node hierarchy (agents need this to understand the tree)
		if (!Summary.IsEmpty()) Summary += TEXT("\n");
		Summary += GetBehaviorTreeNodes(BehaviorTree);

		// Also include blackboard keys if the BT has a blackboard
		if (BehaviorTree->BlackboardAsset)
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetBlackboardKeys(BehaviorTree->BlackboardAsset);
		}

		// BTs don't have traditional graphs, output is in Summary
		if (Summary.IsEmpty())
		{
			Summary = FString::Printf(TEXT("# BEHAVIOR_TREE %s (no data)\n"), *BehaviorTree->GetName());
		}
		return FToolResult::Ok(Summary);
	}
	else if (UEnvQuery* EnvQuery = Cast<UEnvQuery>(Asset))
	{
		AssetType = TEXT("EnvironmentQuery");

		// Build summary
		Summary = GetEQSSummary(EnvQuery);

		// Always include full details (options, generators, tests, properties)
		if (!Summary.IsEmpty()) Summary += TEXT("\n");
		Summary += GetEQSDetails(EnvQuery);

		if (Summary.IsEmpty())
		{
			Summary = FString::Printf(TEXT("# ENVIRONMENT_QUERY %s (no data)\n"), *EnvQuery->GetName());
		}
		return FToolResult::Ok(Summary);
	}
	else if (UBlackboardData* Blackboard = Cast<UBlackboardData>(Asset))
	{
		AssetType = TEXT("Blackboard");

		// Build summary with keys - always include full key details
		Summary = GetBlackboardSummary(Blackboard);
		if (!Summary.IsEmpty()) Summary += TEXT("\n");
		Summary += GetBlackboardKeys(Blackboard);

		// Return early - Blackboards don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UStateTree* StateTree = Cast<UStateTree>(Asset))
	{
		// StateTree has its own reader
		return ReadStateTree(StateTree, Include);
	}
	else if (UUserDefinedStruct* UserStruct = Cast<UUserDefinedStruct>(Asset))
	{
		AssetType = TEXT("Struct");

		// Build summary - always include full field details
		Summary = GetStructSummary(UserStruct);
		if (!Summary.IsEmpty()) Summary += TEXT("\n");
		Summary += GetStructFields(UserStruct);

		// Return early - Structs don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UUserDefinedEnum* UserEnum = Cast<UUserDefinedEnum>(Asset))
	{
		AssetType = TEXT("Enum");

		// Build summary - always include full enum values
		Summary = GetEnumSummary(UserEnum);
		if (!Summary.IsEmpty()) Summary += TEXT("\n");
		Summary += GetEnumValues(UserEnum);

		// Return early - Enums don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UDataTable* DataTable = Cast<UDataTable>(Asset))
	{
		AssetType = TEXT("DataTable");

		// Build summary
		Summary = GetDataTableSummary(DataTable);

		if (Include.Contains(TEXT("rows")) || Include.Contains(TEXT("data")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetDataTableRows(DataTable, Offset, Limit);
		}

		// Return early - DataTables don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UChooserTable* ChooserTable = Cast<UChooserTable>(Asset))
	{
		AssetType = TEXT("ChooserTable");

		// Build summary
		Summary = GetChooserTableSummary(ChooserTable);

		if (Include.Contains(TEXT("references")) || Include.Contains(TEXT("data")) || Include.Contains(TEXT("rows")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetChooserTableReferences(ChooserTable, Offset, Limit);
		}

		if (Include.Contains(TEXT("columns")))
		{
			if (!Summary.IsEmpty()) Summary += TEXT("\n");
			Summary += GetChooserTableColumns(ChooserTable);
		}

		// Return early - ChooserTables don't have graphs
		return FToolResult::Ok(Summary);
	}
	else if (UStringTable* StringTable = Cast<UStringTable>(Asset))
	{
		return ReadStringTable(StringTable, Offset, Limit);
	}
	else if (UNiagaraSystem* NiagaraSystem = Cast<UNiagaraSystem>(Asset))
	{
		AssetType = TEXT("NiagaraSystem");

		// Build summary
		if (Include.Contains(TEXT("summary")))
		{
			Summary = GetNiagaraSystemSummary(NiagaraSystem);
		}

		// "graphs", "emitters", "modules", "stacks" all show emitter/stack details for Niagara
		if (Include.Contains(TEXT("emitters")) || Include.Contains(TEXT("modules")) ||
			Include.Contains(TEXT("stacks")) || Include.Contains(TEXT("graphs")) || Include.Contains(TEXT("graph")))
		{
			// If a specific graph/emitter name is requested, filter to just that emitter
			if (!GraphName.IsEmpty())
			{
				// Find the requested emitter
				const TArray<FNiagaraEmitterHandle>& Handles = NiagaraSystem->GetEmitterHandles();
				bool bFound = false;
				for (const FNiagaraEmitterHandle& Handle : Handles)
				{
					if (Handle.GetName().ToString().Equals(GraphName, ESearchCase::IgnoreCase))
					{
						if (!Summary.IsEmpty()) Summary += TEXT("\n");
						Summary += GetNiagaraEmitterDetails(Handle);
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					// List available emitters
					TArray<FString> EmitterNames;
					for (const FNiagaraEmitterHandle& Handle : Handles)
					{
						EmitterNames.Add(Handle.GetName().ToString());
					}
					return FToolResult::Fail(FString::Printf(TEXT("Emitter '%s' not found. Available: %s"),
						*GraphName, *FString::Join(EmitterNames, TEXT(", "))));
				}
			}
			else
			{
				if (!Summary.IsEmpty()) Summary += TEXT("\n");
				Summary += GetNiagaraEmitters(NiagaraSystem);
			}
		}

		// Return early - Niagara uses stacks, not traditional graphs
		if (Summary.IsEmpty())
		{
			Summary = FString::Printf(TEXT("# NIAGARA_SYSTEM %s\nUse include: [\"emitters\"] to see emitter stacks and modules.\n"), *NiagaraSystem->GetName());
		}
		return FToolResult::Ok(Summary);
	}
	else if (ULevelSequence* LevelSequence = Cast<ULevelSequence>(Asset))
	{
		return ReadLevelSequence(LevelSequence);
	}
	else if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset))
	{
		return ReadSkeletalMesh(SkelMesh);
	}
	else if (UAnimMontage* Montage = Cast<UAnimMontage>(Asset))
	{
		return ReadAnimMontage(Montage);
	}
	else if (UAnimSequence* AnimSeq = Cast<UAnimSequence>(Asset))
	{
		return ReadAnimSequence(AnimSeq);
	}
	else if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(Asset))
	{
		return ReadBlendSpace(BlendSpace);
	}
	else if (UStaticMesh* StaticMeshAsset = Cast<UStaticMesh>(Asset))
	{
		return ReadStaticMesh(StaticMeshAsset);
	}
	else if (USkeleton* Skeleton = Cast<USkeleton>(Asset))
	{
		return ReadSkeleton(Skeleton);
	}
	else if (UPhysicsAsset* PhysAsset = Cast<UPhysicsAsset>(Asset))
	{
		return ReadPhysicsAsset(PhysAsset);
	}
	else if (UCurveTable* CurveTableAsset = Cast<UCurveTable>(Asset))
	{
		return ReadCurveTable(CurveTableAsset);
	}
	else if (UCurveBase* CurveBaseAsset = Cast<UCurveBase>(Asset))
	{
		return ReadCurveAsset(CurveBaseAsset);
	}
	else if (UInputMappingContext* IMC = Cast<UInputMappingContext>(Asset))
	{
		return ReadInputMappingContext(IMC);
	}
	else if (UInputAction* IA = Cast<UInputAction>(Asset))
	{
		return ReadInputAction(IA);
	}
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	else if (UIKRigDefinition* IKRig = Cast<UIKRigDefinition>(Asset))
	{
		return ReadIKRig(IKRig);
	}
	else if (UIKRetargeter* Retargeter = Cast<UIKRetargeter>(Asset))
	{
		return ReadIKRetargeter(Retargeter);
	}
#if WITH_POSE_SEARCH
	else if (UPoseSearchSchema* PoseSchema = Cast<UPoseSearchSchema>(Asset))
	{
		return ReadPoseSearchSchema(PoseSchema);
	}
	else if (UPoseSearchDatabase* PoseDB = Cast<UPoseSearchDatabase>(Asset))
	{
		return ReadPoseSearchDatabase(PoseDB);
	}
	else if (UPoseSearchNormalizationSet* NormSet = Cast<UPoseSearchNormalizationSet>(Asset))
	{
		return ReadPoseSearchNormalizationSet(NormSet);
	}
#endif // WITH_POSE_SEARCH
#endif
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	else if (UPCGGraphInterface* PCGGraphInterface = Cast<UPCGGraphInterface>(Asset))
	{
		return ReadPCGGraph(PCGGraphInterface);
	}
#endif
	else if (Cast<UMetaSoundSource>(Asset) || Cast<UMetaSoundPatch>(Asset))
	{
		return ReadMetaSound(Asset, FullAssetPath, Include, GraphName, Offset, Limit);
	}
	else
	{
		return ReadGenericAsset(Asset);
	}

	// If specific graph requested, find and return just that one
	if (!GraphName.IsEmpty())
	{
		FString GraphNameLower = GraphName.ToLower();
		for (const auto& GraphPair : Graphs)
		{
			if (GraphPair.Key->GetName().Equals(GraphName, ESearchCase::IgnoreCase) ||
				GraphPair.Value.Equals(GraphName, ESearchCase::IgnoreCase) ||
				GraphPair.Value.ToLower().Equals(GraphNameLower))
			{
				FString Output = GetGraphWithNodes(GraphPair.Key, GraphPair.Value, TEXT(""), Offset, Limit);
				Output += TEXT("\n") + GetGraphConnections(GraphPair.Key);
				return FToolResult::Ok(Output);
			}
		}
		return FToolResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	// Build output
	FString Output = Summary;

	// Add graphs if requested
	if (Include.Contains(TEXT("graphs")) || Include.Contains(TEXT("graph")))
	{
		for (const auto& GraphPair : Graphs)
		{
			if (!Output.IsEmpty()) Output += TEXT("\n");
			Output += GetGraphWithNodes(GraphPair.Key, GraphPair.Value, TEXT(""), Offset, Limit);
			Output += TEXT("\n") + GetGraphConnections(GraphPair.Key);
		}
	}

	if (Output.IsEmpty())
	{
		Output = FString::Printf(TEXT("# %s %s (no data)\n"), *AssetType, *Asset->GetName());
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadTextFile(const FString& Name, const FString& Path, int32 Offset, int32 Limit)
{
	FString FullPath = NeoStackToolUtils::BuildFilePath(Name, Path);

	// Profiling capture formats are binary and should not be interpreted as text.
	const FString Extension = FPaths::GetExtension(FullPath, true).ToLower();
	if (Extension == TEXT(".utrace") || Extension == TEXT(".ue4stats") || Extension == TEXT(".rtt"))
	{
		return FToolResult::Fail(FString::Printf(
			TEXT("Binary profiling capture cannot be read as text: %s. Open this file in Unreal Insights, then read exported text/JSON/CSV artifacts instead. If you pass an absolute file path, set it in 'name' and leave 'path' empty."),
			*FullPath));
	}

	// Check if file exists
	if (!FPaths::FileExists(FullPath))
	{
		return FToolResult::Fail(FString::Printf(TEXT("File not found: %s"), *FullPath));
	}

	// Read file
	FString Content;
	if (!FFileHelper::LoadFileToString(Content, *FullPath))
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to read file: %s"), *FullPath));
	}

	// Split into lines
	TArray<FString> Lines;
	Content.ParseIntoArrayLines(Lines);

	int32 TotalLines = Lines.Num();
	int32 StartIndex = Offset - 1; // Convert to 0-based
	int32 EndIndex = FMath::Min(StartIndex + Limit, TotalLines);

	if (StartIndex >= TotalLines)
	{
		return FToolResult::Ok(FString::Printf(TEXT("# FILE %s lines=%d offset=%d beyond_end"), *Name, TotalLines, Offset));
	}

	// Build output
	FString Output = FString::Printf(TEXT("# FILE %s lines=%d-%d/%d\n"), *Name, Offset, EndIndex, TotalLines);

	for (int32 i = StartIndex; i < EndIndex; i++)
	{
		Output += FString::Printf(TEXT("%d\t%s\n"), i + 1, *Lines[i]);
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadGenericAsset(UObject* Asset)
{
	if (!Asset)
	{
		return FToolResult::Fail(TEXT("Asset is null"));
	}

	UClass* AssetClass = Asset->GetClass();
	FString ClassName = AssetClass->GetName();
	FString ParentClassName = AssetClass->GetSuperClass() ? AssetClass->GetSuperClass()->GetName() : TEXT("None");

	FString Output = FString::Printf(TEXT("# %s: %s\n"), *ClassName, *Asset->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *Asset->GetPathName());
	Output += FString::Printf(TEXT("Class: %s\n"), *AssetClass->GetPathName());
	Output += FString::Printf(TEXT("Parent Class: %s\n"), *ParentClassName);

	TMap<FString, TArray<FString>> CategorizedProps;
	TArray<FString> UncategorizedProps;

	for (TFieldIterator<FProperty> PropIt(AssetClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
		{
			continue;
		}

		if (Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FDelegateProperty>())
		{
			continue;
		}

		FString PropName = Property->GetName();
		if (PropName.StartsWith(TEXT("__")))
		{
			continue;
		}

		FString PropType = Property->GetCPPType();

		FString ValueStr;
		const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Asset);
		Property->ExportTextItem_Direct(ValueStr, PropertyValue, nullptr, nullptr, PPF_None);

		if (ValueStr.Len() > 120)
		{
			ValueStr = ValueStr.Left(117) + TEXT("...");
		}

		FString Line = FString::Printf(TEXT("%s\t%s\t%s"), *PropName, *PropType, *ValueStr);

		const FString& Category = Property->GetMetaData(TEXT("Category"));
		if (!Category.IsEmpty())
		{
			CategorizedProps.FindOrAdd(Category).Add(Line);
		}
		else
		{
			UncategorizedProps.Add(Line);
		}
	}

	if (CategorizedProps.Num() == 0 && UncategorizedProps.Num() == 0)
	{
		Output += TEXT("\n(no editable properties)\n");
		return FToolResult::Ok(Output);
	}

	Output += TEXT("\n## Properties\n");

	CategorizedProps.KeySort([](const FString& A, const FString& B) { return A < B; });
	for (const auto& Pair : CategorizedProps)
	{
		Output += FString::Printf(TEXT("\n### %s\n"), *Pair.Key);
		for (const FString& Line : Pair.Value)
		{
			Output += Line + TEXT("\n");
		}
	}

	if (UncategorizedProps.Num() > 0)
	{
		if (CategorizedProps.Num() > 0)
		{
			Output += TEXT("\n### Other\n");
		}
		for (const FString& Line : UncategorizedProps)
		{
			Output += Line + TEXT("\n");
		}
	}

	return FToolResult::Ok(Output);
}
