// Copyright 2026 Betide Studio. All Rights Reserved.
// EQS (Environment Query System) spawn paths and property reflection for EditGraphTool

#include "Tools/EditGraphTool.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Tools/NodeNameRegistry.h"
#include "Tools/NeoStackToolUtils.h"
#include "AgentIntegrationKitModule.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

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

// AI Graph base
#include "AIGraphSchema.h"
#include "AIGraphNode.h"

// Property reflection
#include "UObject/UnrealType.h"

// Editor
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EdGraph/EdGraphSchema.h"

// Asset factory
#include "EnvironmentQueryFactory.h"
#include "AssetToolsModule.h"

// EQS action caches — defined in FindNodeTool_EQS.cpp
extern TMap<FString, TSharedPtr<FEdGraphSchemaAction>> EQSOptionActionCache;
extern TMap<FString, TSharedPtr<FEdGraphSchemaAction>> EQSTestActionCache;

// Auto-populate EQS caches on demand — defined in FindNodeTool_EQS.cpp
extern bool EnsureEQSCachesPopulated(UEnvQuery* Query, bool bForceRebuild = false);

// ---------------------------------------------------------------------------
// Helper: ensure the EQS editor graph exists
// ---------------------------------------------------------------------------
static UEnvironmentQueryGraph* EnsureEQSGraphForEdit(UEnvQuery* Query)
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
// EQS graph detection for EditGraphTool::Execute()
// Returns the target graph and registers Root + existing nodes
// ---------------------------------------------------------------------------
UEdGraph* EditGraphTool_DetectEQSGraph(
	UObject* Asset,
	const FString& GraphName,
	const FString& FullAssetPath,
	FString& OutActualGraphName,
	TArray<FString>& Errors)
{
	UEnvQuery* Query = Cast<UEnvQuery>(Asset);
	if (!Query)
	{
		return nullptr;
	}

	UEnvironmentQueryGraph* EQSGraph = EnsureEQSGraphForEdit(Query);
	if (!EQSGraph)
	{
		Errors.Add(TEXT("Failed to create or find EQS editor graph"));
		return nullptr;
	}

	// Initialize the graph (spawns missing nodes, calculates weights)
	EQSGraph->Initialize();

	OutActualGraphName = EQSGraph->GetName();

	TMap<FString, int32> NameCounts; // Track duplicates

	for (UEdGraphNode* Node : EQSGraph->Nodes)
	{
		if (!Node)
		{
			continue;
		}

		// Register Root node
		if (Node->IsA<UEnvironmentQueryGraphNode_Root>())
		{
			FNodeNameRegistry::Get().Register(FullAssetPath, OutActualGraphName, TEXT("Root"), Node->NodeGuid);
			continue;
		}

		// Register Option nodes (generators)
		UEnvironmentQueryGraphNode_Option* OptionNode = Cast<UEnvironmentQueryGraphNode_Option>(Node);
		if (OptionNode)
		{
			FString Title = OptionNode->GetNodeTitle(ENodeTitleType::ListView).ToString().TrimStartAndEnd();
			if (!Title.IsEmpty())
			{
				int32& Count = NameCounts.FindOrAdd(Title);
				FString RegName = (Count == 0) ? Title : FString::Printf(TEXT("%s_%d"), *Title, Count);
				Count++;
				FNodeNameRegistry::Get().Register(FullAssetPath, OutActualGraphName, RegName, OptionNode->NodeGuid);
			}

			// Register sub-nodes (tests) on this option
			for (UAIGraphNode* SubNode : OptionNode->SubNodes)
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
	}

	return EQSGraph;
}

// ---------------------------------------------------------------------------
// EQS_OPTION spawn: generators via cached FAISchemaAction_NewNode
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_SpawnEQSOption(
	const FString& SpawnerId,
	UEdGraph* Graph,
	const FVector2D& Position,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("EQS_OPTION: graph is null");
		return nullptr;
	}

	if (!Graph->IsA<UEnvironmentQueryGraph>())
	{
		OutError = FString::Printf(TEXT("EQS_OPTION: graph is not an EQS graph (%s)"),
			*Graph->GetClass()->GetName());
		return nullptr;
	}

	UEnvQuery* Query = Graph->GetTypedOuter<UEnvQuery>();
	if (!Query)
	{
		OutError = TEXT("EQS_OPTION: could not resolve owning UEnvQuery");
		return nullptr;
	}

	// Rebuild cache for each spawn to avoid reusing consumed schema actions/templates
	if (!EnsureEQSCachesPopulated(Query, true))
	{
		OutError = TEXT("EQS_OPTION: failed to populate EQS action cache");
		return nullptr;
	}

	TSharedPtr<FEdGraphSchemaAction>* CachedAction = EQSOptionActionCache.Find(SpawnerId);
	if (!CachedAction || !CachedAction->IsValid())
	{
		OutError = FString::Printf(TEXT("EQS option not found in cache: %s. Run edit_graph with operation='find_nodes' first to discover available EQS nodes."), *SpawnerId);
		return nullptr;
	}

	TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2f(Position.X, Position.Y), true);
#else
	UEdGraphNode* NewNode = (*CachedAction)->PerformAction(Graph, EmptyPins, FVector2D(Position.X, Position.Y), true);
#endif

	// Prevent reuse
	EQSOptionActionCache.Remove(SpawnerId);

	if (!NewNode)
	{
		OutError = FString::Printf(TEXT("EQS_OPTION PerformAction returned null: %s"), *SpawnerId);
		return nullptr;
	}

	NewNode->SetFlags(RF_Transactional);

	// Auto-connect to Root node's output pin
	UEnvironmentQueryGraphNode_Root* RootNode = nullptr;
	for (UEdGraphNode* ExistingNode : Graph->Nodes)
	{
		if (UEnvironmentQueryGraphNode_Root* Root = Cast<UEnvironmentQueryGraphNode_Root>(ExistingNode))
		{
			RootNode = Root;
			break;
		}
	}

	if (RootNode && RootNode->Pins.Num() > 0)
	{
		UEdGraphPin* RootOutputPin = nullptr;
		for (UEdGraphPin* Pin : RootNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output)
			{
				RootOutputPin = Pin;
				break;
			}
		}

		UEdGraphPin* OptionInputPin = nullptr;
		for (UEdGraphPin* Pin : NewNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input)
			{
				OptionInputPin = Pin;
				break;
			}
		}

		if (RootOutputPin && OptionInputPin)
		{
			RootOutputPin->MakeLinkTo(OptionInputPin);
		}
	}

	return NewNode;
}

// ---------------------------------------------------------------------------
// EQS_TEST spawn: tests via cached FAISchemaAction_NewSubNode
// PerformAction returns NULL for sub-nodes — we find the new node by diffing
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_SpawnEQSTest(
	const FString& SpawnerId,
	UEdGraph* Graph,
	UEdGraphNode* ParentOptionNode,
	FString& OutError)
{
	if (!Graph)
	{
		OutError = TEXT("EQS_TEST: graph is null");
		return nullptr;
	}

	if (!Graph->IsA<UEnvironmentQueryGraph>())
	{
		OutError = FString::Printf(TEXT("EQS_TEST: graph is not an EQS graph (%s)"),
			*Graph->GetClass()->GetName());
		return nullptr;
	}

	if (!ParentOptionNode)
	{
		OutError = TEXT("EQS_TEST: parent node is required (must be an Option/generator node)");
		return nullptr;
	}

	UAIGraphNode* AIParent = Cast<UAIGraphNode>(ParentOptionNode);
	if (!AIParent)
	{
		OutError = FString::Printf(TEXT("EQS_TEST: parent '%s' is not an AI graph node"),
			*ParentOptionNode->GetNodeTitle(ENodeTitleType::ListView).ToString());
		return nullptr;
	}

	UEnvQuery* Query = Graph->GetTypedOuter<UEnvQuery>();
	if (!Query)
	{
		OutError = TEXT("EQS_TEST: could not resolve owning UEnvQuery");
		return nullptr;
	}

	// Rebuild for each spawn
	if (!EnsureEQSCachesPopulated(Query, true))
	{
		OutError = TEXT("EQS_TEST: failed to populate EQS action cache");
		return nullptr;
	}

	TSharedPtr<FEdGraphSchemaAction>* CachedAction = EQSTestActionCache.Find(SpawnerId);
	if (!CachedAction || !CachedAction->IsValid())
	{
		OutError = FString::Printf(TEXT("EQS test not found in cache: %s. Run edit_graph with operation='find_nodes' first to discover available EQS nodes."), *SpawnerId);
		return nullptr;
	}

	FAISchemaAction_NewSubNode* SubNodeAction = static_cast<FAISchemaAction_NewSubNode*>(CachedAction->Get());
	if (!SubNodeAction || !SubNodeAction->NodeTemplate)
	{
		OutError = FString::Printf(TEXT("EQS_TEST: invalid cached sub-node action/template: %s"), *SpawnerId);
		return nullptr;
	}

	// Snapshot current sub-nodes before adding
	TArray<UAIGraphNode*> OldSubNodes = AIParent->SubNodes;

	// Set parent and perform action
	SubNodeAction->ParentNode = AIParent;
	TArray<UEdGraphPin*> EmptyPins;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	SubNodeAction->PerformAction(Graph, EmptyPins, FVector2f(0, 0), true);
#else
	SubNodeAction->PerformAction(Graph, EmptyPins, FVector2D(0, 0), true);
#endif

	// Prevent reuse
	EQSTestActionCache.Remove(SpawnerId);

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
		OutError = FString::Printf(TEXT("EQS_TEST: sub-node was not added to parent (action: %s)"), *SpawnerId);
		return nullptr;
	}

	return NewSubNode;
}

// ---------------------------------------------------------------------------
// Helper: check if a property is an FAIDataProvider*Value struct
// Returns the default value field name if it is, empty string otherwise
// ---------------------------------------------------------------------------
static FString GetAIDataProviderDefaultFieldName(FStructProperty* StructProp)
{
	if (!StructProp || !StructProp->Struct)
	{
		return TEXT("");
	}

	FString StructName = StructProp->Struct->GetName();
	if (StructName == TEXT("AIDataProviderFloatValue") ||
		StructName == TEXT("AIDataProviderIntValue") ||
		StructName == TEXT("AIDataProviderBoolValue"))
	{
		return TEXT("DefaultValue");
	}
	return TEXT("");
}

// ---------------------------------------------------------------------------
// EQS property reflection: set properties on EQS node instances
// Handles FAIDataProvider*Value shorthand and standard reflection
// ---------------------------------------------------------------------------
TArray<FString> EditGraphTool_SetEQSNodeValues(
	UEdGraphNode* Node,
	const TSharedPtr<FJsonObject>& Values)
{
	TArray<FString> Results;

	// EQS nodes can be Option (generator) or Test nodes
	UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
	if (!AINode)
	{
		Results.Add(TEXT("! Node is not an AI graph node"));
		return Results;
	}

	// Get the runtime instance (UEnvQueryOption for generators, UEnvQueryTest for tests)
	UObject* NodeInstance = AINode->NodeInstance;
	if (!NodeInstance)
	{
		Results.Add(TEXT("! EQS graph node has no runtime NodeInstance"));
		return Results;
	}

	// For Option nodes, the NodeInstance is UEnvQueryOption — we want to set properties
	// on the Generator, not the option container
	UObject* PropertyTarget = NodeInstance;
	UEnvQueryOption* Option = Cast<UEnvQueryOption>(NodeInstance);
	if (Option && Option->Generator)
	{
		PropertyTarget = Option->Generator;
	}

	PropertyTarget->Modify();

	for (const auto& Pair : Values->Values)
	{
		const FString& PropName = Pair.Key;
		const TSharedPtr<FJsonValue>& JsonValue = Pair.Value;

		// Special keys: enabled, comment
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

		// Handle bTestEnabled on test graph nodes specifically
		if (PropName.Equals(TEXT("bTestEnabled"), ESearchCase::IgnoreCase))
		{
			UEnvironmentQueryGraphNode_Test* TestNode = Cast<UEnvironmentQueryGraphNode_Test>(Node);
			if (TestNode)
			{
				bool bVal = true;
				if (JsonValue->TryGetBool(bVal))
				{
					TestNode->bTestEnabled = bVal;
					Results.Add(FString::Printf(TEXT("bTestEnabled = %s"), bVal ? TEXT("true") : TEXT("false")));
				}
				else
				{
					Results.Add(TEXT("! bTestEnabled: expected boolean value"));
				}
			}
			else
			{
				Results.Add(TEXT("! bTestEnabled: node is not an EQS Test node"));
			}
			continue;
		}

		// Handle dot-path for nested struct access (e.g., "GridSize.DefaultValue")
		FString BasePropName = PropName;
		FString SubFieldPath;
		int32 DotIndex;
		if (PropName.FindChar(TEXT('.'), DotIndex))
		{
			BasePropName = PropName.Left(DotIndex);
			SubFieldPath = PropName.Mid(DotIndex + 1);
		}

		// Find property on the target instance
		FProperty* Property = nullptr;
		for (TFieldIterator<FProperty> PropIt(PropertyTarget->GetClass()); PropIt; ++PropIt)
		{
			if ((*PropIt)->GetName().Equals(BasePropName, ESearchCase::IgnoreCase))
			{
				Property = *PropIt;
				break;
			}
		}

		if (!Property)
		{
			Results.Add(FString::Printf(TEXT("! %s: property not found on %s"), *PropName, *PropertyTarget->GetClass()->GetName()));
			continue;
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(PropertyTarget);
		if (!ValuePtr)
		{
			Results.Add(FString::Printf(TEXT("! %s: could not get value pointer"), *PropName));
			continue;
		}

		// Check for FAIDataProvider*Value struct — support direct value shorthand
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			FString DefaultFieldName = GetAIDataProviderDefaultFieldName(StructProp);
			if (!DefaultFieldName.IsEmpty())
			{
				// If a dot-path was used, resolve the sub-field within the struct
				if (!SubFieldPath.IsEmpty())
				{
					FProperty* SubProp = StructProp->Struct->FindPropertyByName(FName(*SubFieldPath));
					if (SubProp)
					{
						void* SubPtr = SubProp->ContainerPtrToValuePtr<void>(ValuePtr);
						if (SubPtr)
						{
							FString TextValue;
							double NumValue;
							bool BoolValue;
							if (JsonValue->TryGetNumber(NumValue))
							{
								TextValue = FString::SanitizeFloat(NumValue);
							}
							else if (JsonValue->TryGetBool(BoolValue))
							{
								TextValue = BoolValue ? TEXT("true") : TEXT("false");
							}
							else if (JsonValue->TryGetString(TextValue))
							{
								// already set
							}
							else
							{
								Results.Add(FString::Printf(TEXT("! %s: unsupported value type"), *PropName));
								continue;
							}

							const TCHAR* TextPtr = *TextValue;
							if (SubProp->ImportText_Direct(TextPtr, SubPtr, PropertyTarget, PPF_None))
							{
								Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *TextValue));
							}
							else
							{
								Results.Add(FString::Printf(TEXT("! %s: failed to set '%s'"), *PropName, *TextValue));
							}
							continue;
						}
					}
					Results.Add(FString::Printf(TEXT("! %s: sub-field '%s' not found"), *BasePropName, *SubFieldPath));
					continue;
				}

				// Direct value shorthand: "GridSize": 500.0 → sets GridSize.DefaultValue = 500.0
				double NumValue;
				bool BoolValue;
				if (JsonValue->TryGetNumber(NumValue) || JsonValue->TryGetBool(BoolValue))
				{
					FProperty* DefaultProp = StructProp->Struct->FindPropertyByName(FName(*DefaultFieldName));
					if (DefaultProp)
					{
						void* DefaultPtr = DefaultProp->ContainerPtrToValuePtr<void>(ValuePtr);
						if (DefaultPtr)
						{
							FString TextValue;
							if (JsonValue->TryGetNumber(NumValue))
							{
								TextValue = FString::SanitizeFloat(NumValue);
							}
							else
							{
								JsonValue->TryGetBool(BoolValue);
								TextValue = BoolValue ? TEXT("true") : TEXT("false");
							}

							const TCHAR* TextPtr = *TextValue;
							if (DefaultProp->ImportText_Direct(TextPtr, DefaultPtr, PropertyTarget, PPF_None))
							{
								Results.Add(FString::Printf(TEXT("%s.DefaultValue = %s"), *BasePropName, *TextValue));
							}
							else
							{
								Results.Add(FString::Printf(TEXT("! %s: failed to set DefaultValue to '%s'"), *BasePropName, *TextValue));
							}
							continue;
						}
					}
				}

				// Fall through to generic handling for string/object values on the struct
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
						ByteProp->SetPropertyValue_InContainer(PropertyTarget, static_cast<uint8>(EnumVal));
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

		// Handle TSubclassOf<UEnvQueryContext> — context class references
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
		{
			FString ClassName;
			if (JsonValue->TryGetString(ClassName))
			{
				// Try to find the class by name
				UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
				if (!FoundClass)
				{
					// Try with common prefixes
					FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), *ClassName));
				}
				if (!FoundClass)
				{
					// Search all classes
					for (TObjectIterator<UClass> It; It; ++It)
					{
						if ((*It)->GetName().Equals(ClassName, ESearchCase::IgnoreCase))
						{
							FoundClass = *It;
							break;
						}
					}
				}

				if (FoundClass)
				{
					ClassProp->SetObjectPropertyValue(ValuePtr, FoundClass);
					Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *FoundClass->GetName()));
				}
				else
				{
					Results.Add(FString::Printf(TEXT("! %s: class not found '%s'"), *PropName, *ClassName));
				}
				continue;
			}
		}

		// Handle soft class references (TSoftClassPtr)
		if (FSoftClassProperty* SoftClassProp = CastField<FSoftClassProperty>(Property))
		{
			FString ClassName;
			if (JsonValue->TryGetString(ClassName))
			{
				UClass* FoundClass = FindObject<UClass>(nullptr, *ClassName);
				if (!FoundClass)
				{
					FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/AIModule.%s"), *ClassName));
				}
				if (!FoundClass)
				{
					for (TObjectIterator<UClass> It; It; ++It)
					{
						if ((*It)->GetName().Equals(ClassName, ESearchCase::IgnoreCase))
						{
							FoundClass = *It;
							break;
						}
					}
				}

				if (FoundClass)
				{
					FString ClassPathName = FoundClass->GetPathName();
					if (SoftClassProp->ImportText_Direct(*ClassPathName, ValuePtr, PropertyTarget, PPF_None))
					{
						Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *FoundClass->GetName()));
					}
					else
					{
						Results.Add(FString::Printf(TEXT("! %s: failed to import class '%s'"), *PropName, *ClassName));
					}
				}
				else
				{
					Results.Add(FString::Printf(TEXT("! %s: class not found '%s'"), *PropName, *ClassName));
				}
				continue;
			}
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
		if (Property->ImportText_Direct(TextPtr, ValuePtr, PropertyTarget, PPF_None))
		{
			Results.Add(FString::Printf(TEXT("%s = %s"), *PropName, *TextValue));
		}
		else
		{
			Results.Add(FString::Printf(TEXT("! %s: failed to set '%s'"), *PropName, *TextValue));
		}
	}

	return Results;
}

// ---------------------------------------------------------------------------
// EQS post-operation finalization — sync graph → runtime
// ---------------------------------------------------------------------------
void EditGraphTool_FinalizeEQSGraph(UEdGraph* Graph, UObject* Asset)
{
	UEnvironmentQueryGraph* EQSGraph = Cast<UEnvironmentQueryGraph>(Graph);
	if (EQSGraph)
	{
		EQSGraph->UpdateAsset();
		// Note: CalculateAllWeights() is not exported from EnvironmentQueryEditor module.
		// Weights are recalculated automatically when the asset is opened in the editor.
		Asset->MarkPackageDirty();
	}
}

// ---------------------------------------------------------------------------
// Check if an asset is an EQS query
// ---------------------------------------------------------------------------
bool EditGraphTool_IsEnvQuery(UObject* Asset)
{
	return Asset && Asset->IsA<UEnvQuery>();
}

// ---------------------------------------------------------------------------
// Check if a graph is an EQS graph
// ---------------------------------------------------------------------------
bool EditGraphTool_IsEQSGraph(UEdGraph* Graph)
{
	if (!Graph)
	{
		return false;
	}
	return Graph->IsA<UEnvironmentQueryGraph>();
}

// ---------------------------------------------------------------------------
// Get the UEnvQuery* from an asset
// ---------------------------------------------------------------------------
UEnvQuery* EditGraphTool_GetEnvQuery(UObject* Asset)
{
	return Cast<UEnvQuery>(Asset);
}

// ---------------------------------------------------------------------------
// Find an EQS sub-node (test) by name or GUID across all option nodes
// ResolveNodeRef only searches Graph->Nodes which doesn't include sub-nodes.
// ---------------------------------------------------------------------------
UEdGraphNode* EditGraphTool_FindEQSSubNode(
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

// ---------------------------------------------------------------------------
// Create a new EQS asset
// ---------------------------------------------------------------------------
UEnvQuery* EditGraphTool_CreateEQSAsset(const FString& Name, const FString& Path, FString& OutError)
{
	if (Name.IsEmpty())
	{
		OutError = TEXT("Asset name is empty");
		return nullptr;
	}

	FString FullPath = Path;
	if (FullPath.IsEmpty())
	{
		FullPath = TEXT("/Game");
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UEnvironmentQueryFactory* Factory = NewObject<UEnvironmentQueryFactory>();

	UObject* NewAsset = AssetTools.CreateAsset(Name, FullPath, UEnvQuery::StaticClass(), Factory);
	UEnvQuery* Query = Cast<UEnvQuery>(NewAsset);
	if (!Query)
	{
		OutError = FString::Printf(TEXT("Failed to create EQS asset: %s/%s"), *FullPath, *Name);
		return nullptr;
	}

	return Query;
}

#else // ENGINE_MINOR_VERSION < 6 — stub implementations

#include "EnvironmentQuery/EnvQuery.h"

UEdGraph* EditGraphTool_DetectEQSGraph(UObject*, const FString&, const FString&, FString&, TArray<FString>& Errors)
{ Errors.Add(TEXT("EQS editing requires UE 5.6+")); return nullptr; }
UEdGraphNode* EditGraphTool_SpawnEQSOption(const FString&, UEdGraph*, const FVector2D&, FString& E)
{ E = TEXT("EQS requires UE 5.6+"); return nullptr; }
UEdGraphNode* EditGraphTool_SpawnEQSTest(const FString&, UEdGraph*, UEdGraphNode*, FString& E)
{ E = TEXT("EQS requires UE 5.6+"); return nullptr; }
TArray<FString> EditGraphTool_SetEQSNodeValues(UEdGraphNode*, const TSharedPtr<FJsonObject>&)
{ return { TEXT("! EQS requires UE 5.6+") }; }
void EditGraphTool_FinalizeEQSGraph(UEdGraph*, UObject*) {}
bool EditGraphTool_IsEnvQuery(UObject*) { return false; }
bool EditGraphTool_IsEQSGraph(UEdGraph*) { return false; }
UEnvQuery* EditGraphTool_GetEnvQuery(UObject*) { return nullptr; }
UEdGraphNode* EditGraphTool_FindEQSSubNode(UEdGraph*, const FString&, const FString&, const FString&) { return nullptr; }
UEnvQuery* EditGraphTool_CreateEQSAsset(const FString&, const FString&, FString& E)
{ E = TEXT("EQS requires UE 5.6+"); return nullptr; }

#endif
