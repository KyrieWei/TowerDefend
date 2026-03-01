// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditNiagaraTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Niagara includes
#include "NiagaraSystem.h"
#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "NiagaraEmitterBase.h"
#endif
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNode.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeInput.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraCommon.h"
#include "NiagaraTypes.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "NiagaraTypeRegistry.h"
#endif
#include "NiagaraEditorUtilities.h"
#include "NiagaraParameterMapHistory.h"
#include "NiagaraParameterDefinitions.h"
#include "NiagaraParameterDefinitionsBase.h"
#include "NiagaraParameterDefinitionsSubscriber.h"
#include "NiagaraValidationRules.h"
#include "NiagaraValidationRuleSet.h"
#include "NiagaraEditorSettings.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "UpgradeNiagaraScriptResults.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"
#include "ViewModels/Stack/NiagaraParameterHandle.h"
#include "EdGraphSchema_Niagara.h"
#include "NiagaraNodeCustomHlsl.h" // MinimalAPI: type info + StaticClass only, methods NOT linkable
#include "NiagaraBakerSettings.h"

// Renderer includes
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"

// Data interfaces
#include "NiagaraDataInterface.h"

// User parameters
#include "NiagaraParameterStore.h"

// Property reflection
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"

// Transaction support for undo/redo
#include "ScopedTransaction.h"

// Asset loading
#include "AssetRegistry/AssetRegistryModule.h"

// ========== Parameter Map Pin Helpers ==========
// The engine identifies parameter map pins via PinToTypeDefinition() == GetParameterMapDef(),
// NOT by checking PinCategory == PinCategoryMisc. Parameter map pins use PinCategoryType
// with a PinSubCategoryObject pointing to the parameter map struct.

namespace NiagaraToolHelpers
{
	/** Check if a pin is a parameter map pin (matches engine's GetParameterMapPin logic) */
	static bool IsParameterMapPin(const UEdGraphPin* Pin)
	{
		if (!Pin) return false;
		FNiagaraTypeDefinition PinDef = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
		return PinDef == FNiagaraTypeDefinition::GetParameterMapDef();
	}

	/** Find the parameter map input pin on a node */
	static UEdGraphPin* GetParameterMapInputPin(UEdGraphNode* Node)
	{
		if (!Node) return nullptr;
		for (UEdGraphPin* Pin : Node->GetAllPins())
		{
			if (Pin->Direction == EGPD_Input && IsParameterMapPin(Pin))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/** Find the parameter map output pin on a node */
	static UEdGraphPin* GetParameterMapOutputPin(UEdGraphNode* Node)
	{
		if (!Node) return nullptr;
		for (UEdGraphPin* Pin : Node->GetAllPins())
		{
			if (Pin->Direction == EGPD_Output && IsParameterMapPin(Pin))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	/**
	 * Collect all nodes in a module's sub-graph (dynamic inputs, input nodes, etc.)
	 * by traversing input pins recursively. Must be called AFTER disconnecting the
	 * module from the parameter map chain to avoid traversing into adjacent modules.
	 */
	static void CollectModuleSubGraphNodes(UEdGraphNode* Node, TArray<UEdGraphNode*>& OutNodes, TSet<UEdGraphNode*>& Visited)
	{
		if (!Node || Visited.Contains(Node)) return;
		Visited.Add(Node);
		OutNodes.Add(Node);

		for (UEdGraphPin* Pin : Node->GetAllPins())
		{
			if (Pin->Direction == EGPD_Input)
			{
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode())
					{
						CollectModuleSubGraphNodes(LinkedNode, OutNodes, Visited);
					}
				}
			}
		}
	}

	/**
	 * Find all scripts that share a compilation context with a module in a given stage.
	 * Replicates the engine's FNiagaraStackGraphUtilities::FindAffectedScripts logic.
	 * System spawn/update scripts are ALWAYS included. Emitter scripts are included
	 * if they contain the given usage.
	 */
	static TArray<UNiagaraScript*> FindAffectedScripts(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		ENiagaraScriptUsage Usage)
	{
		TArray<UNiagaraScript*> AffectedScripts;

		// System scripts are always affected
		if (System)
		{
			if (UNiagaraScript* SpawnScript = System->GetSystemSpawnScript())
			{
				AffectedScripts.AddUnique(SpawnScript);
			}
			if (UNiagaraScript* UpdateScript = System->GetSystemUpdateScript())
			{
				AffectedScripts.AddUnique(UpdateScript);
			}
		}

		// Add matching emitter scripts
		FVersionedNiagaraEmitterData* EmitterData = EmitterHandle.GetEmitterData();
		if (EmitterData)
		{
			TArray<UNiagaraScript*> EmitterScripts;
			EmitterData->GetScripts(EmitterScripts, false);

			for (UNiagaraScript* Script : EmitterScripts)
			{
				if (Script && Script->ContainsUsage(Usage))
				{
					AffectedScripts.AddUnique(Script);
				}
			}
		}

		return AffectedScripts;
	}

	/**
	 * Clean up nodes connected to an override pin before setting a new value mode.
	 * Reimplements the unexported FNiagaraStackGraphUtilities::RemoveNodesForStackFunctionInputOverridePin.
	 *
	 * Uses GetClass()->GetName() checks for private types (NiagaraNodeParameterMapGet,
	 * NiagaraNodeParameterMapSet) since their headers are in NiagaraEditor's Private folder.
	 *
	 * Handles:
	 * - NiagaraNodeInput / NiagaraNodeParameterMapGet: simple removal
	 * - NiagaraNodeFunctionCall / NiagaraNodeCustomHlsl (dynamic inputs): recursive sub-graph removal
	 * - Unknown nodes: safe link breakage
	 */
	static void CleanupOverridePinNodes(UEdGraphPin& OverridePin)
	{
		if (OverridePin.LinkedTo.Num() == 0)
		{
			return;
		}

		UEdGraphPin* LinkedPin = OverridePin.LinkedTo[0];
		if (!LinkedPin)
		{
			return;
		}

		UEdGraphNode* LinkedNode = LinkedPin->GetOwningNode();
		if (!LinkedNode)
		{
			OverridePin.BreakAllPinLinks(true);
			return;
		}

		UEdGraph* Graph = LinkedNode->GetGraph();
		if (!Graph)
		{
			OverridePin.BreakAllPinLinks(true);
			return;
		}

		FString ClassName = LinkedNode->GetClass()->GetName();

		if (ClassName == TEXT("NiagaraNodeInput") || ClassName == TEXT("NiagaraNodeParameterMapGet"))
		{
			// Simple leaf nodes — break links and remove
			OverridePin.BreakAllPinLinks(true);
			LinkedNode->BreakAllNodeLinks(true);
			Graph->RemoveNode(LinkedNode);
		}
		else if (ClassName.Contains(TEXT("NiagaraNodeFunctionCall")) || ClassName == TEXT("NiagaraNodeCustomHlsl"))
		{
			// Dynamic input or Custom HLSL node — has its own sub-graph of data input nodes.
			// First, disconnect the override pin from this node.
			OverridePin.BreakAllPinLinks(true);

			// Disconnect the dynamic input node from the parameter map chain so
			// CollectModuleSubGraphNodes won't traverse into adjacent modules.
			UEdGraphPin* DIInputMap = GetParameterMapInputPin(LinkedNode);
			UEdGraphPin* DIOutputMap = GetParameterMapOutputPin(LinkedNode);

			if (DIInputMap)
			{
				DIInputMap->BreakAllPinLinks(true);
			}
			if (DIOutputMap)
			{
				DIOutputMap->BreakAllPinLinks(true);
			}

			// Collect all nodes in the dynamic input's sub-graph (function calls,
			// input nodes, nested dynamic inputs, etc.)
			TArray<UEdGraphNode*> NodesToRemove;
			TSet<UEdGraphNode*> Visited;
			CollectModuleSubGraphNodes(LinkedNode, NodesToRemove, Visited);

			// Also check if there's a ParameterMapSet node that was the override node
			// for this dynamic input's own inputs — it may have been linked via the
			// parameter map chain. If the DI had its own override node, it would have
			// been between the DI's parameter map input and some prior node.
			// The CollectModuleSubGraphNodes already handles this via data input traversal.

			for (UEdGraphNode* NodeToRemove : NodesToRemove)
			{
				if (NodeToRemove)
				{
					NodeToRemove->BreakAllNodeLinks(true);
					Graph->RemoveNode(NodeToRemove);
				}
			}
		}
		else
		{
			// Unknown node type — safely break links
			OverridePin.BreakAllPinLinks(true);
			LinkedNode->BreakAllNodeLinks(true);
			Graph->RemoveNode(LinkedNode);
		}
	}

	/**
	 * Remove a rapid iteration parameter for a specific input across all affected scripts.
	 * Called when switching from static (RI param) mode to dynamic/linked/HLSL mode,
	 * to avoid a stale RI param conflicting with the new graph-based value.
	 */
	static void RemoveRapidIterationParameter(
		UNiagaraSystem* System,
		const FNiagaraEmitterHandle& EmitterHandle,
		UNiagaraScript* Script,
		const FNiagaraVariable& AliasedVar)
	{
		if (!System || !Script)
		{
			return;
		}

		FString UniqueEmitterName;
		if (EmitterHandle.GetInstance().Emitter)
		{
			UniqueEmitterName = EmitterHandle.GetInstance().Emitter->GetUniqueEmitterName();
		}

		FNiagaraVariable RIParam = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
			AliasedVar,
			UniqueEmitterName.IsEmpty() ? nullptr : *UniqueEmitterName,
			Script->GetUsage());

		TArray<UNiagaraScript*> AffectedScripts = FindAffectedScripts(System, EmitterHandle, Script->GetUsage());

		for (UNiagaraScript* AffectedScript : AffectedScripts)
		{
			if (AffectedScript)
			{
				AffectedScript->Modify();
				AffectedScript->RapidIterationParameters.RemoveParameter(RIParam);
			}
		}
	}
}

TSharedPtr<FJsonObject> FEditNiagaraTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Asset parameter
	TSharedPtr<FJsonObject> AssetProp = MakeShared<FJsonObject>();
	AssetProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetProp->SetStringField(TEXT("description"), TEXT("Niagara System asset name or path"));
	Properties->SetObjectField(TEXT("asset"), AssetProp);

	// Path parameter
	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (default: /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// Add modules array
	TSharedPtr<FJsonObject> AddModulesProp = MakeShared<FJsonObject>();
	AddModulesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddModulesProp->SetStringField(TEXT("description"),
		TEXT("Modules to add: [{module_path, emitter?, stage, stage_usage_id?, name?, index?, parameters?}]. ")
		TEXT("Stages: particle_spawn, particle_update, particle_event, simulation_stage, emitter_spawn, emitter_update, system_spawn, system_update. ")
		TEXT("For particle_event/simulation_stage pass stage_usage_id from read_asset output."));
	TSharedPtr<FJsonObject> AddModulesItems = MakeShared<FJsonObject>();
	AddModulesItems->SetStringField(TEXT("type"), TEXT("object"));
	AddModulesProp->SetObjectField(TEXT("items"), AddModulesItems);
	Properties->SetObjectField(TEXT("add_modules"), AddModulesProp);

	// Remove modules array
	TSharedPtr<FJsonObject> RemoveModulesProp = MakeShared<FJsonObject>();
	RemoveModulesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveModulesProp->SetStringField(TEXT("description"), TEXT("Modules to remove: [{module_name, emitter?, stage, stage_usage_id?}]"));
	TSharedPtr<FJsonObject> RemoveModulesItems = MakeShared<FJsonObject>();
	RemoveModulesItems->SetStringField(TEXT("type"), TEXT("object"));
	RemoveModulesProp->SetObjectField(TEXT("items"), RemoveModulesItems);
	Properties->SetObjectField(TEXT("remove_modules"), RemoveModulesProp);

	// Set parameters array
	TSharedPtr<FJsonObject> SetParamsProp = MakeShared<FJsonObject>();
	SetParamsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetParamsProp->SetStringField(TEXT("description"),
		TEXT("Set module parameters: [{module_name, emitter?, stage, stage_usage_id?, parameters}]. "
				 "Values can be static (direct value) or advanced modes: "
				 "{mode:'dynamic_input', script:'<path>', parameters:{...}} | "
				 "{mode:'linked', parameter:'Particles.Position'} | "
				 "{mode:'hlsl', code:'float2(1,1)'} | "
				 "{mode:'reset'} | "
				 "{mode:'data_interface', type:'SkeletalMesh', properties:{...}}"));
	TSharedPtr<FJsonObject> SetParamsItems = MakeShared<FJsonObject>();
	SetParamsItems->SetStringField(TEXT("type"), TEXT("object"));
	SetParamsProp->SetObjectField(TEXT("items"), SetParamsItems);
	Properties->SetObjectField(TEXT("set_parameters"), SetParamsProp);

	// Move modules array
	TSharedPtr<FJsonObject> MoveModulesProp = MakeShared<FJsonObject>();
	MoveModulesProp->SetStringField(TEXT("type"), TEXT("array"));
	MoveModulesProp->SetStringField(TEXT("description"),
		TEXT("Move/reorder existing modules: [{module_name, source_emitter?, source_stage, source_stage_usage_id?, target_emitter?, target_stage, target_stage_usage_id?, target_index?, force_copy?}]"));
	TSharedPtr<FJsonObject> MoveModulesItems = MakeShared<FJsonObject>();
	MoveModulesItems->SetStringField(TEXT("type"), TEXT("object"));
	MoveModulesProp->SetObjectField(TEXT("items"), MoveModulesItems);
	Properties->SetObjectField(TEXT("move_modules"), MoveModulesProp);

	// Add emitters array
	TSharedPtr<FJsonObject> AddEmittersProp = MakeShared<FJsonObject>();
	AddEmittersProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEmittersProp->SetStringField(TEXT("description"), TEXT("Emitters to add: [{name, template_asset}]. template_asset is required."));
	TSharedPtr<FJsonObject> AddEmittersItems = MakeShared<FJsonObject>();
	AddEmittersItems->SetStringField(TEXT("type"), TEXT("object"));
	AddEmittersProp->SetObjectField(TEXT("items"), AddEmittersItems);
	Properties->SetObjectField(TEXT("add_emitters"), AddEmittersProp);

	// Remove emitters array
	TSharedPtr<FJsonObject> RemoveEmittersProp = MakeShared<FJsonObject>();
	RemoveEmittersProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveEmittersProp->SetStringField(TEXT("description"), TEXT("Emitters to remove: [{emitter}]"));
	TSharedPtr<FJsonObject> RemoveEmittersItems = MakeShared<FJsonObject>();
	RemoveEmittersItems->SetStringField(TEXT("type"), TEXT("object"));
	RemoveEmittersProp->SetObjectField(TEXT("items"), RemoveEmittersItems);
	Properties->SetObjectField(TEXT("remove_emitters"), RemoveEmittersProp);

	// Rename emitters array
	TSharedPtr<FJsonObject> RenameEmittersProp = MakeShared<FJsonObject>();
	RenameEmittersProp->SetStringField(TEXT("type"), TEXT("array"));
	RenameEmittersProp->SetStringField(TEXT("description"), TEXT("Emitters to rename: [{emitter, new_name}]"));
	TSharedPtr<FJsonObject> RenameEmittersItems = MakeShared<FJsonObject>();
	RenameEmittersItems->SetStringField(TEXT("type"), TEXT("object"));
	RenameEmittersProp->SetObjectField(TEXT("items"), RenameEmittersItems);
	Properties->SetObjectField(TEXT("rename_emitters"), RenameEmittersProp);

	// Duplicate emitters array
	TSharedPtr<FJsonObject> DuplicateEmittersProp = MakeShared<FJsonObject>();
	DuplicateEmittersProp->SetStringField(TEXT("type"), TEXT("array"));
	DuplicateEmittersProp->SetStringField(TEXT("description"), TEXT("Emitters to duplicate: [{emitter, new_name?}]"));
	TSharedPtr<FJsonObject> DuplicateEmittersItems = MakeShared<FJsonObject>();
	DuplicateEmittersItems->SetStringField(TEXT("type"), TEXT("object"));
	DuplicateEmittersProp->SetObjectField(TEXT("items"), DuplicateEmittersItems);
	Properties->SetObjectField(TEXT("duplicate_emitters"), DuplicateEmittersProp);

	// Reorder emitters array
	TSharedPtr<FJsonObject> ReorderEmittersProp = MakeShared<FJsonObject>();
	ReorderEmittersProp->SetStringField(TEXT("type"), TEXT("array"));
	ReorderEmittersProp->SetStringField(TEXT("description"), TEXT("Reorder emitters: [{emitter, new_index}]"));
	TSharedPtr<FJsonObject> ReorderEmittersItems = MakeShared<FJsonObject>();
	ReorderEmittersItems->SetStringField(TEXT("type"), TEXT("object"));
	ReorderEmittersProp->SetObjectField(TEXT("items"), ReorderEmittersItems);
	Properties->SetObjectField(TEXT("reorder_emitters"), ReorderEmittersProp);

	// ========== Renderer Operations ==========

	// Add renderers
	TSharedPtr<FJsonObject> AddRenderersProp = MakeShared<FJsonObject>();
	AddRenderersProp->SetStringField(TEXT("type"), TEXT("array"));
	AddRenderersProp->SetStringField(TEXT("description"),
		TEXT("Renderers to add: [{type, emitter, properties?, bindings?}]. ")
		TEXT("Types: Sprite, Mesh, Ribbon, Light, and any Niagara renderer class name (e.g., DecalRendererProperties). ")
		TEXT("Bindings: {Position: 'Particles.Position', Color: 'Particles.Color', ...}"));
	TSharedPtr<FJsonObject> AddRenderersItems = MakeShared<FJsonObject>();
	AddRenderersItems->SetStringField(TEXT("type"), TEXT("object"));
	AddRenderersProp->SetObjectField(TEXT("items"), AddRenderersItems);
	Properties->SetObjectField(TEXT("add_renderers"), AddRenderersProp);

	// Remove renderers
	TSharedPtr<FJsonObject> RemoveRenderersProp = MakeShared<FJsonObject>();
	RemoveRenderersProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveRenderersProp->SetStringField(TEXT("description"),
		TEXT("Renderers to remove: [{emitter, index}] or [{emitter, type}]. Use read_asset to see renderer indices."));
	TSharedPtr<FJsonObject> RemoveRenderersItems = MakeShared<FJsonObject>();
	RemoveRenderersItems->SetStringField(TEXT("type"), TEXT("object"));
	RemoveRenderersProp->SetObjectField(TEXT("items"), RemoveRenderersItems);
	Properties->SetObjectField(TEXT("remove_renderers"), RemoveRenderersProp);

	// Configure renderers
	TSharedPtr<FJsonObject> ConfigureRenderersProp = MakeShared<FJsonObject>();
	ConfigureRenderersProp->SetStringField(TEXT("type"), TEXT("array"));
	ConfigureRenderersProp->SetStringField(TEXT("description"),
		TEXT("Configure existing renderers: [{emitter, index, properties?, bindings?}]"));
	TSharedPtr<FJsonObject> ConfigureRenderersItems = MakeShared<FJsonObject>();
	ConfigureRenderersItems->SetStringField(TEXT("type"), TEXT("object"));
	ConfigureRenderersProp->SetObjectField(TEXT("items"), ConfigureRenderersItems);
	Properties->SetObjectField(TEXT("configure_renderer"), ConfigureRenderersProp);

	// Reorder renderers
	TSharedPtr<FJsonObject> ReorderRenderersProp = MakeShared<FJsonObject>();
	ReorderRenderersProp->SetStringField(TEXT("type"), TEXT("array"));
	ReorderRenderersProp->SetStringField(TEXT("description"),
		TEXT("Reorder renderers: [{emitter, index, new_index}]"));
	TSharedPtr<FJsonObject> ReorderRenderersItems = MakeShared<FJsonObject>();
	ReorderRenderersItems->SetStringField(TEXT("type"), TEXT("object"));
	ReorderRenderersProp->SetObjectField(TEXT("items"), ReorderRenderersItems);
	Properties->SetObjectField(TEXT("reorder_renderers"), ReorderRenderersProp);

	// ========== Discovery Operations ==========

	// List user parameters
	TSharedPtr<FJsonObject> ListUserParamsProp = MakeShared<FJsonObject>();
	ListUserParamsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ListUserParamsProp->SetStringField(TEXT("description"), TEXT("If true, lists all user parameters exposed on the system (name, type, current value)"));
	Properties->SetObjectField(TEXT("list_user_parameters"), ListUserParamsProp);

	// List dynamic inputs
	TSharedPtr<FJsonObject> ListDIProp = MakeShared<FJsonObject>();
	ListDIProp->SetStringField(TEXT("type"), TEXT("string"));
	ListDIProp->SetStringField(TEXT("description"),
		TEXT("List available dynamic input scripts filtered by output type (e.g., 'Float', 'Vector', 'LinearColor', 'Position'). "
			 "Returns script paths usable with set_parameters mode:'dynamic_input'. Pass empty string for all types."));
	Properties->SetObjectField(TEXT("list_dynamic_inputs"), ListDIProp);

	// List reflected properties
	TSharedPtr<FJsonObject> ListReflectedProps = MakeShared<FJsonObject>();
	ListReflectedProps->SetStringField(TEXT("type"), TEXT("array"));
	ListReflectedProps->SetStringField(TEXT("description"),
		TEXT("List editable reflected properties for Niagara objects: "
			 "[{target:'system'|'emitter'|'renderer'|'simulation_stage'|'baker_settings', emitter?, renderer_index?, stage_usage_id?}]"));
	TSharedPtr<FJsonObject> ListReflectedItems = MakeShared<FJsonObject>();
	ListReflectedItems->SetStringField(TEXT("type"), TEXT("object"));
	ListReflectedProps->SetObjectField(TEXT("items"), ListReflectedItems);
	Properties->SetObjectField(TEXT("list_reflected_properties"), ListReflectedProps);

	TSharedPtr<FJsonObject> ListVersionsProp = MakeShared<FJsonObject>();
	ListVersionsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ListVersionsProp->SetStringField(TEXT("description"), TEXT("If true, list emitter and module script versions available in this system."));
	Properties->SetObjectField(TEXT("list_versions"), ListVersionsProp);

	TSharedPtr<FJsonObject> ListParamDefsProp = MakeShared<FJsonObject>();
	ListParamDefsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ListParamDefsProp->SetStringField(TEXT("description"), TEXT("If true, list parameter definitions subscriptions and available libraries."));
	Properties->SetObjectField(TEXT("list_parameter_definitions"), ListParamDefsProp);

	TSharedPtr<FJsonObject> ListValidationRuleSetsProp = MakeShared<FJsonObject>();
	ListValidationRuleSetsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ListValidationRuleSetsProp->SetStringField(TEXT("description"), TEXT("If true, list validation rule sets from editor defaults and effect type."));
	Properties->SetObjectField(TEXT("list_validation_rule_sets"), ListValidationRuleSetsProp);

	// ========== User Parameter Operations ==========

	// Add user parameters
	TSharedPtr<FJsonObject> AddUserParamsProp = MakeShared<FJsonObject>();
	AddUserParamsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddUserParamsProp->SetStringField(TEXT("description"),
		TEXT("User parameters to add (exposed in Details panel): [{name, type, default?}]. ")
		TEXT("Type accepts any Niagara registered user-variable type name."));
	TSharedPtr<FJsonObject> AddUserParamsItems = MakeShared<FJsonObject>();
	AddUserParamsItems->SetStringField(TEXT("type"), TEXT("object"));
	AddUserParamsProp->SetObjectField(TEXT("items"), AddUserParamsItems);
	Properties->SetObjectField(TEXT("add_user_parameters"), AddUserParamsProp);

	// Remove user parameters
	TSharedPtr<FJsonObject> RemoveUserParamsProp = MakeShared<FJsonObject>();
	RemoveUserParamsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveUserParamsProp->SetStringField(TEXT("description"), TEXT("User parameter names to remove"));
	TSharedPtr<FJsonObject> RemoveUserParamsItems = MakeShared<FJsonObject>();
	RemoveUserParamsItems->SetStringField(TEXT("type"), TEXT("string"));
	RemoveUserParamsProp->SetObjectField(TEXT("items"), RemoveUserParamsItems);
	Properties->SetObjectField(TEXT("remove_user_parameters"), RemoveUserParamsProp);

	// Set user parameters
	TSharedPtr<FJsonObject> SetUserParamsProp = MakeShared<FJsonObject>();
	SetUserParamsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetUserParamsProp->SetStringField(TEXT("description"), TEXT("User parameter values to set: [{name, value}]"));
	TSharedPtr<FJsonObject> SetUserParamsItems = MakeShared<FJsonObject>();
	SetUserParamsItems->SetStringField(TEXT("type"), TEXT("object"));
	SetUserParamsProp->SetObjectField(TEXT("items"), SetUserParamsItems);
	Properties->SetObjectField(TEXT("set_user_parameters"), SetUserParamsProp);

	// ========== Emitter Properties ==========

	// Set emitter properties
	TSharedPtr<FJsonObject> SetEmitterPropsProp = MakeShared<FJsonObject>();
	SetEmitterPropsProp->SetStringField(TEXT("type"), TEXT("array"));
	SetEmitterPropsProp->SetStringField(TEXT("description"),
		TEXT("Emitter properties to set: [{emitter, sim_target?, local_space?, determinism?, random_seed?}]. ")
		TEXT("sim_target: 'cpu' or 'gpu'. Light renderer requires CPU."));
	TSharedPtr<FJsonObject> SetEmitterPropsItems = MakeShared<FJsonObject>();
	SetEmitterPropsItems->SetStringField(TEXT("type"), TEXT("object"));
	SetEmitterPropsProp->SetObjectField(TEXT("items"), SetEmitterPropsItems);
	Properties->SetObjectField(TEXT("set_emitter_properties"), SetEmitterPropsProp);

	// Add event handlers
	TSharedPtr<FJsonObject> AddEventHandlersProp = MakeShared<FJsonObject>();
	AddEventHandlersProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEventHandlersProp->SetStringField(TEXT("description"),
		TEXT("Event handlers to add: [{emitter, source_event_name, source_emitter?, execution_mode?, spawn_number?, max_events_per_frame?, random_spawn_number?, min_spawn_number?, update_attribute_initial_values?}]"));
	TSharedPtr<FJsonObject> AddEventHandlersItems = MakeShared<FJsonObject>();
	AddEventHandlersItems->SetStringField(TEXT("type"), TEXT("object"));
	AddEventHandlersProp->SetObjectField(TEXT("items"), AddEventHandlersItems);
	Properties->SetObjectField(TEXT("add_event_handlers"), AddEventHandlersProp);

	// Remove event handlers
	TSharedPtr<FJsonObject> RemoveEventHandlersProp = MakeShared<FJsonObject>();
	RemoveEventHandlersProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveEventHandlersProp->SetStringField(TEXT("description"),
		TEXT("Event handlers to remove: [{emitter, stage_usage_id}]"));
	TSharedPtr<FJsonObject> RemoveEventHandlersItems = MakeShared<FJsonObject>();
	RemoveEventHandlersItems->SetStringField(TEXT("type"), TEXT("object"));
	RemoveEventHandlersProp->SetObjectField(TEXT("items"), RemoveEventHandlersItems);
	Properties->SetObjectField(TEXT("remove_event_handlers"), RemoveEventHandlersProp);

	// Update event handlers
	TSharedPtr<FJsonObject> SetEventHandlersProp = MakeShared<FJsonObject>();
	SetEventHandlersProp->SetStringField(TEXT("type"), TEXT("array"));
	SetEventHandlersProp->SetStringField(TEXT("description"),
		TEXT("Edit event handlers: [{emitter, stage_usage_id, source_event_name?, source_emitter?, execution_mode?, spawn_number?, max_events_per_frame?, random_spawn_number?, min_spawn_number?, update_attribute_initial_values?}]"));
	TSharedPtr<FJsonObject> SetEventHandlersItems = MakeShared<FJsonObject>();
	SetEventHandlersItems->SetStringField(TEXT("type"), TEXT("object"));
	SetEventHandlersProp->SetObjectField(TEXT("items"), SetEventHandlersItems);
	Properties->SetObjectField(TEXT("set_event_handlers"), SetEventHandlersProp);

	// Add simulation stages
	TSharedPtr<FJsonObject> AddSimulationStagesProp = MakeShared<FJsonObject>();
	AddSimulationStagesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSimulationStagesProp->SetStringField(TEXT("description"),
		TEXT("Simulation stages to add: [{emitter, stage_class?, name?, enabled?, index?}]. stage_class defaults to /Script/Niagara.NiagaraSimulationStageGeneric."));
	TSharedPtr<FJsonObject> AddSimulationStagesItems = MakeShared<FJsonObject>();
	AddSimulationStagesItems->SetStringField(TEXT("type"), TEXT("object"));
	AddSimulationStagesProp->SetObjectField(TEXT("items"), AddSimulationStagesItems);
	Properties->SetObjectField(TEXT("add_simulation_stages"), AddSimulationStagesProp);

	// Remove simulation stages
	TSharedPtr<FJsonObject> RemoveSimulationStagesProp = MakeShared<FJsonObject>();
	RemoveSimulationStagesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSimulationStagesProp->SetStringField(TEXT("description"),
		TEXT("Simulation stages to remove: [{emitter, stage_usage_id}]"));
	TSharedPtr<FJsonObject> RemoveSimulationStagesItems = MakeShared<FJsonObject>();
	RemoveSimulationStagesItems->SetStringField(TEXT("type"), TEXT("object"));
	RemoveSimulationStagesProp->SetObjectField(TEXT("items"), RemoveSimulationStagesItems);
	Properties->SetObjectField(TEXT("remove_simulation_stages"), RemoveSimulationStagesProp);

	// Update simulation stages
	TSharedPtr<FJsonObject> SetSimulationStagesProp = MakeShared<FJsonObject>();
	SetSimulationStagesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetSimulationStagesProp->SetStringField(TEXT("description"),
		TEXT("Edit simulation stages: [{emitter, stage_usage_id, name?, enabled?}]"));
	TSharedPtr<FJsonObject> SetSimulationStagesItems = MakeShared<FJsonObject>();
	SetSimulationStagesItems->SetStringField(TEXT("type"), TEXT("object"));
	SetSimulationStagesProp->SetObjectField(TEXT("items"), SetSimulationStagesItems);
	Properties->SetObjectField(TEXT("set_simulation_stages"), SetSimulationStagesProp);

	// Reorder simulation stages
	TSharedPtr<FJsonObject> ReorderSimulationStagesProp = MakeShared<FJsonObject>();
	ReorderSimulationStagesProp->SetStringField(TEXT("type"), TEXT("array"));
	ReorderSimulationStagesProp->SetStringField(TEXT("description"),
		TEXT("Reorder simulation stages: [{emitter, stage_usage_id, new_index}]"));
	TSharedPtr<FJsonObject> ReorderSimulationStagesItems = MakeShared<FJsonObject>();
	ReorderSimulationStagesItems->SetStringField(TEXT("type"), TEXT("object"));
	ReorderSimulationStagesProp->SetObjectField(TEXT("items"), ReorderSimulationStagesItems);
	Properties->SetObjectField(TEXT("reorder_simulation_stages"), ReorderSimulationStagesProp);

	// Set module enabled
	TSharedPtr<FJsonObject> SetModuleEnabledProp = MakeShared<FJsonObject>();
	SetModuleEnabledProp->SetStringField(TEXT("type"), TEXT("array"));
	SetModuleEnabledProp->SetStringField(TEXT("description"),
		TEXT("Enable/disable modules: [{module_name, emitter?, stage, enabled}]. Quick on/off without removal."));
	TSharedPtr<FJsonObject> SetModuleEnabledItems = MakeShared<FJsonObject>();
	SetModuleEnabledItems->SetStringField(TEXT("type"), TEXT("object"));
	SetModuleEnabledProp->SetObjectField(TEXT("items"), SetModuleEnabledItems);
	Properties->SetObjectField(TEXT("set_module_enabled"), SetModuleEnabledProp);

	// Set system properties
	TSharedPtr<FJsonObject> SetSystemPropsProp = MakeShared<FJsonObject>();
	SetSystemPropsProp->SetStringField(TEXT("type"), TEXT("object"));
	SetSystemPropsProp->SetStringField(TEXT("description"),
		TEXT("System-level properties: {warmup_time?, determinism?, random_seed?}. warmup_time in seconds."));
	Properties->SetObjectField(TEXT("set_system_properties"), SetSystemPropsProp);

	// Generic reflected property writes
	TSharedPtr<FJsonObject> SetReflectedProp = MakeShared<FJsonObject>();
	SetReflectedProp->SetStringField(TEXT("type"), TEXT("array"));
	SetReflectedProp->SetStringField(TEXT("description"),
		TEXT("Set reflected properties dynamically: "
			 "[{target:'system'|'emitter'|'renderer'|'simulation_stage'|'baker_settings', emitter?, renderer_index?, stage_usage_id?, properties:{field:value,...}}]. "
			 "Graph edits are not supported here; use edit_graph for graph operations."));
	TSharedPtr<FJsonObject> SetReflectedItems = MakeShared<FJsonObject>();
	SetReflectedItems->SetStringField(TEXT("type"), TEXT("object"));
	SetReflectedProp->SetObjectField(TEXT("items"), SetReflectedItems);
	Properties->SetObjectField(TEXT("set_reflected_properties"), SetReflectedProp);

	TSharedPtr<FJsonObject> SetModuleVersionProp = MakeShared<FJsonObject>();
	SetModuleVersionProp->SetStringField(TEXT("type"), TEXT("array"));
	SetModuleVersionProp->SetStringField(TEXT("description"),
		TEXT("Set module script version: [{module_name, emitter?, stage, stage_usage_id?, version_guid}]"));
	TSharedPtr<FJsonObject> SetModuleVersionItems = MakeShared<FJsonObject>();
	SetModuleVersionItems->SetStringField(TEXT("type"), TEXT("object"));
	SetModuleVersionProp->SetObjectField(TEXT("items"), SetModuleVersionItems);
	Properties->SetObjectField(TEXT("set_module_script_version"), SetModuleVersionProp);

	TSharedPtr<FJsonObject> SetEmitterVersionProp = MakeShared<FJsonObject>();
	SetEmitterVersionProp->SetStringField(TEXT("type"), TEXT("array"));
	SetEmitterVersionProp->SetStringField(TEXT("description"),
		TEXT("Set emitter version: [{emitter, version_guid}]"));
	TSharedPtr<FJsonObject> SetEmitterVersionItems = MakeShared<FJsonObject>();
	SetEmitterVersionItems->SetStringField(TEXT("type"), TEXT("object"));
	SetEmitterVersionProp->SetObjectField(TEXT("items"), SetEmitterVersionItems);
	Properties->SetObjectField(TEXT("set_emitter_version"), SetEmitterVersionProp);

	TSharedPtr<FJsonObject> SubscribeParamDefsProp = MakeShared<FJsonObject>();
	SubscribeParamDefsProp->SetStringField(TEXT("type"), TEXT("array"));
	SubscribeParamDefsProp->SetStringField(TEXT("description"),
		TEXT("Subscribe parameter definitions: [{asset_path}]"));
	TSharedPtr<FJsonObject> SubscribeParamDefsItems = MakeShared<FJsonObject>();
	SubscribeParamDefsItems->SetStringField(TEXT("type"), TEXT("object"));
	SubscribeParamDefsProp->SetObjectField(TEXT("items"), SubscribeParamDefsItems);
	Properties->SetObjectField(TEXT("subscribe_parameter_definitions"), SubscribeParamDefsProp);

	TSharedPtr<FJsonObject> UnsubscribeParamDefsProp = MakeShared<FJsonObject>();
	UnsubscribeParamDefsProp->SetStringField(TEXT("type"), TEXT("array"));
	UnsubscribeParamDefsProp->SetStringField(TEXT("description"),
		TEXT("Unsubscribe parameter definitions: [{definitions_id}]"));
	TSharedPtr<FJsonObject> UnsubscribeParamDefsItems = MakeShared<FJsonObject>();
	UnsubscribeParamDefsItems->SetStringField(TEXT("type"), TEXT("object"));
	UnsubscribeParamDefsProp->SetObjectField(TEXT("items"), UnsubscribeParamDefsItems);
	Properties->SetObjectField(TEXT("unsubscribe_parameter_definitions"), UnsubscribeParamDefsProp);

	TSharedPtr<FJsonObject> SyncParamDefsProp = MakeShared<FJsonObject>();
	SyncParamDefsProp->SetStringField(TEXT("type"), TEXT("boolean"));
	SyncParamDefsProp->SetStringField(TEXT("description"), TEXT("If true, synchronize subscribed parameter definitions to system and emitters."));
	Properties->SetObjectField(TEXT("synchronize_parameter_definitions"), SyncParamDefsProp);

	TSharedPtr<FJsonObject> RunValidationProp = MakeShared<FJsonObject>();
	RunValidationProp->SetStringField(TEXT("type"), TEXT("boolean"));
	RunValidationProp->SetStringField(TEXT("description"), TEXT("If true, run Niagara validation and return findings."));
	Properties->SetObjectField(TEXT("run_validation"), RunValidationProp);

	// Scratch pad script authoring
	TSharedPtr<FJsonObject> CreateScratchScriptsProp = MakeShared<FJsonObject>();
	CreateScratchScriptsProp->SetStringField(TEXT("type"), TEXT("array"));
	CreateScratchScriptsProp->SetStringField(TEXT("description"),
		TEXT("Create scratch pad scripts: [{name, script_type:'module|dynamic_input', duplicate_from?, target_stage?, emitter?, output_type?}]"));
	TSharedPtr<FJsonObject> CreateScratchScriptsItems = MakeShared<FJsonObject>();
	CreateScratchScriptsItems->SetStringField(TEXT("type"), TEXT("object"));
	CreateScratchScriptsProp->SetObjectField(TEXT("items"), CreateScratchScriptsItems);
	Properties->SetObjectField(TEXT("create_scratch_pad_scripts"), CreateScratchScriptsProp);

	TSharedPtr<FJsonObject> DeleteScratchScriptsProp = MakeShared<FJsonObject>();
	DeleteScratchScriptsProp->SetStringField(TEXT("type"), TEXT("array"));
	DeleteScratchScriptsProp->SetStringField(TEXT("description"), TEXT("Delete scratch pad scripts: [{name}]"));
	TSharedPtr<FJsonObject> DeleteScratchScriptsItems = MakeShared<FJsonObject>();
	DeleteScratchScriptsItems->SetStringField(TEXT("type"), TEXT("object"));
	DeleteScratchScriptsProp->SetObjectField(TEXT("items"), DeleteScratchScriptsItems);
	Properties->SetObjectField(TEXT("delete_scratch_pad_scripts"), DeleteScratchScriptsProp);

	TSharedPtr<FJsonObject> RenameScratchScriptsProp = MakeShared<FJsonObject>();
	RenameScratchScriptsProp->SetStringField(TEXT("type"), TEXT("array"));
	RenameScratchScriptsProp->SetStringField(TEXT("description"), TEXT("Rename scratch pad scripts: [{name, new_name}]"));
	TSharedPtr<FJsonObject> RenameScratchScriptsItems = MakeShared<FJsonObject>();
	RenameScratchScriptsItems->SetStringField(TEXT("type"), TEXT("object"));
	RenameScratchScriptsProp->SetObjectField(TEXT("items"), RenameScratchScriptsItems);
	Properties->SetObjectField(TEXT("rename_scratch_pad_scripts"), RenameScratchScriptsProp);

	TSharedPtr<FJsonObject> AddScratchModulesProp = MakeShared<FJsonObject>();
	AddScratchModulesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddScratchModulesProp->SetStringField(TEXT("description"),
		TEXT("Add modules to scratch script graphs: [{scratch_script, module_path, name?, index?, parameters?}]"));
	TSharedPtr<FJsonObject> AddScratchModulesItems = MakeShared<FJsonObject>();
	AddScratchModulesItems->SetStringField(TEXT("type"), TEXT("object"));
	AddScratchModulesProp->SetObjectField(TEXT("items"), AddScratchModulesItems);
	Properties->SetObjectField(TEXT("add_scratch_modules"), AddScratchModulesProp);

	TSharedPtr<FJsonObject> RemoveScratchModulesProp = MakeShared<FJsonObject>();
	RemoveScratchModulesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveScratchModulesProp->SetStringField(TEXT("description"),
		TEXT("Remove modules from scratch script graphs: [{scratch_script, module_name}]"));
	TSharedPtr<FJsonObject> RemoveScratchModulesItems = MakeShared<FJsonObject>();
	RemoveScratchModulesItems->SetStringField(TEXT("type"), TEXT("object"));
	RemoveScratchModulesProp->SetObjectField(TEXT("items"), RemoveScratchModulesItems);
	Properties->SetObjectField(TEXT("remove_scratch_modules"), RemoveScratchModulesProp);

	TSharedPtr<FJsonObject> SetScratchModulesProp = MakeShared<FJsonObject>();
	SetScratchModulesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetScratchModulesProp->SetStringField(TEXT("description"),
		TEXT("Set module parameters in scratch script graphs: [{scratch_script, module_name, parameters}]"));
	TSharedPtr<FJsonObject> SetScratchModulesItems = MakeShared<FJsonObject>();
	SetScratchModulesItems->SetStringField(TEXT("type"), TEXT("object"));
	SetScratchModulesProp->SetObjectField(TEXT("items"), SetScratchModulesItems);
	Properties->SetObjectField(TEXT("set_scratch_parameters"), SetScratchModulesProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditNiagaraTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// Keep graph/node edits in edit_graph.
	static const TCHAR* GraphOnlyFields[] = {
		TEXT("operation"), TEXT("graph_name"), TEXT("add_nodes"), TEXT("remove_nodes"),
		TEXT("connections"), TEXT("set_pins"), TEXT("set_node_values"), TEXT("move_nodes"), TEXT("layout_nodes")
	};
	for (const TCHAR* FieldName : GraphOnlyFields)
	{
		if (Args->HasField(FieldName))
		{
			return FToolResult::Fail(FString::Printf(
				TEXT("Field '%s' is graph-specific. Use edit_graph for Niagara graph/node operations."),
				FieldName));
		}
	}

	// Parse asset name
	FString AssetName;
	if (!Args->TryGetStringField(TEXT("asset"), AssetName) || AssetName.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: asset"));
	}

	// Parse path
	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);
	if (Path.IsEmpty())
	{
		Path = TEXT("/Game");
	}

	// Build asset path and load
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(AssetName, Path);

	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, *FullAssetPath);
	if (!System)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Niagara System not found: %s"), *FullAssetPath));
	}

	// Track results
	TArray<FString> AddedModules;
	TArray<FString> RemovedModules;
	TArray<FString> SetParameters;
	TArray<FString> AddedEmitters;
	TArray<FString> Errors;
	FString DiscoveryOutput;

	// ========== Discovery Operations (run first) ==========

	bool bListUserParams = false;
	Args->TryGetBoolField(TEXT("list_user_parameters"), bListUserParams);
	if (bListUserParams)
	{
		DiscoveryOutput += ListUserParameters(System);
	}

	FString ListDITypeFilter;
	if (Args->TryGetStringField(TEXT("list_dynamic_inputs"), ListDITypeFilter))
	{
		DiscoveryOutput += ListDynamicInputs(ListDITypeFilter);
	}

	const TArray<TSharedPtr<FJsonValue>>* ListReflectedArray = nullptr;
	if (Args->TryGetArrayField(TEXT("list_reflected_properties"), ListReflectedArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ListReflectedArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				DiscoveryOutput += TEXT("ERROR: list_reflected_properties entry must be an object\n");
				continue;
			}

			FReflectedPropertyTarget Def;
			FString ParseError;
			if (!ParseReflectedPropertyTarget(*Obj, Def, ParseError))
			{
				DiscoveryOutput += FString::Printf(TEXT("ERROR: %s\n"), *ParseError);
				continue;
			}

			UObject* TargetObject = nullptr;
			FString TargetLabel;
			FString ResolveError;
			if (!ResolveReflectedPropertyTargetObject(System, Def, TargetObject, TargetLabel, ResolveError))
			{
				DiscoveryOutput += FString::Printf(TEXT("ERROR: %s\n"), *ResolveError);
				continue;
			}

			if (!DiscoveryOutput.IsEmpty() && !DiscoveryOutput.EndsWith(TEXT("\n")))
			{
				DiscoveryOutput += TEXT("\n");
			}
			DiscoveryOutput += ListReflectedObjectProperties(TargetObject, TargetLabel);
			DiscoveryOutput += TEXT("\n");
		}
	}

	bool bListVersions = false;
	Args->TryGetBoolField(TEXT("list_versions"), bListVersions);
	if (bListVersions)
	{
		if (!DiscoveryOutput.IsEmpty() && !DiscoveryOutput.EndsWith(TEXT("\n")))
		{
			DiscoveryOutput += TEXT("\n");
		}
		DiscoveryOutput += ListVersions(System);
		DiscoveryOutput += TEXT("\n");
	}

	bool bListParamDefs = false;
	Args->TryGetBoolField(TEXT("list_parameter_definitions"), bListParamDefs);
	if (bListParamDefs)
	{
		if (!DiscoveryOutput.IsEmpty() && !DiscoveryOutput.EndsWith(TEXT("\n")))
		{
			DiscoveryOutput += TEXT("\n");
		}
		DiscoveryOutput += ListParameterDefinitions(System);
		DiscoveryOutput += TEXT("\n");
	}

	bool bListValidationRuleSets = false;
	Args->TryGetBoolField(TEXT("list_validation_rule_sets"), bListValidationRuleSets);
	if (bListValidationRuleSets)
	{
		if (!DiscoveryOutput.IsEmpty() && !DiscoveryOutput.EndsWith(TEXT("\n")))
		{
			DiscoveryOutput += TEXT("\n");
		}
		DiscoveryOutput += ListValidationRuleSets(System);
		DiscoveryOutput += TEXT("\n");
	}

	// If only discovery operations were requested, return early
	if (!DiscoveryOutput.IsEmpty() &&
		!Args->HasField(TEXT("add_emitters")) &&
		!Args->HasField(TEXT("remove_emitters")) &&
		!Args->HasField(TEXT("rename_emitters")) &&
		!Args->HasField(TEXT("duplicate_emitters")) &&
		!Args->HasField(TEXT("reorder_emitters")) &&
		!Args->HasField(TEXT("add_modules")) &&
		!Args->HasField(TEXT("remove_modules")) &&
		!Args->HasField(TEXT("set_parameters")) &&
		!Args->HasField(TEXT("move_modules")) &&
		!Args->HasField(TEXT("add_renderers")) &&
		!Args->HasField(TEXT("remove_renderers")) &&
		!Args->HasField(TEXT("configure_renderer")) &&
		!Args->HasField(TEXT("reorder_renderers")) &&
		!Args->HasField(TEXT("add_user_parameters")) &&
		!Args->HasField(TEXT("remove_user_parameters")) &&
		!Args->HasField(TEXT("set_user_parameters")) &&
		!Args->HasField(TEXT("set_emitter_properties")) &&
		!Args->HasField(TEXT("add_event_handlers")) &&
		!Args->HasField(TEXT("remove_event_handlers")) &&
		!Args->HasField(TEXT("set_event_handlers")) &&
		!Args->HasField(TEXT("add_simulation_stages")) &&
		!Args->HasField(TEXT("remove_simulation_stages")) &&
		!Args->HasField(TEXT("set_simulation_stages")) &&
		!Args->HasField(TEXT("reorder_simulation_stages")) &&
		!Args->HasField(TEXT("set_module_enabled")) &&
			!Args->HasField(TEXT("set_system_properties")) &&
			!Args->HasField(TEXT("set_reflected_properties")) &&
			!Args->HasField(TEXT("set_module_script_version")) &&
			!Args->HasField(TEXT("set_emitter_version")) &&
			!Args->HasField(TEXT("subscribe_parameter_definitions")) &&
			!Args->HasField(TEXT("unsubscribe_parameter_definitions")) &&
			!Args->HasField(TEXT("synchronize_parameter_definitions")) &&
			!Args->HasField(TEXT("run_validation")) &&
			!Args->HasField(TEXT("create_scratch_pad_scripts")) &&
			!Args->HasField(TEXT("delete_scratch_pad_scripts")) &&
			!Args->HasField(TEXT("rename_scratch_pad_scripts")) &&
			!Args->HasField(TEXT("add_scratch_modules")) &&
			!Args->HasField(TEXT("remove_scratch_modules")) &&
			!Args->HasField(TEXT("set_scratch_parameters")))
	{
		return FToolResult::Ok(DiscoveryOutput);
	}

	// Begin transaction for undo/redo support
	FScopedTransaction Transaction(FText::FromString(FString::Printf(TEXT("Edit Niagara System: %s"), *AssetName)));

	// Track renderer results
	TArray<FString> AddedRenderers;
	TArray<FString> RemovedRenderers;
	TArray<FString> ConfiguredRenderers;

	// Track user parameter results
	TArray<FString> AddedUserParams;
	TArray<FString> RemovedUserParams;
	TArray<FString> SetUserParams;

	// Track emitter property results
	TArray<FString> SetEmitterProps;
	TArray<FString> AddedEventHandlers;
	TArray<FString> RemovedEventHandlers;
	TArray<FString> UpdatedEventHandlers;
	TArray<FString> AddedSimulationStages;
	TArray<FString> RemovedSimulationStages;
	TArray<FString> UpdatedSimulationStages;
	TArray<FString> ReorderedSimulationStages;

	// Track module enable results
	TArray<FString> ModuleEnables;
	TArray<FString> MovedModules;
	TArray<FString> ScratchPadScripts;
	TArray<FString> ReflectedPropertySets;
	TArray<FString> VersionChanges;
	TArray<FString> ParameterDefinitionChanges;
	FString ValidationOutput;

	// Track system property result
	FString SystemPropsResult;

	// Process add_emitters
	const TArray<TSharedPtr<FJsonValue>>* AddEmittersArray;
	if (Args->TryGetArrayField(TEXT("add_emitters"), AddEmittersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddEmittersArray)
		{
			const TSharedPtr<FJsonObject>* EmitterObj;
			if (Value->TryGetObject(EmitterObj))
			{
				FEmitterDefinition EmitterDef;
				FString ParseError;
				if (ParseEmitterDefinition(*EmitterObj, EmitterDef, ParseError))
				{
					FString Result = AddEmitter(System, EmitterDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedEmitters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_emitters
	const TArray<TSharedPtr<FJsonValue>>* RemoveEmittersArray;
	if (Args->TryGetArrayField(TEXT("remove_emitters"), RemoveEmittersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveEmittersArray)
		{
			const TSharedPtr<FJsonObject>* EmitterObj;
			if (Value->TryGetObject(EmitterObj))
			{
				FEmitterRemoval EmitterDef;
				FString ParseError;
				if (ParseEmitterRemoval(*EmitterObj, EmitterDef, ParseError))
				{
					FString Result = RemoveEmitter(System, EmitterDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedEmitters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process rename_emitters
	const TArray<TSharedPtr<FJsonValue>>* RenameEmittersArray;
	if (Args->TryGetArrayField(TEXT("rename_emitters"), RenameEmittersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RenameEmittersArray)
		{
			const TSharedPtr<FJsonObject>* EmitterObj;
			if (Value->TryGetObject(EmitterObj))
			{
				FEmitterRename EmitterDef;
				FString ParseError;
				if (ParseEmitterRename(*EmitterObj, EmitterDef, ParseError))
				{
					FString Result = RenameEmitter(System, EmitterDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedEmitters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process duplicate_emitters
	const TArray<TSharedPtr<FJsonValue>>* DuplicateEmittersArray;
	if (Args->TryGetArrayField(TEXT("duplicate_emitters"), DuplicateEmittersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *DuplicateEmittersArray)
		{
			const TSharedPtr<FJsonObject>* EmitterObj;
			if (Value->TryGetObject(EmitterObj))
			{
				FEmitterDuplicate EmitterDef;
				FString ParseError;
				if (ParseEmitterDuplicate(*EmitterObj, EmitterDef, ParseError))
				{
					FString Result = DuplicateEmitter(System, EmitterDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedEmitters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process reorder_emitters
	const TArray<TSharedPtr<FJsonValue>>* ReorderEmittersArray;
	if (Args->TryGetArrayField(TEXT("reorder_emitters"), ReorderEmittersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ReorderEmittersArray)
		{
			const TSharedPtr<FJsonObject>* EmitterObj;
			if (Value->TryGetObject(EmitterObj))
			{
				FEmitterReorder EmitterDef;
				FString ParseError;
				if (ParseEmitterReorder(*EmitterObj, EmitterDef, ParseError))
				{
					FString Result = ReorderEmitter(System, EmitterDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedEmitters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process add_modules
	const TArray<TSharedPtr<FJsonValue>>* AddModulesArray;
	if (Args->TryGetArrayField(TEXT("add_modules"), AddModulesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddModulesArray)
		{
			const TSharedPtr<FJsonObject>* ModuleObj;
			if (Value->TryGetObject(ModuleObj))
			{
				FModuleDefinition ModDef;
				FString ParseError;
				if (ParseModuleDefinition(*ModuleObj, ModDef, ParseError))
				{
					FString Result = AddModule(System, ModDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedModules.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_modules
	const TArray<TSharedPtr<FJsonValue>>* RemoveModulesArray;
	if (Args->TryGetArrayField(TEXT("remove_modules"), RemoveModulesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveModulesArray)
		{
			const TSharedPtr<FJsonObject>* ModuleObj;
			if (Value->TryGetObject(ModuleObj))
			{
				FModuleRemoval RemovalDef;
				FString ParseError;
				if (ParseModuleRemoval(*ModuleObj, RemovalDef, ParseError))
				{
					FString Result = RemoveModule(System, RemovalDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						RemovedModules.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_parameters
	const TArray<TSharedPtr<FJsonValue>>* SetParamsArray;
	if (Args->TryGetArrayField(TEXT("set_parameters"), SetParamsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetParamsArray)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (Value->TryGetObject(ParamObj))
			{
				FSetParameterOp SetOp;
				FString ParseError;
				if (ParseSetParameterOp(*ParamObj, SetOp, ParseError))
				{
					FString Result = SetModuleParameters(System, SetOp);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						SetParameters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process move_modules
	const TArray<TSharedPtr<FJsonValue>>* MoveModulesArray;
	if (Args->TryGetArrayField(TEXT("move_modules"), MoveModulesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *MoveModulesArray)
		{
			const TSharedPtr<FJsonObject>* MoveObj;
			if (Value->TryGetObject(MoveObj))
			{
				FModuleMoveOp MoveOp;
				FString ParseError;
				if (ParseModuleMoveOp(*MoveObj, MoveOp, ParseError))
				{
					FString Result = MoveModule(System, MoveOp);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						MovedModules.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process add_renderers
	const TArray<TSharedPtr<FJsonValue>>* AddRenderersArray;
	if (Args->TryGetArrayField(TEXT("add_renderers"), AddRenderersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddRenderersArray)
		{
			const TSharedPtr<FJsonObject>* RendererObj;
			if (Value->TryGetObject(RendererObj))
			{
				FRendererDefinition RendererDef;
				FString ParseError;
				if (ParseRendererDefinition(*RendererObj, RendererDef, ParseError))
				{
					FString Result = AddRenderer(System, RendererDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedRenderers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_renderers
	const TArray<TSharedPtr<FJsonValue>>* RemoveRenderersArray;
	if (Args->TryGetArrayField(TEXT("remove_renderers"), RemoveRenderersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveRenderersArray)
		{
			const TSharedPtr<FJsonObject>* RendererObj;
			if (Value->TryGetObject(RendererObj))
			{
				FRendererRemoval RemovalDef;
				FString ParseError;
				if (ParseRendererRemoval(*RendererObj, RemovalDef, ParseError))
				{
					FString Result = RemoveRenderer(System, RemovalDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						RemovedRenderers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process configure_renderer
	const TArray<TSharedPtr<FJsonValue>>* ConfigureRenderersArray;
	if (Args->TryGetArrayField(TEXT("configure_renderer"), ConfigureRenderersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ConfigureRenderersArray)
		{
			const TSharedPtr<FJsonObject>* ConfigObj;
			if (Value->TryGetObject(ConfigObj))
			{
				FRendererConfiguration Config;
				FString ParseError;
				if (ParseRendererConfiguration(*ConfigObj, Config, ParseError))
				{
					FString Result = ConfigureRenderer(System, Config);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ConfiguredRenderers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process reorder_renderers
	const TArray<TSharedPtr<FJsonValue>>* ReorderRenderersArray;
	if (Args->TryGetArrayField(TEXT("reorder_renderers"), ReorderRenderersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ReorderRenderersArray)
		{
			const TSharedPtr<FJsonObject>* ReorderObj;
			if (Value->TryGetObject(ReorderObj))
			{
				FRendererReorder ReorderOp;
				FString ParseError;
				if (ParseRendererReorder(*ReorderObj, ReorderOp, ParseError))
				{
					FString Result = ReorderRenderer(System, ReorderOp);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ConfiguredRenderers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process add_user_parameters
	const TArray<TSharedPtr<FJsonValue>>* AddUserParamsArray;
	if (Args->TryGetArrayField(TEXT("add_user_parameters"), AddUserParamsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddUserParamsArray)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (Value->TryGetObject(ParamObj))
			{
				FUserParameterDefinition ParamDef;
				FString ParseError;
				if (ParseUserParameterDefinition(*ParamObj, ParamDef, ParseError))
				{
					FString Result = AddUserParameter(System, ParamDef);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedUserParams.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_user_parameters
	const TArray<TSharedPtr<FJsonValue>>* RemoveUserParamsArray;
	if (Args->TryGetArrayField(TEXT("remove_user_parameters"), RemoveUserParamsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveUserParamsArray)
		{
			FString ParamName;
			if (Value->TryGetString(ParamName))
			{
				FString Result = RemoveUserParameter(System, ParamName);
				if (Result.StartsWith(TEXT("ERROR:")))
				{
					Errors.Add(Result);
				}
				else
				{
					RemovedUserParams.Add(Result);
				}
			}
		}
	}

	// Process set_user_parameters
	const TArray<TSharedPtr<FJsonValue>>* SetUserParamsArray;
	if (Args->TryGetArrayField(TEXT("set_user_parameters"), SetUserParamsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetUserParamsArray)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (Value->TryGetObject(ParamObj))
			{
				FUserParameterValue ParamValue;
				FString ParseError;
				if (ParseUserParameterValue(*ParamObj, ParamValue, ParseError))
				{
					FString Result = SetUserParameterValue(System, ParamValue);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						SetUserParams.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_emitter_properties
	const TArray<TSharedPtr<FJsonValue>>* SetEmitterPropsArray;
	if (Args->TryGetArrayField(TEXT("set_emitter_properties"), SetEmitterPropsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetEmitterPropsArray)
		{
			const TSharedPtr<FJsonObject>* PropsObj;
			if (Value->TryGetObject(PropsObj))
			{
				FEmitterPropertySet Props;
				FString ParseError;
				if (ParseEmitterPropertySet(*PropsObj, Props, ParseError))
				{
					FString Result = SetEmitterProperties(System, Props);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						SetEmitterProps.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process add_event_handlers
	const TArray<TSharedPtr<FJsonValue>>* AddEventHandlersArray;
	if (Args->TryGetArrayField(TEXT("add_event_handlers"), AddEventHandlersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddEventHandlersArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FEventHandlerDefinition Def;
				FString ParseError;
				if (ParseEventHandlerDefinition(*Obj, Def, ParseError))
				{
					FString Result = AddEventHandler(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedEventHandlers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_event_handlers
	const TArray<TSharedPtr<FJsonValue>>* RemoveEventHandlersArray;
	if (Args->TryGetArrayField(TEXT("remove_event_handlers"), RemoveEventHandlersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveEventHandlersArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FEventHandlerRemoval Def;
				FString ParseError;
				if (ParseEventHandlerRemoval(*Obj, Def, ParseError))
				{
					FString Result = RemoveEventHandler(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						RemovedEventHandlers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_event_handlers
	const TArray<TSharedPtr<FJsonValue>>* SetEventHandlersArray;
	if (Args->TryGetArrayField(TEXT("set_event_handlers"), SetEventHandlersArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetEventHandlersArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FEventHandlerUpdate Def;
				FString ParseError;
				if (ParseEventHandlerUpdate(*Obj, Def, ParseError))
				{
					FString Result = SetEventHandler(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						UpdatedEventHandlers.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process add_simulation_stages
	const TArray<TSharedPtr<FJsonValue>>* AddSimulationStagesArray;
	if (Args->TryGetArrayField(TEXT("add_simulation_stages"), AddSimulationStagesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddSimulationStagesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FSimulationStageDefinition Def;
				FString ParseError;
				if (ParseSimulationStageDefinition(*Obj, Def, ParseError))
				{
					FString Result = AddSimulationStage(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedSimulationStages.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_simulation_stages
	const TArray<TSharedPtr<FJsonValue>>* RemoveSimulationStagesArray;
	if (Args->TryGetArrayField(TEXT("remove_simulation_stages"), RemoveSimulationStagesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveSimulationStagesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FSimulationStageRemoval Def;
				FString ParseError;
				if (ParseSimulationStageRemoval(*Obj, Def, ParseError))
				{
					FString Result = RemoveSimulationStage(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						RemovedSimulationStages.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_simulation_stages
	const TArray<TSharedPtr<FJsonValue>>* SetSimulationStagesArray;
	if (Args->TryGetArrayField(TEXT("set_simulation_stages"), SetSimulationStagesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetSimulationStagesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FSimulationStageUpdate Def;
				FString ParseError;
				if (ParseSimulationStageUpdate(*Obj, Def, ParseError))
				{
					FString Result = SetSimulationStage(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						UpdatedSimulationStages.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process reorder_simulation_stages
	const TArray<TSharedPtr<FJsonValue>>* ReorderSimulationStagesArray;
	if (Args->TryGetArrayField(TEXT("reorder_simulation_stages"), ReorderSimulationStagesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *ReorderSimulationStagesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FSimulationStageReorder Def;
				FString ParseError;
				if (ParseSimulationStageReorder(*Obj, Def, ParseError))
				{
					FString Result = ReorderSimulationStage(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ReorderedSimulationStages.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_module_enabled
	const TArray<TSharedPtr<FJsonValue>>* SetModuleEnabledArray;
	if (Args->TryGetArrayField(TEXT("set_module_enabled"), SetModuleEnabledArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetModuleEnabledArray)
		{
			const TSharedPtr<FJsonObject>* EnableObj;
			if (Value->TryGetObject(EnableObj))
			{
				FModuleEnableOp EnableOp;
				FString ParseError;
				if (ParseModuleEnableOp(*EnableObj, EnableOp, ParseError))
				{
					FString Result = SetModuleEnabled(System, EnableOp);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ModuleEnables.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_system_properties
	const TSharedPtr<FJsonObject>* SystemPropsObj;
	if (Args->TryGetObjectField(TEXT("set_system_properties"), SystemPropsObj))
	{
		FSystemPropertySet Props;
		FString ParseError;
		if (ParseSystemPropertySet(*SystemPropsObj, Props, ParseError))
		{
			SystemPropsResult = SetSystemProperties(System, Props);
			if (SystemPropsResult.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(SystemPropsResult);
				SystemPropsResult.Empty();
			}
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
		}
	}

	// Process set_reflected_properties
	const TArray<TSharedPtr<FJsonValue>>* SetReflectedArray = nullptr;
	if (Args->TryGetArrayField(TEXT("set_reflected_properties"), SetReflectedArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetReflectedArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				Errors.Add(TEXT("ERROR: set_reflected_properties entry must be an object"));
				continue;
			}

			FReflectedPropertyTarget Def;
			FString ParseError;
			if (!ParseReflectedPropertyTarget(*Obj, Def, ParseError))
			{
				Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				continue;
			}
			if (!Def.Properties.IsValid() || Def.Properties->Values.Num() == 0)
			{
				Errors.Add(TEXT("ERROR: set_reflected_properties requires non-empty 'properties' object"));
				continue;
			}

			UObject* TargetObject = nullptr;
			FString TargetLabel;
			FString ResolveError;
			if (!ResolveReflectedPropertyTargetObject(System, Def, TargetObject, TargetLabel, ResolveError))
			{
				Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ResolveError));
				continue;
			}

			const FString Result = SetReflectedObjectProperties(TargetObject, TargetLabel, Def.Properties);
			if (Result.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(Result);
			}
			else
			{
				ReflectedPropertySets.Add(Result);
			}
		}
	}

	// Process set_module_script_version
	const TArray<TSharedPtr<FJsonValue>>* SetModuleVersionArray = nullptr;
	if (Args->TryGetArrayField(TEXT("set_module_script_version"), SetModuleVersionArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetModuleVersionArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				Errors.Add(TEXT("ERROR: set_module_script_version entry must be an object"));
				continue;
			}

			FModuleVersionSet Def;
			FString ParseError;
			if (!ParseModuleVersionSet(*Obj, Def, ParseError))
			{
				Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				continue;
			}

			const FString Result = SetModuleScriptVersion(System, Def);
			if (Result.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(Result);
			}
			else
			{
				VersionChanges.Add(Result);
			}
		}
	}

	// Process set_emitter_version
	const TArray<TSharedPtr<FJsonValue>>* SetEmitterVersionArray = nullptr;
	if (Args->TryGetArrayField(TEXT("set_emitter_version"), SetEmitterVersionArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetEmitterVersionArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				Errors.Add(TEXT("ERROR: set_emitter_version entry must be an object"));
				continue;
			}

			FEmitterVersionSet Def;
			FString ParseError;
			if (!ParseEmitterVersionSet(*Obj, Def, ParseError))
			{
				Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				continue;
			}

			const FString Result = SetEmitterVersion(System, Def);
			if (Result.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(Result);
			}
			else
			{
				VersionChanges.Add(Result);
			}
		}
	}

	// Process subscribe_parameter_definitions
	const TArray<TSharedPtr<FJsonValue>>* SubscribeParamDefsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("subscribe_parameter_definitions"), SubscribeParamDefsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SubscribeParamDefsArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				Errors.Add(TEXT("ERROR: subscribe_parameter_definitions entry must be an object"));
				continue;
			}

			FParameterDefinitionsOp Def;
			FString ParseError;
			if (!ParseParameterDefinitionsOp(*Obj, Def, ParseError))
			{
				Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				continue;
			}

			const FString Result = SubscribeParameterDefinitions(System, Def);
			if (Result.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(Result);
			}
			else
			{
				ParameterDefinitionChanges.Add(Result);
			}
		}
	}

	// Process unsubscribe_parameter_definitions
	const TArray<TSharedPtr<FJsonValue>>* UnsubscribeParamDefsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("unsubscribe_parameter_definitions"), UnsubscribeParamDefsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *UnsubscribeParamDefsArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (!Value->TryGetObject(Obj) || !Obj || !(*Obj).IsValid())
			{
				Errors.Add(TEXT("ERROR: unsubscribe_parameter_definitions entry must be an object"));
				continue;
			}

			FParameterDefinitionsOp Def;
			FString ParseError;
			if (!ParseParameterDefinitionsOp(*Obj, Def, ParseError))
			{
				Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				continue;
			}

			const FString Result = UnsubscribeParameterDefinitions(System, Def);
			if (Result.StartsWith(TEXT("ERROR:")))
			{
				Errors.Add(Result);
			}
			else
			{
				ParameterDefinitionChanges.Add(Result);
			}
		}
	}

	// Process synchronize_parameter_definitions
	bool bSyncParamDefs = false;
	Args->TryGetBoolField(TEXT("synchronize_parameter_definitions"), bSyncParamDefs);
	if (bSyncParamDefs)
	{
		const FString Result = SynchronizeParameterDefinitions(System);
		if (Result.StartsWith(TEXT("ERROR:")))
		{
			Errors.Add(Result);
		}
		else
		{
			ParameterDefinitionChanges.Add(Result);
		}
	}

	// Process run_validation
	bool bRunValidation = false;
	Args->TryGetBoolField(TEXT("run_validation"), bRunValidation);
	if (bRunValidation)
	{
		ValidationOutput = RunValidation(System);
		if (ValidationOutput.StartsWith(TEXT("ERROR:")))
		{
			Errors.Add(ValidationOutput);
			ValidationOutput.Empty();
		}
	}

	// Process create_scratch_pad_scripts
	const TArray<TSharedPtr<FJsonValue>>* CreateScratchArray;
	if (Args->TryGetArrayField(TEXT("create_scratch_pad_scripts"), CreateScratchArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *CreateScratchArray)
		{
			const TSharedPtr<FJsonObject>* ScratchObj;
			if (Value->TryGetObject(ScratchObj))
			{
				FScratchPadScriptDefinition Def;
				FString ParseError;
				if (ParseScratchPadScriptDefinition(*ScratchObj, Def, ParseError))
				{
					FString Result = CreateScratchPadScript(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ScratchPadScripts.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process delete_scratch_pad_scripts
	const TArray<TSharedPtr<FJsonValue>>* DeleteScratchArray;
	if (Args->TryGetArrayField(TEXT("delete_scratch_pad_scripts"), DeleteScratchArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *DeleteScratchArray)
		{
			const TSharedPtr<FJsonObject>* ScratchObj;
			if (Value->TryGetObject(ScratchObj))
			{
				FScratchPadScriptDeletion Def;
				FString ParseError;
				if (ParseScratchPadScriptDeletion(*ScratchObj, Def, ParseError))
				{
					FString Result = DeleteScratchPadScript(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ScratchPadScripts.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process rename_scratch_pad_scripts
	const TArray<TSharedPtr<FJsonValue>>* RenameScratchArray;
	if (Args->TryGetArrayField(TEXT("rename_scratch_pad_scripts"), RenameScratchArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RenameScratchArray)
		{
			const TSharedPtr<FJsonObject>* ScratchObj;
			if (Value->TryGetObject(ScratchObj))
			{
				FScratchPadScriptRename Def;
				FString ParseError;
				if (ParseScratchPadScriptRename(*ScratchObj, Def, ParseError))
				{
					FString Result = RenameScratchPadScript(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						ScratchPadScripts.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process add_scratch_modules
	const TArray<TSharedPtr<FJsonValue>>* AddScratchModulesArray;
	if (Args->TryGetArrayField(TEXT("add_scratch_modules"), AddScratchModulesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddScratchModulesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FScratchModuleDefinition Def;
				FString ParseError;
				if (ParseScratchModuleDefinition(*Obj, Def, ParseError))
				{
					FString Result = AddScratchModule(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						AddedModules.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process remove_scratch_modules
	const TArray<TSharedPtr<FJsonValue>>* RemoveScratchModulesArray;
	if (Args->TryGetArrayField(TEXT("remove_scratch_modules"), RemoveScratchModulesArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveScratchModulesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FScratchModuleRemoval Def;
				FString ParseError;
				if (ParseScratchModuleRemoval(*Obj, Def, ParseError))
				{
					FString Result = RemoveScratchModule(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						RemovedModules.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Process set_scratch_parameters
	const TArray<TSharedPtr<FJsonValue>>* SetScratchParamsArray;
	if (Args->TryGetArrayField(TEXT("set_scratch_parameters"), SetScratchParamsArray))
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetScratchParamsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Value->TryGetObject(Obj))
			{
				FScratchModuleSetParameters Def;
				FString ParseError;
				if (ParseScratchModuleSetParameters(*Obj, Def, ParseError))
				{
					FString Result = SetScratchModuleParameters(System, Def);
					if (Result.StartsWith(TEXT("ERROR:")))
					{
						Errors.Add(Result);
					}
					else
					{
						SetParameters.Add(Result);
					}
				}
				else
				{
					Errors.Add(FString::Printf(TEXT("ERROR: %s"), *ParseError));
				}
			}
		}
	}

	// Mark system modified and request recompilation
	bool bHadChanges = AddedModules.Num() > 0 || RemovedModules.Num() > 0 || SetParameters.Num() > 0 || AddedEmitters.Num() > 0 ||
		AddedRenderers.Num() > 0 || RemovedRenderers.Num() > 0 || ConfiguredRenderers.Num() > 0 ||
		AddedUserParams.Num() > 0 || RemovedUserParams.Num() > 0 || SetUserParams.Num() > 0 ||
		SetEmitterProps.Num() > 0 || AddedEventHandlers.Num() > 0 || RemovedEventHandlers.Num() > 0 || UpdatedEventHandlers.Num() > 0 ||
		AddedSimulationStages.Num() > 0 || RemovedSimulationStages.Num() > 0 || UpdatedSimulationStages.Num() > 0 || ReorderedSimulationStages.Num() > 0 ||
		ModuleEnables.Num() > 0 || MovedModules.Num() > 0 || ScratchPadScripts.Num() > 0 || !SystemPropsResult.IsEmpty() || ReflectedPropertySets.Num() > 0 ||
		VersionChanges.Num() > 0 || ParameterDefinitionChanges.Num() > 0;

	if (bHadChanges)
	{
		System->Modify();
		System->MarkPackageDirty();

		// Broadcast post-edit change so the Niagara editor (if open) refreshes
		// its view models, resets the running system instance, and stays in sync.
		System->OnSystemPostEditChange().Broadcast(System);

		// Synchronous recompile (including GPU shaders) so we can detect errors
		// before the agent moves on. Without this, a bad configuration (e.g. unset
		// GPU float input) would crash at render time with "required shader parameter
		// was not set" instead of surfacing as a compile error.
		System->RequestCompile(false);
		System->WaitForCompilationComplete(/*bIncludingGPUShaders=*/true, /*bShowProgress=*/false);

		// Collect compile errors/warnings across all scripts and report to agent
		auto CollectCompileEvents = [&Errors](UNiagaraScript* Script, const FString& ScriptLabel)
		{
			if (!Script) return;
			ENiagaraScriptCompileStatus Status = Script->GetLastCompileStatus();
			if (Status == ENiagaraScriptCompileStatus::NCS_Error
				|| Status == ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings
				|| Status == ENiagaraScriptCompileStatus::NCS_ComputeUpToDateWithWarnings)
			{
				const TArray<FNiagaraCompileEvent>& Events = Script->GetVMExecutableData().LastCompileEvents;
				for (const FNiagaraCompileEvent& Event : Events)
				{
					if (Event.Severity == FNiagaraCompileEventSeverity::Error)
					{
						Errors.Add(FString::Printf(TEXT("COMPILE ERROR [%s]: %s"), *ScriptLabel, *Event.Message));
					}
					else if (Event.Severity == FNiagaraCompileEventSeverity::Warning)
					{
						Errors.Add(FString::Printf(TEXT("COMPILE WARNING [%s]: %s"), *ScriptLabel, *Event.Message));
					}
				}
			}
		};

		CollectCompileEvents(System->GetSystemSpawnScript(), TEXT("SystemSpawn"));
		CollectCompileEvents(System->GetSystemUpdateScript(), TEXT("SystemUpdate"));

		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FString EmName = Handle.GetUniqueInstanceName();
			if (FVersionedNiagaraEmitterData* EmData = Handle.GetEmitterData())
			{
				TArray<UNiagaraScript*> EmScripts;
				EmData->GetScripts(EmScripts, /*bCompilableOnly=*/false);
				for (UNiagaraScript* Script : EmScripts)
				{
					if (Script)
					{
						FString Label = FString::Printf(TEXT("%s/%s"), *EmName,
							*StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage())));
						CollectCompileEvents(Script, Label);
					}
				}
			}
		}
	}

	// Format output
	FString Output = FormatResults(AssetName, AddedModules, RemovedModules, SetParameters, AddedEmitters,
		AddedEventHandlers, RemovedEventHandlers, UpdatedEventHandlers,
		AddedSimulationStages, RemovedSimulationStages, UpdatedSimulationStages, ReorderedSimulationStages,
		AddedRenderers, RemovedRenderers, ConfiguredRenderers,
		AddedUserParams, RemovedUserParams, SetUserParams, SetEmitterProps, ModuleEnables, MovedModules, ScratchPadScripts, SystemPropsResult, Errors);

	if (ReflectedPropertySets.Num() > 0)
	{
		Output += TEXT("\n## Reflected Properties\n");
		for (const FString& Entry : ReflectedPropertySets)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Entry);
		}
	}

	if (VersionChanges.Num() > 0)
	{
		Output += TEXT("\n## Version Changes\n");
		for (const FString& Entry : VersionChanges)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Entry);
		}
	}

	if (ParameterDefinitionChanges.Num() > 0)
	{
		Output += TEXT("\n## Parameter Definitions\n");
		for (const FString& Entry : ParameterDefinitionChanges)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Entry);
		}
	}

	if (!ValidationOutput.IsEmpty())
	{
		Output += TEXT("\n");
		Output += ValidationOutput;
	}

	// Append discovery output if present
	if (!DiscoveryOutput.IsEmpty())
	{
		Output += DiscoveryOutput;
	}

	if (Errors.Num() > 0 && AddedModules.Num() == 0 && RemovedModules.Num() == 0 &&
		SetParameters.Num() == 0 && AddedEmitters.Num() == 0 &&
		AddedRenderers.Num() == 0 && RemovedRenderers.Num() == 0 && ConfiguredRenderers.Num() == 0 &&
		AddedUserParams.Num() == 0 && RemovedUserParams.Num() == 0 && SetUserParams.Num() == 0 &&
		SetEmitterProps.Num() == 0 && AddedEventHandlers.Num() == 0 && RemovedEventHandlers.Num() == 0 && UpdatedEventHandlers.Num() == 0 &&
		AddedSimulationStages.Num() == 0 && RemovedSimulationStages.Num() == 0 && UpdatedSimulationStages.Num() == 0 && ReorderedSimulationStages.Num() == 0 &&
		ModuleEnables.Num() == 0 && MovedModules.Num() == 0 && ScratchPadScripts.Num() == 0 && SystemPropsResult.IsEmpty() && ReflectedPropertySets.Num() == 0 &&
		VersionChanges.Num() == 0 && ParameterDefinitionChanges.Num() == 0 && ValidationOutput.IsEmpty())
	{
		return FToolResult::Fail(Output);
	}

	return FToolResult::Ok(Output);
}

// ========== Parsing ==========

ENiagaraScriptUsage FEditNiagaraTool::ParseScriptUsage(const FString& StageStr, bool& bOutValid) const
{
	bOutValid = true;
	FString Lower = StageStr.ToLower();

	// Remove underscores and spaces for flexible matching
	FString Normalized = Lower.Replace(TEXT("_"), TEXT("")).Replace(TEXT(" "), TEXT(""));

	// Particle stages
	if (Normalized == TEXT("particlespawn") || Normalized == TEXT("spawn"))
	{
		return ENiagaraScriptUsage::ParticleSpawnScript;
	}
	if (Normalized == TEXT("particleupdate") || Normalized == TEXT("update") || Normalized == TEXT("particle"))
	{
		return ENiagaraScriptUsage::ParticleUpdateScript;
	}
	if (Normalized == TEXT("particleevent") || Normalized == TEXT("event"))
	{
		return ENiagaraScriptUsage::ParticleEventScript;
	}
	if (Normalized == TEXT("particlesimulationstage") || Normalized == TEXT("simstage") || Normalized == TEXT("simulationstage"))
	{
		return ENiagaraScriptUsage::ParticleSimulationStageScript;
	}

	// Emitter stages
	if (Normalized == TEXT("emitterspawn"))
	{
		return ENiagaraScriptUsage::EmitterSpawnScript;
	}
	if (Normalized == TEXT("emitterupdate") || Normalized == TEXT("emitter"))
	{
		return ENiagaraScriptUsage::EmitterUpdateScript;
	}

	// System stages
	if (Normalized == TEXT("systemspawn"))
	{
		return ENiagaraScriptUsage::SystemSpawnScript;
	}
	if (Normalized == TEXT("systemupdate") || Normalized == TEXT("system"))
	{
		return ENiagaraScriptUsage::SystemUpdateScript;
	}

	// Also accept the enum names directly
	if (Lower == TEXT("particlespawnscript")) return ENiagaraScriptUsage::ParticleSpawnScript;
	if (Lower == TEXT("particleupdatescript")) return ENiagaraScriptUsage::ParticleUpdateScript;
	if (Lower == TEXT("particleeventscript")) return ENiagaraScriptUsage::ParticleEventScript;
	if (Lower == TEXT("particlesimulationstagescript")) return ENiagaraScriptUsage::ParticleSimulationStageScript;
	if (Lower == TEXT("emitterspawnscript")) return ENiagaraScriptUsage::EmitterSpawnScript;
	if (Lower == TEXT("emitterupdatescript")) return ENiagaraScriptUsage::EmitterUpdateScript;
	if (Lower == TEXT("systemspawnscript")) return ENiagaraScriptUsage::SystemSpawnScript;
	if (Lower == TEXT("systemupdatescript")) return ENiagaraScriptUsage::SystemUpdateScript;

	bOutValid = false;
	return ENiagaraScriptUsage::ParticleUpdateScript; // Default
}

FString FEditNiagaraTool::GetValidStageNames()
{
	return TEXT("particle_spawn, particle_update, particle_event, simulation_stage, emitter_spawn, emitter_update, system_spawn, system_update");
}

FString FEditNiagaraTool::UsageToString(ENiagaraScriptUsage Usage) const
{
	switch (Usage)
	{
	case ENiagaraScriptUsage::ParticleSpawnScript: return TEXT("Particle Spawn");
	case ENiagaraScriptUsage::ParticleUpdateScript: return TEXT("Particle Update");
	case ENiagaraScriptUsage::ParticleEventScript: return TEXT("Particle Event");
	case ENiagaraScriptUsage::ParticleSimulationStageScript: return TEXT("Simulation Stage");
	case ENiagaraScriptUsage::EmitterSpawnScript: return TEXT("Emitter Spawn");
	case ENiagaraScriptUsage::EmitterUpdateScript: return TEXT("Emitter Update");
	case ENiagaraScriptUsage::SystemSpawnScript: return TEXT("System Spawn");
	case ENiagaraScriptUsage::SystemUpdateScript: return TEXT("System Update");
	default: return TEXT("Unknown");
	}
}

static bool IsSystemStageUsage(ENiagaraScriptUsage Usage)
{
	return Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript;
}

static bool ParseOptionalGuidField(const TSharedPtr<FJsonObject>& Obj, const TCHAR* FieldName, FGuid& OutGuid, FString& OutError)
{
	FString GuidString;
	if (!Obj.IsValid() || !Obj->TryGetStringField(FieldName, GuidString) || GuidString.IsEmpty())
	{
		return true;
	}

	if (!FGuid::Parse(GuidString, OutGuid))
	{
		OutError = FString::Printf(TEXT("Invalid %s '%s' (expected GUID format)"), FieldName, *GuidString);
		return false;
	}

	return true;
}

static bool ResolveEmitterHandleIdByName(UNiagaraSystem* System, const FString& EmitterName, FGuid& OutEmitterId)
{
	if (!System)
	{
		return false;
	}

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		if (Handle.GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			OutEmitterId = Handle.GetId();
			return true;
		}
	}

	return false;
}

static UClass* ResolveSimulationStageClassByName(const FString& StageClassName, FString& OutError)
{
	if (StageClassName.IsEmpty())
	{
		return UNiagaraSimulationStageGeneric::StaticClass();
	}

	UClass* ResolvedClass = StaticLoadClass(UNiagaraSimulationStageBase::StaticClass(), nullptr, *StageClassName);
	if (!ResolvedClass)
	{
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Candidate = *It;
			if (!Candidate || !Candidate->IsChildOf(UNiagaraSimulationStageBase::StaticClass()))
			{
				continue;
			}

			if (Candidate->GetName().Equals(StageClassName, ESearchCase::IgnoreCase) ||
				Candidate->GetPathName().Equals(StageClassName, ESearchCase::IgnoreCase))
			{
				ResolvedClass = Candidate;
				break;
			}
		}
	}

	if (!ResolvedClass)
	{
		OutError = FString::Printf(TEXT("Unknown simulation stage class '%s'"), *StageClassName);
		return nullptr;
	}

	if (!ResolvedClass->IsChildOf(UNiagaraSimulationStageBase::StaticClass()))
	{
		OutError = FString::Printf(TEXT("Class '%s' is not a Niagara simulation stage class"), *StageClassName);
		return nullptr;
	}

	return ResolvedClass;
}

static bool EnsureNiagaraGraphOutputForUsage(UNiagaraScriptSource* Source, ENiagaraScriptUsage Usage, const FGuid& UsageId, FString& OutError)
{
	if (!Source || !Source->NodeGraph)
	{
		OutError = TEXT("Emitter graph source is missing");
		return false;
	}

	UNiagaraGraph* Graph = Source->NodeGraph;
	Graph->Modify();

	UNiagaraNodeOutput* OutputNode = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UNiagaraNodeOutput* OutputCandidate = Cast<UNiagaraNodeOutput>(Node);
		if (OutputCandidate && OutputCandidate->GetUsage() == Usage && OutputCandidate->GetUsageId() == UsageId)
		{
			OutputNode = OutputCandidate;
			break;
		}
	}
	UEdGraphPin* OutputNodeInputPin = OutputNode ? NiagaraToolHelpers::GetParameterMapInputPin(OutputNode) : nullptr;
	if (OutputNode && !OutputNodeInputPin)
	{
		Graph->RemoveNode(OutputNode);
		OutputNode = nullptr;
	}

	if (!OutputNode)
	{
		FGraphNodeCreator<UNiagaraNodeOutput> OutputNodeCreator(*Graph);
		OutputNode = OutputNodeCreator.CreateNode();
		OutputNode->SetUsage(Usage);
		OutputNode->SetUsageId(UsageId);
		OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Out")));
		OutputNodeCreator.Finalize();
		OutputNodeInputPin = NiagaraToolHelpers::GetParameterMapInputPin(OutputNode);
	}

	if (!OutputNodeInputPin)
	{
		OutError = TEXT("Failed to create output node input pin");
		return false;
	}

	FGraphNodeCreator<UNiagaraNodeInput> InputNodeCreator(*Graph);
	UNiagaraNodeInput* InputNode = InputNodeCreator.CreateNode();
	InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("InputMap"));
	InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
	InputNodeCreator.Finalize();

	UEdGraphPin* InputNodeOutputPin = NiagaraToolHelpers::GetParameterMapOutputPin(InputNode);
	if (!InputNodeOutputPin)
	{
		OutError = TEXT("Failed to create input node output pin");
		return false;
	}

	OutputNodeInputPin->BreakAllPinLinks(true);
	InputNodeOutputPin->MakeLinkTo(OutputNodeInputPin);
	return true;
}

bool FEditNiagaraTool::ParseModuleDefinition(const TSharedPtr<FJsonObject>& Obj, FModuleDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("module_path"), OutDef.ModulePath) || OutDef.ModulePath.IsEmpty())
	{
		OutError = TEXT("Module definition missing 'module_path'");
		return false;
	}

	FString StageStr;
	if (!Obj->TryGetStringField(TEXT("stage"), StageStr) || StageStr.IsEmpty())
	{
		OutError = TEXT("Module definition missing 'stage'");
		return false;
	}

	bool bValidUsage;
	OutDef.Usage = ParseScriptUsage(StageStr, bValidUsage);
	if (!bValidUsage)
	{
		OutError = FString::Printf(TEXT("Unknown stage '%s'. Valid stages: %s"), *StageStr, *GetValidStageNames());
		return false;
	}

	Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName);
	if (!IsSystemStageUsage(OutDef.Usage) && OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Module definition missing 'emitter' (required for non-system stages)");
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("stage_usage_id"), OutDef.UsageId, OutError))
	{
		return false;
	}

	// Optional fields
	Obj->TryGetStringField(TEXT("name"), OutDef.Name);
	if (Obj->HasField(TEXT("index")))
	{
		OutDef.Index = static_cast<int32>(Obj->GetNumberField(TEXT("index")));
	}

	const TSharedPtr<FJsonObject>* ParamsObj;
	if (Obj->TryGetObjectField(TEXT("parameters"), ParamsObj))
	{
		OutDef.Parameters = *ParamsObj;
	}

	return true;
}

bool FEditNiagaraTool::ParseModuleRemoval(const TSharedPtr<FJsonObject>& Obj, FModuleRemoval& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("module_name"), OutDef.ModuleName) || OutDef.ModuleName.IsEmpty())
	{
		OutError = TEXT("Module removal missing 'module_name'");
		return false;
	}

	FString StageStr;
	if (!Obj->TryGetStringField(TEXT("stage"), StageStr) || StageStr.IsEmpty())
	{
		OutError = TEXT("Module removal missing 'stage'");
		return false;
	}

	bool bValidUsage;
	OutDef.Usage = ParseScriptUsage(StageStr, bValidUsage);
	if (!bValidUsage)
	{
		OutError = FString::Printf(TEXT("Unknown stage '%s'. Valid stages: %s"), *StageStr, *GetValidStageNames());
		return false;
	}

	Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName);
	if (!IsSystemStageUsage(OutDef.Usage) && OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Module removal missing 'emitter' (required for non-system stages)");
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("stage_usage_id"), OutDef.UsageId, OutError))
	{
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseEmitterDefinition(const TSharedPtr<FJsonObject>& Obj, FEmitterDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("name"), OutDef.Name) || OutDef.Name.IsEmpty())
	{
		OutError = TEXT("Emitter definition missing 'name'");
		return false;
	}

	// Template is required
	Obj->TryGetStringField(TEXT("template_asset"), OutDef.TemplateAsset);

	return true;
}

bool FEditNiagaraTool::ParseEmitterRemoval(const TSharedPtr<FJsonObject>& Obj, FEmitterRemoval& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Emitter removal missing 'emitter'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseEmitterRename(const TSharedPtr<FJsonObject>& Obj, FEmitterRename& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Emitter rename missing 'emitter'");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), OutDef.NewName) || OutDef.NewName.IsEmpty())
	{
		OutError = TEXT("Emitter rename missing 'new_name'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseEmitterDuplicate(const TSharedPtr<FJsonObject>& Obj, FEmitterDuplicate& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Emitter duplicate missing 'emitter'");
		return false;
	}
	Obj->TryGetStringField(TEXT("new_name"), OutDef.NewName);
	return true;
}

bool FEditNiagaraTool::ParseEmitterReorder(const TSharedPtr<FJsonObject>& Obj, FEmitterReorder& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Emitter reorder missing 'emitter'");
		return false;
	}
	if (!Obj->HasField(TEXT("new_index")))
	{
		OutError = TEXT("Emitter reorder missing 'new_index'");
		return false;
	}
	OutDef.NewIndex = static_cast<int32>(Obj->GetNumberField(TEXT("new_index")));
	return true;
}

bool FEditNiagaraTool::ParseSetParameterOp(const TSharedPtr<FJsonObject>& Obj, FSetParameterOp& OutOp, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("module_name"), OutOp.ModuleName) || OutOp.ModuleName.IsEmpty())
	{
		OutError = TEXT("Set parameter missing 'module_name'");
		return false;
	}

	FString StageStr;
	if (!Obj->TryGetStringField(TEXT("stage"), StageStr) || StageStr.IsEmpty())
	{
		OutError = TEXT("Set parameter missing 'stage'");
		return false;
	}

	bool bValidUsage;
	OutOp.Usage = ParseScriptUsage(StageStr, bValidUsage);
	if (!bValidUsage)
	{
		OutError = FString::Printf(TEXT("Unknown stage '%s'. Valid stages: %s"), *StageStr, *GetValidStageNames());
		return false;
	}

	Obj->TryGetStringField(TEXT("emitter"), OutOp.EmitterName);
	if (!IsSystemStageUsage(OutOp.Usage) && OutOp.EmitterName.IsEmpty())
	{
		OutError = TEXT("Set parameter missing 'emitter' (required for non-system stages)");
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("stage_usage_id"), OutOp.UsageId, OutError))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* ParamsObj;
	if (!Obj->TryGetObjectField(TEXT("parameters"), ParamsObj))
	{
		OutError = TEXT("Set parameter missing 'parameters' object");
		return false;
	}
	OutOp.Parameters = *ParamsObj;

	return true;
}

bool FEditNiagaraTool::ParseModuleMoveOp(const TSharedPtr<FJsonObject>& Obj, FModuleMoveOp& OutOp, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("module_name"), OutOp.ModuleName) || OutOp.ModuleName.IsEmpty())
	{
		OutError = TEXT("Move module missing 'module_name'");
		return false;
	}

	FString SourceStage;
	if (!Obj->TryGetStringField(TEXT("source_stage"), SourceStage) || SourceStage.IsEmpty())
	{
		OutError = TEXT("Move module missing 'source_stage'");
		return false;
	}
	bool bValidSource = false;
	OutOp.SourceUsage = ParseScriptUsage(SourceStage, bValidSource);
	if (!bValidSource)
	{
		OutError = FString::Printf(TEXT("Unknown source_stage '%s'. Valid stages: %s"), *SourceStage, *GetValidStageNames());
		return false;
	}

	FString TargetStage;
	if (!Obj->TryGetStringField(TEXT("target_stage"), TargetStage) || TargetStage.IsEmpty())
	{
		OutError = TEXT("Move module missing 'target_stage'");
		return false;
	}
	bool bValidTarget = false;
	OutOp.TargetUsage = ParseScriptUsage(TargetStage, bValidTarget);
	if (!bValidTarget)
	{
		OutError = FString::Printf(TEXT("Unknown target_stage '%s'. Valid stages: %s"), *TargetStage, *GetValidStageNames());
		return false;
	}

	Obj->TryGetStringField(TEXT("source_emitter"), OutOp.SourceEmitterName);
	Obj->TryGetStringField(TEXT("target_emitter"), OutOp.TargetEmitterName);

	if (!IsSystemStageUsage(OutOp.SourceUsage) && OutOp.SourceEmitterName.IsEmpty())
	{
		OutError = TEXT("Move module missing 'source_emitter' (required for non-system source_stage)");
		return false;
	}
	if (!IsSystemStageUsage(OutOp.TargetUsage) && OutOp.TargetEmitterName.IsEmpty())
	{
		OutError = TEXT("Move module missing 'target_emitter' (required for non-system target_stage)");
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("source_stage_usage_id"), OutOp.SourceUsageId, OutError))
	{
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("target_stage_usage_id"), OutOp.TargetUsageId, OutError))
	{
		return false;
	}

	if (Obj->HasField(TEXT("target_index")))
	{
		OutOp.TargetIndex = static_cast<int32>(Obj->GetNumberField(TEXT("target_index")));
	}

	Obj->TryGetBoolField(TEXT("force_copy"), OutOp.bForceCopy);
	return true;
}

bool FEditNiagaraTool::ParseRendererDefinition(const TSharedPtr<FJsonObject>& Obj, FRendererDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("type"), OutDef.Type) || OutDef.Type.IsEmpty())
	{
		OutError = TEXT("Renderer definition missing 'type' (sprite, mesh, ribbon, light, or renderer class name)");
		return false;
	}

	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Renderer definition missing 'emitter'");
		return false;
	}

	const TSharedPtr<FJsonObject>* PropsObj;
	if (Obj->TryGetObjectField(TEXT("properties"), PropsObj))
	{
		OutDef.Properties = *PropsObj;
	}

	const TSharedPtr<FJsonObject>* BindingsObj;
	if (Obj->TryGetObjectField(TEXT("bindings"), BindingsObj))
	{
		OutDef.Bindings = *BindingsObj;
	}

	return true;
}

bool FEditNiagaraTool::ParseRendererRemoval(const TSharedPtr<FJsonObject>& Obj, FRendererRemoval& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Renderer removal missing 'emitter'");
		return false;
	}

	if (Obj->HasField(TEXT("index")))
	{
		OutDef.Index = static_cast<int32>(Obj->GetNumberField(TEXT("index")));
	}

	Obj->TryGetStringField(TEXT("type"), OutDef.Type);

	if (OutDef.Index == INDEX_NONE && OutDef.Type.IsEmpty())
	{
		OutError = TEXT("Renderer removal requires 'index' or 'type'");
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseRendererConfiguration(const TSharedPtr<FJsonObject>& Obj, FRendererConfiguration& OutConfig, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutConfig.EmitterName) || OutConfig.EmitterName.IsEmpty())
	{
		OutError = TEXT("Renderer configuration missing 'emitter'");
		return false;
	}

	if (!Obj->HasField(TEXT("index")))
	{
		OutError = TEXT("Renderer configuration missing 'index'");
		return false;
	}
	OutConfig.Index = static_cast<int32>(Obj->GetNumberField(TEXT("index")));

	const TSharedPtr<FJsonObject>* PropsObj;
	if (Obj->TryGetObjectField(TEXT("properties"), PropsObj))
	{
		OutConfig.Properties = *PropsObj;
	}

	const TSharedPtr<FJsonObject>* BindingsObj;
	if (Obj->TryGetObjectField(TEXT("bindings"), BindingsObj))
	{
		OutConfig.Bindings = *BindingsObj;
	}

	if (!OutConfig.Properties.IsValid() && !OutConfig.Bindings.IsValid())
	{
		OutError = TEXT("Renderer configuration requires 'properties' and/or 'bindings'");
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseRendererReorder(const TSharedPtr<FJsonObject>& Obj, FRendererReorder& OutOp, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutOp.EmitterName) || OutOp.EmitterName.IsEmpty())
	{
		OutError = TEXT("Renderer reorder missing 'emitter'");
		return false;
	}
	if (!Obj->HasField(TEXT("index")))
	{
		OutError = TEXT("Renderer reorder missing 'index'");
		return false;
	}
	if (!Obj->HasField(TEXT("new_index")))
	{
		OutError = TEXT("Renderer reorder missing 'new_index'");
		return false;
	}

	OutOp.Index = static_cast<int32>(Obj->GetNumberField(TEXT("index")));
	OutOp.NewIndex = static_cast<int32>(Obj->GetNumberField(TEXT("new_index")));
	return true;
}

bool FEditNiagaraTool::ParseUserParameterDefinition(const TSharedPtr<FJsonObject>& Obj, FUserParameterDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("name"), OutDef.Name) || OutDef.Name.IsEmpty())
	{
		OutError = TEXT("User parameter definition missing 'name'");
		return false;
	}

	if (!Obj->TryGetStringField(TEXT("type"), OutDef.Type) || OutDef.Type.IsEmpty())
	{
		OutError = TEXT("User parameter definition missing 'type' (Niagara registered user-variable type name)");
		return false;
	}

	if (Obj->HasField(TEXT("default")))
	{
		OutDef.DefaultValue = Obj->TryGetField(TEXT("default"));
	}

	return true;
}

bool FEditNiagaraTool::ParseUserParameterValue(const TSharedPtr<FJsonObject>& Obj, FUserParameterValue& OutValue, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("name"), OutValue.Name) || OutValue.Name.IsEmpty())
	{
		OutError = TEXT("User parameter value missing 'name'");
		return false;
	}

	if (!Obj->HasField(TEXT("value")))
	{
		OutError = TEXT("User parameter value missing 'value'");
		return false;
	}
	OutValue.Value = Obj->TryGetField(TEXT("value"));

	return true;
}

bool FEditNiagaraTool::ParseEmitterPropertySet(const TSharedPtr<FJsonObject>& Obj, FEmitterPropertySet& OutProps, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutProps.EmitterName) || OutProps.EmitterName.IsEmpty())
	{
		OutError = TEXT("Emitter property set missing 'emitter'");
		return false;
	}

	FString SimTarget;
	if (Obj->TryGetStringField(TEXT("sim_target"), SimTarget))
	{
		OutProps.SimTarget = SimTarget;
	}

	bool bLocalSpace;
	if (Obj->TryGetBoolField(TEXT("local_space"), bLocalSpace))
	{
		OutProps.bLocalSpace = bLocalSpace;
	}

	bool bDeterminism;
	if (Obj->TryGetBoolField(TEXT("determinism"), bDeterminism))
	{
		OutProps.bDeterminism = bDeterminism;
	}

	if (Obj->HasField(TEXT("random_seed")))
	{
		OutProps.RandomSeed = static_cast<int32>(Obj->GetNumberField(TEXT("random_seed")));
	}

	return true;
}

bool FEditNiagaraTool::ParseEventHandlerDefinition(const TSharedPtr<FJsonObject>& Obj, FEventHandlerDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Event handler definition missing 'emitter'");
		return false;
	}

	if (!Obj->TryGetStringField(TEXT("source_event_name"), OutDef.SourceEventName) || OutDef.SourceEventName.IsEmpty())
	{
		OutError = TEXT("Event handler definition missing 'source_event_name'");
		return false;
	}

	Obj->TryGetStringField(TEXT("source_emitter"), OutDef.SourceEmitterName);

	if (Obj->HasField(TEXT("execution_mode")))
	{
		OutDef.ExecutionMode = static_cast<int32>(Obj->GetNumberField(TEXT("execution_mode")));
	}
	if (Obj->HasField(TEXT("spawn_number")))
	{
		OutDef.SpawnNumber = static_cast<int32>(Obj->GetNumberField(TEXT("spawn_number")));
	}
	if (Obj->HasField(TEXT("max_events_per_frame")))
	{
		OutDef.MaxEventsPerFrame = static_cast<int32>(Obj->GetNumberField(TEXT("max_events_per_frame")));
	}
	if (Obj->HasField(TEXT("min_spawn_number")))
	{
		OutDef.MinSpawnNumber = static_cast<int32>(Obj->GetNumberField(TEXT("min_spawn_number")));
	}

	bool bTemp = false;
	if (Obj->TryGetBoolField(TEXT("random_spawn_number"), bTemp))
	{
		OutDef.bRandomSpawnNumber = bTemp;
	}
	if (Obj->TryGetBoolField(TEXT("update_attribute_initial_values"), bTemp))
	{
		OutDef.bUpdateAttributeInitialValues = bTemp;
	}

	return true;
}

bool FEditNiagaraTool::ParseEventHandlerRemoval(const TSharedPtr<FJsonObject>& Obj, FEventHandlerRemoval& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Event handler removal missing 'emitter'");
		return false;
	}

	FString UsageIdString;
	if (!Obj->TryGetStringField(TEXT("stage_usage_id"), UsageIdString) || UsageIdString.IsEmpty())
	{
		OutError = TEXT("Event handler removal missing 'stage_usage_id'");
		return false;
	}
	if (!FGuid::Parse(UsageIdString, OutDef.UsageId))
	{
		OutError = FString::Printf(TEXT("Invalid stage_usage_id '%s' (expected GUID format)"), *UsageIdString);
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseEventHandlerUpdate(const TSharedPtr<FJsonObject>& Obj, FEventHandlerUpdate& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Event handler edit missing 'emitter'");
		return false;
	}

	FString UsageIdString;
	if (!Obj->TryGetStringField(TEXT("stage_usage_id"), UsageIdString) || UsageIdString.IsEmpty())
	{
		OutError = TEXT("Event handler edit missing 'stage_usage_id'");
		return false;
	}
	if (!FGuid::Parse(UsageIdString, OutDef.UsageId))
	{
		OutError = FString::Printf(TEXT("Invalid stage_usage_id '%s' (expected GUID format)"), *UsageIdString);
		return false;
	}

	Obj->TryGetStringField(TEXT("source_event_name"), OutDef.SourceEventName);
	Obj->TryGetStringField(TEXT("source_emitter"), OutDef.SourceEmitterName);

	if (Obj->HasField(TEXT("execution_mode")))
	{
		OutDef.ExecutionMode = static_cast<int32>(Obj->GetNumberField(TEXT("execution_mode")));
	}
	if (Obj->HasField(TEXT("spawn_number")))
	{
		OutDef.SpawnNumber = static_cast<int32>(Obj->GetNumberField(TEXT("spawn_number")));
	}
	if (Obj->HasField(TEXT("max_events_per_frame")))
	{
		OutDef.MaxEventsPerFrame = static_cast<int32>(Obj->GetNumberField(TEXT("max_events_per_frame")));
	}
	if (Obj->HasField(TEXT("min_spawn_number")))
	{
		OutDef.MinSpawnNumber = static_cast<int32>(Obj->GetNumberField(TEXT("min_spawn_number")));
	}

	bool bTemp = false;
	if (Obj->TryGetBoolField(TEXT("random_spawn_number"), bTemp))
	{
		OutDef.bRandomSpawnNumber = bTemp;
	}
	if (Obj->TryGetBoolField(TEXT("update_attribute_initial_values"), bTemp))
	{
		OutDef.bUpdateAttributeInitialValues = bTemp;
	}

	return true;
}

bool FEditNiagaraTool::ParseSimulationStageDefinition(const TSharedPtr<FJsonObject>& Obj, FSimulationStageDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Simulation stage definition missing 'emitter'");
		return false;
	}

	Obj->TryGetStringField(TEXT("stage_class"), OutDef.StageClass);
	Obj->TryGetStringField(TEXT("name"), OutDef.Name);

	if (Obj->HasField(TEXT("index")))
	{
		OutDef.Index = static_cast<int32>(Obj->GetNumberField(TEXT("index")));
	}

	bool bEnabled = true;
	if (Obj->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		OutDef.bEnabled = bEnabled;
	}

	return true;
}

bool FEditNiagaraTool::ParseSimulationStageRemoval(const TSharedPtr<FJsonObject>& Obj, FSimulationStageRemoval& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Simulation stage removal missing 'emitter'");
		return false;
	}

	FString UsageIdString;
	if (!Obj->TryGetStringField(TEXT("stage_usage_id"), UsageIdString) || UsageIdString.IsEmpty())
	{
		OutError = TEXT("Simulation stage removal missing 'stage_usage_id'");
		return false;
	}
	if (!FGuid::Parse(UsageIdString, OutDef.UsageId))
	{
		OutError = FString::Printf(TEXT("Invalid stage_usage_id '%s' (expected GUID format)"), *UsageIdString);
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseSimulationStageUpdate(const TSharedPtr<FJsonObject>& Obj, FSimulationStageUpdate& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Simulation stage edit missing 'emitter'");
		return false;
	}

	FString UsageIdString;
	if (!Obj->TryGetStringField(TEXT("stage_usage_id"), UsageIdString) || UsageIdString.IsEmpty())
	{
		OutError = TEXT("Simulation stage edit missing 'stage_usage_id'");
		return false;
	}
	if (!FGuid::Parse(UsageIdString, OutDef.UsageId))
	{
		OutError = FString::Printf(TEXT("Invalid stage_usage_id '%s' (expected GUID format)"), *UsageIdString);
		return false;
	}

	Obj->TryGetStringField(TEXT("name"), OutDef.Name);

	bool bEnabled = true;
	if (Obj->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		OutDef.bEnabled = bEnabled;
	}

	return true;
}

bool FEditNiagaraTool::ParseSimulationStageReorder(const TSharedPtr<FJsonObject>& Obj, FSimulationStageReorder& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("Simulation stage reorder missing 'emitter'");
		return false;
	}

	FString UsageIdString;
	if (!Obj->TryGetStringField(TEXT("stage_usage_id"), UsageIdString) || UsageIdString.IsEmpty())
	{
		OutError = TEXT("Simulation stage reorder missing 'stage_usage_id'");
		return false;
	}
	if (!FGuid::Parse(UsageIdString, OutDef.UsageId))
	{
		OutError = FString::Printf(TEXT("Invalid stage_usage_id '%s' (expected GUID format)"), *UsageIdString);
		return false;
	}

	if (!Obj->HasField(TEXT("new_index")))
	{
		OutError = TEXT("Simulation stage reorder missing 'new_index'");
		return false;
	}
	OutDef.NewIndex = static_cast<int32>(Obj->GetNumberField(TEXT("new_index")));

	return true;
}

bool FEditNiagaraTool::ParseModuleEnableOp(const TSharedPtr<FJsonObject>& Obj, FModuleEnableOp& OutOp, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("module_name"), OutOp.ModuleName) || OutOp.ModuleName.IsEmpty())
	{
		OutError = TEXT("Module enable missing 'module_name'");
		return false;
	}

	FString StageStr;
	if (!Obj->TryGetStringField(TEXT("stage"), StageStr) || StageStr.IsEmpty())
	{
		OutError = TEXT("Module enable missing 'stage'");
		return false;
	}

	bool bValidUsage;
	OutOp.Usage = ParseScriptUsage(StageStr, bValidUsage);
	if (!bValidUsage)
	{
		OutError = FString::Printf(TEXT("Unknown stage '%s'. Valid stages: %s"), *StageStr, *GetValidStageNames());
		return false;
	}

	Obj->TryGetStringField(TEXT("emitter"), OutOp.EmitterName);
	if (!IsSystemStageUsage(OutOp.Usage) && OutOp.EmitterName.IsEmpty())
	{
		OutError = TEXT("Module enable missing 'emitter' (required for non-system stages)");
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("stage_usage_id"), OutOp.UsageId, OutError))
	{
		return false;
	}

	if (!Obj->TryGetBoolField(TEXT("enabled"), OutOp.bEnabled))
	{
		OutError = TEXT("Module enable missing 'enabled' (true/false)");
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseSystemPropertySet(const TSharedPtr<FJsonObject>& Obj, FSystemPropertySet& OutProps, FString& OutError)
{
	if (Obj->HasField(TEXT("warmup_time")))
	{
		OutProps.WarmupTime = static_cast<float>(Obj->GetNumberField(TEXT("warmup_time")));
	}

	bool bDeterminism;
	if (Obj->TryGetBoolField(TEXT("determinism"), bDeterminism))
	{
		OutProps.bDeterminism = bDeterminism;
	}

	if (Obj->HasField(TEXT("random_seed")))
	{
		OutProps.RandomSeed = static_cast<int32>(Obj->GetNumberField(TEXT("random_seed")));
	}

	return true;
}

bool FEditNiagaraTool::ParseReflectedPropertyTarget(const TSharedPtr<FJsonObject>& Obj, FReflectedPropertyTarget& OutDef, FString& OutError)
{
	if (!Obj.IsValid())
	{
		OutError = TEXT("Invalid reflected property target object");
		return false;
	}

	if (!Obj->TryGetStringField(TEXT("target"), OutDef.Target) || OutDef.Target.IsEmpty())
	{
		OutError = TEXT("Reflected property target requires 'target'");
		return false;
	}
	OutDef.Target = OutDef.Target.ToLower();

	Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName);

	double RendererIndex = 0.0;
	if (Obj->TryGetNumberField(TEXT("renderer_index"), RendererIndex))
	{
		OutDef.RendererIndex = (int32)RendererIndex;
	}

	FString UsageIdStr;
	if (Obj->TryGetStringField(TEXT("stage_usage_id"), UsageIdStr) && !UsageIdStr.IsEmpty())
	{
		FGuid::Parse(UsageIdStr, OutDef.UsageId);
	}

	const TSharedPtr<FJsonObject>* PropertiesObj = nullptr;
	if (Obj->TryGetObjectField(TEXT("properties"), PropertiesObj) && PropertiesObj)
	{
		OutDef.Properties = *PropertiesObj;
	}

	if ((OutDef.Target == TEXT("emitter") || OutDef.Target == TEXT("renderer") || OutDef.Target == TEXT("simulation_stage")) && OutDef.EmitterName.IsEmpty())
	{
		OutError = FString::Printf(TEXT("target '%s' requires 'emitter'"), *OutDef.Target);
		return false;
	}

	if (OutDef.Target == TEXT("renderer") && OutDef.RendererIndex == INDEX_NONE)
	{
		OutError = TEXT("target 'renderer' requires 'renderer_index'");
		return false;
	}

	if (OutDef.Target == TEXT("simulation_stage") && !OutDef.UsageId.IsValid())
	{
		OutError = TEXT("target 'simulation_stage' requires valid 'stage_usage_id'");
		return false;
	}

	if (OutDef.Target != TEXT("system") &&
		OutDef.Target != TEXT("emitter") &&
		OutDef.Target != TEXT("renderer") &&
		OutDef.Target != TEXT("simulation_stage") &&
		OutDef.Target != TEXT("baker_settings"))
	{
		OutError = FString::Printf(TEXT("Unsupported reflected target '%s'. Supported: system, emitter, renderer, simulation_stage, baker_settings"), *OutDef.Target);
		return false;
	}

	return true;
}

bool FEditNiagaraTool::ParseModuleVersionSet(const TSharedPtr<FJsonObject>& Obj, FModuleVersionSet& OutDef, FString& OutError)
{
	if (!Obj.IsValid())
	{
		OutError = TEXT("Invalid module version object");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("module_name"), OutDef.ModuleName) || OutDef.ModuleName.IsEmpty())
	{
		OutError = TEXT("set_module_script_version missing 'module_name'");
		return false;
	}

	FString StageStr;
	if (!Obj->TryGetStringField(TEXT("stage"), StageStr) || StageStr.IsEmpty())
	{
		OutError = TEXT("set_module_script_version missing 'stage'");
		return false;
	}
	bool bValidUsage = false;
	OutDef.Usage = ParseScriptUsage(StageStr, bValidUsage);
	if (!bValidUsage)
	{
		OutError = FString::Printf(TEXT("Unknown stage '%s'. Valid stages: %s"), *StageStr, *GetValidStageNames());
		return false;
	}
	Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName);
	if (!IsSystemStageUsage(OutDef.Usage) && OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("set_module_script_version missing 'emitter' for non-system stages");
		return false;
	}
	if (!ParseOptionalGuidField(Obj, TEXT("stage_usage_id"), OutDef.UsageId, OutError))
	{
		return false;
	}

	FString VersionGuidStr;
	if (!Obj->TryGetStringField(TEXT("version_guid"), VersionGuidStr) || !FGuid::Parse(VersionGuidStr, OutDef.VersionGuid) || !OutDef.VersionGuid.IsValid())
	{
		OutError = TEXT("set_module_script_version requires valid 'version_guid'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseEmitterVersionSet(const TSharedPtr<FJsonObject>& Obj, FEmitterVersionSet& OutDef, FString& OutError)
{
	if (!Obj.IsValid())
	{
		OutError = TEXT("Invalid emitter version object");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("emitter"), OutDef.EmitterName) || OutDef.EmitterName.IsEmpty())
	{
		OutError = TEXT("set_emitter_version missing 'emitter'");
		return false;
	}
	FString VersionGuidStr;
	if (!Obj->TryGetStringField(TEXT("version_guid"), VersionGuidStr) || !FGuid::Parse(VersionGuidStr, OutDef.VersionGuid) || !OutDef.VersionGuid.IsValid())
	{
		OutError = TEXT("set_emitter_version requires valid 'version_guid'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseParameterDefinitionsOp(const TSharedPtr<FJsonObject>& Obj, FParameterDefinitionsOp& OutDef, FString& OutError)
{
	if (!Obj.IsValid())
	{
		OutError = TEXT("Invalid parameter definitions object");
		return false;
	}

	Obj->TryGetStringField(TEXT("asset_path"), OutDef.AssetPath);

	FString IdStr;
	if (Obj->TryGetStringField(TEXT("definitions_id"), IdStr) && !IdStr.IsEmpty())
	{
		FGuid::Parse(IdStr, OutDef.DefinitionsId);
	}

	if (OutDef.AssetPath.IsEmpty() && !OutDef.DefinitionsId.IsValid())
	{
		OutError = TEXT("Parameter definitions op requires 'asset_path' or valid 'definitions_id'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseScratchPadScriptDefinition(const TSharedPtr<FJsonObject>& Obj, FScratchPadScriptDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("name"), OutDef.Name) || OutDef.Name.IsEmpty())
	{
		OutError = TEXT("Scratch pad creation missing 'name'");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("script_type"), OutDef.ScriptType) || OutDef.ScriptType.IsEmpty())
	{
		OutError = TEXT("Scratch pad creation missing 'script_type' (module|dynamic_input)");
		return false;
	}

	Obj->TryGetStringField(TEXT("duplicate_from"), OutDef.DuplicateFrom);
	Obj->TryGetStringField(TEXT("target_stage"), OutDef.TargetStage);
	Obj->TryGetStringField(TEXT("emitter"), OutDef.TargetEmitterName);
	Obj->TryGetStringField(TEXT("output_type"), OutDef.OutputType);
	return true;
}

bool FEditNiagaraTool::ParseScratchPadScriptDeletion(const TSharedPtr<FJsonObject>& Obj, FScratchPadScriptDeletion& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("name"), OutDef.Name) || OutDef.Name.IsEmpty())
	{
		OutError = TEXT("Scratch pad deletion missing 'name'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseScratchPadScriptRename(const TSharedPtr<FJsonObject>& Obj, FScratchPadScriptRename& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("name"), OutDef.Name) || OutDef.Name.IsEmpty())
	{
		OutError = TEXT("Scratch pad rename missing 'name'");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("new_name"), OutDef.NewName) || OutDef.NewName.IsEmpty())
	{
		OutError = TEXT("Scratch pad rename missing 'new_name'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseScratchModuleDefinition(const TSharedPtr<FJsonObject>& Obj, FScratchModuleDefinition& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("scratch_script"), OutDef.ScratchScriptName) || OutDef.ScratchScriptName.IsEmpty())
	{
		OutError = TEXT("Scratch module add missing 'scratch_script'");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("module_path"), OutDef.ModulePath) || OutDef.ModulePath.IsEmpty())
	{
		OutError = TEXT("Scratch module add missing 'module_path'");
		return false;
	}

	Obj->TryGetStringField(TEXT("name"), OutDef.ModuleName);
	if (Obj->HasField(TEXT("index")))
	{
		OutDef.Index = static_cast<int32>(Obj->GetNumberField(TEXT("index")));
	}

	const TSharedPtr<FJsonObject>* ParamsObj;
	if (Obj->TryGetObjectField(TEXT("parameters"), ParamsObj))
	{
		OutDef.Parameters = *ParamsObj;
	}

	return true;
}

bool FEditNiagaraTool::ParseScratchModuleRemoval(const TSharedPtr<FJsonObject>& Obj, FScratchModuleRemoval& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("scratch_script"), OutDef.ScratchScriptName) || OutDef.ScratchScriptName.IsEmpty())
	{
		OutError = TEXT("Scratch module remove missing 'scratch_script'");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("module_name"), OutDef.ModuleName) || OutDef.ModuleName.IsEmpty())
	{
		OutError = TEXT("Scratch module remove missing 'module_name'");
		return false;
	}
	return true;
}

bool FEditNiagaraTool::ParseScratchModuleSetParameters(const TSharedPtr<FJsonObject>& Obj, FScratchModuleSetParameters& OutDef, FString& OutError)
{
	if (!Obj->TryGetStringField(TEXT("scratch_script"), OutDef.ScratchScriptName) || OutDef.ScratchScriptName.IsEmpty())
	{
		OutError = TEXT("Scratch module set missing 'scratch_script'");
		return false;
	}
	if (!Obj->TryGetStringField(TEXT("module_name"), OutDef.ModuleName) || OutDef.ModuleName.IsEmpty())
	{
		OutError = TEXT("Scratch module set missing 'module_name'");
		return false;
	}

	const TSharedPtr<FJsonObject>* ParamsObj;
	if (!Obj->TryGetObjectField(TEXT("parameters"), ParamsObj))
	{
		OutError = TEXT("Scratch module set missing 'parameters' object");
		return false;
	}
	OutDef.Parameters = *ParamsObj;
	return true;
}

// ========== Niagara Operations ==========

int32 FEditNiagaraTool::FindEmitterIndexByName(UNiagaraSystem* System, const FString& EmitterName)
{
	if (!System)
	{
		return INDEX_NONE;
	}

	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	for (int32 i = 0; i < EmitterHandles.Num(); ++i)
	{
		if (EmitterHandles[i].GetName().ToString().Equals(EmitterName, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}

	return INDEX_NONE;
}

UNiagaraScript* FEditNiagaraTool::FindScratchPadScriptByName(UNiagaraSystem* System, const FString& ScriptName) const
{
	if (!System || ScriptName.IsEmpty())
	{
		return nullptr;
	}

	for (UNiagaraScript* Script : System->ScratchPadScripts)
	{
		if (!Script)
		{
			continue;
		}

		const bool bMatchesObjectName = Script->GetName().Equals(ScriptName, ESearchCase::IgnoreCase);
		const bool bMatchesPath = Script->GetPathName().Equals(ScriptName, ESearchCase::IgnoreCase);
		if (bMatchesObjectName || bMatchesPath)
		{
			return Script;
		}
	}

	return nullptr;
}

UNiagaraNodeOutput* FEditNiagaraTool::GetOutputNodeForUsage(UNiagaraSystem* System, int32 EmitterIndex, ENiagaraScriptUsage Usage, const FGuid& UsageId)
{
	if (!System)
	{
		return nullptr;
	}

	if (IsSystemStageUsage(Usage))
	{
		UNiagaraScript* SystemScript = (Usage == ENiagaraScriptUsage::SystemSpawnScript)
			? System->GetSystemSpawnScript()
			: System->GetSystemUpdateScript();
		return SystemScript ? FNiagaraEditorUtilities::GetScriptOutputNode(*SystemScript) : nullptr;
	}

	if (EmitterIndex == INDEX_NONE)
	{
		return nullptr;
	}

	const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
	if (!EmitterHandles.IsValidIndex(EmitterIndex))
	{
		return nullptr;
	}

	const FNiagaraEmitterHandle& Handle = EmitterHandles[EmitterIndex];
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!EmitterData)
	{
		return nullptr;
	}

	// Get the appropriate script based on usage
	const FGuid ResolvedUsageId = UsageId.IsValid() ? UsageId : FGuid();
	UNiagaraScript* Script = EmitterData->GetScript(Usage, ResolvedUsageId);
	if (!Script)
	{
		return nullptr;
	}

	return FNiagaraEditorUtilities::GetScriptOutputNode(*Script);
}

bool FEditNiagaraTool::ModuleSupportsUsage(UNiagaraScript* ModuleScript, ENiagaraScriptUsage Usage) const
{
	if (!ModuleScript)
	{
		return false;
	}

	// Get the module's usage bitmask from its script data
	const FVersionedNiagaraScriptData* ScriptData = ModuleScript->GetLatestScriptData();
	if (!ScriptData)
	{
		return false;
	}

	// Use UNiagaraScript's static method to check if usage is supported
	return UNiagaraScript::IsSupportedUsageContextForBitmask(ScriptData->ModuleUsageBitmask, Usage, false);
}

TArray<ENiagaraScriptUsage> FEditNiagaraTool::GetModuleSupportedUsages(UNiagaraScript* ModuleScript) const
{
	TArray<ENiagaraScriptUsage> Usages;
	if (!ModuleScript)
	{
		return Usages;
	}

	const FVersionedNiagaraScriptData* ScriptData = ModuleScript->GetLatestScriptData();
	if (!ScriptData)
	{
		return Usages;
	}

	return UNiagaraScript::GetSupportedUsageContextsForBitmask(ScriptData->ModuleUsageBitmask, false);
}

UNiagaraNodeFunctionCall* FEditNiagaraTool::FindModuleByName(UNiagaraNodeOutput* OutputNode, const FString& ModuleName)
{
	if (!OutputNode)
	{
		return nullptr;
	}

	// Walk the parameter map chain backward from the output node to collect
	// module function calls in stack execution order. This is more correct than
	// iterating Graph->Nodes which includes non-module nodes and has no ordering.
	// NOTE: Parameter map pins use PinCategoryType (not PinCategoryMisc) —
	// must use PinToTypeDefinition == GetParameterMapDef() to identify them.
	TArray<UNiagaraNodeFunctionCall*> OrderedModules;
	UEdGraphNode* CurrentNode = OutputNode;
	while (CurrentNode)
	{
		UEdGraphPin* InputMapPin = NiagaraToolHelpers::GetParameterMapInputPin(CurrentNode);

		if (!InputMapPin || InputMapPin->LinkedTo.Num() == 0)
			break;

		CurrentNode = InputMapPin->LinkedTo[0]->GetOwningNode();
		if (UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(CurrentNode))
		{
			OrderedModules.Insert(FuncNode, 0);
		}
	}

	// Normalize search name (remove spaces, lowercase)
	FString NormalizedSearch = ModuleName.ToLower().Replace(TEXT(" "), TEXT(""));

	UNiagaraNodeFunctionCall* BestMatch = nullptr;
	int32 BestScore = 0;

	for (UNiagaraNodeFunctionCall* FuncNode : OrderedModules)
	{
		FString NodeName = FuncNode->GetFunctionName();
		FString NormalizedNode = NodeName.ToLower().Replace(TEXT(" "), TEXT(""));

		// Exact match
		if (NormalizedNode.Equals(NormalizedSearch))
		{
			return FuncNode;
		}

		// Partial match - check if search term is contained in node name
		if (NormalizedNode.Contains(NormalizedSearch))
		{
			int32 Score = NormalizedSearch.Len();
			if (Score > BestScore)
			{
				BestScore = Score;
				BestMatch = FuncNode;
			}
		}

		// Reverse partial match
		if (NormalizedSearch.Contains(NormalizedNode))
		{
			int32 Score = NormalizedNode.Len();
			if (Score > BestScore)
			{
				BestScore = Score;
				BestMatch = FuncNode;
			}
		}
	}

	return BestMatch;
}

TArray<FString> FEditNiagaraTool::ListModulesInStack(UNiagaraNodeOutput* OutputNode) const
{
	TArray<FString> ModuleNames;
	if (!OutputNode)
	{
		return ModuleNames;
	}

	// Walk the parameter map chain backward from the output node to collect
	// module names in stack execution order.
	// NOTE: Parameter map pins use PinCategoryType (not PinCategoryMisc) —
	// must use PinToTypeDefinition == GetParameterMapDef() to identify them.
	UEdGraphNode* CurrentNode = OutputNode;
	TArray<FString> ReversedNames;
	while (CurrentNode)
	{
		UEdGraphPin* InputMapPin = NiagaraToolHelpers::GetParameterMapInputPin(CurrentNode);

		if (!InputMapPin || InputMapPin->LinkedTo.Num() == 0)
			break;

		CurrentNode = InputMapPin->LinkedTo[0]->GetOwningNode();
		if (UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(CurrentNode))
		{
			ReversedNames.Insert(FuncNode->GetFunctionName(), 0);
		}
	}
	ModuleNames = MoveTemp(ReversedNames);

	return ModuleNames;
}

FString FEditNiagaraTool::AddModule(UNiagaraSystem* System, const FModuleDefinition& ModDef)
{
	const bool bSystemUsage = IsSystemStageUsage(ModDef.Usage);
	int32 EmitterIndex = INDEX_NONE;
	if (!bSystemUsage)
	{
		EmitterIndex = FindEmitterIndexByName(System, ModDef.EmitterName);
		if (EmitterIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *ModDef.EmitterName);
		}
	}

	// Get output node using the exported utility function
	UNiagaraNodeOutput* OutputNode = GetOutputNodeForUsage(System, EmitterIndex, ModDef.Usage, ModDef.UsageId);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not get output node for %s in '%s'. "
				"The emitter may need to be created from a valid template that has an initialized graph structure."),
			*UsageToString(ModDef.Usage), bSystemUsage ? TEXT("system") : *ModDef.EmitterName);
	}

	// Load the module script
	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *ModDef.ModulePath);
	if (!ModuleScript)
	{
		return FString::Printf(TEXT("ERROR: Module script not found: %s"), *ModDef.ModulePath);
	}

	// Check if module supports this usage
	if (!ModuleSupportsUsage(ModuleScript, ModDef.Usage))
	{
		TArray<ENiagaraScriptUsage> Supported = GetModuleSupportedUsages(ModuleScript);
		FString SupportedStr;
		for (ENiagaraScriptUsage Usage : Supported)
		{
			if (!SupportedStr.IsEmpty()) SupportedStr += TEXT(", ");
			SupportedStr += UsageToString(Usage);
		}
		return FString::Printf(TEXT("ERROR: Module '%s' does not support %s. Supported stages: %s"),
			*FPaths::GetBaseFilename(ModDef.ModulePath), *UsageToString(ModDef.Usage), *SupportedStr);
	}

	// Add the module to the stack
	FString SuggestedName = ModDef.Name.IsEmpty() ? ModuleScript->GetName() : ModDef.Name;
	UNiagaraNodeFunctionCall* NewModule = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		ModuleScript, *OutputNode, ModDef.Index, SuggestedName);

	if (!NewModule)
	{
		return FString::Printf(TEXT("ERROR: Failed to add module '%s' to stack. "
			"The emitter's graph may not be properly initialized. Try using an emitter from a built-in template."),
			*ModDef.ModulePath);
	}

	// Set parameters if provided
	FString ParamResult;
	if (ModDef.Parameters.IsValid() && ModDef.Parameters->Values.Num() > 0)
	{
		FSetParameterOp SetOp;
		SetOp.ModuleName = NewModule->GetFunctionName();
		SetOp.EmitterName = ModDef.EmitterName;
		SetOp.Usage = ModDef.Usage;
		SetOp.Parameters = ModDef.Parameters;

		ParamResult = SetModuleParameters(System, SetOp);
	}

	FString Result = FString::Printf(TEXT("Added '%s' to %s/%s"),
		*NewModule->GetFunctionName(), bSystemUsage ? TEXT("System") : *ModDef.EmitterName, *UsageToString(ModDef.Usage));

	if (!ParamResult.IsEmpty())
	{
		if (ParamResult.Contains(TEXT("ERROR:")))
		{
			// Parameter setting failed — still report module was added, but include error clearly
			Result += FString::Printf(TEXT("\n  Parameter error: %s"), *ParamResult);
		}
		else
		{
			Result += FString::Printf(TEXT(" (%s)"), *ParamResult);
		}
	}

	TArray<FNiagaraVariable> AvailableInputs = DiscoverModuleInputs(NewModule);
	if (AvailableInputs.Num() > 0 && (!ModDef.Parameters.IsValid() || ModDef.Parameters->Values.Num() == 0))
	{
		Result += TEXT("\nAvailable inputs:\n");
		Result += FormatAvailableInputs(AvailableInputs);
	}

	return Result;
}

FString FEditNiagaraTool::RemoveModule(UNiagaraSystem* System, const FModuleRemoval& RemovalDef)
{
	const bool bSystemUsage = IsSystemStageUsage(RemovalDef.Usage);
	int32 EmitterIndex = INDEX_NONE;
	if (!bSystemUsage)
	{
		EmitterIndex = FindEmitterIndexByName(System, RemovalDef.EmitterName);
		if (EmitterIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *RemovalDef.EmitterName);
		}
	}

	UNiagaraNodeOutput* OutputNode = GetOutputNodeForUsage(System, EmitterIndex, RemovalDef.Usage, RemovalDef.UsageId);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not get output node for %s in '%s'"),
			*UsageToString(RemovalDef.Usage), bSystemUsage ? TEXT("system") : *RemovalDef.EmitterName);
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(OutputNode, RemovalDef.ModuleName);
	if (!ModuleNode)
	{
		TArray<FString> AvailableModules = ListModulesInStack(OutputNode);
		FString ModuleList = AvailableModules.Num() > 0 ? FString::Join(AvailableModules, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Module '%s' not found in %s/%s. Available modules: %s"),
			*RemovalDef.ModuleName, bSystemUsage ? TEXT("System") : *RemovalDef.EmitterName, *UsageToString(RemovalDef.Usage), *ModuleList);
	}

	FString ActualModuleName = ModuleNode->GetFunctionName();

	UNiagaraGraph* Graph = ModuleNode->GetNiagaraGraph();
	if (!Graph)
	{
		return TEXT("ERROR: Could not get Niagara graph for module removal");
	}

	// Mark the owning script for undo so rapid iteration parameter values are retained
	if (!bSystemUsage)
	{
		const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
		FVersionedNiagaraEmitterData* EmitterData = EmitterHandles[EmitterIndex].GetEmitterData();
		if (EmitterData)
		{
				UNiagaraScript* OwningScript = EmitterData->GetScript(RemovalDef.Usage, RemovalDef.UsageId.IsValid() ? RemovalDef.UsageId : FGuid());
			if (OwningScript)
			{
				OwningScript->Modify();
			}
		}
	}
	else
	{
		UNiagaraScript* OwningScript = (RemovalDef.Usage == ENiagaraScriptUsage::SystemSpawnScript)
			? System->GetSystemSpawnScript()
			: System->GetSystemUpdateScript();
		if (OwningScript)
		{
			OwningScript->Modify();
		}
	}

	Graph->Modify();

	// Step 1: Save parameter map chain connections before disconnecting.
	// Parameter map pins use PinCategoryType — identified via PinToTypeDefinition.
	UEdGraphPin* PrevOutputPin = nullptr;
	UEdGraphPin* NextInputPin = nullptr;

	UEdGraphPin* ModuleInputMap = NiagaraToolHelpers::GetParameterMapInputPin(ModuleNode);
	if (ModuleInputMap && ModuleInputMap->LinkedTo.Num() > 0)
	{
		PrevOutputPin = ModuleInputMap->LinkedTo[0];
	}
	UEdGraphPin* ModuleOutputMap = NiagaraToolHelpers::GetParameterMapOutputPin(ModuleNode);
	if (ModuleOutputMap && ModuleOutputMap->LinkedTo.Num() > 0)
	{
		NextInputPin = ModuleOutputMap->LinkedTo[0];
	}

	// Step 2: Break ONLY the parameter map chain links to disconnect the module
	// from the chain. This isolates the module's sub-graph so we can traverse
	// and remove all associated nodes without accidentally entering adjacent modules.
	if (ModuleInputMap)
	{
		ModuleInputMap->BreakAllPinLinks(true);
	}
	if (ModuleOutputMap)
	{
		ModuleOutputMap->BreakAllPinLinks(true);
	}

	// Step 3: Reconnect the parameter map chain and notify both nodes
	if (PrevOutputPin && NextInputPin)
	{
		PrevOutputPin->MakeLinkTo(NextInputPin);
		// Notify nodes so pin caches are invalidated
		if (UNiagaraNode* PrevNode = Cast<UNiagaraNode>(PrevOutputPin->GetOwningNode()))
		{
			PrevNode->PinConnectionListChanged(PrevOutputPin);
		}
		if (UNiagaraNode* NextNode = Cast<UNiagaraNode>(NextInputPin->GetOwningNode()))
		{
			NextNode->PinConnectionListChanged(NextInputPin);
		}
	}

	// Step 4: Collect ALL nodes in the module's sub-graph (dynamic inputs, input
	// override nodes, custom HLSL nodes, etc.) by traversing input pin connections.
	// The parameter map links are already broken, so this only collects the module's
	// own sub-graph and won't traverse into adjacent modules.
	TArray<UEdGraphNode*> NodesToRemove;
	TSet<UEdGraphNode*> Visited;
	NiagaraToolHelpers::CollectModuleSubGraphNodes(ModuleNode, NodesToRemove, Visited);

	// Step 5: Remove all collected nodes with proper undo support
	for (UEdGraphNode* NodeToRemove : NodesToRemove)
	{
		if (NodeToRemove)
		{
			NodeToRemove->Modify();
			Graph->RemoveNode(NodeToRemove, true);
		}
	}

	return FString::Printf(TEXT("Removed '%s' from %s/%s"),
		*ActualModuleName, bSystemUsage ? TEXT("System") : *RemovalDef.EmitterName, *UsageToString(RemovalDef.Usage));
}

FString FEditNiagaraTool::SetModuleParameters(UNiagaraSystem* System, const FSetParameterOp& SetOp)
{
	const bool bSystemUsage = IsSystemStageUsage(SetOp.Usage);
	int32 EmitterIndex = INDEX_NONE;
	const FNiagaraEmitterHandle* EmitterHandlePtr = nullptr;
	FNiagaraEmitterHandle DummyEmitterHandle;
	FVersionedNiagaraEmitterData* EmitterData = nullptr;

	if (!bSystemUsage)
	{
		EmitterIndex = FindEmitterIndexByName(System, SetOp.EmitterName);
		if (EmitterIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *SetOp.EmitterName);
		}

		const TArray<FNiagaraEmitterHandle>& EmitterHandles = System->GetEmitterHandles();
		EmitterHandlePtr = &EmitterHandles[EmitterIndex];
		EmitterData = EmitterHandlePtr->GetEmitterData();
		if (!EmitterData)
		{
			return FString::Printf(TEXT("ERROR: Could not get emitter data for '%s'"), *SetOp.EmitterName);
		}
	}
	else
	{
		EmitterHandlePtr = &DummyEmitterHandle;
	}

	UNiagaraNodeOutput* OutputNode = GetOutputNodeForUsage(System, EmitterIndex, SetOp.Usage, SetOp.UsageId);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not get output node for %s in '%s'"),
			*UsageToString(SetOp.Usage), bSystemUsage ? TEXT("system") : *SetOp.EmitterName);
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(OutputNode, SetOp.ModuleName);
	if (!ModuleNode)
	{
		TArray<FString> AvailableModules = ListModulesInStack(OutputNode);
		FString ModuleList = AvailableModules.Num() > 0 ? FString::Join(AvailableModules, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Module '%s' not found in %s/%s. Available modules: %s"),
			*SetOp.ModuleName, bSystemUsage ? TEXT("System") : *SetOp.EmitterName, *UsageToString(SetOp.Usage), *ModuleList);
	}

	UNiagaraScript* Script = nullptr;
	if (bSystemUsage)
	{
		Script = (SetOp.Usage == ENiagaraScriptUsage::SystemSpawnScript)
			? System->GetSystemSpawnScript()
			: System->GetSystemUpdateScript();
	}
	else
	{
		Script = EmitterData->GetScript(SetOp.Usage, SetOp.UsageId.IsValid() ? SetOp.UsageId : FGuid());
	}
	if (!Script)
	{
		return FString::Printf(TEXT("ERROR: Could not get script for %s"), *UsageToString(SetOp.Usage));
	}

	TArray<FNiagaraVariable> AvailableInputs = DiscoverModuleInputs(ModuleNode);

	TArray<FString> SetValues;
	TArray<FString> Errors;

	for (const auto& Param : SetOp.Parameters->Values)
	{
		FString ParamName = Param.Key;
		TSharedPtr<FJsonValue> ParamValue = Param.Value;

		FNiagaraVariable* MatchedInput = nullptr;
		for (FNiagaraVariable& Input : AvailableInputs)
		{
			FString InputName = Input.GetName().ToString();
			FString ShortName = InputName;
			if (ShortName.Contains(TEXT(".")))
			{
				ShortName = ShortName.RightChop(ShortName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
			}

			if (InputName.Equals(ParamName, ESearchCase::IgnoreCase) ||
				ShortName.Equals(ParamName, ESearchCase::IgnoreCase) ||
				InputName.Equals(TEXT("Module.") + ParamName, ESearchCase::IgnoreCase))
			{
				MatchedInput = &Input;
				break;
			}
		}

		if (!MatchedInput)
		{
			Errors.Add(FString::Printf(TEXT("Input '%s' not found"), *ParamName));
			continue;
		}

		// Detect value mode: if ParamValue is a JSON object with a "mode" field,
		// dispatch to the appropriate advanced value mode handler.
		// Plain values (number, string, bool, array, or object without "mode") use static mode.
		FString SetError;
		bool bSuccess = false;
		FString ResultDesc;

		const TSharedPtr<FJsonObject>* ValueObjPtr = nullptr;
		FString ModeStr;
		bool bAdvancedMode = false;

		if (ParamValue->TryGetObject(ValueObjPtr) && ValueObjPtr && (*ValueObjPtr)->TryGetStringField(TEXT("mode"), ModeStr))
		{
			bAdvancedMode = true;
			TSharedPtr<FJsonObject> ValueObj = *ValueObjPtr;
			const FString ModeLower = ModeStr.ToLower();

			if (ModeLower == TEXT("dynamic_input") || ModeLower == TEXT("dynamic"))
			{
				FString ScriptPath;
				ValueObj->TryGetStringField(TEXT("script"), ScriptPath);
				TSharedPtr<FJsonObject> ChildParamsObj;
				const TSharedPtr<FJsonObject>* ChildParamsPtr;
				if (ValueObj->TryGetObjectField(TEXT("parameters"), ChildParamsPtr))
				{
					ChildParamsObj = *ChildParamsPtr;
				}

				FString Result = SetDynamicInput(System, *EmitterHandlePtr, ModuleNode, Script,
					*MatchedInput, ScriptPath, ChildParamsObj, SetError);
				bSuccess = !Result.IsEmpty();
				ResultDesc = FString::Printf(TEXT("%s=dynamic(%s)"), *ParamName,
					*FPaths::GetBaseFilename(ScriptPath));
			}
			else if (ModeLower == TEXT("linked") || ModeLower == TEXT("link"))
			{
				FString LinkedParam;
				ValueObj->TryGetStringField(TEXT("parameter"), LinkedParam);

				FString Result = SetLinkedInput(System, *EmitterHandlePtr, ModuleNode, Script,
					*MatchedInput, LinkedParam, SetError);
				bSuccess = !Result.IsEmpty();
				ResultDesc = FString::Printf(TEXT("%s->%s"), *ParamName, *LinkedParam);
			}
			else if (ModeLower == TEXT("hlsl") || ModeLower == TEXT("custom_hlsl") || ModeLower == TEXT("expression"))
			{
				FString Code;
				ValueObj->TryGetStringField(TEXT("code"), Code);

				FString Result = SetCustomHLSLInput(System, *EmitterHandlePtr, ModuleNode, Script,
					*MatchedInput, Code, SetError);
				bSuccess = !Result.IsEmpty();
				ResultDesc = FString::Printf(TEXT("%s=hlsl(...)"), *ParamName);
			}
			else if (ModeLower == TEXT("data_interface") || ModeLower == TEXT("di"))
			{
				FString DIType;
				ValueObj->TryGetStringField(TEXT("type"), DIType);
				TSharedPtr<FJsonObject> DIPropsObj;
				const TSharedPtr<FJsonObject>* DIPropsPtr;
				if (ValueObj->TryGetObjectField(TEXT("properties"), DIPropsPtr))
				{
					DIPropsObj = *DIPropsPtr;
				}

				FString Result = SetDataInterfaceInput(System, *EmitterHandlePtr, ModuleNode, Script,
					*MatchedInput, DIType, DIPropsObj, SetError);
				bSuccess = !Result.IsEmpty();
				ResultDesc = FString::Printf(TEXT("%s=di(%s)"), *ParamName, *DIType);
			}
			else if (ModeLower == TEXT("reset") || ModeLower == TEXT("default"))
			{
				bSuccess = ResetModuleInputToDefault(System, *EmitterHandlePtr, ModuleNode, Script, *MatchedInput, SetError);
				ResultDesc = FString::Printf(TEXT("%s=reset"), *ParamName);
			}
			else
			{
				// Unknown mode — treat as error
				SetError = FString::Printf(TEXT("Unknown input mode '%s'. Valid modes: dynamic_input, linked, hlsl, reset, data_interface"), *ModeStr);
			}
		}

		if (!bAdvancedMode)
		{
			// Static value mode (backward compatible — existing behavior)
			bSuccess = SetInputValue(System, *EmitterHandlePtr, ModuleNode, Script, *MatchedInput, ParamValue, SetError);
			if (bSuccess)
			{
				FString ValueStr;
				if (ParamValue->TryGetString(ValueStr))
				{
					ResultDesc = FString::Printf(TEXT("%s=\"%s\""), *ParamName, *ValueStr);
				}
				else if (ParamValue->Type == EJson::Number)
				{
					ResultDesc = FString::Printf(TEXT("%s=%s"), *ParamName, *FString::SanitizeFloat(ParamValue->AsNumber()));
				}
				else if (ParamValue->Type == EJson::Boolean)
				{
					ResultDesc = FString::Printf(TEXT("%s=%s"), *ParamName, ParamValue->AsBool() ? TEXT("true") : TEXT("false"));
				}
				else if (ParamValue->Type == EJson::Array)
				{
					ResultDesc = FString::Printf(TEXT("%s=[...]"), *ParamName);
				}
				else
				{
					ResultDesc = FString::Printf(TEXT("%s={...}"), *ParamName);
				}
			}
		}

		if (bSuccess)
		{
			SetValues.Add(ResultDesc);
		}
		else
		{
			Errors.Add(FString::Printf(TEXT("%s: %s"), *ParamName, *SetError));
		}
	}

	if (SetValues.Num() == 0 && Errors.Num() > 0)
	{
		FString AvailableStr = FormatAvailableInputs(AvailableInputs);
		return FString::Printf(TEXT("ERROR: Failed to set parameters on '%s': %s\nAvailable inputs:\n%s"),
			*ModuleNode->GetFunctionName(), *FString::Join(Errors, TEXT(", ")), *AvailableStr);
	}

	FString Result;
	if (SetValues.Num() > 0)
	{
		Result = FString::Printf(TEXT("Set %d parameters on '%s': %s"),
			SetValues.Num(), *ModuleNode->GetFunctionName(), *FString::Join(SetValues, TEXT(", ")));
	}
	else
	{
		Result = FString::Printf(TEXT("No parameters set on '%s'"), *ModuleNode->GetFunctionName());
	}

	if (Errors.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (errors: %s)"), *FString::Join(Errors, TEXT("; ")));
	}

	return Result;
}

FString FEditNiagaraTool::AddEmitter(UNiagaraSystem* System, const FEmitterDefinition& EmitterDef)
{
	if (!EmitterDef.TemplateAsset.IsEmpty())
	{
		// Load template emitter
		UNiagaraEmitter* TemplateEmitter = LoadObject<UNiagaraEmitter>(nullptr, *EmitterDef.TemplateAsset);
		if (!TemplateEmitter)
		{
			return FString::Printf(TEXT("ERROR: Template emitter not found: %s"), *EmitterDef.TemplateAsset);
		}

		// Add emitter from template
		FGuid NewGuid = FNiagaraEditorUtilities::AddEmitterToSystem(*System, *TemplateEmitter, TemplateEmitter->GetExposedVersion().VersionGuid, true);
		if (!NewGuid.IsValid())
		{
			return FString::Printf(TEXT("ERROR: Failed to add emitter from template '%s'"), *EmitterDef.TemplateAsset);
		}

		// Find and rename the new emitter
		const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
		for (int32 i = Handles.Num() - 1; i >= 0; --i)
		{
			if (Handles[i].GetId() == NewGuid)
			{
				// Rename if name provided
				if (!EmitterDef.Name.IsEmpty())
				{
					FNiagaraEmitterHandle& Handle = const_cast<FNiagaraEmitterHandle&>(Handles[i]);
					Handle.SetName(FName(*EmitterDef.Name), *System);
				}
				return FString::Printf(TEXT("Added emitter '%s' from template '%s'"),
					*Handles[i].GetName().ToString(), *EmitterDef.TemplateAsset);
			}
		}
	}
	else
	{
		// Build list of available templates
		FString TemplateList;
		TArray<FAssetData> EmitterAssets;
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		AssetRegistryModule.Get().GetAssetsByClass(FTopLevelAssetPath(TEXT("/Script/Niagara"), TEXT("NiagaraEmitter")), EmitterAssets, true);

		// Find common templates
		TArray<FString> Templates;
		for (const FAssetData& Asset : EmitterAssets)
		{
			FString Path = Asset.GetObjectPathString();
			// Focus on Niagara template emitters
			if (Path.Contains(TEXT("/Niagara/")) || Path.Contains(TEXT("Template")) || Path.Contains(TEXT("Default")))
			{
				Templates.Add(Path);
				if (Templates.Num() >= 5) break;
			}
		}

		if (Templates.Num() > 0)
		{
			TemplateList = TEXT(" Available templates:\n");
			for (const FString& T : Templates)
			{
				TemplateList += FString::Printf(TEXT("  - %s\n"), *T);
			}
		}

		return FString::Printf(TEXT("ERROR: Creating empty emitters requires a template_asset.%s"), *TemplateList);
	}

	return TEXT("ERROR: Failed to add emitter");
}

FString FEditNiagaraTool::RemoveEmitter(UNiagaraSystem* System, const FEmitterRemoval& EmitterDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, EmitterDef.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *EmitterDef.EmitterName);
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	const FGuid HandleId = Handle.GetId();
	const FString ActualName = Handle.GetName().ToString();

	System->Modify();
	TSet<FGuid> HandleIdsToDelete;
	HandleIdsToDelete.Add(HandleId);
	System->RemoveEmitterHandlesById(HandleIdsToDelete);

	return FString::Printf(TEXT("Removed emitter '%s'"), *ActualName);
}

FString FEditNiagaraTool::RenameEmitter(UNiagaraSystem* System, const FEmitterRename& EmitterDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, EmitterDef.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *EmitterDef.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	const FString OldName = Handle.GetName().ToString();

	System->Modify();
	Handle.SetName(FName(*EmitterDef.NewName), *System);
	return FString::Printf(TEXT("Renamed emitter '%s' -> '%s'"), *OldName, *Handle.GetName().ToString());
}

FString FEditNiagaraTool::DuplicateEmitter(UNiagaraSystem* System, const FEmitterDuplicate& EmitterDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, EmitterDef.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *EmitterDef.EmitterName);
	}

	const FNiagaraEmitterHandle& SourceHandle = System->GetEmitterHandles()[EmitterIndex];
	const FString DesiredName = EmitterDef.NewName.IsEmpty()
		? FString::Printf(TEXT("%s_Copy"), *SourceHandle.GetName().ToString())
		: EmitterDef.NewName;

	System->Modify();
	FNiagaraEmitterHandle NewHandle = System->DuplicateEmitterHandle(SourceHandle, FName(*DesiredName));
	return FString::Printf(TEXT("Duplicated emitter '%s' as '%s'"), *SourceHandle.GetName().ToString(), *NewHandle.GetName().ToString());
}

FString FEditNiagaraTool::ReorderEmitter(UNiagaraSystem* System, const FEmitterReorder& EmitterDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	const int32 SourceIndex = FindEmitterIndexByName(System, EmitterDef.EmitterName);
	if (SourceIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *EmitterDef.EmitterName);
	}

	if (EmitterDef.NewIndex < 0 || EmitterDef.NewIndex >= Handles.Num())
	{
		return FString::Printf(TEXT("ERROR: new_index %d is out of bounds (0..%d)"), EmitterDef.NewIndex, Handles.Num() - 1);
	}

	if (SourceIndex == EmitterDef.NewIndex)
	{
		return FString::Printf(TEXT("Emitter '%s' already at index %d"), *EmitterDef.EmitterName, SourceIndex);
	}

	const FString Name = Handles[SourceIndex].GetName().ToString();
	System->Modify();
	const FNiagaraEmitterHandle HandleToMove = Handles[SourceIndex];
	Handles.RemoveAt(SourceIndex);
	const int32 InsertIndex = FMath::Clamp(EmitterDef.NewIndex, 0, Handles.Num());
	Handles.Insert(HandleToMove, InsertIndex);

	return FString::Printf(TEXT("Moved emitter '%s' from %d to %d"), *Name, SourceIndex, InsertIndex);
}

// ========== Dynamic Input Discovery & Value Setting ==========

TArray<FNiagaraVariable> FEditNiagaraTool::DiscoverModuleInputs(UNiagaraNodeFunctionCall* ModuleNode)
{
	TArray<FNiagaraVariable> Inputs;
	if (!ModuleNode)
	{
		return Inputs;
	}

	// Use Niagara stack utilities (same path as the editor UI) instead of directly
	// enumerating function-call pins. Module call nodes don't expose all logical inputs
	// as direct pins, so pin scanning can miss value types like float/vector/color.
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FNiagaraStackGraphUtilities::GetStackFunctionInputs(
		*ModuleNode,
		Inputs,
		FCompileConstantResolver(),
		FNiagaraStackGraphUtilities::ENiagaraGetStackFunctionInputPinsOptions::ModuleInputsOnly,
		/*bIgnoreDisabled=*/ false);
	#else
	// UE 5.5 does not reliably export this symbol for plugin linkage.
	// Fall back to explicit pin enumeration below.
	#endif

	// Defensive fallback: if traversal returns nothing (invalid graph context, etc.),
	// fall back to visible non-parameter-map input pins.
	if (Inputs.Num() == 0)
	{
		for (UEdGraphPin* Pin : ModuleNode->GetAllPins())
		{
			if (Pin->Direction == EGPD_Input && !Pin->bHidden && !NiagaraToolHelpers::IsParameterMapPin(Pin))
			{
				FNiagaraTypeDefinition TypeDef = UEdGraphSchema_Niagara::PinToTypeDefinition(Pin);
				if (TypeDef.IsValid())
				{
					Inputs.Add(FNiagaraVariable(TypeDef, Pin->GetFName()));
				}
			}
		}
	}

	// Keep a stable, deduplicated list for matching/error output.
	TSet<FString> Seen;
	TArray<FNiagaraVariable> UniqueInputs;
	UniqueInputs.Reserve(Inputs.Num());
	for (const FNiagaraVariable& Input : Inputs)
	{
		const FString Key = Input.GetName().ToString() + TEXT("|") + Input.GetType().GetName();
		if (!Seen.Contains(Key))
		{
			Seen.Add(Key);
			UniqueInputs.Add(Input);
		}
	}

	return UniqueInputs;
}

bool FEditNiagaraTool::SetPropertyFromJsonValue(FProperty* Property, void* ValuePtr, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Property || !ValuePtr || !Value.IsValid())
	{
		OutError = TEXT("Invalid property or value");
		return false;
	}

	if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			FloatProp->SetPropertyValue(ValuePtr, (float)NumValue);
			return true;
		}
	}
	else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			DoubleProp->SetPropertyValue(ValuePtr, NumValue);
			return true;
		}
	}
	else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
	{
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			IntProp->SetPropertyValue(ValuePtr, (int32)NumValue);
			return true;
		}
	}
	else if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
	{
		bool BoolValue;
		if (Value->TryGetBool(BoolValue))
		{
			BoolProp->SetPropertyValue(ValuePtr, BoolValue);
			return true;
		}
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			BoolProp->SetPropertyValue(ValuePtr, NumValue != 0.0);
			return true;
		}
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		return SetStructFromJson(StructProp->Struct, ValuePtr, Value, OutError);
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		FString StrValue;
		if (Value->TryGetString(StrValue))
		{
			UEnum* Enum = EnumProp->GetEnum();
			if (Enum)
			{
				int64 EnumValue = Enum->GetValueByNameString(StrValue);
				if (EnumValue != INDEX_NONE)
				{
					EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, EnumValue);
					return true;
				}
			}
		}
		double NumValue;
		if (Value->TryGetNumber(NumValue))
		{
			EnumProp->GetUnderlyingProperty()->SetIntPropertyValue(ValuePtr, (int64)NumValue);
			return true;
		}
	}

	FString StrValue;
	if (Value->TryGetString(StrValue))
	{
		if (Property->ImportText_Direct(*StrValue, ValuePtr, nullptr, PPF_None))
		{
			return true;
		}
	}

	OutError = FString::Printf(TEXT("Could not convert value for property '%s'"), *Property->GetName());
	return false;
}

bool FEditNiagaraTool::SetStructFromJson(UScriptStruct* Struct, void* StructMemory, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	if (!Struct || !StructMemory || !JsonValue.IsValid())
	{
		OutError = TEXT("Invalid struct or value");
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ArrayValue;
	if (JsonValue->TryGetArray(ArrayValue))
	{
		int32 Index = 0;
		for (TFieldIterator<FProperty> PropIt(Struct); PropIt && Index < ArrayValue->Num(); ++PropIt, ++Index)
		{
			FProperty* Property = *PropIt;
			void* PropPtr = Property->ContainerPtrToValuePtr<void>(StructMemory);
			FString PropError;
			if (!SetPropertyFromJsonValue(Property, PropPtr, (*ArrayValue)[Index], PropError))
			{
				OutError = FString::Printf(TEXT("Failed to set array element %d: %s"), Index, *PropError);
				return false;
			}
		}
		return true;
	}

	const TSharedPtr<FJsonObject>* ObjValue;
	if (JsonValue->TryGetObject(ObjValue))
	{
		for (const auto& Pair : (*ObjValue)->Values)
		{
			FProperty* Property = Struct->FindPropertyByName(FName(*Pair.Key));
			if (!Property)
			{
				for (TFieldIterator<FProperty> PropIt(Struct); PropIt; ++PropIt)
				{
					if ((*PropIt)->GetName().Equals(Pair.Key, ESearchCase::IgnoreCase))
					{
						Property = *PropIt;
						break;
					}
				}
			}

			if (Property)
			{
				void* PropPtr = Property->ContainerPtrToValuePtr<void>(StructMemory);
				FString PropError;
				if (!SetPropertyFromJsonValue(Property, PropPtr, Pair.Value, PropError))
				{
					OutError = FString::Printf(TEXT("Failed to set '%s': %s"), *Pair.Key, *PropError);
					return false;
				}
			}
		}
		return true;
	}

	FString StrValue;
	if (JsonValue->TryGetString(StrValue))
	{
		if (Struct->ImportText(*StrValue, StructMemory, nullptr, PPF_None, nullptr, Struct->GetName()))
		{
			return true;
		}
	}

	OutError = TEXT("Expected array, object, or string representation for struct");
	return false;
}

bool FEditNiagaraTool::ParseJsonToNiagaraVariable(const FNiagaraTypeDefinition& TypeDef, const TSharedPtr<FJsonValue>& JsonValue, FNiagaraVariable& OutVar, FString& OutError)
{
	OutVar.SetType(TypeDef);
	OutVar.AllocateData();

	if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
	{
		double NumValue;
		if (JsonValue->TryGetNumber(NumValue))
		{
			OutVar.SetValue<float>((float)NumValue);
			return true;
		}
		OutError = TEXT("Expected number for float type");
		return false;
	}

	if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
	{
		double NumValue;
		if (JsonValue->TryGetNumber(NumValue))
		{
			OutVar.SetValue<int32>((int32)NumValue);
			return true;
		}
		OutError = TEXT("Expected number for int type");
		return false;
	}

	if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
	{
		FNiagaraBool BoolStruct;
		bool BoolValue;
		if (JsonValue->TryGetBool(BoolValue))
		{
			BoolStruct.SetValue(BoolValue);
			OutVar.SetValue<FNiagaraBool>(BoolStruct);
			return true;
		}
		double NumValue;
		if (JsonValue->TryGetNumber(NumValue))
		{
			BoolStruct.SetValue(NumValue != 0.0);
			OutVar.SetValue<FNiagaraBool>(BoolStruct);
			return true;
		}
		OutError = TEXT("Expected boolean or number for bool type");
		return false;
	}

	UScriptStruct* Struct = TypeDef.GetScriptStruct();
	if (Struct)
	{
		void* DataPtr = OutVar.GetData();
		if (!SetStructFromJson(Struct, DataPtr, JsonValue, OutError))
		{
			return false;
		}
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported type: %s"), *TypeDef.GetName());
	return false;
}

bool FEditNiagaraTool::SetInputValue(
	UNiagaraSystem* System,
	const FNiagaraEmitterHandle& EmitterHandle,
	UNiagaraNodeFunctionCall* ModuleNode,
	UNiagaraScript* Script,
	const FNiagaraVariable& Input,
	const TSharedPtr<FJsonValue>& Value,
	FString& OutError)
{
	FNiagaraTypeDefinition InputType = Input.GetType();

	FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(Input.GetName());
	FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		InputHandle, ModuleNode);

	FNiagaraVariable TempVar(InputType, AliasedHandle.GetParameterHandleString());
	if (!ParseJsonToNiagaraVariable(InputType, Value, TempVar, OutError))
	{
		return false;
	}

	// Determine if this type supports rapid iteration parameters.
	// Static types always use RI. Non-RI types: bool, enum, parameter map, UObject.
	bool bIsRapidIterationType = InputType.IsStatic() ||
		(InputType != FNiagaraTypeDefinition::GetBoolDef() &&
		 !InputType.IsEnum() &&
		 InputType != FNiagaraTypeDefinition::GetParameterMapDef() &&
		 !InputType.IsUObject());

	// Only use RI path if both the type supports it AND the system hasn't disabled RI params
	// (e.g. bBakeOutRapidIteration is set). If RI is disabled, fall through to override pin.
	if (bIsRapidIterationType && System->ShouldUseRapidIterationParameters())
	{
		// If this input previously used graph-based override modes (dynamic/linked/HLSL/DI),
		// clear that override pin so RI/static mode can take effect.
		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());
		if (OverridePin.LinkedTo.Num() > 0)
		{
			NiagaraToolHelpers::CleanupOverridePinNodes(OverridePin);
		}
		if (!OverridePin.DefaultValue.IsEmpty() || OverridePin.LinkedTo.Num() == 0)
		{
			if (UEdGraphNode* OwningNode = OverridePin.GetOwningNode())
			{
				OwningNode->Modify();
				OwningNode->RemovePin(&OverridePin);
				if (UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(OwningNode))
				{
					NiagaraNode->MarkNodeRequiresSynchronization(TEXT("SetInputValueStatic"), true);
				}
			}
		}

		// Get the unique emitter name for RI param name construction.
		// For emitter-level modules, this is the emitter's unique name.
		// For system-level modules, this would be empty/nullptr.
		FString UniqueEmitterName;
		if (EmitterHandle.GetInstance().Emitter)
		{
			UniqueEmitterName = EmitterHandle.GetInstance().Emitter->GetUniqueEmitterName();
		}

		// Construct the RI parameter name — same name is written to ALL affected scripts
		FNiagaraVariable RIParam = FNiagaraUtilities::ConvertVariableToRapidIterationConstantName(
			TempVar,
			UniqueEmitterName.IsEmpty() ? nullptr : *UniqueEmitterName,
			Script->GetUsage());

		// Find ALL scripts sharing this compilation context.
		// The engine writes RI params to system spawn+update scripts PLUS any emitter
		// scripts that contain the target usage. This ensures the compiled bytecode
		// sees the value regardless of which script's compilation includes the module.
		TArray<UNiagaraScript*> AffectedScripts = NiagaraToolHelpers::FindAffectedScripts(
			System, EmitterHandle, Script->GetUsage());

		// Mark all affected scripts for undo before modifying any
		for (UNiagaraScript* AffectedScript : AffectedScripts)
		{
			if (AffectedScript)
			{
				AffectedScript->Modify();
			}
		}

		// Write the RI param to all affected scripts
		bool bAnyWritten = false;
		for (UNiagaraScript* AffectedScript : AffectedScripts)
		{
			if (AffectedScript)
			{
				bool bAdded = AffectedScript->RapidIterationParameters.SetParameterData(
					TempVar.GetData(), RIParam, /*bAddParameterIfMissing=*/ true);
				bAnyWritten |= bAdded;
			}
		}

		if (!bAnyWritten)
		{
			OutError = FString::Printf(TEXT("Failed to set rapid iteration parameter for %s across %d scripts"),
				*Input.GetName().ToString(), AffectedScripts.Num());
			return false;
		}
	}
	else
	{
		// Non-RI types (bool, enum, UObject) or RI is disabled — use override pin
		UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
			*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

		FString PinValue;
		if (!GetDefault<UEdGraphSchema_Niagara>()->TryGetPinDefaultValueFromNiagaraVariable(TempVar, PinValue))
		{
			OutError = FString::Printf(TEXT("Could not convert value to pin format for type %s"), *InputType.GetName());
			return false;
		}

		OverridePin.Modify();
		OverridePin.DefaultValue = PinValue;

		if (UNiagaraNode* OwningNode = Cast<UNiagaraNode>(OverridePin.GetOwningNode()))
		{
			OwningNode->MarkNodeRequiresSynchronization(TEXT("Value Changed"), true);
		}
	}

	return true;
}

// ==========================================================================
// Advanced Input Value Modes — Dynamic Input, Linked, Custom HLSL, Data Interface
// ==========================================================================

FString FEditNiagaraTool::SetDynamicInput(
	UNiagaraSystem* System,
	const FNiagaraEmitterHandle& EmitterHandle,
	UNiagaraNodeFunctionCall* ModuleNode,
	UNiagaraScript* Script,
	const FNiagaraVariable& Input,
	const FString& DynamicInputPath,
	const TSharedPtr<FJsonObject>& ChildParams,
	FString& OutError)
{
	// 1. Validate dynamic input path
	if (DynamicInputPath.IsEmpty())
	{
		OutError = TEXT("Missing 'script' path for dynamic input");
		return FString();
	}

	// 2. Load the dynamic input script
	UNiagaraScript* DIScript = LoadObject<UNiagaraScript>(nullptr, *DynamicInputPath);
	if (!DIScript)
	{
		OutError = FString::Printf(TEXT("Dynamic input script not found: %s"), *DynamicInputPath);
		return FString();
	}

	// 3. Validate it's actually a dynamic input
	if (DIScript->GetUsage() != ENiagaraScriptUsage::DynamicInput)
	{
		OutError = FString::Printf(TEXT("Script '%s' is not a Dynamic Input (usage: %s)"),
			*DynamicInputPath, *StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue((int64)DIScript->GetUsage()));
		return FString();
	}

	// 4. Validate output type matches input type (with Vec3 <-> Position flexibility)
	FNiagaraTypeDefinition InputType = Input.GetType();
	{
		// Use the exported GetScriptOutputNode instead of the unexported FindOutputNode
		UNiagaraNodeOutput* OutputNode = FNiagaraEditorUtilities::GetScriptOutputNode(*DIScript);
		if (OutputNode && OutputNode->GetOutputs().Num() > 0)
		{
			FNiagaraTypeDefinition OutputType = OutputNode->GetOutputs()[0].GetType();
			bool bTypesCompatible = (OutputType == InputType)
				|| (OutputType == FNiagaraTypeDefinition::GetVec3Def() && InputType == FNiagaraTypeDefinition::GetPositionDef())
				|| (OutputType == FNiagaraTypeDefinition::GetPositionDef() && InputType == FNiagaraTypeDefinition::GetVec3Def());

			if (!bTypesCompatible)
			{
				OutError = FString::Printf(TEXT("Dynamic input output type '%s' doesn't match input type '%s'"),
					*OutputType.GetName(), *InputType.GetName());
				return FString();
			}
		}
	}

	// 5. Construct the aliased parameter handle
	FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(Input.GetName());
	FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		InputHandle, ModuleNode);
	FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

	// 6. Get or create the override pin
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	// 7. Clean up any existing nodes on the override pin
	if (OverridePin.LinkedTo.Num() > 0)
	{
		NiagaraToolHelpers::CleanupOverridePinNodes(OverridePin);
	}

	// 8. Remove any existing RI parameter (avoid stale static value conflicting)
	NiagaraToolHelpers::RemoveRapidIterationParameter(System, EmitterHandle, Script, AliasedVar);

	// 9. Clear any default value on the pin
	OverridePin.DefaultValue = FString();

	// 10. Call the exported engine function to create the dynamic input node
	UNiagaraNodeFunctionCall* DynamicInputNode = nullptr;
	FNiagaraStackGraphUtilities::SetDynamicInputForFunctionInput(
		OverridePin, DIScript, DynamicInputNode);

	if (!DynamicInputNode)
	{
		OutError = TEXT("Failed to create dynamic input node — SetDynamicInputForFunctionInput returned null");
		return FString();
	}

	// 11. Recursively set child parameters if provided
	FString ChildResults;
	if (ChildParams.IsValid() && ChildParams->Values.Num() > 0)
	{
		TArray<FNiagaraVariable> DIInputs = DiscoverModuleInputs(DynamicInputNode);

		for (const auto& ChildParam : ChildParams->Values)
		{
			FString ChildName = ChildParam.Key;
			TSharedPtr<FJsonValue> ChildValue = ChildParam.Value;

			// Match child input by name (same fuzzy matching as module inputs)
			FNiagaraVariable* MatchedChild = nullptr;
			for (FNiagaraVariable& DIInput : DIInputs)
			{
				FString InputName = DIInput.GetName().ToString();
				FString ShortName = InputName;
				if (ShortName.Contains(TEXT(".")))
				{
					ShortName = ShortName.RightChop(
						ShortName.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
				}

				if (InputName.Equals(ChildName, ESearchCase::IgnoreCase) ||
					ShortName.Equals(ChildName, ESearchCase::IgnoreCase) ||
					InputName.Equals(TEXT("Module.") + ChildName, ESearchCase::IgnoreCase))
				{
					MatchedChild = &DIInput;
					break;
				}
			}

			if (MatchedChild)
			{
				FString ChildError;
				// Child params on dynamic inputs are set using the same SetInputValue path
				// (they support RI params on the dynamic input node just like module nodes)
				if (!SetInputValue(System, EmitterHandle, DynamicInputNode, Script, *MatchedChild, ChildValue, ChildError))
				{
					if (!ChildResults.IsEmpty()) ChildResults += TEXT("; ");
					ChildResults += FString::Printf(TEXT("%s: %s"), *ChildName, *ChildError);
				}
			}
			else
			{
				if (!ChildResults.IsEmpty()) ChildResults += TEXT("; ");
				ChildResults += FString::Printf(TEXT("Child input '%s' not found"), *ChildName);
			}
		}
	}

	FString Result = FString::Printf(TEXT("Set dynamic input '%s' on '%s'"),
		*FPaths::GetBaseFilename(DynamicInputPath), *Input.GetName().ToString());
	if (!ChildResults.IsEmpty())
	{
		Result += FString::Printf(TEXT(" (child param warnings: %s)"), *ChildResults);
	}
	return Result;
}

FString FEditNiagaraTool::SetLinkedInput(
	UNiagaraSystem* System,
	const FNiagaraEmitterHandle& EmitterHandle,
	UNiagaraNodeFunctionCall* ModuleNode,
	UNiagaraScript* Script,
	const FNiagaraVariable& Input,
	const FString& LinkedParameterName,
	FString& OutError)
{
	if (LinkedParameterName.IsEmpty())
	{
		OutError = TEXT("Missing 'parameter' name for linked input");
		return FString();
	}

	FNiagaraTypeDefinition InputType = Input.GetType();

	// 1. Construct the aliased handle
	FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(Input.GetName());
	FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		InputHandle, ModuleNode);
	FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

	// 2. Get or create override pin
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	// 3. Clean up existing nodes
	if (OverridePin.LinkedTo.Num() > 0)
	{
		NiagaraToolHelpers::CleanupOverridePinNodes(OverridePin);
	}

	// 4. Remove any existing RI parameter
	NiagaraToolHelpers::RemoveRapidIterationParameter(System, EmitterHandle, Script, AliasedVar);
	OverridePin.DefaultValue = FString();

	// 5. Build the linked parameter variable with the correct type
	FNiagaraVariable LinkedParam(InputType, FName(*LinkedParameterName));

	// 6. Build KnownParameters set — the engine uses this to validate the link target.
	// Keep this path export-safe: exposed parameters + requested target parameter.
	TArray<FNiagaraVariable> ExposedVars;
	System->GetExposedParameters().GetParameters(ExposedVars);

#if ENGINE_MINOR_VERSION >= 6
	TSet<FNiagaraVariableBase> KnownParameters;
	for (const FNiagaraVariable& Var : ExposedVars)
	{
		KnownParameters.Add(Var);
	}
	KnownParameters.Add(FNiagaraVariableBase(LinkedParam.GetType(), LinkedParam.GetName()));

	// 7. Call the exported engine function (5.6+ API)
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(
		OverridePin, LinkedParam, KnownParameters, ENiagaraDefaultMode::FailIfPreviouslyNotSet);
#else
	// UE 5.5: Use the older SetLinkedValueHandleForFunctionInput API
	TSet<FNiagaraVariable> KnownParameters;
	for (const FNiagaraVariable& Var : ExposedVars)
	{
		KnownParameters.Add(Var);
	}
	KnownParameters.Add(LinkedParam);

	FNiagaraParameterHandle LinkedHandle(LinkedParam.GetName());
	FNiagaraStackGraphUtilities::SetLinkedValueHandleForFunctionInput(
		OverridePin, LinkedHandle, KnownParameters, ENiagaraDefaultMode::FailIfPreviouslyNotSet);
#endif

	return FString::Printf(TEXT("Linked '%s' -> '%s'"),
		*Input.GetName().ToString(), *LinkedParameterName);
}

FString FEditNiagaraTool::SetCustomHLSLInput(
	UNiagaraSystem* System,
	const FNiagaraEmitterHandle& EmitterHandle,
	UNiagaraNodeFunctionCall* ModuleNode,
	UNiagaraScript* Script,
	const FNiagaraVariable& Input,
	const FString& HLSLCode,
	FString& OutError)
{
	if (HLSLCode.IsEmpty())
	{
		OutError = TEXT("Missing 'code' for custom HLSL input");
		return FString();
	}

	if (HLSLCode.Len() > 10000)
	{
		OutError = TEXT("HLSL code exceeds 10000 character limit");
		return FString();
	}

	FNiagaraTypeDefinition InputType = Input.GetType();

	// 1. Construct aliased handle
	FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(Input.GetName());
	FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		InputHandle, ModuleNode);
	FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

	// 2. Get override pin
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	// 3. Clean up existing
	if (OverridePin.LinkedTo.Num() > 0)
	{
		NiagaraToolHelpers::CleanupOverridePinNodes(OverridePin);
	}
	NiagaraToolHelpers::RemoveRapidIterationParameter(System, EmitterHandle, Script, AliasedVar);
	OverridePin.DefaultValue = FString();

	// 4. SetCustomExpressionForFunctionInput is NOT exported, so we replicate it.
	// UNiagaraNodeCustomHlsl is MinimalAPI — type info + StaticClass available, but
	// InitAsCustomHlslDynamicInput() and SetCustomHlsl() are NOT linkable.
	// We replicate them using public APIs: CreatePin(), reflection, and virtual dispatch.
	UEdGraphNode* OverrideNode = OverridePin.GetOwningNode();
	if (!OverrideNode)
	{
		OutError = TEXT("Override pin has no owning node");
		return FString();
	}

	UEdGraph* Graph = OverrideNode->GetGraph();
	if (!Graph)
	{
		OutError = TEXT("Override node has no graph");
		return FString();
	}

	Graph->Modify();

	// Create the Custom HLSL node via FGraphNodeCreator (uses StaticClass — exported by MinimalAPI)
	FGraphNodeCreator<UNiagaraNodeCustomHlsl> NodeCreator(*Graph);
	UNiagaraNodeCustomHlsl* HlslNode = NodeCreator.CreateNode();

	// Replicate InitAsCustomHlslDynamicInput() without calling the unexported method:
	// Original does: Modify() → ReallocatePins() → RequestNewTypedPin(Input, Map) →
	//                RequestNewTypedPin(Output, Type) → ScriptUsage = DynamicInput
	// ReallocatePins and RequestNewTypedPin are protected, but CreatePin is public on UEdGraphNode
	// and TypeDefinitionToPinType is static+exported on UEdGraphSchema_Niagara.
	HlslNode->Modify();
	HlslNode->ScriptUsage = ENiagaraScriptUsage::DynamicInput; // public UPROPERTY

	FEdGraphPinType MapPinType = UEdGraphSchema_Niagara::TypeDefinitionToPinType(
		FNiagaraTypeDefinition::GetParameterMapDef());
	FEdGraphPinType OutputPinType = UEdGraphSchema_Niagara::TypeDefinitionToPinType(InputType);

	HlslNode->CreatePin(EGPD_Input, MapPinType, FName("Map"));
	HlslNode->CreatePin(EGPD_Output, OutputPinType, FName("CustomHLSLOutput"));
	HlslNode->CreatePin(EGPD_Output, MapPinType, FName("OutputMap"));

	NodeCreator.Finalize();

	// Copy enabled state from the override node
	HlslNode->SetEnabledState(OverrideNode->GetDesiredEnabledState(), OverrideNode->HasUserSetTheEnabledState());

	// Connect parameter map: HLSL node needs its param map input connected to the
	// same source as the override node's param map input
	UEdGraphPin* HlslInputMap = NiagaraToolHelpers::GetParameterMapInputPin(HlslNode);
	UEdGraphPin* OverrideNodeInputMap = NiagaraToolHelpers::GetParameterMapInputPin(OverrideNode);

	if (HlslInputMap && OverrideNodeInputMap && OverrideNodeInputMap->LinkedTo.Num() > 0)
	{
		UEdGraphPin* PrevStackOutputPin = OverrideNodeInputMap->LinkedTo[0];
		HlslInputMap->MakeLinkTo(PrevStackOutputPin);
	}

	// Connect HLSL node's typed output to the override pin
	FPinCollectorArray HlslOutputPins;
	HlslNode->GetOutputPins(HlslOutputPins);
	UEdGraphPin* TypedOutputPin = nullptr;
	for (UEdGraphPin* OutPin : HlslOutputPins)
	{
		FNiagaraTypeDefinition PinType = GetDefault<UEdGraphSchema_Niagara>()->PinToTypeDefinition(OutPin);
		if (PinType != FNiagaraTypeDefinition::GetParameterMapDef())
		{
			TypedOutputPin = OutPin;
			break;
		}
	}

	if (TypedOutputPin)
	{
		TypedOutputPin->MakeLinkTo(&OverridePin);
	}
	else
	{
		OutError = TEXT("Custom HLSL node has no typed output pin after initialization");
		Graph->RemoveNode(HlslNode);
		return FString();
	}

	// Replicate SetCustomHlsl() without calling the unexported method:
	// Original does: Modify() → set CustomHlsl → RefreshFromExternalChanges() → MarkNodeRequiresSynchronization()
	// CustomHlsl is a private UPROPERTY — set via reflection.
	// RefreshFromExternalChanges is virtual on UEdGraphNode — dispatches to override via vtable.
	// MarkNodeRequiresSynchronization is NIAGARAEDITOR_API on UNiagaraNode — directly callable.
	HlslNode->Modify();
	FProperty* HlslProp = UNiagaraNodeCustomHlsl::StaticClass()->FindPropertyByName(FName("CustomHlsl"));
	if (HlslProp)
	{
		FString* HlslPtr = HlslProp->ContainerPtrToValuePtr<FString>(HlslNode);
		if (HlslPtr)
		{
			*HlslPtr = HLSLCode;
		}
	}
	HlslNode->RefreshFromExternalChanges();
	HlslNode->MarkNodeRequiresSynchronization(TEXT("SetCustomHLSLInput"), true);

	return FString::Printf(TEXT("Set custom HLSL on '%s'"), *Input.GetName().ToString());
}

FString FEditNiagaraTool::SetDataInterfaceInput(
	UNiagaraSystem* System,
	const FNiagaraEmitterHandle& EmitterHandle,
	UNiagaraNodeFunctionCall* ModuleNode,
	UNiagaraScript* Script,
	const FNiagaraVariable& Input,
	const FString& DataInterfaceTypeName,
	const TSharedPtr<FJsonObject>& DIProperties,
	FString& OutError)
{
	if (DataInterfaceTypeName.IsEmpty())
	{
		OutError = TEXT("Missing 'type' for data interface input");
		return FString();
	}

	// 1. Validate input type is a data interface
	FNiagaraTypeDefinition InputType = Input.GetType();
	if (!InputType.IsDataInterface())
	{
		OutError = FString::Printf(TEXT("Input '%s' type '%s' is not a data interface type"),
			*Input.GetName().ToString(), *InputType.GetName());
		return FString();
	}

	// 2. Resolve the data interface class by name
	// Try "NiagaraDataInterface<TypeName>" first, then just the name
	FString FullClassName = FString::Printf(TEXT("NiagaraDataInterface%s"), *DataInterfaceTypeName);
	UClass* DIClass = FindFirstObject<UClass>(*FullClassName, EFindFirstObjectOptions::None);

	if (!DIClass)
	{
		DIClass = FindFirstObject<UClass>(*DataInterfaceTypeName, EFindFirstObjectOptions::None);
	}

	if (!DIClass || !DIClass->IsChildOf(UNiagaraDataInterface::StaticClass()))
	{
		// Build list of available DI types for error message
		TArray<UClass*> DIClasses;
		GetDerivedClasses(UNiagaraDataInterface::StaticClass(), DIClasses, true);

		FString Available;
		int32 Count = 0;
		for (UClass* C : DIClasses)
		{
			if (!(C->ClassFlags & (CLASS_Abstract | CLASS_Deprecated)))
			{
				FString Name = C->GetName();
				Name.RemoveFromStart(TEXT("NiagaraDataInterface"));
				if (!Available.IsEmpty()) Available += TEXT(", ");
				Available += Name;
				if (++Count >= 30) { Available += TEXT(", ..."); break; }
			}
		}

		OutError = FString::Printf(TEXT("Data interface type '%s' not found. Available: %s"),
			*DataInterfaceTypeName, *Available);
		return FString();
	}

	// 3. Construct aliased handle
	FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(Input.GetName());
	FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		InputHandle, ModuleNode);
	FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

	// 4. Get override pin
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	// 5. Clean up existing
	if (OverridePin.LinkedTo.Num() > 0)
	{
		NiagaraToolHelpers::CleanupOverridePinNodes(OverridePin);
	}
	OverridePin.DefaultValue = FString();
	NiagaraToolHelpers::RemoveRapidIterationParameter(System, EmitterHandle, Script, AliasedVar);

	// 6. Call the exported engine function
	UNiagaraDataInterface* NewDI = nullptr;
	FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(
		OverridePin, DIClass, AliasedHandle.GetParameterHandleString().ToString(), NewDI);

	if (!NewDI)
	{
		OutError = TEXT("Failed to create data interface — SetDataInterfaceValueForFunctionInput returned null");
		return FString();
	}

	// 7. Set properties on the DI via reflection if provided
	if (DIProperties.IsValid())
	{
		for (const auto& Pair : DIProperties->Values)
		{
			FProperty* Prop = DIClass->FindPropertyByName(FName(*Pair.Key));
			if (Prop)
			{
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(NewDI);
				FString PropError;
				if (!SetPropertyFromJsonValue(Prop, ValuePtr, Pair.Value, PropError))
				{
					UE_LOG(LogTemp, Warning, TEXT("SetDataInterfaceInput: Failed to set DI property '%s': %s"),
						*Pair.Key, *PropError);
				}
			}
		}
	}

	return FString::Printf(TEXT("Set data interface '%s' on '%s'"),
		*DataInterfaceTypeName, *Input.GetName().ToString());
}

bool FEditNiagaraTool::ResetModuleInputToDefault(
	UNiagaraSystem* System,
	const FNiagaraEmitterHandle& EmitterHandle,
	UNiagaraNodeFunctionCall* ModuleNode,
	UNiagaraScript* Script,
	const FNiagaraVariable& Input,
	FString& OutError)
{
	if (!System || !ModuleNode || !Script)
	{
		OutError = TEXT("Invalid reset context");
		return false;
	}

	const FNiagaraTypeDefinition InputType = Input.GetType();
	const FNiagaraParameterHandle InputHandle = FNiagaraParameterHandle::CreateModuleParameterHandle(Input.GetName());
	const FNiagaraParameterHandle AliasedHandle = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(InputHandle, ModuleNode);
	FNiagaraVariable AliasedVar(InputType, AliasedHandle.GetParameterHandleString());

	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*ModuleNode, AliasedHandle, InputType, FGuid(), FGuid());

	if (OverridePin.LinkedTo.Num() > 0)
	{
		NiagaraToolHelpers::CleanupOverridePinNodes(OverridePin);
	}

	NiagaraToolHelpers::RemoveRapidIterationParameter(System, EmitterHandle, Script, AliasedVar);

	OverridePin.Modify();
	OverridePin.DefaultValue = FString();
	if (UEdGraphNode* OwningNode = OverridePin.GetOwningNode())
	{
		OwningNode->Modify();
		// Remove the override pin entirely so the module falls back to authored defaults.
		if (!OwningNode->RemovePin(&OverridePin))
		{
			OutError = TEXT("Failed to remove override pin");
			return false;
		}
		if (UNiagaraNode* NiagaraNode = Cast<UNiagaraNode>(OwningNode))
		{
			NiagaraNode->MarkNodeRequiresSynchronization(TEXT("ResetInputOverride"), true);
		}
	}

	return true;
}

FString FEditNiagaraTool::ListDynamicInputs(const FString& OutputTypeFilter) const
{
	FNiagaraEditorUtilities::FGetFilteredScriptAssetsOptions FilterOptions;
	FilterOptions.ScriptUsageToInclude = ENiagaraScriptUsage::DynamicInput;
	FilterOptions.bIncludeDeprecatedScripts = false;
	FilterOptions.bIncludeNonLibraryScripts = true;

	TArray<FAssetData> FilteredAssets;
	FNiagaraEditorUtilities::GetFilteredScriptAssets(FilterOptions, FilteredAssets);

	FString Result = TEXT("## Available Dynamic Inputs");
	if (!OutputTypeFilter.IsEmpty())
	{
		Result += FString::Printf(TEXT(" (type: %s)"), *OutputTypeFilter);
	}
	Result += TEXT("\n\n");

	int32 Count = 0;
	for (const FAssetData& AssetData : FilteredAssets)
	{
		FString OutputType = TEXT("Unknown");
		FString Description;

		if (UNiagaraScript* DIScript = Cast<UNiagaraScript>(AssetData.GetAsset()))
		{
			UNiagaraNodeOutput* OutNode = FNiagaraEditorUtilities::GetScriptOutputNode(*DIScript);
			if (OutNode && OutNode->GetOutputs().Num() > 0)
			{
				OutputType = OutNode->GetOutputs()[0].GetType().GetName();
			}
			if (FVersionedNiagaraScriptData* ScriptData = DIScript->GetLatestScriptData())
			{
				if (!ScriptData->Description.IsEmpty())
				{
					Description = ScriptData->Description.ToString();
				}
			}
		}

		// Filter by output type if specified
		if (!OutputTypeFilter.IsEmpty() && !OutputType.Contains(OutputTypeFilter))
		{
			continue;
		}

		FString DisplayName = FNiagaraEditorUtilities::FormatScriptName(FName(*AssetData.AssetName.ToString()),
			FNiagaraEditorUtilities::IsScriptAssetInLibrary(AssetData)).ToString();
		if (DisplayName.IsEmpty())
		{
			DisplayName = FName::NameToDisplayString(AssetData.AssetName.ToString(), false);
		}
		FString Path = AssetData.GetSoftObjectPath().ToString();

		Result += FString::Printf(TEXT("- **%s** [%s]\n  Path: `%s`\n"),
			*DisplayName, *OutputType, *Path);
		if (!Description.IsEmpty())
		{
			Result += FString::Printf(TEXT("  %s\n"), *Description);
		}

		Count++;
		if (Count >= 50)
		{
			Result += TEXT("\n... (truncated, use a more specific type filter)\n");
			break;
		}
	}

	if (Count == 0)
	{
		Result += TEXT("(no dynamic inputs found");
		if (!OutputTypeFilter.IsEmpty())
		{
			Result += FString::Printf(TEXT(" for type '%s'"), *OutputTypeFilter);
		}
		Result += TEXT(")\n");
	}
	else
	{
		Result += FString::Printf(TEXT("\n**Total: %d dynamic inputs**\n"), Count);
	}

	return Result;
}

FString FEditNiagaraTool::FormatAvailableInputs(const TArray<FNiagaraVariable>& Inputs) const
{
	FString Result;
	for (const FNiagaraVariable& Input : Inputs)
	{
		FString Name = Input.GetName().ToString();
		FString TypeName = Input.GetType().GetName();

		if (Name.Contains(TEXT(".")))
		{
			Name = Name.RightChop(Name.Find(TEXT("."), ESearchCase::IgnoreCase, ESearchDir::FromEnd) + 1);
		}

		Result += FString::Printf(TEXT("  - %s (%s)\n"), *Name, *TypeName);
	}
	return Result;
}

FString FEditNiagaraTool::FormatResults(
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
	const TArray<FString>& Errors) const
{
	FString Output;

	Output += FString::Printf(TEXT("# EDIT NIAGARA: %s\n\n"), *SystemName);

	if (AddedEmitters.Num() > 0)
	{
		Output += TEXT("## Added Emitters\n");
		for (const FString& Emitter : AddedEmitters)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Emitter);
		}
		Output += TEXT("\n");
	}

	if (AddedEventHandlers.Num() > 0)
	{
		Output += TEXT("## Added Event Handlers\n");
		for (const FString& EventHandler : AddedEventHandlers)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *EventHandler);
		}
		Output += TEXT("\n");
	}

	if (RemovedEventHandlers.Num() > 0)
	{
		Output += TEXT("## Removed Event Handlers\n");
		for (const FString& EventHandler : RemovedEventHandlers)
		{
			Output += FString::Printf(TEXT("- %s\n"), *EventHandler);
		}
		Output += TEXT("\n");
	}

	if (UpdatedEventHandlers.Num() > 0)
	{
		Output += TEXT("## Updated Event Handlers\n");
		for (const FString& EventHandler : UpdatedEventHandlers)
		{
			Output += FString::Printf(TEXT("* %s\n"), *EventHandler);
		}
		Output += TEXT("\n");
	}

	if (AddedSimulationStages.Num() > 0)
	{
		Output += TEXT("## Added Simulation Stages\n");
		for (const FString& SimulationStage : AddedSimulationStages)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *SimulationStage);
		}
		Output += TEXT("\n");
	}

	if (RemovedSimulationStages.Num() > 0)
	{
		Output += TEXT("## Removed Simulation Stages\n");
		for (const FString& SimulationStage : RemovedSimulationStages)
		{
			Output += FString::Printf(TEXT("- %s\n"), *SimulationStage);
		}
		Output += TEXT("\n");
	}

	if (UpdatedSimulationStages.Num() > 0)
	{
		Output += TEXT("## Updated Simulation Stages\n");
		for (const FString& SimulationStage : UpdatedSimulationStages)
		{
			Output += FString::Printf(TEXT("* %s\n"), *SimulationStage);
		}
		Output += TEXT("\n");
	}

	if (ReorderedSimulationStages.Num() > 0)
	{
		Output += TEXT("## Reordered Simulation Stages\n");
		for (const FString& SimulationStage : ReorderedSimulationStages)
		{
			Output += FString::Printf(TEXT("* %s\n"), *SimulationStage);
		}
		Output += TEXT("\n");
	}

	if (AddedModules.Num() > 0)
	{
		Output += TEXT("## Added Modules\n");
		for (const FString& Module : AddedModules)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Module);
		}
		Output += TEXT("\n");
	}

	if (RemovedModules.Num() > 0)
	{
		Output += TEXT("## Removed Modules\n");
		for (const FString& Module : RemovedModules)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Module);
		}
		Output += TEXT("\n");
	}

	if (SetParameters.Num() > 0)
	{
		Output += TEXT("## Updated Parameters\n");
		for (const FString& Param : SetParameters)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Param);
		}
		Output += TEXT("\n");
	}

	if (AddedRenderers.Num() > 0)
	{
		Output += TEXT("## Added Renderers\n");
		for (const FString& Renderer : AddedRenderers)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Renderer);
		}
		Output += TEXT("\n");
	}

	if (RemovedRenderers.Num() > 0)
	{
		Output += TEXT("## Removed Renderers\n");
		for (const FString& Renderer : RemovedRenderers)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Renderer);
		}
		Output += TEXT("\n");
	}

	if (ConfiguredRenderers.Num() > 0)
	{
		Output += TEXT("## Configured Renderers\n");
		for (const FString& Renderer : ConfiguredRenderers)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Renderer);
		}
		Output += TEXT("\n");
	}

	if (AddedUserParams.Num() > 0)
	{
		Output += TEXT("## Added User Parameters\n");
		for (const FString& Param : AddedUserParams)
		{
			Output += FString::Printf(TEXT("+ %s\n"), *Param);
		}
		Output += TEXT("\n");
	}

	if (RemovedUserParams.Num() > 0)
	{
		Output += TEXT("## Removed User Parameters\n");
		for (const FString& Param : RemovedUserParams)
		{
			Output += FString::Printf(TEXT("- %s\n"), *Param);
		}
		Output += TEXT("\n");
	}

	if (SetUserParams.Num() > 0)
	{
		Output += TEXT("## Updated User Parameters\n");
		for (const FString& Param : SetUserParams)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Param);
		}
		Output += TEXT("\n");
	}

	if (SetEmitterProps.Num() > 0)
	{
		Output += TEXT("## Emitter Properties\n");
		for (const FString& Prop : SetEmitterProps)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Prop);
		}
		Output += TEXT("\n");
	}

	if (ModuleEnables.Num() > 0)
	{
		Output += TEXT("## Module Enable/Disable\n");
		for (const FString& Enable : ModuleEnables)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Enable);
		}
		Output += TEXT("\n");
	}

	if (MovedModules.Num() > 0)
	{
		Output += TEXT("## Moved Modules\n");
		for (const FString& Move : MovedModules)
		{
			Output += FString::Printf(TEXT("* %s\n"), *Move);
		}
		Output += TEXT("\n");
	}

	if (ScratchPadScripts.Num() > 0)
	{
		Output += TEXT("## Scratch Pad Scripts\n");
		for (const FString& ScriptOp : ScratchPadScripts)
		{
			Output += FString::Printf(TEXT("* %s\n"), *ScriptOp);
		}
		Output += TEXT("\n");
	}

	if (!SystemPropsResult.IsEmpty())
	{
		Output += TEXT("## System Properties\n");
		Output += FString::Printf(TEXT("* %s\n"), *SystemPropsResult);
		Output += TEXT("\n");
	}

	if (Errors.Num() > 0)
	{
		Output += TEXT("## Errors\n");
		for (const FString& Error : Errors)
		{
			Output += FString::Printf(TEXT("! %s\n"), *Error);
		}
		Output += TEXT("\n");
	}

	return Output;
}

// ========== Discovery Operations ==========

FString FEditNiagaraTool::GetRendererTypeName(const UNiagaraRendererProperties* Renderer) const
{
	if (!Renderer)
	{
		return TEXT("Unknown");
	}

	if (Renderer->IsA<UNiagaraSpriteRendererProperties>())
	{
		return TEXT("Sprite");
	}
	if (Renderer->IsA<UNiagaraMeshRendererProperties>())
	{
		return TEXT("Mesh");
	}
	if (Renderer->IsA<UNiagaraRibbonRendererProperties>())
	{
		return TEXT("Ribbon");
	}
	if (Renderer->IsA<UNiagaraLightRendererProperties>())
	{
		return TEXT("Light");
	}

	return Renderer->GetClass()->GetName();
}

FString FEditNiagaraTool::ListUserParameters(UNiagaraSystem* System) const
{
	if (!System)
	{
		return TEXT("! System is null\n");
	}

	const FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();

	TArrayView<const FNiagaraVariableWithOffset> Parameters = ExposedParams.ReadParameterVariables();

	if (Parameters.Num() == 0)
	{
		return TEXT("## User Parameters\nNo user parameters defined.\n\n");
	}

	FString Output = TEXT("## User Parameters\n");
	for (const FNiagaraVariableWithOffset& ParamWithOffset : Parameters)
	{
		FString Name = ParamWithOffset.GetName().ToString();

		// Remove "User." prefix for display
		if (Name.StartsWith(TEXT("User.")))
		{
			Name = Name.RightChop(5);
		}

		FString TypeName = ParamWithOffset.GetType().GetName();

		FNiagaraVariable ValueVar(ParamWithOffset.GetType(), ParamWithOffset.GetName());
		const uint8* ParamData = ExposedParams.GetParameterData(ParamWithOffset);
		FString ValueStr = TEXT("<unavailable>");
		if (ParamData)
		{
			ValueVar.SetData(ParamData);
			ValueStr = ValueVar.ToString();
			if (ValueStr.StartsWith(Name + TEXT("(")))
			{
				ValueStr = ValueStr.RightChop(Name.Len() + 1);
				if (ValueStr.EndsWith(TEXT(")")))
				{
					ValueStr.LeftChopInline(1);
				}
			}
		}

		Output += FString::Printf(TEXT("  - %s (%s) = %s\n"), *Name, *TypeName, *ValueStr);
	}

	Output += TEXT("\n");
	return Output;
}

// ========== Renderer Operations ==========

UNiagaraRendererProperties* FEditNiagaraTool::CreateRendererByType(UNiagaraEmitter* Emitter, const FString& Type)
{
	if (!Emitter)
	{
		return nullptr;
	}

	FString TypeLower = Type.ToLower();
	UClass* RendererClass = nullptr;

	if (TypeLower == TEXT("sprite"))
	{
		RendererClass = UNiagaraSpriteRendererProperties::StaticClass();
	}
	else if (TypeLower == TEXT("mesh"))
	{
		RendererClass = UNiagaraMeshRendererProperties::StaticClass();
	}
	else if (TypeLower == TEXT("ribbon"))
	{
		RendererClass = UNiagaraRibbonRendererProperties::StaticClass();
	}
	else if (TypeLower == TEXT("light"))
	{
		RendererClass = UNiagaraLightRendererProperties::StaticClass();
	}

	// Generic class-name resolution for additional renderer types (e.g., Decal, Volume)
	if (!RendererClass)
	{
		const FString CandidateShortName = Type.EndsWith(TEXT("RendererProperties"))
			? Type
			: (Type + TEXT("RendererProperties"));
		const FString CandidateScriptName = FString::Printf(TEXT("/Script/Niagara.%s"), *CandidateShortName);

		RendererClass = FindFirstObject<UClass>(*Type);
		if (!RendererClass)
		{
			RendererClass = FindFirstObject<UClass>(*CandidateShortName);
		}
		if (!RendererClass)
		{
			RendererClass = LoadObject<UClass>(nullptr, *CandidateScriptName);
		}
		if (RendererClass && !RendererClass->IsChildOf(UNiagaraRendererProperties::StaticClass()))
		{
			RendererClass = nullptr;
		}
	}

	if (!RendererClass)
	{
		return nullptr;
	}

	return NewObject<UNiagaraRendererProperties>(Emitter, RendererClass, NAME_None, RF_Transactional);
}

bool FEditNiagaraTool::SetRendererProperty(UNiagaraRendererProperties* Renderer, const FString& PropertyName, const TSharedPtr<FJsonValue>& Value, FString& OutError)
{
	if (!Renderer || !Value.IsValid())
	{
		OutError = TEXT("Invalid renderer or value");
		return false;
	}

	UClass* RendererClass = Renderer->GetClass();
	FProperty* Property = RendererClass->FindPropertyByName(FName(*PropertyName));

	if (!Property)
	{
		for (TFieldIterator<FProperty> PropIt(RendererClass); PropIt; ++PropIt)
		{
			if ((*PropIt)->GetName().Equals(PropertyName, ESearchCase::IgnoreCase))
			{
				Property = *PropIt;
				break;
			}
		}
	}

	if (!Property)
	{
		OutError = FString::Printf(TEXT("Property '%s' not found on renderer"), *PropertyName);
		return false;
	}

	void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Renderer);
	return SetPropertyFromJsonValue(Property, ValuePtr, Value, OutError);
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
bool FEditNiagaraTool::SetRendererBinding(UNiagaraRendererProperties* Renderer, const FVersionedNiagaraEmitterBase& VersionedEmitter, const FString& BindingName, const FString& AttributeName, FString& OutError)
{
	if (!Renderer)
	{
		OutError = TEXT("Invalid renderer");
		return false;
	}

	FString BindingPropertyName = BindingName;
	if (!BindingPropertyName.EndsWith(TEXT("Binding")))
	{
		BindingPropertyName += TEXT("Binding");
	}

	UClass* RendererClass = Renderer->GetClass();
	FProperty* Property = nullptr;

	for (TFieldIterator<FProperty> PropIt(RendererClass); PropIt; ++PropIt)
	{
		FString PropName = (*PropIt)->GetName();
		if (PropName.Equals(BindingPropertyName, ESearchCase::IgnoreCase) ||
			PropName.Equals(BindingName, ESearchCase::IgnoreCase))
		{
			Property = *PropIt;
			break;
		}
	}

	if (!Property)
	{
		OutError = FString::Printf(TEXT("Binding property '%s' not found"), *BindingName);
		return false;
	}

	FStructProperty* StructProp = CastField<FStructProperty>(Property);
	if (!StructProp || StructProp->Struct->GetName() != TEXT("NiagaraVariableAttributeBinding"))
	{
		OutError = FString::Printf(TEXT("Property '%s' is not a NiagaraVariableAttributeBinding"), *BindingName);
		return false;
	}

	FNiagaraVariableAttributeBinding* Binding = StructProp->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Renderer);
	if (!Binding)
	{
		OutError = TEXT("Could not access binding property");
		return false;
	}

	Binding->SetValue(FName(*AttributeName), VersionedEmitter, ENiagaraRendererSourceDataMode::Particles);

	return true;
}
#else
bool FEditNiagaraTool::SetRendererBinding(UNiagaraRendererProperties* Renderer, const FVersionedNiagaraEmitter& VersionedEmitter, const FString& BindingName, const FString& AttributeName, FString& OutError)
{
	if (!Renderer)
	{
		OutError = TEXT("Invalid renderer");
		return false;
	}

	FString BindingPropertyName = BindingName;
	if (!BindingPropertyName.EndsWith(TEXT("Binding")))
	{
		BindingPropertyName += TEXT("Binding");
	}

	UClass* RendererClass = Renderer->GetClass();
	FProperty* Property = nullptr;

	for (TFieldIterator<FProperty> PropIt(RendererClass); PropIt; ++PropIt)
	{
		FString PropName = (*PropIt)->GetName();
		if (PropName.Equals(BindingPropertyName, ESearchCase::IgnoreCase) ||
			PropName.Equals(BindingName, ESearchCase::IgnoreCase))
		{
			Property = *PropIt;
			break;
		}
	}

	if (!Property)
	{
		OutError = FString::Printf(TEXT("Binding property '%s' not found"), *BindingName);
		return false;
	}

	FStructProperty* StructProp = CastField<FStructProperty>(Property);
	if (!StructProp || StructProp->Struct->GetName() != TEXT("NiagaraVariableAttributeBinding"))
	{
		OutError = FString::Printf(TEXT("Property '%s' is not a NiagaraVariableAttributeBinding"), *BindingName);
		return false;
	}

	FNiagaraVariableAttributeBinding* Binding = StructProp->ContainerPtrToValuePtr<FNiagaraVariableAttributeBinding>(Renderer);
	if (!Binding)
	{
		OutError = TEXT("Could not access binding property");
		return false;
	}

	Binding->SetValue(FName(*AttributeName), VersionedEmitter, ENiagaraRendererSourceDataMode::Particles);
	return true;
}
#endif

FString FEditNiagaraTool::AddRenderer(UNiagaraSystem* System, const FRendererDefinition& RendererDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	int32 EmitterIndex = FindEmitterIndexByName(System, RendererDef.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		TArray<FString> AvailableEmitters;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			AvailableEmitters.Add(Handle.GetName().ToString());
		}
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found. Available: %s"),
			*RendererDef.EmitterName, *FString::Join(AvailableEmitters, TEXT(", ")));
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not get emitter data for '%s'"), *RendererDef.EmitterName);
	}

	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	if (!Emitter)
	{
		return FString::Printf(TEXT("ERROR: Could not get emitter instance for '%s'"), *RendererDef.EmitterName);
	}

	UNiagaraRendererProperties* NewRenderer = CreateRendererByType(Emitter, RendererDef.Type);
	if (!NewRenderer)
	{
		return FString::Printf(TEXT("ERROR: Unknown renderer type '%s'. Valid types: Sprite, Mesh, Ribbon, Light"),
			*RendererDef.Type);
	}

	if (RendererDef.Type.Equals(TEXT("light"), ESearchCase::IgnoreCase) &&
		EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		return FString::Printf(TEXT("ERROR: Light renderer only works with CPU simulation. "
			"Emitter '%s' is set to GPU. Change sim target to CPU first."), *RendererDef.EmitterName);
	}

	TArray<FString> PropErrors;
	if (RendererDef.Properties.IsValid())
	{
		for (const auto& Prop : RendererDef.Properties->Values)
		{
			FString PropError;
			if (!SetRendererProperty(NewRenderer, Prop.Key, Prop.Value, PropError))
			{
				PropErrors.Add(FString::Printf(TEXT("%s: %s"), *Prop.Key, *PropError));
			}
		}
	}

	TArray<FString> BindingErrors;
	if (RendererDef.Bindings.IsValid())
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		FVersionedNiagaraEmitterBase VersionedEmitterBase = Handle.GetInstance().ToBase();
#else
		FVersionedNiagaraEmitter VersionedEmitter = Handle.GetInstance();
#endif
		for (const auto& Binding : RendererDef.Bindings->Values)
		{
			FString BindValue;
			if (Binding.Value->TryGetString(BindValue))
			{
				FString BindError;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				if (!SetRendererBinding(NewRenderer, VersionedEmitterBase, Binding.Key, BindValue, BindError))
#else
				if (!SetRendererBinding(NewRenderer, VersionedEmitter, Binding.Key, BindValue, BindError))
#endif
				{
					BindingErrors.Add(FString::Printf(TEXT("%s: %s"), *Binding.Key, *BindError));
				}
			}
		}
	}

	Emitter->AddRenderer(NewRenderer, Handle.GetInstance().Version);

	FString Result = FString::Printf(TEXT("%s renderer added to '%s'"),
		*GetRendererTypeName(NewRenderer), *RendererDef.EmitterName);

	if (PropErrors.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (property warnings: %s)"), *FString::Join(PropErrors, TEXT("; ")));
	}
	if (BindingErrors.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (binding warnings: %s)"), *FString::Join(BindingErrors, TEXT("; ")));
	}

	return Result;
}

FString FEditNiagaraTool::RemoveRenderer(UNiagaraSystem* System, const FRendererRemoval& RemovalDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	int32 EmitterIndex = FindEmitterIndexByName(System, RemovalDef.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		TArray<FString> AvailableEmitters;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			AvailableEmitters.Add(Handle.GetName().ToString());
		}
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found. Available: %s"),
			*RemovalDef.EmitterName, *FString::Join(AvailableEmitters, TEXT(", ")));
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not get emitter data for '%s'"), *RemovalDef.EmitterName);
	}

	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	if (!Emitter)
	{
		return FString::Printf(TEXT("ERROR: Could not get emitter instance for '%s'"), *RemovalDef.EmitterName);
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (Renderers.Num() == 0)
	{
		return FString::Printf(TEXT("ERROR: No renderers on emitter '%s'"), *RemovalDef.EmitterName);
	}

	int32 TargetIndex = RemovalDef.Index;

	if (!RemovalDef.Type.IsEmpty())
	{
		FString TypeLower = RemovalDef.Type.ToLower();
		for (int32 i = 0; i < Renderers.Num(); ++i)
		{
			FString RendererType = GetRendererTypeName(Renderers[i]).ToLower();
			if (RendererType.Equals(TypeLower))
			{
				TargetIndex = i;
				break;
			}
		}
		if (TargetIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: No %s renderer found on '%s'"),
				*RemovalDef.Type, *RemovalDef.EmitterName);
		}
	}

	if (TargetIndex == INDEX_NONE)
	{
		TargetIndex = Renderers.Num() - 1;
	}

	if (!Renderers.IsValidIndex(TargetIndex))
	{
		return FString::Printf(TEXT("ERROR: Renderer index %d out of range (0-%d) on '%s'"),
			TargetIndex, Renderers.Num() - 1, *RemovalDef.EmitterName);
	}

	UNiagaraRendererProperties* RendererToRemove = Renderers[TargetIndex];
	FString RemovedType = GetRendererTypeName(RendererToRemove);

	Emitter->RemoveRenderer(RendererToRemove, Handle.GetInstance().Version);

	return FString::Printf(TEXT("Removed %s renderer [%d] from '%s'"),
		*RemovedType, TargetIndex, *RemovalDef.EmitterName);
}

FString FEditNiagaraTool::ConfigureRenderer(UNiagaraSystem* System, const FRendererConfiguration& Config)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	int32 EmitterIndex = FindEmitterIndexByName(System, Config.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		TArray<FString> AvailableEmitters;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			AvailableEmitters.Add(Handle.GetName().ToString());
		}
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found. Available: %s"),
			*Config.EmitterName, *FString::Join(AvailableEmitters, TEXT(", ")));
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not get emitter data for '%s'"), *Config.EmitterName);
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (!Renderers.IsValidIndex(Config.Index))
	{
		if (Renderers.Num() == 0)
		{
			return FString::Printf(TEXT("ERROR: No renderers on emitter '%s'"), *Config.EmitterName);
		}
		return FString::Printf(TEXT("ERROR: Renderer index %d out of range (0-%d) on '%s'"),
			Config.Index, Renderers.Num() - 1, *Config.EmitterName);
	}

	UNiagaraRendererProperties* Renderer = Renderers[Config.Index];
	FString RendererType = GetRendererTypeName(Renderer);

	TArray<FString> SetProps;
	TArray<FString> PropErrors;

	if (Config.Properties.IsValid())
	{
		for (const auto& Prop : Config.Properties->Values)
		{
			FString PropError;
			if (SetRendererProperty(Renderer, Prop.Key, Prop.Value, PropError))
			{
				SetProps.Add(Prop.Key);
			}
			else
			{
				PropErrors.Add(FString::Printf(TEXT("%s: %s"), *Prop.Key, *PropError));
			}
		}
	}

	TArray<FString> SetBindings;
	TArray<FString> BindingErrors;

	if (Config.Bindings.IsValid())
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		FVersionedNiagaraEmitterBase VersionedEmitterBase = Handle.GetInstance().ToBase();
#else
		FVersionedNiagaraEmitter VersionedEmitter = Handle.GetInstance();
#endif
		for (const auto& Binding : Config.Bindings->Values)
		{
			FString BindValue;
			if (Binding.Value->TryGetString(BindValue))
			{
				FString BindError;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
				if (SetRendererBinding(Renderer, VersionedEmitterBase, Binding.Key, BindValue, BindError))
#else
				if (SetRendererBinding(Renderer, VersionedEmitter, Binding.Key, BindValue, BindError))
#endif
				{
					SetBindings.Add(Binding.Key);
				}
				else
				{
					BindingErrors.Add(FString::Printf(TEXT("%s: %s"), *Binding.Key, *BindError));
				}
			}
		}
	}

	FString Result = FString::Printf(TEXT("Configured %s renderer [%d] on '%s'"),
		*RendererType, Config.Index, *Config.EmitterName);

	if (SetProps.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (set: %s)"), *FString::Join(SetProps, TEXT(", ")));
	}
	if (SetBindings.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (bindings: %s)"), *FString::Join(SetBindings, TEXT(", ")));
	}
	if (PropErrors.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (property errors: %s)"), *FString::Join(PropErrors, TEXT("; ")));
	}
	if (BindingErrors.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (binding errors: %s)"), *FString::Join(BindingErrors, TEXT("; ")));
	}

	return Result;
}

FString FEditNiagaraTool::ReorderRenderer(UNiagaraSystem* System, const FRendererReorder& ReorderOp)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	int32 EmitterIndex = FindEmitterIndexByName(System, ReorderOp.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *ReorderOp.EmitterName);
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	if (!EmitterData || !Emitter)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter '%s'"), *ReorderOp.EmitterName);
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (!Renderers.IsValidIndex(ReorderOp.Index))
	{
		return FString::Printf(TEXT("ERROR: Renderer index %d out of range (0-%d) on '%s'"),
			ReorderOp.Index, FMath::Max(0, Renderers.Num() - 1), *ReorderOp.EmitterName);
	}
	if (ReorderOp.NewIndex < 0 || ReorderOp.NewIndex >= Renderers.Num())
	{
		return FString::Printf(TEXT("ERROR: New renderer index %d out of range (0-%d) on '%s'"),
			ReorderOp.NewIndex, FMath::Max(0, Renderers.Num() - 1), *ReorderOp.EmitterName);
	}
	if (ReorderOp.Index == ReorderOp.NewIndex)
	{
		return FString::Printf(TEXT("Renderer [%d] already at target index on '%s'"), ReorderOp.Index, *ReorderOp.EmitterName);
	}

	UNiagaraRendererProperties* Renderer = Renderers[ReorderOp.Index];
	const FString RendererType = GetRendererTypeName(Renderer);
	Emitter->MoveRenderer(Renderer, ReorderOp.NewIndex, Handle.GetInstance().Version);
	return FString::Printf(TEXT("Moved %s renderer from index %d to %d on '%s'"),
		*RendererType, ReorderOp.Index, ReorderOp.NewIndex, *ReorderOp.EmitterName);
}

// ========== User Parameter Operations ==========

bool FEditNiagaraTool::ResolveUserParameterType(const FString& TypeStr, FNiagaraTypeDefinition& OutType) const
{
	const FString TypeTrimmed = TypeStr.TrimStartAndEnd();
	const FString TypeLower = TypeTrimmed.ToLower();

	if (TypeLower == TEXT("float"))
	{
		OutType = FNiagaraTypeDefinition::GetFloatDef();
		return true;
	}
	if (TypeLower == TEXT("int") || TypeLower == TEXT("int32") || TypeLower == TEXT("integer"))
	{
		OutType = FNiagaraTypeDefinition::GetIntDef();
		return true;
	}
	if (TypeLower == TEXT("bool") || TypeLower == TEXT("boolean"))
	{
		OutType = FNiagaraTypeDefinition::GetBoolDef();
		return true;
	}
	if (TypeLower == TEXT("vector2") || TypeLower == TEXT("vec2"))
	{
		OutType = FNiagaraTypeDefinition::GetVec2Def();
		return true;
	}
	if (TypeLower == TEXT("vector3") || TypeLower == TEXT("vec3") || TypeLower == TEXT("vector"))
	{
		OutType = FNiagaraTypeDefinition::GetVec3Def();
		return true;
	}
	if (TypeLower == TEXT("vector4") || TypeLower == TEXT("vec4"))
	{
		OutType = FNiagaraTypeDefinition::GetVec4Def();
		return true;
	}
	if (TypeLower == TEXT("linearcolor") || TypeLower == TEXT("color"))
	{
		OutType = FNiagaraTypeDefinition::GetColorDef();
		return true;
	}
	if (TypeLower == TEXT("position"))
	{
		OutType = FNiagaraTypeDefinition::GetPositionDef();
		return true;
	}
	if (TypeLower == TEXT("quat") || TypeLower == TEXT("quaternion"))
	{
		OutType = FNiagaraTypeDefinition::GetQuatDef();
		return true;
	}

	// Resolve through Niagara's registered user-variable types to avoid a fixed primitive-only set.
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	const FName TypeName(*TypeTrimmed);
	if (const TOptional<FNiagaraTypeDefinition> DirectType = FNiagaraTypeRegistry::GetRegisteredTypeByName(TypeName))
	{
		OutType = DirectType.GetValue();
		return true;
	}
#endif

	const TArray<FNiagaraTypeDefinition> UserTypes = FNiagaraTypeRegistry::GetRegisteredUserVariableTypes();
	for (const FNiagaraTypeDefinition& UserType : UserTypes)
	{
		const FString CandidateName = UserType.GetName();
		if (CandidateName.Equals(TypeTrimmed, ESearchCase::IgnoreCase) ||
			CandidateName.Replace(TEXT(" "), TEXT("")).Equals(TypeLower.Replace(TEXT(" "), TEXT("")), ESearchCase::IgnoreCase))
		{
			OutType = UserType;
			return true;
		}
	}

	return false;
}

FString FEditNiagaraTool::AddUserParameter(UNiagaraSystem* System, const FUserParameterDefinition& ParamDef)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FNiagaraTypeDefinition TypeDef;
	if (!ResolveUserParameterType(ParamDef.Type, TypeDef))
	{
		TArray<FString> SupportedTypes;
		const TArray<FNiagaraTypeDefinition> UserTypes = FNiagaraTypeRegistry::GetRegisteredUserVariableTypes();
		SupportedTypes.Reserve(UserTypes.Num());
		for (const FNiagaraTypeDefinition& UserType : UserTypes)
		{
			if (UserType.IsValid())
			{
				SupportedTypes.Add(UserType.GetName());
			}
		}
		SupportedTypes.Sort();

		const FString SupportedMsg = SupportedTypes.Num() > 0
			? FString::Join(SupportedTypes, TEXT(", "))
			: TEXT("No registered user parameter types available");
		return FString::Printf(TEXT("ERROR: Unknown type '%s'. Registered user parameter types: %s"),
			*ParamDef.Type, *SupportedMsg);
	}

	FString ParamName = ParamDef.Name;
	if (!ParamName.StartsWith(TEXT("User.")))
	{
		ParamName = TEXT("User.") + ParamName;
	}

	FNiagaraVariable NewParam(TypeDef, FName(*ParamName));
	NewParam.AllocateData();

	if (ParamDef.DefaultValue.IsValid())
	{
		FString ParseError;
		FNiagaraVariable TempVar;
		if (ParseJsonToNiagaraVariable(TypeDef, ParamDef.DefaultValue, TempVar, ParseError))
		{
			NewParam.SetData(TempVar.GetData());
		}
	}

	FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();

	if (ExposedParams.FindParameterOffset(NewParam) != nullptr)
	{
		return FString::Printf(TEXT("ERROR: User parameter '%s' already exists"), *ParamDef.Name);
	}

	System->Modify();
	ExposedParams.AddParameter(NewParam, true, false);

	return FString::Printf(TEXT("Added user parameter '%s' (%s)"), *ParamDef.Name, *ParamDef.Type);
}

FString FEditNiagaraTool::RemoveUserParameter(UNiagaraSystem* System, const FString& ParamName)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FString FullName = ParamName;
	if (!FullName.StartsWith(TEXT("User.")))
	{
		FullName = TEXT("User.") + FullName;
	}

	FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();

	TArrayView<const FNiagaraVariableWithOffset> Parameters = ExposedParams.ReadParameterVariables();

	FNiagaraVariable FoundParam;
	bool bFound = false;
	for (const FNiagaraVariableWithOffset& ParamWithOffset : Parameters)
	{
		if (ParamWithOffset.GetName().ToString().Equals(FullName, ESearchCase::IgnoreCase))
		{
			FoundParam = FNiagaraVariable(ParamWithOffset.GetType(), ParamWithOffset.GetName());
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		TArray<FString> AvailableParams;
		for (const FNiagaraVariableWithOffset& P : Parameters)
		{
			FString Name = P.GetName().ToString();
			if (Name.StartsWith(TEXT("User.")))
			{
				Name = Name.RightChop(5);
			}
			AvailableParams.Add(Name);
		}
		return FString::Printf(TEXT("ERROR: User parameter '%s' not found. Available: %s"),
			*ParamName, *FString::Join(AvailableParams, TEXT(", ")));
	}

	System->Modify();
	ExposedParams.RemoveParameter(FoundParam);

	return FString::Printf(TEXT("Removed user parameter '%s'"), *ParamName);
}

FString FEditNiagaraTool::SetUserParameterValue(UNiagaraSystem* System, const FUserParameterValue& ParamValue)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FString FullName = ParamValue.Name;
	if (!FullName.StartsWith(TEXT("User.")))
	{
		FullName = TEXT("User.") + FullName;
	}

	FNiagaraUserRedirectionParameterStore& ExposedParams = System->GetExposedParameters();

	TArrayView<const FNiagaraVariableWithOffset> Parameters = ExposedParams.ReadParameterVariables();

	const FNiagaraVariableWithOffset* FoundParam = nullptr;
	for (const FNiagaraVariableWithOffset& ParamWithOffset : Parameters)
	{
		if (ParamWithOffset.GetName().ToString().Equals(FullName, ESearchCase::IgnoreCase))
		{
			FoundParam = &ParamWithOffset;
			break;
		}
	}

	if (!FoundParam)
	{
		TArray<FString> AvailableParams;
		for (const FNiagaraVariableWithOffset& P : Parameters)
		{
			FString Name = P.GetName().ToString();
			if (Name.StartsWith(TEXT("User.")))
			{
				Name = Name.RightChop(5);
			}
			AvailableParams.Add(Name);
		}
		return FString::Printf(TEXT("ERROR: User parameter '%s' not found. Available: %s"),
			*ParamValue.Name, *FString::Join(AvailableParams, TEXT(", ")));
	}

	FNiagaraTypeDefinition TypeDef = FoundParam->GetType();
	FNiagaraVariable TempVar;
	FString ParseError;
	if (!ParseJsonToNiagaraVariable(TypeDef, ParamValue.Value, TempVar, ParseError))
	{
		return FString::Printf(TEXT("ERROR: Failed to parse value for '%s': %s"), *ParamValue.Name, *ParseError);
	}

	System->Modify();
	ExposedParams.SetParameterData(TempVar.GetData(), *FoundParam, true);

	return FString::Printf(TEXT("Set user parameter '%s'"), *ParamValue.Name);
}

// ========== Emitter Property Operations ==========

FString FEditNiagaraTool::SetEmitterProperties(UNiagaraSystem* System, const FEmitterPropertySet& Props)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	int32 EmitterIndex = FindEmitterIndexByName(System, Props.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		TArray<FString> AvailableEmitters;
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			AvailableEmitters.Add(Handle.GetName().ToString());
		}
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found. Available: %s"),
			*Props.EmitterName, *FString::Join(AvailableEmitters, TEXT(", ")));
	}

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not get emitter data for '%s'"), *Props.EmitterName);
	}

	TArray<FString> Changes;

	if (Props.SimTarget.IsSet())
	{
		FString Target = Props.SimTarget.GetValue().ToLower();
		if (Target == TEXT("gpu"))
		{
			const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
			for (UNiagaraRendererProperties* Renderer : Renderers)
			{
				if (Renderer && Renderer->IsA<UNiagaraLightRendererProperties>())
				{
					return FString::Printf(TEXT("ERROR: Cannot set sim target to GPU on '%s' - has Light renderer which only works on CPU"),
						*Props.EmitterName);
				}
			}
			EmitterData->SimTarget = ENiagaraSimTarget::GPUComputeSim;
			Changes.Add(TEXT("SimTarget=GPU"));
		}
		else if (Target == TEXT("cpu"))
		{
			EmitterData->SimTarget = ENiagaraSimTarget::CPUSim;
			Changes.Add(TEXT("SimTarget=CPU"));
		}
		else
		{
			return FString::Printf(TEXT("ERROR: Unknown sim_target '%s'. Use 'cpu' or 'gpu'."), *Props.SimTarget.GetValue());
		}
	}

	if (Props.bLocalSpace.IsSet())
	{
		EmitterData->bLocalSpace = Props.bLocalSpace.GetValue();
		Changes.Add(FString::Printf(TEXT("LocalSpace=%s"), Props.bLocalSpace.GetValue() ? TEXT("true") : TEXT("false")));
	}

	if (Props.bDeterminism.IsSet())
	{
		EmitterData->bDeterminism = Props.bDeterminism.GetValue();
		Changes.Add(FString::Printf(TEXT("Determinism=%s"), Props.bDeterminism.GetValue() ? TEXT("true") : TEXT("false")));
	}

	if (Props.RandomSeed.IsSet())
	{
		EmitterData->RandomSeed = Props.RandomSeed.GetValue();
		Changes.Add(FString::Printf(TEXT("RandomSeed=%d"), Props.RandomSeed.GetValue()));
	}

	if (Changes.Num() == 0)
	{
		return FString::Printf(TEXT("No changes made to emitter '%s'"), *Props.EmitterName);
	}

	System->Modify();

	return FString::Printf(TEXT("Set emitter '%s': %s"), *Props.EmitterName, *FString::Join(Changes, TEXT(", ")));
}

FString FEditNiagaraTool::AddEventHandler(UNiagaraSystem* System, const FEventHandlerDefinition& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
	if (!Source)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' has no editable graph source"), *Def.EmitterName);
	}

	FNiagaraEventScriptProperties EventProps;
	EventProps.SourceEventName = FName(*Def.SourceEventName);

	if (!Def.SourceEmitterName.IsEmpty())
	{
		FGuid SourceEmitterId;
		if (!ResolveEmitterHandleIdByName(System, Def.SourceEmitterName, SourceEmitterId))
		{
			return FString::Printf(TEXT("ERROR: source_emitter '%s' not found"), *Def.SourceEmitterName);
		}
		EventProps.SourceEmitterID = SourceEmitterId;
	}

	if (Def.ExecutionMode.IsSet())
	{
		EventProps.ExecutionMode = static_cast<EScriptExecutionMode>(Def.ExecutionMode.GetValue());
	}
	if (Def.SpawnNumber.IsSet())
	{
		EventProps.SpawnNumber = static_cast<uint32>(FMath::Max(0, Def.SpawnNumber.GetValue()));
	}
	if (Def.MaxEventsPerFrame.IsSet())
	{
		EventProps.MaxEventsPerFrame = static_cast<uint32>(FMath::Max(0, Def.MaxEventsPerFrame.GetValue()));
	}
	if (Def.bRandomSpawnNumber.IsSet())
	{
		EventProps.bRandomSpawnNumber = Def.bRandomSpawnNumber.GetValue();
	}
	if (Def.MinSpawnNumber.IsSet())
	{
		EventProps.MinSpawnNumber = static_cast<uint32>(FMath::Max(0, Def.MinSpawnNumber.GetValue()));
	}
	if (Def.bUpdateAttributeInitialValues.IsSet())
	{
		EventProps.UpdateAttributeInitialValues = Def.bUpdateAttributeInitialValues.GetValue();
	}

	EventProps.Script = NewObject<UNiagaraScript>(
		Emitter,
		MakeUniqueObjectName(Emitter, UNiagaraScript::StaticClass(), TEXT("EventScript")),
		RF_Transactional);
	if (!EventProps.Script)
	{
		return TEXT("ERROR: Failed to create event handler script");
	}

	EventProps.Script->SetUsage(ENiagaraScriptUsage::ParticleEventScript);
	EventProps.Script->SetUsageId(FGuid::NewGuid());
	EventProps.Script->SetLatestSource(Source);

	FString GraphError;
	if (!EnsureNiagaraGraphOutputForUsage(Source, ENiagaraScriptUsage::ParticleEventScript, EventProps.Script->GetUsageId(), GraphError))
	{
		return FString::Printf(TEXT("ERROR: Failed to initialize event handler graph: %s"), *GraphError);
	}

	System->Modify();
	Emitter->Modify();
	Emitter->AddEventHandler(EventProps, Handle.GetInstance().Version);

	return FString::Printf(TEXT("Added event handler '%s' to '%s' (stage_usage_id=%s)"),
		*Def.SourceEventName, *Def.EmitterName, *EventProps.Script->GetUsageId().ToString());
}

FString FEditNiagaraTool::RemoveEventHandler(UNiagaraSystem* System, const FEventHandlerRemoval& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	FNiagaraEventScriptProperties* EventProps = EmitterData->GetEventHandlerByIdUnsafe(Def.UsageId);
	if (!EventProps)
	{
		TArray<FString> AvailableUsageIds;
		for (const FNiagaraEventScriptProperties& ExistingEvent : EmitterData->GetEventHandlers())
		{
			if (ExistingEvent.Script)
			{
				AvailableUsageIds.Add(ExistingEvent.Script->GetUsageId().ToString());
			}
		}
		const FString AvailableText = AvailableUsageIds.Num() > 0 ? FString::Join(AvailableUsageIds, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Event handler stage_usage_id '%s' not found on '%s'. Available: %s"),
			*Def.UsageId.ToString(), *Def.EmitterName, *AvailableText);
	}

	const FString SourceEventName = EventProps->SourceEventName.ToString();
	System->Modify();
	Emitter->Modify();
	Emitter->RemoveEventHandlerByUsageId(Def.UsageId, Handle.GetInstance().Version);

	return FString::Printf(TEXT("Removed event handler '%s' from '%s' (stage_usage_id=%s)"),
		*SourceEventName, *Def.EmitterName, *Def.UsageId.ToString());
}

FString FEditNiagaraTool::SetEventHandler(UNiagaraSystem* System, const FEventHandlerUpdate& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	FNiagaraEventScriptProperties* EventProps = EmitterData->GetEventHandlerByIdUnsafe(Def.UsageId);
	if (!EventProps)
	{
		return FString::Printf(TEXT("ERROR: Event handler stage_usage_id '%s' not found on '%s'"),
			*Def.UsageId.ToString(), *Def.EmitterName);
	}

	TArray<FString> Changes;
	if (Def.ExecutionMode.IsSet())
	{
		EventProps->ExecutionMode = static_cast<EScriptExecutionMode>(Def.ExecutionMode.GetValue());
		Changes.Add(FString::Printf(TEXT("execution_mode=%d"), Def.ExecutionMode.GetValue()));
	}
	if (Def.SpawnNumber.IsSet())
	{
		EventProps->SpawnNumber = static_cast<uint32>(FMath::Max(0, Def.SpawnNumber.GetValue()));
		Changes.Add(FString::Printf(TEXT("spawn_number=%u"), EventProps->SpawnNumber));
	}
	if (Def.MaxEventsPerFrame.IsSet())
	{
		EventProps->MaxEventsPerFrame = static_cast<uint32>(FMath::Max(0, Def.MaxEventsPerFrame.GetValue()));
		Changes.Add(FString::Printf(TEXT("max_events_per_frame=%u"), EventProps->MaxEventsPerFrame));
	}
	if (Def.bRandomSpawnNumber.IsSet())
	{
		EventProps->bRandomSpawnNumber = Def.bRandomSpawnNumber.GetValue();
		Changes.Add(FString::Printf(TEXT("random_spawn_number=%s"), EventProps->bRandomSpawnNumber ? TEXT("true") : TEXT("false")));
	}
	if (Def.MinSpawnNumber.IsSet())
	{
		EventProps->MinSpawnNumber = static_cast<uint32>(FMath::Max(0, Def.MinSpawnNumber.GetValue()));
		Changes.Add(FString::Printf(TEXT("min_spawn_number=%u"), EventProps->MinSpawnNumber));
	}
	if (Def.bUpdateAttributeInitialValues.IsSet())
	{
		EventProps->UpdateAttributeInitialValues = Def.bUpdateAttributeInitialValues.GetValue();
		Changes.Add(FString::Printf(TEXT("update_attribute_initial_values=%s"), EventProps->UpdateAttributeInitialValues ? TEXT("true") : TEXT("false")));
	}
	if (!Def.SourceEventName.IsEmpty())
	{
		EventProps->SourceEventName = FName(*Def.SourceEventName);
		Changes.Add(FString::Printf(TEXT("source_event_name=%s"), *Def.SourceEventName));
	}
	if (!Def.SourceEmitterName.IsEmpty())
	{
		FGuid SourceEmitterId;
		if (!ResolveEmitterHandleIdByName(System, Def.SourceEmitterName, SourceEmitterId))
		{
			return FString::Printf(TEXT("ERROR: source_emitter '%s' not found"), *Def.SourceEmitterName);
		}
		EventProps->SourceEmitterID = SourceEmitterId;
		Changes.Add(FString::Printf(TEXT("source_emitter=%s"), *Def.SourceEmitterName));
	}

	if (Changes.Num() == 0)
	{
		return FString::Printf(TEXT("No event handler changes for '%s' (stage_usage_id=%s)"), *Def.EmitterName, *Def.UsageId.ToString());
	}

	System->Modify();
	Emitter->Modify();
	return FString::Printf(TEXT("Updated event handler on '%s' (stage_usage_id=%s): %s"),
		*Def.EmitterName, *Def.UsageId.ToString(), *FString::Join(Changes, TEXT(", ")));
}

FString FEditNiagaraTool::AddSimulationStage(UNiagaraSystem* System, const FSimulationStageDefinition& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(EmitterData->GraphSource);
	if (!Source)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' has no editable graph source"), *Def.EmitterName);
	}

	FString ResolveError;
	UClass* StageClass = ResolveSimulationStageClassByName(Def.StageClass, ResolveError);
	if (!StageClass)
	{
		return FString::Printf(TEXT("ERROR: %s"), *ResolveError);
	}

	UNiagaraSimulationStageBase* NewStage = NewObject<UNiagaraSimulationStageBase>(Emitter, StageClass, NAME_None, RF_Transactional);
	if (!NewStage)
	{
		return TEXT("ERROR: Failed to create simulation stage object");
	}

	NewStage->Script = NewObject<UNiagaraScript>(
		NewStage,
		MakeUniqueObjectName(NewStage, UNiagaraScript::StaticClass(), TEXT("SimulationStage")),
		RF_Transactional);
	if (!NewStage->Script)
	{
		return TEXT("ERROR: Failed to create simulation stage script");
	}

	NewStage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
	NewStage->Script->SetUsageId(FGuid::NewGuid());
	NewStage->Script->SetLatestSource(Source);

	if (!Def.Name.IsEmpty())
	{
		NewStage->SimulationStageName = FName(*Def.Name);
	}
	if (Def.bEnabled.IsSet())
	{
		NewStage->bEnabled = Def.bEnabled.GetValue() ? 1u : 0u;
	}

	FString GraphError;
	if (!EnsureNiagaraGraphOutputForUsage(Source, ENiagaraScriptUsage::ParticleSimulationStageScript, NewStage->Script->GetUsageId(), GraphError))
	{
		return FString::Printf(TEXT("ERROR: Failed to initialize simulation stage graph: %s"), *GraphError);
	}

	System->Modify();
	Emitter->Modify();
	Emitter->AddSimulationStage(NewStage, Handle.GetInstance().Version);
	if (Def.Index != INDEX_NONE)
	{
		Emitter->MoveSimulationStageToIndex(NewStage, Def.Index, Handle.GetInstance().Version);
	}

	const FString StageName = NewStage->SimulationStageName.IsNone() ? NewStage->GetName() : NewStage->SimulationStageName.ToString();
	return FString::Printf(TEXT("Added simulation stage '%s' to '%s' (stage_usage_id=%s)"),
		*StageName, *Def.EmitterName, *NewStage->Script->GetUsageId().ToString());
}

FString FEditNiagaraTool::RemoveSimulationStage(UNiagaraSystem* System, const FSimulationStageRemoval& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	UNiagaraSimulationStageBase* SimStage = EmitterData->GetSimulationStageById(Def.UsageId);
	if (!SimStage)
	{
		TArray<FString> AvailableUsageIds;
		for (UNiagaraSimulationStageBase* ExistingStage : EmitterData->GetSimulationStages())
		{
			if (ExistingStage && ExistingStage->Script)
			{
				AvailableUsageIds.Add(ExistingStage->Script->GetUsageId().ToString());
			}
		}
		const FString AvailableText = AvailableUsageIds.Num() > 0 ? FString::Join(AvailableUsageIds, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Simulation stage stage_usage_id '%s' not found on '%s'. Available: %s"),
			*Def.UsageId.ToString(), *Def.EmitterName, *AvailableText);
	}

	const FString StageName = SimStage->SimulationStageName.IsNone() ? SimStage->GetName() : SimStage->SimulationStageName.ToString();
	System->Modify();
	Emitter->Modify();
	Emitter->RemoveSimulationStage(SimStage, Handle.GetInstance().Version);

	return FString::Printf(TEXT("Removed simulation stage '%s' from '%s' (stage_usage_id=%s)"),
		*StageName, *Def.EmitterName, *Def.UsageId.ToString());
}

FString FEditNiagaraTool::SetSimulationStage(UNiagaraSystem* System, const FSimulationStageUpdate& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	UNiagaraSimulationStageBase* SimStage = EmitterData->GetSimulationStageById(Def.UsageId);
	if (!SimStage)
	{
		return FString::Printf(TEXT("ERROR: Simulation stage stage_usage_id '%s' not found on '%s'"),
			*Def.UsageId.ToString(), *Def.EmitterName);
	}

	TArray<FString> Changes;
	if (!Def.Name.IsEmpty())
	{
		SimStage->SimulationStageName = FName(*Def.Name);
		Changes.Add(FString::Printf(TEXT("name=%s"), *Def.Name));
	}
	if (Def.bEnabled.IsSet())
	{
		SimStage->bEnabled = Def.bEnabled.GetValue() ? 1u : 0u;
		Changes.Add(FString::Printf(TEXT("enabled=%s"), Def.bEnabled.GetValue() ? TEXT("true") : TEXT("false")));
	}

	if (Changes.Num() == 0)
	{
		return FString::Printf(TEXT("No simulation stage changes for '%s' (stage_usage_id=%s)"), *Def.EmitterName, *Def.UsageId.ToString());
	}

	System->Modify();
	Emitter->Modify();
	return FString::Printf(TEXT("Updated simulation stage on '%s' (stage_usage_id=%s): %s"),
		*Def.EmitterName, *Def.UsageId.ToString(), *FString::Join(Changes, TEXT(", ")));
}

FString FEditNiagaraTool::ReorderSimulationStage(UNiagaraSystem* System, const FSimulationStageReorder& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter data for '%s'"), *Def.EmitterName);
	}

	UNiagaraSimulationStageBase* SimStage = EmitterData->GetSimulationStageById(Def.UsageId);
	if (!SimStage)
	{
		return FString::Printf(TEXT("ERROR: Simulation stage stage_usage_id '%s' not found on '%s'"),
			*Def.UsageId.ToString(), *Def.EmitterName);
	}

	const int32 NumStages = EmitterData->GetSimulationStages().Num();
	if (Def.NewIndex < 0 || Def.NewIndex >= NumStages)
	{
		return FString::Printf(TEXT("ERROR: new_index %d out of range [0, %d] for '%s'"),
			Def.NewIndex, FMath::Max(0, NumStages - 1), *Def.EmitterName);
	}

	System->Modify();
	Emitter->Modify();
	Emitter->MoveSimulationStageToIndex(SimStage, Def.NewIndex, Handle.GetInstance().Version);

	const FString StageName = SimStage->SimulationStageName.IsNone() ? SimStage->GetName() : SimStage->SimulationStageName.ToString();
	return FString::Printf(TEXT("Moved simulation stage '%s' on '%s' to index %d"),
		*StageName, *Def.EmitterName, Def.NewIndex);
}

FString FEditNiagaraTool::SetModuleEnabled(UNiagaraSystem* System, const FModuleEnableOp& Op)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const bool bSystemUsage = IsSystemStageUsage(Op.Usage);
	int32 EmitterIndex = INDEX_NONE;
	if (!bSystemUsage)
	{
		EmitterIndex = FindEmitterIndexByName(System, Op.EmitterName);
		if (EmitterIndex == INDEX_NONE)
		{
			TArray<FString> AvailableEmitters;
			for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
			{
				AvailableEmitters.Add(Handle.GetName().ToString());
			}
			return FString::Printf(TEXT("ERROR: Emitter '%s' not found. Available: %s"),
				*Op.EmitterName, *FString::Join(AvailableEmitters, TEXT(", ")));
		}
	}

	UNiagaraNodeOutput* OutputNode = GetOutputNodeForUsage(System, EmitterIndex, Op.Usage, Op.UsageId);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not get output node for %s in '%s'"),
			*UsageToString(Op.Usage), bSystemUsage ? TEXT("system") : *Op.EmitterName);
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(OutputNode, Op.ModuleName);
	if (!ModuleNode)
	{
		TArray<FString> AvailableModules = ListModulesInStack(OutputNode);
		FString ModuleList = AvailableModules.Num() > 0 ? FString::Join(AvailableModules, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Module '%s' not found in %s/%s. Available modules: %s"),
			*Op.ModuleName, bSystemUsage ? TEXT("System") : *Op.EmitterName, *UsageToString(Op.Usage), *ModuleList);
	}

	FString ActualModuleName = ModuleNode->GetFunctionName();
	bool bWasEnabled = ModuleNode->IsNodeEnabled();

	if (bWasEnabled == Op.bEnabled)
	{
		return FString::Printf(TEXT("Module '%s' was already %s"),
			*ActualModuleName, Op.bEnabled ? TEXT("enabled") : TEXT("disabled"));
	}

	FNiagaraStackGraphUtilities::SetModuleIsEnabled(*ModuleNode, Op.bEnabled);

	return FString::Printf(TEXT("%s module '%s' in %s/%s"),
		Op.bEnabled ? TEXT("Enabled") : TEXT("Disabled"),
		*ActualModuleName, bSystemUsage ? TEXT("System") : *Op.EmitterName, *UsageToString(Op.Usage));
}

bool FEditNiagaraTool::ResolveReflectedPropertyTargetObject(UNiagaraSystem* System, const FReflectedPropertyTarget& Def, UObject*& OutObject, FString& OutLabel, FString& OutError)
{
	OutObject = nullptr;
	OutLabel.Empty();

	if (!System)
	{
		OutError = TEXT("System is null");
		return false;
	}

	if (Def.Target == TEXT("system"))
	{
		OutObject = System;
		OutLabel = FString::Printf(TEXT("system '%s'"), *System->GetName());
		return true;
	}

	if (Def.Target == TEXT("baker_settings"))
	{
		UNiagaraBakerSettings* BakerSettings = System->GetBakerSettings();
		if (!BakerSettings)
		{
			OutError = TEXT("System has no baker settings object");
			return false;
		}
		OutObject = BakerSettings;
		OutLabel = FString::Printf(TEXT("baker_settings for system '%s'"), *System->GetName());
		return true;
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		OutError = FString::Printf(TEXT("Emitter '%s' not found"), *Def.EmitterName);
		return false;
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	if (!Emitter || !EmitterData)
	{
		OutError = FString::Printf(TEXT("Could not resolve emitter data for '%s'"), *Def.EmitterName);
		return false;
	}

	if (Def.Target == TEXT("emitter"))
	{
		OutObject = Emitter;
		OutLabel = FString::Printf(TEXT("emitter '%s'"), *Def.EmitterName);
		return true;
	}

	if (Def.Target == TEXT("renderer"))
	{
		const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
		if (Def.RendererIndex < 0 || Def.RendererIndex >= Renderers.Num())
		{
			OutError = FString::Printf(TEXT("renderer_index %d out of range [0, %d] for '%s'"),
				Def.RendererIndex, FMath::Max(0, Renderers.Num() - 1), *Def.EmitterName);
			return false;
		}
		OutObject = Renderers[Def.RendererIndex];
		OutLabel = FString::Printf(TEXT("renderer[%d] on emitter '%s'"), Def.RendererIndex, *Def.EmitterName);
		if (!OutObject)
		{
			OutError = FString::Printf(TEXT("renderer[%d] on '%s' is null"), Def.RendererIndex, *Def.EmitterName);
			return false;
		}
		return true;
	}

	if (Def.Target == TEXT("simulation_stage"))
	{
		UNiagaraSimulationStageBase* Stage = EmitterData->GetSimulationStageById(Def.UsageId);
		if (!Stage)
		{
			OutError = FString::Printf(TEXT("Simulation stage '%s' not found on '%s'"),
				*Def.UsageId.ToString(), *Def.EmitterName);
			return false;
		}
		OutObject = Stage;
		OutLabel = FString::Printf(TEXT("simulation_stage '%s' on emitter '%s'"),
			*Def.UsageId.ToString(), *Def.EmitterName);
		return true;
	}

	OutError = FString::Printf(TEXT("Unsupported reflected target '%s'"), *Def.Target);
	return false;
}

FString FEditNiagaraTool::ListReflectedObjectProperties(UObject* TargetObject, const FString& TargetLabel) const
{
	if (!TargetObject)
	{
		return FString::Printf(TEXT("ERROR: Invalid reflected target (%s)"), *TargetLabel);
	}

	FString Output = FString::Printf(TEXT("# REFLECTED_PROPERTIES %s class=%s\n"), *TargetLabel, *TargetObject->GetClass()->GetName());
	int32 Count = 0;

	for (TFieldIterator<FProperty> It(TargetObject->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property)
		{
			continue;
		}

		const bool bEditable = Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);
		if (!bEditable || Property->HasAnyPropertyFlags(CPF_Deprecated | CPF_Transient))
		{
			continue;
		}

		Output += FString::Printf(TEXT("%s\t%s\n"), *Property->GetName(), *Property->GetClass()->GetName());
		++Count;
	}

	if (Count == 0)
	{
		Output += TEXT("(no editable reflected properties found)\n");
	}

	return Output;
}

FString FEditNiagaraTool::SetReflectedObjectProperties(UObject* TargetObject, const FString& TargetLabel, const TSharedPtr<FJsonObject>& Properties)
{
	if (!TargetObject)
	{
		return FString::Printf(TEXT("ERROR: Invalid reflected target (%s)"), *TargetLabel);
	}
	if (!Properties.IsValid() || Properties->Values.Num() == 0)
	{
		return FString::Printf(TEXT("ERROR: No properties provided for %s"), *TargetLabel);
	}

	TargetObject->Modify();
	TArray<FString> Applied;

	for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Properties->Values)
	{
		FProperty* Property = TargetObject->GetClass()->FindPropertyByName(FName(*Pair.Key));
		if (!Property)
		{
			for (TFieldIterator<FProperty> It(TargetObject->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
			{
				if ((*It) && (*It)->GetName().Equals(Pair.Key, ESearchCase::IgnoreCase))
				{
					Property = *It;
					break;
				}
			}
		}
		if (!Property)
		{
			return FString::Printf(TEXT("ERROR: Property '%s' not found on %s"), *Pair.Key, *TargetLabel);
		}
		if (!Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible))
		{
			return FString::Printf(TEXT("ERROR: Property '%s' is not editable on %s"), *Property->GetName(), *TargetLabel);
		}

		void* ValuePtr = Property->ContainerPtrToValuePtr<void>(TargetObject);
		FString PropError;
		if (!SetPropertyFromJsonValue(Property, ValuePtr, Pair.Value, PropError))
		{
			return FString::Printf(TEXT("ERROR: Failed setting '%s' on %s: %s"), *Property->GetName(), *TargetLabel, *PropError);
		}
		Applied.Add(Property->GetName());
	}

	if (Applied.Num() == 0)
	{
		return FString::Printf(TEXT("No reflected properties changed for %s"), *TargetLabel);
	}

	TargetObject->PostEditChange();
	TargetObject->MarkPackageDirty();
	return FString::Printf(TEXT("Set reflected properties on %s: %s"), *TargetLabel, *FString::Join(Applied, TEXT(", ")));
}

FString FEditNiagaraTool::ListVersions(UNiagaraSystem* System) const
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FString Output = FString::Printf(TEXT("# NIAGARA_VERSIONS %s\n"), *System->GetName());
	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	Output += FString::Printf(TEXT("emitters=%d\n"), Handles.Num());

	for (const FNiagaraEmitterHandle& Handle : Handles)
	{
		UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
		if (!Emitter)
		{
			continue;
		}

		Output += FString::Printf(TEXT("\n## EMITTER %s\n"), *Handle.GetName().ToString());
		Output += FString::Printf(TEXT("versioning=%s\n"), Emitter->IsVersioningEnabled() ? TEXT("true") : TEXT("false"));
		if (Emitter->IsVersioningEnabled())
		{
			const FNiagaraAssetVersion Exposed = Emitter->GetExposedVersion();
			Output += FString::Printf(TEXT("exposed=%s (%d.%d)\n"),
				*Exposed.VersionGuid.ToString(), Exposed.MajorVersion, Exposed.MinorVersion);
			for (const FNiagaraAssetVersion& Version : Emitter->GetAllAvailableVersions())
			{
				const bool bIsExposed = Version.VersionGuid == Exposed.VersionGuid;
				Output += FString::Printf(TEXT("  - %s\tv%d.%d%s\n"),
					*Version.VersionGuid.ToString(), Version.MajorVersion, Version.MinorVersion, bIsExposed ? TEXT(" (exposed)") : TEXT(""));
			}
		}
	}

	return Output;
#else
	return TEXT("ERROR: Version listing requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::SetModuleScriptVersion(UNiagaraSystem* System, const FModuleVersionSet& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const bool bSystemUsage = IsSystemStageUsage(Def.Usage);
	int32 EmitterIndex = INDEX_NONE;
	if (!bSystemUsage)
	{
		EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
		if (EmitterIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
		}
	}

	UNiagaraNodeOutput* OutputNode = GetOutputNodeForUsage(System, EmitterIndex, Def.Usage, Def.UsageId);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve output node for stage '%s'"), *UsageToString(Def.Usage));
	}
	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(OutputNode, Def.ModuleName);
	if (!ModuleNode)
	{
		return FString::Printf(TEXT("ERROR: Module '%s' not found in %s/%s"), *Def.ModuleName, bSystemUsage ? TEXT("System") : *Def.EmitterName, *UsageToString(Def.Usage));
	}
	if (!ModuleNode->FunctionScript)
	{
		return FString::Printf(TEXT("ERROR: Module '%s' has no function script"), *Def.ModuleName);
	}
	if (!ModuleNode->FunctionScript->IsVersioningEnabled())
	{
		return FString::Printf(TEXT("ERROR: Module '%s' script does not have versioning enabled"), *Def.ModuleName);
	}
	const FNiagaraAssetVersion* TargetVersion = ModuleNode->FunctionScript->FindVersionData(Def.VersionGuid);
	if (!TargetVersion)
	{
		return FString::Printf(TEXT("ERROR: version_guid '%s' not found on script '%s'"), *Def.VersionGuid.ToString(), *ModuleNode->FunctionScript->GetName());
	}

	ModuleNode->Modify();
	FNiagaraScriptVersionUpgradeContext UpgradeContext;
	ModuleNode->ChangeScriptVersion(Def.VersionGuid, UpgradeContext, true, false);
	return FString::Printf(TEXT("Set module '%s' script version to %s in %s/%s"),
		*ModuleNode->GetFunctionName(), *Def.VersionGuid.ToString(), bSystemUsage ? TEXT("System") : *Def.EmitterName, *UsageToString(Def.Usage));
}

FString FEditNiagaraTool::SetEmitterVersion(UNiagaraSystem* System, const FEmitterVersionSet& Def)
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const int32 EmitterIndex = FindEmitterIndexByName(System, Def.EmitterName);
	if (EmitterIndex == INDEX_NONE)
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' not found"), *Def.EmitterName);
	}

	FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EmitterIndex];
	UNiagaraEmitter* Emitter = Handle.GetInstance().Emitter;
	if (!Emitter)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve emitter '%s'"), *Def.EmitterName);
	}
	if (!Emitter->IsVersioningEnabled())
	{
		return FString::Printf(TEXT("ERROR: Emitter '%s' does not have versioning enabled"), *Def.EmitterName);
	}
	if (!Emitter->FindVersionData(Def.VersionGuid))
	{
		return FString::Printf(TEXT("ERROR: version_guid '%s' not found on emitter '%s'"), *Def.VersionGuid.ToString(), *Def.EmitterName);
	}

	System->Modify();
	if (!System->ChangeEmitterVersion(Handle.GetInstance(), Def.VersionGuid))
	{
		return FString::Printf(TEXT("ERROR: Failed changing emitter '%s' to version '%s'"), *Def.EmitterName, *Def.VersionGuid.ToString());
	}

	return FString::Printf(TEXT("Set emitter '%s' to version %s"), *Def.EmitterName, *Def.VersionGuid.ToString());
#else
	return TEXT("ERROR: set_emitter_version requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::ListParameterDefinitions(UNiagaraSystem* System) const
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FString Output = FString::Printf(TEXT("# PARAMETER_DEFINITIONS %s\n"), *System->GetName());

	const TArray<UNiagaraParameterDefinitionsBase*> Subscribed = System->GetSubscribedParameterDefinitions();
	Output += FString::Printf(TEXT("subscribed=%d\n"), Subscribed.Num());
	for (UNiagaraParameterDefinitionsBase* DefBase : Subscribed)
	{
		if (!DefBase) continue;
		const FGuid DefId = DefBase->GetDefinitionsUniqueId();
		Output += FString::Printf(TEXT("  - %s\t%s\n"), *DefBase->GetPathName(), *DefId.ToString());
	}

	TArray<UNiagaraParameterDefinitions*> AvailableDefs;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FNiagaraEditorUtilities::GetAvailableParameterDefinitions(AvailableDefs);
#endif
	Output += FString::Printf(TEXT("available=%d\n"), AvailableDefs.Num());
	for (UNiagaraParameterDefinitions* Def : AvailableDefs)
	{
		if (!Def) continue;
		Output += FString::Printf(TEXT("  - %s\t%s\n"), *Def->GetPathName(), *Def->GetDefinitionsUniqueId().ToString());
	}

	return Output;
#else
	return TEXT("ERROR: Parameter definitions require editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::SubscribeParameterDefinitions(UNiagaraSystem* System, const FParameterDefinitionsOp& Def)
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	UNiagaraParameterDefinitionsBase* Definitions = nullptr;
	if (!Def.AssetPath.IsEmpty())
	{
		Definitions = LoadObject<UNiagaraParameterDefinitionsBase>(nullptr, *Def.AssetPath);
	}
	if (!Definitions && Def.DefinitionsId.IsValid())
	{
		TArray<UNiagaraParameterDefinitions*> AvailableDefs;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		FNiagaraEditorUtilities::GetAvailableParameterDefinitions(AvailableDefs);
#endif
		for (UNiagaraParameterDefinitions* Candidate : AvailableDefs)
		{
			if (Candidate && Candidate->GetDefinitionsUniqueId() == Def.DefinitionsId)
			{
				Definitions = Candidate;
				break;
			}
		}
	}
	if (!Definitions)
	{
		return FString::Printf(TEXT("ERROR: Parameter definitions not found (asset_path='%s', definitions_id='%s')"),
			*Def.AssetPath, *Def.DefinitionsId.ToString());
	}

	System->Modify();
	System->SubscribeToParameterDefinitions(Definitions, true);
	return FString::Printf(TEXT("Subscribed parameter definitions: %s (%s)"), *Definitions->GetPathName(), *Definitions->GetDefinitionsUniqueId().ToString());
#else
	return TEXT("ERROR: subscribe_parameter_definitions requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::UnsubscribeParameterDefinitions(UNiagaraSystem* System, const FParameterDefinitionsOp& Def)
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FGuid DefinitionsId = Def.DefinitionsId;
	if (!DefinitionsId.IsValid() && !Def.AssetPath.IsEmpty())
	{
		if (UNiagaraParameterDefinitionsBase* Definitions = LoadObject<UNiagaraParameterDefinitionsBase>(nullptr, *Def.AssetPath))
		{
			DefinitionsId = Definitions->GetDefinitionsUniqueId();
		}
	}
	if (!DefinitionsId.IsValid())
	{
		return TEXT("ERROR: unsubscribe_parameter_definitions requires a valid definitions id or resolvable asset path");
	}

	System->Modify();
	System->UnsubscribeFromParameterDefinitions(DefinitionsId);
	return FString::Printf(TEXT("Unsubscribed parameter definitions id=%s"), *DefinitionsId.ToString());
#else
	return TEXT("ERROR: unsubscribe_parameter_definitions requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::SynchronizeParameterDefinitions(UNiagaraSystem* System)
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	System->Modify();
	FSynchronizeWithParameterDefinitionsArgs Args;
	System->SynchronizeWithParameterDefinitions(Args);
	return TEXT("Synchronized parameter definitions for system and owned emitters");
#else
	return TEXT("ERROR: synchronize_parameter_definitions requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::ListValidationRuleSets(UNiagaraSystem* System) const
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FString Output = FString::Printf(TEXT("# VALIDATION_RULE_SETS %s\n"), *System->GetName());
	const UNiagaraEditorSettings* EditorSettings = GetDefault<UNiagaraEditorSettings>();
	int32 DefaultRuleSets = 0;
	if (EditorSettings)
	{
		for (const TSoftObjectPtr<UNiagaraValidationRuleSet>& RuleSetPtr : EditorSettings->DefaultValidationRuleSets)
		{
			if (const UNiagaraValidationRuleSet* RuleSet = RuleSetPtr.LoadSynchronous())
			{
				++DefaultRuleSets;
				Output += FString::Printf(TEXT("default\t%s\trules=%d\n"), *RuleSet->GetPathName(), RuleSet->ValidationRules.Num());
			}
		}
	}
	Output += FString::Printf(TEXT("default_count=%d\n"), DefaultRuleSets);

	if (UNiagaraEffectType* EffectType = System->GetEffectType())
	{
		Output += FString::Printf(TEXT("effect_type=%s\n"), *EffectType->GetPathName());
		int32 EffectTypeRuleSets = 0;
		for (UNiagaraValidationRuleSet* RuleSet : EffectType->ValidationRuleSets)
		{
			if (RuleSet)
			{
				++EffectTypeRuleSets;
				Output += FString::Printf(TEXT("effect_type\t%s\trules=%d\n"), *RuleSet->GetPathName(), RuleSet->ValidationRules.Num());
			}
		}
		Output += FString::Printf(TEXT("effect_type_count=%d\n"), EffectTypeRuleSets);
	}
	else
	{
		Output += TEXT("effect_type=(none)\n");
	}

	Output += FString::Printf(TEXT("has_validation_rules=%s\n"), NiagaraValidation::HasValidationRules(System) ? TEXT("true") : TEXT("false"));
	return Output;
#else
	return TEXT("ERROR: list_validation_rule_sets requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::RunValidation(UNiagaraSystem* System)
{
#if WITH_EDITORONLY_DATA
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	FNiagaraSystemViewModelOptions Options;
	Options.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
	Options.bCanAutoCompile = false;
	Options.bCanSimulate = false;
	Options.bCanModifyEmittersFromTimeline = false;
	Options.bIsForDataProcessingOnly = true;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	Options.bCompileForEdit = false;
#endif

	TSharedRef<FNiagaraSystemViewModel> SysViewModel = MakeShared<FNiagaraSystemViewModel>();
	SysViewModel->Initialize(*System, Options);

	TArray<FString> Results;
	NiagaraValidation::ValidateAllRulesInSystem(SysViewModel,
		[&Results](const FNiagaraValidationResult& Result)
		{
			Results.Add(FString::Printf(TEXT("[%s] %s - %s"),
				*StaticEnum<ENiagaraValidationSeverity>()->GetNameStringByValue((int64)Result.Severity),
				*Result.SummaryText.ToString(),
				*Result.Description.ToString()));
		});

	FString Output = FString::Printf(TEXT("# VALIDATION_RESULTS %s\n"), *System->GetName());
	Output += FString::Printf(TEXT("issues=%d\n"), Results.Num());
	for (const FString& Entry : Results)
	{
		Output += FString::Printf(TEXT("- %s\n"), *Entry);
	}

	return Output;
#else
	return TEXT("ERROR: run_validation requires editor-only Niagara data");
#endif
}

FString FEditNiagaraTool::SetSystemProperties(UNiagaraSystem* System, const FSystemPropertySet& Props)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	TArray<FString> Changes;
	UClass* SystemClass = System->GetClass();
	auto SetBoolPropertyByName = [&](const TCHAR* PropertyName, bool bValue) -> bool
	{
		FBoolProperty* BoolProp = FindFProperty<FBoolProperty>(SystemClass, PropertyName);
		if (!BoolProp)
		{
			return false;
		}
		void* ValuePtr = BoolProp->ContainerPtrToValuePtr<void>(System);
		BoolProp->SetPropertyValue(ValuePtr, bValue);
		return true;
	};
	auto SetIntPropertyByName = [&](const TCHAR* PropertyName, int32 IntValue) -> bool
	{
		FIntProperty* IntProp = FindFProperty<FIntProperty>(SystemClass, PropertyName);
		if (!IntProp)
		{
			return false;
		}
		void* ValuePtr = IntProp->ContainerPtrToValuePtr<void>(System);
		IntProp->SetPropertyValue(ValuePtr, IntValue);
		return true;
	};

	System->Modify();

	if (Props.WarmupTime.IsSet())
	{
		System->SetWarmupTime(Props.WarmupTime.GetValue());
		Changes.Add(FString::Printf(TEXT("WarmupTime=%.2fs"), Props.WarmupTime.GetValue()));
	}

	if (Props.bDeterminism.IsSet())
	{
		if (SetBoolPropertyByName(TEXT("bDeterminism"), Props.bDeterminism.GetValue()))
		{
			Changes.Add(FString::Printf(TEXT("Determinism=%s"), Props.bDeterminism.GetValue() ? TEXT("true") : TEXT("false")));
		}
		else
		{
			Changes.Add(TEXT("Determinism=(property not found)"));
		}
	}

	if (Props.RandomSeed.IsSet())
	{
		if (SetIntPropertyByName(TEXT("RandomSeed"), Props.RandomSeed.GetValue()))
		{
			Changes.Add(FString::Printf(TEXT("RandomSeed=%d"), Props.RandomSeed.GetValue()));
		}
		else
		{
			Changes.Add(TEXT("RandomSeed=(property not found)"));
		}
	}

	if (Changes.Num() == 0)
	{
		return TEXT("No system properties changed");
	}

	return FString::Printf(TEXT("Set system properties: %s"), *FString::Join(Changes, TEXT(", ")));
}

FString FEditNiagaraTool::MoveModule(UNiagaraSystem* System, const FModuleMoveOp& Op)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	const bool bSourceSystem = IsSystemStageUsage(Op.SourceUsage);
	const bool bTargetSystem = IsSystemStageUsage(Op.TargetUsage);

	int32 SourceEmitterIndex = INDEX_NONE;
	if (!bSourceSystem)
	{
		SourceEmitterIndex = FindEmitterIndexByName(System, Op.SourceEmitterName);
		if (SourceEmitterIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: Source emitter '%s' not found"), *Op.SourceEmitterName);
		}
	}

	int32 TargetEmitterIndex = INDEX_NONE;
	if (!bTargetSystem)
	{
		TargetEmitterIndex = FindEmitterIndexByName(System, Op.TargetEmitterName);
		if (TargetEmitterIndex == INDEX_NONE)
		{
			return FString::Printf(TEXT("ERROR: Target emitter '%s' not found"), *Op.TargetEmitterName);
		}
	}

	UNiagaraNodeOutput* SourceOutputNode = GetOutputNodeForUsage(System, SourceEmitterIndex, Op.SourceUsage, Op.SourceUsageId);
	if (!SourceOutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve source output node for %s"), *UsageToString(Op.SourceUsage));
	}
	UNiagaraNodeOutput* TargetOutputNode = GetOutputNodeForUsage(System, TargetEmitterIndex, Op.TargetUsage, Op.TargetUsageId);
	if (!TargetOutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve target output node for %s"), *UsageToString(Op.TargetUsage));
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(SourceOutputNode, Op.ModuleName);
	if (!ModuleNode)
	{
		return FString::Printf(TEXT("ERROR: Source module '%s' not found"), *Op.ModuleName);
	}

	if (!ModuleNode->FunctionScript)
	{
		return TEXT("ERROR: Source module does not reference a function script");
	}

	UNiagaraNodeFunctionCall* CopiedModule = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		ModuleNode->FunctionScript,
		*TargetOutputNode,
		Op.TargetIndex,
		ModuleNode->GetFunctionName(),
		ModuleNode->SelectedScriptVersion);
	if (!CopiedModule)
	{
		return TEXT("ERROR: Move module failed while adding target module");
	}

	// Engine-internal MoveModule() is not exported from NiagaraEditor in 5.7.
	// Emulate move by copy+remove when requested.
	if (!Op.bForceCopy)
	{
		FModuleRemoval Removal;
		Removal.EmitterName = Op.SourceEmitterName;
		Removal.Usage = Op.SourceUsage;
		Removal.ModuleName = ModuleNode->GetFunctionName();
		const FString RemoveResult = RemoveModule(System, Removal);
		if (RemoveResult.StartsWith(TEXT("ERROR:")))
		{
			return FString::Printf(TEXT("ERROR: Copied module but failed to remove source module: %s"), *RemoveResult);
		}
		return FString::Printf(TEXT("Moved module '%s' to %s/%s at index %d"),
			*CopiedModule->GetFunctionName(),
			bTargetSystem ? TEXT("System") : *Op.TargetEmitterName,
			*UsageToString(Op.TargetUsage),
			Op.TargetIndex);
	}

	return FString::Printf(TEXT("Copied module '%s' to %s/%s at index %d"),
		*CopiedModule->GetFunctionName(),
		bTargetSystem ? TEXT("System") : *Op.TargetEmitterName,
		*UsageToString(Op.TargetUsage),
		Op.TargetIndex);
}

FString FEditNiagaraTool::CreateScratchPadScript(UNiagaraSystem* System, const FScratchPadScriptDefinition& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	System->Modify();
	UNiagaraScript* NewScript = nullptr;
	if (!Def.DuplicateFrom.IsEmpty())
	{
		UNiagaraScript* ExistingScript = LoadObject<UNiagaraScript>(nullptr, *Def.DuplicateFrom);
		if (!ExistingScript)
		{
			for (UNiagaraScript* Script : System->ScratchPadScripts)
			{
				if (Script && Script->GetName().Equals(Def.DuplicateFrom, ESearchCase::IgnoreCase))
				{
					ExistingScript = Script;
					break;
				}
			}
		}
		if (!ExistingScript)
		{
			return FString::Printf(TEXT("ERROR: duplicate_from script not found: %s"), *Def.DuplicateFrom);
		}

		NewScript = Cast<UNiagaraScript>(StaticDuplicateObject(ExistingScript, System, FName(*Def.Name)));
	}
	else
	{
		NewScript = NewObject<UNiagaraScript>(System, FName(*Def.Name), RF_Transactional);
		if (!NewScript)
		{
			return TEXT("ERROR: Failed to allocate scratch pad script");
		}

		const FString ScriptType = Def.ScriptType.ToLower();
		const ENiagaraScriptUsage Usage = (ScriptType == TEXT("dynamic_input"))
			? ENiagaraScriptUsage::DynamicInput
			: ENiagaraScriptUsage::Module;
		NewScript->SetUsage(Usage);

		UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(NewScript, NAME_None, RF_Transactional);
		UNiagaraGraph* Graph = Source ? NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional) : nullptr;
		if (!Source || !Graph)
		{
			return TEXT("ERROR: Failed to initialize scratch pad graph");
		}
		Source->NodeGraph = Graph;

		const UEdGraphSchema_Niagara* NiagaraSchema = Cast<UEdGraphSchema_Niagara>(Graph->GetSchema());
		if (!NiagaraSchema)
		{
			return TEXT("ERROR: Failed to resolve Niagara graph schema");
		}

		FGraphNodeCreator<UNiagaraNodeOutput> OutputNodeCreator(*Graph);
		UNiagaraNodeOutput* OutputNode = OutputNodeCreator.CreateNode();
		OutputNode->SetUsage(Usage);

		FGraphNodeCreator<UNiagaraNodeInput> InputNodeCreator(*Graph);
		UNiagaraNodeInput* InputNode = InputNodeCreator.CreateNode();

		if (Usage == ENiagaraScriptUsage::DynamicInput)
		{
			InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
			InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
			OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetFloatDef(), TEXT("Output")));
			OutputNodeCreator.Finalize();
			InputNodeCreator.Finalize();
			NiagaraSchema->TryCreateConnection(InputNode->GetOutputPin(0), OutputNode->GetInputPin(0));
		}
		else
		{
			InputNode->Usage = ENiagaraInputNodeUsage::Parameter;
			InputNode->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
			OutputNode->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
			OutputNodeCreator.Finalize();
			InputNodeCreator.Finalize();
			NiagaraSchema->TryCreateConnection(InputNode->GetOutputPin(0), OutputNode->GetInputPin(0));
		}

		NewScript->SetLatestSource(Source);
		NewScript->RequestCompile(FGuid());
	}

	if (!NewScript)
	{
		return TEXT("ERROR: Failed to create scratch pad script");
	}

	const FString ScriptType = Def.ScriptType.ToLower();
	if (ScriptType == TEXT("dynamic_input"))
	{
		NewScript->SetUsage(ENiagaraScriptUsage::DynamicInput);
	}
	else
	{
		NewScript->SetUsage(ENiagaraScriptUsage::Module);
	}

	if (!Def.TargetStage.IsEmpty())
	{
		bool bValidUsage = false;
		const ENiagaraScriptUsage TargetUsage = ParseScriptUsage(Def.TargetStage, bValidUsage);
		if (bValidUsage && NewScript->GetLatestScriptData())
		{
			NewScript->GetLatestScriptData()->ModuleUsageBitmask |= (1 << static_cast<int32>(TargetUsage));
		}
	}

	NewScript->ClearFlags(RF_Public | RF_Standalone);
	System->ScratchPadScripts.AddUnique(NewScript);

	return FString::Printf(TEXT("Created scratch pad script '%s' (%s)"), *NewScript->GetName(), *Def.ScriptType);
}

FString FEditNiagaraTool::DeleteScratchPadScript(UNiagaraSystem* System, const FScratchPadScriptDeletion& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	for (int32 i = 0; i < System->ScratchPadScripts.Num(); ++i)
	{
		UNiagaraScript* Script = System->ScratchPadScripts[i];
		if (Script && Script->GetName().Equals(Def.Name, ESearchCase::IgnoreCase))
		{
			System->Modify();
			System->ScratchPadScripts.RemoveAt(i);
			return FString::Printf(TEXT("Deleted scratch pad script '%s'"), *Script->GetName());
		}
	}
	return FString::Printf(TEXT("ERROR: Scratch pad script '%s' not found"), *Def.Name);
}

FString FEditNiagaraTool::RenameScratchPadScript(UNiagaraSystem* System, const FScratchPadScriptRename& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	for (UNiagaraScript* Script : System->ScratchPadScripts)
	{
		if (Script && Script->GetName().Equals(Def.Name, ESearchCase::IgnoreCase))
		{
			System->Modify();
			Script->Rename(*Def.NewName, System, REN_DontCreateRedirectors);
			return FString::Printf(TEXT("Renamed scratch pad script '%s' -> '%s'"), *Def.Name, *Script->GetName());
		}
	}
	return FString::Printf(TEXT("ERROR: Scratch pad script '%s' not found"), *Def.Name);
}

FString FEditNiagaraTool::AddScratchModule(UNiagaraSystem* System, const FScratchModuleDefinition& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	UNiagaraScript* ScratchScript = FindScratchPadScriptByName(System, Def.ScratchScriptName);
	if (!ScratchScript)
	{
		return FString::Printf(TEXT("ERROR: Scratch pad script '%s' not found"), *Def.ScratchScriptName);
	}

	UNiagaraNodeOutput* OutputNode = FNiagaraEditorUtilities::GetScriptOutputNode(*ScratchScript);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve output node for scratch script '%s'"), *Def.ScratchScriptName);
	}

	UNiagaraScript* ModuleScript = LoadObject<UNiagaraScript>(nullptr, *Def.ModulePath);
	if (!ModuleScript)
	{
		return FString::Printf(TEXT("ERROR: Could not load module script: %s"), *Def.ModulePath);
	}

	if (!ModuleSupportsUsage(ModuleScript, OutputNode->GetUsage()))
	{
		return FString::Printf(TEXT("ERROR: Module '%s' is incompatible with scratch script usage '%s'"),
			*FPaths::GetBaseFilename(Def.ModulePath), *UsageToString(OutputNode->GetUsage()));
	}

	System->Modify();
	UNiagaraNodeFunctionCall* NewModuleNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(
		ModuleScript, *OutputNode, Def.Index, Def.ModuleName);
	if (!NewModuleNode)
	{
		return FString::Printf(TEXT("ERROR: Failed to add module '%s' to scratch script '%s'"),
			*FPaths::GetBaseFilename(Def.ModulePath), *Def.ScratchScriptName);
	}

	if (Def.Parameters.IsValid() && Def.Parameters->Values.Num() > 0)
	{
		FNiagaraEmitterHandle DummyEmitterHandle;
		for (const auto& ParamPair : Def.Parameters->Values)
		{
			FName InputName(*ParamPair.Key);
			FNiagaraVariable InputVar;
			bool bFoundInput = false;
			for (const FNiagaraVariable& Input : DiscoverModuleInputs(NewModuleNode))
			{
				if (Input.GetName() == InputName || Input.GetName().ToString().Equals(ParamPair.Key, ESearchCase::IgnoreCase))
				{
					InputVar = Input;
					bFoundInput = true;
					break;
				}
			}

			if (!bFoundInput)
			{
				continue;
			}

			FString SetError;
			if (!SetInputValue(System, DummyEmitterHandle, NewModuleNode, ScratchScript, InputVar, ParamPair.Value, SetError))
			{
				return FString::Printf(TEXT("ERROR: Added module but failed setting '%s': %s"), *ParamPair.Key, *SetError);
			}
		}
	}

	return FString::Printf(TEXT("Added '%s' to scratch script '%s'"),
		*NewModuleNode->GetFunctionName(), *ScratchScript->GetName());
}

FString FEditNiagaraTool::RemoveScratchModule(UNiagaraSystem* System, const FScratchModuleRemoval& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	UNiagaraScript* ScratchScript = FindScratchPadScriptByName(System, Def.ScratchScriptName);
	if (!ScratchScript)
	{
		return FString::Printf(TEXT("ERROR: Scratch pad script '%s' not found"), *Def.ScratchScriptName);
	}

	UNiagaraNodeOutput* OutputNode = FNiagaraEditorUtilities::GetScriptOutputNode(*ScratchScript);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve output node for scratch script '%s'"), *Def.ScratchScriptName);
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(OutputNode, Def.ModuleName);
	if (!ModuleNode)
	{
		TArray<FString> AvailableModules = ListModulesInStack(OutputNode);
		const FString ModuleList = AvailableModules.Num() > 0 ? FString::Join(AvailableModules, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Module '%s' not found in scratch script '%s'. Available modules: %s"),
			*Def.ModuleName, *Def.ScratchScriptName, *ModuleList);
	}

	UEdGraphPin* ParamMapInPin = nullptr;
	UEdGraphPin* ParamMapOutPin = nullptr;
	for (UEdGraphPin* Pin : ModuleNode->Pins)
	{
		if (Pin && Pin->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryType)
		{
			if (Pin->Direction == EGPD_Input)
			{
				ParamMapInPin = Pin;
			}
			else if (Pin->Direction == EGPD_Output)
			{
				ParamMapOutPin = Pin;
			}
		}
	}

	if (!ParamMapInPin || !ParamMapOutPin || ParamMapInPin->LinkedTo.Num() == 0 || ParamMapOutPin->LinkedTo.Num() == 0)
	{
		return FString::Printf(TEXT("ERROR: Module '%s' in scratch script '%s' has invalid parameter map links"),
			*Def.ModuleName, *Def.ScratchScriptName);
	}

	UEdGraphPin* PrevOutputPin = ParamMapInPin->LinkedTo[0];
	UEdGraphPin* NextInputPin = ParamMapOutPin->LinkedTo[0];
	if (!PrevOutputPin || !NextInputPin)
	{
		return FString::Printf(TEXT("ERROR: Module '%s' in scratch script '%s' has null adjacent links"),
			*Def.ModuleName, *Def.ScratchScriptName);
	}

	ParamMapInPin->Modify();
	ParamMapOutPin->Modify();
	PrevOutputPin->Modify();
	NextInputPin->Modify();

	ParamMapInPin->BreakAllPinLinks(true);
	ParamMapOutPin->BreakAllPinLinks(true);
	PrevOutputPin->MakeLinkTo(NextInputPin);

	if (UNiagaraNode* PrevNode = Cast<UNiagaraNode>(PrevOutputPin->GetOwningNode()))
	{
		PrevNode->PinConnectionListChanged(PrevOutputPin);
	}
	if (UNiagaraNode* NextNode = Cast<UNiagaraNode>(NextInputPin->GetOwningNode()))
	{
		NextNode->PinConnectionListChanged(NextInputPin);
	}

	UEdGraph* Graph = ModuleNode->GetGraph();
	if (!Graph)
	{
		return FString::Printf(TEXT("ERROR: Module '%s' in scratch script '%s' has no graph"), *Def.ModuleName, *Def.ScratchScriptName);
	}

	TArray<UEdGraphNode*> NodesToRemove;
	TSet<UEdGraphNode*> Visited;
	NiagaraToolHelpers::CollectModuleSubGraphNodes(ModuleNode, NodesToRemove, Visited);

	for (UEdGraphNode* NodeToRemove : NodesToRemove)
	{
		if (NodeToRemove)
		{
			NodeToRemove->Modify();
			Graph->RemoveNode(NodeToRemove, true);
		}
	}

	return FString::Printf(TEXT("Removed '%s' from scratch script '%s'"), *Def.ModuleName, *ScratchScript->GetName());
}

FString FEditNiagaraTool::SetScratchModuleParameters(UNiagaraSystem* System, const FScratchModuleSetParameters& Def)
{
	if (!System)
	{
		return TEXT("ERROR: System is null");
	}

	UNiagaraScript* ScratchScript = FindScratchPadScriptByName(System, Def.ScratchScriptName);
	if (!ScratchScript)
	{
		return FString::Printf(TEXT("ERROR: Scratch pad script '%s' not found"), *Def.ScratchScriptName);
	}

	UNiagaraNodeOutput* OutputNode = FNiagaraEditorUtilities::GetScriptOutputNode(*ScratchScript);
	if (!OutputNode)
	{
		return FString::Printf(TEXT("ERROR: Could not resolve output node for scratch script '%s'"), *Def.ScratchScriptName);
	}

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleByName(OutputNode, Def.ModuleName);
	if (!ModuleNode)
	{
		TArray<FString> AvailableModules = ListModulesInStack(OutputNode);
		const FString ModuleList = AvailableModules.Num() > 0 ? FString::Join(AvailableModules, TEXT(", ")) : TEXT("(none)");
		return FString::Printf(TEXT("ERROR: Module '%s' not found in scratch script '%s'. Available modules: %s"),
			*Def.ModuleName, *Def.ScratchScriptName, *ModuleList);
	}

	TArray<FNiagaraVariable> AvailableInputs = DiscoverModuleInputs(ModuleNode);
	if (AvailableInputs.Num() == 0)
	{
		return FString::Printf(TEXT("ERROR: Module '%s' has no discoverable inputs"), *Def.ModuleName);
	}

	FNiagaraEmitterHandle DummyEmitterHandle;
	TArray<FString> SetKeys;
	TArray<FString> Errors;
	for (const auto& ParamPair : Def.Parameters->Values)
	{
		FNiagaraVariable* MatchingInput = nullptr;
		for (FNiagaraVariable& Input : AvailableInputs)
		{
			if (Input.GetName().ToString().Equals(ParamPair.Key, ESearchCase::IgnoreCase))
			{
				MatchingInput = &Input;
				break;
			}
		}

		if (!MatchingInput)
		{
			Errors.Add(FString::Printf(TEXT("%s (input not found)"), *ParamPair.Key));
			continue;
		}

		FString SetError;
		if (!SetInputValue(System, DummyEmitterHandle, ModuleNode, ScratchScript, *MatchingInput, ParamPair.Value, SetError))
		{
			Errors.Add(FString::Printf(TEXT("%s (%s)"), *ParamPair.Key, *SetError));
			continue;
		}

		SetKeys.Add(ParamPair.Key);
	}

	if (SetKeys.Num() == 0)
	{
		return FString::Printf(TEXT("ERROR: No parameters were set on '%s': %s"),
			*Def.ModuleName, *FString::Join(Errors, TEXT("; ")));
	}

	FString Result = FString::Printf(TEXT("Set parameters on '%s' in scratch script '%s': %s"),
		*Def.ModuleName, *ScratchScript->GetName(), *FString::Join(SetKeys, TEXT(", ")));
	if (Errors.Num() > 0)
	{
		Result += FString::Printf(TEXT(" (errors: %s)"), *FString::Join(Errors, TEXT("; ")));
	}
	return Result;
}
