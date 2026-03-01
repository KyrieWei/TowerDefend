// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/EditBlueprintTool.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/EditEnhancedInputTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Blueprint editing
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameMode.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_Event.h"
#include "K2Node_Tunnel.h"
#include "EdGraphSchema_K2_Actions.h"
#include "UObject/Script.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyAccessUtil.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/UObjectIterator.h"

// Widget Blueprint support
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"
#include "Components/HorizontalBox.h"
#include "Components/VerticalBox.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/Border.h"
#include "Components/CheckBox.h"
#include "Components/EditableTextBox.h"
#include "Components/ProgressBar.h"
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Overlay.h"
#include "Components/GridPanel.h"
#include "Components/UniformGridPanel.h"
#include "Components/WrapBox.h"
#include "Components/WidgetSwitcher.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "WidgetBlueprintEditor.h"

// Timeline support
#include "Engine/TimelineTemplate.h"
#include "K2Node_Timeline.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"

// Animation Blueprint support
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimLayerInterface.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "Engine/MemberReference.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"
#include "AnimationStateGraph.h"
#include "AnimationTransitionGraph.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimationStateMachineSchema.h"
#include "AnimationStateGraphSchema.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimationTransitionSchema.h"
#include "Kismet2/Kismet2NameValidators.h"

// Transaction support for undo/redo
#include "ScopedTransaction.h"

// Asset types for creation
#include "Materials/Material.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/DataTable.h"
#include "Animation/Skeleton.h"
#include "Animation/AnimInstance.h"

// Asset creation factories
#include "Factories/BlueprintFactory.h"
#include "Factories/AnimBlueprintFactory.h"
#include "WidgetBlueprintFactory.h"
#include "Factories/BlueprintFunctionLibraryFactory.h"
#include "Factories/BlueprintMacroFactory.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialFunctionFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Factories/DataTableFactory.h"
#include "NiagaraSystemFactoryNew.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Containers/Ticker.h"
#include <initializer_list>

namespace
{
	static bool IsTransientWorldObject_EditBlueprint(const UObject* Object)
	{
		if (!Object)
		{
			return false;
		}

		const UWorld* WorldOuter = Object->GetTypedOuter<UWorld>();
		return WorldOuter && WorldOuter->GetPackage() == GetTransientPackage();
	}

	static void QueueWidgetPreviewRefresh_EditBlueprint(UWidgetBlueprint* WidgetBlueprint)
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
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("NeoStack: Deferred Widget Blueprint preview refresh for %s"), *SafeWidgetBlueprint->GetName());
				return false;
			}),
			0.0f);
	}

	static const TArray<FString>& GetEnhancedInputInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("create_type"),
			TEXT("value_type"),
			TEXT("consume_input"),
			TEXT("trigger_when_paused"),
			TEXT("add_triggers"),
			TEXT("remove_triggers"),
			TEXT("add_modifiers"),
			TEXT("remove_modifiers"),
			TEXT("add_mappings"),
			TEXT("remove_mappings"),
			TEXT("modify_mappings")
		};
		return Fields;
	}

	static const TArray<FString>& GetBlueprintInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("create"),
			TEXT("add_variables"),
			TEXT("remove_variables"),
			TEXT("modify_variables"),
			TEXT("add_components"),
			TEXT("remove_components"),
			TEXT("rename_components"),
			TEXT("duplicate_components"),
			TEXT("reparent_components"),
			TEXT("set_root_component"),
			TEXT("configure_components"),
			TEXT("add_functions"),
			TEXT("remove_functions"),
			TEXT("rename_functions"),
			TEXT("override_functions"),
			TEXT("add_macros"),
			TEXT("remove_macros"),
			TEXT("add_events"),
			TEXT("remove_events"),
			TEXT("add_custom_events"),
			TEXT("remove_custom_events"),
			TEXT("add_event_graphs"),
			TEXT("reparent"),
			TEXT("add_interfaces"),
			TEXT("remove_interfaces"),
			TEXT("add_widgets"),
			TEXT("remove_widgets"),
			TEXT("configure_widgets"),
			TEXT("bind_events"),
			TEXT("unbind_events"),
			TEXT("list_events"),
			TEXT("add_state_machine"),
			TEXT("add_anim_state"),
			TEXT("add_state_transition"),
			TEXT("add_linked_anim_layers"),
			TEXT("add_timelines"),
			TEXT("remove_timelines")
		};
		return Fields;
	}

	static bool BPToolHasAnyField(const TSharedPtr<FJsonObject>& Args, const TArray<FString>& Fields)
	{
		for (const FString& Field : Fields)
		{
			if (Args->HasField(Field))
			{
				return true;
			}
		}
		return false;
	}

	static void BPToolMergeSchemaProperties(TSharedPtr<FJsonObject> TargetSchema, const TSharedPtr<FJsonObject>& SourceSchema)
	{
		if (!TargetSchema.IsValid() || !SourceSchema.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> TargetProps;
		const TSharedPtr<FJsonObject>* TargetPropsPtr = nullptr;
		if (!TargetSchema->TryGetObjectField(TEXT("properties"), TargetPropsPtr) || !TargetPropsPtr || !(*TargetPropsPtr).IsValid())
		{
			TargetProps = MakeShared<FJsonObject>();
			TargetSchema->SetObjectField(TEXT("properties"), TargetProps);
		}
		else
		{
			TargetProps = *TargetPropsPtr;
		}

		const TSharedPtr<FJsonObject>* SourceProps = nullptr;
		if (!SourceSchema->TryGetObjectField(TEXT("properties"), SourceProps) || !SourceProps || !(*SourceProps).IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*SourceProps)->Values)
		{
			if (!TargetProps->HasField(Pair.Key))
			{
				TargetProps->SetField(Pair.Key, Pair.Value);
			}
		}
	}

	static bool BPToolIsClassOrSuperNamed(const UObject* Asset, std::initializer_list<const TCHAR*> Names)
	{
		if (!Asset || !Asset->GetClass())
		{
			return false;
		}

		for (const UClass* Class = Asset->GetClass(); Class; Class = Class->GetSuperClass())
		{
			const FString ClassName = Class->GetName();
			for (const TCHAR* Name : Names)
			{
				if (ClassName.Equals(Name, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		return false;
	}
}

TSharedPtr<FJsonObject> FEditBlueprintTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Blueprint asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> AssetDomainProp = MakeShared<FJsonObject>();
	AssetDomainProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetDomainProp->SetStringField(TEXT("description"), TEXT("Optional routing domain: 'blueprint' (default) or 'enhanced_input'."));
	TArray<TSharedPtr<FJsonValue>> AssetDomainEnum;
	AssetDomainEnum.Add(MakeShared<FJsonValueString>(TEXT("blueprint")));
	AssetDomainEnum.Add(MakeShared<FJsonValueString>(TEXT("enhanced_input")));
	AssetDomainProp->SetArrayField(TEXT("enum"), AssetDomainEnum);
	Properties->SetObjectField(TEXT("asset_domain"), AssetDomainProp);

	TSharedPtr<FJsonObject> CreateProp = MakeShared<FJsonObject>();
	CreateProp->SetStringField(TEXT("type"), TEXT("object"));
	CreateProp->SetStringField(TEXT("description"),
		TEXT("Create a new asset. 'name' and 'path' specify where. "
			 "type: Blueprint, AnimBlueprint, WidgetBlueprint, Interface, FunctionLibrary, MacroLibrary, AnimLayerInterface, "
			 "Material, MaterialFunction, MaterialInstance, NiagaraSystem, DataTable, LevelSequence. "
			 "parent_class: parent class for Blueprint (Actor, Pawn, Character, GameModeBase, PlayerController, "
			 "HUD, PlayerState, GameStateBase, ActorComponent, SceneComponent, or full class path). "
			 "For AnimBlueprint: requires 'skeleton' (path to USkeleton asset). "
			 "For MaterialInstance: requires 'parent_material' (path to parent material). "
			 "For DataTable: requires 'row_struct' (struct name or path). "
			 "After creation, other edit operations (add_variables, add_components, etc.) are applied to the new asset."));
	Properties->SetObjectField(TEXT("create"), CreateProp);

	TSharedPtr<FJsonObject> AddVarsProp = MakeShared<FJsonObject>();
	AddVarsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddVarsProp->SetStringField(TEXT("description"),
		TEXT("Variables to add: [{name, type:{base, container, subtype}, default, category, function, replicated, rep_notify, expose_on_spawn, private, transient, save_game, advanced_display, deprecated, interp, read_only, blueprint_only}]. "
		     "If 'function' is set, creates a LOCAL variable scoped to that function graph (local vars don't support replication/expose_on_spawn). "
		     "Flags: save_game (persisted in save), advanced_display (hidden under Advanced), deprecated (show warning), "
		     "interp (exposed to Sequencer), read_only (blueprint read-only), blueprint_only (not editable in Details)."));
	Properties->SetObjectField(TEXT("add_variables"), AddVarsProp);

	TSharedPtr<FJsonObject> RemoveVarsProp = MakeShared<FJsonObject>();
	RemoveVarsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveVarsProp->SetStringField(TEXT("description"),
		TEXT("Variables to remove: string names OR objects [{name, function}]. "
		     "If 'function' is set, removes a LOCAL variable from that function graph."));
	Properties->SetObjectField(TEXT("remove_variables"), RemoveVarsProp);

	TSharedPtr<FJsonObject> ModifyVarsProp = MakeShared<FJsonObject>();
	ModifyVarsProp->SetStringField(TEXT("type"), TEXT("array"));
	ModifyVarsProp->SetStringField(TEXT("description"),
		TEXT("Modify existing variables: [{name, function?, new_name?, new_type?, category?, default?, "
		     "replicated?, rep_notify?, expose_on_spawn?, private?, transient?, save_game?, advanced_display?, "
		     "deprecated?, interp?, read_only?, blueprint_only?}]. "
		     "All fields except 'name' are optional - only provided fields are changed. "
		     "If 'function' is set, modifies a LOCAL variable in that function graph. "
		     "new_type uses same format as add_variables type: {base, container?, subtype?}."));
	Properties->SetObjectField(TEXT("modify_variables"), ModifyVarsProp);

	TSharedPtr<FJsonObject> AddCompsProp = MakeShared<FJsonObject>();
	AddCompsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddCompsProp->SetStringField(TEXT("description"), TEXT("Components to add: [{name, class, parent, properties:{}}]"));
	Properties->SetObjectField(TEXT("add_components"), AddCompsProp);

	TSharedPtr<FJsonObject> RemoveCompsProp = MakeShared<FJsonObject>();
	RemoveCompsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveCompsProp->SetStringField(TEXT("description"), TEXT("Component names to remove"));
	Properties->SetObjectField(TEXT("remove_components"), RemoveCompsProp);

	TSharedPtr<FJsonObject> ConfigureCompsProp = MakeShared<FJsonObject>();
	ConfigureCompsProp->SetStringField(TEXT("type"), TEXT("array"));
	ConfigureCompsProp->SetStringField(TEXT("description"),
		TEXT("Configure component properties (works with inherited components too): [{name, properties:{PropertyName:Value}}]. "
		     "Use read_asset with component parameter to see available properties. "
		     "For object properties like SkeletalMesh, use asset path string: \"/Game/Meshes/MyMesh.MyMesh\""));
	Properties->SetObjectField(TEXT("configure_components"), ConfigureCompsProp);

	TSharedPtr<FJsonObject> RenameCompsProp = MakeShared<FJsonObject>();
	RenameCompsProp->SetStringField(TEXT("type"), TEXT("array"));
	RenameCompsProp->SetStringField(TEXT("description"),
		TEXT("Rename components: [{name, new_name}]. Updates all variable references in graphs."));
	Properties->SetObjectField(TEXT("rename_components"), RenameCompsProp);

	TSharedPtr<FJsonObject> DuplicateCompsProp = MakeShared<FJsonObject>();
	DuplicateCompsProp->SetStringField(TEXT("type"), TEXT("array"));
	DuplicateCompsProp->SetStringField(TEXT("description"),
		TEXT("Duplicate components: [{name, new_name?}]. Copies component with all properties. "
		     "Optional new_name (auto-generated if omitted). Duplicated under same parent."));
	Properties->SetObjectField(TEXT("duplicate_components"), DuplicateCompsProp);

	TSharedPtr<FJsonObject> ReparentCompsProp = MakeShared<FJsonObject>();
	ReparentCompsProp->SetStringField(TEXT("type"), TEXT("array"));
	ReparentCompsProp->SetStringField(TEXT("description"),
		TEXT("Reparent components: [{name, parent}]. Moves component under a new parent in hierarchy. "
		     "Set parent to empty string or \"root\" to detach to scene root."));
	Properties->SetObjectField(TEXT("reparent_components"), ReparentCompsProp);

	TSharedPtr<FJsonObject> SetRootProp = MakeShared<FJsonObject>();
	SetRootProp->SetStringField(TEXT("type"), TEXT("string"));
	SetRootProp->SetStringField(TEXT("description"),
		TEXT("Set scene root component: component name to promote as the new scene root. "
		     "Must be a SceneComponent. Old root becomes child of new root."));
	Properties->SetObjectField(TEXT("set_root_component"), SetRootProp);

	TSharedPtr<FJsonObject> AddFuncsProp = MakeShared<FJsonObject>();
	AddFuncsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddFuncsProp->SetStringField(TEXT("description"),
		TEXT("Functions to add: [{name, pure, category, inputs:[{name,type}], outputs:[{name,type}]}]. "
		     "type can be simple string (\"Float\", \"Vector\", \"Name\", \"Actor\") or object {base, container?, subtype?} for complex types like arrays."));
	Properties->SetObjectField(TEXT("add_functions"), AddFuncsProp);

	TSharedPtr<FJsonObject> RemoveFuncsProp = MakeShared<FJsonObject>();
	RemoveFuncsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveFuncsProp->SetStringField(TEXT("description"), TEXT("Function names to remove"));
	Properties->SetObjectField(TEXT("remove_functions"), RemoveFuncsProp);

	TSharedPtr<FJsonObject> RenameFuncsProp = MakeShared<FJsonObject>();
	RenameFuncsProp->SetStringField(TEXT("type"), TEXT("array"));
	RenameFuncsProp->SetStringField(TEXT("description"),
		TEXT("Rename functions or macros: [{name, new_name}]. Updates all call sites and references."));
	Properties->SetObjectField(TEXT("rename_functions"), RenameFuncsProp);

	TSharedPtr<FJsonObject> AddMacrosProp = MakeShared<FJsonObject>();
	AddMacrosProp->SetStringField(TEXT("type"), TEXT("array"));
	AddMacrosProp->SetStringField(TEXT("description"),
		TEXT("Macros to add: [{name, category, inputs:[{name,type}], outputs:[{name,type}]}]. "
		     "Macros are like functions but inline at call sites. Same type format as add_functions."));
	Properties->SetObjectField(TEXT("add_macros"), AddMacrosProp);

	TSharedPtr<FJsonObject> RemoveMacrosProp = MakeShared<FJsonObject>();
	RemoveMacrosProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveMacrosProp->SetStringField(TEXT("description"), TEXT("Macro names to remove"));
	Properties->SetObjectField(TEXT("remove_macros"), RemoveMacrosProp);

	TSharedPtr<FJsonObject> OverrideFuncsProp = MakeShared<FJsonObject>();
	OverrideFuncsProp->SetStringField(TEXT("type"), TEXT("array"));
	OverrideFuncsProp->SetStringField(TEXT("description"),
		TEXT("Parent class functions to override: [\"BeginPlay\", \"Tick\", \"EndPlay\", ...]. "
		     "Creates event nodes or function graphs depending on the function type."));
	Properties->SetObjectField(TEXT("override_functions"), OverrideFuncsProp);

	TSharedPtr<FJsonObject> AddEventGraphsProp = MakeShared<FJsonObject>();
	AddEventGraphsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEventGraphsProp->SetStringField(TEXT("description"),
		TEXT("Additional event graph pages to add: [\"GraphName\"]. "
		     "Useful for organizing event logic into separate graphs."));
	Properties->SetObjectField(TEXT("add_event_graphs"), AddEventGraphsProp);

	// Blueprint-level operations
	TSharedPtr<FJsonObject> ReparentProp = MakeShared<FJsonObject>();
	ReparentProp->SetStringField(TEXT("type"), TEXT("string"));
	ReparentProp->SetStringField(TEXT("description"),
		TEXT("New parent class name or path to reparent the Blueprint. "
		     "Examples: \"Character\", \"Pawn\", \"/Script/Engine.Character\". "
		     "WARNING: May cause data loss if new parent is not in the same hierarchy."));
	Properties->SetObjectField(TEXT("reparent"), ReparentProp);

	TSharedPtr<FJsonObject> AddInterfacesProp = MakeShared<FJsonObject>();
	AddInterfacesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddInterfacesProp->SetStringField(TEXT("description"),
		TEXT("Interfaces to implement: [\"InterfaceName\" or \"/Script/Module.InterfaceName\"]. "
		     "Creates function graph stubs for all interface functions automatically. "
		     "Examples: [\"BPI_Interactable\"], [\"/Game/Interfaces/BPI_Damageable.BPI_Damageable\"]"));
	Properties->SetObjectField(TEXT("add_interfaces"), AddInterfacesProp);

	TSharedPtr<FJsonObject> RemoveInterfacesProp = MakeShared<FJsonObject>();
	RemoveInterfacesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveInterfacesProp->SetStringField(TEXT("description"),
		TEXT("Interfaces to remove: [\"InterfaceName\"] or [{name:\"InterfaceName\", preserve_functions:true}]. "
		     "preserve_functions (default false): if true, keeps the function graphs as regular functions instead of deleting them."));
	Properties->SetObjectField(TEXT("remove_interfaces"), RemoveInterfacesProp);

	TSharedPtr<FJsonObject> AddEventsProp = MakeShared<FJsonObject>();
	AddEventsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEventsProp->SetStringField(TEXT("description"),
		TEXT("Event Dispatcher VARIABLES (multicast delegate properties) to add: [{name, params:[{name,type}]}]. "
		     "These are delegate variables you can Bind/Unbind/Call, NOT event nodes. "
		     "IMPORTANT: For Custom Event NODES (red event nodes in EventGraph, including RPCs), use add_custom_events instead."));
	Properties->SetObjectField(TEXT("add_events"), AddEventsProp);

	TSharedPtr<FJsonObject> RemoveEventsProp = MakeShared<FJsonObject>();
	RemoveEventsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveEventsProp->SetStringField(TEXT("description"), TEXT("Event dispatcher names to remove"));
	Properties->SetObjectField(TEXT("remove_events"), RemoveEventsProp);

	TSharedPtr<FJsonObject> AddCustomEventsProp = MakeShared<FJsonObject>();
	AddCustomEventsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddCustomEventsProp->SetStringField(TEXT("description"),
		TEXT("Custom Event NODES to add in EventGraph (red event nodes that can be called): [{name, params:[{name,type}], replication, reliable, call_in_editor}]. "
		     "This creates actual event nodes you can wire logic to and call with 'Call <EventName>'. "
		     "type: simple string (\"Float\", \"Vector\", \"Bool\", \"Name\", \"Actor\") or object {base, subtype} for Object refs. "
		     "replication: \"NotReplicated\" (default), \"Multicast\" (Server->All Clients), \"Server\" (Client->Server RPC), \"Client\" (Server->Owning Client). "
		     "reliable: true for guaranteed delivery. "
		     "NOTE: For Event Dispatcher variables (Bind/Call pattern), use add_events instead."));
	Properties->SetObjectField(TEXT("add_custom_events"), AddCustomEventsProp);

	TSharedPtr<FJsonObject> RemoveCustomEventsProp = MakeShared<FJsonObject>();
	RemoveCustomEventsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveCustomEventsProp->SetStringField(TEXT("description"), TEXT("Custom event names to remove"));
	Properties->SetObjectField(TEXT("remove_custom_events"), RemoveCustomEventsProp);

	TSharedPtr<FJsonObject> ListEventsProp = MakeShared<FJsonObject>();
	ListEventsProp->SetStringField(TEXT("type"), TEXT("string"));
	ListEventsProp->SetStringField(TEXT("description"), TEXT("Component or widget name to list available events"));
	Properties->SetObjectField(TEXT("list_events"), ListEventsProp);

	TSharedPtr<FJsonObject> BindEventsProp = MakeShared<FJsonObject>();
	BindEventsProp->SetStringField(TEXT("type"), TEXT("array"));
	BindEventsProp->SetStringField(TEXT("description"), TEXT("Events to bind: [{source, event, handler}]"));
	Properties->SetObjectField(TEXT("bind_events"), BindEventsProp);

	TSharedPtr<FJsonObject> UnbindEventsProp = MakeShared<FJsonObject>();
	UnbindEventsProp->SetStringField(TEXT("type"), TEXT("array"));
	UnbindEventsProp->SetStringField(TEXT("description"), TEXT("Events to unbind: [{source, event}]"));
	Properties->SetObjectField(TEXT("unbind_events"), UnbindEventsProp);

	TSharedPtr<FJsonObject> AddWidgetsProp = MakeShared<FJsonObject>();
	AddWidgetsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddWidgetsProp->SetStringField(TEXT("description"), TEXT("Widgets to add (Widget Blueprints): [{type, name, parent}]"));
	Properties->SetObjectField(TEXT("add_widgets"), AddWidgetsProp);

	TSharedPtr<FJsonObject> RemoveWidgetsProp = MakeShared<FJsonObject>();
	RemoveWidgetsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveWidgetsProp->SetStringField(TEXT("description"), TEXT("Widget names to remove"));
	Properties->SetObjectField(TEXT("remove_widgets"), RemoveWidgetsProp);

	TSharedPtr<FJsonObject> ConfigWidgetsProp = MakeShared<FJsonObject>();
	ConfigWidgetsProp->SetStringField(TEXT("type"), TEXT("array"));
	ConfigWidgetsProp->SetStringField(TEXT("description"),
		TEXT("Configure widget properties. Use UE struct string format (NOT JSON objects). "
			 "Format: [{name, properties:{...}, slot:{...}}]. "
			 "Examples: Text:\"Hello\", ColorAndOpacity:\"(R=1.0,G=0.0,B=0.0,A=1.0)\". "
			 "For CanvasPanel slots use LayoutData: slot:{LayoutData:\"(Offsets=(Left=0,Top=0,Right=100,Bottom=50),Anchors=(Minimum=(X=0,Y=0),Maximum=(X=0,Y=0)),Alignment=(X=0,Y=0))\"}"));
	Properties->SetObjectField(TEXT("configure_widgets"), ConfigWidgetsProp);

	// Timeline operations
	TSharedPtr<FJsonObject> AddTimelinesProp = MakeShared<FJsonObject>();
	AddTimelinesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddTimelinesProp->SetStringField(TEXT("description"),
		TEXT("Configure timeline tracks/properties. WORKFLOW: 1) Use edit_graph with operation='find_nodes' to find 'Timeline' node, "
		     "2) Use edit_graph to add timeline node to graph (creates UTimelineTemplate with matching name), "
		     "3) Use THIS to configure it: [{name, length, auto_play, loop, replicated, tracks:[{name, type, keyframes}]}]. "
		     "Track type: \"Float\" (default), \"Vector\", \"LinearColor\", \"Event\". "
		     "Keyframes: [{time, value}] for float, [{time, x, y, z}] for vector, [{time, r, g, b, a}] for color, [{time}] for event. "
		     "interp_mode: \"Linear\" (default), \"Constant\", \"Cubic\". "
		     "The 'name' must match the timeline node's name in the graph."));
	Properties->SetObjectField(TEXT("add_timelines"), AddTimelinesProp);

	TSharedPtr<FJsonObject> RemoveTimelinesProp = MakeShared<FJsonObject>();
	RemoveTimelinesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveTimelinesProp->SetStringField(TEXT("description"), TEXT("Timeline names to remove"));
	Properties->SetObjectField(TEXT("remove_timelines"), RemoveTimelinesProp);

	TSharedPtr<FJsonObject> AddSMProp = MakeShared<FJsonObject>();
	AddSMProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSMProp->SetStringField(TEXT("description"), TEXT("State machines to add (AnimBP): [{name}]"));
	Properties->SetObjectField(TEXT("add_state_machine"), AddSMProp);

	TSharedPtr<FJsonObject> AddStateProp = MakeShared<FJsonObject>();
	AddStateProp->SetStringField(TEXT("type"), TEXT("array"));
	AddStateProp->SetStringField(TEXT("description"), TEXT("States to add: [{name, state_machine}]"));
	Properties->SetObjectField(TEXT("add_anim_state"), AddStateProp);

	TSharedPtr<FJsonObject> AddTransProp = MakeShared<FJsonObject>();
	AddTransProp->SetStringField(TEXT("type"), TEXT("array"));
	AddTransProp->SetStringField(TEXT("description"), TEXT("Transitions to add: [{state_machine, from_state, to_state}]"));
	Properties->SetObjectField(TEXT("add_state_transition"), AddTransProp);

	TSharedPtr<FJsonObject> AddLayersProp = MakeShared<FJsonObject>();
	AddLayersProp->SetStringField(TEXT("type"), TEXT("array"));
	AddLayersProp->SetStringField(TEXT("description"),
		TEXT("Add Linked Anim Layer nodes to AnimGraph (AnimBP only). "
			"Each: {layer_name (function name from AnimLayerInterface, required), "
			"interface?: AnimLayerInterface class/path (optional — auto-detected if AnimBP implements only one)}. "
			"First add the interface via add_interfaces, then add layer nodes. "
			"Use edit_graph with 'animlayer:<LayerName>' selector to edit the layer function graph."));
	Properties->SetObjectField(TEXT("add_linked_anim_layers"), AddLayersProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	// Fold Enhanced Input parameter surface into edit_blueprint.
	FEditEnhancedInputTool EnhancedInputTool;
	BPToolMergeSchemaProperties(Schema, EnhancedInputTool.GetInputSchema());

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditBlueprintTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	FString AssetDomain;
	Args->TryGetStringField(TEXT("asset_domain"), AssetDomain);
	AssetDomain = AssetDomain.ToLower();

	const bool bHasEnhancedInputFields = BPToolHasAnyField(Args, GetEnhancedInputInferenceFields());
	const bool bHasBlueprintFields = BPToolHasAnyField(Args, GetBlueprintInferenceFields());

	if (AssetDomain.IsEmpty() && bHasEnhancedInputFields && bHasBlueprintFields)
	{
		return FToolResult::Fail(TEXT("Ambiguous request: contains both Blueprint and Enhanced Input operations. Set asset_domain explicitly."));
	}

	if (AssetDomain.IsEmpty() && bHasEnhancedInputFields)
	{
		AssetDomain = TEXT("enhanced_input");
	}

	if (AssetDomain.IsEmpty())
	{
		const FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
		if (UObject* ExistingAsset = LoadObject<UObject>(nullptr, *FullAssetPath))
		{
			if (BPToolIsClassOrSuperNamed(ExistingAsset, { TEXT("InputAction"), TEXT("InputMappingContext") }))
			{
				AssetDomain = TEXT("enhanced_input");
			}
		}
	}

	if (AssetDomain == TEXT("enhanced_input") || AssetDomain == TEXT("input"))
	{
		FEditEnhancedInputTool Tool;
		return Tool.Execute(Args);
	}

	if (!AssetDomain.IsEmpty() && AssetDomain != TEXT("blueprint"))
	{
		return FToolResult::Fail(TEXT("Invalid asset_domain. Use 'blueprint' or 'enhanced_input'."));
	}

	// ── CREATE ACTION ──────────────────────────────────────────────
	const TSharedPtr<FJsonObject>* CreateObj = nullptr;
	if (Args->TryGetObjectField(TEXT("create"), CreateObj) && CreateObj && (*CreateObj)->Values.Num() > 0)
	{
		FToolResult CreateResult = HandleCreate(Name, Path, *CreateObj);
		if (!CreateResult.bSuccess)
		{
			return CreateResult;
		}

		// Check if there are any edit operations beyond create
		bool bHasEditOps = false;
		static const TArray<FString> EditKeys = {
			TEXT("add_variables"), TEXT("remove_variables"), TEXT("modify_variables"),
			TEXT("add_components"), TEXT("remove_components"), TEXT("rename_components"),
			TEXT("duplicate_components"), TEXT("reparent_components"), TEXT("set_root_component"),
			TEXT("configure_components"), TEXT("add_functions"), TEXT("remove_functions"),
			TEXT("rename_functions"), TEXT("override_functions"), TEXT("add_macros"), TEXT("remove_macros"),
			TEXT("add_events"), TEXT("remove_events"), TEXT("add_custom_events"), TEXT("remove_custom_events"),
			TEXT("add_event_graphs"), TEXT("reparent"), TEXT("add_interfaces"), TEXT("remove_interfaces"),
			TEXT("add_widgets"), TEXT("remove_widgets"), TEXT("configure_widgets"),
			TEXT("bind_events"), TEXT("unbind_events"), TEXT("list_events"),
			TEXT("add_state_machines"), TEXT("add_states"), TEXT("add_transitions"),
			TEXT("add_linked_anim_layers"), TEXT("add_timelines"), TEXT("remove_timelines"),
			TEXT("add_timeline_tracks"), TEXT("add_timeline_keyframes")
		};
		for (const FString& Key : EditKeys)
		{
			if (Args->HasField(Key))
			{
				bHasEditOps = true;
				break;
			}
		}

		// If no edit operations, just return the creation result
		if (!bHasEditOps)
		{
			return CreateResult;
		}

		// Otherwise, fall through to load the newly created asset for editing
	}

	// Build asset path and load Blueprint
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);

	UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *FullAssetPath);

	if (!Blueprint)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Blueprint not found: %s"), *FullAssetPath));
	}

	// Create transaction for undo/redo support
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditBlueprint", "AI Edit Blueprint: {0}"),
		FText::FromString(Name)));

	Blueprint->Modify();

	TArray<FString> Results;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;
	int32 ModifiedCount = 0;

	// Process add_variables
	const TArray<TSharedPtr<FJsonValue>>* AddVariables;
	if (Args->TryGetArrayField(TEXT("add_variables"), AddVariables))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddVariables)
		{
			const TSharedPtr<FJsonObject>* VarObj;
			if (Value->TryGetObject(VarObj))
			{
				FVariableDefinition VarDef;
				(*VarObj)->TryGetStringField(TEXT("name"), VarDef.Name);

				const TSharedPtr<FJsonObject>* TypeObj;
				if ((*VarObj)->TryGetObjectField(TEXT("type"), TypeObj))
				{
					VarDef.Type = ParseTypeDefinition(*TypeObj);
				}
				else
				{
					// Support simple string type: {"type": "Float"}
					FString TypeStr;
					if ((*VarObj)->TryGetStringField(TEXT("type"), TypeStr))
					{
						VarDef.Type.Base = TypeStr;
						VarDef.Type.Container = TEXT("Single");
					}
				}

				(*VarObj)->TryGetStringField(TEXT("default"), VarDef.Default);
				(*VarObj)->TryGetStringField(TEXT("category"), VarDef.Category);
				(*VarObj)->TryGetStringField(TEXT("function"), VarDef.Function);
				(*VarObj)->TryGetBoolField(TEXT("replicated"), VarDef.bReplicated);
				(*VarObj)->TryGetBoolField(TEXT("rep_notify"), VarDef.bRepNotify);
				(*VarObj)->TryGetBoolField(TEXT("expose_on_spawn"), VarDef.bExposeOnSpawn);
				(*VarObj)->TryGetBoolField(TEXT("private"), VarDef.bPrivate);
				(*VarObj)->TryGetBoolField(TEXT("transient"), VarDef.bTransient);
				(*VarObj)->TryGetBoolField(TEXT("save_game"), VarDef.bSaveGame);
				(*VarObj)->TryGetBoolField(TEXT("advanced_display"), VarDef.bAdvancedDisplay);
				(*VarObj)->TryGetBoolField(TEXT("deprecated"), VarDef.bDeprecated);
				(*VarObj)->TryGetBoolField(TEXT("interp"), VarDef.bInterp);
				(*VarObj)->TryGetBoolField(TEXT("read_only"), VarDef.bReadOnly);
				(*VarObj)->TryGetBoolField(TEXT("blueprint_only"), VarDef.bBlueprintOnly);

				FString Result = AddVariable(Blueprint, VarDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_variables (supports string names or objects with {name, function})
	const TArray<TSharedPtr<FJsonValue>>* RemoveVariables;
	if (Args->TryGetArrayField(TEXT("remove_variables"), RemoveVariables))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveVariables)
		{
			FString VarName;
			FString Function;

			// Try object format first: {name, function}
			const TSharedPtr<FJsonObject>* RemObj;
			if (Value->TryGetObject(RemObj))
			{
				(*RemObj)->TryGetStringField(TEXT("name"), VarName);
				(*RemObj)->TryGetStringField(TEXT("function"), Function);
			}
			else
			{
				// Fall back to simple string
				Value->TryGetString(VarName);
			}

			if (!VarName.IsEmpty())
			{
				FString Result = RemoveVariable(Blueprint, VarName, Function);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process modify_variables (rename, change type, update flags)
	const TArray<TSharedPtr<FJsonValue>>* ModifyVariables;
	if (Args->TryGetArrayField(TEXT("modify_variables"), ModifyVariables))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ModifyVariables)
		{
			const TSharedPtr<FJsonObject>* ModObj;
			if (Value->TryGetObject(ModObj))
			{
				FVariableModification ModDef;
				(*ModObj)->TryGetStringField(TEXT("name"), ModDef.Name);
				(*ModObj)->TryGetStringField(TEXT("function"), ModDef.Function);
				(*ModObj)->TryGetStringField(TEXT("new_name"), ModDef.NewName);
				(*ModObj)->TryGetStringField(TEXT("category"), ModDef.Category);
				(*ModObj)->TryGetStringField(TEXT("default"), ModDef.Default);

				const TSharedPtr<FJsonObject>* NewTypeObj;
				if ((*ModObj)->TryGetObjectField(TEXT("new_type"), NewTypeObj))
				{
					ModDef.NewType = *NewTypeObj;
				}
				else
				{
					// Support simple string: "new_type": "Float"
					FString TypeStr;
					if ((*ModObj)->TryGetStringField(TEXT("new_type"), TypeStr))
					{
						ModDef.NewType = MakeShared<FJsonObject>();
						ModDef.NewType->SetStringField(TEXT("base"), TypeStr);
					}
				}

				// Parse optional flags (only set if explicitly present in JSON)
				bool bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("replicated"), bTemp)) ModDef.bReplicated = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("rep_notify"), bTemp)) ModDef.bRepNotify = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("expose_on_spawn"), bTemp)) ModDef.bExposeOnSpawn = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("private"), bTemp)) ModDef.bPrivate = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("transient"), bTemp)) ModDef.bTransient = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("save_game"), bTemp)) ModDef.bSaveGame = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("advanced_display"), bTemp)) ModDef.bAdvancedDisplay = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("deprecated"), bTemp)) ModDef.bDeprecated = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("interp"), bTemp)) ModDef.bInterp = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("read_only"), bTemp)) ModDef.bReadOnly = bTemp;
				if ((*ModObj)->TryGetBoolField(TEXT("blueprint_only"), bTemp)) ModDef.bBlueprintOnly = bTemp;

				FString Result = ModifyVariable(Blueprint, ModDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
			}
		}
	}

	// Process add_components
	const TArray<TSharedPtr<FJsonValue>>* AddComponents;
	if (Args->TryGetArrayField(TEXT("add_components"), AddComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddComponents)
		{
			const TSharedPtr<FJsonObject>* CompObj;
			if (Value->TryGetObject(CompObj))
			{
				FComponentDefinition CompDef;
				(*CompObj)->TryGetStringField(TEXT("name"), CompDef.Name);
				(*CompObj)->TryGetStringField(TEXT("class"), CompDef.Class);
				(*CompObj)->TryGetStringField(TEXT("parent"), CompDef.Parent);

				const TSharedPtr<FJsonObject>* PropsObj;
				if ((*CompObj)->TryGetObjectField(TEXT("properties"), PropsObj))
				{
					CompDef.Properties = *PropsObj;
				}

				FString Result = AddComponent(Blueprint, CompDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_components
	const TArray<TSharedPtr<FJsonValue>>* RemoveComponents;
	if (Args->TryGetArrayField(TEXT("remove_components"), RemoveComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveComponents)
		{
			FString CompName;
			if (Value->TryGetString(CompName))
			{
				FString Result = RemoveComponent(Blueprint, CompName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process configure_components (works with inherited components too)
	const TArray<TSharedPtr<FJsonValue>>* ConfigureComponents;
	if (Args->TryGetArrayField(TEXT("configure_components"), ConfigureComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ConfigureComponents)
		{
			const TSharedPtr<FJsonObject>* CompObj;
			if (Value->TryGetObject(CompObj))
			{
				FString CompName;
				(*CompObj)->TryGetStringField(TEXT("name"), CompName);

				const TSharedPtr<FJsonObject>* PropsObj;
				if ((*CompObj)->TryGetObjectField(TEXT("properties"), PropsObj))
				{
					FString Result = ConfigureComponent(Blueprint, CompName, *PropsObj);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
				}
			}
		}
	}

	// Process rename_components
	const TArray<TSharedPtr<FJsonValue>>* RenameComponents;
	if (Args->TryGetArrayField(TEXT("rename_components"), RenameComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RenameComponents)
		{
			const TSharedPtr<FJsonObject>* RenObj;
			if (Value->TryGetObject(RenObj))
			{
				FString CompName, NewName;
				(*RenObj)->TryGetStringField(TEXT("name"), CompName);
				(*RenObj)->TryGetStringField(TEXT("new_name"), NewName);

				FString Result = RenameComponent(Blueprint, CompName, NewName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
			}
		}
	}

	// Process duplicate_components
	const TArray<TSharedPtr<FJsonValue>>* DuplicateComponents;
	if (Args->TryGetArrayField(TEXT("duplicate_components"), DuplicateComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *DuplicateComponents)
		{
			const TSharedPtr<FJsonObject>* DupObj;
			if (Value->TryGetObject(DupObj))
			{
				FString CompName, NewName;
				(*DupObj)->TryGetStringField(TEXT("name"), CompName);
				(*DupObj)->TryGetStringField(TEXT("new_name"), NewName);

				FString Result = DuplicateComponent(Blueprint, CompName, NewName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process reparent_components
	const TArray<TSharedPtr<FJsonValue>>* ReparentComponents;
	if (Args->TryGetArrayField(TEXT("reparent_components"), ReparentComponents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ReparentComponents)
		{
			const TSharedPtr<FJsonObject>* RepObj;
			if (Value->TryGetObject(RepObj))
			{
				FString CompName, NewParent;
				(*RepObj)->TryGetStringField(TEXT("name"), CompName);
				(*RepObj)->TryGetStringField(TEXT("parent"), NewParent);

				FString Result = ReparentComponent(Blueprint, CompName, NewParent);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
			}
		}
	}

	// Process set_root_component
	FString SetRootName;
	if (Args->TryGetStringField(TEXT("set_root_component"), SetRootName) && !SetRootName.IsEmpty())
	{
		FString Result = SetRootComponent(Blueprint, SetRootName);
		Results.Add(Result);
		if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
	}

	// Process add_functions
	const TArray<TSharedPtr<FJsonValue>>* AddFunctions;
	if (Args->TryGetArrayField(TEXT("add_functions"), AddFunctions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddFunctions)
		{
			const TSharedPtr<FJsonObject>* FuncObj;
			if (Value->TryGetObject(FuncObj))
			{
				FFunctionDefinition FuncDef;
				(*FuncObj)->TryGetStringField(TEXT("name"), FuncDef.Name);
				(*FuncObj)->TryGetBoolField(TEXT("pure"), FuncDef.bPure);
				(*FuncObj)->TryGetStringField(TEXT("category"), FuncDef.Category);

				const TArray<TSharedPtr<FJsonValue>>* Inputs;
				if ((*FuncObj)->TryGetArrayField(TEXT("inputs"), Inputs))
				{
					for (const TSharedPtr<FJsonValue>& InputVal : *Inputs)
					{
						const TSharedPtr<FJsonObject>* InputObj;
						if (InputVal->TryGetObject(InputObj))
						{
							FuncDef.Inputs.Add(ParseFunctionParam(*InputObj));
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Outputs;
				if ((*FuncObj)->TryGetArrayField(TEXT("outputs"), Outputs))
				{
					for (const TSharedPtr<FJsonValue>& OutputVal : *Outputs)
					{
						const TSharedPtr<FJsonObject>* OutputObj;
						if (OutputVal->TryGetObject(OutputObj))
						{
							FuncDef.Outputs.Add(ParseFunctionParam(*OutputObj));
						}
					}
				}

				FString Result = AddFunction(Blueprint, FuncDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_functions
	const TArray<TSharedPtr<FJsonValue>>* RemoveFunctions;
	if (Args->TryGetArrayField(TEXT("remove_functions"), RemoveFunctions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveFunctions)
		{
			FString FuncName;
			if (Value->TryGetString(FuncName))
			{
				FString Result = RemoveFunction(Blueprint, FuncName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process rename_functions (works for both functions and macros)
	const TArray<TSharedPtr<FJsonValue>>* RenameFunctions;
	if (Args->TryGetArrayField(TEXT("rename_functions"), RenameFunctions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RenameFunctions)
		{
			const TSharedPtr<FJsonObject>* RenObj;
			if (Value->TryGetObject(RenObj))
			{
				FString FuncName, NewName;
				(*RenObj)->TryGetStringField(TEXT("name"), FuncName);
				(*RenObj)->TryGetStringField(TEXT("new_name"), NewName);

				FString Result = RenameFunction(Blueprint, FuncName, NewName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
			}
		}
	}

	// Process add_macros
	const TArray<TSharedPtr<FJsonValue>>* AddMacros;
	if (Args->TryGetArrayField(TEXT("add_macros"), AddMacros))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddMacros)
		{
			const TSharedPtr<FJsonObject>* MacroObj;
			if (Value->TryGetObject(MacroObj))
			{
				FFunctionDefinition MacroDef;
				(*MacroObj)->TryGetStringField(TEXT("name"), MacroDef.Name);
				(*MacroObj)->TryGetStringField(TEXT("category"), MacroDef.Category);

				const TArray<TSharedPtr<FJsonValue>>* Inputs;
				if ((*MacroObj)->TryGetArrayField(TEXT("inputs"), Inputs))
				{
					for (const TSharedPtr<FJsonValue>& InputVal : *Inputs)
					{
						const TSharedPtr<FJsonObject>* InputObj;
						if (InputVal->TryGetObject(InputObj))
						{
							MacroDef.Inputs.Add(ParseFunctionParam(*InputObj));
						}
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Outputs;
				if ((*MacroObj)->TryGetArrayField(TEXT("outputs"), Outputs))
				{
					for (const TSharedPtr<FJsonValue>& OutputVal : *Outputs)
					{
						const TSharedPtr<FJsonObject>* OutputObj;
						if (OutputVal->TryGetObject(OutputObj))
						{
							MacroDef.Outputs.Add(ParseFunctionParam(*OutputObj));
						}
					}
				}

				FString Result = AddMacro(Blueprint, MacroDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_macros
	const TArray<TSharedPtr<FJsonValue>>* RemoveMacros;
	if (Args->TryGetArrayField(TEXT("remove_macros"), RemoveMacros))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveMacros)
		{
			FString MacroName;
			if (Value->TryGetString(MacroName))
			{
				FString Result = RemoveMacro(Blueprint, MacroName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process override_functions
	const TArray<TSharedPtr<FJsonValue>>* OverrideFunctions;
	if (Args->TryGetArrayField(TEXT("override_functions"), OverrideFunctions))
	{
		for (const TSharedPtr<FJsonValue>& Value : *OverrideFunctions)
		{
			FString FuncName;
			if (Value->TryGetString(FuncName))
			{
				FString Result = OverrideFunction(Blueprint, FuncName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process add_event_graphs
	const TArray<TSharedPtr<FJsonValue>>* AddEventGraphs;
	if (Args->TryGetArrayField(TEXT("add_event_graphs"), AddEventGraphs))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddEventGraphs)
		{
			FString GraphName;
			if (Value->TryGetString(GraphName))
			{
				FString Result = AddEventGraph(Blueprint, GraphName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process reparent (Blueprint-level: change parent class)
	FString ReparentClass;
	if (Args->TryGetStringField(TEXT("reparent"), ReparentClass) && !ReparentClass.IsEmpty())
	{
		FString Result = ReparentBlueprint(Blueprint, ReparentClass);
		Results.Add(Result);
		if (Result.StartsWith(TEXT("~"))) ModifiedCount++;
	}

	// Process add_interfaces
	const TArray<TSharedPtr<FJsonValue>>* AddInterfaces;
	if (Args->TryGetArrayField(TEXT("add_interfaces"), AddInterfaces))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddInterfaces)
		{
			FString InterfaceName;
			if (Value->TryGetString(InterfaceName))
			{
				FString Result = AddInterface(Blueprint, InterfaceName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_interfaces
	const TArray<TSharedPtr<FJsonValue>>* RemoveInterfaces;
	if (Args->TryGetArrayField(TEXT("remove_interfaces"), RemoveInterfaces))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveInterfaces)
		{
			FString InterfaceName;
			bool bPreserve = false;

			// Support object format: {name, preserve_functions}
			const TSharedPtr<FJsonObject>* IntObj;
			if (Value->TryGetObject(IntObj))
			{
				(*IntObj)->TryGetStringField(TEXT("name"), InterfaceName);
				(*IntObj)->TryGetBoolField(TEXT("preserve_functions"), bPreserve);
			}
			else
			{
				Value->TryGetString(InterfaceName);
			}

			if (!InterfaceName.IsEmpty())
			{
				FString Result = RemoveInterface(Blueprint, InterfaceName, bPreserve);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_events
	const TArray<TSharedPtr<FJsonValue>>* AddEvents;
	if (Args->TryGetArrayField(TEXT("add_events"), AddEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FEventDefinition EventDef;
				(*EventObj)->TryGetStringField(TEXT("name"), EventDef.Name);

				const TArray<TSharedPtr<FJsonValue>>* Params;
				if ((*EventObj)->TryGetArrayField(TEXT("params"), Params))
				{
					for (const TSharedPtr<FJsonValue>& ParamVal : *Params)
					{
						const TSharedPtr<FJsonObject>* ParamObj;
						if (ParamVal->TryGetObject(ParamObj))
						{
							EventDef.Params.Add(ParseFunctionParam(*ParamObj));
						}
					}
				}

				FString Result = AddEvent(Blueprint, EventDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_events
	const TArray<TSharedPtr<FJsonValue>>* RemoveEvents;
	if (Args->TryGetArrayField(TEXT("remove_events"), RemoveEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveEvents)
		{
			FString EventName;
			if (Value->TryGetString(EventName))
			{
				FString Result = RemoveEvent(Blueprint, EventName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_custom_events (actual event nodes in EventGraph, can be RPCs)
	const TArray<TSharedPtr<FJsonValue>>* AddCustomEvents;
	if (Args->TryGetArrayField(TEXT("add_custom_events"), AddCustomEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddCustomEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FCustomEventDefinition EventDef;
				(*EventObj)->TryGetStringField(TEXT("name"), EventDef.Name);
				(*EventObj)->TryGetBoolField(TEXT("reliable"), EventDef.bReliable);
				(*EventObj)->TryGetBoolField(TEXT("call_in_editor"), EventDef.bCallInEditor);

				FString ReplicationStr;
				if ((*EventObj)->TryGetStringField(TEXT("replication"), ReplicationStr))
				{
					if (ReplicationStr.Equals(TEXT("Multicast"), ESearchCase::IgnoreCase))
					{
						EventDef.Replication = EEventReplication::Multicast;
					}
					else if (ReplicationStr.Equals(TEXT("Server"), ESearchCase::IgnoreCase))
					{
						EventDef.Replication = EEventReplication::Server;
					}
					else if (ReplicationStr.Equals(TEXT("Client"), ESearchCase::IgnoreCase))
					{
						EventDef.Replication = EEventReplication::Client;
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Params;
				if ((*EventObj)->TryGetArrayField(TEXT("params"), Params))
				{
					for (const TSharedPtr<FJsonValue>& ParamVal : *Params)
					{
						const TSharedPtr<FJsonObject>* ParamObj;
						if (ParamVal->TryGetObject(ParamObj))
						{
							EventDef.Params.Add(ParseFunctionParam(*ParamObj));
						}
					}
				}

				FString Result = AddCustomEvent(Blueprint, EventDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_custom_events
	const TArray<TSharedPtr<FJsonValue>>* RemoveCustomEvents;
	if (Args->TryGetArrayField(TEXT("remove_custom_events"), RemoveCustomEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveCustomEvents)
		{
			FString EventName;
			if (Value->TryGetString(EventName))
			{
				FString Result = RemoveCustomEvent(Blueprint, EventName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process widget operations (only for Widget Blueprints)
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);

	// Process list_events - discover available events on a component/widget
	FString ListEventsSource;
	if (Args->TryGetStringField(TEXT("list_events"), ListEventsSource) && !ListEventsSource.IsEmpty())
	{
		FString EventsOutput = ListEvents(Blueprint, ListEventsSource);
		Results.Add(EventsOutput);
	}

	// Process bind_events
	const TArray<TSharedPtr<FJsonValue>>* BindEvents;
	if (Args->TryGetArrayField(TEXT("bind_events"), BindEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *BindEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FEventBindingDef EventDef;
				(*EventObj)->TryGetStringField(TEXT("source"), EventDef.Source);
				(*EventObj)->TryGetStringField(TEXT("event"), EventDef.Event);
				(*EventObj)->TryGetStringField(TEXT("handler"), EventDef.Handler);

				FString Result = BindEvent(Blueprint, EventDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process unbind_events
	const TArray<TSharedPtr<FJsonValue>>* UnbindEvents;
	if (Args->TryGetArrayField(TEXT("unbind_events"), UnbindEvents))
	{
		for (const TSharedPtr<FJsonValue>& Value : *UnbindEvents)
		{
			const TSharedPtr<FJsonObject>* EventObj;
			if (Value->TryGetObject(EventObj))
			{
				FString Source, Event;
				(*EventObj)->TryGetStringField(TEXT("source"), Source);
				(*EventObj)->TryGetStringField(TEXT("event"), Event);

				FString Result = UnbindEvent(Blueprint, Source, Event);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Process add_widgets
	const TArray<TSharedPtr<FJsonValue>>* AddWidgets;
	if (Args->TryGetArrayField(TEXT("add_widgets"), AddWidgets))
	{
		if (!WidgetBlueprint)
		{
			Results.Add(TEXT("! Widgets: Not a Widget Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddWidgets)
			{
				const TSharedPtr<FJsonObject>* WidgetObj;
				if (Value->TryGetObject(WidgetObj))
				{
					FWidgetDefinition WidgetDef;
					(*WidgetObj)->TryGetStringField(TEXT("type"), WidgetDef.Type);
					(*WidgetObj)->TryGetStringField(TEXT("name"), WidgetDef.Name);
					(*WidgetObj)->TryGetStringField(TEXT("parent"), WidgetDef.Parent);

					FString Result = AddWidget(WidgetBlueprint, WidgetDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process remove_widgets
	const TArray<TSharedPtr<FJsonValue>>* RemoveWidgets;
	if (Args->TryGetArrayField(TEXT("remove_widgets"), RemoveWidgets))
	{
		if (!WidgetBlueprint)
		{
			Results.Add(TEXT("! Widgets: Not a Widget Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *RemoveWidgets)
			{
				FString WidgetName;
				if (Value->TryGetString(WidgetName))
				{
					FString Result = RemoveWidget(WidgetBlueprint, WidgetName);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("-"))) RemovedCount++;
				}
			}
		}
	}

	// Process configure_widgets
	const TArray<TSharedPtr<FJsonValue>>* ConfigureWidgets;
	if (Args->TryGetArrayField(TEXT("configure_widgets"), ConfigureWidgets))
	{
		if (!WidgetBlueprint)
		{
			Results.Add(TEXT("! Configure: Not a Widget Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *ConfigureWidgets)
			{
				const TSharedPtr<FJsonObject>* ConfigObj;
				if (Value->TryGetObject(ConfigObj))
				{
					FWidgetConfigDefinition ConfigDef;
					(*ConfigObj)->TryGetStringField(TEXT("name"), ConfigDef.Name);

					const TSharedPtr<FJsonObject>* PropsObj;
					if ((*ConfigObj)->TryGetObjectField(TEXT("properties"), PropsObj))
					{
						ConfigDef.Properties = *PropsObj;
					}

					const TSharedPtr<FJsonObject>* SlotObj;
					if ((*ConfigObj)->TryGetObjectField(TEXT("slot"), SlotObj))
					{
						ConfigDef.Slot = *SlotObj;
					}

					FString Result = ConfigureWidget(WidgetBlueprint, ConfigDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("*"))) AddedCount++; // * for modified
				}
			}
		}
	}

	// Timeline operations
	const TArray<TSharedPtr<FJsonValue>>* AddTimelines;
	if (Args->TryGetArrayField(TEXT("add_timelines"), AddTimelines))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddTimelines)
		{
			const TSharedPtr<FJsonObject>* TimelineObj;
			if (Value->TryGetObject(TimelineObj))
			{
				FTimelineDefinition TimelineDef;
				(*TimelineObj)->TryGetStringField(TEXT("name"), TimelineDef.Name);
				(*TimelineObj)->TryGetNumberField(TEXT("length"), TimelineDef.Length);
				(*TimelineObj)->TryGetBoolField(TEXT("auto_play"), TimelineDef.bAutoPlay);
				(*TimelineObj)->TryGetBoolField(TEXT("loop"), TimelineDef.bLoop);
				(*TimelineObj)->TryGetBoolField(TEXT("replicated"), TimelineDef.bReplicated);
				(*TimelineObj)->TryGetBoolField(TEXT("ignore_time_dilation"), TimelineDef.bIgnoreTimeDilation);

				const TArray<TSharedPtr<FJsonValue>>* Tracks;
				if ((*TimelineObj)->TryGetArrayField(TEXT("tracks"), Tracks))
				{
					for (const TSharedPtr<FJsonValue>& TrackVal : *Tracks)
					{
						const TSharedPtr<FJsonObject>* TrackObj;
						if (TrackVal->TryGetObject(TrackObj))
						{
							FTimelineTrackDef TrackDef;
							(*TrackObj)->TryGetStringField(TEXT("name"), TrackDef.Name);
							(*TrackObj)->TryGetStringField(TEXT("external_curve"), TrackDef.ExternalCurve);

							FString TypeStr;
							if ((*TrackObj)->TryGetStringField(TEXT("type"), TypeStr))
							{
								if (TypeStr.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
								{
									TrackDef.Type = ETimelineTrackType::Vector;
								}
								else if (TypeStr.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Color"), ESearchCase::IgnoreCase))
								{
									TrackDef.Type = ETimelineTrackType::LinearColor;
								}
								else if (TypeStr.Equals(TEXT("Event"), ESearchCase::IgnoreCase))
								{
									TrackDef.Type = ETimelineTrackType::Event;
								}
							}

							const TArray<TSharedPtr<FJsonValue>>* Keyframes;
							if ((*TrackObj)->TryGetArrayField(TEXT("keyframes"), Keyframes))
							{
								for (const TSharedPtr<FJsonValue>& KeyVal : *Keyframes)
								{
									const TSharedPtr<FJsonObject>* KeyObj;
									if (KeyVal->TryGetObject(KeyObj))
									{
										FTimelineKeyframe Keyframe;
										(*KeyObj)->TryGetNumberField(TEXT("time"), Keyframe.Time);
										(*KeyObj)->TryGetNumberField(TEXT("value"), Keyframe.Value);
										(*KeyObj)->TryGetStringField(TEXT("interp_mode"), Keyframe.InterpMode);

										// Vector components
										double X = 0, Y = 0, Z = 0;
										if ((*KeyObj)->TryGetNumberField(TEXT("x"), X)) Keyframe.VectorValue.X = X;
										if ((*KeyObj)->TryGetNumberField(TEXT("y"), Y)) Keyframe.VectorValue.Y = Y;
										if ((*KeyObj)->TryGetNumberField(TEXT("z"), Z)) Keyframe.VectorValue.Z = Z;

										// Color components
										double R = 1, G = 1, B = 1, A = 1;
										if ((*KeyObj)->TryGetNumberField(TEXT("r"), R)) Keyframe.ColorValue.R = R;
										if ((*KeyObj)->TryGetNumberField(TEXT("g"), G)) Keyframe.ColorValue.G = G;
										if ((*KeyObj)->TryGetNumberField(TEXT("b"), B)) Keyframe.ColorValue.B = B;
										if ((*KeyObj)->TryGetNumberField(TEXT("a"), A)) Keyframe.ColorValue.A = A;

										TrackDef.Keyframes.Add(Keyframe);
									}
								}
							}

							TimelineDef.Tracks.Add(TrackDef);
						}
					}
				}

				FString Result = AddTimeline(Blueprint, TimelineDef);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("+"))) AddedCount++;
			}
		}
	}

	// Process remove_timelines
	const TArray<TSharedPtr<FJsonValue>>* RemoveTimelines;
	if (Args->TryGetArrayField(TEXT("remove_timelines"), RemoveTimelines))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveTimelines)
		{
			FString TimelineName;
			if (Value->TryGetString(TimelineName))
			{
				FString Result = RemoveTimeline(Blueprint, TimelineName);
				Results.Add(Result);
				if (Result.StartsWith(TEXT("-"))) RemovedCount++;
			}
		}
	}

	// Animation Blueprint operations
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Blueprint);

	// Process add_state_machine
	const TArray<TSharedPtr<FJsonValue>>* AddStateMachines;
	if (Args->TryGetArrayField(TEXT("add_state_machine"), AddStateMachines))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! StateMachine: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddStateMachines)
			{
				const TSharedPtr<FJsonObject>* SMObj;
				if (Value->TryGetObject(SMObj))
				{
					FStateMachineDefinition SMDef;
					(*SMObj)->TryGetStringField(TEXT("name"), SMDef.Name);

					FString Result = AddStateMachine(AnimBlueprint, SMDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process add_anim_state
	const TArray<TSharedPtr<FJsonValue>>* AddAnimStates;
	if (Args->TryGetArrayField(TEXT("add_anim_state"), AddAnimStates))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! AnimState: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddAnimStates)
			{
				const TSharedPtr<FJsonObject>* StateObj;
				if (Value->TryGetObject(StateObj))
				{
					FAnimStateDefinition StateDef;
					(*StateObj)->TryGetStringField(TEXT("name"), StateDef.Name);
					(*StateObj)->TryGetStringField(TEXT("state_machine"), StateDef.StateMachine);

					FString Result = AddAnimState(AnimBlueprint, StateDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process add_state_transition
	const TArray<TSharedPtr<FJsonValue>>* AddTransitions;
	if (Args->TryGetArrayField(TEXT("add_state_transition"), AddTransitions))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! Transition: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddTransitions)
			{
				const TSharedPtr<FJsonObject>* TransObj;
				if (Value->TryGetObject(TransObj))
				{
					FStateTransitionDefinition TransDef;
					(*TransObj)->TryGetStringField(TEXT("state_machine"), TransDef.StateMachine);
					(*TransObj)->TryGetStringField(TEXT("from_state"), TransDef.FromState);
					(*TransObj)->TryGetStringField(TEXT("to_state"), TransDef.ToState);

					FString Result = AddStateTransition(AnimBlueprint, TransDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Process add_linked_anim_layers
	const TArray<TSharedPtr<FJsonValue>>* AddLinkedLayers;
	if (Args->TryGetArrayField(TEXT("add_linked_anim_layers"), AddLinkedLayers))
	{
		if (!AnimBlueprint)
		{
			Results.Add(TEXT("! LinkedAnimLayer: Not an Animation Blueprint"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddLinkedLayers)
			{
				const TSharedPtr<FJsonObject>* LayerObj;
				if (Value->TryGetObject(LayerObj))
				{
					FLinkedAnimLayerDefinition LayerDef;
					(*LayerObj)->TryGetStringField(TEXT("layer_name"), LayerDef.LayerName);
					(*LayerObj)->TryGetStringField(TEXT("interface"), LayerDef.Interface);

					FString Result = AddLinkedAnimLayer(AnimBlueprint, LayerDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}
	}

	// Single recompilation point — all removal operations above skip per-operation recompile.
	// During PIE, defer compilation to avoid FRepLayout stale pointer crashes.
	if (GEditor && GEditor->IsPlayingSessionInEditor())
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
	else
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	}

	// Build output
	FString Output = FString::Printf(TEXT("# EDIT %s at %s\n"), *Name, *Path);
	for (const FString& R : Results)
	{
		Output += R + TEXT("\n");
	}
	Output += FString::Printf(TEXT("= %d added, %d removed, %d modified\n"), AddedCount, RemovedCount, ModifiedCount);

	return FToolResult::Ok(Output);
}

FEditBlueprintTool::FTypeDefinition FEditBlueprintTool::ParseTypeDefinition(const TSharedPtr<FJsonObject>& TypeObj, int32 Depth)
{
	FTypeDefinition TypeDef;

	// Prevent stack overflow from deeply nested/malicious JSON input
	static constexpr int32 MaxTypeNestingDepth = 10;
	if (Depth > MaxTypeNestingDepth || !TypeObj.IsValid())
	{
		return TypeDef;
	}

	TypeObj->TryGetStringField(TEXT("base"), TypeDef.Base);
	TypeObj->TryGetStringField(TEXT("container"), TypeDef.Container);
	TypeObj->TryGetStringField(TEXT("subtype"), TypeDef.Subtype);

	if (TypeDef.Container.IsEmpty())
	{
		TypeDef.Container = TEXT("Single");
	}

	const TSharedPtr<FJsonObject>* KeyTypeObj;
	if (TypeObj->TryGetObjectField(TEXT("key_type"), KeyTypeObj))
	{
		TypeDef.KeyType = MakeShared<FTypeDefinition>(ParseTypeDefinition(*KeyTypeObj, Depth + 1));
	}

	return TypeDef;
}

FEditBlueprintTool::FFunctionParam FEditBlueprintTool::ParseFunctionParam(const TSharedPtr<FJsonObject>& ParamObj)
{
	FFunctionParam Param;
	ParamObj->TryGetStringField(TEXT("name"), Param.Name);

	// Try object format first: {"type": {"base": "Float"}}
	const TSharedPtr<FJsonObject>* TypeObj;
	if (ParamObj->TryGetObjectField(TEXT("type"), TypeObj))
	{
		Param.Type = ParseTypeDefinition(*TypeObj);
	}
	else
	{
		// Try simple string format: {"type": "Float"} or {"type": "Vector"}
		FString TypeStr;
		if (ParamObj->TryGetStringField(TEXT("type"), TypeStr))
		{
			Param.Type.Base = TypeStr;
			Param.Type.Container = TEXT("Single");
		}
	}

	return Param;
}

bool FEditBlueprintTool::TypeDefinitionToPinType(const FTypeDefinition& TypeDef, FEdGraphPinType& OutPinType, FString& OutError)
{
	OutPinType = FEdGraphPinType();

	// Set container type
	if (TypeDef.Container.Equals(TEXT("Array"), ESearchCase::IgnoreCase))
	{
		OutPinType.ContainerType = EPinContainerType::Array;
	}
	else if (TypeDef.Container.Equals(TEXT("Set"), ESearchCase::IgnoreCase))
	{
		OutPinType.ContainerType = EPinContainerType::Set;
	}
	else if (TypeDef.Container.Equals(TEXT("Map"), ESearchCase::IgnoreCase))
	{
		OutPinType.ContainerType = EPinContainerType::Map;
		if (TypeDef.KeyType.IsValid())
		{
			FEdGraphPinType KeyPinType;
			FString KeyError;
			if (!TypeDefinitionToPinType(*TypeDef.KeyType, KeyPinType, KeyError))
			{
				OutError = FString::Printf(TEXT("Map key type error: %s"), *KeyError);
				return false;
			}
			OutPinType.PinValueType = FEdGraphTerminalType::FromPinType(KeyPinType);
		}
	}

	// Normalize common type name variations
	FString Base = TypeDef.Base;
	if (Base.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		Base = TEXT("Boolean");
	}
	else if (Base.Equals(TEXT("real"), ESearchCase::IgnoreCase) ||
			 Base.Equals(TEXT("double"), ESearchCase::IgnoreCase))
	{
		Base = TEXT("Float");
	}
	else if (Base.Equals(TEXT("int"), ESearchCase::IgnoreCase) ||
			 Base.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
	{
		Base = TEXT("Integer");
	}
	else if (Base.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
	{
		Base = TEXT("Integer64");
	}

	if (Base.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Base.Equals(TEXT("Byte"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (Base.Equals(TEXT("Integer"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Base.Equals(TEXT("Integer64"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Base.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutPinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (Base.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Base.Equals(TEXT("String"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Base.Equals(TEXT("Text"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Base.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (Base.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (Base.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutPinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (Base.Equals(TEXT("Structure"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindStructByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown struct type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
	}
	else if (Base.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown class type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
		else
		{
			OutPinType.PinSubCategoryObject = UObject::StaticClass();
		}
	}
	else if (Base.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown class type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
		else
		{
			OutPinType.PinSubCategoryObject = UObject::StaticClass();
		}
	}
	else if (Base.Equals(TEXT("SoftObject"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown class type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
	}
	else if (Base.Equals(TEXT("SoftClass"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown class type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
	}
	else if (Base.Equals(TEXT("Interface"), ESearchCase::IgnoreCase))
	{
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Interface;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindClassByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown interface type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
	}
	else if (Base.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		// Engine uses PC_Byte for all enum types (see EdGraphSchema_K2::ConvertPropertyToPinType)
		OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		if (!TypeDef.Subtype.IsEmpty())
		{
			OutPinType.PinSubCategoryObject = FindEnumByName(TypeDef.Subtype);
			if (!OutPinType.PinSubCategoryObject.IsValid())
			{
				OutError = FString::Printf(TEXT("Unknown enum type '%s'"), *TypeDef.Subtype);
				return false;
			}
		}
	}
	else
	{
		// Try to find the type by name - could be a struct, class, or enum
		if (UScriptStruct* FoundStruct = FindStructByName(Base))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			OutPinType.PinSubCategoryObject = FoundStruct;
		}
		else if (UClass* FoundClass = FindClassByName(Base))
		{
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			OutPinType.PinSubCategoryObject = FoundClass;
		}
		else if (UEnum* FoundEnum = FindEnumByName(Base))
		{
			// Engine uses PC_Byte for all enum types (see EdGraphSchema_K2::ConvertPropertyToPinType)
			OutPinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			OutPinType.PinSubCategoryObject = FoundEnum;
		}
		else
		{
			OutError = FString::Printf(TEXT("Unknown type '%s'. Use base types (Boolean, Integer, Float, String, etc.) or specify Object/Structure/Enum with subtype."), *TypeDef.Base);
			return false;
		}
	}

	return true;
}

UClass* FEditBlueprintTool::FindClassByName(const FString& ClassName)
{
	// Try common prefixes
	TArray<FString> SearchNames;
	SearchNames.Add(ClassName);
	SearchNames.Add(TEXT("A") + ClassName); // AActor, ACharacter
	SearchNames.Add(TEXT("U") + ClassName); // UObject, UComponent

	for (const FString& SearchName : SearchNames)
	{
		// Try to find native class
		UClass* FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/Engine.%s"), *SearchName));
		if (FoundClass) return FoundClass;

		FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/CoreUObject.%s"), *SearchName));
		if (FoundClass) return FoundClass;

		// Search all classes
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	// If the input looks like an asset path (starts with /), try loading it directly
	if (ClassName.StartsWith(TEXT("/")))
	{
		// Agent may send a full path - try loading it as-is
		FString DirectPath = ClassName;

		// Ensure it has the .AssetName suffix required by LoadObject
		if (!DirectPath.Contains(TEXT(".")))
		{
			FString AssetName = FPaths::GetCleanFilename(DirectPath);
			DirectPath = FString::Printf(TEXT("%s.%s"), *DirectPath, *AssetName);
		}

		UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *DirectPath);
		if (BP && BP->GeneratedClass)
		{
			return BP->GeneratedClass;
		}

		// Also try stripping path to just the asset name and searching by name
		FString JustName = FPaths::GetCleanFilename(ClassName);
		if (JustName != ClassName)
		{
			return FindClassByName(JustName);
		}

		return nullptr;
	}

	// Try loading as Blueprint from conventional path
	FString BPPath = FString::Printf(TEXT("/Game/Blueprints/%s.%s"), *ClassName, *ClassName);
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath);
	if (BP && BP->GeneratedClass)
	{
		return BP->GeneratedClass;
	}

	// Search asset registry for Blueprint
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UBlueprint::StaticClass()->GetClassPathName(), Assets);

	for (const FAssetData& Asset : Assets)
	{
		if (Asset.AssetName.ToString().Equals(ClassName, ESearchCase::IgnoreCase))
		{
			UBlueprint* FoundBP = Cast<UBlueprint>(Asset.GetAsset());
			if (FoundBP && FoundBP->GeneratedClass)
			{
				return FoundBP->GeneratedClass;
			}
		}
	}

	return nullptr;
}

UScriptStruct* FEditBlueprintTool::FindStructByName(const FString& StructName)
{
	FString SearchName = StructName;
	if (!SearchName.StartsWith(TEXT("F")))
	{
		SearchName = TEXT("F") + StructName;
	}

	// Search common modules
	TArray<FString> Modules = { TEXT("Engine"), TEXT("CoreUObject"), TEXT("InputCore"), TEXT("SlateCore") };

	for (const FString& Module : Modules)
	{
		FString Path = FString::Printf(TEXT("/Script/%s.%s"), *Module, *SearchName);
		UScriptStruct* Found = FindObject<UScriptStruct>(nullptr, *Path);
		if (Found) return Found;
	}

	// Search all structs
	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(StructName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	return nullptr;
}

UEnum* FEditBlueprintTool::FindEnumByName(const FString& EnumName)
{
	FString SearchName = EnumName;
	if (!SearchName.StartsWith(TEXT("E")))
	{
		SearchName = TEXT("E") + EnumName;
	}

	// Search all enums
	for (TObjectIterator<UEnum> It; It; ++It)
	{
		if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
			It->GetName().Equals(EnumName, ESearchCase::IgnoreCase))
		{
			return *It;
		}
	}

	return nullptr;
}

FString FEditBlueprintTool::AddVariable(UBlueprint* Blueprint, const FVariableDefinition& VarDef)
{
	if (VarDef.Name.IsEmpty())
	{
		return TEXT("! Variable: Missing name");
	}

	FName VarName = FName(*VarDef.Name);

	// Create pin type
	FEdGraphPinType PinType;
	FString TypeError;
	if (!TypeDefinitionToPinType(VarDef.Type, PinType, TypeError))
	{
		return FString::Printf(TEXT("! Variable %s: %s"), *VarDef.Name, *TypeError);
	}

	// Local variable path
	if (!VarDef.Function.IsEmpty())
	{
		UEdGraph* FuncGraph = FindFunctionGraph(Blueprint, VarDef.Function);
		if (!FuncGraph)
		{
			return FString::Printf(TEXT("! Variable %s: Function '%s' not found"), *VarDef.Name, *VarDef.Function);
		}

		bool bSuccess = FBlueprintEditorUtils::AddLocalVariable(Blueprint, FuncGraph, VarName, PinType, VarDef.Default);
		if (!bSuccess)
		{
			return FString::Printf(TEXT("! Variable: Failed to add local variable %s"), *VarDef.Name);
		}

		FString TypeStr = VarDef.Type.Base;
		if (!VarDef.Type.Subtype.IsEmpty()) TypeStr += TEXT("<") + VarDef.Type.Subtype + TEXT(">");
		return FString::Printf(TEXT("+ Local Variable: %s (%s) in %s"), *VarDef.Name, *TypeStr, *VarDef.Function);
	}

	// Member variable path
	// Check if variable already exists
	for (const FBPVariableDescription& Existing : Blueprint->NewVariables)
	{
		if (Existing.VarName == VarName)
		{
			return FString::Printf(TEXT("! Variable: %s already exists"), *VarDef.Name);
		}
	}

	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, VarName, PinType);
	if (!bSuccess)
	{
		return FString::Printf(TEXT("! Variable: Failed to add %s"), *VarDef.Name);
	}

	// Find the variable we just added and set properties
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			// Set category
			if (!VarDef.Category.IsEmpty())
			{
				Var.Category = FText::FromString(VarDef.Category);
			}

			// Set flags directly on the description
			if (VarDef.bReplicated)
			{
				Var.PropertyFlags |= CPF_Net;
			}
			if (VarDef.bRepNotify)
			{
				Var.PropertyFlags |= CPF_Net | CPF_RepNotify;
				Var.ReplicationCondition = COND_None;
				FString OnRepFuncName = FString::Printf(TEXT("OnRep_%s"), *VarDef.Name);
				Var.RepNotifyFunc = FName(*OnRepFuncName);

				// Create the OnRep function graph if it doesn't exist
				bool bFuncExists = false;
				for (UEdGraph* Graph : Blueprint->FunctionGraphs)
				{
					if (Graph && Graph->GetFName() == Var.RepNotifyFunc)
					{
						bFuncExists = true;
						break;
					}
				}
				if (!bFuncExists)
				{
					UEdGraph* RepFuncGraph = FBlueprintEditorUtils::CreateNewGraph(
						Blueprint,
						Var.RepNotifyFunc,
						UEdGraph::StaticClass(),
						UEdGraphSchema_K2::StaticClass()
					);
					if (RepFuncGraph)
					{
						FBlueprintEditorUtils::AddFunctionGraph(Blueprint, RepFuncGraph, true, static_cast<UFunction*>(nullptr));
					}
				}
			}
			if (VarDef.bExposeOnSpawn)
			{
				Var.PropertyFlags |= CPF_ExposeOnSpawn;
			}
			if (VarDef.bPrivate)
			{
				Var.PropertyFlags |= CPF_DisableEditOnInstance;
			}
			if (VarDef.bTransient)
			{
				Var.PropertyFlags |= CPF_Transient;
			}

			break;
		}
	}

	// Apply additional flags using engine utilities
	ApplyVariableFlags(Blueprint, VarName, VarDef.bSaveGame, VarDef.bAdvancedDisplay,
		VarDef.bDeprecated, VarDef.bInterp, VarDef.bReadOnly, VarDef.bBlueprintOnly);

	// Set default value if provided
	if (!VarDef.Default.IsEmpty())
	{
		SetVariableDefaultValue(Blueprint, VarDef.Name, VarDef.Default);
	}

	// Build result string
	FString TypeStr = VarDef.Type.Base;
	if (!VarDef.Type.Subtype.IsEmpty())
	{
		TypeStr += TEXT("<") + VarDef.Type.Subtype + TEXT(">");
	}
	if (!VarDef.Type.Container.Equals(TEXT("Single"), ESearchCase::IgnoreCase))
	{
		TypeStr = VarDef.Type.Container + TEXT("<") + TypeStr + TEXT(">");
	}

	FString Flags;
	if (VarDef.bReplicated) Flags += TEXT(" [Replicated]");
	if (VarDef.bRepNotify) Flags += TEXT(" [RepNotify]");
	if (VarDef.bExposeOnSpawn) Flags += TEXT(" [ExposeOnSpawn]");
	if (VarDef.bSaveGame) Flags += TEXT(" [SaveGame]");
	if (VarDef.bInterp) Flags += TEXT(" [Interp]");
	if (VarDef.bDeprecated) Flags += TEXT(" [Deprecated]");

	FString DefaultStr;
	if (!VarDef.Default.IsEmpty())
	{
		DefaultStr = FString::Printf(TEXT(" = %s"), *VarDef.Default);
	}

	return FString::Printf(TEXT("+ Variable: %s (%s)%s%s"), *VarDef.Name, *TypeStr, *DefaultStr, *Flags);
}

FString FEditBlueprintTool::RemoveVariable(UBlueprint* Blueprint, const FString& VarName, const FString& Function)
{
	FName Name = FName(*VarName);

	// Local variable path
	if (!Function.IsEmpty())
	{
		UEdGraph* FuncGraph = FindFunctionGraph(Blueprint, Function);
		if (!FuncGraph)
		{
			return FString::Printf(TEXT("! Variable %s: Function '%s' not found"), *VarName, *Function);
		}

		UStruct* Scope = GetFunctionScope(Blueprint, FuncGraph);
		if (!Scope)
		{
			return FString::Printf(TEXT("! Variable %s: Could not resolve scope for function '%s'"), *VarName, *Function);
		}

		FBPVariableDescription* LocalVar = FBlueprintEditorUtils::FindLocalVariable(Blueprint, Scope, Name);
		if (!LocalVar)
		{
			return FString::Printf(TEXT("! Local Variable: %s not found in %s"), *VarName, *Function);
		}

		// Manual local variable removal WITHOUT per-variable recompile.
		// RemoveLocalVariable() calls MarkBlueprintAsStructurallyModified() internally.
		// We replicate its logic but skip the recompile — finalization handles it once.
		TArray<UK2Node_FunctionEntry*> EntryNodes;
		FuncGraph->GetNodesOfClass<UK2Node_FunctionEntry>(EntryNodes);
		if (EntryNodes.Num() > 0)
		{
			for (int32 VarIdx = EntryNodes[0]->LocalVariables.Num() - 1; VarIdx >= 0; --VarIdx)
			{
				if (EntryNodes[0]->LocalVariables[VarIdx].VarName == Name)
				{
					EntryNodes[0]->LocalVariables.RemoveAt(VarIdx);
					FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, Name, true, FuncGraph);
					break;
				}
			}
		}
		return FString::Printf(TEXT("- Local Variable: %s from %s"), *VarName, *Function);
	}

	// Member variable path — manual removal WITHOUT per-variable recompile.
	// RemoveMemberVariable() internally calls MarkBlueprintAsStructurallyModified() which triggers
	// a full skeleton recompile. When removing N variables in a batch, that causes N recompiles
	// where each intermediate recompile can invalidate FProperty pointers (via PurgeClass) while
	// FRepLayout still holds references to them → crash. Instead, we do the removal manually
	// and let the single finalization MarkBlueprintAsStructurallyModified() handle recompilation.
	int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(Blueprint, Name);
	if (VarIndex != INDEX_NONE)
	{
		Blueprint->Modify();
		Blueprint->NewVariables.RemoveAt(VarIndex);
		FBlueprintEditorUtils::RemoveVariableNodes(Blueprint, Name);
		return FString::Printf(TEXT("- Variable: %s"), *VarName);
	}

	return FString::Printf(TEXT("! Variable: %s not found"), *VarName);
}

void FEditBlueprintTool::SetVariableDefaultValue(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValue)
{
	// Set default on the variable description (pre-compilation safe)
	FName VarFName = FName(*VarName);
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarFName)
		{
			Var.DefaultValue = DefaultValue;
			return;
		}
	}
}

UEdGraph* FEditBlueprintTool::FindFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (!Blueprint || FunctionName.IsEmpty())
	{
		return nullptr;
	}

	FName FuncName = FName(*FunctionName);

	// Search function graphs
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FuncName)
		{
			return Graph;
		}
	}

	// Also check by display name (case-insensitive)
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetName().Equals(FunctionName, ESearchCase::IgnoreCase))
		{
			return Graph;
		}
	}

	return nullptr;
}

UStruct* FEditBlueprintTool::GetFunctionScope(UBlueprint* Blueprint, UEdGraph* FunctionGraph)
{
	if (!Blueprint || !FunctionGraph)
	{
		return nullptr;
	}

	// Find the FunctionEntry node to get the UFunction scope
	for (UEdGraphNode* Node : FunctionGraph->Nodes)
	{
		UK2Node_FunctionEntry* EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode)
		{
			// The scope is the UFunction generated from this graph
			if (Blueprint->SkeletonGeneratedClass)
			{
				UFunction* Func = Blueprint->SkeletonGeneratedClass->FindFunctionByName(FunctionGraph->GetFName());
				if (Func)
				{
					return Func;
				}
			}
			break;
		}
	}

	return nullptr;
}

void FEditBlueprintTool::ApplyVariableFlags(UBlueprint* Blueprint, const FName& VarName,
	bool bSaveGame, bool bAdvancedDisplay, bool bDeprecated, bool bInterp, bool bReadOnly, bool bBlueprintOnly)
{
	if (bSaveGame)
	{
		FBlueprintEditorUtils::SetVariableSaveGameFlag(Blueprint, VarName, true);
	}
	if (bAdvancedDisplay)
	{
		FBlueprintEditorUtils::SetVariableAdvancedDisplayFlag(Blueprint, VarName, true);
	}
	if (bDeprecated)
	{
		FBlueprintEditorUtils::SetVariableDeprecatedFlag(Blueprint, VarName, true);
	}
	if (bInterp)
	{
		FBlueprintEditorUtils::SetInterpFlag(Blueprint, VarName, true);
	}
	if (bReadOnly)
	{
		FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VarName, true);
	}
	if (bBlueprintOnly)
	{
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VarName, true);
	}
}

FString FEditBlueprintTool::ModifyVariable(UBlueprint* Blueprint, const FVariableModification& ModDef)
{
	if (ModDef.Name.IsEmpty())
	{
		return TEXT("! ModifyVariable: Missing variable name");
	}

	FName VarName = FName(*ModDef.Name);
	TArray<FString> Changes;

	// Local variable path
	if (!ModDef.Function.IsEmpty())
	{
		UEdGraph* FuncGraph = FindFunctionGraph(Blueprint, ModDef.Function);
		if (!FuncGraph)
		{
			return FString::Printf(TEXT("! ModifyVariable %s: Function '%s' not found"), *ModDef.Name, *ModDef.Function);
		}

		UStruct* Scope = GetFunctionScope(Blueprint, FuncGraph);
		if (!Scope)
		{
			return FString::Printf(TEXT("! ModifyVariable %s: Could not resolve scope for '%s'. Try compiling the Blueprint first."), *ModDef.Name, *ModDef.Function);
		}

		// Verify the local variable exists
		FBPVariableDescription* LocalVar = FBlueprintEditorUtils::FindLocalVariable(Blueprint, Scope, VarName);
		if (!LocalVar)
		{
			return FString::Printf(TEXT("! ModifyVariable: Local variable '%s' not found in function '%s'"), *ModDef.Name, *ModDef.Function);
		}

		// Rename
		if (!ModDef.NewName.IsEmpty() && ModDef.NewName != ModDef.Name)
		{
			FName NewName = FName(*ModDef.NewName);
			FBlueprintEditorUtils::RenameLocalVariable(Blueprint, Scope, VarName, NewName);
			VarName = NewName;
			Changes.Add(FString::Printf(TEXT("renamed to '%s'"), *ModDef.NewName));
		}

		// Change type
		if (ModDef.NewType.IsValid())
		{
			FTypeDefinition TypeDef = ParseTypeDefinition(ModDef.NewType);
			FEdGraphPinType NewPinType;
			FString TypeError;
			if (TypeDefinitionToPinType(TypeDef, NewPinType, TypeError))
			{
				FBlueprintEditorUtils::ChangeLocalVariableType(Blueprint, Scope, VarName, NewPinType);
				Changes.Add(FString::Printf(TEXT("type changed to '%s'"), *TypeDef.Base));
			}
			else
			{
				Changes.Add(FString::Printf(TEXT("type change failed: %s"), *TypeError));
			}
		}

		// Default value
		if (!ModDef.Default.IsEmpty())
		{
			// Find the local variable again (name may have changed)
			FBPVariableDescription* UpdatedVar = FBlueprintEditorUtils::FindLocalVariable(Blueprint, Scope, VarName);
			if (UpdatedVar)
			{
				UpdatedVar->DefaultValue = ModDef.Default;
				Changes.Add(FString::Printf(TEXT("default set to '%s'"), *ModDef.Default));
			}
		}

		if (Changes.Num() == 0)
		{
			return FString::Printf(TEXT("! ModifyVariable: No changes specified for local variable '%s'"), *ModDef.Name);
		}

		return FString::Printf(TEXT("~ Local Variable %s: %s"), *ModDef.Name, *FString::Join(Changes, TEXT(", ")));
	}

	// Member variable path
	// Verify the variable exists
	bool bFound = false;
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			bFound = true;
			break;
		}
	}
	if (!bFound)
	{
		return FString::Printf(TEXT("! ModifyVariable: Variable '%s' not found"), *ModDef.Name);
	}

	// Rename (do this first since subsequent operations need the new name)
	if (!ModDef.NewName.IsEmpty() && ModDef.NewName != ModDef.Name)
	{
		FName NewName = FName(*ModDef.NewName);
		FBlueprintEditorUtils::RenameMemberVariable(Blueprint, VarName, NewName);
		VarName = NewName;
		Changes.Add(FString::Printf(TEXT("renamed to '%s'"), *ModDef.NewName));
	}

	// Change type
	if (ModDef.NewType.IsValid())
	{
		FTypeDefinition TypeDef = ParseTypeDefinition(ModDef.NewType);
		FEdGraphPinType NewPinType;
		FString TypeError;
		if (TypeDefinitionToPinType(TypeDef, NewPinType, TypeError))
		{
			FBlueprintEditorUtils::ChangeMemberVariableType(Blueprint, VarName, NewPinType);
			Changes.Add(FString::Printf(TEXT("type changed to '%s'"), *TypeDef.Base));
		}
		else
		{
			Changes.Add(FString::Printf(TEXT("type change failed: %s"), *TypeError));
		}
	}

	// Update flags on the variable description and via engine utils
	for (FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == VarName)
		{
			// Category
			if (!ModDef.Category.IsEmpty())
			{
				Var.Category = FText::FromString(ModDef.Category);
				Changes.Add(FString::Printf(TEXT("category set to '%s'"), *ModDef.Category));
			}

			// Default
			if (!ModDef.Default.IsEmpty())
			{
				Var.DefaultValue = ModDef.Default;
				Changes.Add(FString::Printf(TEXT("default set to '%s'"), *ModDef.Default));
			}

			// Direct property flag modifications
			if (ModDef.bReplicated.IsSet())
			{
				if (ModDef.bReplicated.GetValue())
				{
					Var.PropertyFlags |= CPF_Net;
				}
				else
				{
					Var.PropertyFlags &= ~CPF_Net;
				}
				Changes.Add(FString::Printf(TEXT("replicated=%s"), ModDef.bReplicated.GetValue() ? TEXT("true") : TEXT("false")));
			}

			if (ModDef.bRepNotify.IsSet())
			{
				if (ModDef.bRepNotify.GetValue())
				{
					Var.PropertyFlags |= CPF_Net | CPF_RepNotify;
					Var.ReplicationCondition = COND_None;
					FString OnRepFuncName = FString::Printf(TEXT("OnRep_%s"), *VarName.ToString());
					Var.RepNotifyFunc = FName(*OnRepFuncName);

					// Create OnRep function if needed
					bool bFuncExists = false;
					for (UEdGraph* Graph : Blueprint->FunctionGraphs)
					{
						if (Graph && Graph->GetFName() == Var.RepNotifyFunc)
						{
							bFuncExists = true;
							break;
						}
					}
					if (!bFuncExists)
					{
						UEdGraph* RepFuncGraph = FBlueprintEditorUtils::CreateNewGraph(
							Blueprint, Var.RepNotifyFunc, UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
						if (RepFuncGraph)
						{
							FBlueprintEditorUtils::AddFunctionGraph(Blueprint, RepFuncGraph, true, static_cast<UFunction*>(nullptr));
						}
					}
				}
				else
				{
					Var.PropertyFlags &= ~CPF_RepNotify;
					Var.RepNotifyFunc = NAME_None;
				}
				Changes.Add(FString::Printf(TEXT("rep_notify=%s"), ModDef.bRepNotify.GetValue() ? TEXT("true") : TEXT("false")));
			}

			if (ModDef.bExposeOnSpawn.IsSet())
			{
				if (ModDef.bExposeOnSpawn.GetValue())
				{
					Var.PropertyFlags |= CPF_ExposeOnSpawn;
				}
				else
				{
					Var.PropertyFlags &= ~CPF_ExposeOnSpawn;
				}
				Changes.Add(FString::Printf(TEXT("expose_on_spawn=%s"), ModDef.bExposeOnSpawn.GetValue() ? TEXT("true") : TEXT("false")));
			}

			if (ModDef.bPrivate.IsSet())
			{
				if (ModDef.bPrivate.GetValue())
				{
					Var.PropertyFlags |= CPF_DisableEditOnInstance;
				}
				else
				{
					Var.PropertyFlags &= ~CPF_DisableEditOnInstance;
				}
				Changes.Add(FString::Printf(TEXT("private=%s"), ModDef.bPrivate.GetValue() ? TEXT("true") : TEXT("false")));
			}

			break;
		}
	}

	// Flags that use engine utility functions (these handle additional bookkeeping)
	if (ModDef.bTransient.IsSet())
	{
		FBlueprintEditorUtils::SetVariableTransientFlag(Blueprint, VarName, ModDef.bTransient.GetValue());
		Changes.Add(FString::Printf(TEXT("transient=%s"), ModDef.bTransient.GetValue() ? TEXT("true") : TEXT("false")));
	}
	if (ModDef.bSaveGame.IsSet())
	{
		FBlueprintEditorUtils::SetVariableSaveGameFlag(Blueprint, VarName, ModDef.bSaveGame.GetValue());
		Changes.Add(FString::Printf(TEXT("save_game=%s"), ModDef.bSaveGame.GetValue() ? TEXT("true") : TEXT("false")));
	}
	if (ModDef.bAdvancedDisplay.IsSet())
	{
		FBlueprintEditorUtils::SetVariableAdvancedDisplayFlag(Blueprint, VarName, ModDef.bAdvancedDisplay.GetValue());
		Changes.Add(FString::Printf(TEXT("advanced_display=%s"), ModDef.bAdvancedDisplay.GetValue() ? TEXT("true") : TEXT("false")));
	}
	if (ModDef.bDeprecated.IsSet())
	{
		FBlueprintEditorUtils::SetVariableDeprecatedFlag(Blueprint, VarName, ModDef.bDeprecated.GetValue());
		Changes.Add(FString::Printf(TEXT("deprecated=%s"), ModDef.bDeprecated.GetValue() ? TEXT("true") : TEXT("false")));
	}
	if (ModDef.bInterp.IsSet())
	{
		FBlueprintEditorUtils::SetInterpFlag(Blueprint, VarName, ModDef.bInterp.GetValue());
		Changes.Add(FString::Printf(TEXT("interp=%s"), ModDef.bInterp.GetValue() ? TEXT("true") : TEXT("false")));
	}
	if (ModDef.bReadOnly.IsSet())
	{
		FBlueprintEditorUtils::SetBlueprintPropertyReadOnlyFlag(Blueprint, VarName, ModDef.bReadOnly.GetValue());
		Changes.Add(FString::Printf(TEXT("read_only=%s"), ModDef.bReadOnly.GetValue() ? TEXT("true") : TEXT("false")));
	}
	if (ModDef.bBlueprintOnly.IsSet())
	{
		FBlueprintEditorUtils::SetBlueprintOnlyEditableFlag(Blueprint, VarName, ModDef.bBlueprintOnly.GetValue());
		Changes.Add(FString::Printf(TEXT("blueprint_only=%s"), ModDef.bBlueprintOnly.GetValue() ? TEXT("true") : TEXT("false")));
	}

	if (Changes.Num() == 0)
	{
		return FString::Printf(TEXT("! ModifyVariable: No changes specified for '%s'"), *ModDef.Name);
	}

	return FString::Printf(TEXT("~ Variable %s: %s"), *ModDef.Name, *FString::Join(Changes, TEXT(", ")));
}

FString FEditBlueprintTool::AddComponent(UBlueprint* Blueprint, const FComponentDefinition& CompDef)
{
	if (CompDef.Name.IsEmpty() || CompDef.Class.IsEmpty())
	{
		return TEXT("! Component: Missing name or class");
	}

	// Components only work on Actor-based Blueprints (SCS requires AActor CDO)
	if (!Blueprint->ParentClass || !Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
	{
		return FString::Printf(TEXT("! Component: Cannot add components to non-Actor Blueprint (parent: %s). Components require an Actor-based Blueprint."),
			Blueprint->ParentClass ? *Blueprint->ParentClass->GetName() : TEXT("null"));
	}

	// Find component class
	UClass* ComponentClass = FindClassByName(CompDef.Class);
	if (!ComponentClass)
	{
		// Try with Component suffix
		ComponentClass = FindClassByName(CompDef.Class + TEXT("Component"));
	}
	if (!ComponentClass)
	{
		return FString::Printf(TEXT("! Component: Class not found: %s"), *CompDef.Class);
	}

	// Validate class inherits from UActorComponent (SCS CreateNode internally does check())
	if (!ComponentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		return FString::Printf(TEXT("! Component: %s is not an ActorComponent subclass"), *CompDef.Class);
	}

	// Ensure SCS exists
	if (!Blueprint->SimpleConstructionScript)
	{
		Blueprint->SimpleConstructionScript = NewObject<USimpleConstructionScript>(Blueprint);
	}

	// Check if component already exists
	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(CompDef.Name, ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT("! Component: %s already exists"), *CompDef.Name);
		}
	}

	// Create the node
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*CompDef.Name));
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Component: Failed to create %s"), *CompDef.Name);
	}

	// Find parent node
	USCS_Node* ParentNode = nullptr;
	if (!CompDef.Parent.IsEmpty())
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(CompDef.Parent, ESearchCase::IgnoreCase))
			{
				ParentNode = Node;
				break;
			}
		}
	}

	// Add to hierarchy
	if (ParentNode)
	{
		ParentNode->AddChildNode(NewNode);
	}
	else
	{
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	// Set properties if provided
	if (CompDef.Properties.IsValid() && NewNode->ComponentTemplate)
	{
		for (const auto& Pair : CompDef.Properties->Values)
		{
			SetComponentProperty(Blueprint, NewNode, Pair.Key, Pair.Value);
		}
	}

	FString ParentStr = CompDef.Parent.IsEmpty() ? TEXT("Root") : CompDef.Parent;
	return FString::Printf(TEXT("+ Component: %s (%s) -> %s"), *CompDef.Name, *CompDef.Class, *ParentStr);
}

FString FEditBlueprintTool::RemoveComponent(UBlueprint* Blueprint, const FString& CompName)
{
	if (!Blueprint->SimpleConstructionScript)
	{
		return FString::Printf(TEXT("! Component: %s not found"), *CompName);
	}

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(CompName, ESearchCase::IgnoreCase))
		{
			Blueprint->SimpleConstructionScript->RemoveNode(Node);
			return FString::Printf(TEXT("- Component: %s"), *CompName);
		}
	}

	return FString::Printf(TEXT("! Component: %s not found"), *CompName);
}

USCS_Node* FEditBlueprintTool::FindSCSNodeByName(UBlueprint* Blueprint, const FString& Name)
{
	if (!Blueprint || !Blueprint->SimpleConstructionScript || Name.IsEmpty())
	{
		return nullptr;
	}

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (Node && Node->GetVariableName().ToString().Equals(Name, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	return nullptr;
}

FString FEditBlueprintTool::RenameComponent(UBlueprint* Blueprint, const FString& OldName, const FString& NewName)
{
	if (OldName.IsEmpty() || NewName.IsEmpty())
	{
		return TEXT("! RenameComponent: Missing name or new_name");
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		return FString::Printf(TEXT("! RenameComponent: No SCS in Blueprint"));
	}

	USCS_Node* Node = FindSCSNodeByName(Blueprint, OldName);
	if (!Node)
	{
		return FString::Printf(TEXT("! RenameComponent: '%s' not found"), *OldName);
	}

	// Check new name doesn't conflict
	USCS_Node* Existing = FindSCSNodeByName(Blueprint, NewName);
	if (Existing && Existing != Node)
	{
		return FString::Printf(TEXT("! RenameComponent: '%s' already exists"), *NewName);
	}

	FName NewFName = FName(*NewName);
	FBlueprintEditorUtils::RenameComponentMemberVariable(Blueprint, Node, NewFName);

	return FString::Printf(TEXT("~ Component: '%s' renamed to '%s'"), *OldName, *NewName);
}

FString FEditBlueprintTool::DuplicateComponent(UBlueprint* Blueprint, const FString& SourceName, const FString& NewName)
{
	if (SourceName.IsEmpty())
	{
		return TEXT("! DuplicateComponent: Missing source name");
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		return FString::Printf(TEXT("! DuplicateComponent: No SCS in Blueprint"));
	}

	USCS_Node* SourceNode = FindSCSNodeByName(Blueprint, SourceName);
	if (!SourceNode || !SourceNode->ComponentTemplate)
	{
		return FString::Printf(TEXT("! DuplicateComponent: '%s' not found"), *SourceName);
	}

	// Generate name if not provided
	FString ActualNewName = NewName;
	if (ActualNewName.IsEmpty())
	{
		// Find unique name based on source
		int32 Suffix = 2;
		ActualNewName = SourceName + FString::FromInt(Suffix);
		while (FindSCSNodeByName(Blueprint, ActualNewName))
		{
			Suffix++;
			ActualNewName = SourceName + FString::FromInt(Suffix);
		}
	}
	else
	{
		// Check new name doesn't conflict
		if (FindSCSNodeByName(Blueprint, ActualNewName))
		{
			return FString::Printf(TEXT("! DuplicateComponent: '%s' already exists"), *ActualNewName);
		}
	}

	// Create new node of same class
	UClass* ComponentClass = SourceNode->ComponentTemplate->GetClass();
	USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ActualNewName));
	if (!NewNode || !NewNode->ComponentTemplate)
	{
		return FString::Printf(TEXT("! DuplicateComponent: Failed to create duplicate"));
	}

	// Copy all properties from source template to new template
	UEngine::CopyPropertiesForUnrelatedObjects(SourceNode->ComponentTemplate, NewNode->ComponentTemplate);

	// Find parent of source and add duplicate under same parent
	USCS_Node* ParentNode = Blueprint->SimpleConstructionScript->FindParentNode(SourceNode);
	if (ParentNode)
	{
		ParentNode->AddChildNode(NewNode);
	}
	else
	{
		// Source is a root node
		Blueprint->SimpleConstructionScript->AddNode(NewNode);
	}

	FString ParentStr = ParentNode ? ParentNode->GetVariableName().ToString() : TEXT("Root");
	return FString::Printf(TEXT("+ Component: '%s' duplicated as '%s' (%s) -> %s"),
		*SourceName, *ActualNewName, *ComponentClass->GetName(), *ParentStr);
}

FString FEditBlueprintTool::ReparentComponent(UBlueprint* Blueprint, const FString& CompName, const FString& NewParent)
{
	if (CompName.IsEmpty())
	{
		return TEXT("! ReparentComponent: Missing component name");
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		return FString::Printf(TEXT("! ReparentComponent: No SCS in Blueprint"));
	}

	USCS_Node* Node = FindSCSNodeByName(Blueprint, CompName);
	if (!Node)
	{
		return FString::Printf(TEXT("! ReparentComponent: '%s' not found"), *CompName);
	}

	// Must be a scene component to reparent
	USceneComponent* SceneTemplate = Cast<USceneComponent>(Node->ComponentTemplate);
	if (!SceneTemplate)
	{
		return FString::Printf(TEXT("! ReparentComponent: '%s' is not a SceneComponent, cannot reparent"), *CompName);
	}

	// Determine target parent before detaching (for validation)
	bool bDetachToRoot = NewParent.IsEmpty() || NewParent.Equals(TEXT("root"), ESearchCase::IgnoreCase);
	USCS_Node* TargetParent = nullptr;

	if (!bDetachToRoot)
	{
		TargetParent = FindSCSNodeByName(Blueprint, NewParent);
		if (!TargetParent)
		{
			return FString::Printf(TEXT("! ReparentComponent: Parent '%s' not found"), *NewParent);
		}

		// Prevent circular hierarchy - check if target is a descendant of the node
		USCS_Node* Check = TargetParent;
		while (Check)
		{
			if (Check == Node)
			{
				return FString::Printf(TEXT("! ReparentComponent: Cannot parent '%s' under '%s' (would create circular hierarchy)"),
					*CompName, *NewParent);
			}
			Check = Blueprint->SimpleConstructionScript->FindParentNode(Check);
		}
	}

	// Detach from current position
	// RemoveNode removes from both RootNodes and AllNodes
	// RemoveChildNode(Node, false) removes from parent's ChildNodes only
	USCS_Node* CurrentParent = Blueprint->SimpleConstructionScript->FindParentNode(Node);
	if (CurrentParent)
	{
		CurrentParent->RemoveChildNode(Node, false);
	}
	else
	{
		// It's a root node - use RemoveNode to remove from RootNodes+AllNodes
		Blueprint->SimpleConstructionScript->RemoveNode(Node, /*bValidateSceneRootNodes=*/false);
	}

	// Attach to new position
	if (bDetachToRoot)
	{
		// AddNode adds to both RootNodes and AllNodes
		Blueprint->SimpleConstructionScript->AddNode(Node);
		return FString::Printf(TEXT("~ Component: '%s' moved to Root"), *CompName);
	}
	else
	{
		// AddChildNode with bAddToAllNodes=true ensures it's in AllNodes
		TargetParent->AddChildNode(Node, true);
		return FString::Printf(TEXT("~ Component: '%s' moved under '%s'"), *CompName, *NewParent);
	}
}

FString FEditBlueprintTool::SetRootComponent(UBlueprint* Blueprint, const FString& CompName)
{
	if (CompName.IsEmpty())
	{
		return TEXT("! SetRootComponent: Missing component name");
	}

	if (!Blueprint->SimpleConstructionScript)
	{
		return FString::Printf(TEXT("! SetRootComponent: No SCS in Blueprint"));
	}

	USCS_Node* NewRootNode = FindSCSNodeByName(Blueprint, CompName);
	if (!NewRootNode)
	{
		return FString::Printf(TEXT("! SetRootComponent: '%s' not found"), *CompName);
	}

	// Must be a scene component
	USceneComponent* SceneTemplate = Cast<USceneComponent>(NewRootNode->ComponentTemplate);
	if (!SceneTemplate)
	{
		return FString::Printf(TEXT("! SetRootComponent: '%s' is not a SceneComponent"), *CompName);
	}

	// Find the current root scene component
	const TArray<USCS_Node*>& RootNodes = Blueprint->SimpleConstructionScript->GetRootNodes();
	USCS_Node* CurrentRootNode = nullptr;
	for (USCS_Node* RootNode : RootNodes)
	{
		if (RootNode && Cast<USceneComponent>(RootNode->ComponentTemplate))
		{
			CurrentRootNode = RootNode;
			break;
		}
	}

	if (CurrentRootNode == NewRootNode)
	{
		return FString::Printf(TEXT("! SetRootComponent: '%s' is already the root"), *CompName);
	}

	// Check if it's the default scene root - if so, it will be auto-removed
	bool bCurrentIsDefaultRoot = CurrentRootNode &&
		CurrentRootNode == Blueprint->SimpleConstructionScript->GetDefaultSceneRootNode();

	// Detach new root from its current parent
	USCS_Node* OldParent = Blueprint->SimpleConstructionScript->FindParentNode(NewRootNode);
	if (OldParent)
	{
		OldParent->RemoveChildNode(NewRootNode, false);
	}

	// If new root was a root node already, remove it temporarily
	TArray<USCS_Node*> MutableRootNodes = RootNodes;
	if (MutableRootNodes.Contains(NewRootNode))
	{
		Blueprint->SimpleConstructionScript->RemoveNode(NewRootNode, /*bValidateSceneRootNodes=*/false);
	}

	// Move current root's children to new root
	if (CurrentRootNode && !bCurrentIsDefaultRoot)
	{
		// Move all children from current root to new root
		TArray<USCS_Node*> ChildrenToMove = CurrentRootNode->GetChildNodes();
		for (USCS_Node* Child : ChildrenToMove)
		{
			if (Child != NewRootNode) // Don't re-add the new root as its own child
			{
				CurrentRootNode->RemoveChildNode(Child, false);
				NewRootNode->AddChildNode(Child, false);
			}
		}

		// Move current root under new root
		Blueprint->SimpleConstructionScript->RemoveNode(CurrentRootNode, false);
		NewRootNode->AddChildNode(CurrentRootNode, true);
	}

	// Add new root as root node
	Blueprint->SimpleConstructionScript->AddNode(NewRootNode);

	// Validate scene root nodes (handles default root cleanup)
	Blueprint->SimpleConstructionScript->ValidateSceneRootNodes();

	FString OldRootName = CurrentRootNode ? CurrentRootNode->GetVariableName().ToString() : TEXT("DefaultSceneRoot");
	return FString::Printf(TEXT("~ SetRoot: '%s' is now scene root (old root '%s' is now child)"),
		*CompName, *OldRootName);
}

FString FEditBlueprintTool::ConfigureComponent(UBlueprint* Blueprint, const FString& CompName, const TSharedPtr<FJsonObject>& Properties)
{
	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		return FString::Printf(TEXT("! ConfigureComponent: No properties specified for %s"), *CompName);
	}

	UActorComponent* ComponentTemplate = nullptr;
	FString SourceInfo;
	bool bIsInherited = false;

	// 1. First check this Blueprint's SCS for the component
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate &&
				Node->GetVariableName().ToString().Equals(CompName, ESearchCase::IgnoreCase))
			{
				ComponentTemplate = Cast<UActorComponent>(Node->ComponentTemplate);
				SourceInfo = TEXT("this");
				break;
			}
		}
	}

	// 2. If not found, search parent hierarchy and create override if needed
	if (!ComponentTemplate)
	{
		UClass* ParentClass = Blueprint->ParentClass;
		USCS_Node* InheritedNode = nullptr;
		UBlueprint* OwnerBlueprint = nullptr;

		// Walk up to find the inherited component
		while (ParentClass && !InheritedNode)
		{
			if (UBlueprintGeneratedClass* ParentBPGC = Cast<UBlueprintGeneratedClass>(ParentClass))
			{
				UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentBPGC->ClassGeneratedBy);
				if (ParentBlueprint && ParentBlueprint->SimpleConstructionScript)
				{
					for (USCS_Node* Node : ParentBlueprint->SimpleConstructionScript->GetAllNodes())
					{
						if (Node && Node->ComponentTemplate &&
							Node->GetVariableName().ToString().Equals(CompName, ESearchCase::IgnoreCase))
						{
							InheritedNode = Node;
							OwnerBlueprint = ParentBlueprint;
							break;
						}
					}
				}
				ParentClass = ParentClass->GetSuperClass();
			}
			else
			{
				// Native C++ class - can't override native components this way
				break;
			}
		}

		if (InheritedNode && OwnerBlueprint)
		{
			// Create component override via InheritableComponentHandler
			UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(true);
			if (ICH)
			{
				FComponentKey Key(InheritedNode);

				// Check if override already exists
				ComponentTemplate = ICH->GetOverridenComponentTemplate(Key);

				if (!ComponentTemplate)
				{
					// Create the override - this copies from parent template
					ComponentTemplate = ICH->CreateOverridenComponentTemplate(Key);
				}

				if (ComponentTemplate)
				{
					bIsInherited = true;
					SourceInfo = FString::Printf(TEXT("override:%s"), *OwnerBlueprint->GetName());
				}
			}
		}
	}

	if (!ComponentTemplate)
	{
		return FString::Printf(TEXT("! ConfigureComponent: Component '%s' not found (check name, must exist in this Blueprint or parent hierarchy)"), *CompName);
	}

	// 3. Set properties on the component template
	TArray<FString> SuccessProps;
	TArray<FString> FailedProps;

	ComponentTemplate->Modify();

	for (const auto& Pair : Properties->Values)
	{
		const FString& PropName = Pair.Key;
		const TSharedPtr<FJsonValue>& PropValue = Pair.Value;

		FProperty* Property = FindFProperty<FProperty>(ComponentTemplate->GetClass(), FName(*PropName));
		if (!Property)
		{
			FailedProps.Add(FString::Printf(TEXT("%s (not found)"), *PropName));
			continue;
		}

		// Pre-edit notification
		ComponentTemplate->PreEditChange(Property);

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(ComponentTemplate);
		bool bSuccess = false;

		// Handle different JSON value types
		if (PropValue->Type == EJson::Boolean)
		{
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
			{
				BoolProp->SetPropertyValue(ValuePtr, PropValue->AsBool());
				bSuccess = true;
			}
		}
		else if (PropValue->Type == EJson::Number)
		{
			if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
			{
				FloatProp->SetPropertyValue(ValuePtr, (float)PropValue->AsNumber());
				bSuccess = true;
			}
			else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
			{
				DoubleProp->SetPropertyValue(ValuePtr, PropValue->AsNumber());
				bSuccess = true;
			}
			else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
			{
				IntProp->SetPropertyValue(ValuePtr, (int32)PropValue->AsNumber());
				bSuccess = true;
			}
		}
		else if (PropValue->Type == EJson::String)
		{
			// ImportText handles complex types: object refs, structs, enums, etc.
			const TCHAR* Result = Property->ImportText_InContainer(*PropValue->AsString(), ComponentTemplate, ComponentTemplate, PPF_None);
			bSuccess = (Result != nullptr);
		}

		if (bSuccess)
		{
			// Post-edit notification
			FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
			ComponentTemplate->PostEditChangeProperty(PropertyEvent);
			SuccessProps.Add(PropName);
		}
		else
		{
			FailedProps.Add(FString::Printf(TEXT("%s (set failed)"), *PropName));
		}
	}

	// Mark package dirty
	ComponentTemplate->MarkPackageDirty();

	// Build result message
	FString Result = FString::Printf(TEXT("~ ConfigureComponent: %s [%s]"), *CompName, *SourceInfo);
	if (SuccessProps.Num() > 0)
	{
		Result += FString::Printf(TEXT(" set: %s"), *FString::Join(SuccessProps, TEXT(", ")));
	}
	if (FailedProps.Num() > 0)
	{
		Result += FString::Printf(TEXT(" FAILED: %s"), *FString::Join(FailedProps, TEXT(", ")));
	}

	return Result;
}

bool FEditBlueprintTool::SetComponentProperty(UBlueprint* Blueprint, USCS_Node* Node, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value)
{
	if (!Blueprint || !Node || !Node->ComponentTemplate || !Value.IsValid())
	{
		return false;
	}

	UObject* Component = Node->ComponentTemplate;
	FProperty* Property = FindFProperty<FProperty>(Component->GetClass(), FName(*PropertyName));
	if (!Property)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("SetComponentProperty: Property '%s' not found on %s"),
			*PropertyName, *Component->GetClass()->GetName());
		return false;
	}

	// Mark component for transaction (undo/redo support)
	Component->Modify();

	// Notify pre-change (critical for some property types)
	Component->PreEditChange(Property);

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Component);
	bool bSuccess = false;

	// Handle different JSON value types
	if (Value->Type == EJson::Boolean)
	{
		if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
		{
			BoolProp->SetPropertyValue(ValuePtr, Value->AsBool());
			bSuccess = true;
		}
	}
	else if (Value->Type == EJson::Number)
	{
		if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
		{
			FloatProp->SetPropertyValue(ValuePtr, (float)Value->AsNumber());
			bSuccess = true;
		}
		else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
		{
			DoubleProp->SetPropertyValue(ValuePtr, Value->AsNumber());
			bSuccess = true;
		}
		else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
		{
			IntProp->SetPropertyValue(ValuePtr, (int32)Value->AsNumber());
			bSuccess = true;
		}
		else if (FInt64Property* Int64Prop = CastField<FInt64Property>(Property))
		{
			Int64Prop->SetPropertyValue(ValuePtr, (int64)Value->AsNumber());
			bSuccess = true;
		}
	}
	else if (Value->Type == EJson::String)
	{
		// ImportText_InContainer handles complex types like object references, structs, enums, etc.
		const TCHAR* Result = Property->ImportText_InContainer(*Value->AsString(), Component, Component, PPF_None);
		bSuccess = (Result != nullptr);

		if (!bSuccess)
		{
			UE_LOG(LogAgentIntegrationKit, Warning, TEXT("SetComponentProperty: Failed to import '%s' for property '%s'"),
				*Value->AsString(), *PropertyName);
		}
	}

	if (bSuccess)
	{
		// Mark package dirty
		Component->MarkPackageDirty();

		// Notify post-change with proper event
		FPropertyChangedEvent PropertyEvent(Property, EPropertyChangeType::ValueSet);
		Component->PostEditChangeProperty(PropertyEvent);
	}

	return bSuccess;
}

FString FEditBlueprintTool::AddFunction(UBlueprint* Blueprint, const FFunctionDefinition& FuncDef)
{
	if (FuncDef.Name.IsEmpty())
	{
		return TEXT("! Function: Missing name");
	}

	// Check if function already exists
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*FuncDef.Name))
		{
			return FString::Printf(TEXT("! Function: %s already exists"), *FuncDef.Name);
		}
	}

	// Create the function graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		FName(*FuncDef.Name),
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		return FString::Printf(TEXT("! Function: Failed to create %s"), *FuncDef.Name);
	}

	FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, true, static_cast<UFunction*>(nullptr));

	// Find the entry node and set up parameters
	UK2Node_FunctionEntry* EntryNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		EntryNode = Cast<UK2Node_FunctionEntry>(Node);
		if (EntryNode) break;
	}

	if (EntryNode)
	{
		// Set pure flag
		if (FuncDef.bPure)
		{
			EntryNode->SetExtraFlags((EntryNode->GetExtraFlags() | FUNC_BlueprintPure) & ~FUNC_BlueprintEvent);
		}

		// Add input parameters
		for (const FFunctionParam& Input : FuncDef.Inputs)
		{
			FEdGraphPinType PinType;
			FString TypeError;
			if (TypeDefinitionToPinType(Input.Type, PinType, TypeError))
			{
				EntryNode->CreateUserDefinedPin(FName(*Input.Name), PinType, EGPD_Output, false);
			}
		}

		// Add output parameters (need FunctionResult node)
		if (FuncDef.Outputs.Num() > 0)
		{
			UK2Node_FunctionResult* ResultNode = FBlueprintEditorUtils::FindOrCreateFunctionResultNode(EntryNode);
			if (ResultNode)
			{
				for (const FFunctionParam& Output : FuncDef.Outputs)
				{
					FEdGraphPinType PinType;
					FString TypeError;
					if (TypeDefinitionToPinType(Output.Type, PinType, TypeError))
					{
						ResultNode->CreateUserDefinedPin(FName(*Output.Name), PinType, EGPD_Input, false);
					}
				}
			}
		}
	}

	// Apply category if specified
	if (!FuncDef.Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(NewGraph, FText::FromString(FuncDef.Category));
	}

	// Build result string
	FString InputsStr;
	for (const FFunctionParam& Input : FuncDef.Inputs)
	{
		if (!InputsStr.IsEmpty()) InputsStr += TEXT(", ");
		InputsStr += Input.Name;
	}

	FString OutputsStr;
	for (const FFunctionParam& Output : FuncDef.Outputs)
	{
		if (!OutputsStr.IsEmpty()) OutputsStr += TEXT(", ");
		OutputsStr += Output.Name;
	}

	FString Flags = FuncDef.bPure ? TEXT(" [Pure]") : TEXT("");
	if (!FuncDef.Category.IsEmpty()) Flags += FString::Printf(TEXT(" [Category: %s]"), *FuncDef.Category);

	if (!OutputsStr.IsEmpty())
	{
		return FString::Printf(TEXT("+ Function: %s(%s) -> %s%s"), *FuncDef.Name, *InputsStr, *OutputsStr, *Flags);
	}
	return FString::Printf(TEXT("+ Function: %s(%s)%s"), *FuncDef.Name, *InputsStr, *Flags);
}

FString FEditBlueprintTool::RemoveFunction(UBlueprint* Blueprint, const FString& FuncName)
{
	FName Name = FName(*FuncName);

	for (int32 i = Blueprint->FunctionGraphs.Num() - 1; i >= 0; i--)
	{
		UEdGraph* Graph = Blueprint->FunctionGraphs[i];
		if (Graph && Graph->GetFName() == Name)
		{
			// MarkTransient only — skip per-removal recompile. Finalization handles single recompile.
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
			return FString::Printf(TEXT("- Function: %s"), *FuncName);
		}
	}

	return FString::Printf(TEXT("! Function: %s not found"), *FuncName);
}

FString FEditBlueprintTool::RenameFunction(UBlueprint* Blueprint, const FString& OldName, const FString& NewName)
{
	if (OldName.IsEmpty() || NewName.IsEmpty())
	{
		return TEXT("! Rename: Missing name or new_name");
	}

	// Search function graphs, macro graphs, and ubergraph pages
	UEdGraph* FoundGraph = nullptr;
	FString GraphType;

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == FName(*OldName))
		{
			FoundGraph = Graph;
			GraphType = TEXT("Function");
			break;
		}
	}

	if (!FoundGraph)
	{
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph && Graph->GetFName() == FName(*OldName))
			{
				FoundGraph = Graph;
				GraphType = TEXT("Macro");
				break;
			}
		}
	}

	if (!FoundGraph)
	{
		return FString::Printf(TEXT("! Rename: Function/Macro '%s' not found"), *OldName);
	}

	// Check that the new name doesn't conflict
	FName NewFName(*NewName);
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == NewFName)
		{
			return FString::Printf(TEXT("! Rename: '%s' already exists as a function"), *NewName);
		}
	}
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetFName() == NewFName)
		{
			return FString::Printf(TEXT("! Rename: '%s' already exists as a macro"), *NewName);
		}
	}

	FBlueprintEditorUtils::RenameGraph(FoundGraph, NewName);

	return FString::Printf(TEXT("~ %s: %s -> %s"), *GraphType, *OldName, *NewName);
}

FString FEditBlueprintTool::AddMacro(UBlueprint* Blueprint, const FFunctionDefinition& MacroDef)
{
	if (MacroDef.Name.IsEmpty())
	{
		return TEXT("! Macro: Missing name");
	}

	FName MacroName(*MacroDef.Name);

	// Check if macro already exists
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetFName() == MacroName)
		{
			return FString::Printf(TEXT("! Macro: %s already exists"), *MacroDef.Name);
		}
	}

	// Create the macro graph
	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		MacroName,
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		return FString::Printf(TEXT("! Macro: Failed to create graph for %s"), *MacroDef.Name);
	}

	// AddMacroGraph creates tunnel entry/exit nodes, sets up flags, adds to Blueprint->MacroGraphs
	FBlueprintEditorUtils::AddMacroGraph(Blueprint, NewGraph, /*bIsUserCreated=*/ true, /*SignatureFromClass=*/ nullptr);

	// Find tunnel entry/exit nodes to add input/output pins
	UK2Node_Tunnel* EntryNode = nullptr;
	UK2Node_Tunnel* ExitNode = nullptr;
	for (UEdGraphNode* Node : NewGraph->Nodes)
	{
		UK2Node_Tunnel* TunnelNode = Cast<UK2Node_Tunnel>(Node);
		if (TunnelNode)
		{
			if (TunnelNode->bCanHaveOutputs && !TunnelNode->bCanHaveInputs)
			{
				EntryNode = TunnelNode;  // Entry: outputs only (feeds into macro body)
			}
			else if (TunnelNode->bCanHaveInputs && !TunnelNode->bCanHaveOutputs)
			{
				ExitNode = TunnelNode;   // Exit: inputs only (receives from macro body)
			}
		}
	}

	// Add input pins on entry node
	if (EntryNode)
	{
		for (const FFunctionParam& Input : MacroDef.Inputs)
		{
			FEdGraphPinType PinType;
			FString TypeError;
			if (TypeDefinitionToPinType(Input.Type, PinType, TypeError))
			{
				EntryNode->CreateUserDefinedPin(FName(*Input.Name), PinType, EGPD_Output, false);
			}
		}
	}

	// Add output pins on exit node
	if (ExitNode)
	{
		for (const FFunctionParam& Output : MacroDef.Outputs)
		{
			FEdGraphPinType PinType;
			FString TypeError;
			if (TypeDefinitionToPinType(Output.Type, PinType, TypeError))
			{
				ExitNode->CreateUserDefinedPin(FName(*Output.Name), PinType, EGPD_Input, false);
			}
		}
	}

	// Apply category if specified
	if (!MacroDef.Category.IsEmpty())
	{
		FBlueprintEditorUtils::SetBlueprintFunctionOrMacroCategory(NewGraph, FText::FromString(MacroDef.Category));
	}

	// Build result string
	FString InputsStr;
	for (const FFunctionParam& Input : MacroDef.Inputs)
	{
		if (!InputsStr.IsEmpty()) InputsStr += TEXT(", ");
		InputsStr += Input.Name;
	}

	FString OutputsStr;
	for (const FFunctionParam& Output : MacroDef.Outputs)
	{
		if (!OutputsStr.IsEmpty()) OutputsStr += TEXT(", ");
		OutputsStr += Output.Name;
	}

	FString Info;
	if (!MacroDef.Category.IsEmpty()) Info += FString::Printf(TEXT(" [Category: %s]"), *MacroDef.Category);

	if (!OutputsStr.IsEmpty())
	{
		return FString::Printf(TEXT("+ Macro: %s(%s) -> %s%s"), *MacroDef.Name, *InputsStr, *OutputsStr, *Info);
	}
	return FString::Printf(TEXT("+ Macro: %s(%s)%s"), *MacroDef.Name, *InputsStr, *Info);
}

FString FEditBlueprintTool::RemoveMacro(UBlueprint* Blueprint, const FString& MacroName)
{
	FName Name = FName(*MacroName);

	for (int32 i = Blueprint->MacroGraphs.Num() - 1; i >= 0; i--)
	{
		UEdGraph* Graph = Blueprint->MacroGraphs[i];
		if (Graph && Graph->GetFName() == Name)
		{
			// MarkTransient only — skip per-removal recompile. Finalization handles single recompile.
			FBlueprintEditorUtils::RemoveGraph(Blueprint, Graph, EGraphRemoveFlags::MarkTransient);
			return FString::Printf(TEXT("- Macro: %s"), *MacroName);
		}
	}

	return FString::Printf(TEXT("! Macro: %s not found"), *MacroName);
}

FString FEditBlueprintTool::OverrideFunction(UBlueprint* Blueprint, const FString& FunctionName)
{
	if (FunctionName.IsEmpty())
	{
		return TEXT("! Override: Missing function name");
	}

	FName FuncName(*FunctionName);

	// Find the parent class function and the class that declares it
	UFunction* OverrideFunc = nullptr;
	UClass* const OverrideFuncClass = FBlueprintEditorUtils::GetOverrideFunctionClass(Blueprint, FuncName, &OverrideFunc);

	if (!OverrideFunc || !OverrideFuncClass)
	{
		return FString::Printf(TEXT("! Override: Function '%s' not found in parent class hierarchy"), *FunctionName);
	}

	// Check if this function can be placed as an event (BlueprintEvent, no return value, etc.)
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	bool bPlaceAsEvent = UEdGraphSchema_K2::FunctionCanBePlacedAsEvent(OverrideFunc) && EventGraph != nullptr;

	if (bPlaceAsEvent)
	{
		// Check if override already exists
		FName EventName = OverrideFunc->GetFName();
		UK2Node_Event* ExistingNode = FBlueprintEditorUtils::FindOverrideForFunction(Blueprint, OverrideFuncClass, EventName);
		if (ExistingNode)
		{
			return FString::Printf(TEXT("! Override: %s already overridden (event node exists)"), *FunctionName);
		}

		// Spawn the override event node using the engine's SpawnNode pattern
		UK2Node_Event* NewEventNode = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_Event>(
			EventGraph,
			EventGraph->GetGoodPlaceForNewNode(),
			EK2NewNodeFlags::None,
			[EventName, OverrideFuncClass](UK2Node_Event* NewInstance)
			{
				NewInstance->EventReference.SetExternalMember(EventName, OverrideFuncClass);
				NewInstance->bOverrideFunction = true;
			}
		);

		if (!NewEventNode)
		{
			return FString::Printf(TEXT("! Override: Failed to create event node for %s"), *FunctionName);
		}

		return FString::Printf(TEXT("+ Override: %s (event node in EventGraph)"), *FunctionName);
	}
	else
	{
		// Non-event override: create a function graph (like interface implementations or functions with return values)
		UEdGraph* ExistingGraph = FindObject<UEdGraph>(Blueprint, *FunctionName);
		if (ExistingGraph)
		{
			return FString::Printf(TEXT("! Override: %s already overridden (function graph exists)"), *FunctionName);
		}

		UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
			Blueprint,
			FuncName,
			UEdGraph::StaticClass(),
			UEdGraphSchema_K2::StaticClass()
		);

		if (!NewGraph)
		{
			return FString::Printf(TEXT("! Override: Failed to create graph for %s"), *FunctionName);
		}

		FBlueprintEditorUtils::AddFunctionGraph(Blueprint, NewGraph, /*bIsUserCreated=*/ false, OverrideFuncClass);

		return FString::Printf(TEXT("+ Override: %s (function graph)"), *FunctionName);
	}
}

FString FEditBlueprintTool::AddEventGraph(UBlueprint* Blueprint, const FString& GraphName)
{
	if (GraphName.IsEmpty())
	{
		return TEXT("! EventGraph: Missing graph name");
	}

	FName Name(*GraphName);

	// Check if a graph with this name already exists
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return FString::Printf(TEXT("! EventGraph: '%s' already exists"), *GraphName);
		}
	}

	// Also check function graphs and macro graphs to avoid name collision
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return FString::Printf(TEXT("! EventGraph: Name '%s' conflicts with existing function"), *GraphName);
		}
	}
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		if (Graph && Graph->GetFName() == Name)
		{
			return FString::Printf(TEXT("! EventGraph: Name '%s' conflicts with existing macro"), *GraphName);
		}
	}

	UEdGraph* NewGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		Name,
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (!NewGraph)
	{
		return FString::Printf(TEXT("! EventGraph: Failed to create graph '%s'"), *GraphName);
	}

	FBlueprintEditorUtils::AddUbergraphPage(Blueprint, NewGraph);

	return FString::Printf(TEXT("+ EventGraph: %s"), *GraphName);
}

FString FEditBlueprintTool::ReparentBlueprint(UBlueprint* Blueprint, const FString& NewParentClassName)
{
	if (NewParentClassName.IsEmpty())
	{
		return TEXT("! Reparent: Missing parent class name");
	}

	// Find the new parent class — try multiple resolution strategies
	UClass* NewParentClass = nullptr;

	// 1. Try as a full path first (e.g., "/Script/Engine.Character")
	NewParentClass = FindObject<UClass>(nullptr, *NewParentClassName);

	// 2. Try FindClassByName helper (handles short names like "Character", "Pawn")
	if (!NewParentClass)
	{
		NewParentClass = FindClassByName(NewParentClassName);
	}

	// 3. Try as a Blueprint asset path (e.g., "/Game/Blueprints/BP_Base.BP_Base")
	if (!NewParentClass)
	{
		UBlueprint* ParentBP = LoadObject<UBlueprint>(nullptr, *NewParentClassName);
		if (ParentBP && ParentBP->GeneratedClass)
		{
			NewParentClass = ParentBP->GeneratedClass;
		}
	}

	if (!NewParentClass)
	{
		return FString::Printf(TEXT("! Reparent: Class '%s' not found"), *NewParentClassName);
	}

	// Can't reparent to the same class
	if (NewParentClass == Blueprint->ParentClass)
	{
		return FString::Printf(TEXT("! Reparent: Already parented to '%s'"), *NewParentClass->GetName());
	}

	// Can't reparent to own generated class (circular)
	if (Blueprint->GeneratedClass && NewParentClass->IsChildOf(Blueprint->GeneratedClass))
	{
		return FString::Printf(TEXT("! Reparent: Cannot reparent to own child class '%s'"), *NewParentClass->GetName());
	}

	// Can't reparent to interfaces
	if (NewParentClass->IsChildOf(UInterface::StaticClass()))
	{
		return FString::Printf(TEXT("! Reparent: Cannot reparent to interface '%s', use add_interfaces instead"), *NewParentClass->GetName());
	}

	// Validate type compatibility based on Blueprint type
	if (Blueprint->BlueprintType == BPTYPE_Interface || Blueprint->BlueprintType == BPTYPE_MacroLibrary)
	{
		return FString::Printf(TEXT("! Reparent: Cannot reparent %s Blueprints"),
			Blueprint->BlueprintType == BPTYPE_Interface ? TEXT("Interface") : TEXT("MacroLibrary"));
	}

	// For Actor BPs, new parent must also be an Actor (or AActor itself)
	if (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(AActor::StaticClass()))
	{
		if (!NewParentClass->IsChildOf(AActor::StaticClass()))
		{
			return FString::Printf(TEXT("! Reparent: Actor Blueprint cannot reparent to non-Actor class '%s'"), *NewParentClass->GetName());
		}
	}

	// For Component BPs, new parent must be a component
	if (Blueprint->ParentClass && Blueprint->ParentClass->IsChildOf(UActorComponent::StaticClass()))
	{
		if (!NewParentClass->IsChildOf(UActorComponent::StaticClass()))
		{
			return FString::Printf(TEXT("! Reparent: Component Blueprint cannot reparent to non-Component class '%s'"), *NewParentClass->GetName());
		}
	}

	// Check for interface conflicts — warn but don't block (matches engine behavior)
	FString InterfaceWarnings;
	for (const FBPInterfaceDescription& InterfaceDesc : Blueprint->ImplementedInterfaces)
	{
		if (InterfaceDesc.Interface && NewParentClass->ImplementsInterface(InterfaceDesc.Interface))
		{
			if (!InterfaceWarnings.IsEmpty()) InterfaceWarnings += TEXT(", ");
			InterfaceWarnings += InterfaceDesc.Interface->GetName();
		}
	}

	// Store old parent name for reporting
	FString OldParentName = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");

	// --- Perform reparent (matches engine's ReparentBlueprint_NewParentChosen flow) ---

	// Mark all objects for modification (undo support)
	Blueprint->Modify();
	if (USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
	{
		SCS->Modify();
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node) Node->Modify();
		}
	}

	// Actually change the parent class
	Blueprint->ParentClass = NewParentClass;

	// Handle sparse class data conformance (when new parent has different layout)
	if (UBlueprintGeneratedClass* BPGC = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
	{
		UScriptStruct* NewSparseStruct = NewParentClass->GetSparseClassDataStruct();
		if (NewSparseStruct)
		{
			BPGC->PrepareToConformSparseClassData(NewSparseStruct);
		}
	}

	// Refresh all graph nodes to update self-references and inherited member access
	FBlueprintEditorUtils::RefreshAllNodes(Blueprint);

	// Mark as modified — triggers skeleton regeneration
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	// Build result
	FString Result = FString::Printf(TEXT("~ Reparent: %s -> %s"), *OldParentName, *NewParentClass->GetName());
	if (!InterfaceWarnings.IsEmpty())
	{
		Result += FString::Printf(TEXT(" (note: parent already implements: %s)"), *InterfaceWarnings);
	}
	return Result;
}

FString FEditBlueprintTool::AddInterface(UBlueprint* Blueprint, const FString& InterfaceName)
{
	if (InterfaceName.IsEmpty())
	{
		return TEXT("! Interface: Missing interface name");
	}

	// Resolve the interface class
	UClass* InterfaceClass = nullptr;

	// 1. Try as full object path (e.g., "/Script/Engine.UInterface" or "/Game/Interfaces/BPI_X.BPI_X_C")
	InterfaceClass = FindObject<UClass>(nullptr, *InterfaceName);

	// 2. Try FindClassByName helper
	if (!InterfaceClass)
	{
		InterfaceClass = FindClassByName(InterfaceName);
	}

	// 3. Try as Blueprint Interface asset path — load the BP and get its generated class
	if (!InterfaceClass)
	{
		UBlueprint* InterfaceBP = LoadObject<UBlueprint>(nullptr, *InterfaceName);
		if (InterfaceBP && InterfaceBP->GeneratedClass)
		{
			InterfaceClass = InterfaceBP->GeneratedClass;
		}
	}

	// 4. Try searching with _C suffix (common pattern: agent sends "BPI_X" but class is "BPI_X_C")
	if (!InterfaceClass)
	{
		FString WithSuffix = InterfaceName + TEXT("_C");
		InterfaceClass = FindObject<UClass>(nullptr, *WithSuffix);
	}

	if (!InterfaceClass)
	{
		return FString::Printf(TEXT("! Interface: Class '%s' not found"), *InterfaceName);
	}

	// Validate it's actually an interface
	if (!InterfaceClass->IsChildOf(UInterface::StaticClass()) && !InterfaceClass->HasAnyClassFlags(CLASS_Interface))
	{
		return FString::Printf(TEXT("! Interface: '%s' is not an interface class"), *InterfaceClass->GetName());
	}

	// Use the engine's ImplementNewInterface — handles all graph creation, validation, and structural modification
	FTopLevelAssetPath InterfacePath = InterfaceClass->GetClassPathName();
	bool bSuccess = FBlueprintEditorUtils::ImplementNewInterface(Blueprint, InterfacePath);

	if (!bSuccess)
	{
		return FString::Printf(TEXT("! Interface: Failed to implement '%s' (may already exist or have conflicts)"), *InterfaceClass->GetName());
	}

	// Count how many function stubs were created
	int32 FuncCount = 0;
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (Desc.Interface == InterfaceClass)
		{
			FuncCount = Desc.Graphs.Num();
			break;
		}
	}

	if (FuncCount > 0)
	{
		return FString::Printf(TEXT("+ Interface: %s (%d function stubs created)"), *InterfaceClass->GetName(), FuncCount);
	}
	return FString::Printf(TEXT("+ Interface: %s"), *InterfaceClass->GetName());
}

FString FEditBlueprintTool::RemoveInterface(UBlueprint* Blueprint, const FString& InterfaceName, bool bPreserveFunctions)
{
	if (InterfaceName.IsEmpty())
	{
		return TEXT("! RemoveInterface: Missing interface name");
	}

	// Find the interface in Blueprint's ImplementedInterfaces
	UClass* FoundInterface = nullptr;
	for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
	{
		if (!Desc.Interface) continue;

		// Match by class name, short name, or path
		FString ClassName = Desc.Interface->GetName();
		FString ClassPath = Desc.Interface->GetPathName();

		if (ClassName.Equals(InterfaceName, ESearchCase::IgnoreCase) ||
			ClassPath.Equals(InterfaceName, ESearchCase::IgnoreCase))
		{
			FoundInterface = Desc.Interface;
			break;
		}

		// Also try matching without _C suffix (agent might send "BPI_X" for "BPI_X_C")
		FString WithoutSuffix = ClassName;
		if (WithoutSuffix.RemoveFromEnd(TEXT("_C")))
		{
			if (WithoutSuffix.Equals(InterfaceName, ESearchCase::IgnoreCase))
			{
				FoundInterface = Desc.Interface;
				break;
			}
		}
	}

	if (!FoundInterface)
	{
		// List available interfaces for better error messages
		FString Available;
		for (const FBPInterfaceDescription& Desc : Blueprint->ImplementedInterfaces)
		{
			if (Desc.Interface)
			{
				if (!Available.IsEmpty()) Available += TEXT(", ");
				Available += Desc.Interface->GetName();
			}
		}
		if (Available.IsEmpty()) Available = TEXT("(none)");

		return FString::Printf(TEXT("! RemoveInterface: '%s' not found. Implemented: %s"), *InterfaceName, *Available);
	}

	// Use the engine's RemoveInterface — handles graph cleanup, event node removal, child BP updates
	FTopLevelAssetPath InterfacePath = FoundInterface->GetClassPathName();
	FBlueprintEditorUtils::RemoveInterface(Blueprint, InterfacePath, bPreserveFunctions);

	if (bPreserveFunctions)
	{
		return FString::Printf(TEXT("- Interface: %s (functions preserved as regular functions)"), *FoundInterface->GetName());
	}
	return FString::Printf(TEXT("- Interface: %s"), *FoundInterface->GetName());
}

FString FEditBlueprintTool::AddEvent(UBlueprint* Blueprint, const FEventDefinition& EventDef)
{
	if (EventDef.Name.IsEmpty())
	{
		return TEXT("! Event: Missing name");
	}

	FName EventName = FName(*EventDef.Name);

	// Check if event dispatcher already exists
	for (const FBPVariableDescription& Var : Blueprint->NewVariables)
	{
		if (Var.VarName == EventName && Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			return FString::Printf(TEXT("! Event: %s already exists"), *EventDef.Name);
		}
	}

	// Create multicast delegate type
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	PinType.PinSubCategoryObject = nullptr;

	// Add as variable
	bool bSuccess = FBlueprintEditorUtils::AddMemberVariable(Blueprint, EventName, PinType);
	if (!bSuccess)
	{
		return FString::Printf(TEXT("! Event: Failed to add %s"), *EventDef.Name);
	}

	// Create the delegate signature graph (matches engine's OnAddNewDelegate flow)
	UEdGraph* DelegateGraph = FBlueprintEditorUtils::CreateNewGraph(
		Blueprint,
		EventName,
		UEdGraph::StaticClass(),
		UEdGraphSchema_K2::StaticClass()
	);

	if (DelegateGraph)
	{
		DelegateGraph->bEditable = false;

		Blueprint->DelegateSignatureGraphs.Add(DelegateGraph);

		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->CreateDefaultNodesForGraph(*DelegateGraph);
		K2Schema->CreateFunctionGraphTerminators(*DelegateGraph, static_cast<UClass*>(nullptr));
		K2Schema->AddExtraFunctionFlags(DelegateGraph, FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public);
		K2Schema->MarkFunctionEntryAsEditable(DelegateGraph, true);

		// Add parameter pins from EventDef.Params on the entry node
		if (EventDef.Params.Num() > 0)
		{
			UK2Node_FunctionEntry* EntryNode = nullptr;
			for (UEdGraphNode* Node : DelegateGraph->Nodes)
			{
				EntryNode = Cast<UK2Node_FunctionEntry>(Node);
				if (EntryNode) break;
			}

			if (EntryNode)
			{
				for (const FFunctionParam& Param : EventDef.Params)
				{
					FEdGraphPinType ParamPinType;
					FString TypeError;
					if (TypeDefinitionToPinType(Param.Type, ParamPinType, TypeError))
					{
						EntryNode->CreateUserDefinedPin(FName(*Param.Name), ParamPinType, EGPD_Output, false);
					}
				}
			}
		}
	}

	// Build result string
	FString ParamsStr;
	for (const FFunctionParam& Param : EventDef.Params)
	{
		if (!ParamsStr.IsEmpty()) ParamsStr += TEXT(", ");
		ParamsStr += Param.Name;
	}

	return FString::Printf(TEXT("+ Event: %s(%s)"), *EventDef.Name, *ParamsStr);
}

FString FEditBlueprintTool::RemoveEvent(UBlueprint* Blueprint, const FString& EventName)
{
	FName Name = FName(*EventName);

	// Event dispatchers are stored as variables with MCDelegate type
	for (int32 i = Blueprint->NewVariables.Num() - 1; i >= 0; i--)
	{
		const FBPVariableDescription& Var = Blueprint->NewVariables[i];
		if (Var.VarName == Name && Var.VarType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate)
		{
			FBlueprintEditorUtils::RemoveMemberVariable(Blueprint, Name);
			return FString::Printf(TEXT("- Event: %s"), *EventName);
		}
	}

	return FString::Printf(TEXT("! Event: %s not found"), *EventName);
}

FString FEditBlueprintTool::AddCustomEvent(UBlueprint* Blueprint, const FCustomEventDefinition& EventDef)
{
	if (EventDef.Name.IsEmpty())
	{
		return TEXT("! CustomEvent: Missing name");
	}

	// Find the EventGraph
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!EventGraph)
	{
		return TEXT("! CustomEvent: No EventGraph found");
	}

	// Check if custom event already exists
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_CustomEvent* ExistingEvent = Cast<UK2Node_CustomEvent>(Node);
		if (ExistingEvent && ExistingEvent->CustomFunctionName == FName(*EventDef.Name))
		{
			return FString::Printf(TEXT("! CustomEvent: %s already exists"), *EventDef.Name);
		}
	}

	UK2Node_CustomEvent* NewEventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
	NewEventNode->CreateNewGuid();
	NewEventNode->CustomFunctionName = FName(*EventDef.Name);
	NewEventNode->bCallInEditor = EventDef.bCallInEditor;
	NewEventNode->bIsEditable = true;
	NewEventNode->bCanRenameNode = true;

	uint32 FuncFlags = FUNC_BlueprintCallable | FUNC_BlueprintEvent | FUNC_Public;
	switch (EventDef.Replication)
	{
	case EEventReplication::Multicast:
		FuncFlags |= FUNC_Net | FUNC_NetMulticast;
		break;
	case EEventReplication::Server:
		FuncFlags |= FUNC_Net | FUNC_NetServer;
		break;
	case EEventReplication::Client:
		FuncFlags |= FUNC_Net | FUNC_NetClient;
		break;
	default:
		break;
	}
	if (EventDef.bReliable && (FuncFlags & FUNC_Net))
	{
		FuncFlags |= FUNC_NetReliable;
	}
	NewEventNode->FunctionFlags = FuncFlags;

	NewEventNode->SetFlags(RF_Transactional);
	EventGraph->Modify();
	EventGraph->AddNode(NewEventNode, true, false);
	NewEventNode->PostPlacedNewNode();
	NewEventNode->AllocateDefaultPins();

	int32 NodeX = 0;
	int32 NodeY = 0;
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		if (Node != NewEventNode)
		{
			NodeY = FMath::Max(NodeY, static_cast<int32>(Node->NodePosY) + 200);
		}
	}
	NewEventNode->NodePosX = NodeX;
	NewEventNode->NodePosY = NodeY;

	TArray<FString> TypeErrors;
	for (const FFunctionParam& Param : EventDef.Params)
	{
		FEdGraphPinType PinType;
		FString TypeError;
		if (TypeDefinitionToPinType(Param.Type, PinType, TypeError))
		{
			NewEventNode->CreateUserDefinedPin(FName(*Param.Name), PinType, EGPD_Output);
		}
		else
		{
			TypeErrors.Add(FString::Printf(TEXT("  ! Param %s: %s (type=%s)"), *Param.Name, *TypeError, *Param.Type.Base));
		}
	}

	// Build result string
	FString ParamsStr;
	for (const FFunctionParam& Param : EventDef.Params)
	{
		if (!ParamsStr.IsEmpty()) ParamsStr += TEXT(", ");
		ParamsStr += Param.Name;
	}

	FString ReplicationStr;
	switch (EventDef.Replication)
	{
	case EEventReplication::Multicast:
		ReplicationStr = EventDef.bReliable ? TEXT(" [Multicast, Reliable]") : TEXT(" [Multicast]");
		break;
	case EEventReplication::Server:
		ReplicationStr = EventDef.bReliable ? TEXT(" [Server, Reliable]") : TEXT(" [Server]");
		break;
	case EEventReplication::Client:
		ReplicationStr = EventDef.bReliable ? TEXT(" [Client, Reliable]") : TEXT(" [Client]");
		break;
	default:
		break;
	}

	FString Result = FString::Printf(TEXT("+ CustomEvent: %s(%s)%s"), *EventDef.Name, *ParamsStr, *ReplicationStr);

	// Append any type errors
	for (const FString& Error : TypeErrors)
	{
		Result += TEXT("\n") + Error;
	}

	return Result;
}

FString FEditBlueprintTool::RemoveCustomEvent(UBlueprint* Blueprint, const FString& EventName)
{
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!EventGraph)
	{
		return FString::Printf(TEXT("! CustomEvent: %s - No EventGraph found"), *EventName);
	}

	FName TargetName = FName(*EventName);

	for (int32 i = EventGraph->Nodes.Num() - 1; i >= 0; i--)
	{
		UK2Node_CustomEvent* EventNode = Cast<UK2Node_CustomEvent>(EventGraph->Nodes[i]);
		if (EventNode && EventNode->CustomFunctionName == TargetName)
		{
			FBlueprintEditorUtils::RemoveNode(Blueprint, EventNode, true);
			return FString::Printf(TEXT("- CustomEvent: %s"), *EventName);
		}
	}

	return FString::Printf(TEXT("! CustomEvent: %s not found"), *EventName);
}

// Widget Blueprint operations

UClass* FEditBlueprintTool::FindWidgetClass(const FString& TypeName)
{
	// Map common widget type names to their classes
	static TMap<FString, UClass*> WidgetClassMap;
	if (WidgetClassMap.Num() == 0)
	{
		// Panels
		WidgetClassMap.Add(TEXT("CanvasPanel"), UCanvasPanel::StaticClass());
		WidgetClassMap.Add(TEXT("HorizontalBox"), UHorizontalBox::StaticClass());
		WidgetClassMap.Add(TEXT("VerticalBox"), UVerticalBox::StaticClass());
		WidgetClassMap.Add(TEXT("GridPanel"), UGridPanel::StaticClass());
		WidgetClassMap.Add(TEXT("UniformGridPanel"), UUniformGridPanel::StaticClass());
		WidgetClassMap.Add(TEXT("WrapBox"), UWrapBox::StaticClass());
		WidgetClassMap.Add(TEXT("ScrollBox"), UScrollBox::StaticClass());
		WidgetClassMap.Add(TEXT("SizeBox"), USizeBox::StaticClass());
		WidgetClassMap.Add(TEXT("Overlay"), UOverlay::StaticClass());
		WidgetClassMap.Add(TEXT("WidgetSwitcher"), UWidgetSwitcher::StaticClass());

		// Common widgets
		WidgetClassMap.Add(TEXT("Button"), UButton::StaticClass());
		WidgetClassMap.Add(TEXT("TextBlock"), UTextBlock::StaticClass());
		WidgetClassMap.Add(TEXT("Image"), UImage::StaticClass());
		WidgetClassMap.Add(TEXT("Border"), UBorder::StaticClass());
		WidgetClassMap.Add(TEXT("Spacer"), USpacer::StaticClass());

		// Input widgets
		WidgetClassMap.Add(TEXT("CheckBox"), UCheckBox::StaticClass());
		WidgetClassMap.Add(TEXT("EditableTextBox"), UEditableTextBox::StaticClass());
		WidgetClassMap.Add(TEXT("Slider"), USlider::StaticClass());

		// Progress
		WidgetClassMap.Add(TEXT("ProgressBar"), UProgressBar::StaticClass());
	}

	// Try direct lookup (case-insensitive)
	for (const auto& Pair : WidgetClassMap)
	{
		if (Pair.Key.Equals(TypeName, ESearchCase::IgnoreCase))
		{
			return Pair.Value;
		}
	}

	// Try finding by class name with U prefix
	FString SearchName = TypeName;
	if (!SearchName.StartsWith(TEXT("U")))
	{
		SearchName = TEXT("U") + TypeName;
	}

	// Search all loaded UWidget classes (native classes)
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UWidget::StaticClass()))
		{
			if (It->GetName().Equals(SearchName, ESearchCase::IgnoreCase) ||
				It->GetName().Equals(TypeName, ESearchCase::IgnoreCase))
			{
				return *It;
			}
		}
	}

	// Try to find Blueprint Widget class via Asset Registry
	// Supports: "WBP_MyWidget", "/Game/UI/WBP_MyWidget", "/Game/UI/WBP_MyWidget.WBP_MyWidget_C"
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	// If it looks like a path, try to load directly
	if (TypeName.Contains(TEXT("/")))
	{
		FString ObjectPath = TypeName;

		// Normalize path: /Game/UI/WBP_MyWidget -> /Game/UI/WBP_MyWidget.WBP_MyWidget_C
		if (!ObjectPath.Contains(TEXT(".")))
		{
			FString AssetName = FPaths::GetBaseFilename(ObjectPath);
			ObjectPath = ObjectPath + TEXT(".") + AssetName + TEXT("_C");
		}
		else if (!ObjectPath.EndsWith(TEXT("_C")))
		{
			ObjectPath = ObjectPath + TEXT("_C");
		}

		UClass* FoundClass = LoadObject<UClass>(nullptr, *ObjectPath);
		if (FoundClass && FoundClass->IsChildOf(UWidget::StaticClass()))
		{
			return FoundClass;
		}
	}

	// Search Asset Registry for Widget Blueprints by name
	TArray<FAssetData> AssetList;
	AssetRegistry.GetAssetsByClass(UWidgetBlueprint::StaticClass()->GetClassPathName(), AssetList);

	for (const FAssetData& Asset : AssetList)
	{
		FString AssetName = Asset.AssetName.ToString();

		// Match by name (case-insensitive)
		if (AssetName.Equals(TypeName, ESearchCase::IgnoreCase) ||
			AssetName.Equals(SearchName, ESearchCase::IgnoreCase))
		{
			// Load the Blueprint and get its generated class
			UWidgetBlueprint* WidgetBP = Cast<UWidgetBlueprint>(Asset.GetAsset());
			if (WidgetBP && WidgetBP->GeneratedClass)
			{
				return WidgetBP->GeneratedClass;
			}
		}
	}

	return nullptr;
}

UWidget* FEditBlueprintTool::FindWidgetByName(UWidgetTree* WidgetTree, const FString& Name)
{
	if (!WidgetTree) return nullptr;

	return WidgetTree->FindWidget(FName(*Name));
}

FString FEditBlueprintTool::AddWidget(UWidgetBlueprint* WidgetBlueprint, const FWidgetDefinition& WidgetDef)
{
	if (WidgetDef.Type.IsEmpty() || WidgetDef.Name.IsEmpty())
	{
		return TEXT("! Widget: Missing type or name");
	}

	// Ensure widget tree exists
	if (!WidgetBlueprint->WidgetTree)
	{
		WidgetBlueprint->WidgetTree = NewObject<UWidgetTree>(WidgetBlueprint, UWidgetTree::StaticClass(), NAME_None, RF_Transactional);
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;

	// Check if widget already exists
	if (FindWidgetByName(WidgetTree, WidgetDef.Name))
	{
		return FString::Printf(TEXT("! Widget: %s already exists"), *WidgetDef.Name);
	}

	// Find widget class
	UClass* WidgetClass = FindWidgetClass(WidgetDef.Type);
	if (!WidgetClass)
	{
		return FString::Printf(TEXT("! Widget: Unknown type %s"), *WidgetDef.Type);
	}

	// Mark tree as transactional for undo support (matches engine editor flow)
	WidgetTree->SetFlags(RF_Transactional);
	WidgetTree->Modify();

	// Create the widget
	UWidget* NewWidget = WidgetTree->ConstructWidget<UWidget>(WidgetClass, FName(*WidgetDef.Name));
	if (!NewWidget)
	{
		return FString::Printf(TEXT("! Widget: Failed to create %s"), *WidgetDef.Name);
	}

	// Notify widget it was created from palette (initializes editor defaults)
	NewWidget->CreatedFromPalette();

	// Find parent widget
	UPanelWidget* ParentPanel = nullptr;
	if (!WidgetDef.Parent.IsEmpty())
	{
		UWidget* ParentWidget = FindWidgetByName(WidgetTree, WidgetDef.Parent);
		if (!ParentWidget)
		{
			return FString::Printf(TEXT("! Widget: Parent not found: %s"), *WidgetDef.Parent);
		}

		ParentPanel = Cast<UPanelWidget>(ParentWidget);
		if (!ParentPanel)
		{
			return FString::Printf(TEXT("! Widget: Parent %s is not a panel widget"), *WidgetDef.Parent);
		}
	}
	else
	{
		// Use root widget as parent, or set as root if no root exists
		if (WidgetTree->RootWidget)
		{
			ParentPanel = Cast<UPanelWidget>(WidgetTree->RootWidget);
			if (!ParentPanel)
			{
				return TEXT("! Widget: Root widget is not a panel, cannot add children");
			}
		}
	}

	// Add to parent or set as root
	if (ParentPanel)
	{
		ParentPanel->SetFlags(RF_Transactional);
		ParentPanel->Modify();
		ParentPanel->AddChild(NewWidget);
	}
	else
	{
		// This is the first widget, set as root (should be a panel)
		WidgetTree->RootWidget = NewWidget;
	}

	// Register widget GUID (required by WidgetBlueprintCompiler in 5.6+)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	WidgetBlueprint->OnVariableAdded(NewWidget->GetFName());
#endif

	// Trigger skeleton recompilation and mark package dirty
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	// Refresh editor if open
	RefreshWidgetEditor(WidgetBlueprint);

	FString ParentStr = WidgetDef.Parent.IsEmpty() ? TEXT("Root") : WidgetDef.Parent;
	return FString::Printf(TEXT("+ Widget: %s (%s) -> %s"), *WidgetDef.Name, *WidgetDef.Type, *ParentStr);
}

FString FEditBlueprintTool::RemoveWidget(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName)
{
	if (!WidgetBlueprint->WidgetTree)
	{
		return FString::Printf(TEXT("! Widget: %s not found (no widget tree)"), *WidgetName);
	}

	UWidgetTree* WidgetTree = WidgetBlueprint->WidgetTree;
	UWidget* Widget = FindWidgetByName(WidgetTree, WidgetName);

	if (!Widget)
	{
		return FString::Printf(TEXT("! Widget: %s not found"), *WidgetName);
	}

	// Collect all child widgets BEFORE removal (engine pattern from FWidgetBlueprintEditorUtils::DeleteWidgets)
	TArray<UWidget*> AllWidgetsToRemove;
	UWidgetTree::GetChildWidgets(Widget, AllWidgetsToRemove);
	AllWidgetsToRemove.Add(Widget); // Add the widget itself last

	// Cache all widget names before any modifications (pointers may be invalidated)
	TArray<FName> AllWidgetNames;
	for (UWidget* W : AllWidgetsToRemove)
	{
		AllWidgetNames.Add(W->GetFName());
	}

	// Mark all affected objects as transactional for undo (matches engine editor flow)
	WidgetTree->SetFlags(RF_Transactional);
	WidgetTree->Modify();
	WidgetBlueprint->Modify();
	Widget->SetFlags(RF_Transactional);
	Widget->Modify();

	// Don't allow removing root if it has children
	if (Widget == WidgetTree->RootWidget)
	{
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			if (Panel->GetChildrenCount() > 0)
			{
				return FString::Printf(TEXT("! Widget: Cannot remove root %s - has children"), *WidgetName);
			}
		}
		WidgetTree->RootWidget = nullptr;
	}
	else
	{
		// Mark parent as modified before removing child
		UPanelWidget* Parent = Widget->GetParent();
		if (Parent)
		{
			Parent->SetFlags(RF_Transactional);
			Parent->Modify();
		}

		// Remove from parent
		WidgetTree->RemoveWidget(Widget);
	}

	// Remove delegate bindings and GUIDs for the widget AND all its children
	for (int32 WidgetIdx = 0; WidgetIdx < AllWidgetsToRemove.Num(); WidgetIdx++)
	{
		UWidget* W = AllWidgetsToRemove[WidgetIdx];
		const FName WName = AllWidgetNames[WidgetIdx];

		// Remove delegate bindings referencing this widget
		const FString WNameStr = W->GetName();
		for (int32 i = WidgetBlueprint->Bindings.Num() - 1; i >= 0; i--)
		{
			if (WidgetBlueprint->Bindings[i].ObjectName == WNameStr)
			{
				WidgetBlueprint->Bindings.RemoveAt(i);
			}
		}

		// Move widget to transient package to prevent name conflicts (engine pattern)
		W->Rename(nullptr, GetTransientPackage());

		// Unregister widget GUID (keep map in sync with widget tree in 5.6+)
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		WidgetBlueprint->OnVariableRemoved(WName);
#endif
	}

	// Trigger skeleton recompilation and mark package dirty
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(WidgetBlueprint);

	// Refresh editor if open
	RefreshWidgetEditor(WidgetBlueprint);

	return FString::Printf(TEXT("- Widget: %s"), *WidgetName);
}

FString FEditBlueprintTool::ConfigureWidget(UWidgetBlueprint* WidgetBlueprint, const FWidgetConfigDefinition& ConfigDef)
{
	if (ConfigDef.Name.IsEmpty())
	{
		return TEXT("! Configure: Missing widget name");
	}

	if (!WidgetBlueprint->WidgetTree)
	{
		return FString::Printf(TEXT("! Configure: %s not found (no widget tree)"), *ConfigDef.Name);
	}

	UWidget* Widget = FindWidgetByName(WidgetBlueprint->WidgetTree, ConfigDef.Name);
	if (!Widget)
	{
		return FString::Printf(TEXT("! Configure: Widget %s not found"), *ConfigDef.Name);
	}

	if (IsTransientWorldObject_EditBlueprint(Widget))
	{
		return FString::Printf(
			TEXT("! Configure: Widget %s resolved to a transient-world instance; edit the Widget Blueprint asset widget tree instead"),
			*ConfigDef.Name);
	}

	// Mark widget for undo before modifying (Widget::Modify also marks its Slot)
	Widget->Modify();

	TArray<FString> SetProps;
	FString Error;

	// Set widget properties
	if (ConfigDef.Properties.IsValid())
	{
		for (const auto& Pair : ConfigDef.Properties->Values)
		{
			if (SetPropertyFromJson(Widget, Pair.Key, Pair.Value, Error))
			{
				SetProps.Add(Pair.Key);
			}
			else
			{
				return FString::Printf(TEXT("! Configure: %s.%s - %s"), *ConfigDef.Name, *Pair.Key, *Error);
			}
		}
	}

	// Set slot properties (if widget has a slot)
	if (ConfigDef.Slot.IsValid() && Widget->Slot)
	{
		for (const auto& Pair : ConfigDef.Slot->Values)
		{
			if (SetPropertyFromJson(Widget->Slot, Pair.Key, Pair.Value, Error))
			{
				SetProps.Add(FString::Printf(TEXT("Slot.%s"), *Pair.Key));
			}
			else
			{
				return FString::Printf(TEXT("! Configure: %s.Slot.%s - %s"), *ConfigDef.Name, *Pair.Key, *Error);
			}
		}
	}

	// Notify property system of changes (syncs to Slate widget)
	FPropertyChangedEvent ChangedEvent(nullptr);
	Widget->PostEditChangeProperty(ChangedEvent);

	// Synchronize slot properties to Slate if slot was modified
	if (ConfigDef.Slot.IsValid() && Widget->Slot)
	{
		Widget->Slot->SynchronizeProperties();
	}

	// Mark blueprint as modified (triggers cache invalidation + package dirty)
	FBlueprintEditorUtils::MarkBlueprintAsModified(WidgetBlueprint);

	// Refresh editor if open
	RefreshWidgetEditor(WidgetBlueprint);

	if (SetProps.Num() == 0)
	{
		return FString::Printf(TEXT("* Widget: %s (no properties changed)"), *ConfigDef.Name);
	}

	return FString::Printf(TEXT("* Widget: %s set [%s]"), *ConfigDef.Name, *FString::Join(SetProps, TEXT(", ")));
}

bool FEditBlueprintTool::SetPropertyFromJson(UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Object || !Value.IsValid())
	{
		OutError = TEXT("Invalid object or value");
		return false;
	}

	// Find the property
	FProperty* Property = Object->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Property)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found"), *PropertyName);
		return false;
	}

	// Get property pointer
	void* PropertyPtr = Property->ContainerPtrToValuePtr<void>(Object);

	// Handle different JSON value types and property types
	if (FStrProperty* StrProp = CastField<FStrProperty>(Property))
	{
		FString StrValue;
		if (Value->TryGetString(StrValue))
		{
			StrProp->SetPropertyValue(PropertyPtr, StrValue);
			return true;
		}
	}
	else if (FTextProperty* TextProp = CastField<FTextProperty>(Property))
	{
		FString StrValue;
		if (Value->TryGetString(StrValue))
		{
			TextProp->SetPropertyValue(PropertyPtr, FText::FromString(StrValue));
			return true;
		}
	}
	else if (FNameProperty* NameProp = CastField<FNameProperty>(Property))
	{
		FString StrValue;
		if (Value->TryGetString(StrValue))
		{
			NameProp->SetPropertyValue(PropertyPtr, FName(*StrValue));
			return true;
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool BoolValue;
		if (Value->TryGetBool(BoolValue))
		{
			BoolProp->SetPropertyValue(PropertyPtr, BoolValue);
			return true;
		}
	}
	else if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			FloatProp->SetPropertyValue(PropertyPtr, (float)NumValue);
			return true;
		}
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			DoubleProp->SetPropertyValue(PropertyPtr, NumValue);
			return true;
		}
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			IntProp->SetPropertyValue(PropertyPtr, (int32)NumValue);
			return true;
		}
	}
	else if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			ByteProp->SetPropertyValue(PropertyPtr, (uint8)NumValue);
			return true;
		}
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		// Try to set enum by name or value
		FString StrValue;
		if (Value->TryGetString(StrValue))
		{
			UEnum* Enum = EnumProp->GetEnum();
			int64 EnumValue = Enum->GetValueByNameString(StrValue);
			if (EnumValue != INDEX_NONE)
			{
				EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropertyPtr, EnumValue);
				return true;
			}
		}
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(PropertyPtr, (int64)NumValue);
			return true;
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		// For structs, try to import from string representation
		FString StrValue;
		if (Value->TryGetString(StrValue))
		{
			// Use ImportText to parse struct from string
			if (Property->ImportText_Direct(*StrValue, PropertyPtr, Object, PPF_None))
			{
				return true;
			}
			OutError = FString::Printf(TEXT("Failed to parse struct value '%s'"), *StrValue);
			return false;
		}

		// For struct JSON objects, iterate properties
		const TSharedPtr<FJsonObject>* ObjValue;
		if (Value->TryGetObject(ObjValue))
		{
			UScriptStruct* Struct = StructProp->Struct;
			for (const auto& StructPair : (*ObjValue)->Values)
			{
				FProperty* StructField = Struct->FindPropertyByName(FName(*StructPair.Key));
				if (StructField)
				{
					void* StructFieldPtr = StructField->ContainerPtrToValuePtr<void>(PropertyPtr);

					// Recursively handle the struct field
					FString FieldError;
					// Create a temp object to use SetPropertyFromJson on struct fields
					// For now, use ImportText on the whole struct
				}
			}

			// Fallback: serialize object to string and import
			FString JsonStr;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonStr);
			FJsonSerializer::Serialize((*ObjValue).ToSharedRef(), *Writer);

			// Try formats like "(X=1.0,Y=2.0,Z=3.0)" for vectors
			if (Property->ImportText_Direct(*JsonStr, PropertyPtr, Object, PPF_None))
			{
				return true;
			}
		}
	}

	// Fallback: try ImportText for any property type
	FString StrValue;
	if (Value->TryGetString(StrValue))
	{
		if (Property->ImportText_Direct(*StrValue, PropertyPtr, Object, PPF_None))
		{
			return true;
		}
	}

	OutError = FString::Printf(TEXT("Could not set property of type %s"), *Property->GetClass()->GetName());
	return false;
}

void FEditBlueprintTool::RefreshWidgetEditor(UWidgetBlueprint* WidgetBlueprint)
{
	QueueWidgetPreviewRefresh_EditBlueprint(WidgetBlueprint);
}

// =============================================================================
// Event Binding Operations
// =============================================================================

FString FEditBlueprintTool::ListEvents(UBlueprint* Blueprint, const FString& SourceName)
{
	// Check if this is a Widget Blueprint
	UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint);

	TArray<FEventInfo> Events;
	FString SourceType;

	if (WidgetBlueprint)
	{
		Events = ListWidgetEvents(WidgetBlueprint, SourceName);
		// Get widget type
		if (WidgetBlueprint->WidgetTree)
		{
			if (UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*SourceName)))
			{
				SourceType = Widget->GetClass()->GetName();
				SourceType.RemoveFromStart(TEXT("U"));
			}
		}
	}
	else
	{
		Events = ListComponentEvents(Blueprint, SourceName);
		// Get component type (checks both SCS and CDO)
		FComponentDiscoveryResult Discovery = FindComponentByName(Blueprint, SourceName);
		if (Discovery.ComponentTemplate)
		{
			SourceType = Discovery.ComponentTemplate->GetClass()->GetName();
			SourceType.RemoveFromStart(TEXT("U"));
		}
	}

	if (Events.Num() == 0)
	{
		return FString::Printf(TEXT("! No bindable events found on '%s'"), *SourceName);
	}

	FString Output = FString::Printf(TEXT("Events on %s (%s):\n"), *SourceName, *SourceType);
	for (const FEventInfo& Event : Events)
	{
		Output += FString::Printf(TEXT("  - %s%s\n"), *Event.Name, *Event.Signature);
	}

	return Output;
}

TArray<FEditBlueprintTool::FEventInfo> FEditBlueprintTool::ListComponentEvents(UBlueprint* Blueprint, const FString& ComponentName)
{
	TArray<FEventInfo> Events;

	if (!Blueprint)
	{
		return Events;
	}

	// Find the component (checks both SCS and CDO)
	FComponentDiscoveryResult Discovery = FindComponentByName(Blueprint, ComponentName);
	UActorComponent* ComponentTemplate = Discovery.ComponentTemplate;

	if (!ComponentTemplate)
	{
		return Events;
	}

	// Find all multicast delegate properties on the component
	UClass* ComponentClass = ComponentTemplate->GetClass();
	for (TFieldIterator<FMulticastDelegateProperty> It(ComponentClass); It; ++It)
	{
		FMulticastDelegateProperty* DelegateProp = *It;

		// Skip if not BlueprintAssignable
		if (!DelegateProp->HasAnyPropertyFlags(CPF_BlueprintAssignable))
		{
			continue;
		}

		FEventInfo Info;
		Info.Name = DelegateProp->GetName();

		// Get signature from the delegate's signature function
		if (UFunction* SignatureFunc = DelegateProp->SignatureFunction)
		{
			FString Params;
			for (TFieldIterator<FProperty> ParamIt(SignatureFunc); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (Param->HasAnyPropertyFlags(CPF_Parm) && !Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (!Params.IsEmpty()) Params += TEXT(", ");
					Params += Param->GetName();
				}
			}
			Info.Signature = FString::Printf(TEXT("(%s)"), *Params);
		}
		else
		{
			Info.Signature = TEXT("()");
		}

		Events.Add(Info);
	}

	return Events;
}

TArray<FEditBlueprintTool::FEventInfo> FEditBlueprintTool::ListWidgetEvents(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName)
{
	TArray<FEventInfo> Events;

	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return Events;
	}

	// Find the widget
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*WidgetName));
	if (!Widget)
	{
		return Events;
	}

	// Find all multicast delegate properties on the widget
	UClass* WidgetClass = Widget->GetClass();
	for (TFieldIterator<FMulticastDelegateProperty> It(WidgetClass); It; ++It)
	{
		FMulticastDelegateProperty* DelegateProp = *It;

		// Skip if not BlueprintAssignable
		if (!DelegateProp->HasAnyPropertyFlags(CPF_BlueprintAssignable))
		{
			continue;
		}

		FEventInfo Info;
		Info.Name = DelegateProp->GetName();

		// Get signature
		if (UFunction* SignatureFunc = DelegateProp->SignatureFunction)
		{
			FString Params;
			for (TFieldIterator<FProperty> ParamIt(SignatureFunc); ParamIt; ++ParamIt)
			{
				FProperty* Param = *ParamIt;
				if (Param->HasAnyPropertyFlags(CPF_Parm) && !Param->HasAnyPropertyFlags(CPF_ReturnParm))
				{
					if (!Params.IsEmpty()) Params += TEXT(", ");
					Params += Param->GetName();
				}
			}
			Info.Signature = FString::Printf(TEXT("(%s)"), *Params);
		}
		else
		{
			Info.Signature = TEXT("()");
		}

		Events.Add(Info);
	}

	return Events;
}

FString FEditBlueprintTool::BindEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef)
{
	if (EventDef.Source.IsEmpty() || EventDef.Event.IsEmpty() || EventDef.Handler.IsEmpty())
	{
		return TEXT("! Event binding: Missing source, event, or handler");
	}

	// Route to widget or component binding based on Blueprint type
	if (UWidgetBlueprint* WidgetBlueprint = Cast<UWidgetBlueprint>(Blueprint))
	{
		return BindWidgetEvent(WidgetBlueprint, EventDef);
	}
	else
	{
		return BindComponentEvent(Blueprint, EventDef);
	}
}

FString FEditBlueprintTool::BindWidgetEvent(UWidgetBlueprint* WidgetBlueprint, const FEventBindingDef& EventDef)
{
	if (!WidgetBlueprint || !WidgetBlueprint->WidgetTree)
	{
		return TEXT("! Widget binding: Invalid Widget Blueprint");
	}

	// Find the widget to get its class
	UWidget* Widget = WidgetBlueprint->WidgetTree->FindWidget(FName(*EventDef.Source));
	if (!Widget)
	{
		return FString::Printf(TEXT("! Widget binding: Widget '%s' not found"), *EventDef.Source);
	}

	UClass* WidgetClass = Widget->GetClass();
	FName EventName(*EventDef.Event);
	FName PropertyName(*EventDef.Source);

	// Find the corresponding variable property in the Blueprint's skeleton class
	// (Widgets become FObjectProperty variables in the generated class)
	FObjectProperty* VariableProperty = nullptr;
	if (WidgetBlueprint->SkeletonGeneratedClass)
	{
		VariableProperty = FindFProperty<FObjectProperty>(WidgetBlueprint->SkeletonGeneratedClass, PropertyName);
	}

	if (!VariableProperty)
	{
		return FString::Printf(TEXT("! Widget binding: Could not find property for widget '%s'. Try compiling the Blueprint first."),
			*EventDef.Source);
	}

	// Check if event already exists using engine utility
	if (const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, EventName, PropertyName))
	{
		// Return existing node info so AI can still wire it
		FString Output = FString::Printf(TEXT("! Widget binding: Event '%s' on '%s' already exists\n"),
			*EventDef.Event, *EventDef.Source);
		Output += FString::Printf(TEXT("  GUID: %s\n"), *ExistingNode->NodeGuid.ToString());
		Output += TEXT("  Output Pins:");
		for (UEdGraphPin* Pin : ExistingNode->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
			}
		}
		return Output;
	}

	// Use engine utility to create the bound event node (same as clicking "+" in editor)
	FKismetEditorUtilities::CreateNewBoundEventForClass(WidgetClass, EventName, WidgetBlueprint, VariableProperty);

	// Find the newly created node to return its info
	const UK2Node_ComponentBoundEvent* NewNode = FKismetEditorUtilities::FindBoundEventForComponent(WidgetBlueprint, EventName, PropertyName);
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Widget binding: Event created but node not found for %s.%s"),
			*EventDef.Source, *EventDef.Event);
	}

	// Build output with GUID and pins for wiring
	FString Output = FString::Printf(TEXT("+ Created event: %s.%s\n"), *EventDef.Source, *EventDef.Event);
	Output += FString::Printf(TEXT("  GUID: %s\n"), *NewNode->NodeGuid.ToString());
	Output += TEXT("  Output Pins:");
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
		}
	}

	return Output;
}

FString FEditBlueprintTool::BindComponentEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef)
{
	if (!Blueprint)
	{
		return TEXT("! Component binding: Invalid Blueprint");
	}

	// Find the component (checks both SCS and CDO)
	FComponentDiscoveryResult Discovery = FindComponentByName(Blueprint, EventDef.Source);

	if (!Discovery.ComponentTemplate)
	{
		return FString::Printf(TEXT("! Component binding: Component '%s' not found"), *EventDef.Source);
	}

	UClass* ComponentClass = Discovery.ComponentTemplate->GetClass();
	FName EventName(*EventDef.Event);
	FName PropertyName = Discovery.VariableName;

	// Find the FObjectProperty for this component in the Blueprint class
	FObjectProperty* ComponentProperty = nullptr;
	if (Blueprint->SkeletonGeneratedClass)
	{
		ComponentProperty = FindFProperty<FObjectProperty>(Blueprint->SkeletonGeneratedClass, PropertyName);
	}
	if (!ComponentProperty && Blueprint->GeneratedClass)
	{
		ComponentProperty = FindFProperty<FObjectProperty>(Blueprint->GeneratedClass, PropertyName);
	}

	if (!ComponentProperty)
	{
		return FString::Printf(TEXT("! Component binding: Could not find property for component '%s'. Try compiling the Blueprint first."),
			*EventDef.Source);
	}

	// Check if event already exists using engine utility
	if (const UK2Node_ComponentBoundEvent* ExistingNode = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, EventName, PropertyName))
	{
		// Return existing node info so AI can still wire it
		FString Output = FString::Printf(TEXT("! Component binding: Event '%s' on '%s' already exists\n"),
			*EventDef.Event, *EventDef.Source);
		Output += FString::Printf(TEXT("  GUID: %s\n"), *ExistingNode->NodeGuid.ToString());
		Output += TEXT("  Output Pins:");
		for (UEdGraphPin* Pin : ExistingNode->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
			}
		}
		return Output;
	}

	// Use engine utility to create the bound event node (same as clicking "+" in editor)
	FKismetEditorUtilities::CreateNewBoundEventForClass(ComponentClass, EventName, Blueprint, ComponentProperty);

	// Find the newly created node to return its info
	const UK2Node_ComponentBoundEvent* NewNode = FKismetEditorUtilities::FindBoundEventForComponent(Blueprint, EventName, PropertyName);
	if (!NewNode)
	{
		return FString::Printf(TEXT("! Component binding: Event created but node not found for %s.%s"),
			*EventDef.Source, *EventDef.Event);
	}

	// Build output with GUID and pins for wiring
	FString Output = FString::Printf(TEXT("+ Created event: %s.%s\n"), *EventDef.Source, *EventDef.Event);
	Output += FString::Printf(TEXT("  GUID: %s\n"), *NewNode->NodeGuid.ToString());
	Output += TEXT("  Output Pins:");
	for (UEdGraphPin* Pin : NewNode->Pins)
	{
		if (Pin->Direction == EGPD_Output)
		{
			Output += FString::Printf(TEXT(" %s"), *Pin->PinName.ToString());
		}
	}

	return Output;
}

FString FEditBlueprintTool::UnbindEvent(UBlueprint* Blueprint, const FString& Source, const FString& Event)
{
	if (Source.IsEmpty() || Event.IsEmpty())
	{
		return TEXT("! Unbind: Missing source or event");
	}

	// Both Widget Blueprints and regular Blueprints use UK2Node_ComponentBoundEvent
	// when events are created via the "+" button (or our bind_events)
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		for (int32 i = Graph->Nodes.Num() - 1; i >= 0; i--)
		{
			if (UK2Node_ComponentBoundEvent* BoundEvent = Cast<UK2Node_ComponentBoundEvent>(Graph->Nodes[i]))
			{
				if (BoundEvent->ComponentPropertyName.ToString().Equals(Source, ESearchCase::IgnoreCase) &&
					BoundEvent->DelegatePropertyName.ToString().Equals(Event, ESearchCase::IgnoreCase))
				{
					Graph->RemoveNode(BoundEvent);
					Blueprint->Modify();
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					return FString::Printf(TEXT("- Removed event: %s.%s"), *Source, *Event);
				}
			}
		}
	}

	return FString::Printf(TEXT("! Unbind: No event found for %s.%s"), *Source, *Event);
}

FEditBlueprintTool::FComponentDiscoveryResult FEditBlueprintTool::FindComponentByName(UBlueprint* Blueprint, const FString& ComponentName)
{
	FComponentDiscoveryResult Result;

	if (!Blueprint)
	{
		return Result;
	}

	// First: Check SimpleConstructionScript (catches recently added, uncompiled components)
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				Result.ComponentTemplate = Node->ComponentTemplate;
				Result.SCSNode = Node;
				Result.VariableName = Node->GetVariableName();
				Result.bFoundInSCS = true;
				return Result;  // SCS is authoritative for uncompiled changes
			}
		}
	}

	// Second: Check CDO (catches compiled components that might not be in SCS)
	UBlueprintGeneratedClass* BPClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass);
	if (BPClass)
	{
		AActor* CDO = Cast<AActor>(BPClass->GetDefaultObject());
		if (CDO)
		{
			TArray<UActorComponent*> Components;
			CDO->GetComponents<UActorComponent>(Components);

			for (UActorComponent* Component : Components)
			{
				if (Component && Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
				{
					Result.ComponentTemplate = Component;
					Result.VariableName = FName(*ComponentName);
					Result.bFoundInCDO = true;
					return Result;
				}
			}
		}
	}

	return Result;  // Not found
}

// =============================================================================
// Animation Blueprint State Machine Operations
// =============================================================================

UEdGraph* FEditBlueprintTool::FindAnimGraph(UAnimBlueprint* AnimBlueprint)
{
	if (!AnimBlueprint)
	{
		return nullptr;
	}

	for (UEdGraph* Graph : AnimBlueprint->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
		{
			return Graph;
		}
	}

	return nullptr;
}

UAnimGraphNode_StateMachine* FEditBlueprintTool::FindStateMachineNode(UAnimBlueprint* AnimBlueprint, const FString& StateMachineName)
{
	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	if (!AnimGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_StateMachine* SMNode = Cast<UAnimGraphNode_StateMachine>(Node))
		{
			FString SMName = SMNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (SMName.Equals(StateMachineName, ESearchCase::IgnoreCase))
			{
				return SMNode;
			}
		}
	}

	return nullptr;
}

UAnimStateNode* FEditBlueprintTool::FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName)
{
	if (!SMGraph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
		{
			FString NodeName = StateNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
			if (NodeName.Equals(StateName, ESearchCase::IgnoreCase))
			{
				return StateNode;
			}
		}
	}

	return nullptr;
}

FString FEditBlueprintTool::AddStateMachine(UAnimBlueprint* AnimBlueprint, const FStateMachineDefinition& SMDef)
{
	if (SMDef.Name.IsEmpty())
	{
		return TEXT("! StateMachine: Missing name");
	}

	// Find AnimGraph
	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	if (!AnimGraph)
	{
		return TEXT("! StateMachine: AnimGraph not found. Open the Animation Blueprint in the editor first.");
	}

	// Check if state machine already exists
	if (FindStateMachineNode(AnimBlueprint, SMDef.Name))
	{
		return FString::Printf(TEXT("! StateMachine: '%s' already exists"), *SMDef.Name);
	}

	// CRITICAL: Call Modify() BEFORE making changes (engine pattern for undo/redo)
	AnimGraph->Modify();

	// Create the state machine node
	UAnimGraphNode_StateMachine* NewSMNode = NewObject<UAnimGraphNode_StateMachine>(AnimGraph);
	if (!NewSMNode)
	{
		return FString::Printf(TEXT("! StateMachine: Failed to create '%s'"), *SMDef.Name);
	}

	// Set up the node
	NewSMNode->CreateNewGuid();
	AnimGraph->AddNode(NewSMNode, false, false);
	NewSMNode->SetFlags(RF_Transactional);

	// PostPlacedNewNode creates the EditorStateMachineGraph, initializes default nodes,
	// and registers it as a subgraph of the AnimGraph (engine pattern).
	NewSMNode->PostPlacedNewNode();
	// Allocate default pins (engine schema path does this explicitly)
	NewSMNode->AllocateDefaultPins();

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(NewSMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("StateMachine: EditorStateMachineGraph not created for '%s'"), *SMDef.Name);
		return FString::Printf(TEXT("! StateMachine: Failed to create graph for '%s'"), *SMDef.Name);
	}

	// Rename the state machine graph to the requested name
	TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(NewSMNode);
	FBlueprintEditorUtils::RenameGraphWithSuggestion(SMGraph, NameValidator, SMDef.Name);

	// Ensure the state machine graph is registered as a subgraph (defensive)
	if (AnimGraph->SubGraphs.Find(SMGraph) == INDEX_NONE)
	{
		AnimGraph->Modify();
		AnimGraph->SubGraphs.Add(SMGraph);
	}

	// Position the node in the AnimGraph
	NewSMNode->NodePosX = 200;
	NewSMNode->NodePosY = 0;

	// Mark blueprint as structurally modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	FString NodeGuid = NewSMNode->NodeGuid.ToString();
	return FString::Printf(TEXT("+ StateMachine: %s (GUID: %s)"), *SMDef.Name, *NodeGuid);
}

FString FEditBlueprintTool::AddAnimState(UAnimBlueprint* AnimBlueprint, const FAnimStateDefinition& StateDef)
{
	if (StateDef.Name.IsEmpty())
	{
		return TEXT("! AnimState: Missing state name");
	}
	if (StateDef.StateMachine.IsEmpty())
	{
		return TEXT("! AnimState: Missing state_machine parameter");
	}

	// Find the state machine
	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBlueprint, StateDef.StateMachine);
	if (!SMNode)
	{
		return FString::Printf(TEXT("! AnimState: State machine '%s' not found"), *StateDef.StateMachine);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return FString::Printf(TEXT("! AnimState: State machine '%s' has no graph"), *StateDef.StateMachine);
	}

	// Check if state already exists
	if (FindStateNode(SMGraph, StateDef.Name))
	{
		return FString::Printf(TEXT("! AnimState: State '%s' already exists in '%s'"), *StateDef.Name, *StateDef.StateMachine);
	}

	// Create the state node
	UAnimStateNode* NewStateNode = NewObject<UAnimStateNode>(SMGraph);
	if (!NewStateNode)
	{
		return FString::Printf(TEXT("! AnimState: Failed to create state '%s'"), *StateDef.Name);
	}

	// CRITICAL: Call Modify() BEFORE making changes (engine pattern for undo/redo)
	SMGraph->Modify();

	// Set up the node - IMPORTANT: Add to graph BEFORE calling PostPlacedNewNode
	// PostPlacedNewNode creates the BoundGraph with proper schema and result node
	NewStateNode->CreateNewGuid();
	SMGraph->AddNode(NewStateNode, false, false);
	NewStateNode->SetFlags(RF_Transactional);

	// PostPlacedNewNode creates the BoundGraph with UAnimationStateGraphSchema
	// which properly initializes the StateResult node required for compilation
	NewStateNode->PostPlacedNewNode();
	// Allocate default pins (engine schema path does this explicitly)
	NewStateNode->AllocateDefaultPins();

	// Validate BoundGraph was created (engine uses check() for this)
	if (!NewStateNode->BoundGraph)
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("AnimState: BoundGraph not created for state '%s'"), *StateDef.Name);
		return FString::Printf(TEXT("! AnimState: Failed to create state graph for '%s'"), *StateDef.Name);
	}

	// Rename the BoundGraph to match the state name
	TSharedPtr<INameValidatorInterface> NameValidator = FNameValidatorFactory::MakeValidator(NewStateNode);
	FBlueprintEditorUtils::RenameGraphWithSuggestion(NewStateNode->BoundGraph, NameValidator, StateDef.Name);

	// Position the node based on existing nodes to avoid overlap
	// Calculate max Y position from existing state nodes
	int32 MaxY = 0;
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateNode* ExistingState = Cast<UAnimStateNode>(Node))
		{
			MaxY = FMath::Max(MaxY, static_cast<int32>(ExistingState->NodePosY));
		}
	}
	NewStateNode->NodePosX = 300;
	NewStateNode->NodePosY = MaxY + 150;  // Position below existing states

	FString NodeGuid = NewStateNode->NodeGuid.ToString();
	FString BoundGraphName = NewStateNode->BoundGraph ? NewStateNode->BoundGraph->GetName() : TEXT("none");

	// Mark blueprint as structurally modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	return FString::Printf(TEXT("+ AnimState: %s in %s (GUID: %s, graph: %s)"),
		*StateDef.Name, *StateDef.StateMachine, *NodeGuid, *BoundGraphName);
}

FString FEditBlueprintTool::AddStateTransition(UAnimBlueprint* AnimBlueprint, const FStateTransitionDefinition& TransDef)
{
	if (TransDef.StateMachine.IsEmpty())
	{
		return TEXT("! Transition: Missing state_machine parameter");
	}
	if (TransDef.FromState.IsEmpty() || TransDef.ToState.IsEmpty())
	{
		return TEXT("! Transition: Missing from_state or to_state parameter");
	}

	// Find the state machine
	UAnimGraphNode_StateMachine* SMNode = FindStateMachineNode(AnimBlueprint, TransDef.StateMachine);
	if (!SMNode)
	{
		return FString::Printf(TEXT("! Transition: State machine '%s' not found"), *TransDef.StateMachine);
	}

	UAnimationStateMachineGraph* SMGraph = Cast<UAnimationStateMachineGraph>(SMNode->EditorStateMachineGraph);
	if (!SMGraph)
	{
		return FString::Printf(TEXT("! Transition: State machine '%s' has no graph"), *TransDef.StateMachine);
	}

	// Find source and destination states
	// Note: UAnimStateEntryNode is NOT a UAnimStateNodeBase in UE 5.7+, so handle separately
	UEdGraphNode* FromNode = nullptr;
	UAnimStateNodeBase* ToStateNode = nullptr;
	bool bFromEntry = false;

	// Check for [Entry] as special source
	if (TransDef.FromState.Equals(TEXT("[Entry]"), ESearchCase::IgnoreCase) ||
		TransDef.FromState.Equals(TEXT("Entry"), ESearchCase::IgnoreCase))
	{
		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateEntryNode* EntryNode = Cast<UAnimStateEntryNode>(Node))
			{
				FromNode = EntryNode;
				bFromEntry = true;
				break;
			}
		}
	}
	else
	{
		FromNode = FindStateNode(SMGraph, TransDef.FromState);
	}

	ToStateNode = FindStateNode(SMGraph, TransDef.ToState);

	if (!FromNode)
	{
		return FString::Printf(TEXT("! Transition: Source state '%s' not found"), *TransDef.FromState);
	}
	if (!ToStateNode)
	{
		return FString::Printf(TEXT("! Transition: Target state '%s' not found"), *TransDef.ToState);
	}

	// Check if transition already exists
	for (UEdGraphNode* Node : SMGraph->Nodes)
	{
		if (UAnimStateTransitionNode* ExistingTrans = Cast<UAnimStateTransitionNode>(Node))
		{
			// Safely check pins - GetInputPin/GetOutputPin can crash if Pins array is empty
			// Also GetPreviousState/GetNextState crash if pins have no connections
			if (ExistingTrans->Pins.Num() < 2)
			{
				// Skip malformed transition nodes with missing pins
				continue;
			}

			UEdGraphPin* InputPin = ExistingTrans->GetInputPin();
			UEdGraphPin* OutputPin = ExistingTrans->GetOutputPin();
			if (!InputPin || !OutputPin || InputPin->LinkedTo.Num() == 0 || OutputPin->LinkedTo.Num() == 0)
			{
				// Skip transition nodes with unconnected pins
				continue;
			}

			// Safely get previous/next states - these can return nullptr
			UAnimStateNodeBase* ExistingFrom = ExistingTrans->GetPreviousState();
			UAnimStateNodeBase* ExistingTo = ExistingTrans->GetNextState();

			// Skip transitions with invalid state references
			if (!ExistingTo)
			{
				continue;
			}

			// Compare nodes directly (GetPreviousState returns UAnimStateNodeBase, but we compare pointers)
			// Note: ExistingFrom can be nullptr for entry transitions
			bool bSameFrom = (ExistingFrom == FromNode) ||
				(bFromEntry && !ExistingFrom);  // Entry transitions have no previous state
			if (bSameFrom && ExistingTo == ToStateNode)
			{
				// Return the existing transition info for wiring
				FString TransGuid = ExistingTrans->NodeGuid.ToString();
				FString TransGraphName = TEXT("none");
				if (UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(ExistingTrans->BoundGraph))
				{
					TransGraphName = TransGraph->GetName();
				}
				return FString::Printf(TEXT("! Transition: %s -> %s already exists (GUID: %s, graph: %s)"),
					*TransDef.FromState, *TransDef.ToState, *TransGuid, *TransGraphName);
			}
		}
	}

	// Create the transition node
	UAnimStateTransitionNode* TransitionNode = NewObject<UAnimStateTransitionNode>(SMGraph);
	if (!TransitionNode)
	{
		return FString::Printf(TEXT("! Transition: Failed to create transition from '%s' to '%s'"),
			*TransDef.FromState, *TransDef.ToState);
	}

	// CRITICAL: Modify() is called later in pin wiring section (engine pattern)
	// Set up the node - IMPORTANT: Add to graph BEFORE calling PostPlacedNewNode
	// PostPlacedNewNode calls CreateBoundGraph() which creates the transition graph
	// with proper UAnimationTransitionSchema and TransitionResult node
	TransitionNode->CreateNewGuid();
	SMGraph->AddNode(TransitionNode, false, false);
	TransitionNode->SetFlags(RF_Transactional);

	// PostPlacedNewNode creates BoundGraph via CreateBoundGraph() with proper schema
	// This initializes the TransitionResult node required for compilation
	TransitionNode->PostPlacedNewNode();
	// Allocate default pins (engine schema path does this explicitly)
	TransitionNode->AllocateDefaultPins();

	// Position between states
	TransitionNode->NodePosX = (FromNode->NodePosX + ToStateNode->NodePosX) / 2;
	TransitionNode->NodePosY = (FromNode->NodePosY + ToStateNode->NodePosY) / 2;

	// Get the transition graph created by PostPlacedNewNode
	UAnimationTransitionGraph* TransGraph = Cast<UAnimationTransitionGraph>(TransitionNode->BoundGraph);
	if (!TransGraph)
	{
		return FString::Printf(TEXT("! Transition: Failed to create transition graph from '%s' to '%s'"),
			*TransDef.FromState, *TransDef.ToState);
	}

	// Get the result node created by the schema's CreateDefaultNodesForGraph
	UAnimGraphNode_TransitionResult* ResultNode = TransGraph->MyResultNode;
	if (!ResultNode)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("Transition result node not created by schema for '%s' -> '%s'"),
			*TransDef.FromState, *TransDef.ToState);
	}

	// Ensure pins exist before wiring (transition nodes require pins)
	if (TransitionNode->Pins.Num() < 2)
	{
		return FString::Printf(TEXT("! Transition: Transition node pins not created for '%s' -> '%s'"),
			*TransDef.FromState, *TransDef.ToState);
	}

	// CRITICAL: Call Modify() on graph BEFORE making connections (engine pattern)
	SMGraph->Modify();

	// Use engine path to wire transitions (ensures pins and invariants)
	if (bFromEntry)
	{
		// Entry transitions are connected via pin linking (entry node isn't a UAnimStateNodeBase)
		UEdGraphPin* EntryOutput = nullptr;
		for (UEdGraphPin* Pin : FromNode->Pins)
		{
			if (Pin->Direction == EGPD_Output)
			{
				EntryOutput = Pin;
				break;
			}
		}
		UEdGraphPin* TransInput = TransitionNode->GetInputPin();
		UEdGraphPin* TransOutput = TransitionNode->GetOutputPin();
		UEdGraphPin* ToInput = ToStateNode->GetInputPin();

		if (EntryOutput && TransInput)
		{
			EntryOutput->Modify();
			TransInput->Modify();
			EntryOutput->MakeLinkTo(TransInput);
		}
		if (TransOutput && ToInput)
		{
			TransOutput->Modify();
			ToInput->Modify();
			TransOutput->MakeLinkTo(ToInput);
		}
	}
	else
	{
		UAnimStateNodeBase* FromStateNode = Cast<UAnimStateNodeBase>(FromNode);
		if (!FromStateNode)
		{
			return FString::Printf(TEXT("! Transition: Source node is not a state for '%s' -> '%s'"),
				*TransDef.FromState, *TransDef.ToState);
		}
		TransitionNode->CreateConnections(FromStateNode, ToStateNode);
	}

	// Build result with info for adding condition nodes to the transition graph
	FString TransGuid = TransitionNode->NodeGuid.ToString();
	FString ResultGuid = ResultNode ? ResultNode->NodeGuid.ToString() : TEXT("none");

	FString Output = FString::Printf(TEXT("+ Transition: %s -> %s in %s\n"),
		*TransDef.FromState, *TransDef.ToState, *TransDef.StateMachine);
	Output += FString::Printf(TEXT("  GUID: %s\n"), *TransGuid);
	Output += FString::Printf(TEXT("  Condition Graph: %s\n"), *TransGraph->GetName());
	Output += FString::Printf(TEXT("  Result Node GUID: %s (connect to bCanEnterTransition pin)\n"), *ResultGuid);

	// Mark blueprint as structurally modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	return Output;
}

// Linked Anim Layer operations

FString FEditBlueprintTool::AddLinkedAnimLayer(UAnimBlueprint* AnimBlueprint, const FLinkedAnimLayerDefinition& LayerDef)
{
	if (LayerDef.LayerName.IsEmpty())
	{
		return TEXT("! LinkedAnimLayer: Missing 'layer_name'");
	}

	// Find the AnimGraph
	UEdGraph* AnimGraph = FindAnimGraph(AnimBlueprint);
	if (!AnimGraph)
	{
		return TEXT("! LinkedAnimLayer: AnimGraph not found. Open the Animation Blueprint in the editor first.");
	}

	// Resolve the AnimLayerInterface class
	TSubclassOf<UAnimLayerInterface> InterfaceClass;

	if (!LayerDef.Interface.IsEmpty())
	{
		// Explicit interface specified — look it up
		UClass* FoundClass = nullptr;

		// Try direct class lookup
		FoundClass = FindObject<UClass>(nullptr, *LayerDef.Interface);

		// Try as Blueprint asset path
		if (!FoundClass)
		{
			UBlueprint* InterfaceBP = LoadObject<UBlueprint>(nullptr, *LayerDef.Interface);
			if (InterfaceBP && InterfaceBP->GeneratedClass)
			{
				FoundClass = InterfaceBP->GeneratedClass;
			}
		}

		// Try with _C suffix
		if (!FoundClass)
		{
			FString WithSuffix = LayerDef.Interface + TEXT("_C");
			FoundClass = FindObject<UClass>(nullptr, *WithSuffix);
		}

		// Try BuildAssetPath + load
		if (!FoundClass)
		{
			FString AssetPath = NeoStackToolUtils::BuildAssetPath(LayerDef.Interface, TEXT(""));
			UBlueprint* InterfaceBP = LoadObject<UBlueprint>(nullptr, *AssetPath);
			if (InterfaceBP && InterfaceBP->GeneratedClass)
			{
				FoundClass = InterfaceBP->GeneratedClass;
			}
		}

		if (!FoundClass || !FoundClass->IsChildOf(UAnimLayerInterface::StaticClass()))
		{
			return FString::Printf(TEXT("! LinkedAnimLayer: '%s' is not a valid AnimLayerInterface"), *LayerDef.Interface);
		}

		InterfaceClass = FoundClass;
	}
	else
	{
		// Auto-detect: find AnimLayerInterface from ImplementedInterfaces
		int32 AnimLayerCount = 0;
		for (const FBPInterfaceDescription& Desc : AnimBlueprint->ImplementedInterfaces)
		{
			if (Desc.Interface && Desc.Interface->IsChildOf(UAnimLayerInterface::StaticClass()))
			{
				InterfaceClass = Desc.Interface;
				AnimLayerCount++;
			}
		}

		if (AnimLayerCount == 0)
		{
			return TEXT("! LinkedAnimLayer: No AnimLayerInterface implemented on this AnimBP. Use add_interfaces first.");
		}
		if (AnimLayerCount > 1)
		{
			return TEXT("! LinkedAnimLayer: Multiple AnimLayerInterfaces found — specify 'interface' to disambiguate");
		}
	}

	// Verify the layer function exists on the interface
	UFunction* LayerFunc = InterfaceClass->FindFunctionByName(FName(*LayerDef.LayerName));
	if (!LayerFunc)
	{
		// List available functions for helpful error
		TArray<FString> AvailableFuncs;
		for (TFieldIterator<UFunction> It(InterfaceClass.Get(), EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintEvent))
			{
				AvailableFuncs.Add(It->GetName());
			}
		}
		FString Available = AvailableFuncs.Num() > 0
			? FString::Join(AvailableFuncs, TEXT(", "))
			: TEXT("none");
		return FString::Printf(TEXT("! LinkedAnimLayer: Function '%s' not found on interface '%s'. Available: %s"),
			*LayerDef.LayerName, *InterfaceClass->GetName(), *Available);
	}

	// Create the LinkedAnimLayer node
	AnimGraph->Modify();

	UAnimGraphNode_LinkedAnimLayer* LayerNode = NewObject<UAnimGraphNode_LinkedAnimLayer>(AnimGraph);
	if (!LayerNode)
	{
		return FString::Printf(TEXT("! LinkedAnimLayer: Failed to create node for '%s'"), *LayerDef.LayerName);
	}

	// Set interface BEFORE PostPlacedNewNode so pins are created correctly
	LayerNode->Node.Interface = InterfaceClass;

	LayerNode->CreateNewGuid();
	AnimGraph->AddNode(LayerNode, false, false);
	LayerNode->SetFlags(RF_Transactional);
	LayerNode->PostPlacedNewNode();
	LayerNode->AllocateDefaultPins();

	// Replicate SetLayerName() inline — the method isn't exported from AnimGraph (MinimalAPI)
	{
		FName LayerFName(*LayerDef.LayerName);

		// 1. Set Node.Layer directly (public UPROPERTY on FAnimNode_LinkedAnimLayer)
		LayerNode->Node.Layer = LayerFName;

		// 2. Access protected FunctionReference via UProperty reflection
		FProperty* FuncRefProp = LayerNode->GetClass()->FindPropertyByName(TEXT("FunctionReference"));
		if (FuncRefProp)
		{
			FMemberReference* FuncRef = FuncRefProp->ContainerPtrToValuePtr<FMemberReference>(LayerNode);
			if (UClass* TargetClass = LayerNode->GetTargetClass())
			{
				FGuid FunctionGuid;
				FBlueprintEditorUtils::GetFunctionGuidFromClassByFieldName(
					FBlueprintEditorUtils::GetMostUpToDateClass(TargetClass), LayerFName, FunctionGuid);
				FuncRef->SetExternalMember(LayerFName, TargetClass, FunctionGuid);
			}
			else
			{
				FuncRef->SetSelfMember(LayerFName);
			}
		}

		// 3. Replicate UpdateGuidForLayer() — InterfaceGuid is public UPROPERTY
		if (!LayerNode->InterfaceGuid.IsValid())
		{
			// Replicate protected GetGuidForLayer(): find matching interface graph GUID
			{
				for (FBPInterfaceDescription& Desc : AnimBlueprint->ImplementedInterfaces)
				{
					for (UEdGraph* Graph : Desc.Graphs)
					{
						if (Graph && Graph->GetFName() == LayerFName)
						{
							LayerNode->InterfaceGuid = Graph->InterfaceGuid;
							break;
						}
					}
					if (LayerNode->InterfaceGuid.IsValid()) break;
				}
			}
		}
	}

	// Reconstruct node to rebuild pins based on the layer function signature
	LayerNode->ReconstructNode();

	// Position based on existing nodes to avoid overlap
	int32 MaxY = 0;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (Node && Node != LayerNode)
		{
			MaxY = FMath::Max(MaxY, static_cast<int32>(Node->NodePosY));
		}
	}
	LayerNode->NodePosX = 400;
	LayerNode->NodePosY = MaxY + 150;

	// Mark as structurally modified
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBlueprint);

	FString NodeGuid = LayerNode->NodeGuid.ToString();
	FString Output = FString::Printf(TEXT("+ LinkedAnimLayer: '%s' from '%s' (GUID: %s)\n"),
		*LayerDef.LayerName, *InterfaceClass->GetName(), *NodeGuid);
	Output += FString::Printf(TEXT("  Use edit_graph with graph_name='animlayer:%s' to edit the layer function graph"),
		*LayerDef.LayerName);

	return Output;
}

// Timeline operations

UTimelineTemplate* FEditBlueprintTool::FindTimelineTemplate(UBlueprint* Blueprint, const FString& TimelineName)
{
	if (!Blueprint) return nullptr;

	FName TargetName = FName(*TimelineName);
	for (UTimelineTemplate* Timeline : Blueprint->Timelines)
	{
		if (Timeline)
		{
			FName VarName = Timeline->GetVariableName();
			if (VarName == TargetName)
			{
				return Timeline;
			}
		}
	}
	return nullptr;
}

UK2Node_Timeline* FEditBlueprintTool::FindTimelineNode(UBlueprint* Blueprint, const FString& TimelineName)
{
	if (!Blueprint) return nullptr;

	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (!EventGraph) return nullptr;

	FName TargetName = FName(*TimelineName);
	for (UEdGraphNode* Node : EventGraph->Nodes)
	{
		UK2Node_Timeline* TimelineNode = Cast<UK2Node_Timeline>(Node);
		if (TimelineNode && TimelineNode->TimelineName == TargetName)
		{
			return TimelineNode;
		}
	}
	return nullptr;
}

FString FEditBlueprintTool::AddTimeline(UBlueprint* Blueprint, const FTimelineDefinition& TimelineDef)
{
	if (TimelineDef.Name.IsEmpty())
	{
		return TEXT("! Timeline: Missing name");
	}

	// Check if Blueprint supports timelines
	if (!FBlueprintEditorUtils::DoesSupportTimelines(Blueprint))
	{
		return FString::Printf(TEXT("! Timeline: Blueprint type does not support timelines"));
	}

	// Find existing timeline (created when Timeline node was added via edit_graph)
	UTimelineTemplate* Timeline = FindTimelineTemplate(Blueprint, TimelineDef.Name);
	if (!Timeline)
	{
		return FString::Printf(TEXT("! Timeline: %s not found. First add a Timeline node named '%s' using edit_graph tool"), *TimelineDef.Name, *TimelineDef.Name);
	}

	// Configure timeline properties
	Timeline->TimelineLength = TimelineDef.Length;
	Timeline->bAutoPlay = TimelineDef.bAutoPlay;
	Timeline->bLoop = TimelineDef.bLoop;
	Timeline->bReplicated = TimelineDef.bReplicated;
	Timeline->bIgnoreTimeDilation = TimelineDef.bIgnoreTimeDilation;

	// Add tracks
	TArray<FString> TrackResults;
	for (const FTimelineTrackDef& TrackDef : TimelineDef.Tracks)
	{
		FString TrackResult = AddTimelineTrack(Blueprint, TimelineDef.Name, TrackDef);
		TrackResults.Add(TrackResult);
	}

	// Reconstruct the timeline node to update its output pins for new tracks
	UK2Node_Timeline* TimelineNode = FindTimelineNode(Blueprint, TimelineDef.Name);
	if (TimelineNode)
	{
		TimelineNode->ReconstructNode();
	}

	// Build result string
	FString Flags;
	if (TimelineDef.bAutoPlay) Flags += TEXT(" [AutoPlay]");
	if (TimelineDef.bLoop) Flags += TEXT(" [Loop]");
	if (TimelineDef.bReplicated) Flags += TEXT(" [Replicated]");

	FString Result = FString::Printf(TEXT("~ Timeline: %s configured (length=%.2fs)%s"), *TimelineDef.Name, TimelineDef.Length, *Flags);

	for (const FString& TrackResult : TrackResults)
	{
		Result += TEXT("\n  ") + TrackResult;
	}

	return Result;
}

FString FEditBlueprintTool::RemoveTimeline(UBlueprint* Blueprint, const FString& TimelineName)
{
	UTimelineTemplate* Timeline = FindTimelineTemplate(Blueprint, TimelineName);
	if (!Timeline)
	{
		return FString::Printf(TEXT("! Timeline: %s not found"), *TimelineName);
	}

	// Remove the timeline (this also removes the K2Node_Timeline)
	FBlueprintEditorUtils::RemoveTimeline(Blueprint, Timeline, true);

	return FString::Printf(TEXT("- Timeline: %s"), *TimelineName);
}

FString FEditBlueprintTool::AddTimelineTrack(UBlueprint* Blueprint, const FString& TimelineName, const FTimelineTrackDef& TrackDef)
{
	if (TrackDef.Name.IsEmpty())
	{
		return TEXT("! Track: Missing name");
	}

	UTimelineTemplate* Timeline = FindTimelineTemplate(Blueprint, TimelineName);
	if (!Timeline)
	{
		return FString::Printf(TEXT("! Track: Timeline %s not found"), *TimelineName);
	}

	FName TrackName = FName(*TrackDef.Name);

	// Check if track already exists (skip duplicates)
	for (const FTTFloatTrack& Track : Timeline->FloatTracks)
	{
		if (Track.GetTrackName() == TrackName)
		{
			return FString::Printf(TEXT("! Track: %s already exists on timeline %s"), *TrackDef.Name, *TimelineName);
		}
	}
	for (const FTTVectorTrack& Track : Timeline->VectorTracks)
	{
		if (Track.GetTrackName() == TrackName)
		{
			return FString::Printf(TEXT("! Track: %s already exists on timeline %s"), *TrackDef.Name, *TimelineName);
		}
	}
	for (const FTTLinearColorTrack& Track : Timeline->LinearColorTracks)
	{
		if (Track.GetTrackName() == TrackName)
		{
			return FString::Printf(TEXT("! Track: %s already exists on timeline %s"), *TrackDef.Name, *TimelineName);
		}
	}
	for (const FTTEventTrack& Track : Timeline->EventTracks)
	{
		if (Track.GetTrackName() == TrackName)
		{
			return FString::Printf(TEXT("! Track: %s already exists on timeline %s"), *TrackDef.Name, *TimelineName);
		}
	}

	// Create curve objects with Blueprint as outer (not GeneratedClass)
	UObject* CurveOuter = Blueprint;
	FString TrackTypeName;
	int32 KeyframeCount = 0;

	switch (TrackDef.Type)
	{
	case ETimelineTrackType::Float:
	{
		TrackTypeName = TEXT("Float");
		FTTFloatTrack NewTrack;
		NewTrack.SetTrackName(TrackName, Timeline);
		NewTrack.CurveFloat = NewObject<UCurveFloat>(CurveOuter, NAME_None, RF_Public);

		// Add keyframes
		if (NewTrack.CurveFloat)
		{
			FRichCurve& Curve = NewTrack.CurveFloat->FloatCurve;
			for (const FTimelineKeyframe& Key : TrackDef.Keyframes)
			{
				FKeyHandle Handle = Curve.AddKey(Key.Time, Key.Value);
				if (Key.InterpMode.Equals(TEXT("Constant"), ESearchCase::IgnoreCase))
				{
					Curve.SetKeyInterpMode(Handle, RCIM_Constant);
				}
				else if (Key.InterpMode.Equals(TEXT("Cubic"), ESearchCase::IgnoreCase))
				{
					Curve.SetKeyInterpMode(Handle, RCIM_Cubic);
				}
				KeyframeCount++;
			}
		}

		Timeline->FloatTracks.Add(NewTrack);
		break;
	}

	case ETimelineTrackType::Vector:
	{
		TrackTypeName = TEXT("Vector");
		FTTVectorTrack NewTrack;
		NewTrack.SetTrackName(TrackName, Timeline);
		NewTrack.CurveVector = NewObject<UCurveVector>(CurveOuter, NAME_None, RF_Public);

		if (NewTrack.CurveVector)
		{
			// FloatCurves is a fixed-size array [3], always has X/Y/Z
			for (const FTimelineKeyframe& Key : TrackDef.Keyframes)
			{
				NewTrack.CurveVector->FloatCurves[0].AddKey(Key.Time, Key.VectorValue.X);
				NewTrack.CurveVector->FloatCurves[1].AddKey(Key.Time, Key.VectorValue.Y);
				NewTrack.CurveVector->FloatCurves[2].AddKey(Key.Time, Key.VectorValue.Z);
				KeyframeCount++;
			}
		}

		Timeline->VectorTracks.Add(NewTrack);
		break;
	}

	case ETimelineTrackType::LinearColor:
	{
		TrackTypeName = TEXT("LinearColor");
		FTTLinearColorTrack NewTrack;
		NewTrack.SetTrackName(TrackName, Timeline);
		NewTrack.CurveLinearColor = NewObject<UCurveLinearColor>(CurveOuter, NAME_None, RF_Public);

		if (NewTrack.CurveLinearColor)
		{
			// FloatCurves is a fixed-size array [4], always has R/G/B/A
			for (const FTimelineKeyframe& Key : TrackDef.Keyframes)
			{
				NewTrack.CurveLinearColor->FloatCurves[0].AddKey(Key.Time, Key.ColorValue.R);
				NewTrack.CurveLinearColor->FloatCurves[1].AddKey(Key.Time, Key.ColorValue.G);
				NewTrack.CurveLinearColor->FloatCurves[2].AddKey(Key.Time, Key.ColorValue.B);
				NewTrack.CurveLinearColor->FloatCurves[3].AddKey(Key.Time, Key.ColorValue.A);
				KeyframeCount++;
			}
		}

		Timeline->LinearColorTracks.Add(NewTrack);
		break;
	}

	case ETimelineTrackType::Event:
	{
		TrackTypeName = TEXT("Event");
		FTTEventTrack NewTrack;
		NewTrack.SetTrackName(TrackName, Timeline);
		NewTrack.CurveKeys = NewObject<UCurveFloat>(CurveOuter, NAME_None, RF_Public);

		if (NewTrack.CurveKeys)
		{
			NewTrack.CurveKeys->bIsEventCurve = true;
			for (const FTimelineKeyframe& Key : TrackDef.Keyframes)
			{
				NewTrack.CurveKeys->FloatCurve.AddKey(Key.Time, 0.0f);
				KeyframeCount++;
			}
		}

		Timeline->EventTracks.Add(NewTrack);
		break;
	}
	}

	// Find and reconstruct the timeline node to update its pins
	UK2Node_Timeline* TimelineNode = FindTimelineNode(Blueprint, TimelineName);
	if (TimelineNode)
	{
		TimelineNode->ReconstructNode();
	}

	return FString::Printf(TEXT("+ Track: %s (%s, %d keyframes)"), *TrackDef.Name, *TrackTypeName, KeyframeCount);
}

FString FEditBlueprintTool::AddTimelineKeyframes(UBlueprint* Blueprint, const FString& TimelineName, const FString& TrackName, const TArray<FTimelineKeyframe>& Keyframes)
{
	UTimelineTemplate* Timeline = FindTimelineTemplate(Blueprint, TimelineName);
	if (!Timeline)
	{
		return FString::Printf(TEXT("! Keyframes: Timeline %s not found"), *TimelineName);
	}

	FName TargetTrackName = FName(*TrackName);
	int32 AddedCount = 0;

	// Search float tracks
	for (FTTFloatTrack& Track : Timeline->FloatTracks)
	{
		if (Track.GetTrackName() == TargetTrackName && Track.CurveFloat)
		{
			FRichCurve& Curve = Track.CurveFloat->FloatCurve;
			for (const FTimelineKeyframe& Key : Keyframes)
			{
				Curve.AddKey(Key.Time, Key.Value);
				AddedCount++;
			}
			return FString::Printf(TEXT("+ Keyframes: Added %d to %s.%s"), AddedCount, *TimelineName, *TrackName);
		}
	}

	// Search vector tracks
	for (FTTVectorTrack& Track : Timeline->VectorTracks)
	{
		if (Track.GetTrackName() == TargetTrackName && Track.CurveVector)
		{
			// FloatCurves is a fixed-size array [3], always has X/Y/Z
			for (const FTimelineKeyframe& Key : Keyframes)
			{
				Track.CurveVector->FloatCurves[0].AddKey(Key.Time, Key.VectorValue.X);
				Track.CurveVector->FloatCurves[1].AddKey(Key.Time, Key.VectorValue.Y);
				Track.CurveVector->FloatCurves[2].AddKey(Key.Time, Key.VectorValue.Z);
				AddedCount++;
			}
			return FString::Printf(TEXT("+ Keyframes: Added %d to %s.%s"), AddedCount, *TimelineName, *TrackName);
		}
	}

	// Search color tracks
	for (FTTLinearColorTrack& Track : Timeline->LinearColorTracks)
	{
		if (Track.GetTrackName() == TargetTrackName && Track.CurveLinearColor)
		{
			// FloatCurves is a fixed-size array [4], always has R/G/B/A
			for (const FTimelineKeyframe& Key : Keyframes)
			{
				Track.CurveLinearColor->FloatCurves[0].AddKey(Key.Time, Key.ColorValue.R);
				Track.CurveLinearColor->FloatCurves[1].AddKey(Key.Time, Key.ColorValue.G);
				Track.CurveLinearColor->FloatCurves[2].AddKey(Key.Time, Key.ColorValue.B);
				Track.CurveLinearColor->FloatCurves[3].AddKey(Key.Time, Key.ColorValue.A);
				AddedCount++;
			}
			return FString::Printf(TEXT("+ Keyframes: Added %d to %s.%s"), AddedCount, *TimelineName, *TrackName);
		}
	}

	// Search event tracks
	for (FTTEventTrack& Track : Timeline->EventTracks)
	{
		if (Track.GetTrackName() == TargetTrackName && Track.CurveKeys)
		{
			for (const FTimelineKeyframe& Key : Keyframes)
			{
				Track.CurveKeys->FloatCurve.AddKey(Key.Time, 0.0f);
				AddedCount++;
			}
			return FString::Printf(TEXT("+ Keyframes: Added %d to %s.%s"), AddedCount, *TimelineName, *TrackName);
		}
	}

	return FString::Printf(TEXT("! Keyframes: Track %s not found in %s"), *TrackName, *TimelineName);
}

// ── ASSET CREATION ─────────────────────────────────────────────────

UClass* FEditBlueprintTool::ResolveParentClass(const FString& ClassName)
{
	if (ClassName.IsEmpty()) return AActor::StaticClass();

	// Common shorthand names
	static const TMap<FString, UClass*> Shortcuts = {
		{ TEXT("actor"), AActor::StaticClass() },
		{ TEXT("pawn"), APawn::StaticClass() },
		{ TEXT("character"), ACharacter::StaticClass() },
		{ TEXT("gamemodebase"), AGameModeBase::StaticClass() },
		{ TEXT("gamemode"), AGameMode::StaticClass() },
		{ TEXT("playercontroller"), APlayerController::StaticClass() },
		{ TEXT("aicontroller"), AAIController::StaticClass() },
		{ TEXT("hud"), AHUD::StaticClass() },
		{ TEXT("playerstate"), APlayerState::StaticClass() },
		{ TEXT("gamestatebase"), AGameStateBase::StaticClass() },
		{ TEXT("actorcomponent"), UActorComponent::StaticClass() },
		{ TEXT("scenecomponent"), USceneComponent::StaticClass() },
		{ TEXT("object"), UObject::StaticClass() },
	};

	const FString Lower = ClassName.ToLower();
	if (UClass* const* Found = Shortcuts.Find(Lower))
	{
		return *Found;
	}

	// Try loading as a class path or finding by name
	return FindClassByName(ClassName);
}

FToolResult FEditBlueprintTool::HandleCreate(const FString& Name, const FString& Path, const TSharedPtr<FJsonObject>& CreateObj)
{
	FString Type;
	CreateObj->TryGetStringField(TEXT("type"), Type);
	if (Type.IsEmpty())
	{
		Type = TEXT("Blueprint");
	}

	const FString TypeLower = Type.ToLower();

	// Normalize the package path (remove trailing slash, ensure /Game prefix)
	FString PackagePath = Path;
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		PackagePath = TEXT("/Game/") + PackagePath;
	}
	while (PackagePath.EndsWith(TEXT("/")))
	{
		PackagePath.LeftChopInline(1);
	}

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
	UObject* CreatedAsset = nullptr;

	// ─── BLUEPRINT ───
	if (TypeLower == TEXT("blueprint") || TypeLower == TEXT("bp"))
	{
		FString ParentClassName;
		CreateObj->TryGetStringField(TEXT("parent_class"), ParentClassName);

		UClass* ParentClass = ResolveParentClass(ParentClassName);
		if (!ParentClass)
		{
			return FToolResult::Fail(FString::Printf(TEXT("! Create: Parent class '%s' not found"), *ParentClassName));
		}

		UBlueprintFactory* Factory = NewObject<UBlueprintFactory>();
		Factory->ParentClass = ParentClass;
		Factory->bSkipClassPicker = true;

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), Factory);
	}
	// ─── ANIMATION BLUEPRINT ───
	else if (TypeLower == TEXT("animblueprint") || TypeLower == TEXT("animbp") || TypeLower == TEXT("animation_blueprint"))
	{
		FString SkeletonPath;
		if (!CreateObj->TryGetStringField(TEXT("skeleton"), SkeletonPath) || SkeletonPath.IsEmpty())
		{
			return FToolResult::Fail(TEXT("! Create AnimBlueprint: 'skeleton' is required (path to USkeleton asset)"));
		}

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
		if (!Skeleton)
		{
			// Try building asset path
			FString BuiltPath = NeoStackToolUtils::BuildAssetPath(SkeletonPath, TEXT(""));
			Skeleton = LoadObject<USkeleton>(nullptr, *BuiltPath);
		}
		if (!Skeleton)
		{
			return FToolResult::Fail(FString::Printf(TEXT("! Create AnimBlueprint: Skeleton not found: %s"), *SkeletonPath));
		}

		UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
		Factory->TargetSkeleton = Skeleton;
		Factory->ParentClass = UAnimInstance::StaticClass();

		// Check for custom parent class
		FString ParentClassName;
		if (CreateObj->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
		{
			UClass* ParentClass = FindClassByName(ParentClassName);
			if (ParentClass && ParentClass->IsChildOf(UAnimInstance::StaticClass()))
			{
				Factory->ParentClass = ParentClass;
			}
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	}
	// ─── WIDGET BLUEPRINT ───
	else if (TypeLower == TEXT("widgetblueprint") || TypeLower == TEXT("widget") || TypeLower == TEXT("wbp") || TypeLower == TEXT("widget_blueprint"))
	{
		UWidgetBlueprintFactory* Factory = NewObject<UWidgetBlueprintFactory>();

		FString ParentClassName;
		if (CreateObj->TryGetStringField(TEXT("parent_class"), ParentClassName) && !ParentClassName.IsEmpty())
		{
			UClass* ParentClass = FindClassByName(ParentClassName);
			if (ParentClass && ParentClass->IsChildOf(UUserWidget::StaticClass()))
			{
				Factory->ParentClass = ParentClass;
			}
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UWidgetBlueprint::StaticClass(), Factory);
	}
	// ─── BLUEPRINT INTERFACE ───
	else if (TypeLower == TEXT("interface") || TypeLower == TEXT("blueprintinterface") || TypeLower == TEXT("blueprint_interface"))
	{
		// UE 5.5 does not export UBlueprintInterfaceFactory symbols for direct C++ instantiation.
		// Resolve and instantiate the factory by class path to stay binary-compatible across versions.
		UClass* InterfaceFactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/UnrealEd.BlueprintInterfaceFactory"));
		if (!InterfaceFactoryClass)
		{
			return FToolResult::Fail(TEXT("! Create: BlueprintInterfaceFactory class not found"));
		}

		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), InterfaceFactoryClass);
		if (!Factory)
		{
			return FToolResult::Fail(TEXT("! Create: Failed to instantiate BlueprintInterfaceFactory"));
		}

		if (FBoolProperty* SkipPickerProp = CastField<FBoolProperty>(InterfaceFactoryClass->FindPropertyByName(TEXT("bSkipClassPicker"))))
		{
			SkipPickerProp->SetPropertyValue(SkipPickerProp->ContainerPtrToValuePtr<void>(Factory), true);
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), Factory);
	}
	// ─── FUNCTION LIBRARY ───
	else if (TypeLower == TEXT("functionlibrary") || TypeLower == TEXT("function_library"))
	{
		UBlueprintFunctionLibraryFactory* Factory = NewObject<UBlueprintFunctionLibraryFactory>();
		Factory->bSkipClassPicker = true;
		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), Factory);
	}
	// ─── MACRO LIBRARY ───
	else if (TypeLower == TEXT("macrolibrary") || TypeLower == TEXT("macro_library"))
	{
		UBlueprintMacroFactory* Factory = NewObject<UBlueprintMacroFactory>();
		Factory->bSkipClassPicker = true;
		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UBlueprint::StaticClass(), Factory);
	}
	// ─── ANIM LAYER INTERFACE ───
	else if (TypeLower == TEXT("animlayerinterface") || TypeLower == TEXT("anim_layer_interface"))
	{
		FString SkeletonPath;
		CreateObj->TryGetStringField(TEXT("skeleton"), SkeletonPath);

		// AnimLayerInterface factory is UAnimLayerInterfaceFactory (subclass of UAnimBlueprintFactory)
		UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/UnrealEd.AnimLayerInterfaceFactory"));
		if (!FactoryClass)
		{
			return FToolResult::Fail(TEXT("! Create: AnimLayerInterfaceFactory class not found"));
		}

		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
		if (!SkeletonPath.IsEmpty())
		{
			USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkeletonPath);
			if (Skeleton)
			{
				FObjectPropertyBase* SkeletonProp = CastField<FObjectPropertyBase>(FactoryClass->FindPropertyByName(TEXT("TargetSkeleton")));
				if (SkeletonProp)
				{
					SkeletonProp->SetObjectPropertyValue(SkeletonProp->ContainerPtrToValuePtr<void>(Factory), Skeleton);
				}
			}
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UAnimBlueprint::StaticClass(), Factory);
	}
	// ─── MATERIAL ───
	else if (TypeLower == TEXT("material"))
	{
		UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();

		FString TexturePath;
		if (CreateObj->TryGetStringField(TEXT("initial_texture"), TexturePath) && !TexturePath.IsEmpty())
		{
			UTexture* Tex = LoadObject<UTexture>(nullptr, *TexturePath);
			if (Tex)
			{
				Factory->InitialTexture = Tex;
			}
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterial::StaticClass(), Factory);
	}
	// ─── MATERIAL FUNCTION ───
	else if (TypeLower == TEXT("materialfunction") || TypeLower == TEXT("material_function") || TypeLower == TEXT("mf"))
	{
		UMaterialFunctionFactoryNew* Factory = NewObject<UMaterialFunctionFactoryNew>();
		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterialFunction::StaticClass(), Factory);
	}
	// ─── MATERIAL INSTANCE ───
	else if (TypeLower == TEXT("materialinstance") || TypeLower == TEXT("material_instance") || TypeLower == TEXT("mic"))
	{
		UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();

		FString ParentMaterialPath;
		if (CreateObj->TryGetStringField(TEXT("parent_material"), ParentMaterialPath) && !ParentMaterialPath.IsEmpty())
		{
			UMaterialInterface* ParentMat = LoadObject<UMaterialInterface>(nullptr, *ParentMaterialPath);
			if (!ParentMat)
			{
				FString BuiltPath = NeoStackToolUtils::BuildAssetPath(ParentMaterialPath, TEXT(""));
				ParentMat = LoadObject<UMaterialInterface>(nullptr, *BuiltPath);
			}
			if (ParentMat)
			{
				Factory->InitialParent = ParentMat;
			}
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
	}
	// ─── NIAGARA SYSTEM ───
	else if (TypeLower == TEXT("niagarasystem") || TypeLower == TEXT("niagara") || TypeLower == TEXT("niagara_system") || TypeLower == TEXT("vfx"))
	{
		UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/NiagaraEditor.NiagaraSystemFactoryNew"));
		if (!FactoryClass)
		{
			return FToolResult::Fail(TEXT("! Create: NiagaraSystemFactoryNew not found. Is Niagara plugin enabled?"));
		}

		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

		// Try to find the asset class
		UClass* NiagaraSystemClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraSystem"));
		if (!NiagaraSystemClass)
		{
			return FToolResult::Fail(TEXT("! Create: UNiagaraSystem class not found"));
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, NiagaraSystemClass, Factory);
	}
	// ─── DATA TABLE ───
	else if (TypeLower == TEXT("datatable") || TypeLower == TEXT("data_table"))
	{
		FString RowStructName;
		if (!CreateObj->TryGetStringField(TEXT("row_struct"), RowStructName) || RowStructName.IsEmpty())
		{
			return FToolResult::Fail(TEXT("! Create DataTable: 'row_struct' is required (struct name or path)"));
		}

		UScriptStruct* RowStruct = FindStructByName(RowStructName);
		if (!RowStruct)
		{
			return FToolResult::Fail(FString::Printf(TEXT("! Create DataTable: Struct '%s' not found"), *RowStructName));
		}

		UDataTableFactory* Factory = NewObject<UDataTableFactory>();
		Factory->Struct = RowStruct;

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, UDataTable::StaticClass(), Factory);
	}
	// ─── LEVEL SEQUENCE ───
	else if (TypeLower == TEXT("levelsequence") || TypeLower == TEXT("level_sequence") || TypeLower == TEXT("sequence"))
	{
		UClass* FactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew"));
		if (!FactoryClass)
		{
			return FToolResult::Fail(TEXT("! Create: LevelSequenceFactoryNew not found. Is LevelSequenceEditor plugin enabled?"));
		}

		UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);

		UClass* LevelSeqClass = FindObject<UClass>(nullptr, TEXT("/Script/LevelSequence.LevelSequence"));
		if (!LevelSeqClass)
		{
			return FToolResult::Fail(TEXT("! Create: ULevelSequence class not found"));
		}

		CreatedAsset = AssetTools.CreateAsset(Name, PackagePath, LevelSeqClass, Factory);
	}
	// ─── UNKNOWN TYPE ───
	else
	{
		return FToolResult::Fail(FString::Printf(
			TEXT("! Create: Unknown asset type '%s'. Supported: Blueprint, AnimBlueprint, WidgetBlueprint, Interface, "
				 "FunctionLibrary, MacroLibrary, AnimLayerInterface, Material, MaterialFunction, MaterialInstance, NiagaraSystem, DataTable, LevelSequence"),
			*Type));
	}

	if (!CreatedAsset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("! Create: Failed to create %s '%s' at %s"), *Type, *Name, *PackagePath));
	}

	return FToolResult::Ok(FString::Printf(TEXT("+ Created %s: %s at %s"), *Type, *Name, *PackagePath));
}
