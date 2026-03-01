// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"

class UNiagaraSystem;
class UNiagaraEmitter;
class UNiagaraScript;
class UNiagaraNodeFunctionCall;
class UNiagaraNodeOutput;
class UNiagaraRendererProperties;
class UNiagaraSimulationStageBase;
struct FNiagaraEmitterHandle;
struct FNiagaraVariable;
struct FVersionedNiagaraEmitter;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
struct FVersionedNiagaraEmitterBase;
#endif

/**
 * Tool for editing Niagara Systems and Emitters:
 * - Add/remove modules to emitter stacks (Spawn, Update, Event stages)
 * - Set module input parameters (static values, dynamic inputs, linked params, custom HLSL, data interfaces)
 * - Add/remove/configure renderers (Sprite, Mesh, Ribbon, Light)
 * - Add new emitters to systems
 * - Configure emitter properties (sim target, bounds, allocation)
 * - Add/remove/set user parameters (exposed in Details panel)
 * - Discovery operations (list emitters, modules, renderers, parameters, dynamic inputs)
 *
 * Input value modes for set_parameters:
 * - Static: direct value (float, int, vector, color, etc.) — default
 * - Dynamic Input: {mode:"dynamic_input", script:"<path>", parameters:{...}}
 * - Linked: {mode:"linked", parameter:"Particles.Position"}
 * - Custom HLSL: {mode:"hlsl", code:"float2(1,1)"}
 * - Data Interface: {mode:"data_interface", type:"SkeletalMesh", properties:{...}}
 *
 * Stage names supported:
 * - particle_spawn, spawn
 * - particle_update, update
 * - particle_event, event
 * - simulation_stage, simstage
 * - emitter_spawn, emitter_update
 * - system_spawn, system_update
 */
class AGENTINTEGRATIONKIT_API FEditNiagaraTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_niagara"); }
	virtual FString GetDescription() const override
	{
		return TEXT(
			"Edit Niagara Systems: add/remove modules, configure renderers, set parameters (static, dynamic_input, linked, hlsl, data_interface), manage user parameters. "
			"Use list_emitters/list_modules/list_renderers/list_dynamic_inputs plus list_reflected_properties to discover content. "
			"Graph/node editing stays in edit_graph; edit_niagara handles Niagara stack/object operations. "
			"Use edit_graph with operation='find_nodes' to get module/dynamic input asset paths. Stages: particle_spawn, particle_update, particle_event, simulation_stage, emitter_spawn, emitter_update."
		);
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// ========== Definitions ==========

	/** Module definition for adding to a stack */
		struct FModuleDefinition
		{
			FString ModulePath;     // Asset path to the module script (from edit_graph discovery)
			FString EmitterName;    // Target emitter name (empty for system-level)
			ENiagaraScriptUsage Usage;  // Target script usage
			FGuid UsageId;          // Optional usage id for event/simulation-stage stacks
			FString Name;           // Optional friendly name for the module instance
			int32 Index = INDEX_NONE;  // Position in stack (-1 = append)
			TSharedPtr<FJsonObject> Parameters;  // Optional parameter overrides
		};

	/** Module removal definition */
		struct FModuleRemoval
		{
			FString ModuleName;     // Name of module to remove
			FString EmitterName;    // Target emitter name
			ENiagaraScriptUsage Usage;  // Which script to remove from
			FGuid UsageId;          // Optional usage id for event/simulation-stage stacks
		};

	/** Emitter definition for adding to a system */
	struct FEmitterDefinition
	{
		FString Name;           // Emitter name
		FString TemplateAsset;  // Optional template emitter asset path
	};

	/** Emitter removal operation */
	struct FEmitterRemoval
	{
		FString EmitterName;
	};

	/** Emitter rename operation */
	struct FEmitterRename
	{
		FString EmitterName;
		FString NewName;
	};

	/** Emitter duplicate operation */
	struct FEmitterDuplicate
	{
		FString EmitterName;
		FString NewName; // Optional; auto-generated when empty
	};

	/** Emitter reorder operation */
	struct FEmitterReorder
	{
		FString EmitterName;
		int32 NewIndex = INDEX_NONE;
	};

	/** Set parameter operation */
		struct FSetParameterOp
		{
			FString ModuleName;     // Module name to set parameters on
			FString EmitterName;    // Target emitter (optional for system stages)
			ENiagaraScriptUsage Usage;  // Script usage
			FGuid UsageId;          // Optional usage id for event/simulation-stage stacks
			TSharedPtr<FJsonObject> Parameters;  // Parameter name -> value
		};

	/** Module move/reorder operation */
		struct FModuleMoveOp
		{
			FString ModuleName;
			FString SourceEmitterName; // Optional for system stages
			ENiagaraScriptUsage SourceUsage = ENiagaraScriptUsage::ParticleUpdateScript;
			FGuid SourceUsageId;       // Optional usage id for source event/sim stage
			FString TargetEmitterName; // Optional for system stages
			ENiagaraScriptUsage TargetUsage = ENiagaraScriptUsage::ParticleUpdateScript;
			FGuid TargetUsageId;       // Optional usage id for target event/sim stage
			int32 TargetIndex = INDEX_NONE;
			bool bForceCopy = false;
		};

	/** Renderer definition for adding */
	struct FRendererDefinition
	{
		FString Type;              // sprite, mesh, ribbon, light
		FString EmitterName;       // Target emitter
		TSharedPtr<FJsonObject> Properties;  // Renderer properties
		TSharedPtr<FJsonObject> Bindings;    // Particle attribute bindings
	};

	/** Renderer removal definition */
	struct FRendererRemoval
	{
		FString EmitterName;
		int32 Index = INDEX_NONE;  // Index to remove, or -1 for last
		FString Type;              // Optional: remove by type instead of index
	};

	/** Renderer configuration */
		struct FRendererConfiguration
		{
			FString EmitterName;
			int32 Index = INDEX_NONE;
			TSharedPtr<FJsonObject> Properties;
			TSharedPtr<FJsonObject> Bindings;
		};

		/** Renderer reorder operation */
		struct FRendererReorder
		{
			FString EmitterName;
			int32 Index = INDEX_NONE;
			int32 NewIndex = INDEX_NONE;
		};

	/** User parameter definition for adding */
	struct FUserParameterDefinition
	{
		FString Name;              // Parameter name (will be prefixed with "User.")
		FString Type;              // Niagara registered user-variable type name (e.g., Float, Vector3, ActorComponentInterface)
		TSharedPtr<FJsonValue> DefaultValue;  // Optional default value
	};

	/** User parameter value setting */
	struct FUserParameterValue
	{
		FString Name;              // Parameter name
		TSharedPtr<FJsonValue> Value;  // New value
	};

	/** Emitter property set operation */
	struct FEmitterPropertySet
	{
		FString EmitterName;
		TOptional<FString> SimTarget;      // "cpu" or "gpu"
		TOptional<bool> bLocalSpace;
		TOptional<bool> bDeterminism;
		TOptional<int32> RandomSeed;
	};

	/** Event handler creation */
	struct FEventHandlerDefinition
	{
		FString EmitterName;
		FString SourceEventName;
		FString SourceEmitterName;
		TOptional<int32> ExecutionMode;
		TOptional<int32> SpawnNumber;
		TOptional<int32> MaxEventsPerFrame;
		TOptional<bool> bRandomSpawnNumber;
		TOptional<int32> MinSpawnNumber;
		TOptional<bool> bUpdateAttributeInitialValues;
	};

	/** Event handler removal */
	struct FEventHandlerRemoval
	{
		FString EmitterName;
		FGuid UsageId;
	};

	/** Event handler edit */
	struct FEventHandlerUpdate
	{
		FString EmitterName;
		FGuid UsageId;
		TOptional<int32> ExecutionMode;
		TOptional<int32> SpawnNumber;
		TOptional<int32> MaxEventsPerFrame;
		TOptional<bool> bRandomSpawnNumber;
		TOptional<int32> MinSpawnNumber;
		TOptional<bool> bUpdateAttributeInitialValues;
		FString SourceEventName;
		FString SourceEmitterName;
	};

	/** Simulation stage creation */
	struct FSimulationStageDefinition
	{
		FString EmitterName;
		FString StageClass;
		FString Name;
		TOptional<bool> bEnabled;
		int32 Index = INDEX_NONE;
	};

	/** Simulation stage removal */
	struct FSimulationStageRemoval
	{
		FString EmitterName;
		FGuid UsageId;
	};

	/** Simulation stage edit */
	struct FSimulationStageUpdate
	{
		FString EmitterName;
		FGuid UsageId;
		FString Name;
		TOptional<bool> bEnabled;
	};

	/** Simulation stage reorder */
	struct FSimulationStageReorder
	{
		FString EmitterName;
		FGuid UsageId;
		int32 NewIndex = INDEX_NONE;
	};

	/** Module enable/disable operation */
		struct FModuleEnableOp
		{
			FString ModuleName;
			FString EmitterName; // Optional for system stages
			ENiagaraScriptUsage Usage;
			FGuid UsageId;      // Optional usage id for event/simulation-stage stacks
			bool bEnabled;
		};

	/** Scratch pad script creation */
	struct FScratchPadScriptDefinition
	{
		FString Name;
		FString ScriptType;           // "module" or "dynamic_input"
		FString DuplicateFrom;        // Optional source script path/object path
		FString TargetStage;          // Optional stage used for module usage bitmask
		FString TargetEmitterName;    // Optional for emitter-bound stage names
		FString OutputType;           // Optional for dynamic input scripts
	};

	/** Scratch pad script deletion */
	struct FScratchPadScriptDeletion
	{
		FString Name;
	};

	/** Scratch pad script rename */
	struct FScratchPadScriptRename
	{
		FString Name;
		FString NewName;
	};

	/** Add module to a scratch pad script graph */
	struct FScratchModuleDefinition
	{
		FString ScratchScriptName;
		FString ModulePath;
		FString ModuleName;
		int32 Index = INDEX_NONE;
		TSharedPtr<FJsonObject> Parameters;
	};

	/** Remove module from a scratch pad script graph */
	struct FScratchModuleRemoval
	{
		FString ScratchScriptName;
		FString ModuleName;
	};

	/** Set parameters on a module inside a scratch pad script graph */
	struct FScratchModuleSetParameters
	{
		FString ScratchScriptName;
		FString ModuleName;
		TSharedPtr<FJsonObject> Parameters;
	};

	/** System property set operation */
	struct FSystemPropertySet
	{
		TOptional<float> WarmupTime;
		TOptional<bool> bDeterminism;
		TOptional<int32> RandomSeed;
	};

	/** Module script version change operation */
	struct FModuleVersionSet
	{
		FString ModuleName;
		FString EmitterName;
		ENiagaraScriptUsage Usage = ENiagaraScriptUsage::ParticleUpdateScript;
		FGuid UsageId;
		FGuid VersionGuid;
	};

	/** Emitter version change operation */
	struct FEmitterVersionSet
	{
		FString EmitterName;
		FGuid VersionGuid;
	};

	/** Parameter definitions subscribe/unsubscribe operation */
	struct FParameterDefinitionsOp
	{
		FString AssetPath;
		FGuid DefinitionsId;
	};

	/** Generic reflected-property target selector */
	struct FReflectedPropertyTarget
	{
		FString Target;            // system | emitter | renderer | simulation_stage | baker_settings
		FString EmitterName;       // Required for emitter/renderer/simulation_stage
		int32 RendererIndex = INDEX_NONE; // Required for renderer
		FGuid UsageId;             // Required for simulation_stage
		TSharedPtr<FJsonObject> Properties; // Optional for list operation, required for set operation
	};

	// ========== Parsing ==========

	/** Parse script usage from user-friendly stage string */
	ENiagaraScriptUsage ParseScriptUsage(const FString& StageStr, bool& bOutValid) const;

	/** Get all valid stage name strings */
	static FString GetValidStageNames();

	/** Convert script usage to user-friendly string */
	FString UsageToString(ENiagaraScriptUsage Usage) const;

	/** Parse module definition from JSON */
	bool ParseModuleDefinition(const TSharedPtr<FJsonObject>& Obj, FModuleDefinition& OutDef, FString& OutError);

	/** Parse module removal from JSON */
	bool ParseModuleRemoval(const TSharedPtr<FJsonObject>& Obj, FModuleRemoval& OutDef, FString& OutError);

	/** Parse emitter definition from JSON */
	bool ParseEmitterDefinition(const TSharedPtr<FJsonObject>& Obj, FEmitterDefinition& OutDef, FString& OutError);

	/** Parse emitter removal from JSON */
	bool ParseEmitterRemoval(const TSharedPtr<FJsonObject>& Obj, FEmitterRemoval& OutDef, FString& OutError);

	/** Parse emitter rename from JSON */
	bool ParseEmitterRename(const TSharedPtr<FJsonObject>& Obj, FEmitterRename& OutDef, FString& OutError);

	/** Parse emitter duplicate from JSON */
	bool ParseEmitterDuplicate(const TSharedPtr<FJsonObject>& Obj, FEmitterDuplicate& OutDef, FString& OutError);

	/** Parse emitter reorder from JSON */
	bool ParseEmitterReorder(const TSharedPtr<FJsonObject>& Obj, FEmitterReorder& OutDef, FString& OutError);

	/** Parse set parameter operation from JSON */
	bool ParseSetParameterOp(const TSharedPtr<FJsonObject>& Obj, FSetParameterOp& OutOp, FString& OutError);

	/** Parse module move operation from JSON */
	bool ParseModuleMoveOp(const TSharedPtr<FJsonObject>& Obj, FModuleMoveOp& OutOp, FString& OutError);

	/** Parse renderer definition from JSON */
	bool ParseRendererDefinition(const TSharedPtr<FJsonObject>& Obj, FRendererDefinition& OutDef, FString& OutError);

	/** Parse renderer removal from JSON */
	bool ParseRendererRemoval(const TSharedPtr<FJsonObject>& Obj, FRendererRemoval& OutDef, FString& OutError);

	/** Parse renderer configuration from JSON */
	bool ParseRendererConfiguration(const TSharedPtr<FJsonObject>& Obj, FRendererConfiguration& OutConfig, FString& OutError);

	/** Parse renderer reorder from JSON */
	bool ParseRendererReorder(const TSharedPtr<FJsonObject>& Obj, FRendererReorder& OutOp, FString& OutError);

	/** Parse user parameter definition from JSON */
	bool ParseUserParameterDefinition(const TSharedPtr<FJsonObject>& Obj, FUserParameterDefinition& OutDef, FString& OutError);

	/** Parse user parameter value from JSON */
	bool ParseUserParameterValue(const TSharedPtr<FJsonObject>& Obj, FUserParameterValue& OutValue, FString& OutError);

	/** Parse emitter property set from JSON */
	bool ParseEmitterPropertySet(const TSharedPtr<FJsonObject>& Obj, FEmitterPropertySet& OutProps, FString& OutError);

	/** Parse event handler definition from JSON */
	bool ParseEventHandlerDefinition(const TSharedPtr<FJsonObject>& Obj, FEventHandlerDefinition& OutDef, FString& OutError);

	/** Parse event handler removal from JSON */
	bool ParseEventHandlerRemoval(const TSharedPtr<FJsonObject>& Obj, FEventHandlerRemoval& OutDef, FString& OutError);

	/** Parse event handler update from JSON */
	bool ParseEventHandlerUpdate(const TSharedPtr<FJsonObject>& Obj, FEventHandlerUpdate& OutDef, FString& OutError);

	/** Parse simulation stage definition from JSON */
	bool ParseSimulationStageDefinition(const TSharedPtr<FJsonObject>& Obj, FSimulationStageDefinition& OutDef, FString& OutError);

	/** Parse simulation stage removal from JSON */
	bool ParseSimulationStageRemoval(const TSharedPtr<FJsonObject>& Obj, FSimulationStageRemoval& OutDef, FString& OutError);

	/** Parse simulation stage update from JSON */
	bool ParseSimulationStageUpdate(const TSharedPtr<FJsonObject>& Obj, FSimulationStageUpdate& OutDef, FString& OutError);

	/** Parse simulation stage reorder from JSON */
	bool ParseSimulationStageReorder(const TSharedPtr<FJsonObject>& Obj, FSimulationStageReorder& OutDef, FString& OutError);

	/** Parse module enable operation from JSON */
	bool ParseModuleEnableOp(const TSharedPtr<FJsonObject>& Obj, FModuleEnableOp& OutOp, FString& OutError);

	/** Parse system property set from JSON */
	bool ParseSystemPropertySet(const TSharedPtr<FJsonObject>& Obj, FSystemPropertySet& OutProps, FString& OutError);

	/** Parse reflected-property target op from JSON */
	bool ParseReflectedPropertyTarget(const TSharedPtr<FJsonObject>& Obj, FReflectedPropertyTarget& OutDef, FString& OutError);

	/** Parse module version set operation */
	bool ParseModuleVersionSet(const TSharedPtr<FJsonObject>& Obj, FModuleVersionSet& OutDef, FString& OutError);

	/** Parse emitter version set operation */
	bool ParseEmitterVersionSet(const TSharedPtr<FJsonObject>& Obj, FEmitterVersionSet& OutDef, FString& OutError);

	/** Parse parameter definitions op */
	bool ParseParameterDefinitionsOp(const TSharedPtr<FJsonObject>& Obj, FParameterDefinitionsOp& OutDef, FString& OutError);

	/** Parse scratch pad script creation from JSON */
	bool ParseScratchPadScriptDefinition(const TSharedPtr<FJsonObject>& Obj, FScratchPadScriptDefinition& OutDef, FString& OutError);

	/** Parse scratch pad script deletion from JSON */
	bool ParseScratchPadScriptDeletion(const TSharedPtr<FJsonObject>& Obj, FScratchPadScriptDeletion& OutDef, FString& OutError);

	/** Parse scratch pad script rename from JSON */
	bool ParseScratchPadScriptRename(const TSharedPtr<FJsonObject>& Obj, FScratchPadScriptRename& OutDef, FString& OutError);

	/** Parse scratch graph module add from JSON */
	bool ParseScratchModuleDefinition(const TSharedPtr<FJsonObject>& Obj, FScratchModuleDefinition& OutDef, FString& OutError);

	/** Parse scratch graph module remove from JSON */
	bool ParseScratchModuleRemoval(const TSharedPtr<FJsonObject>& Obj, FScratchModuleRemoval& OutDef, FString& OutError);

	/** Parse scratch graph module set-parameters from JSON */
	bool ParseScratchModuleSetParameters(const TSharedPtr<FJsonObject>& Obj, FScratchModuleSetParameters& OutDef, FString& OutError);

	// ========== Niagara Operations ==========

	/** Find emitter handle by name in the system */
	int32 FindEmitterIndexByName(UNiagaraSystem* System, const FString& EmitterName);

	/** Get the output node for a specific script usage / usage id in emitter or system context */
	UNiagaraNodeOutput* GetOutputNodeForUsage(UNiagaraSystem* System, int32 EmitterIndex, ENiagaraScriptUsage Usage, const FGuid& UsageId = FGuid());

	/** Check if a module script supports the given usage */
	bool ModuleSupportsUsage(UNiagaraScript* ModuleScript, ENiagaraScriptUsage Usage) const;

	/** Get supported usages for a module script */
	TArray<ENiagaraScriptUsage> GetModuleSupportedUsages(UNiagaraScript* ModuleScript) const;

	/** Add a module to a stack */
	FString AddModule(UNiagaraSystem* System, const FModuleDefinition& ModDef);

	/** Remove a module from a stack */
	FString RemoveModule(UNiagaraSystem* System, const FModuleRemoval& RemovalDef);

	/** Set parameters on a module */
	FString SetModuleParameters(UNiagaraSystem* System, const FSetParameterOp& SetOp);

	/** Add a new emitter to the system */
	FString AddEmitter(UNiagaraSystem* System, const FEmitterDefinition& EmitterDef);

	/** Remove emitter from the system */
	FString RemoveEmitter(UNiagaraSystem* System, const FEmitterRemoval& EmitterDef);

	/** Rename emitter in the system */
	FString RenameEmitter(UNiagaraSystem* System, const FEmitterRename& EmitterDef);

	/** Duplicate emitter in the system */
	FString DuplicateEmitter(UNiagaraSystem* System, const FEmitterDuplicate& EmitterDef);

	/** Reorder emitter in the system */
	FString ReorderEmitter(UNiagaraSystem* System, const FEmitterReorder& EmitterDef);

	/** Find a module node by name in a stack (supports partial matching) */
	UNiagaraNodeFunctionCall* FindModuleByName(UNiagaraNodeOutput* OutputNode, const FString& ModuleName);

	/** List all modules in a stack (for error messages) */
	TArray<FString> ListModulesInStack(UNiagaraNodeOutput* OutputNode) const;

	// ========== Renderer Operations ==========

	/** Add a renderer to an emitter */
	FString AddRenderer(UNiagaraSystem* System, const FRendererDefinition& RendererDef);

	/** Remove a renderer from an emitter */
	FString RemoveRenderer(UNiagaraSystem* System, const FRendererRemoval& RemovalDef);

	/** Configure an existing renderer */
	FString ConfigureRenderer(UNiagaraSystem* System, const FRendererConfiguration& Config);

	/** Reorder an existing renderer */
	FString ReorderRenderer(UNiagaraSystem* System, const FRendererReorder& ReorderOp);

	/** Create a renderer by type name */
	UNiagaraRendererProperties* CreateRendererByType(UNiagaraEmitter* Emitter, const FString& Type);

	/** Set a renderer property via reflection */
	bool SetRendererProperty(UNiagaraRendererProperties* Renderer, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Set a renderer binding */
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	bool SetRendererBinding(UNiagaraRendererProperties* Renderer, const FVersionedNiagaraEmitterBase& VersionedEmitter, const FString& BindingName, const FString& AttributeName, FString& OutError);
#else
	bool SetRendererBinding(UNiagaraRendererProperties* Renderer, const FVersionedNiagaraEmitter& VersionedEmitter, const FString& BindingName, const FString& AttributeName, FString& OutError);
#endif

	// ========== User Parameter Operations ==========

	/** Add a user parameter to the system */
	FString AddUserParameter(UNiagaraSystem* System, const FUserParameterDefinition& ParamDef);

	/** Remove a user parameter from the system */
	FString RemoveUserParameter(UNiagaraSystem* System, const FString& ParamName);

	/** Set a user parameter value */
	FString SetUserParameterValue(UNiagaraSystem* System, const FUserParameterValue& ParamValue);

	/** Resolve type string to Niagara type definition */
	bool ResolveUserParameterType(const FString& TypeStr, FNiagaraTypeDefinition& OutType) const;

	/** Find a scratch pad script by display/object name */
	UNiagaraScript* FindScratchPadScriptByName(UNiagaraSystem* System, const FString& ScriptName) const;

	// ========== Emitter Property Operations ==========

	/** Set emitter properties (sim target, local space, determinism, etc.) */
	FString SetEmitterProperties(UNiagaraSystem* System, const FEmitterPropertySet& Props);

	/** Add an event handler object (Particle Event script usage) */
	FString AddEventHandler(UNiagaraSystem* System, const FEventHandlerDefinition& Def);

	/** Remove an event handler object by stage usage id */
	FString RemoveEventHandler(UNiagaraSystem* System, const FEventHandlerRemoval& Def);

	/** Edit event handler object properties by stage usage id */
	FString SetEventHandler(UNiagaraSystem* System, const FEventHandlerUpdate& Def);

	/** Add a simulation stage object */
	FString AddSimulationStage(UNiagaraSystem* System, const FSimulationStageDefinition& Def);

	/** Remove a simulation stage object by stage usage id */
	FString RemoveSimulationStage(UNiagaraSystem* System, const FSimulationStageRemoval& Def);

	/** Edit simulation stage object properties by stage usage id */
	FString SetSimulationStage(UNiagaraSystem* System, const FSimulationStageUpdate& Def);

	/** Reorder a simulation stage object by stage usage id */
	FString ReorderSimulationStage(UNiagaraSystem* System, const FSimulationStageReorder& Def);

	/** Set module enabled state */
	FString SetModuleEnabled(UNiagaraSystem* System, const FModuleEnableOp& Op);

	/** Move/reorder module in stack(s) */
	FString MoveModule(UNiagaraSystem* System, const FModuleMoveOp& Op);

	/** Set system-level properties (warmup, determinism, etc.) */
	FString SetSystemProperties(UNiagaraSystem* System, const FSystemPropertySet& Props);

	/** List available versions for emitters and module scripts */
	FString ListVersions(UNiagaraSystem* System) const;

	/** Set module script version on a stack node */
	FString SetModuleScriptVersion(UNiagaraSystem* System, const FModuleVersionSet& Def);

	/** Set emitter handle version in system */
	FString SetEmitterVersion(UNiagaraSystem* System, const FEmitterVersionSet& Def);

	/** List current parameter definitions subscriptions and available libraries */
	FString ListParameterDefinitions(UNiagaraSystem* System) const;

	/** Subscribe system to a parameter definitions library */
	FString SubscribeParameterDefinitions(UNiagaraSystem* System, const FParameterDefinitionsOp& Def);

	/** Unsubscribe system from a parameter definitions library */
	FString UnsubscribeParameterDefinitions(UNiagaraSystem* System, const FParameterDefinitionsOp& Def);

	/** Synchronize system + emitters with subscribed parameter definitions */
	FString SynchronizeParameterDefinitions(UNiagaraSystem* System);

	/** List validation rule sets that apply to this system */
	FString ListValidationRuleSets(UNiagaraSystem* System) const;

	/** Run Niagara validation and return findings */
	FString RunValidation(UNiagaraSystem* System);

	/** Resolve a reflected-property target to a UObject instance */
	bool ResolveReflectedPropertyTargetObject(UNiagaraSystem* System, const FReflectedPropertyTarget& Def, UObject*& OutObject, FString& OutLabel, FString& OutError);

	/** Enumerate editable reflected properties on an object */
	FString ListReflectedObjectProperties(UObject* TargetObject, const FString& TargetLabel) const;

	/** Apply reflected properties to a target object */
	FString SetReflectedObjectProperties(UObject* TargetObject, const FString& TargetLabel, const TSharedPtr<FJsonObject>& Properties);

	/** Create scratch pad script directly on the system */
	FString CreateScratchPadScript(UNiagaraSystem* System, const FScratchPadScriptDefinition& Def);

	/** Delete scratch pad script from the system */
	FString DeleteScratchPadScript(UNiagaraSystem* System, const FScratchPadScriptDeletion& Def);

	/** Rename scratch pad script on the system */
	FString RenameScratchPadScript(UNiagaraSystem* System, const FScratchPadScriptRename& Def);

	/** Add a module node to a scratch pad script graph */
	FString AddScratchModule(UNiagaraSystem* System, const FScratchModuleDefinition& Def);

	/** Remove a module node from a scratch pad script graph */
	FString RemoveScratchModule(UNiagaraSystem* System, const FScratchModuleRemoval& Def);

	/** Set module parameters in a scratch pad script graph */
	FString SetScratchModuleParameters(UNiagaraSystem* System, const FScratchModuleSetParameters& Def);

	// ========== Dynamic Input Discovery & Value Setting ==========

	/** Discover all inputs for a module using GetStackFunctionInputs */
	TArray<FNiagaraVariable> DiscoverModuleInputs(UNiagaraNodeFunctionCall* ModuleNode);

	/** Convert JSON value to FNiagaraVariable using type reflection */
	bool ParseJsonToNiagaraVariable(const FNiagaraTypeDefinition& TypeDef, const TSharedPtr<FJsonValue>& JsonValue, FNiagaraVariable& OutVar, FString& OutError);

	/** Set struct memory from JSON value using property reflection */
	bool SetStructFromJson(UScriptStruct* Struct, void* StructMemory, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError);

	/** Set a single property value from JSON using CastField<T>() pattern */
	bool SetPropertyFromJsonValue(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, FString& OutError);

	/** Set an input value on a module using Rapid Iteration or Override Pin as appropriate */
	bool SetInputValue(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraNodeFunctionCall* ModuleNode,
		UNiagaraScript* Script,
		const FNiagaraVariable& Input,
		const TSharedPtr<FJsonValue>& Value,
		FString& OutError);

	/** Set a dynamic input on a module input (connects a DI script's output to the input) */
	FString SetDynamicInput(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraNodeFunctionCall* ModuleNode,
		UNiagaraScript* Script,
		const FNiagaraVariable& Input,
		const FString& DynamicInputPath,
		const TSharedPtr<FJsonObject>& ChildParams,
		FString& OutError);

	/** Link a module input to read from an existing parameter (e.g., Particles.Position) */
	FString SetLinkedInput(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraNodeFunctionCall* ModuleNode,
		UNiagaraScript* Script,
		const FNiagaraVariable& Input,
		const FString& LinkedParameterName,
		FString& OutError);

	/** Set a custom HLSL expression on a module input */
	FString SetCustomHLSLInput(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraNodeFunctionCall* ModuleNode,
		UNiagaraScript* Script,
		const FNiagaraVariable& Input,
		const FString& HLSLCode,
		FString& OutError);

	/** Set a data interface on a module input */
	FString SetDataInterfaceInput(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraNodeFunctionCall* ModuleNode,
		UNiagaraScript* Script,
		const FNiagaraVariable& Input,
		const FString& DataInterfaceTypeName,
		const TSharedPtr<FJsonObject>& DIProperties,
		FString& OutError);

	/** Reset a module input override back to authored/default value */
	bool ResetModuleInputToDefault(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraNodeFunctionCall* ModuleNode,
		UNiagaraScript* Script,
		const FNiagaraVariable& Input,
		FString& OutError);

	/** List available dynamic input scripts filtered by output type */
	FString ListDynamicInputs(const FString& OutputTypeFilter) const;

	/** Format available inputs for display */
	FString FormatAvailableInputs(const TArray<FNiagaraVariable>& Inputs) const;

	// ========== Discovery Operations ==========

	/** List all user parameters on the system */
	FString ListUserParameters(UNiagaraSystem* System) const;

	/** Get renderer type name from renderer properties */
	FString GetRendererTypeName(const UNiagaraRendererProperties* Renderer) const;

	// ========== Result Formatting ==========

	/** Format results to output string */
	FString FormatResults(
		const FString& SystemName,
		const TArray<FString>& AddedModules,
		const TArray<FString>& RemovedModules,
		const TArray<FString>& SetParameters,
		const TArray<FString>& AddedEmitters,
		const TArray<FString>& AddedEventHandlers,
		const TArray<FString>& RemovedEventHandlers,
		const TArray<FString>& UpdatedEventHandlers,
		const TArray<FString>& AddedSimulationStages,
		const TArray<FString>& RemovedSimulationStages,
		const TArray<FString>& UpdatedSimulationStages,
		const TArray<FString>& ReorderedSimulationStages,
		const TArray<FString>& AddedRenderers,
		const TArray<FString>& RemovedRenderers,
		const TArray<FString>& ConfiguredRenderers,
		const TArray<FString>& AddedUserParams,
		const TArray<FString>& RemovedUserParams,
		const TArray<FString>& SetUserParams,
		const TArray<FString>& SetEmitterProps,
		const TArray<FString>& ModuleEnables,
		const TArray<FString>& MovedModules,
		const TArray<FString>& ScratchPadScripts,
		const FString& SystemPropsResult,
		const TArray<FString>& Errors
	) const;
};
