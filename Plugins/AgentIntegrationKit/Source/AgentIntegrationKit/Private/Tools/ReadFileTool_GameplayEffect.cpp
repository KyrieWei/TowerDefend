// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Engine/Blueprint.h"
#include "GameplayEffect.h"
#include "GameplayEffectComponent.h"
#include "GameplayEffectExecutionCalculation.h"
#include "UObject/UnrealType.h"

// Helper to format a magnitude into a readable string
static FString FormatMagnitude(const FGameplayEffectModifierMagnitude& Magnitude)
{
	switch (Magnitude.GetMagnitudeCalculationType())
	{
	case EGameplayEffectMagnitudeCalculation::ScalableFloat:
	{
		float Value = 0.f;
		// Try to get the static value (at level 0)
		Magnitude.GetStaticMagnitudeIfPossible(1.f, Value);
		return FString::Printf(TEXT("ScalableFloat(%.2f)"), Value);
	}
	case EGameplayEffectMagnitudeCalculation::AttributeBased:
		return TEXT("AttributeBased");
	case EGameplayEffectMagnitudeCalculation::CustomCalculationClass:
		return TEXT("CustomCalculation");
	case EGameplayEffectMagnitudeCalculation::SetByCaller:
		return TEXT("SetByCaller");
	default:
		return TEXT("Unknown");
	}
}

// Helper to format modifier operation enum
static FString FormatModOp(TEnumAsByte<EGameplayModOp::Type> Op)
{
	switch (Op.GetValue())
	{
	case EGameplayModOp::Additive: return TEXT("Add");
	case EGameplayModOp::Multiplicitive: return TEXT("Multiply");
	case EGameplayModOp::Division: return TEXT("Divide");
	case EGameplayModOp::Override: return TEXT("Override");
	case EGameplayModOp::MultiplyCompound: return TEXT("MultiplyCompound");
	case EGameplayModOp::AddFinal: return TEXT("AddFinal");
	default: return FString::Printf(TEXT("Op(%d)"), (int32)Op.GetValue());
	}
}

FToolResult FReadFileTool::ReadGameplayEffect(UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return FToolResult::Fail(TEXT("Blueprint is null"));
	}

	if (!Blueprint->GeneratedClass)
	{
		return FToolResult::Fail(TEXT("Blueprint has no GeneratedClass - may need compilation"));
	}

	// Get the CDO (Class Default Object) which holds all effect configuration
	UGameplayEffect* EffectCDO = Cast<UGameplayEffect>(Blueprint->GeneratedClass->GetDefaultObject());
	if (!EffectCDO)
	{
		return FToolResult::Fail(TEXT("Could not get GameplayEffect CDO"));
	}

	FString Output;

	// === Header ===
	FString DurationStr;
	switch (EffectCDO->DurationPolicy)
	{
	case EGameplayEffectDurationType::Instant: DurationStr = TEXT("Instant"); break;
	case EGameplayEffectDurationType::Infinite: DurationStr = TEXT("Infinite"); break;
	case EGameplayEffectDurationType::HasDuration: DurationStr = TEXT("HasDuration"); break;
	default: DurationStr = TEXT("Unknown"); break;
	}

	Output = FString::Printf(TEXT("# GAMEPLAY_EFFECT %s duration=%s\n"),
		*Blueprint->GetName(), *DurationStr);

	// Duration magnitude (if applicable)
	if (EffectCDO->DurationPolicy == EGameplayEffectDurationType::HasDuration)
	{
		Output += FString::Printf(TEXT("duration_magnitude=%s\n"),
			*FormatMagnitude(EffectCDO->DurationMagnitude));
	}

	// Period
	float PeriodValue = EffectCDO->Period.GetValueAtLevel(1.f);
	if (PeriodValue > 0.f)
	{
		Output += FString::Printf(TEXT("period=%.2f execute_on_application=%s\n"),
			PeriodValue,
			EffectCDO->bExecutePeriodicEffectOnApplication ? TEXT("true") : TEXT("false"));
	}

	// === Modifiers ===
	Output += FString::Printf(TEXT("\n## MODIFIERS (%d)\n"), EffectCDO->Modifiers.Num());
	if (EffectCDO->Modifiers.Num() > 0)
	{
		Output += TEXT("  #\tAttribute\tOperation\tMagnitude\n");
		for (int32 i = 0; i < EffectCDO->Modifiers.Num(); i++)
		{
			const FGameplayModifierInfo& Mod = EffectCDO->Modifiers[i];

			FString AttrName = Mod.Attribute.IsValid()
				? Mod.Attribute.GetName()
				: TEXT("(none)");

			Output += FString::Printf(TEXT("  %d\t%s\t%s\t%s\n"),
				i,
				*AttrName,
				*FormatModOp(Mod.ModifierOp),
				*FormatMagnitude(Mod.ModifierMagnitude));
		}
	}

	// === Executions ===
	if (EffectCDO->Executions.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## EXECUTIONS (%d)\n"), EffectCDO->Executions.Num());
		for (int32 i = 0; i < EffectCDO->Executions.Num(); i++)
		{
			const FGameplayEffectExecutionDefinition& Exec = EffectCDO->Executions[i];
			FString ClassName = Exec.CalculationClass
				? Exec.CalculationClass->GetName()
				: TEXT("(none)");
			Output += FString::Printf(TEXT("  %d\t%s\n"), i, *ClassName);
		}
	}

	// === Stacking ===
	// GetStackingType() isn't exported, read StackingType via reflection
	EGameplayEffectStackingType StackType = EGameplayEffectStackingType::None;
	if (FProperty* StackProp = EffectCDO->GetClass()->FindPropertyByName(TEXT("StackingType")))
	{
		StackProp->CopyCompleteValue(&StackType, StackProp->ContainerPtrToValuePtr<void>(EffectCDO));
	}
	if (StackType != EGameplayEffectStackingType::None)
	{
		FString StackTypeStr;
		switch (StackType)
		{
		case EGameplayEffectStackingType::AggregateBySource: StackTypeStr = TEXT("AggregateBySource"); break;
		case EGameplayEffectStackingType::AggregateByTarget: StackTypeStr = TEXT("AggregateByTarget"); break;
		default: StackTypeStr = TEXT("Unknown"); break;
		}

		Output += FString::Printf(TEXT("\n## STACKING\n"));
		Output += FString::Printf(TEXT("  type=%s\n"), *StackTypeStr);
		Output += FString::Printf(TEXT("  stack_limit=%d\n"), EffectCDO->StackLimitCount);

		FString DurPolicy;
		switch (EffectCDO->StackDurationRefreshPolicy)
		{
		case EGameplayEffectStackingDurationPolicy::RefreshOnSuccessfulApplication: DurPolicy = TEXT("RefreshOnApplication"); break;
		case EGameplayEffectStackingDurationPolicy::NeverRefresh: DurPolicy = TEXT("NeverRefresh"); break;
		default: DurPolicy = TEXT("Unknown"); break;
		}
		Output += FString::Printf(TEXT("  duration_refresh=%s\n"), *DurPolicy);

		FString PeriodPolicy;
		switch (EffectCDO->StackPeriodResetPolicy)
		{
		case EGameplayEffectStackingPeriodPolicy::ResetOnSuccessfulApplication: PeriodPolicy = TEXT("ResetOnApplication"); break;
		case EGameplayEffectStackingPeriodPolicy::NeverReset: PeriodPolicy = TEXT("NeverReset"); break;
		default: PeriodPolicy = TEXT("Unknown"); break;
		}
		Output += FString::Printf(TEXT("  period_reset=%s\n"), *PeriodPolicy);

		FString ExpPolicy;
		switch (EffectCDO->StackExpirationPolicy)
		{
		case EGameplayEffectStackingExpirationPolicy::ClearEntireStack: ExpPolicy = TEXT("ClearEntireStack"); break;
		case EGameplayEffectStackingExpirationPolicy::RemoveSingleStackAndRefreshDuration: ExpPolicy = TEXT("RemoveSingleAndRefresh"); break;
		case EGameplayEffectStackingExpirationPolicy::RefreshDuration: ExpPolicy = TEXT("RefreshDuration"); break;
		default: ExpPolicy = TEXT("Unknown"); break;
		}
		Output += FString::Printf(TEXT("  expiration=%s\n"), *ExpPolicy);

		if (EffectCDO->OverflowEffects.Num() > 0)
		{
			TArray<FString> OverflowNames;
			for (const TSubclassOf<UGameplayEffect>& OE : EffectCDO->OverflowEffects)
			{
				OverflowNames.Add(OE ? OE->GetName() : TEXT("(null)"));
			}
			Output += FString::Printf(TEXT("  overflow_effects=[%s]\n"), *FString::Join(OverflowNames, TEXT(", ")));
		}
	}

	// === GameplayCues ===
	if (EffectCDO->GameplayCues.Num() > 0)
	{
		Output += FString::Printf(TEXT("\n## GAMEPLAY_CUES (%d)\n"), EffectCDO->GameplayCues.Num());
		for (int32 i = 0; i < EffectCDO->GameplayCues.Num(); i++)
		{
			const FGameplayEffectCue& Cue = EffectCDO->GameplayCues[i];
			FString Tags = Cue.GameplayCueTags.ToStringSimple();
			if (Tags.IsEmpty()) Tags = TEXT("(none)");

			Output += FString::Printf(TEXT("  %d\ttags=%s\tlevel_range=[%.1f, %.1f]\n"),
				i, *Tags, Cue.MinLevel, Cue.MaxLevel);
		}
	}

	// === GE Components (UE 5.3+ modular architecture) ===
	// GEComponents is protected, so access via property reflection
	FArrayProperty* GECompProp = CastField<FArrayProperty>(
		EffectCDO->GetClass()->FindPropertyByName(TEXT("GEComponents")));
	if (GECompProp)
	{
		FScriptArrayHelper ArrayHelper(GECompProp, GECompProp->ContainerPtrToValuePtr<void>(EffectCDO));
		FObjectPropertyBase* InnerObjProp = CastField<FObjectPropertyBase>(GECompProp->Inner);

		if (ArrayHelper.Num() > 0 && InnerObjProp)
		{
		Output += FString::Printf(TEXT("\n## COMPONENTS (%d)\n"), ArrayHelper.Num());
		for (int32 i = 0; i < ArrayHelper.Num(); i++)
		{
			UGameplayEffectComponent* Comp = Cast<UGameplayEffectComponent>(
				InnerObjProp->GetObjectPropertyValue(ArrayHelper.GetElementPtr(i)));
			if (!Comp)
			{
				Output += FString::Printf(TEXT("  %d\t(null)\n"), i);
				continue;
			}

			FString FullClassName = Comp->GetClass()->GetName();
			// Strip suffix for a readable short name
			FString ShortName = FullClassName;
			ShortName.RemoveFromEnd(TEXT("GameplayEffectComponent"));
			if (ShortName.IsEmpty()) ShortName = FullClassName;

			Output += FString::Printf(TEXT("  %d\t%s (%s)\n"), i, *ShortName, *FullClassName);

			// Try to extract tag info from the component via reflection
			// Look for GameplayTag container properties
			for (TFieldIterator<FProperty> PropIt(Comp->GetClass()); PropIt; ++PropIt)
			{
				FProperty* Prop = *PropIt;
				if (!Prop) continue;

				// Check for FGameplayTagContainer properties
				if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
				{
					if (StructProp->Struct && StructProp->Struct->GetFName() == FName("GameplayTagContainer"))
					{
						const FGameplayTagContainer* TagContainer = StructProp->ContainerPtrToValuePtr<FGameplayTagContainer>(Comp);
						if (TagContainer && TagContainer->Num() > 0)
						{
							Output += FString::Printf(TEXT("       %s: %s\n"),
								*Prop->GetName(), *TagContainer->ToStringSimple());
						}
					}
					else if (StructProp->Struct && StructProp->Struct->GetFName() == FName("GameplayTagRequirements"))
					{
						const FGameplayTagRequirements* TagReqs = StructProp->ContainerPtrToValuePtr<FGameplayTagRequirements>(Comp);
						if (TagReqs)
						{
							FString ReqStr;
							if (TagReqs->RequireTags.Num() > 0)
							{
								ReqStr += TEXT("require=[") + TagReqs->RequireTags.ToStringSimple() + TEXT("]");
							}
							if (TagReqs->IgnoreTags.Num() > 0)
							{
								if (!ReqStr.IsEmpty()) ReqStr += TEXT(" ");
								ReqStr += TEXT("ignore=[") + TagReqs->IgnoreTags.ToStringSimple() + TEXT("]");
							}
							if (!ReqStr.IsEmpty())
							{
								Output += FString::Printf(TEXT("       %s: %s\n"),
									*Prop->GetName(), *ReqStr);
							}
						}
					}
				}
			}
		}
		}
	}

	return FToolResult::Ok(Output);
}
