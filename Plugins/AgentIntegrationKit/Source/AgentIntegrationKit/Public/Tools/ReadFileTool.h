// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

// Forward declarations
class UWidgetBlueprint;
class UWidget;
class UPanelSlot;
class FProperty;
class UAnimBlueprint;
class UEdGraph;
class UBehaviorTree;
class UBTCompositeNode;
class UBlackboardData;
class UUserDefinedStruct;
class UUserDefinedEnum;
class UDataTable;
class UNiagaraSystem;
class UNiagaraEmitter;
class UMaterialInstance;
class UStaticMesh;
class UBlendSpace;
class UAnimMontage;
class USkeleton;
class USoundCue;
class UPhysicsAsset;
class UCurveBase;
class UCurveTable;
class UStateTree;
class UStateTreeState;
class UStateTreeEditorData;
class UChooserTable;
class UInputAction;
class UInputMappingContext;
class UStringTable;
class UEnvQuery;

/**
 * Tool for reading files and UE assets (Blueprint, Material, WidgetBlueprint, AnimBlueprint, BehaviorTree, PCGGraph, MetaSound, etc.)
 * - Text files: returns content with pagination
 * - Graph assets: returns nodes and connections using shared UEdGraph reading
 * - Widget Blueprints: returns widget tree hierarchy
 * - Animation Blueprints: returns state machines, states, transitions, and their subgraphs
 * - Behavior Trees: returns node hierarchy with composites, tasks, decorators, and services
 * - Environment Queries (EQS): returns options with generators, tests, and all properties
 * - Blackboards: returns keys with types and inheritance
 * - User Defined Structs: returns fields with names, types, and default values
 * - User Defined Enums: returns values with names and display names
 * - DataTables: returns row struct info and row data
 * - PCG Graphs: returns nodes with settings, pins, and connections
 */
class AGENTINTEGRATIONKIT_API FReadFileTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("read_asset"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Read a file or Unreal asset. Dedicated readers for: Blueprint, WidgetBlueprint, AnimBlueprint, Material, MaterialFunction, MaterialInstance, "
			"BehaviorTree, EnvironmentQuery, Blackboard, StateTree, SoundCue, Niagara, LevelSequence, MetaSound, StaticMesh, SkeletalMesh, AnimSequence, AnimMontage, "
			"BlendSpace, Skeleton, ControlRig, PhysicsAsset, CurveFloat/Vector/Color, CurveTable, DataTable, ChooserTable, StringTable, Struct, Enum, "
			"IKRig, IKRetargeter, PoseSearch, PCGGraph, InputAction, InputMappingContext, GameplayEffect. "
			"Graph dumps include node pin names plus unlinked input pin literal defaults (enums, numbers, bools, strings, object refs) when available. "
			"For AnimBPs, graph selectors can target subgraphs: animgraph:<Graph>, statemachine:<AnimGraph>/<SM>, "
			"state:<AnimGraph>/<State>, transition:<AnimGraph>/<From->To>, custom_transition:<AnimGraph>/<From->To>, "
			"conduit:<AnimGraph>/<Conduit>, composite:<AnimGraph>/<Composite>. "
			"Any other asset type falls back to generic property reflection.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Read a text file with pagination */
	FToolResult ReadTextFile(const FString& Name, const FString& Path, int32 Offset, int32 Limit);

	/** Get Blueprint summary with graph list */
	FString GetBlueprintSummary(class UBlueprint* Blueprint);

	/** Get Blueprint components in tab-delimited format */
	FString GetBlueprintComponents(class UBlueprint* Blueprint, int32 Offset, int32 Limit);

	/** Get all editable properties of a specific component with their current values */
	FString GetBlueprintComponentProperties(class UBlueprint* Blueprint, const FString& ComponentName, int32 Offset, int32 Limit);

	/** Get Blueprint variables in tab-delimited format */
	FString GetBlueprintVariables(class UBlueprint* Blueprint, int32 Offset, int32 Limit);

	/** Get all graphs with full nodes and connections */
	FString GetBlueprintGraphs(class UBlueprint* Blueprint, int32 Offset, int32 Limit);

	/** Get Blueprint interfaces */
	FString GetBlueprintInterfaces(class UBlueprint* Blueprint);

	/** Get graph type string */
	FString GetGraphType(class UEdGraph* Graph, class UBlueprint* Blueprint);

	/** Get single graph with nodes in UNIX format */
	FString GetGraphWithNodes(class UEdGraph* Graph, const FString& GraphType, const FString& ParentGraph, int32 Offset, int32 Limit);

	/** Get connections for a graph */
	FString GetGraphConnections(class UEdGraph* Graph);

	/** Get pin names for a node */
	FString GetNodePins(class UEdGraphNode* Node);

	/** Get Widget Blueprint summary */
	FString GetWidgetBlueprintSummary(UWidgetBlueprint* WidgetBlueprint);

	/** Get widget tree structure (bShowSchema shows all editable properties with types) */
	FString GetWidgetTree(UWidgetBlueprint* WidgetBlueprint, bool bShowSchema = false);

	/** Recursively get widget hierarchy */
	FString GetWidgetHierarchy(UWidget* Widget, int32 Depth, bool bShowSchema = false);

	/** Get slot info for a widget using reflection */
	FString GetWidgetSlotInfo(UWidget* Widget);

	/** Get widget properties using reflection (text, colors, brushes, etc.) */
	FString GetWidgetProperties(UWidget* Widget, const FString& Indent);

	/** Get full property schema for a widget (shows all editable properties with types) */
	FString GetWidgetPropertySchema(UWidget* Widget, const FString& Indent);

	/** Get full property schema for a slot (shows all editable slot properties with types) */
	FString GetSlotPropertySchema(class UPanelSlot* Slot, const FString& Indent);

	/** Get human-readable type string with format hints */
	FString GetPropertyTypeString(class FProperty* Property);

	/** Get property value as string for display */
	FString GetPropertyValueString(UObject* Object, class FProperty* Property);

	// Animation Blueprint support

	/** Get Animation Blueprint summary with skeleton and state machine info */
	FString GetAnimBlueprintSummary(UAnimBlueprint* AnimBlueprint);

	/** Get detailed state machine information including states and transitions */
	FString GetAnimBlueprintStateMachines(UAnimBlueprint* AnimBlueprint);

	/** Collect all graphs from AnimBP including AnimGraph, state machines, states, and transitions */
	void CollectAnimBlueprintGraphs(UAnimBlueprint* AnimBlueprint, TArray<TPair<UEdGraph*, FString>>& OutGraphs);

	// Behavior Tree support

	/** Get Behavior Tree summary with blackboard and node counts */
	FString GetBehaviorTreeSummary(UBehaviorTree* BehaviorTree);

	/** Count nodes recursively in the behavior tree */
	void CountBTNodes(UBTCompositeNode* Node, int32& OutTasks, int32& OutComposites, int32& OutDecorators, int32& OutServices);

	/** Get behavior tree node hierarchy (builds runtime→graph GUID map for references) */
	FString GetBehaviorTreeNodes(UBehaviorTree* BehaviorTree);

	/** Build reverse map from runtime node instances to graph node GUIDs and positions */
	void BuildBTGuidMap(UBehaviorTree* BehaviorTree, TMap<UObject*, FGuid>& OutGuidMap, TMap<UObject*, FVector2D>& OutPositionMap);

	/** Recursively get BT node hierarchy with decorators, services, and graph GUIDs */
	FString GetBTNodeHierarchy(UBTCompositeNode* Node, int32 Depth, const TMap<UObject*, FGuid>& GuidMap, const TMap<UObject*, FVector2D>& PositionMap);

	/** Get key property values for a BT node (blackboard keys, enums) */
	FString GetBTNodePropertySummary(class UBTNode* Node, const FString& Indent);

	// Environment Query (EQS) support

	/** Get EQS summary with option and test counts */
	FString GetEQSSummary(UEnvQuery* Query);

	/** Get full EQS structure with options, generators, tests, and properties */
	FString GetEQSDetails(UEnvQuery* Query);

	// Blackboard support

	/** Get Blackboard summary with parent and key count */
	FString GetBlackboardSummary(UBlackboardData* Blackboard);

	/** Get all Blackboard keys with types */
	FString GetBlackboardKeys(UBlackboardData* Blackboard);

	// User Defined Struct support

	/** Get User Defined Struct summary with field count */
	FString GetStructSummary(UUserDefinedStruct* Struct);

	/** Get all struct fields with types and default values */
	FString GetStructFields(UUserDefinedStruct* Struct);

	// User Defined Enum support

	/** Get User Defined Enum summary with value count */
	FString GetEnumSummary(UUserDefinedEnum* Enum);

	/** Get all enum values with display names */
	FString GetEnumValues(UUserDefinedEnum* Enum);

	// DataTable support

	/** Get DataTable summary with row struct and row count */
	FString GetDataTableSummary(UDataTable* DataTable);

	/** Get DataTable rows with values */
	FString GetDataTableRows(UDataTable* DataTable, int32 Offset, int32 Limit);

	// Niagara System support

	/** Get Niagara System summary with emitter list */
	FString GetNiagaraSystemSummary(UNiagaraSystem* System);

	/** Get all emitters in the system with their stacks */
	FString GetNiagaraEmitters(UNiagaraSystem* System);

	/** Get detailed info for a single emitter (by handle) */
	FString GetNiagaraEmitterDetails(const struct FNiagaraEmitterHandle& Handle);

	/** Get emitter stack modules (spawn, update, etc.) */
	FString GetNiagaraEmitterStacks(UNiagaraEmitter* Emitter, const FString& EmitterName);

	/** Get module parameters from a Niagara script */
	FString GetNiagaraModuleParameters(class UNiagaraScript* Script, const FString& ModuleName);

	/** Get modules from a Niagara script graph */
	FString GetNiagaraScriptModules(class UNiagaraScript* Script);

	/** Read a Material or MaterialFunction asset with graph support */
	FToolResult ReadMaterial(UObject* Asset, const TArray<FString>& Include,
		const FString& GraphName, int32 Offset, int32 Limit);

	/** Read a Level Sequence asset */
	FToolResult ReadLevelSequence(class ULevelSequence* Sequence);

	/** Read a Skeletal Mesh asset (bone hierarchy) */
	FToolResult ReadSkeletalMesh(class USkeletalMesh* Mesh);

	/** Read an Animation Sequence asset */
	FToolResult ReadAnimSequence(class UAnimSequence* AnimSeq);

	/** Read a MetaSound asset (Source or Patch) */
	FToolResult ReadMetaSound(UObject* Asset, const FString& AssetPath,
		const TArray<FString>& Include, const FString& GraphName, int32 Offset, int32 Limit);

	/** Read a Material Instance asset (parameter overrides, parent chain) */
	FToolResult ReadMaterialInstance(UMaterialInstance* MatInstance);

	/** Read a Static Mesh asset (LODs, material slots, collision, nanite) */
	FToolResult ReadStaticMesh(UStaticMesh* StaticMesh);

	/** Read a Blend Space asset (axis config, samples, animations) */
	FToolResult ReadBlendSpace(UBlendSpace* BlendSpace);

	/** Read an Animation Montage asset (sections, slots, notifies, blend settings) */
	FToolResult ReadAnimMontage(UAnimMontage* Montage);

	/** Read a Control Rig Blueprint (hierarchy: bones, controls, nulls, curves) */
	FToolResult ReadControlRig(UObject* Asset);

	/** Read a Skeleton asset (bone hierarchy, sockets, virtual bones, slot groups) */
	FToolResult ReadSkeleton(USkeleton* Skeleton);

	/** Generic fallback reader for any UObject using property reflection */
	FToolResult ReadGenericAsset(UObject* Asset);

	// Physics support

	/** Read a PhysicsAsset (bodies with collision shapes, constraints with limits) */
	FToolResult ReadPhysicsAsset(UPhysicsAsset* PhysAsset);

	// Curve support

	/** Read a Curve asset (Float, Vector, or LinearColor with keyframes) */
	FToolResult ReadCurveAsset(UCurveBase* CurveAsset);

	/** Read a CurveTable asset (rows of curves) */
	FToolResult ReadCurveTable(UCurveTable* CurveTableAsset);

	/** Format a FRichCurve's keys for output */
	FString FormatRichCurve(const struct FRichCurve& Curve, const FString& CurveName);

	// IK Rig / Retargeter / Pose Search support (UE 5.6+)

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FToolResult ReadIKRig(class UIKRigDefinition* IKRig);
	FToolResult ReadIKRetargeter(class UIKRetargeter* Retargeter);
#if WITH_POSE_SEARCH
	FToolResult ReadPoseSearchSchema(class UPoseSearchSchema* Schema);
	FToolResult ReadPoseSearchDatabase(class UPoseSearchDatabase* Database);
	FToolResult ReadPoseSearchNormalizationSet(class UPoseSearchNormalizationSet* NormSet);
#endif // WITH_POSE_SEARCH
#endif

	// PCG support (UE 5.7+)

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	FToolResult ReadPCGGraph(class UPCGGraphInterface* PCGGraphInterface);
#endif

	// StateTree support

	/** Read a StateTree asset */
	FToolResult ReadStateTree(UStateTree* StateTree, const TArray<FString>& Include);

	/** Get StateTree summary with counts */
	FString GetStateTreeSummary(UStateTree* StateTree);

	/** Get StateTree state hierarchy */
	FString GetStateTreeHierarchy(UStateTree* StateTree);

	/** Recursively get state hierarchy with tasks, conditions, transitions */
	FString GetStateTreeStateHierarchy(const UStateTreeState* State, int32 Depth, UStateTreeEditorData* EditorData = nullptr);

	/** Get StateTree evaluators */
	FString GetStateTreeEvaluators(UStateTree* StateTree);

	/** Get StateTree global tasks */
	FString GetStateTreeGlobalTasks(UStateTree* StateTree);

	/** Get StateTree transitions in table format */
	FString GetStateTreeTransitions(UStateTree* StateTree);

	/** Get StateTree schema and context data */
	FString GetStateTreeSchema(UStateTree* StateTree);

	// Enhanced Input support

	/** Read an InputAction asset (value type, triggers, modifiers) */
	FToolResult ReadInputAction(UInputAction* InputAction);

	/** Read an InputMappingContext asset (mappings with keys, triggers, modifiers) */
	FToolResult ReadInputMappingContext(UInputMappingContext* MappingContext);

	/** Format a trigger object into a readable string */
	FString FormatTrigger(class UInputTrigger* Trigger);

	/** Format a modifier object into a readable string */
	FString FormatModifier(class UInputModifier* Modifier);

	// Gameplay Ability System support

	/** Read a GameplayEffect Blueprint (duration, modifiers, stacking, tags, cues) */
	FToolResult ReadGameplayEffect(UBlueprint* Blueprint);

	// StringTable support

	/** Read a StringTable asset (namespace, all entries with keys and values) */
	FToolResult ReadStringTable(UStringTable* StringTable, int32 Offset, int32 Limit);

	// ChooserTable support

	/** Get ChooserTable summary with column and row counts */
	FString GetChooserTableSummary(UChooserTable* ChooserTable);

	/** Get all object references in the ChooserTable */
	FString GetChooserTableReferences(UChooserTable* ChooserTable, int32 Offset, int32 Limit);

	/** Get ChooserTable column information */
	FString GetChooserTableColumns(UChooserTable* ChooserTable);
};
