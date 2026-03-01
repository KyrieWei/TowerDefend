// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

FString FReadFileTool::FormatTrigger(UInputTrigger* Trigger)
{
	if (!Trigger)
	{
		return TEXT("(null)");
	}

	FString TypeName = Trigger->GetClass()->GetName();
	// Strip "InputTrigger" prefix for readability
	TypeName.RemoveFromStart(TEXT("InputTrigger"));

	FString Details;

	// Common property
	if (Trigger->ActuationThreshold != 0.5f)
	{
		Details += FString::Printf(TEXT(" actuation=%.2f"), Trigger->ActuationThreshold);
	}

	// Time-based triggers
	if (UInputTriggerTimedBase* TimedTrigger = Cast<UInputTriggerTimedBase>(Trigger))
	{
		Details += FString::Printf(TEXT(" affected_by_time_dilation=%s"),
			TimedTrigger->bAffectedByTimeDilation ? TEXT("true") : TEXT("false"));
	}

	if (UInputTriggerHold* Hold = Cast<UInputTriggerHold>(Trigger))
	{
		Details += FString::Printf(TEXT(" hold_time=%.2f one_shot=%s"),
			Hold->HoldTimeThreshold,
			Hold->bIsOneShot ? TEXT("true") : TEXT("false"));
	}
	else if (UInputTriggerHoldAndRelease* HoldRelease = Cast<UInputTriggerHoldAndRelease>(Trigger))
	{
		Details += FString::Printf(TEXT(" hold_time=%.2f"), HoldRelease->HoldTimeThreshold);
	}
	else if (UInputTriggerTap* Tap = Cast<UInputTriggerTap>(Trigger))
	{
		Details += FString::Printf(TEXT(" tap_release_time=%.2f"), Tap->TapReleaseTimeThreshold);
	}
	else if (UInputTriggerPulse* Pulse = Cast<UInputTriggerPulse>(Trigger))
	{
		Details += FString::Printf(TEXT(" interval=%.2f trigger_on_start=%s"),
			Pulse->Interval,
			Pulse->bTriggerOnStart ? TEXT("true") : TEXT("false"));
	}
	else if (UInputTriggerChordAction* Chord = Cast<UInputTriggerChordAction>(Trigger))
	{
		FString ChordActionName = Chord->ChordAction ? Chord->ChordAction->GetName() : TEXT("(none)");
		Details += FString::Printf(TEXT(" chord_action=%s"), *ChordActionName);
	}
	else if (UInputTriggerCombo* Combo = Cast<UInputTriggerCombo>(Trigger))
	{
		TArray<FString> ActionNames;
		for (const FInputComboStepData& Step : Combo->ComboActions)
		{
			if (Step.ComboStepAction)
			{
				ActionNames.Add(Step.ComboStepAction->GetName());
			}
		}
		Details += FString::Printf(TEXT(" combo_actions=[%s]"), *FString::Join(ActionNames, TEXT(", ")));
	}

	return FString::Printf(TEXT("%s%s"), *TypeName, *Details);
}

FString FReadFileTool::FormatModifier(UInputModifier* Modifier)
{
	if (!Modifier)
	{
		return TEXT("(null)");
	}

	FString TypeName = Modifier->GetClass()->GetName();
	// Strip "InputModifier" prefix for readability
	TypeName.RemoveFromStart(TEXT("InputModifier"));

	FString Details;

	if (UInputModifierDeadZone* DZ = Cast<UInputModifierDeadZone>(Modifier))
	{
		FString DZType;
		switch (DZ->Type)
		{
		case EDeadZoneType::Axial: DZType = TEXT("Axial"); break;
		case EDeadZoneType::Radial: DZType = TEXT("Radial"); break;
		default: DZType = TEXT("Unknown"); break;
		}
		Details = FString::Printf(TEXT(" lower=%.2f upper=%.2f type=%s"),
			DZ->LowerThreshold, DZ->UpperThreshold, *DZType);
	}
	else if (UInputModifierScalar* Scalar = Cast<UInputModifierScalar>(Modifier))
	{
		Details = FString::Printf(TEXT(" scalar=(%.2f, %.2f, %.2f)"),
			Scalar->Scalar.X, Scalar->Scalar.Y, Scalar->Scalar.Z);
	}
	else if (UInputModifierNegate* Negate = Cast<UInputModifierNegate>(Modifier))
	{
		Details = FString::Printf(TEXT(" bX=%s bY=%s bZ=%s"),
			Negate->bX ? TEXT("true") : TEXT("false"),
			Negate->bY ? TEXT("true") : TEXT("false"),
			Negate->bZ ? TEXT("true") : TEXT("false"));
	}
	else if (UInputModifierSwizzleAxis* Swizzle = Cast<UInputModifierSwizzleAxis>(Modifier))
	{
		FString Order;
		switch (Swizzle->Order)
		{
		case EInputAxisSwizzle::YXZ: Order = TEXT("YXZ"); break;
		case EInputAxisSwizzle::ZYX: Order = TEXT("ZYX"); break;
		case EInputAxisSwizzle::XZY: Order = TEXT("XZY"); break;
		case EInputAxisSwizzle::YZX: Order = TEXT("YZX"); break;
		case EInputAxisSwizzle::ZXY: Order = TEXT("ZXY"); break;
		default: Order = TEXT("Unknown"); break;
		}
		Details = FString::Printf(TEXT(" order=%s"), *Order);
	}
	else if (UInputModifierResponseCurveExponential* Exp = Cast<UInputModifierResponseCurveExponential>(Modifier))
	{
		Details = FString::Printf(TEXT(" exponent=(%.2f, %.2f, %.2f)"),
			Exp->CurveExponent.X, Exp->CurveExponent.Y, Exp->CurveExponent.Z);
	}
	else if (UInputModifierFOVScaling* FOV = Cast<UInputModifierFOVScaling>(Modifier))
	{
		Details = FString::Printf(TEXT(" fov_scale=%.2f"), FOV->FOVScale);
	}

	return FString::Printf(TEXT("%s%s"), *TypeName, *Details);
}

FToolResult FReadFileTool::ReadInputAction(UInputAction* InputAction)
{
	if (!InputAction)
	{
		return FToolResult::Fail(TEXT("InputAction is null"));
	}

	// Value type
	FString ValueTypeStr;
	switch (InputAction->ValueType)
	{
	case EInputActionValueType::Boolean: ValueTypeStr = TEXT("Boolean"); break;
	case EInputActionValueType::Axis1D: ValueTypeStr = TEXT("Axis1D"); break;
	case EInputActionValueType::Axis2D: ValueTypeStr = TEXT("Axis2D"); break;
	case EInputActionValueType::Axis3D: ValueTypeStr = TEXT("Axis3D"); break;
	default: ValueTypeStr = TEXT("Unknown"); break;
	}

	FString Output = FString::Printf(
		TEXT("# INPUT_ACTION %s value_type=%s consume_input=%s trigger_when_paused=%s\n"),
		*InputAction->GetName(),
		*ValueTypeStr,
		InputAction->bConsumeInput ? TEXT("true") : TEXT("false"),
		InputAction->bTriggerWhenPaused ? TEXT("true") : TEXT("false"));

	// Accumulation behavior
	FString AccumStr;
	switch (InputAction->AccumulationBehavior)
	{
	case EInputActionAccumulationBehavior::TakeHighestAbsoluteValue:
		AccumStr = TEXT("TakeHighestAbsoluteValue"); break;
	case EInputActionAccumulationBehavior::Cumulative:
		AccumStr = TEXT("Cumulative"); break;
	default: AccumStr = TEXT("Unknown"); break;
	}
	Output += FString::Printf(TEXT("accumulation=%s\n"), *AccumStr);

	// Triggers
	Output += FString::Printf(TEXT("\n## ACTION TRIGGERS (%d)\n"), InputAction->Triggers.Num());
	for (int32 i = 0; i < InputAction->Triggers.Num(); i++)
	{
		Output += FString::Printf(TEXT("  %d\t%s\n"), i, *FormatTrigger(InputAction->Triggers[i]));
	}

	// Modifiers
	Output += FString::Printf(TEXT("\n## ACTION MODIFIERS (%d)\n"), InputAction->Modifiers.Num());
	for (int32 i = 0; i < InputAction->Modifiers.Num(); i++)
	{
		Output += FString::Printf(TEXT("  %d\t%s\n"), i, *FormatModifier(InputAction->Modifiers[i]));
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadInputMappingContext(UInputMappingContext* MappingContext)
{
	if (!MappingContext)
	{
		return FToolResult::Fail(TEXT("InputMappingContext is null"));
	}

	const TArray<FEnhancedActionKeyMapping>& Mappings = MappingContext->GetMappings();

	FString Output = FString::Printf(TEXT("# INPUT_MAPPING_CONTEXT %s mappings=%d\n"),
		*MappingContext->GetName(), Mappings.Num());

	Output += TEXT("\n## MAPPINGS\n");

	for (int32 i = 0; i < Mappings.Num(); i++)
	{
		const FEnhancedActionKeyMapping& Mapping = Mappings[i];

		FString ActionName = Mapping.Action ? Mapping.Action->GetName() : TEXT("(none)");
		FString KeyName = Mapping.Key.GetFName().ToString();

		// Value type from the action
		FString ValueTypeStr = TEXT("");
		if (Mapping.Action)
		{
			switch (Mapping.Action->ValueType)
			{
			case EInputActionValueType::Boolean: ValueTypeStr = TEXT("Boolean"); break;
			case EInputActionValueType::Axis1D: ValueTypeStr = TEXT("Axis1D"); break;
			case EInputActionValueType::Axis2D: ValueTypeStr = TEXT("Axis2D"); break;
			case EInputActionValueType::Axis3D: ValueTypeStr = TEXT("Axis3D"); break;
			default: break;
			}
		}

		Output += FString::Printf(TEXT("  %d\t%s\t%s"), i, *ActionName, *KeyName);
		if (!ValueTypeStr.IsEmpty())
		{
			Output += FString::Printf(TEXT("\t(%s)"), *ValueTypeStr);
		}
		Output += TEXT("\n");

		// Modifiers for this mapping
		if (Mapping.Modifiers.Num() > 0)
		{
			TArray<FString> ModStrs;
			for (UInputModifier* Mod : Mapping.Modifiers)
			{
				ModStrs.Add(FString::Printf(TEXT("[%s]"), *FormatModifier(Mod)));
			}
			Output += FString::Printf(TEXT("       Modifiers: %s\n"), *FString::Join(ModStrs, TEXT(", ")));
		}
		else
		{
			Output += TEXT("       Modifiers: (none)\n");
		}

		// Triggers for this mapping
		if (Mapping.Triggers.Num() > 0)
		{
			TArray<FString> TrigStrs;
			for (UInputTrigger* Trig : Mapping.Triggers)
			{
				TrigStrs.Add(FString::Printf(TEXT("[%s]"), *FormatTrigger(Trig)));
			}
			Output += FString::Printf(TEXT("       Triggers: %s\n"), *FString::Join(TrigStrs, TEXT(", ")));
		}
		else
		{
			Output += TEXT("       Triggers: (none)\n");
		}
	}

	return FToolResult::Ok(Output);
}
