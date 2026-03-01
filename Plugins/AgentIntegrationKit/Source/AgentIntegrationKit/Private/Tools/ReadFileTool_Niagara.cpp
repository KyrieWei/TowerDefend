// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraSimulationStageBase.h"

FString FReadFileTool::GetNiagaraSystemSummary(UNiagaraSystem* System)
{
	if (!System)
	{
		return TEXT("# NIAGARA_SYSTEM (null)\n");
	}

	int32 EmitterCount = System->GetEmitterHandles().Num();

	FString EffectTypeName = TEXT("None");
	if (System->GetEffectType())
	{
		EffectTypeName = System->GetEffectType()->GetName();
	}

	FString Output = FString::Printf(TEXT("# NIAGARA_SYSTEM %s\n"), *System->GetName());
	Output += FString::Printf(TEXT("emitters=%d effect_type=%s\n"), EmitterCount, *EffectTypeName);
	Output += FString::Printf(TEXT("warmup_time=%.2f fixed_bounds=%s\n"),
		System->GetWarmupTime(),
		System->GetFixedBounds().IsValid ? TEXT("true") : TEXT("false"));

	if (EmitterCount > 0)
	{
		Output += TEXT("\n# EMITTER_LIST\n");
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			FString EmitterName = Handle.GetName().ToString();
			bool bEnabled = Handle.GetIsEnabled();
			Output += FString::Printf(TEXT("%s\t%s\n"), *EmitterName, bEnabled ? TEXT("enabled") : TEXT("disabled"));
		}
	}

	return Output;
}

FString FReadFileTool::GetNiagaraEmitterDetails(const FNiagaraEmitterHandle& Handle)
{
	FString Output;
	FString EmitterName = Handle.GetName().ToString();
	FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();

	if (!EmitterData)
	{
		return FString::Printf(TEXT("## EMITTER %s (no data)\n"), *EmitterName);
	}

	Output += FString::Printf(TEXT("## EMITTER %s\n"), *EmitterName);
	Output += FString::Printf(TEXT("enabled=%s "), Handle.GetIsEnabled() ? TEXT("true") : TEXT("false"));
	Output += FString::Printf(TEXT("sim_target=%s "),
		EmitterData->SimTarget == ENiagaraSimTarget::CPUSim ? TEXT("CPU") : TEXT("GPU"));
	Output += FString::Printf(TEXT("local_space=%s\n"),
		EmitterData->bLocalSpace ? TEXT("true") : TEXT("false"));

	if (UNiagaraScript* EmitterSpawnScript = EmitterData->GetScript(ENiagaraScriptUsage::EmitterSpawnScript, FGuid()))
	{
		Output += FString::Printf(TEXT("\n### EMITTER_SPAWN_STACK usage_id=%s\n"),
			*EmitterSpawnScript->GetUsageId().ToString(EGuidFormats::DigitsWithHyphensLower));
		Output += GetNiagaraScriptModules(EmitterSpawnScript);
	}

	if (UNiagaraScript* EmitterUpdateScript = EmitterData->GetScript(ENiagaraScriptUsage::EmitterUpdateScript, FGuid()))
	{
		Output += FString::Printf(TEXT("\n### EMITTER_UPDATE_STACK usage_id=%s\n"),
			*EmitterUpdateScript->GetUsageId().ToString(EGuidFormats::DigitsWithHyphensLower));
		Output += GetNiagaraScriptModules(EmitterUpdateScript);
	}

	if (UNiagaraScript* ParticleSpawnScript = EmitterData->GetScript(ENiagaraScriptUsage::ParticleSpawnScript, FGuid()))
	{
		Output += FString::Printf(TEXT("\n### PARTICLE_SPAWN_STACK usage_id=%s\n"),
			*ParticleSpawnScript->GetUsageId().ToString(EGuidFormats::DigitsWithHyphensLower));
		Output += GetNiagaraScriptModules(ParticleSpawnScript);
	}

	if (UNiagaraScript* ParticleUpdateScript = EmitterData->GetScript(ENiagaraScriptUsage::ParticleUpdateScript, FGuid()))
	{
		Output += FString::Printf(TEXT("\n### PARTICLE_UPDATE_STACK usage_id=%s\n"),
			*ParticleUpdateScript->GetUsageId().ToString(EGuidFormats::DigitsWithHyphensLower));
		Output += GetNiagaraScriptModules(ParticleUpdateScript);
	}

	const TArray<FNiagaraEventScriptProperties>& EventHandlers = EmitterData->GetEventHandlers();
	for (int32 EventIndex = 0; EventIndex < EventHandlers.Num(); ++EventIndex)
	{
		const FNiagaraEventScriptProperties& EventHandler = EventHandlers[EventIndex];
		if (!EventHandler.Script)
		{
			continue;
		}

		Output += FString::Printf(TEXT("\n### PARTICLE_EVENT_STACK index=%d usage_id=%s source_event=%s\n"),
			EventIndex,
			*EventHandler.Script->GetUsageId().ToString(EGuidFormats::DigitsWithHyphensLower),
			*EventHandler.SourceEventName.ToString());
		Output += GetNiagaraScriptModules(EventHandler.Script);
	}

	const TArray<UNiagaraSimulationStageBase*>& SimulationStages = EmitterData->GetSimulationStages();
	for (int32 StageIndex = 0; StageIndex < SimulationStages.Num(); ++StageIndex)
	{
		UNiagaraSimulationStageBase* Stage = SimulationStages[StageIndex];
		if (!Stage || !Stage->Script)
		{
			continue;
		}

		Output += FString::Printf(TEXT("\n### PARTICLE_SIM_STAGE_STACK index=%d name=%s enabled=%s usage_id=%s\n"),
			StageIndex,
			*Stage->SimulationStageName.ToString(),
			Stage->bEnabled ? TEXT("true") : TEXT("false"),
			*Stage->Script->GetUsageId().ToString(EGuidFormats::DigitsWithHyphensLower));
		Output += GetNiagaraScriptModules(Stage->Script);
	}

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	if (Renderers.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n### RENDERERS %d\n"), Renderers.Num());
		for (UNiagaraRendererProperties* Renderer : Renderers)
		{
			if (!Renderer) continue;

			FString RendererType = Renderer->GetClass()->GetName();
			RendererType.RemoveFromStart(TEXT("Niagara"));
			RendererType.RemoveFromEnd(TEXT("Properties"));

			FString RendererName = Renderer->GetName();
			bool bEnabled = Renderer->GetIsEnabled();

			FString MaterialInfo;
		if (UNiagaraSpriteRendererProperties* SpriteRenderer = Cast<UNiagaraSpriteRendererProperties>(Renderer))
		{
			if (SpriteRenderer->Material) MaterialInfo = SpriteRenderer->Material->GetName();
		}
		else if (UNiagaraRibbonRendererProperties* RibbonRenderer = Cast<UNiagaraRibbonRendererProperties>(Renderer))
		{
			if (RibbonRenderer->Material) MaterialInfo = RibbonRenderer->Material->GetName();
		}

		if (MaterialInfo.IsEmpty())
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\n"),
				*RendererName, *RendererType, bEnabled ? TEXT("enabled") : TEXT("disabled"));
		}
		else
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\tMaterial:%s\n"),
				*RendererName, *RendererType, bEnabled ? TEXT("enabled") : TEXT("disabled"), *MaterialInfo);
		}
		}
	}

	return Output;
}

FString FReadFileTool::GetNiagaraEmitters(UNiagaraSystem* System)
{
	if (!System)
	{
		return TEXT("# EMITTERS 0\n");
	}

	FString Output;

	for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
	{
		Output += TEXT("\n");
		Output += GetNiagaraEmitterDetails(Handle);
	}

	return Output;
}

FString FReadFileTool::GetNiagaraEmitterStacks(UNiagaraEmitter* Emitter, const FString& EmitterName)
{
	return TEXT("");
}

FString FReadFileTool::GetNiagaraScriptModules(UNiagaraScript* Script)
{
	if (!Script)
	{
		return TEXT("(no script)\n");
	}

	UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!Source || !Source->NodeGraph)
	{
		return TEXT("(no graph)\n");
	}

	FString Output;
	UNiagaraGraph* Graph = Source->NodeGraph;

	TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
	Graph->GetNodesOfClass<UNiagaraNodeFunctionCall>(ModuleNodes);

	for (UNiagaraNodeFunctionCall* ModuleNode : ModuleNodes)
	{
		if (!ModuleNode) continue;

		FString ModuleName = ModuleNode->GetNodeTitle(ENodeTitleType::ListView).ToString();
		FString ModuleGuid = ModuleNode->NodeGuid.ToString(EGuidFormats::DigitsWithHyphensLower);

		FString ScriptPath = TEXT("");
		if (ModuleNode->FunctionScript)
		{
			ScriptPath = ModuleNode->FunctionScript->GetPathName();
		}

		Output += FString::Printf(TEXT("%s\t%s\n"), *ModuleName, *ModuleGuid);

		if (ModuleNode->FunctionScript)
		{
			if (UNiagaraScriptSource* ModSource = Cast<UNiagaraScriptSource>(ModuleNode->FunctionScript->GetLatestSource()))
			{
				if (UNiagaraGraph* ModGraph = ModSource->NodeGraph)
				{
					const TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& AllMetaData = ModGraph->GetAllMetaData();
					for (const auto& Pair : AllMetaData)
					{
						const FNiagaraVariable& Param = Pair.Key;
						FString ParamName = Param.GetName().ToString();
						FString ParamType = Param.GetType().GetName();
						Output += FString::Printf(TEXT("  %s: %s\n"), *ParamName, *ParamType);
					}
				}
			}
		}
	}

	if (Output.IsEmpty())
	{
		Output = TEXT("(empty stack)\n");
	}

	return Output;
}

FString FReadFileTool::GetNiagaraModuleParameters(UNiagaraScript* Script, const FString& ModuleName)
{
	if (!Script)
	{
		return TEXT("(no script)\n");
	}

	FString Output = FString::Printf(TEXT("# MODULE_PARAMETERS %s\n"), *ModuleName);

	if (UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Script->GetLatestSource()))
	{
		if (UNiagaraGraph* Graph = Source->NodeGraph)
		{
			const TMap<FNiagaraVariable, TObjectPtr<UNiagaraScriptVariable>>& AllMetaData = Graph->GetAllMetaData();
			for (const auto& Pair : AllMetaData)
			{
				const FNiagaraVariable& Param = Pair.Key;
				FString ParamName = Param.GetName().ToString();
				FString ParamType = Param.GetType().GetName();
				Output += FString::Printf(TEXT("%s\t%s\n"), *ParamName, *ParamType);
			}
		}
	}

	return Output;
}
