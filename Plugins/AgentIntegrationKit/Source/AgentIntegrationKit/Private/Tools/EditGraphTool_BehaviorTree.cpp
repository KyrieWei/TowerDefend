// Copyright 2025 Betide Studio. All Rights Reserved.
// BehaviorTree spawn paths and property reflection for EditGraphTool

#include "Tools/EditGraphTool.h"
#include "Tools/NodeNameRegistry.h"
#include "AgentIntegrationKitModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Behavior Tree graph editor
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "BehaviorTreeGraphNode_Root.h"
#include "BehaviorTreeGraphNode_Composite.h"
#include "BehaviorTreeGraphNode_CompositeDecorator.h"
#include "BehaviorTreeDecoratorGraph.h"
#include "AIGraphSchema.h"
#include "AIGraphNode.h"

// Property reflection
#include "UObject/UnrealType.h"

// Editor
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphSchema.h"

// BT action caches — defined in FindNodeTool_BehaviorTree.cpp
extern TMap<FString, TSharedPtr<FEdGraphSchemaAction>> BTActionCache;
extern TMap<FString, TSharedPtr<FEdGraphSchemaAction>> BTSubNodeActionCache;
extern TMap<FString, TSharedPtr<FEdGraphSchemaAction>> BTDecGraphActionCache;

// Auto-populate BT caches on demand (lazy init) — defined in FindNodeTool_BehaviorTree.cpp
extern bool EnsureBTCachesPopulated(UBehaviorTree* BT, bool bForceRebuild = false);

// ---------------------------------------------------------------------------
// Helper: ensure the BT editor graph exists (mirrors FindNodeTool_BehaviorTree.cpp)
// ---------------------------------------------------------------------------
static UBehaviorTreeGraph* EnsureBTGraphForEdit(UBehaviorTree* BT)
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
// Helper: find a CompositeDecorator's bound graph by name (same as FindNodeTool version)
// ---------------------------------------------------------------------------
static UEdGraph* FindDecoratorSubGraphForEdit(UBehaviorTreeGraph* BTGraph, const FString& DecoratorName)
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
// BT graph detection for EditGraphTool::Execute()
// Called from EditGraphTool.cpp; returns the target graph and registers Root node
// ---------------------------------------------------------------------------
UEdGraph* EditGraphTool_DetectBTGraph(
	UObject* Asset,
	const FString& GraphName,
	const FString& FullAssetPath,
	FString& OutActualGraphName,
	TArray<FString>& Errors)
{
	UBehaviorTree* BT = Cast<UBehaviorTree>(Asset);
	if (!BT)
	{
		return nullptr;
	}

	UBehaviorTreeGraph* BTGraph = EnsureBTGraphForEdit(BT);
	if (!BTGraph)
	{
		Errors.Add(TEXT("Failed to create or find BT editor graph"));
		return nullptr;
	}

	// Check if targeting a decorator sub-graph: "decorator:MyCompositeDecoratorName"
	if (GraphName.StartsWith(TEXT("decorator:"), ESearchCase::IgnoreCase))
	{
		FString DecoratorName = GraphName.Mid(10);
		UEdGraph* DecGraph = FindDecoratorSubGraphForEdit(BTGraph, DecoratorName);
		if (!DecGraph)
		{
			Errors.Add(FString::Printf(TEXT("Decorator sub-graph not found: '%s'"), *DecoratorName));
			return nullptr;
		}
		OutActualGraphName = DecGraph->GetName();
		return DecGraph;
	}

	// Main BT graph — auto-register all existing graph nodes
	OutActualGraphName = BTGraph->GetName();

	TMap<FString, int32> NameCounts; // Track duplicates for unique registration

	for (UEdGraphNode* Node : BTGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Register Root node
		if (Node->IsA<UBehaviorTreeGraphNode_Root>())
		{
			FNodeNameRegistry::Get().Register(FullAssetPath, OutActualGraphName, TEXT("Root"), Node->NodeGuid);
			continue;
		}

		UBehaviorTreeGraphNode* BTGNode = Cast<UBehaviorTreeGraphNode>(Node);
		if (!BTGNode)
		{
			continue;
		}

		// Register main graph node (composite or task) by its display title
		FString Title = BTGNode->GetNodeTitle(ENodeTitleType::ListView).ToString().TrimStartAndEnd();
		if (!Title.IsEmpty())
		{
			int32& Count = NameCounts.FindOrAdd(Title);
			FString RegName = (Count == 0) ? Title : FString::Printf(TEXT("%s_%d"), *Title, Count);
			Count++;
			FNodeNameRegistry::Get().Register(FullAssetPath, OutActualGraphName, RegName, BTGNode->NodeGuid);
		}

		// Register sub-nodes (decorators and services) on this node
		for (UAIGraphNode* SubNode : BTGNode->SubNodes)
		{
			if (!SubNode)
			{
				continue;
			}
			FString SubTitle = SubNode->GetNodeTitle(ENodeTitleType::ListView).ToString().TrimStartAndEnd();
			if (!SubTitle.IsEmpty())
			{
				int32& SubCount = NameCounts.FindOrAdd(SubTitle);
				FString SubRegName = (SubCount == 0) ? SubTitle : FString::Printf(TEXT("%s_%d"), *SubTitle, SubCount);
				SubCount++;
				FNodeNameRegistry::Get().Register(FullAssetPath, OutActualGraphName, SubRegName, SubNode->NodeGuid);
			}
		}
	}

	return BTGraph;
}

// ---------------------------------------------------------------------------
// BT_ACTION spawn: composites/tasks via cached FAISchemaAction_NewNode
// Returns the spawned UEdGraphNode*, or nullptr on failure
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_SpawnBTAction(
	const FString& SpawnerId,
	UEdGraph* Graph,
	const FVector2D& Position,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("BT_ACTION: graph is null");
		return nullptr;
	}

	if (!Graph->IsA<UBehaviorTreeGraph>())
	{
		OutError = FString::Printf(TEXT("BT_ACTION: graph is not a BehaviorTree graph (%s)"),
			*Graph->GetClass()->GetName());
		return nullptr;
	}

	UBehaviorTree* BT = Graph->GetTypedOuter<UBehaviorTree>();
	if (!BT)
	{
		OutError = TEXT("BT_ACTION: could not resolve owning BehaviorTree");
		return nullptr;
	}

	// Rebuild BT action cache for each spawn to avoid reusing consumed schema actions/templates.
	// FAISchemaAction_NewNode::PerformAction mutates NodeTemplate (renames/adds it into the graph).
	if (!EnsureBTCachesPopulated(BT, true))
	{
		OutError = TEXT("BT_ACTION: failed to populate BehaviorTree action cache");
		return nullptr;
	}

	TSharedPtr<FEdGraphSchemaAction>* CachedAction = BTActionCache.Find(SpawnerId);
	if (!CachedAction || !CachedAction->IsValid())
	{
		OutError = FString::Printf(TEXT("BT action not found in cache: %s. Run edit_graph with operation='find_nodes' first to discover available BT nodes."), *SpawnerId);
		return nullptr;
	}

	TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2f(Position.X, Position.Y), true);
#else
	UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2D(Position.X, Position.Y), true);
#endif

	// Prevent accidental reuse of consumed action/template in this process lifetime.
	BTActionCache.Remove(SpawnerId);

	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("BT_ACTION PerformAction returned null: %s"), *SpawnerId);
		return nullptr;
	}

	// Keep transactional for undo/redo; PostPlacedNewNode already ran inside PerformAction.
	NewNode->SetFlags(RF_Transactional);

	return NewNode;
}

// ---------------------------------------------------------------------------
// BT_SUBNODE spawn: decorators/services via cached FAISchemaAction_NewSubNode
// PerformAction returns NULL for sub-nodes — we find the new node by diffing SubNodes
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_SpawnBTSubNode(
	const FString& SpawnerId,
	UEdGraph* Graph,
	UEdGraphNode* ParentGraphNode,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("BT_SUBNODE: graph is null");
		return nullptr;
	}

	if (!Graph->IsA<UBehaviorTreeGraph>())
	{
		OutError = FString::Printf(TEXT("BT_SUBNODE: graph is not a BehaviorTree graph (%s)"),
			*Graph->GetClass()->GetName());
		return nullptr;
	}

	if (!ParentGraphNode)
	{
		OutError = TEXT("BT_SUBNODE: parent node is required");
		return nullptr;
	}

	UAIGraphNode* AIParent = Cast<UAIGraphNode>(ParentGraphNode);
	if (!AIParent)
	{
		OutError = FString::Printf(TEXT("BT_SUBNODE: parent '%s' is not an AI graph node"),
			*ParentGraphNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
		return nullptr;
	}

	UBehaviorTree* BT = Graph->GetTypedOuter<UBehaviorTree>();
	if (!BT)
	{
		OutError = TEXT("BT_SUBNODE: could not resolve owning BehaviorTree");
		return nullptr;
	}

	// Rebuild for each spawn to avoid stale action/template state.
	if (!EnsureBTCachesPopulated(BT, true))
	{
		OutError = TEXT("BT_SUBNODE: failed to populate BehaviorTree action cache");
		return nullptr;
	}

	TSharedPtr<FEdGraphSchemaAction>* CachedAction = BTSubNodeActionCache.Find(SpawnerId);
	if (!CachedAction || !CachedAction->IsValid())
	{
		OutError = FString::Printf(TEXT("BT sub-node action not found in cache: %s. Run edit_graph with operation='find_nodes' first to discover available BT nodes."), *SpawnerId);
		return nullptr;
	}

	// FAISchemaAction_NewSubNode needs ParentNode set before PerformAction.
	// BTSubNodeActionCache is populated only with FAISchemaAction_NewSubNode instances.
	FAISchemaAction_NewSubNode* SubNodeAction = static_cast<FAISchemaAction_NewSubNode*>(CachedAction->Get());
	if (!SubNodeAction || !SubNodeAction->NodeTemplate)
	{
		OutError = FString::Printf(TEXT("BT_SUBNODE: invalid cached sub-node action/template: %s"), *SpawnerId);
		return nullptr;
	}

	// Snapshot current sub-nodes before adding
	TArray<UAIGraphNode*> OldSubNodes = AIParent->SubNodes;

	// Set parent and perform action (returns NULL — sub-node added internally)
	SubNodeAction->ParentNode = AIParent;
	TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	SubNodeAction->PerformAction(Graph, EmptyPins, FVector2f(0, 0), true);
#else
	SubNodeAction->PerformAction(Graph, EmptyPins, FVector2D(0, 0), true);
#endif

	// Prevent accidental reuse of consumed action/template.
	BTSubNodeActionCache.Remove(SpawnerId);

	// Find the new sub-node by diffing
	UAIGraphNode* NewSubNode = nullptr;
	for (UAIGraphNode* Sub : AIParent->SubNodes)
	{
		if (!OldSubNodes.Contains(Sub))
		{
			NewSubNode = Sub;
			break;
		}
	}

	if (!NewSubNode)
	{
		OutError = FString::Printf(TEXT("BT_SUBNODE: sub-node was not added to parent (action: %s)"), *SpawnerId);
		return nullptr;
	}

	return NewSubNode;
}

// ---------------------------------------------------------------------------
// BT_DECGRAPH spawn: decorator sub-graph logic nodes
// Same pattern as BT_ACTION but uses BTDecGraphActionCache
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_SpawnBTDecGraphNode(
	const FString& SpawnerId,
	UEdGraph* Graph,
	const FVector2D& Position,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("BT_DECGRAPH: graph is null");
		return nullptr;
	}

	if (!Graph->IsA<UBehaviorTreeDecoratorGraph>())
	{
		OutError = FString::Printf(TEXT("BT_DECGRAPH: graph is not a BehaviorTreeDecoratorGraph (%s)"),
			*Graph->GetClass()->GetName());
		return nullptr;
	}

	// Rebuild decorator graph cache for each spawn to avoid stale consumed actions/templates.
	BTDecGraphActionCache.Empty();
	const UEdGraphSchema* DecSchema = Graph->GetSchema();
	if (DecSchema)
	{
		FGraphContextMenuBuilder CtxBuilder(Graph);
		DecSchema->GetGraphContextActions(CtxBuilder);

		int32 AutoIdx = 0;
		for (int32 i = 0; i < CtxBuilder.GetNumActions(); i++)
		{
			TSharedPtr<FEdGraphSchemaAction> Action = CtxBuilder.GetSchemaAction(i);
			if (Action.IsValid() && !Action->GetMenuDescription().IsEmpty())
			{
				FString NodeName = Action->GetMenuDescription().ToString();
				FString AutoId = FString::Printf(TEXT("BT_DECGRAPH:%d:%s"),
					AutoIdx, *NodeName.Replace(TEXT(" "), TEXT("_")));
				BTDecGraphActionCache.Add(AutoId, Action);
				AutoIdx++;
			}
		}
	}

	TSharedPtr<FEdGraphSchemaAction>* CachedAction = BTDecGraphActionCache.Find(SpawnerId);
	if (!CachedAction || !CachedAction->IsValid())
	{
		OutError = FString::Printf(TEXT("BT decorator graph action not found in cache: %s. Run edit_graph with operation='find_nodes' and graph_name 'decorator:<name>' first."), *SpawnerId);
		return nullptr;
	}

	TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2f(Position.X, Position.Y), true);
#else
	UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2D(Position.X, Position.Y), true);
#endif

	// Prevent accidental reuse of consumed action/template.
	BTDecGraphActionCache.Remove(SpawnerId);

	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("BT_DECGRAPH PerformAction returned null: %s"), *SpawnerId);
		return nullptr;
	}

	// Keep transactional for undo/redo; PostPlacedNewNode already ran inside PerformAction.
	NewNode->SetFlags(RF_Transactional);

	return NewNode;
}

// ---------------------------------------------------------------------------
// BT property reflection: set properties on BT node instances
// Mirrors the pattern from EditBehaviorTreeTool::SetNodeProperties
// ---------------------------------------------------------------------------
TArray<FString> EditGraphTool_SetBTNodeValues(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& Values,
	UBehaviorTree* BT)
{
	TArray<FString> Results;

	UBehaviorTreeGraphNode* BTGNode = Cast<UBehaviorTreeGraphNode>(Node);
	if (!BTGNode)
	{
		Results.Add(TEXT("! Node is not a BehaviorTree graph node"));
		return Results;
	}

	UBTNode* BTNode = Cast<UBTNode>(BTGNode->NodeInstance);
	if (!BTNode)
	{
		Results.Add(TEXT("! BT graph node has no runtime NodeInstance"));
		return Results;
	}

	BTNode->Modify();

	for (const auto& Pair : Values->Values)
	{
		const FString& PropName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		// Special keys: enabled, comment (set on graph node, not runtime node)
		if (PropName.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
		{
			bool bEnabled = true;
			if (JsonValue->TryGetBool(bEnabled))
			{
				Node->SetEnabledState(bEnabled ? ENodeEnabledState::Enabled : ENodeEnabledState::Disabled, true);
				Results.Add(FString::Printf(TEXT("enabled = %s"), bEnabled ? TEXT("true") : TEXT("false")));
			}
			else
			{
				Results.Add(TEXT("! enabled: expected boolean value"));
			}
			continue;
		}

		if (PropName.Equals(TEXT("comment"), ESearchCase::IgnoreCase))
		{
			FString CommentStr;
			if (JsonValue->TryGetString(CommentStr))
			{
				Node->NodeComment = CommentStr;
				Node->bCommentBubbleVisible = !CommentStr.IsEmpty();
				Results.Add(FString::Printf(TEXT("comment = %s"), *CommentStr));
			}
			else
			{
				Results.Add(TEXT("! comment: expected string value"));
			}
			continue;
		}

		// Find property on the runtime BT node
		FProperty* Property = nullptr;
		for (TFieldIterator<FProperty> PropIt(BTNode->GetClass()); PropIt; ++PropIt)
		{
			if ((*PropIt)->GetName().Equals(PropName, ESearchCase::IgnoreCase))
			{
				Property = *PropIt;
				break;
			}
		}

		if (!Property)
		{
			Results.Add(FString::Printf(TEXT("! %s: property not found"), *PropName));
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(BTNode);
		if (!ValuePtr)
		{
			Results.Add(FString::Printf(TEXT("! %s: could not get value pointer"), *PropName));
			continue;
		}

		// Handle BlackboardKeySelector specially
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct && StructProp->Struct->GetName() == TEXT("BlackboardKeySelector"))
			{
				FBlackboardKeySelector* KeySelector = static_cast<FBlackboardKeySelector*>(ValuePtr);
				FString KeyName;
				if (JsonValue->TryGetString(KeyName))
				{
					KeySelector->SelectedKeyName = FName(*KeyName);
					if (BT && BT->BlackboardAsset)
					{
						KeySelector->ResolveSelectedKey(*BT->BlackboardAsset);
					}
					Results.Add(FString::Printf(TEXT("%s = %s (BlackboardKey)"), *PropName, *KeyName));
				}
				else
				{
					Results.Add(FString::Printf(TEXT("! %s: expected string key name"), *PropName));
				}
				continue;
			}
		}

		// Handle enum properties
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
						EnumVal = Enum->GetValueByNameString(Enum->GetName() + TEXT("::") + StringVal);
					}
					if (EnumVal != INDEX_NONE)
					{
						EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumVal);
						Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *StringVal));
					}
					else
					{
						Results.Add(FString::Printf(TEXT("! %s: invalid enum value '%s'"), *PropName, *StringVal));
					}
				}
				continue;
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
						ByteProp->SetPropertyValue_InContainer(BTNode, static_cast<uint8>(EnumVal));
						Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *StringVal));
					}
					else
					{
						Results.Add(FString::Printf(TEXT("! %s: invalid enum value '%s'"), *PropName, *StringVal));
					}
					continue;
				}
			}
		}

		// Handle JSON arrays → UE array format "(elem1, elem2, ...)"
		const TArray<TSharedPtr<FJsonValue>>* JsonArrayPtr = nullptr;
		if (JsonValue->TryGetArray(JsonArrayPtr) && JsonArrayPtr)
		{
			TArray<FString> Elements;
			for (const TSharedPtr<FJsonValue>& Elem : *JsonArrayPtr)
			{
				if (!Elem.IsValid())
				{
					Elements.Add(TEXT(""));
					continue;
				}
				FString ElemStr;
				if (Elem->TryGetString(ElemStr))
				{
					// String elements: wrap in quotes for struct/string arrays
					FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property);
					if (ArrayProp && (CastField<FStrProperty>(ArrayProp->Inner) ||
						CastField<FNameProperty>(ArrayProp->Inner) ||
						CastField<FTextProperty>(ArrayProp->Inner)))
					{
						Elements.Add(FString::Printf(TEXT("\"%s\""), *ElemStr));
					}
					else
					{
						Elements.Add(ElemStr);
					}
				}
				else
				{
					double ElemNum;
					bool ElemBool;
					if (Elem->TryGetNumber(ElemNum))
					{
						if (FMath::IsNearlyEqual(ElemNum, FMath::RoundToDouble(ElemNum)))
						{
							Elements.Add(FString::Printf(TEXT("%lld"), static_cast<int64>(ElemNum)));
						}
						else
						{
							Elements.Add(FString::SanitizeFloat(ElemNum));
						}
					}
					else if (Elem->TryGetBool(ElemBool))
					{
						Elements.Add(ElemBool ? TEXT("True") : TEXT("False"));
					}
					else
					{
						// Nested object/struct — serialize back to JSON string for ImportText
						FString ElemJson;
						TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ElemJson);
						FJsonSerializer::Serialize(Elem.ToSharedRef(), TEXT(""), Writer);
						Elements.Add(ElemJson);
					}
				}
			}

			FString TextValue = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
			const TCHAR* TextPtr = *TextValue;
			if (Property->ImportText_Direct(TextPtr, ValuePtr, BTNode, PPF_None))
			{
				Results.Add(FString::Printf(TEXT("%s = %s (%d elements)"), *PropName, *TextValue, Elements.Num()));
			}
			else
			{
				Results.Add(FString::Printf(TEXT("! %s: failed to import array '%s'"), *PropName, *TextValue));
			}
			continue;
		}

		// Generic: convert JSON to string and use ImportText
		FString TextValue;
		if (JsonValue->TryGetString(TextValue))
		{
			// Use directly
		}
		else
		{
			double NumValue;
			if (JsonValue->TryGetNumber(NumValue))
			{
				if (FMath::IsNearlyEqual(NumValue, FMath::RoundToDouble(NumValue)))
				{
					TextValue = FString::Printf(TEXT("%lld"), static_cast<int64>(NumValue));
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
					Results.Add(FString::Printf(TEXT("! %s: unsupported value type"), *PropName));
					continue;
				}
			}
		}

		const TCHAR* TextPtr = *TextValue;
		if (Property->ImportText_Direct(TextPtr, ValuePtr, BTNode, PPF_None))
		{
			Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *TextValue));
		}
		else
		{
			Results.Add(FString::Printf(TEXT("! %s: failed to set '%s'"), *PropName, *TextValue));
		}
	}

	// Re-initialize from asset to resolve blackboard keys
	if (BT && BT->BlackboardAsset)
	{
		BTNode->InitializeFromAsset(*BT);
	}

	return Results;
}

// ---------------------------------------------------------------------------
// BT post-operation finalization
// ---------------------------------------------------------------------------
void EditGraphTool_FinalizeBTGraph(UEdGraph* Graph, UObject* Asset)
{
	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(Graph);
	if (BTGraph)
	{
		BTGraph->UpdatePinConnectionTypes();
		BTGraph->UpdateAsset();
		BTGraph->UpdateBlackboardChange();
		Asset->MarkPackageDirty();
		return;
	}

	// Decorator sub-graph: find the owning CompositeDecorator and rebuild
	UBehaviorTreeDecoratorGraph* DecGraph = Cast<UBehaviorTreeDecoratorGraph>(Graph);
	if (DecGraph)
	{
		// Walk outer chain to find the BehaviorTree for MarkPackageDirty
		UObject* Outer = DecGraph->GetOuter();
		while (Outer)
		{
			if (UBehaviorTreeGraphNode_CompositeDecorator* CompDec = Cast<UBehaviorTreeGraphNode_CompositeDecorator>(Outer))
			{
				TArray<UBTDecorator*> DecInstances;
				TArray<FBTDecoratorLogic> DecOps;
				CompDec->CollectDecoratorData(DecInstances, DecOps);
				break;
			}
			Outer = Outer->GetOuter();
		}

		// Also update the parent BT graph
		Outer = DecGraph->GetOuter();
		while (Outer)
		{
			if (UBehaviorTreeGraph* ParentBTGraph = Cast<UBehaviorTreeGraph>(Outer))
			{
				ParentBTGraph->UpdateAsset();
				break;
			}
			if (UBehaviorTree* BT = Cast<UBehaviorTree>(Outer))
			{
				UBehaviorTreeGraph* ParentBTG = Cast<UBehaviorTreeGraph>(BT->BTGraph);
				if (ParentBTG)
				{
					ParentBTG->UpdateAsset();
				}
				break;
			}
			Outer = Outer->GetOuter();
		}

		Asset->MarkPackageDirty();
	}
}

// ---------------------------------------------------------------------------
// Check if an asset is a BehaviorTree
// ---------------------------------------------------------------------------
bool EditGraphTool_IsBehaviorTree(UObject* Asset)
{
	return Asset && Asset->IsA<UBehaviorTree>();
}

// ---------------------------------------------------------------------------
// Check if a graph is a BT graph or BT decorator graph
// ---------------------------------------------------------------------------
bool EditGraphTool_IsBTGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}
	return Graph->IsA<UBehaviorTreeGraph>() || Graph->IsA<UBehaviorTreeDecoratorGraph>();
}

// ---------------------------------------------------------------------------
// Get the UBehaviorTree* from an asset (for property reflection)
// ---------------------------------------------------------------------------
UBehaviorTree* EditGraphTool_GetBehaviorTree(UObject* Asset)
{
	return Cast<UBehaviorTree>(Asset);
}

// ---------------------------------------------------------------------------
// Find a BT sub-node (decorator/service) by name or GUID across all parent nodes
// ResolveNodeRef only searches Graph->Nodes which doesn't include sub-nodes.
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_FindBTSubNode(
	UEdGraph* Graph,
	const FString& NodeRef,
	const FString& AssetPath,
	const FString& GraphName)
{
	if (!Graph)
	{
		return nullptr;
	}

	// Check if NodeRef is a registered name
	FGuid RegisteredGuid = FNodeNameRegistry::Get().Resolve(AssetPath, GraphName, NodeRef);
	bool bHasRegistered = RegisteredGuid.IsValid();

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
		if (!AINode)
		{
			continue;
		}

		for (UAIGraphNode* SubNode : AINode->SubNodes)
		{
			if (!SubNode)
			{
				continue;
			}

			// Match by registered GUID
			if (bHasRegistered && SubNode->NodeGuid == RegisteredGuid)
			{
				return SubNode;
			}

			// Match by raw GUID string
			if (SubNode->NodeGuid.ToString().Equals(NodeRef, ESearchCase::IgnoreCase) ||
				SubNode->NodeGuid.ToString().Left(8).Equals(NodeRef, ESearchCase::IgnoreCase))
			{
				return SubNode;
			}

		}
	}

	return nullptr;
}
