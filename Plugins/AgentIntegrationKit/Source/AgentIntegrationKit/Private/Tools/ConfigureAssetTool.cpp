// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/ConfigureAssetTool.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyIterator.h"
#include "UObject/PropertyAccessUtil.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "Animation/AnimBlueprint.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstance.h"
#include "Engine/Texture.h"
#include "MaterialEditingLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"

// For accessing Material Editor preview material
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IMaterialEditor.h"

// Widget Blueprint support
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/OverlaySlot.h"
#include "Widgets/Layout/Anchors.h"
#include "WidgetBlueprintEditor.h"

// Blueprint component support
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Components/ActorComponent.h"

// Animation Montage post-edit support
#include "Animation/AnimMontage.h"

// BlendSpace post-edit support
#include "Animation/BlendSpace.h"

// Transaction support for undo/redo
#include "ScopedTransaction.h"

// BehaviorTree support
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"

// Graph node targeting support
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachineBase.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNodeBase.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateConduitNode.h"
#include "K2Node_Composite.h"
#include "Containers/Ticker.h"

namespace
{
static bool IsTransientWorldObject(const UObject* Object)
{
	if (!Object)
	{
		return false;
	}

	const UWorld* WorldOuter = Object->GetTypedOuter<UWorld>();
	return WorldOuter && WorldOuter->GetPackage() == GetTransientPackage();
}

static void QueueWidgetPreviewRefresh(UWidgetBlueprint* WidgetBlueprint)
{
	if (!WidgetBlueprint)
	{
		return;
	}

	const TWeakObjectPtr<UWidgetBlueprint> WeakWidgetBlueprint(WidgetBlueprint);
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([WeakWidgetBlueprint](float)
		{
			if (!GEditor || !WeakWidgetBlueprint.IsValid())
			{
				return false;
			}

			UWidgetBlueprint* SafeWidgetBlueprint = WeakWidgetBlueprint.Get();
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (!AssetEditorSubsystem)
			{
				return false;
			}

			IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(SafeWidgetBlueprint, false);
			if (!EditorInstance || EditorInstance->GetEditorName() != TEXT("WidgetBlueprintEditor"))
			{
				return false;
			}

			FWidgetBlueprintEditor* WidgetEditor = static_cast<FWidgetBlueprintEditor*>(EditorInstance);
			WidgetEditor->InvalidatePreview();
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack ConfigureAsset: Deferred widget preview refresh for %s"), *SafeWidgetBlueprint->GetName());
			return false;
		}),
		0.0f);
}

static bool IsPinQueryPath(const FString& Query)
{
	return Query.StartsWith(TEXT("pin."), ESearchCase::IgnoreCase) ||
		Query.StartsWith(TEXT("pins."), ESearchCase::IgnoreCase);
}

static void AddPinQueryCandidates(const FString& Query, TArray<FString>& OutCandidates)
{
	auto AddUnique = [&OutCandidates](const FString& Candidate)
	{
		const FString Trimmed = Candidate.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return;
		}

		for (const FString& Existing : OutCandidates)
		{
			if (Existing.Equals(Trimmed, ESearchCase::IgnoreCase))
			{
				return;
			}
		}
		OutCandidates.Add(Trimmed);
	};

	AddUnique(Query);

	if (Query.StartsWith(TEXT("pin."), ESearchCase::IgnoreCase))
	{
		AddUnique(Query.Mid(4));
	}
	if (Query.StartsWith(TEXT("pins."), ESearchCase::IgnoreCase))
	{
		AddUnique(Query.Mid(5));
	}
	if (Query.StartsWith(TEXT("node."), ESearchCase::IgnoreCase))
	{
		AddUnique(Query.Mid(5));
	}

	int32 LastDotIndex = INDEX_NONE;
	if (Query.FindLastChar(TEXT('.'), LastDotIndex))
	{
		AddUnique(Query.Mid(LastDotIndex + 1));
	}
}

static UEdGraphPin* FindNodePinByQuery(UEdGraphNode* Node, const FString& Query, bool bInputOnly = true)
{
	if (!Node || Query.IsEmpty())
	{
		return nullptr;
	}

	TArray<FString> Candidates;
	AddPinQueryCandidates(Query, Candidates);

	for (const FString& Candidate : Candidates)
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin)
			{
				continue;
			}

			if (bInputOnly && Pin->Direction != EGPD_Input)
			{
				continue;
			}

			if (Pin->PinName.ToString().Equals(Candidate, ESearchCase::IgnoreCase))
			{
				return Pin;
			}
		}
	}

	return nullptr;
}

static FString GetPinDefaultValueString(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("");
	}

	if (Pin->DefaultObject)
	{
		return Pin->DefaultObject->GetPathName();
	}
	if (!Pin->DefaultTextValue.IsEmpty())
	{
		return Pin->DefaultTextValue.ToString();
	}
	if (!Pin->DefaultValue.IsEmpty())
	{
		return Pin->DefaultValue;
	}
	if (!Pin->AutogeneratedDefaultValue.IsEmpty())
	{
		return Pin->AutogeneratedDefaultValue;
	}
	return TEXT("");
}

static FString GetReadablePinValueString(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("");
	}

	if (Pin->LinkedTo.Num() > 0)
	{
		return FString::Printf(TEXT("<linked:%d>"), Pin->LinkedTo.Num());
	}

	FString Value = GetPinDefaultValueString(Pin);
	return Value.IsEmpty() ? TEXT("<unset>") : Value;
}

static FString GetPinTypeDisplayString(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("unknown");
	}

	const FEdGraphPinType& PinType = Pin->PinType;
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return TEXT("exec");
	}

	FString TypeName = PinType.PinCategory.ToString();
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeName = PinType.PinSubCategoryObject->GetName();
	}
	else if (!PinType.PinSubCategory.IsNone())
	{
		TypeName = PinType.PinSubCategory.ToString();
	}

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

	if (PinType.bIsReference)
	{
		TypeName += TEXT("&");
	}

	return TypeName;
}

static UClass* TryLoadClassFromPinValue(const FString& RawValue)
{
	const FString Value = RawValue.TrimStartAndEnd();
	if (Value.IsEmpty() || Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
	{
		return nullptr;
	}

	if (UClass* AsClass = LoadClass<UObject>(nullptr, *Value))
	{
		return AsClass;
	}

	if (UBlueprint* AsBlueprint = LoadObject<UBlueprint>(nullptr, *Value))
	{
		return AsBlueprint->GeneratedClass;
	}

	if (Value.StartsWith(TEXT("/")))
	{
		FString ClassPath = Value;
		if (!ClassPath.EndsWith(TEXT("_C")))
		{
			int32 DotIndex = ClassPath.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (DotIndex != INDEX_NONE)
			{
				ClassPath += TEXT("_C");
			}
			else
			{
				int32 SlashIndex = ClassPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				const FString AssetName = ClassPath.Mid(SlashIndex + 1);
				ClassPath = ClassPath + TEXT(".") + AssetName + TEXT("_C");
			}
		}

		if (UClass* PathClass = LoadClass<UObject>(nullptr, *ClassPath))
		{
			return PathClass;
		}
	}

	return nullptr;
}

static bool SetNodePinDefaultValue(UEdGraphNode* Node, UEdGraphPin* Pin, const FString& InValue, FString& OutError)
{
	if (!Node || !Pin)
	{
		OutError = TEXT("Invalid node or pin");
		return false;
	}

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		OutError = TEXT("Cannot set defaults on exec pins");
		return false;
	}

	const UEdGraphSchema* Schema = Node->GetGraph() ? Node->GetGraph()->GetSchema() : nullptr;
	FString Value = InValue.TrimStartAndEnd();

	const bool bIsObjectLike =
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass;

	if (bIsObjectLike)
	{
		UObject* NewDefaultObject = nullptr;
		if (!Value.IsEmpty() && !Value.Equals(TEXT("None"), ESearchCase::IgnoreCase))
		{
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
				Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass)
			{
				NewDefaultObject = TryLoadClassFromPinValue(Value);
				if (!NewDefaultObject)
				{
					OutError = FString::Printf(TEXT("Could not find class: %s"), *Value);
					return false;
				}
			}
			else
			{
				NewDefaultObject = LoadObject<UObject>(nullptr, *Value);
				if (!NewDefaultObject)
				{
					OutError = FString::Printf(TEXT("Could not find object: %s"), *Value);
					return false;
				}
			}
		}

		if (Schema)
		{
			Schema->TrySetDefaultObject(*Pin, NewDefaultObject);
		}
		else
		{
			Pin->DefaultObject = NewDefaultObject;
		}
		Node->PinDefaultValueChanged(Pin);
		return true;
	}

	if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Text)
	{
		if (Schema)
		{
			Schema->TrySetDefaultText(*Pin, FText::FromString(Value));
		}
		else
		{
			Pin->DefaultTextValue = FText::FromString(Value);
		}
		Node->PinDefaultValueChanged(Pin);
		return true;
	}

	if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
	{
		Value = TEXT("True");
	}
	else if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		Value = TEXT("False");
	}

	if (Schema)
	{
		Schema->TrySetDefaultValue(*Pin, Value);
	}
	else
	{
		Pin->DefaultValue = Value;
	}
	Node->PinDefaultValueChanged(Pin);
	return true;
}
} // namespace

TSharedPtr<FJsonObject> FConfigureAssetTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Asset name or path (Material, Blueprint, AnimBP, Widget)"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> SubobjectProp = MakeShared<FJsonObject>();
	SubobjectProp->SetStringField(TEXT("type"), TEXT("string"));
	SubobjectProp->SetStringField(TEXT("description"),
		TEXT("Subobject name: widget in WBP, component in BP, or node name in BehaviorTree. "
		     "Use 'class_defaults' for Blueprint Class Defaults (Actor CDO) to set properties like "
		     "bReplicates, bAlwaysRelevant, NetUpdateFrequency, AutoPossessPlayer, etc."));
	Properties->SetObjectField(TEXT("subobject"), SubobjectProp);

	TSharedPtr<FJsonObject> GraphProp = MakeShared<FJsonObject>();
	GraphProp->SetStringField(TEXT("type"), TEXT("string"));
	GraphProp->SetStringField(TEXT("description"),
		TEXT("Graph selector for targeting nodes inside Blueprint graphs. "
		     "Typed selectors: 'animgraph:AnimGraph', 'statemachine:AnimGraph/Locomotion', "
		     "'state:AnimGraph/Idle', 'transition:AnimGraph/Idle->Walk', 'conduit:AnimGraph/MyConduit', "
		     "'composite:EventGraph/MyComposite'. Or plain graph name like 'EventGraph'. "
		     "Must be used together with 'node' parameter."));
	Properties->SetObjectField(TEXT("graph"), GraphProp);

	TSharedPtr<FJsonObject> NodeProp = MakeShared<FJsonObject>();
	NodeProp->SetStringField(TEXT("type"), TEXT("string"));
	NodeProp->SetStringField(TEXT("description"),
		TEXT("Node GUID within the graph (from read_asset or edit_graph discovery output). "
		     "Must be used together with 'graph' parameter. The targeted node becomes the "
		     "working object for property operations. Use dot-notation for nested struct properties "
		     "(e.g. 'Node.bLoopAnimation' on anim graph nodes)."));
	Properties->SetObjectField(TEXT("node"), NodeProp);

	TSharedPtr<FJsonObject> ListPropsProp = MakeShared<FJsonObject>();
	ListPropsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ListPropsProp->SetStringField(TEXT("description"), TEXT("Set to true to list all editable properties. For graph+node targets, also includes input pins as pin.<PinName>. Combine with include_all to also show internal properties."));
	Properties->SetObjectField(TEXT("list_properties"), ListPropsProp);

	TSharedPtr<FJsonObject> IncludeAllProp = MakeShared<FJsonObject>();
	IncludeAllProp->SetStringField(TEXT("type"), TEXT("boolean"));
	IncludeAllProp->SetStringField(TEXT("description"), TEXT("When true, list_properties includes ALL serialized properties, not just EditAnywhere ones. Useful for internal properties like PlaybackRange, DisplayRate, TickResolution."));
	Properties->SetObjectField(TEXT("include_all"), IncludeAllProp);

	TSharedPtr<FJsonObject> GetProp = MakeShared<FJsonObject>();
	GetProp->SetStringField(TEXT("type"), TEXT("array"));
	GetProp->SetStringField(TEXT("description"), TEXT("Property names to read values from. For graph+node targeting, this also supports input pin reads using pin name (e.g. BlendTime) or pin.<PinName>."));
	TSharedPtr<FJsonObject> GetItems = MakeShared<FJsonObject>();
	GetItems->SetStringField(TEXT("type"), TEXT("string"));
	GetProp->SetObjectField(TEXT("items"), GetItems);
	Properties->SetObjectField(TEXT("get"), GetProp);

	TSharedPtr<FJsonObject> ChangesProp = MakeShared<FJsonObject>();
	ChangesProp->SetStringField(TEXT("type"), TEXT("array"));
	ChangesProp->SetStringField(TEXT("description"), TEXT("Properties to change: [{property, value}]. Values use UE format (BLEND_Translucent, True, (X=1,Y=2,Z=3)). For graph+node targeting, unresolved properties fall back to input pin defaults by pin name (or pin.<PinName>)."));
	Properties->SetObjectField(TEXT("changes"), ChangesProp);

	TSharedPtr<FJsonObject> MIParamsProp = MakeShared<FJsonObject>();
	MIParamsProp->SetStringField(TEXT("type"), TEXT("array"));
	MIParamsProp->SetStringField(TEXT("description"),
		TEXT("Material instance parameter edits: [{type:'scalar'|'vector'|'texture'|'static_switch', name:'Param', value:...}]. "
		     "scalar uses number, vector uses {r,g,b,a} or [r,g,b,a], texture uses asset path string, static_switch uses boolean."));
	Properties->SetObjectField(TEXT("material_instance_parameters"), MIParamsProp);

	TSharedPtr<FJsonObject> SlotProp = MakeShared<FJsonObject>();
	SlotProp->SetStringField(TEXT("type"), TEXT("object"));
	SlotProp->SetStringField(TEXT("description"), TEXT("Widget slot configuration: {position, size, alignment, anchors, z_order, auto_size, padding}"));
	Properties->SetObjectField(TEXT("slot"), SlotProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FConfigureAssetTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path, SubobjectName;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("subobject"), SubobjectName);

	FString GraphSelector, NodeGuid;
	Args->TryGetStringField(TEXT("graph"), GraphSelector);
	Args->TryGetStringField(TEXT("node"), NodeGuid);

	// Load asset
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Create transaction for undo/redo support
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "ConfigureAsset", "AI Configure Asset: {0}"),
		FText::FromString(Name)));

	// Track the original asset for editor refresh
	UObject* OriginalAsset = Asset;
	UObject* WorkingAsset = Asset;
	FString SubobjectContext;

	// Validate graph/node used together
	if (!GraphSelector.IsEmpty() && NodeGuid.IsEmpty())
	{
		return FToolResult::Fail(TEXT("'graph' parameter requires 'node' parameter (the GUID of the node to target)"));
	}
	if (GraphSelector.IsEmpty() && !NodeGuid.IsEmpty())
	{
		return FToolResult::Fail(TEXT("'node' parameter requires 'graph' parameter (which graph the node is in)"));
	}

	// Graph node targeting: find a specific node inside a Blueprint graph
	if (!GraphSelector.IsEmpty() && !NodeGuid.IsEmpty())
	{
		if (!SubobjectName.IsEmpty())
		{
			return FToolResult::Fail(TEXT("Cannot use 'subobject' together with 'graph'+'node'. Use one targeting method."));
		}

		FString GraphError;
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (!Blueprint)
		{
			return FToolResult::Fail(FString::Printf(TEXT("'graph'+'node' targeting requires a Blueprint asset, but '%s' is %s"),
				*Name, *Asset->GetClass()->GetName()));
		}

		UEdGraphNode* GraphNode = FindGraphNode(Blueprint, GraphSelector, NodeGuid, GraphError);
		if (!GraphNode)
		{
			return FToolResult::Fail(GraphError);
		}

		WorkingAsset = GraphNode;
		SubobjectContext = FString::Printf(TEXT(" (graph node: %s in %s)"), *GraphNode->GetNodeTitle(ENodeTitleType::ListView).ToString(), *GraphSelector);
	}

	// If subobject is specified, find it within the asset
	if (!SubobjectName.IsEmpty())
	{
		UObject* Subobject = FindSubobject(Asset, SubobjectName);
		if (!Subobject)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Subobject '%s' not found in %s"), *SubobjectName, *Name));
		}
		WorkingAsset = Subobject;
		SubobjectContext = FString::Printf(TEXT(" (subobject: %s)"), *SubobjectName);
	}

	// CRITICAL: When the Material Editor is open, it works on a PREVIEW COPY of the material.
	// We must configure the preview material for live changes, not the original.

	if (UMaterial* Material = Cast<UMaterial>(Asset))
	{
		if (GEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSubsystem)
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(Asset, false);
				if (EditorInstance && EditorInstance->GetEditorName() == TEXT("MaterialEditor"))
				{
					// Verified this is a MaterialEditor before static_cast
					IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
					if (MaterialEditor)
					{
						// GetMaterialInterface returns the PREVIEW material that the editor is working on
						UMaterialInterface* PreviewMaterial = MaterialEditor->GetMaterialInterface();
						if (UMaterial* PreviewMat = Cast<UMaterial>(PreviewMaterial))
						{
							WorkingAsset = PreviewMat;
							UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack ConfigureAsset: Using preview material from Material Editor"));
						}
					}
				}
			}
		}
	}

	// Parse parameters
	bool bListProperties = false;
	Args->TryGetBoolField(TEXT("list_properties"), bListProperties);
	bool bIncludeAll = false;
	Args->TryGetBoolField(TEXT("include_all"), bIncludeAll);

	TArray<FString> GetProperties;
	const TArray<TSharedPtr<FJsonValue>>* GetArray;
	if (Args->TryGetArrayField(TEXT("get"), GetArray))
	{
		for (const auto& Val : *GetArray)
		{
			FString PropName;
			if (Val->TryGetString(PropName))
			{
				GetProperties.Add(PropName);
			}
		}
	}

	TArray<FPropertyChange> Changes;
	const TArray<TSharedPtr<FJsonValue>>* ChangesArray;
	if (Args->TryGetArrayField(TEXT("changes"), ChangesArray))
	{
		FString ParseError;
		if (!ParseChanges(*ChangesArray, Changes, ParseError))
		{
			return FToolResult::Fail(ParseError);
		}
	}

	TArray<FMIParameterEdit> MIParameterEdits;
	const TArray<TSharedPtr<FJsonValue>>* MIParamsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("material_instance_parameters"), MIParamsArray))
	{
		FString ParseError;
		if (!ParseMaterialInstanceParameterEdits(*MIParamsArray, MIParameterEdits, ParseError))
		{
			return FToolResult::Fail(ParseError);
		}
	}

	// Parse slot configuration (for widgets in panels)
	const TSharedPtr<FJsonObject>* SlotConfig = nullptr;
	Args->TryGetObjectField(TEXT("slot"), SlotConfig);

	// Execute operations
	TArray<TPair<FString, FString>> GetResults;
	TArray<FString> GetErrors;
	TArray<FPropertyInfo> ListedProperties;
	TArray<FChangeResult> ChangeResults;
	FString SlotResult;

	// Get specific property values
	if (GetProperties.Num() > 0)
	{
		GetResults = GetPropertyValues(WorkingAsset, GetProperties, GetErrors);
	}

	// List all editable properties
	if (bListProperties)
	{
		ListedProperties = ListEditableProperties(WorkingAsset, bIncludeAll);
	}

	// Apply changes
	if (Changes.Num() > 0)
	{
		ChangeResults = ApplyChanges(WorkingAsset, Asset, Changes);
	}

	if (MIParameterEdits.Num() > 0)
	{
		UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(WorkingAsset);
		if (!MIC)
		{
			return FToolResult::Fail(TEXT("'material_instance_parameters' requires a MaterialInstanceConstant target"));
		}

		TArray<FChangeResult> MIResults = ApplyMaterialInstanceParameterEdits(MIC, MIParameterEdits);
		ChangeResults.Append(MIResults);
	}

	// Configure slot (for widgets)
	if (SlotConfig && *SlotConfig)
	{
		UWidget* Widget = Cast<UWidget>(WorkingAsset);
		if (!Widget)
		{
			return FToolResult::Fail(TEXT("'slot' parameter only valid for widgets. Use 'subobject' to target a widget first."));
		}
		SlotResult = ConfigureSlot(Widget, *SlotConfig, OriginalAsset);
	}

	// If nothing was requested, show help
	if (GetProperties.Num() == 0 && !bListProperties && Changes.Num() == 0 && MIParameterEdits.Num() == 0 && !SlotConfig)
	{
		return FToolResult::Fail(TEXT("No operation specified. Use 'get', 'list_properties', 'changes', 'material_instance_parameters', or 'slot'."));
	}

	// Format and return results
	FString Output = FormatResults(WorkingAsset->GetName(), GetAssetTypeName(WorkingAsset),
	                                GetResults, GetErrors, ListedProperties, ChangeResults);

	// Append slot configuration result
	if (!SlotResult.IsEmpty())
	{
		Output += TEXT("\n") + SlotResult;
	}

	return FToolResult::Ok(Output);
}

bool FConfigureAssetTool::ParseChanges(const TArray<TSharedPtr<FJsonValue>>& ChangesArray,
                                        TArray<FPropertyChange>& OutChanges, FString& OutError)
{
	for (const auto& ChangeVal : ChangesArray)
	{
		const TSharedPtr<FJsonObject>* ChangeObj;
		if (!ChangeVal->TryGetObject(ChangeObj))
		{
			OutError = TEXT("Each change must be an object with 'property' and 'value'");
			return false;
		}

		FPropertyChange Change;

		if (!(*ChangeObj)->TryGetStringField(TEXT("property"), Change.PropertyName) || Change.PropertyName.IsEmpty())
		{
			OutError = TEXT("Missing 'property' in change");
			return false;
		}

		// Value can be string, number, or boolean - convert all to string
		if ((*ChangeObj)->HasField(TEXT("value")))
		{
			TSharedPtr<FJsonValue> ValueField = (*ChangeObj)->TryGetField(TEXT("value"));
			if (ValueField.IsValid())
			{
				switch (ValueField->Type)
				{
				case EJson::String:
					Change.Value = ValueField->AsString();
					break;
				case EJson::Number:
					Change.Value = FString::SanitizeFloat(ValueField->AsNumber());
					break;
				case EJson::Boolean:
					Change.Value = ValueField->AsBool() ? TEXT("True") : TEXT("False");
					break;
				default:
					OutError = FString::Printf(TEXT("Invalid value type for property '%s'"), *Change.PropertyName);
					return false;
				}
			}
		}
		else
		{
			OutError = FString::Printf(TEXT("Missing 'value' for property '%s'"), *Change.PropertyName);
			return false;
		}

		OutChanges.Add(Change);
	}

	return true;
}

bool FConfigureAssetTool::ParseMaterialInstanceParameterEdits(const TArray<TSharedPtr<FJsonValue>>& EditArray,
	TArray<FMIParameterEdit>& OutEdits, FString& OutError)
{
	for (const TSharedPtr<FJsonValue>& EntryVal : EditArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj = nullptr;
		if (!EntryVal->TryGetObject(EntryObj))
		{
			OutError = TEXT("material_instance_parameters entries must be objects");
			return false;
		}

		FMIParameterEdit Edit;
		if (!(*EntryObj)->TryGetStringField(TEXT("type"), Edit.Type) || Edit.Type.IsEmpty())
		{
			OutError = TEXT("material_instance_parameters entry missing required 'type'");
			return false;
		}
		Edit.Type = Edit.Type.ToLower();

		FString ParamName;
		if (!(*EntryObj)->TryGetStringField(TEXT("name"), ParamName) || ParamName.IsEmpty())
		{
			OutError = TEXT("material_instance_parameters entry missing required 'name'");
			return false;
		}
		Edit.Name = FName(*ParamName);

		Edit.Value = (*EntryObj)->TryGetField(TEXT("value"));
		if (!Edit.Value.IsValid())
		{
			OutError = FString::Printf(TEXT("material_instance_parameters entry missing required 'value' for '%s'"), *ParamName);
			return false;
		}

		if (Edit.Type != TEXT("scalar") && Edit.Type != TEXT("vector") && Edit.Type != TEXT("texture") && Edit.Type != TEXT("static_switch"))
		{
			OutError = FString::Printf(TEXT("Unsupported material_instance_parameters type '%s'"), *Edit.Type);
			return false;
		}

		OutEdits.Add(Edit);
	}

	return true;
}

TArray<FConfigureAssetTool::FChangeResult> FConfigureAssetTool::ApplyMaterialInstanceParameterEdits(
	UMaterialInstanceConstant* MaterialInstance, const TArray<FMIParameterEdit>& Edits)
{
	TArray<FChangeResult> Results;
	if (!MaterialInstance)
	{
		return Results;
	}

	MaterialInstance->Modify();

	for (const FMIParameterEdit& Edit : Edits)
	{
		FChangeResult Result;
		Result.PropertyName = FString::Printf(TEXT("MI.%s.%s"), *Edit.Type, *Edit.Name.ToString());
		Result.bSuccess = false;

		if (Edit.Type == TEXT("scalar"))
		{
			double NumberValue = 0.0;
			if (!Edit.Value->TryGetNumber(NumberValue))
			{
				Result.Error = TEXT("scalar parameter value must be a number");
				Results.Add(Result);
				continue;
			}

			Result.OldValue = FString::SanitizeFloat(UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(MaterialInstance, Edit.Name));
			const bool bSet = UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(MaterialInstance, Edit.Name, static_cast<float>(NumberValue));
			if (!bSet)
			{
				Result.Error = TEXT("parameter was not found or could not be overridden");
				Results.Add(Result);
				continue;
			}

			Result.NewValue = FString::SanitizeFloat(UMaterialEditingLibrary::GetMaterialInstanceScalarParameterValue(MaterialInstance, Edit.Name));
			Result.bSuccess = true;
			Results.Add(Result);
			continue;
		}

		if (Edit.Type == TEXT("vector"))
		{
			FLinearColor NewColor = FLinearColor::Transparent;
			bool bParsed = false;

			if (Edit.Value->Type == EJson::Array)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
				if (Edit.Value->TryGetArray(Arr) && Arr && Arr->Num() >= 3)
				{
					double R = 0, G = 0, B = 0, A = 1;
					(*Arr)[0]->TryGetNumber(R);
					(*Arr)[1]->TryGetNumber(G);
					(*Arr)[2]->TryGetNumber(B);
					if (Arr->Num() > 3) (*Arr)[3]->TryGetNumber(A);
					NewColor = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
					bParsed = true;
				}
			}
			else if (Edit.Value->Type == EJson::Object)
			{
				const TSharedPtr<FJsonObject>* Obj = nullptr;
				if (Edit.Value->TryGetObject(Obj) && Obj)
				{
					double R = 0, G = 0, B = 0, A = 1;
					const bool bHasRGB = (*Obj)->TryGetNumberField(TEXT("r"), R) &&
						(*Obj)->TryGetNumberField(TEXT("g"), G) &&
						(*Obj)->TryGetNumberField(TEXT("b"), B);
					if (bHasRGB)
					{
						(*Obj)->TryGetNumberField(TEXT("a"), A);
						NewColor = FLinearColor(static_cast<float>(R), static_cast<float>(G), static_cast<float>(B), static_cast<float>(A));
						bParsed = true;
					}
				}
			}

			if (!bParsed)
			{
				Result.Error = TEXT("vector parameter value must be [r,g,b,a?] or {r,g,b,a?}");
				Results.Add(Result);
				continue;
			}

			const FLinearColor OldColor = UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(MaterialInstance, Edit.Name);
			Result.OldValue = OldColor.ToString();
			const bool bSet = UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(MaterialInstance, Edit.Name, NewColor);
			if (!bSet)
			{
				Result.Error = TEXT("parameter was not found or could not be overridden");
				Results.Add(Result);
				continue;
			}

			Result.NewValue = UMaterialEditingLibrary::GetMaterialInstanceVectorParameterValue(MaterialInstance, Edit.Name).ToString();
			Result.bSuccess = true;
			Results.Add(Result);
			continue;
		}

		if (Edit.Type == TEXT("texture"))
		{
			FString TexturePath;
			if (!Edit.Value->TryGetString(TexturePath) || TexturePath.IsEmpty())
			{
				Result.Error = TEXT("texture parameter value must be an asset path string");
				Results.Add(Result);
				continue;
			}

			UTexture* NewTexture = LoadObject<UTexture>(nullptr, *TexturePath);
			if (!NewTexture)
			{
				Result.Error = FString::Printf(TEXT("texture asset not found: %s"), *TexturePath);
				Results.Add(Result);
				continue;
			}

			UTexture* OldTexture = UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(MaterialInstance, Edit.Name);
			Result.OldValue = OldTexture ? OldTexture->GetPathName() : TEXT("None");
			const bool bSet = UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(MaterialInstance, Edit.Name, NewTexture);
			if (!bSet)
			{
				Result.Error = TEXT("parameter was not found or could not be overridden");
				Results.Add(Result);
				continue;
			}

			UTexture* AppliedTexture = UMaterialEditingLibrary::GetMaterialInstanceTextureParameterValue(MaterialInstance, Edit.Name);
			Result.NewValue = AppliedTexture ? AppliedTexture->GetPathName() : TEXT("None");
			Result.bSuccess = true;
			Results.Add(Result);
			continue;
		}

		if (Edit.Type == TEXT("static_switch"))
		{
			bool BoolValue = false;
			if (!Edit.Value->TryGetBool(BoolValue))
			{
				Result.Error = TEXT("static_switch parameter value must be boolean");
				Results.Add(Result);
				continue;
			}

			Result.OldValue = UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(MaterialInstance, Edit.Name) ? TEXT("True") : TEXT("False");
			bool bSet = false;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			bSet = UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MaterialInstance, Edit.Name, BoolValue, EMaterialParameterAssociation::GlobalParameter, false);
#else
			bSet = UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(MaterialInstance, Edit.Name, BoolValue, EMaterialParameterAssociation::GlobalParameter);
#endif
			if (!bSet)
			{
				Result.Error = TEXT("parameter was not found or could not be overridden");
				Results.Add(Result);
				continue;
			}

			Result.NewValue = UMaterialEditingLibrary::GetMaterialInstanceStaticSwitchParameterValue(MaterialInstance, Edit.Name) ? TEXT("True") : TEXT("False");
			Result.bSuccess = true;
			Results.Add(Result);
		}
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(MaterialInstance);
	MaterialInstance->MarkPackageDirty();
	return Results;
}

TArray<TPair<FString, FString>> FConfigureAssetTool::GetPropertyValues(UObject* Asset,
                                                                        const TArray<FString>& PropertyNames,
                                                                        TArray<FString>& OutErrors)
{
	TArray<TPair<FString, FString>> Results;
	UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Asset);

	for (const FString& PropName : PropertyNames)
	{
		const bool bForcePin = IsPinQueryPath(PropName);

		if (bForcePin && GraphNode)
		{
			if (UEdGraphPin* Pin = FindNodePinByQuery(GraphNode, PropName))
			{
				Results.Add(TPair<FString, FString>(PropName, GetReadablePinValueString(Pin)));
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("%s - Pin not found"), *PropName));
			}
			continue;
		}

		if (PropName.Contains(TEXT(".")))
		{
			// Dot-notation: resolve through struct chain
			FResolvedProperty Resolved = ResolvePropertyPath(Asset, PropName);
			if (!Resolved.Property)
			{
				if (GraphNode)
				{
					if (UEdGraphPin* Pin = FindNodePinByQuery(GraphNode, PropName))
					{
						Results.Add(TPair<FString, FString>(PropName, GetReadablePinValueString(Pin)));
						continue;
					}
					OutErrors.Add(FString::Printf(TEXT("%s - Property path/pin not found"), *PropName));
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("%s - Property path not found"), *PropName));
				}
				continue;
			}
			FString Value = NeoStackToolUtils::GetPropertyValueAsString(Resolved.ContainerPtr, Resolved.Property, Asset);
			Results.Add(TPair<FString, FString>(PropName, Value));
		}
		else
		{
			FProperty* Property = FindProperty(Asset, PropName);
			if (!Property)
			{
				if (GraphNode)
				{
					if (UEdGraphPin* Pin = FindNodePinByQuery(GraphNode, PropName))
					{
						Results.Add(TPair<FString, FString>(PropName, GetReadablePinValueString(Pin)));
						continue;
					}
					OutErrors.Add(FString::Printf(TEXT("%s - Property/pin not found"), *PropName));
				}
				else
				{
					OutErrors.Add(FString::Printf(TEXT("%s - Property not found"), *PropName));
				}
				continue;
			}
			FString Value = GetPropertyValue(Asset, Property);
			Results.Add(TPair<FString, FString>(Property->GetName(), Value));
		}
	}

	return Results;
}

TArray<FConfigureAssetTool::FPropertyInfo> FConfigureAssetTool::ListEditableProperties(UObject* Asset, bool bIncludeAll)
{
	TArray<FPropertyInfo> Properties;

	if (!Asset) return Properties;

	for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Skip deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		// By default only show editable properties (visible in editor).
		// With bIncludeAll, show all serialized (SaveGame/non-transient) properties.
		if (!bIncludeAll && !Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (bIncludeAll && Property->HasAnyPropertyFlags(CPF_Transient))
		{
			continue;
		}

		FPropertyInfo Info;
		Info.Name = Property->GetName();
		Info.Type = GetPropertyTypeName(Property);
		Info.CurrentValue = GetPropertyValue(Asset, Property);

		// Get category from metadata
		Info.Category = Property->GetMetaData(TEXT("Category"));
		if (Info.Category.IsEmpty())
		{
			Info.Category = TEXT("Default");
		}

		Properties.Add(Info);
	}

	// For anim graph nodes, also list properties from the embedded FAnimNode struct
	// These are accessible via dot-notation (e.g. "Node.bLoopAnimation")
	if (UAnimGraphNode_Base* AnimGraphNode = Cast<UAnimGraphNode_Base>(Asset))
	{
		FStructProperty* FNodeProp = AnimGraphNode->GetFNodeProperty();
		if (FNodeProp && FNodeProp->Struct)
		{
			void* NodeStructPtr = FNodeProp->ContainerPtrToValuePtr<void>(AnimGraphNode);
			FString StructPropName = FNodeProp->GetName(); // Usually "Node"

			for (TFieldIterator<FProperty> PropIt(FNodeProp->Struct); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;

				if (Property->HasAnyPropertyFlags(CPF_Deprecated))
				{
					continue;
				}
				if (!bIncludeAll && !Property->HasAnyPropertyFlags(CPF_Edit))
				{
					continue;
				}
				if (bIncludeAll && Property->HasAnyPropertyFlags(CPF_Transient))
				{
					continue;
				}

				FPropertyInfo Info;
				Info.Name = FString::Printf(TEXT("%s.%s"), *StructPropName, *Property->GetName());
				Info.Type = GetPropertyTypeName(Property);
				Info.CurrentValue = NeoStackToolUtils::GetPropertyValueAsString(NodeStructPtr, Property, AnimGraphNode);
				Info.Category = TEXT("Node Settings");

				Properties.Add(Info);
			}
		}
	}

	// For graph-node targets, include input pins so agents can discover/read/write
	// literal defaults (enum dropdowns, bool checkboxes, string literals, etc.)
	if (UEdGraphNode* GraphNode = Cast<UEdGraphNode>(Asset))
	{
		for (UEdGraphPin* Pin : GraphNode->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input)
			{
				continue;
			}
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				continue;
			}

			FPropertyInfo Info;
			Info.Name = FString::Printf(TEXT("pin.%s"), *Pin->PinName.ToString());
			Info.Type = GetPinTypeDisplayString(Pin);
			if (Pin->bHidden)
			{
				Info.Type += TEXT(" [hidden]");
			}
			Info.CurrentValue = GetReadablePinValueString(Pin);
			Info.Category = TEXT("Pins");
			Properties.Add(Info);
		}
	}

	// Sort by category then name
	Properties.Sort([](const FPropertyInfo& A, const FPropertyInfo& B)
	{
		if (A.Category != B.Category)
		{
			return A.Category < B.Category;
		}
		return A.Name < B.Name;
	});

	return Properties;
}

TArray<FConfigureAssetTool::FChangeResult> FConfigureAssetTool::ApplyChanges(UObject* WorkingAsset,
                                                                              UObject* OriginalAsset,
                                                                              const TArray<FPropertyChange>& Changes)
{
	TArray<FChangeResult> Results;
	UEdGraphNode* GraphNode = Cast<UEdGraphNode>(WorkingAsset);

	// Mark object for transaction (undo/redo support)
	WorkingAsset->Modify();

	// Track which properties changed for Materials
	TArray<FProperty*> ChangedProperties;

	for (const FPropertyChange& Change : Changes)
	{
		FChangeResult Result;
		Result.PropertyName = Change.PropertyName;
		Result.bSuccess = false;
		const bool bForcePin = IsPinQueryPath(Change.PropertyName);

		// Use dot-notation resolution for paths containing dots, flat lookup otherwise
		FProperty* Property = nullptr;
		void* ContainerPtr = WorkingAsset;
		UEdGraphPin* TargetPin = nullptr;

		if (bForcePin && GraphNode)
		{
			TargetPin = FindNodePinByQuery(GraphNode, Change.PropertyName);
			if (!TargetPin)
			{
				Result.Error = TEXT("Pin not found");
				Results.Add(Result);
				continue;
			}
		}

		if (!TargetPin && Change.PropertyName.Contains(TEXT(".")))
		{
			FResolvedProperty Resolved = ResolvePropertyPath(WorkingAsset, Change.PropertyName);
			if (!Resolved.Property)
			{
				if (GraphNode)
				{
					TargetPin = FindNodePinByQuery(GraphNode, Change.PropertyName);
					if (!TargetPin)
					{
						Result.Error = FString::Printf(TEXT("Property path/pin not found: '%s'. Use list_properties to see valid property and pin names."), *Change.PropertyName);
						Results.Add(Result);
						continue;
					}
				}
				else
				{
					Result.Error = FString::Printf(TEXT("Property path not found: '%s'. Check that each segment exists (use list_properties)."), *Change.PropertyName);
					Results.Add(Result);
					continue;
				}
			}
			else
			{
				Property = Resolved.Property;
				ContainerPtr = Resolved.ContainerPtr;
			}
		}
		else if (!TargetPin)
		{
			Property = FindProperty(WorkingAsset, Change.PropertyName);
			if (!Property)
			{
				if (GraphNode)
				{
					TargetPin = FindNodePinByQuery(GraphNode, Change.PropertyName);
					if (!TargetPin)
					{
						Result.Error = TEXT("Property/pin not found");
						Results.Add(Result);
						continue;
					}
				}
				else
				{
					Result.Error = TEXT("Property not found");
					Results.Add(Result);
					continue;
				}
			}
		}

		if (TargetPin)
		{
			Result.OldValue = GetReadablePinValueString(TargetPin);

			FString PinSetError;
			if (!SetNodePinDefaultValue(GraphNode, TargetPin, Change.Value, PinSetError))
			{
				Result.Error = PinSetError;
				Results.Add(Result);
				continue;
			}

			GraphNode->MarkPackageDirty();
			Result.NewValue = GetReadablePinValueString(TargetPin);
			Result.bSuccess = true;
			Results.Add(Result);
			continue;
		}

		// Check if property is writable using engine permission validation
		EPropertyAccessResultFlags AccessResult = PropertyAccessUtil::CanSetPropertyValue(
			Property, PropertyAccessUtil::EditorReadOnlyFlags,
			PropertyAccessUtil::IsObjectTemplate(WorkingAsset));
		if (EnumHasAnyFlags(AccessResult, EPropertyAccessResultFlags::PermissionDenied))
		{
			Result.Error = FString::Printf(TEXT("Property '%s' is read-only (EditConst)"), *Change.PropertyName);
			Results.Add(Result);
			continue;
		}

		// Get old value
		Result.OldValue = NeoStackToolUtils::GetPropertyValueAsString(ContainerPtr, Property, WorkingAsset);

		// Notify pre-change with the actual property (critical for Materials!)
		WorkingAsset->PreEditChange(Property);

		// Set new value using the resolved container
		FString SetError;
		// For dot-notation paths, use ImportText directly on the resolved container
		if (ContainerPtr != static_cast<void*>(WorkingAsset))
		{
			// Nested struct property — use ImportText_InContainer with the struct container
			const TCHAR* ImportResult = Property->ImportText_InContainer(*Change.Value, ContainerPtr, WorkingAsset, PPF_None);
			if (!ImportResult)
			{
				// Try boolean transformations
				FString TransformedValue = Change.Value;
				if (Change.Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
					TransformedValue = TEXT("True");
				else if (Change.Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
					TransformedValue = TEXT("False");

				ImportResult = Property->ImportText_InContainer(*TransformedValue, ContainerPtr, WorkingAsset, PPF_None);
				if (!ImportResult)
				{
					Result.Error = FString::Printf(TEXT("Failed to set value '%s'. Use list_properties to see valid format."), *Change.Value);
					Results.Add(Result);
					continue;
				}
			}
		}
		else
		{
			if (!SetPropertyValue(WorkingAsset, Property, Change.Value, SetError))
			{
				Result.Error = SetError;
				Results.Add(Result);
				continue;
			}
		}

		// Mark package dirty
		WorkingAsset->MarkPackageDirty();

		// Create proper PropertyChangedEvent and notify post-change
		FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
		WorkingAsset->PostEditChangeProperty(PropertyEvent);

		// Track for Materials
		ChangedProperties.Add(Property);

		// Get new value for confirmation
		Result.NewValue = NeoStackToolUtils::GetPropertyValueAsString(ContainerPtr, Property, WorkingAsset);
		Result.bSuccess = true;

		Results.Add(Result);
	}

	// Handle asset-specific post-edit actions
	if (UMaterial* Material = Cast<UMaterial>(WorkingAsset))
	{
		// Force material recompilation for visual changes
		Material->ForceRecompileForRendering();

		// If the Material Editor is open, mark it as dirty so changes appear live
		if (GEditor)
		{
			UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
			if (AssetEditorSubsystem)
			{
				IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(OriginalAsset, false);
				if (EditorInstance && EditorInstance->GetEditorName() == TEXT("MaterialEditor"))
				{
					IMaterialEditor* MaterialEditor = static_cast<IMaterialEditor*>(EditorInstance);
					if (MaterialEditor)
					{
						MaterialEditor->MarkMaterialDirty();
						UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack ConfigureAsset: Marked Material Editor as dirty"));
					}
				}
			}
		}
	}
	else if (UBlueprint* Blueprint = Cast<UBlueprint>(WorkingAsset))
	{
		// Recompile blueprint when directly editing it
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}
	else if (Cast<UWidget>(WorkingAsset) || Cast<UActorComponent>(WorkingAsset))
	{
		// When editing a subobject (widget or component), refresh the parent blueprint
		RefreshBlueprintEditor(OriginalAsset);
	}
	else if (OriginalAsset != WorkingAsset && Cast<UBlueprint>(OriginalAsset))
	{
		// CDO or other Blueprint subobject — mark Blueprint as modified to trigger recompile
		// This propagates CDO changes (bReplicates, etc.) to existing instances
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Cast<UBlueprint>(OriginalAsset));
	}
	else if (UBTNode* BTNode = Cast<UBTNode>(WorkingAsset))
	{
		// After setting properties on a BT node, call InitializeFromAsset to resolve
		// FBlackboardKeySelector fields (SelectedKeyName -> SelectedKeyID + SelectedKeyType).
		// This is the engine pattern used by BTDecorator_BlackboardBase, BTService_BlackboardBase, etc.
		UBehaviorTree* OwnerTree = BTNode->GetTypedOuter<UBehaviorTree>();
		if (OwnerTree)
		{
			BTNode->InitializeFromAsset(*OwnerTree);
			OwnerTree->MarkPackageDirty();
		}
	}
	else if (UEdGraphNode* WorkingGraphNode = Cast<UEdGraphNode>(WorkingAsset))
	{
		// Graph node was targeted via graph+node params — reconstruct and recompile
		HandleGraphNodePostEdit(WorkingGraphNode, Cast<UBlueprint>(OriginalAsset));
	}

	// Animation asset post-edit hooks (work on the original asset, not graph nodes/subobjects)
	if (UAnimMontage* Montage = Cast<UAnimMontage>(OriginalAsset))
	{
		// Rebuild branching point markers and propagate changes to child montages
		Montage->RefreshCacheData();
	}
	else if (UBlendSpace* BlendSpace = Cast<UBlendSpace>(OriginalAsset))
	{
		// Revalidate samples and resample triangulation after property changes
		BlendSpace->ValidateSampleData();
		BlendSpace->ResampleData();
	}

	return Results;
}

FProperty* FConfigureAssetTool::FindProperty(UObject* Asset, const FString& PropertyName)
{
	if (!Asset) return nullptr;

	// Use engine utility with redirector support (handles renamed properties)
	FProperty* Property = PropertyAccessUtil::FindPropertyByName(FName(*PropertyName), Asset->GetClass());
	if (Property)
	{
		return Property;
	}

	// Fallback: case-insensitive search for agent-friendly matching
	for (TFieldIterator<FProperty> PropIt(Asset->GetClass()); PropIt; ++PropIt)
	{
		if (PropIt->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
		{
			return *PropIt;
		}
	}

	return nullptr;
}

FString FConfigureAssetTool::GetPropertyValue(UObject* Asset, FProperty* Property)
{
	if (!Asset || !Property) return TEXT("");
	return NeoStackToolUtils::GetPropertyValueAsString(Asset, Property, Asset);
}

bool FConfigureAssetTool::SetPropertyValue(UObject* Asset, FProperty* Property,
                                            const FString& Value, FString& OutError)
{
	if (!Asset || !Property)
	{
		OutError = TEXT("Invalid asset or property");
		return false;
	}

	void* ContainerPtr = Asset;

	// Special handling for FBlackboardKeySelector: accept a plain key name string
	// so agents can write {"property": "BlackboardKey", "value": "TargetActor"}
	// instead of the ugly struct format (SelectedKeyName="TargetActor")
	if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		if (StructProp->Struct && StructProp->Struct->GetName() == TEXT("BlackboardKeySelector"))
		{
			// If value doesn't look like a struct literal, treat it as SelectedKeyName
			if (!Value.StartsWith(TEXT("(")))
			{
				FBlackboardKeySelector* KeySelector = StructProp->ContainerPtrToValuePtr<FBlackboardKeySelector>(ContainerPtr);
				if (KeySelector)
				{
					KeySelector->SelectedKeyName = FName(*Value);
					return true;
				}
			}
		}
	}

	// ImportText returns the pointer past the parsed text, or nullptr on failure
	const TCHAR* Result = Property->ImportText_InContainer(*Value, ContainerPtr, Asset, PPF_None);

	if (!Result)
	{
		// Try some common transformations for user-friendly input
		FString TransformedValue = Value;

		// Handle booleans
		if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase))
		{
			TransformedValue = TEXT("True");
		}
		else if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase))
		{
			TransformedValue = TEXT("False");
		}

		// Try again with transformed value
		Result = Property->ImportText_InContainer(*TransformedValue, ContainerPtr, Asset, PPF_None);

		if (!Result)
		{
			OutError = FString::Printf(TEXT("Failed to set value '%s'. Use list_properties to see valid format."), *Value);
			return false;
		}
	}

	return true;
}

FString FConfigureAssetTool::GetPropertyTypeName(FProperty* Property) const
{
	return NeoStackToolUtils::GetPropertyTypeName(Property);
}

FString FConfigureAssetTool::GetAssetTypeName(UObject* Asset) const
{
	if (!Asset) return TEXT("Unknown");

	if (Cast<UAnimBlueprint>(Asset)) return TEXT("AnimBlueprint");
	if (Cast<UBlueprint>(Asset)) return TEXT("Blueprint");
	if (Cast<UMaterialFunction>(Asset)) return TEXT("MaterialFunction");
	if (Cast<UMaterial>(Asset)) return TEXT("Material");

	return Asset->GetClass()->GetName();
}

FString FConfigureAssetTool::FormatResults(const FString& AssetName, const FString& AssetType,
                                            const TArray<TPair<FString, FString>>& GetResults,
                                            const TArray<FString>& GetErrors,
                                            const TArray<FPropertyInfo>& ListedProperties,
                                            const TArray<FChangeResult>& ChangeResults) const
{
	FString Output = FString::Printf(TEXT("# CONFIGURE ASSET: %s\nType: %s\n"), *AssetName, *AssetType);

	// Get results
	if (GetResults.Num() > 0 || GetErrors.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Property Values (%d)\n"), GetResults.Num());

		for (const auto& Result : GetResults)
		{
			Output += FString::Printf(TEXT("  %s = %s\n"), *Result.Key, *Result.Value);
		}

		for (const FString& Error : GetErrors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *Error);
		}
	}

	// Listed properties
	if (ListedProperties.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## Editable Properties (%d)\n"), ListedProperties.Num());

		FString CurrentCategory;
		for (const FPropertyInfo& Info : ListedProperties)
		{
			if (Info.Category != CurrentCategory)
			{
				CurrentCategory = Info.Category;
				Output += FString::Printf(TEXT("\n### %s\n"), *CurrentCategory);
			}

			// Truncate long values
			FString DisplayValue = Info.CurrentValue;
			if (DisplayValue.Len() > 50)
			{
				DisplayValue = DisplayValue.Left(47) + TEXT("...");
			}

			Output += FString::Printf(TEXT("  %s (%s) = %s\n"), *Info.Name, *Info.Type, *DisplayValue);
		}
	}

	// Change results
	if (ChangeResults.Num() > 0)
	{
		int32 SuccessCount = 0;
		int32 ErrorCount = 0;

		for (const FChangeResult& Result : ChangeResults)
		{
			if (Result.bSuccess) SuccessCount++;
			else ErrorCount++;
		}

		Output += FString::Printf(TEXT("\n## Changes Applied (%d)\n"), SuccessCount);

		for (const FChangeResult& Result : ChangeResults)
		{
			if (Result.bSuccess)
			{
				Output += FString::Printf(TEXT("+ %s: %s -> %s\n"),
				    *Result.PropertyName, *Result.OldValue, *Result.NewValue);
			}
		}

		if (ErrorCount > 0)
		{
			Output += FString::Printf(TEXT("\n## Errors (%d)\n"), ErrorCount);

			for (const FChangeResult& Result : ChangeResults)
			{
				if (!Result.bSuccess)
				{
					Output += FString::Printf(TEXT("! %s - %s\n"), *Result.PropertyName, *Result.Error);
				}
			}
		}
	}

	// Summary line
	int32 TotalOps = GetResults.Num() + (ListedProperties.Num() > 0 ? 1 : 0);
	int32 TotalChanges = 0;
	int32 TotalErrors = GetErrors.Num();

	for (const FChangeResult& Result : ChangeResults)
	{
		if (Result.bSuccess) TotalChanges++;
		else TotalErrors++;
	}

	if (TotalChanges > 0 || TotalErrors > 0)
	{
		Output += FString::Printf(TEXT("\n= %d properties changed, %d errors\n"), TotalChanges, TotalErrors);
	}

	return Output;
}

UObject* FConfigureAssetTool::FindSubobject(UObject* Asset, const FString& SubobjectName)
{
	if (!Asset || SubobjectName.IsEmpty())
	{
		return nullptr;
	}

	FName SubobjectFName(*SubobjectName);

	// Widget Blueprint: find widget in WidgetTree
	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset))
	{
		if (WidgetBP->WidgetTree)
		{
			return WidgetBP->WidgetTree->FindWidget(SubobjectFName);
		}
		return nullptr;
	}

	// Regular Blueprint: class_defaults for CDO, otherwise find component in SCS
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		// "class_defaults" resolves to the Actor CDO (bReplicates, NetUpdateFrequency, etc.)
		if (SubobjectName.Equals(TEXT("class_defaults"), ESearchCase::IgnoreCase))
		{
			if (Blueprint->GeneratedClass)
			{
				return Blueprint->GeneratedClass->GetDefaultObject();
			}
			// Fall back to skeleton if not yet compiled
			if (Blueprint->SkeletonGeneratedClass)
			{
				return Blueprint->SkeletonGeneratedClass->GetDefaultObject();
			}
			return nullptr;
		}

		if (Blueprint->SimpleConstructionScript)
		{
			USCS_Node* Node = Blueprint->SimpleConstructionScript->FindSCSNode(SubobjectFName);
			if (Node && Node->ComponentTemplate)
			{
				return Node->ComponentTemplate;
			}
		}
		return nullptr;
	}

	// BehaviorTree: find node (composite, task, decorator, service) by NodeName
	if (UBehaviorTree* BehaviorTree = Cast<UBehaviorTree>(Asset))
	{
		return FindBTNodeByName(BehaviorTree, SubobjectName);
	}

	// For other asset types, try to find a subobject by name using UObject's subobject system
	return FindObject<UObject>(Asset, *SubobjectName);
}

// Static recursive helper to walk composite node children
static UBTNode* FindBTNodeRecursive(UBTCompositeNode* Node, const FString& NodeName)
{
	if (!Node) return nullptr;

	// Check this composite node
	if (Node->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
	{
		return Node;
	}

	// Check services on this composite
	for (UBTService* Service : Node->Services)
	{
		if (Service && Service->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
		{
			return Service;
		}
	}

	// Walk children
	for (int32 i = 0; i < Node->GetChildrenNum(); i++)
	{
		FBTCompositeChild& Child = Node->Children[i];

		// Check decorators on the child edge
		for (UBTDecorator* Decorator : Child.Decorators)
		{
			if (Decorator && Decorator->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
			{
				return Decorator;
			}
		}

		// Check child composite (recurse)
		if (Child.ChildComposite)
		{
			UBTNode* Found = FindBTNodeRecursive(Child.ChildComposite, NodeName);
			if (Found) return Found;
		}

		// Check child task
		if (Child.ChildTask)
		{
			if (Child.ChildTask->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
			{
				return Child.ChildTask;
			}

			// Check services on the task
			for (UBTService* Service : Child.ChildTask->Services)
			{
				if (Service && Service->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
				{
					return Service;
				}
			}
		}
	}

	return nullptr;
}

UObject* FConfigureAssetTool::FindBTNodeByName(UBehaviorTree* BehaviorTree, const FString& NodeName)
{
	if (!BehaviorTree || NodeName.IsEmpty()) return nullptr;

	// Check root-level decorators (used in subtrees)
	for (UBTDecorator* Decorator : BehaviorTree->RootDecorators)
	{
		if (Decorator && Decorator->GetNodeName().Equals(NodeName, ESearchCase::IgnoreCase))
		{
			return Decorator;
		}
	}

	// Walk the tree from the root composite
	return FindBTNodeRecursive(BehaviorTree->RootNode, NodeName);
}

void FConfigureAssetTool::RefreshBlueprintEditor(UObject* Asset)
{
	if (!Asset || !GEditor)
	{
		return;
	}

	UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
	if (!AssetEditorSubsystem)
	{
		return;
	}

	// Widget Blueprint: refresh the widget designer
	if (UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset))
	{
		IAssetEditorInstance* EditorInstance = AssetEditorSubsystem->FindEditorForAsset(WidgetBP, false);
		if (EditorInstance && EditorInstance->GetEditorName() == TEXT("WidgetBlueprintEditor"))
		{
			QueueWidgetPreviewRefresh(WidgetBP);
		}
		return;
	}

	// Regular Blueprint: mark as modified to trigger recompile
	if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		return;
	}
}

// ============================================================================
// Dot-Notation Property Resolution
// ============================================================================

FConfigureAssetTool::FResolvedProperty FConfigureAssetTool::ResolvePropertyPath(UObject* Object, const FString& PropertyPath)
{
	FResolvedProperty Result;
	if (!Object || PropertyPath.IsEmpty())
	{
		return Result;
	}

	// If no dots, just do a flat lookup (backward compatible)
	if (!PropertyPath.Contains(TEXT(".")))
	{
		Result.Property = FindProperty(Object, PropertyPath);
		Result.ContainerPtr = Object;
		Result.OwnerObject = Object;
		return Result;
	}

	// Split by dots and walk the property chain
	TArray<FString> Segments;
	PropertyPath.ParseIntoArray(Segments, TEXT("."), true);

	if (Segments.Num() == 0 || Segments.Num() > 10) // Depth cap for safety
	{
		return Result;
	}

	// Start from the UObject's class
	UStruct* CurrentStruct = Object->GetClass();
	void* CurrentContainer = Object;
	UObject* Owner = Object;

	for (int32 i = 0; i < Segments.Num(); i++)
	{
		FString Segment = Segments[i];
		bool bIsLast = (i == Segments.Num() - 1);

		// Parse optional array index: "PropertyName[N]" → PropertyName + index N
		int32 ArrayIndex = INDEX_NONE;
		int32 BracketPos = INDEX_NONE;
		if (Segment.FindChar(TEXT('['), BracketPos))
		{
			FString IndexStr = Segment.Mid(BracketPos + 1);
			IndexStr.RemoveFromEnd(TEXT("]"));
			if (IndexStr.IsNumeric())
			{
				ArrayIndex = FCString::Atoi(*IndexStr);
			}
			else
			{
				return FResolvedProperty(); // Malformed index
			}
			Segment = Segment.Left(BracketPos);
		}

		// Find property in current struct
		FProperty* Prop = nullptr;

		// Try exact name first via engine utility
		Prop = PropertyAccessUtil::FindPropertyByName(FName(*Segment), CurrentStruct);

		// Fallback: case-insensitive search
		if (!Prop)
		{
			for (TFieldIterator<FProperty> PropIt(CurrentStruct); PropIt; ++PropIt)
			{
				if (PropIt->GetName().Equals(Segment, ESearchCase::IgnoreCase))
				{
					Prop = *PropIt;
					break;
				}
			}
		}

		if (!Prop)
		{
			return FResolvedProperty(); // Segment not found
		}

		// Handle array indexing: resolve to the element at [N]
		if (ArrayIndex != INDEX_NONE)
		{
			void* PropContainer = Prop->ContainerPtrToValuePtr<void>(CurrentContainer);

			// TArray<> — FArrayProperty
			if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
			{
				FScriptArrayHelper ArrayHelper(ArrayProp, PropContainer);
				if (ArrayIndex < 0 || ArrayIndex >= ArrayHelper.Num())
				{
					return FResolvedProperty(); // Out of bounds
				}

				void* ElementPtr = ArrayHelper.GetRawPtr(ArrayIndex);
				FProperty* InnerProp = ArrayProp->Inner;

				if (bIsLast)
				{
					// Targeting the element itself — return inner property at element ptr
					// For struct arrays the caller can set the whole struct via ImportText
					Result.Property = InnerProp;
					Result.ContainerPtr = ElementPtr;
					Result.OwnerObject = Owner;
					return Result;
				}

				// Walk into the element (must be a struct)
				FStructProperty* InnerStruct = CastField<FStructProperty>(InnerProp);
				if (!InnerStruct)
				{
					return FResolvedProperty(); // Can't walk into non-struct element
				}
				CurrentContainer = ElementPtr;
				CurrentStruct = InnerStruct->Struct;
				continue;
			}

			// Fixed-size C array (e.g., FInterpolationParameter InterpolationParam[3])
			if (Prop->ArrayDim > 1)
			{
				if (ArrayIndex < 0 || ArrayIndex >= Prop->ArrayDim)
				{
					return FResolvedProperty(); // Out of bounds
				}

				// Offset into the fixed array by element size
				void* ElementPtr = static_cast<uint8*>(PropContainer) + (ArrayIndex * Prop->GetElementSize());

				// Walk into the element struct (must not be last — use sub-properties to target fields)
				FStructProperty* StructProp = CastField<FStructProperty>(Prop);
				if (!StructProp || bIsLast)
				{
					return FResolvedProperty(); // Fixed array elements must be struct and must have sub-property
				}
				CurrentContainer = ElementPtr;
				CurrentStruct = StructProp->Struct;
				continue;
			}

			// Property doesn't support indexing
			return FResolvedProperty();
		}

		if (bIsLast)
		{
			// This is the target property
			Result.Property = Prop;
			Result.ContainerPtr = CurrentContainer;
			Result.OwnerObject = Owner;
			return Result;
		}

		// Not the last segment — must be a struct property to walk into
		FStructProperty* StructProp = CastField<FStructProperty>(Prop);
		if (!StructProp)
		{
			return FResolvedProperty(); // Can't walk into non-struct
		}

		// Advance into the struct
		CurrentContainer = StructProp->ContainerPtrToValuePtr<void>(CurrentContainer);
		CurrentStruct = StructProp->Struct;
	}

	return FResolvedProperty(); // Should not reach here
}

// ============================================================================
// Graph Node Targeting
// ============================================================================

UEdGraph* FConfigureAssetTool::ResolveSubgraph(UBlueprint* Blueprint, const FString& GraphSelector)
{
	if (!Blueprint || GraphSelector.IsEmpty())
	{
		return nullptr;
	}

	// Helper to safely get state name from transition
	auto SafeGetTransitionStateName = [](UAnimStateTransitionNode* TransitionNode, bool bPrev) -> FString
	{
		if (!TransitionNode || TransitionNode->Pins.Num() < 2)
			return TEXT("?");
		UAnimStateNodeBase* StateNode = bPrev
			? TransitionNode->GetPreviousState()
			: TransitionNode->GetNextState();
		if (!StateNode)
			return TEXT("?");
		return StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
	};

	// Build a map of all graphs with their typed selector names
	TArray<TPair<UEdGraph*, FString>> GraphMap;
	TSet<UEdGraph*> Visited;

	// Recursive lambda to collect subgraphs from anim/BP nodes
	auto CollectChildGraphs = [&](UEdGraph* GraphToSearch, const FString& RootName, auto&& CollectRef) -> void
	{
		if (!GraphToSearch || Visited.Contains(GraphToSearch))
			return;
		Visited.Add(GraphToSearch);

		for (UEdGraphNode* CurrentNode : GraphToSearch->Nodes)
		{
			if (!CurrentNode) continue;

			if (UAnimGraphNode_StateMachineBase* StateMachine = Cast<UAnimGraphNode_StateMachineBase>(CurrentNode))
			{
				if (StateMachine->EditorStateMachineGraph)
				{
					FString SMName = StateMachine->GetNodeTitle(ENodeTitleType::ListView).ToString();
					FString SMSelector = FString::Printf(TEXT("statemachine:%s/%s"), *RootName, *SMName);
					UEdGraph* SMGraph = StateMachine->EditorStateMachineGraph;
					GraphMap.Add(TPair<UEdGraph*, FString>(SMGraph, SMSelector));
					// Pass RootName (not RootName/SMName) to match EditGraphTool convention
					CollectRef(SMGraph, RootName, CollectRef);
				}
			}
			else if (UAnimStateTransitionNode* TransitionNode = Cast<UAnimStateTransitionNode>(CurrentNode))
			{
				FString FromState = SafeGetTransitionStateName(TransitionNode, true);
				FString ToState = SafeGetTransitionStateName(TransitionNode, false);

				UEdGraph* BoundGraph = TransitionNode->GetBoundGraph();
				if (BoundGraph)
				{
					FString TransSelector = FString::Printf(TEXT("transition:%s/%s->%s"), *RootName, *FromState, *ToState);
					GraphMap.Add(TPair<UEdGraph*, FString>(BoundGraph, TransSelector));
				}
				if (TransitionNode->CustomTransitionGraph)
				{
					FString CustomSelector = FString::Printf(TEXT("custom_transition:%s/%s->%s"), *RootName, *FromState, *ToState);
					GraphMap.Add(TPair<UEdGraph*, FString>(TransitionNode->CustomTransitionGraph, CustomSelector));
				}
			}
			else if (UAnimStateNodeBase* StateNode = Cast<UAnimStateNodeBase>(CurrentNode))
			{
				UEdGraph* BoundGraph = StateNode->GetBoundGraph();
				if (BoundGraph)
				{
					FString NodeName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
					FString TypePrefix = StateNode->IsA<UAnimStateConduitNode>() ? TEXT("conduit") : TEXT("state");
					FString StateSelector = FString::Printf(TEXT("%s:%s/%s"), *TypePrefix, *RootName, *NodeName);
					GraphMap.Add(TPair<UEdGraph*, FString>(BoundGraph, StateSelector));
					CollectRef(BoundGraph, RootName + TEXT("/") + NodeName, CollectRef);
				}
			}
			else if (UK2Node_Composite* CompositeNode = Cast<UK2Node_Composite>(CurrentNode))
			{
				if (CompositeNode->BoundGraph)
				{
					FString CompName = CompositeNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
					FString CompSelector = FString::Printf(TEXT("composite:%s/%s"), *RootName, *CompName);
					GraphMap.Add(TPair<UEdGraph*, FString>(CompositeNode->BoundGraph, CompSelector));
				}
			}
		}
	};

	// Collect top-level graphs
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (!Graph) continue;
		FString GraphName = Graph->GetName();
		GraphMap.Add(TPair<UEdGraph*, FString>(Graph, GraphName));
		CollectChildGraphs(Graph, GraphName, CollectChildGraphs);
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (!Graph) continue;
		FString GraphName = Graph->GetName();
		GraphMap.Add(TPair<UEdGraph*, FString>(Graph, GraphName));
		// AnimGraph lives in FunctionGraphs — add animgraph: prefix for typed selector support
		if (Cast<UAnimBlueprint>(Blueprint))
		{
			GraphMap.Add(TPair<UEdGraph*, FString>(Graph, TEXT("animgraph:") + GraphName));
		}
		CollectChildGraphs(Graph, GraphName, CollectChildGraphs);
	}

	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (!Graph) continue;
		GraphMap.Add(TPair<UEdGraph*, FString>(Graph, Graph->GetName()));
	}

	// Search the map for a case-insensitive match
	for (const auto& Pair : GraphMap)
	{
		if (Pair.Value.Equals(GraphSelector, ESearchCase::IgnoreCase))
		{
			return Pair.Key;
		}
	}

	// Fallback: use NeoStackToolUtils for plain names
	return NeoStackToolUtils::FindGraphByName(Blueprint, GraphSelector);
}

UEdGraphNode* FConfigureAssetTool::FindGraphNode(UBlueprint* Blueprint, const FString& GraphSelector,
	const FString& NodeGuidStr, FString& OutError)
{
	if (!Blueprint)
	{
		OutError = TEXT("Blueprint is null");
		return nullptr;
	}

	UEdGraph* Graph = ResolveSubgraph(Blueprint, GraphSelector);
	if (!Graph)
	{
		OutError = FString::Printf(TEXT("Graph not found: '%s'. Use read_asset to list available graphs."), *GraphSelector);
		return nullptr;
	}

	UEdGraphNode* Node = NeoStackToolUtils::FindNodeByGuid(Graph, NodeGuidStr);
	if (!Node)
	{
		OutError = FString::Printf(TEXT("Node with GUID '%s' not found in graph '%s'. Use read_asset to list nodes with their GUIDs."),
			*NodeGuidStr, *GraphSelector);
		return nullptr;
	}

	return Node;
}

void FConfigureAssetTool::HandleGraphNodePostEdit(UEdGraphNode* Node, UBlueprint* OwningBlueprint)
{
	if (!Node) return;

	// Reconstruct node to refresh pins (matches editor Details panel behavior)
	UEdGraph* Graph = Node->GetGraph();
	if (Graph)
	{
		const UEdGraphSchema* Schema = Graph->GetSchema();
		if (Schema)
		{
			Schema->ReconstructNode(*Node);
		}
	}

	// Mark owning BP modified to trigger recompile
	UBlueprint* BP = OwningBlueprint;
	if (!BP)
	{
		BP = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	}
	if (BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
	}
}

FString FConfigureAssetTool::ConfigureSlot(UWidget* Widget, const TSharedPtr<FJsonObject>& SlotConfig, UObject* OriginalAsset)
{
	if (!Widget || !SlotConfig.IsValid())
	{
		return TEXT("! Invalid widget or slot config");
	}

	if (IsTransientWorldObject(Widget))
	{
		return TEXT("! Widget belongs to a transient world instance; slot edits must target the Widget Blueprint asset widget tree");
	}

	UPanelSlot* Slot = Widget->Slot;
	if (!Slot)
	{
		return TEXT("! Widget has no slot (not in a panel)");
	}

	// Mark widget for undo (Widget::Modify also marks Slot for undo)
	Widget->Modify();

	FString Result = TEXT("## Slot Configuration\n");
	int32 ChangesApplied = 0;

	// Handle CanvasPanelSlot
	if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(Slot))
	{
		// Position
		const TArray<TSharedPtr<FJsonValue>>* PositionArray;
		if (SlotConfig->TryGetArrayField(TEXT("position"), PositionArray) && PositionArray->Num() >= 2)
		{
			FVector2D OldPos = CanvasSlot->GetPosition();
			FVector2D NewPos((*PositionArray)[0]->AsNumber(), (*PositionArray)[1]->AsNumber());
			CanvasSlot->SetPosition(NewPos);
			Result += FString::Printf(TEXT("+ Position: (%.1f, %.1f) -> (%.1f, %.1f)\n"), OldPos.X, OldPos.Y, NewPos.X, NewPos.Y);
			ChangesApplied++;
		}

		// Size
		const TArray<TSharedPtr<FJsonValue>>* SizeArray;
		if (SlotConfig->TryGetArrayField(TEXT("size"), SizeArray) && SizeArray->Num() >= 2)
		{
			FVector2D OldSize = CanvasSlot->GetSize();
			FVector2D NewSize((*SizeArray)[0]->AsNumber(), (*SizeArray)[1]->AsNumber());
			CanvasSlot->SetSize(NewSize);
			Result += FString::Printf(TEXT("+ Size: (%.1f, %.1f) -> (%.1f, %.1f)\n"), OldSize.X, OldSize.Y, NewSize.X, NewSize.Y);
			ChangesApplied++;
		}

		// Alignment
		const TArray<TSharedPtr<FJsonValue>>* AlignmentArray;
		if (SlotConfig->TryGetArrayField(TEXT("alignment"), AlignmentArray) && AlignmentArray->Num() >= 2)
		{
			FVector2D OldAlign = CanvasSlot->GetAlignment();
			FVector2D NewAlign((*AlignmentArray)[0]->AsNumber(), (*AlignmentArray)[1]->AsNumber());
			CanvasSlot->SetAlignment(NewAlign);
			Result += FString::Printf(TEXT("+ Alignment: (%.2f, %.2f) -> (%.2f, %.2f)\n"), OldAlign.X, OldAlign.Y, NewAlign.X, NewAlign.Y);
			ChangesApplied++;
		}

		// Anchors
		const TSharedPtr<FJsonObject>* AnchorsObj;
		if (SlotConfig->TryGetObjectField(TEXT("anchors"), AnchorsObj))
		{
			FAnchors OldAnchors = CanvasSlot->GetAnchors();
			FAnchors NewAnchors = OldAnchors;

			const TArray<TSharedPtr<FJsonValue>>* MinArray;
			if ((*AnchorsObj)->TryGetArrayField(TEXT("min"), MinArray) && MinArray->Num() >= 2)
			{
				NewAnchors.Minimum = FVector2D((*MinArray)[0]->AsNumber(), (*MinArray)[1]->AsNumber());
			}

			const TArray<TSharedPtr<FJsonValue>>* MaxArray;
			if ((*AnchorsObj)->TryGetArrayField(TEXT("max"), MaxArray) && MaxArray->Num() >= 2)
			{
				NewAnchors.Maximum = FVector2D((*MaxArray)[0]->AsNumber(), (*MaxArray)[1]->AsNumber());
			}

			CanvasSlot->SetAnchors(NewAnchors);
			Result += FString::Printf(TEXT("+ Anchors: Min(%.2f, %.2f) Max(%.2f, %.2f)\n"),
				NewAnchors.Minimum.X, NewAnchors.Minimum.Y, NewAnchors.Maximum.X, NewAnchors.Maximum.Y);
			ChangesApplied++;
		}

		// ZOrder
		int32 ZOrder;
		if (SlotConfig->TryGetNumberField(TEXT("z_order"), ZOrder))
		{
			int32 OldZOrder = CanvasSlot->GetZOrder();
			CanvasSlot->SetZOrder(ZOrder);
			Result += FString::Printf(TEXT("+ ZOrder: %d -> %d\n"), OldZOrder, ZOrder);
			ChangesApplied++;
		}

		// AutoSize
		bool bAutoSize;
		if (SlotConfig->TryGetBoolField(TEXT("auto_size"), bAutoSize))
		{
			bool bOldAutoSize = CanvasSlot->GetAutoSize();
			CanvasSlot->SetAutoSize(bAutoSize);
			Result += FString::Printf(TEXT("+ AutoSize: %s -> %s\n"),
				bOldAutoSize ? TEXT("true") : TEXT("false"),
				bAutoSize ? TEXT("true") : TEXT("false"));
			ChangesApplied++;
		}
	}
	// Handle HorizontalBoxSlot
	else if (UHorizontalBoxSlot* HBoxSlot = Cast<UHorizontalBoxSlot>(Slot))
	{
		// Padding
		const TSharedPtr<FJsonObject>* PaddingObj;
		if (SlotConfig->TryGetObjectField(TEXT("padding"), PaddingObj))
		{
			FMargin Padding;
			(*PaddingObj)->TryGetNumberField(TEXT("left"), Padding.Left);
			(*PaddingObj)->TryGetNumberField(TEXT("top"), Padding.Top);
			(*PaddingObj)->TryGetNumberField(TEXT("right"), Padding.Right);
			(*PaddingObj)->TryGetNumberField(TEXT("bottom"), Padding.Bottom);
			HBoxSlot->SetPadding(Padding);
			Result += FString::Printf(TEXT("+ Padding: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			ChangesApplied++;
		}

		// Size (fill rule)
		const TSharedPtr<FJsonObject>* SizeObj;
		if (SlotConfig->TryGetObjectField(TEXT("size"), SizeObj))
		{
			FSlateChildSize Size;
			FString SizeRule;
			if ((*SizeObj)->TryGetStringField(TEXT("rule"), SizeRule))
			{
				if (SizeRule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Automatic;
				}
				else if (SizeRule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Fill;
				}
			}
			double Value;
			if ((*SizeObj)->TryGetNumberField(TEXT("value"), Value))
			{
				Size.Value = Value;
			}
			HBoxSlot->SetSize(Size);
			Result += FString::Printf(TEXT("+ Size: %s (%.2f)\n"),
				Size.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"), Size.Value);
			ChangesApplied++;
		}
	}
	// Handle VerticalBoxSlot
	else if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Slot))
	{
		// Padding
		const TSharedPtr<FJsonObject>* PaddingObj;
		if (SlotConfig->TryGetObjectField(TEXT("padding"), PaddingObj))
		{
			FMargin Padding;
			(*PaddingObj)->TryGetNumberField(TEXT("left"), Padding.Left);
			(*PaddingObj)->TryGetNumberField(TEXT("top"), Padding.Top);
			(*PaddingObj)->TryGetNumberField(TEXT("right"), Padding.Right);
			(*PaddingObj)->TryGetNumberField(TEXT("bottom"), Padding.Bottom);
			VBoxSlot->SetPadding(Padding);
			Result += FString::Printf(TEXT("+ Padding: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			ChangesApplied++;
		}

		// Size (fill rule)
		const TSharedPtr<FJsonObject>* SizeObj;
		if (SlotConfig->TryGetObjectField(TEXT("size"), SizeObj))
		{
			FSlateChildSize Size;
			FString SizeRule;
			if ((*SizeObj)->TryGetStringField(TEXT("rule"), SizeRule))
			{
				if (SizeRule.Equals(TEXT("Auto"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Automatic;
				}
				else if (SizeRule.Equals(TEXT("Fill"), ESearchCase::IgnoreCase))
				{
					Size.SizeRule = ESlateSizeRule::Fill;
				}
			}
			double Value;
			if ((*SizeObj)->TryGetNumberField(TEXT("value"), Value))
			{
				Size.Value = Value;
			}
			VBoxSlot->SetSize(Size);
			Result += FString::Printf(TEXT("+ Size: %s (%.2f)\n"),
				Size.SizeRule == ESlateSizeRule::Fill ? TEXT("Fill") : TEXT("Auto"), Size.Value);
			ChangesApplied++;
		}
	}
	// Handle OverlaySlot
	else if (UOverlaySlot* OvlSlot = Cast<UOverlaySlot>(Slot))
	{
		// Padding
		const TSharedPtr<FJsonObject>* PaddingObj;
		if (SlotConfig->TryGetObjectField(TEXT("padding"), PaddingObj))
		{
			FMargin Padding;
			(*PaddingObj)->TryGetNumberField(TEXT("left"), Padding.Left);
			(*PaddingObj)->TryGetNumberField(TEXT("top"), Padding.Top);
			(*PaddingObj)->TryGetNumberField(TEXT("right"), Padding.Right);
			(*PaddingObj)->TryGetNumberField(TEXT("bottom"), Padding.Bottom);
			OvlSlot->SetPadding(Padding);
			Result += FString::Printf(TEXT("+ Padding: L=%.1f T=%.1f R=%.1f B=%.1f\n"),
				Padding.Left, Padding.Top, Padding.Right, Padding.Bottom);
			ChangesApplied++;
		}
	}
	else
	{
		Result += FString::Printf(TEXT("! Unsupported slot type: %s\n"), *Slot->GetClass()->GetName());
	}

	// Synchronize and refresh
	if (ChangesApplied > 0)
	{
		Slot->SynchronizeProperties();
		RefreshBlueprintEditor(OriginalAsset);
		Result += FString::Printf(TEXT("= %d slot properties configured\n"), ChangesApplied);
	}
	else
	{
		Result += TEXT("= No slot properties changed\n");
	}

	return Result;
}
