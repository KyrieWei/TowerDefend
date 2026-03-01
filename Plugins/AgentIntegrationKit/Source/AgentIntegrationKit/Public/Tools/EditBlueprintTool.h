// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBlueprint;
class USCS_Node;
class UWidgetBlueprint;
class UWidgetTree;
class UWidget;
class UPanelWidget;
class UAnimBlueprint;
class UAnimGraphNode_StateMachine;
class UAnimationStateMachineGraph;
class UAnimStateNode;
class UTimelineTemplate;
class UK2Node_Timeline;

/**
 * Tool for editing Blueprint assets:
 * - Add/remove/modify variables with full type support (including replication)
 *   Supports member variables AND local variables (scoped to functions via 'function' param)
 *   modify_variables: rename, change type, update flags (save_game, deprecated, interp, etc.)
 * - Add/remove components with property setup
 * - Add/remove custom functions with inputs/outputs
 * - Add/remove event dispatchers (multicast delegate VARIABLES - add_events)
 * - Add/remove custom events (actual event NODES in EventGraph - add_custom_events)
 *   ^ IMPORTANT: These are different! Event dispatchers are delegate variables,
 *     Custom events are red event nodes you wire logic to.
 *     For Multicast RPCs, use add_custom_events with replication:"Multicast"
 * - Add/remove timelines with float/vector/event tracks and keyframes
 * - Add/remove widgets in Widget Blueprints
 * - Add state machines, states, and transitions in Animation Blueprints
 */
class AGENTINTEGRATIONKIT_API FEditBlueprintTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_blueprint"); }
	virtual FString GetDescription() const override
	{
		return TEXT(
			"Create and edit Blueprint assets, and route Enhanced Input edits via asset_domain='enhanced_input'. "
			"CREATE: use 'create' param with type: Blueprint, AnimBlueprint, WidgetBlueprint, Interface, "
			"FunctionLibrary, MacroLibrary, AnimLayerInterface, Material, MaterialInstance, NiagaraSystem, DataTable, LevelSequence. "
			"EDIT: add/remove/modify variables (member + local), components, functions, events, timelines, widgets. "
			"BLUEPRINT-LEVEL: reparent (change parent class), add_interfaces/remove_interfaces (implement/remove interfaces). "
			"For class settings (bGenerateAbstractClass, bGenerateConstClass, bDeprecate), use configure_asset directly on the Blueprint. "
			"VARIABLES: add_variables (create), remove_variables (delete), modify_variables (rename, change type, update flags). "
			"Set 'function' param to target LOCAL variables in a specific function graph. "
			"Flags: replicated, rep_notify, expose_on_spawn, private, transient, save_game, advanced_display, deprecated, interp, read_only, blueprint_only. "
			"IMPORTANT: To set properties on components (including INHERITED components from parent Blueprints), "
			"use configure_components - it creates proper overrides for inherited components. "
			"EVENT TYPES - Two distinct systems: "
			"1) add_custom_events: Creates Custom Event NODES (red nodes in EventGraph) - use for RPCs (Multicast/Server/Client) or callable events. "
			"2) add_events: Creates Event Dispatcher VARIABLES (delegate properties) - use for Bind/Unbind/Call delegate pattern. "
			"For timelines: use add_timelines with tracks (Float/Vector/Event) and keyframes. "
			"For Widget Blueprints: use add_widgets (supports Blueprint widget classes by name or path). "
			"For AnimBP layers: add_linked_anim_layers places Linked Anim Layer nodes (first add interface via add_interfaces). "
			"Property values: structs use \"(R=1.0,G=0.0,B=0.0,A=1.0)\", assets use \"/Game/Mesh.Mesh\"");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Type definition parsed from JSON */
	struct FTypeDefinition
	{
		FString Base;       // Boolean, Float, Object, etc.
		FString Container;  // Single, Array, Set, Map
		FString Subtype;    // For Object/Class/Structure/Interface/Enum
		TSharedPtr<FTypeDefinition> KeyType; // For Map
	};

	/** Variable definition */
	struct FVariableDefinition
	{
		FString Name;
		FTypeDefinition Type;
		FString Default;
		FString Category;
		FString Function; // If set, creates a local variable in this function graph
		bool bReplicated = false;
		bool bRepNotify = false;
		bool bExposeOnSpawn = false;
		bool bPrivate = false;
		bool bTransient = false;
		bool bSaveGame = false;
		bool bAdvancedDisplay = false;
		bool bDeprecated = false;
		bool bInterp = false;
		bool bReadOnly = false;
		bool bBlueprintOnly = false;
	};

	/** Variable modification definition */
	struct FVariableModification
	{
		FString Name;       // Current variable name
		FString Function;   // If set, targets a local variable in this function
		FString NewName;    // Rename to this (optional)
		TSharedPtr<FJsonObject> NewType; // Change type (optional)
		// Flags (optional - only applied if explicitly set in JSON)
		TOptional<bool> bReplicated;
		TOptional<bool> bRepNotify;
		TOptional<bool> bExposeOnSpawn;
		TOptional<bool> bPrivate;
		TOptional<bool> bTransient;
		TOptional<bool> bSaveGame;
		TOptional<bool> bAdvancedDisplay;
		TOptional<bool> bDeprecated;
		TOptional<bool> bInterp;
		TOptional<bool> bReadOnly;
		TOptional<bool> bBlueprintOnly;
		FString Category;   // Change category (optional)
		FString Default;    // Change default value (optional)
	};

	/** Component definition */
	struct FComponentDefinition
	{
		FString Name;
		FString Class;
		FString Parent;
		TSharedPtr<FJsonObject> Properties;
	};

	/** Function parameter */
	struct FFunctionParam
	{
		FString Name;
		FTypeDefinition Type;
	};

	/** Function definition */
	struct FFunctionDefinition
	{
		FString Name;
		bool bPure = false;
		FString Category;
		TArray<FFunctionParam> Inputs;
		TArray<FFunctionParam> Outputs;
	};

	/** Event dispatcher definition (multicast delegate variable) */
	struct FEventDefinition
	{
		FString Name;
		TArray<FFunctionParam> Params;
	};

	/** Replication type for custom events (RPCs) */
	enum class EEventReplication : uint8
	{
		NotReplicated,  // Local only
		Multicast,      // Server -> All Clients (server executes too)
		Server,         // Client -> Server (Run on Server)
		Client          // Server -> Owning Client (Run on Owning Client)
	};

	/** Custom event definition (actual event node in EventGraph, can be RPC) */
	struct FCustomEventDefinition
	{
		FString Name;
		TArray<FFunctionParam> Params;
		EEventReplication Replication = EEventReplication::NotReplicated;
		bool bReliable = false;
		bool bCallInEditor = false;
	};

	/** Widget definition for Widget Blueprints */
	struct FWidgetDefinition
	{
		FString Type;   // Widget class (Button, TextBlock, CanvasPanel, etc.)
		FString Name;   // Widget name (must be unique)
		FString Parent; // Parent widget name (empty = root)
	};

	/** Widget configuration - set properties on existing widgets */
	struct FWidgetConfigDefinition
	{
		FString Name;   // Widget name to configure
		TSharedPtr<FJsonObject> Properties;  // Properties to set (uses reflection)
		TSharedPtr<FJsonObject> Slot;        // Slot properties (position, size, anchors, etc.)
	};

	/** Event binding definition - works for both Widget and regular Blueprints */
	struct FEventBindingDef
	{
		FString Source;   // Component name (BP) or Widget name (WBP)
		FString Event;    // Delegate name (OnClicked, OnComponentBeginOverlap, etc.)
		FString Handler;  // Blueprint function to call
	};

	/** Info about a bindable event/delegate */
	struct FEventInfo
	{
		FString Name;       // Delegate name (OnClicked, OnComponentBeginOverlap)
		FString Signature;  // Parameter signature
	};

	/** Parse type definition from JSON (depth-limited to prevent stack overflow from malicious input) */
	FTypeDefinition ParseTypeDefinition(const TSharedPtr<FJsonObject>& TypeObj, int32 Depth = 0);

	/** Parse function parameter from JSON */
	FFunctionParam ParseFunctionParam(const TSharedPtr<FJsonObject>& ParamObj);

	/** Convert type definition to FEdGraphPinType. Returns false if type cannot be resolved. */
	bool TypeDefinitionToPinType(const FTypeDefinition& TypeDef, FEdGraphPinType& OutPinType, FString& OutError);

	/** Find UClass for a type name */
	UClass* FindClassByName(const FString& ClassName);

	/** Find UScriptStruct for a struct name */
	UScriptStruct* FindStructByName(const FString& StructName);

	/** Find UEnum for an enum name */
	UEnum* FindEnumByName(const FString& EnumName);

	/** Add a variable to the Blueprint */
	FString AddVariable(UBlueprint* Blueprint, const FVariableDefinition& VarDef);

	/** Remove a variable from the Blueprint (member or local if Function is specified) */
	FString RemoveVariable(UBlueprint* Blueprint, const FString& VarName, const FString& Function = FString());

	/** Modify an existing variable (rename, change type, update flags) */
	FString ModifyVariable(UBlueprint* Blueprint, const FVariableModification& ModDef);

	/** Find a function graph by name */
	UEdGraph* FindFunctionGraph(UBlueprint* Blueprint, const FString& FunctionName);

	/** Get the UStruct scope for a function graph (needed for local variable APIs) */
	UStruct* GetFunctionScope(UBlueprint* Blueprint, UEdGraph* FunctionGraph);

	/** Apply variable flags using engine utility functions (shared between add and modify) */
	void ApplyVariableFlags(UBlueprint* Blueprint, const FName& VarName, bool bSaveGame, bool bAdvancedDisplay, bool bDeprecated, bool bInterp, bool bReadOnly, bool bBlueprintOnly);

	/** Add a component to the Blueprint */
	FString AddComponent(UBlueprint* Blueprint, const FComponentDefinition& CompDef);

	/** Remove a component from the Blueprint */
	FString RemoveComponent(UBlueprint* Blueprint, const FString& CompName);

	/** Rename a component in the Blueprint */
	FString RenameComponent(UBlueprint* Blueprint, const FString& OldName, const FString& NewName);

	/** Duplicate a component in the Blueprint */
	FString DuplicateComponent(UBlueprint* Blueprint, const FString& SourceName, const FString& NewName);

	/** Reparent a component (move to a different parent in the hierarchy) */
	FString ReparentComponent(UBlueprint* Blueprint, const FString& CompName, const FString& NewParent);

	/** Make a component the scene root */
	FString SetRootComponent(UBlueprint* Blueprint, const FString& CompName);

	/** Find an SCS node by variable name (case-insensitive) */
	USCS_Node* FindSCSNodeByName(UBlueprint* Blueprint, const FString& Name);

	/** Configure component properties (works with inherited components via InheritableComponentHandler) */
	FString ConfigureComponent(UBlueprint* Blueprint, const FString& CompName, const TSharedPtr<FJsonObject>& Properties);

	/** Add a function to the Blueprint */
	FString AddFunction(UBlueprint* Blueprint, const FFunctionDefinition& FuncDef);

	/** Remove a function from the Blueprint */
	FString RemoveFunction(UBlueprint* Blueprint, const FString& FuncName);

	/** Rename a function or macro graph */
	FString RenameFunction(UBlueprint* Blueprint, const FString& OldName, const FString& NewName);

	/** Add a macro graph to the Blueprint */
	FString AddMacro(UBlueprint* Blueprint, const FFunctionDefinition& MacroDef);

	/** Remove a macro graph from the Blueprint */
	FString RemoveMacro(UBlueprint* Blueprint, const FString& MacroName);

	/** Override a parent class function (creates event node or function graph) */
	FString OverrideFunction(UBlueprint* Blueprint, const FString& FunctionName);

	/** Add an additional event graph page */
	FString AddEventGraph(UBlueprint* Blueprint, const FString& GraphName);

	/** Reparent Blueprint to a new parent class (matches editor's ReparentBlueprint flow) */
	FString ReparentBlueprint(UBlueprint* Blueprint, const FString& NewParentClassName);

	/** Add an interface to the Blueprint */
	FString AddInterface(UBlueprint* Blueprint, const FString& InterfaceName);

	/** Remove an interface from the Blueprint */
	FString RemoveInterface(UBlueprint* Blueprint, const FString& InterfaceName, bool bPreserveFunctions);

	/** Add an event dispatcher to the Blueprint */
	FString AddEvent(UBlueprint* Blueprint, const FEventDefinition& EventDef);

	/** Remove an event dispatcher from the Blueprint */
	FString RemoveEvent(UBlueprint* Blueprint, const FString& EventName);

	/** Add a custom event node to the Blueprint's EventGraph (can be replicated as RPC) */
	FString AddCustomEvent(UBlueprint* Blueprint, const FCustomEventDefinition& EventDef);

	/** Remove a custom event node from the Blueprint */
	FString RemoveCustomEvent(UBlueprint* Blueprint, const FString& EventName);

	/** Set default value on a variable */
	void SetVariableDefaultValue(UBlueprint* Blueprint, const FString& VarName, const FString& DefaultValue);

	/** Set property on a component with proper UE modification protocol */
	bool SetComponentProperty(UBlueprint* Blueprint, USCS_Node* Node, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value);

	// Widget Blueprint operations
	/** Add a widget to a Widget Blueprint */
	FString AddWidget(UWidgetBlueprint* WidgetBlueprint, const FWidgetDefinition& WidgetDef);

	/** Remove a widget from a Widget Blueprint */
	FString RemoveWidget(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName);

	/** Configure a widget's properties using reflection */
	FString ConfigureWidget(UWidgetBlueprint* WidgetBlueprint, const FWidgetConfigDefinition& ConfigDef);

	/** Set a property on a UObject using reflection */
	bool SetPropertyFromJson(UObject* Object, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Find widget class by name */
	UClass* FindWidgetClass(const FString& TypeName);

	/** Find widget in tree by name */
	UWidget* FindWidgetByName(UWidgetTree* WidgetTree, const FString& Name);

	/** Refresh widget editor if open */
	void RefreshWidgetEditor(UWidgetBlueprint* WidgetBlueprint);

	// Event binding operations (unified for both Widget and regular Blueprints)

	/** List available events on a component or widget */
	FString ListEvents(UBlueprint* Blueprint, const FString& SourceName);

	/** List events on a component in a regular Blueprint */
	TArray<FEventInfo> ListComponentEvents(UBlueprint* Blueprint, const FString& ComponentName);

	/** List events on a widget in a Widget Blueprint */
	TArray<FEventInfo> ListWidgetEvents(UWidgetBlueprint* WidgetBlueprint, const FString& WidgetName);

	/** Bind an event - routes to widget or component binding based on Blueprint type */
	FString BindEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef);

	/** Bind widget event using FDelegateEditorBinding */
	FString BindWidgetEvent(UWidgetBlueprint* WidgetBlueprint, const FEventBindingDef& EventDef);

	/** Bind component event by creating UK2Node_ComponentBoundEvent */
	FString BindComponentEvent(UBlueprint* Blueprint, const FEventBindingDef& EventDef);

	/** Unbind an event */
	FString UnbindEvent(UBlueprint* Blueprint, const FString& Source, const FString& Event);

	// Component discovery helpers

	/** Result of component discovery */
	struct FComponentDiscoveryResult
	{
		UActorComponent* ComponentTemplate = nullptr;
		USCS_Node* SCSNode = nullptr;           // Set if found in SCS
		FName VariableName = NAME_None;
		bool bFoundInSCS = false;
		bool bFoundInCDO = false;
	};

	/**
	 * Find a component by name - checks both SCS and CDO for completeness
	 * SCS catches recently added components (before compilation)
	 * CDO catches compiled components (after compilation)
	 */
	FComponentDiscoveryResult FindComponentByName(UBlueprint* Blueprint, const FString& ComponentName);

	// Animation Blueprint state machine operations

	/** State machine definition */
	struct FStateMachineDefinition
	{
		FString Name;   // State machine name
	};

	/** Animation state definition */
	struct FAnimStateDefinition
	{
		FString Name;           // State name
		FString StateMachine;   // Parent state machine name
	};

	/** State transition definition */
	struct FStateTransitionDefinition
	{
		FString StateMachine;   // Parent state machine name
		FString FromState;      // Source state name (or "[Entry]" for entry point)
		FString ToState;        // Target state name
	};

	/** Find the AnimGraph in an Animation Blueprint */
	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBlueprint);

	/** Find a state machine node by name in the AnimGraph */
	UAnimGraphNode_StateMachine* FindStateMachineNode(UAnimBlueprint* AnimBlueprint, const FString& StateMachineName);

	/** Find a state node by name in a state machine graph */
	UAnimStateNode* FindStateNode(UAnimationStateMachineGraph* SMGraph, const FString& StateName);

	/** Add a state machine to an Animation Blueprint */
	FString AddStateMachine(UAnimBlueprint* AnimBlueprint, const FStateMachineDefinition& SMDef);

	/** Add a state to a state machine */
	FString AddAnimState(UAnimBlueprint* AnimBlueprint, const FAnimStateDefinition& StateDef);

	/**
	 * Add a transition between states
	 * Creates a transition node and a transition graph where condition logic can be added
	 * Returns info about the transition graph and result node for wiring condition logic
	 */
	FString AddStateTransition(UAnimBlueprint* AnimBlueprint, const FStateTransitionDefinition& TransDef);

	// Linked Anim Layer operations

	/** Linked anim layer definition */
	struct FLinkedAnimLayerDefinition
	{
		FString LayerName;      // Layer function name from the interface
		FString Interface;      // AnimLayerInterface class or Blueprint path (optional if only one interface)
	};

	/** Add a Linked Anim Layer node to the AnimGraph */
	FString AddLinkedAnimLayer(UAnimBlueprint* AnimBlueprint, const FLinkedAnimLayerDefinition& LayerDef);

	// Timeline operations

	/** Timeline track type */
	enum class ETimelineTrackType : uint8
	{
		Float,
		Vector,
		LinearColor,
		Event
	};

	/** Timeline keyframe */
	struct FTimelineKeyframe
	{
		float Time = 0.0f;
		float Value = 0.0f;              // For float tracks
		FVector VectorValue = FVector::ZeroVector;  // For vector tracks
		FLinearColor ColorValue = FLinearColor::White;  // For color tracks
		FString InterpMode = TEXT("Linear");  // Linear, Constant, Cubic
	};

	/** Timeline track definition */
	struct FTimelineTrackDef
	{
		FString Name;
		ETimelineTrackType Type = ETimelineTrackType::Float;
		TArray<FTimelineKeyframe> Keyframes;
		FString ExternalCurve;  // Optional: path to external curve asset
	};

	/** Timeline definition */
	struct FTimelineDefinition
	{
		FString Name;
		float Length = 1.0f;
		bool bAutoPlay = false;
		bool bLoop = false;
		bool bReplicated = false;
		bool bIgnoreTimeDilation = false;
		TArray<FTimelineTrackDef> Tracks;
	};

	/** Add a timeline to a Blueprint */
	FString AddTimeline(UBlueprint* Blueprint, const FTimelineDefinition& TimelineDef);

	/** Remove a timeline from a Blueprint */
	FString RemoveTimeline(UBlueprint* Blueprint, const FString& TimelineName);

	/** Add a track to an existing timeline */
	FString AddTimelineTrack(UBlueprint* Blueprint, const FString& TimelineName, const FTimelineTrackDef& TrackDef);

	/** Add keyframes to an existing timeline track */
	FString AddTimelineKeyframes(UBlueprint* Blueprint, const FString& TimelineName, const FString& TrackName, const TArray<FTimelineKeyframe>& Keyframes);

	/** Find timeline template by name */
	UTimelineTemplate* FindTimelineTemplate(UBlueprint* Blueprint, const FString& TimelineName);

	/** Find timeline node in event graph */
	UK2Node_Timeline* FindTimelineNode(UBlueprint* Blueprint, const FString& TimelineName);

	/** Handle asset creation (Blueprint, AnimBP, WidgetBP, Material, Niagara, etc.) */
	FToolResult HandleCreate(const FString& Name, const FString& Path, const TSharedPtr<FJsonObject>& CreateObj);

	/** Resolve a parent class name to UClass* */
	UClass* ResolveParentClass(const FString& ClassName);
};
