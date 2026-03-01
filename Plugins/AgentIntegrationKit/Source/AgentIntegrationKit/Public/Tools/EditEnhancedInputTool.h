// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UInputAction;
class UInputMappingContext;
class UInputTrigger;
class UInputModifier;

/**
 * Tool for creating and editing Enhanced Input assets (InputActions and InputMappingContexts).
 *
 * Parameters:
 *   - name: Asset name or path (required)
 *   - path: Folder path if name is not a full path (default: /Game/Input)
 *   - create_type: "InputAction" or "InputMappingContext" to create a new asset
 *
 * InputAction operations:
 *   - value_type: Set action value type (Boolean, Axis1D, Axis2D, Axis3D)
 *   - consume_input: Whether to consume input (bool)
 *   - trigger_when_paused: Whether to trigger when paused (bool)
 *   - add_triggers: Add triggers [{type, actuation_threshold, hold_time, ...}]
 *   - remove_triggers: Remove triggers by index [0, 2, ...]
 *   - add_modifiers: Add modifiers [{type, lower_threshold, upper_threshold, ...}]
 *   - remove_modifiers: Remove modifiers by index [0, 2, ...]
 *
 * InputMappingContext operations:
 *   - add_mappings: Add key->action mappings [{action, key, triggers:[...], modifiers:[...]}]
 *   - remove_mappings: Remove mappings [{action, key} or {index}]
 *   - modify_mappings: Modify existing mappings [{index, new_key, add_triggers, remove_triggers, add_modifiers, remove_modifiers}]
 *
 * Trigger types: Down, Pressed, Released, Hold, HoldAndRelease, Tap, Pulse, ChordAction, Combo
 * Modifier types: DeadZone, Negate, Scalar, ScaleByDeltaTime, Swizzle, Smooth, FOVScaling,
 *                 ToWorldSpace, ResponseCurveExponential
 */
class AGENTINTEGRATIONKIT_API FEditEnhancedInputTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_enhanced_input"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create and edit Enhanced Input assets (InputActions and InputMappingContexts). "
			"Configure key bindings, modifiers (DeadZone, Negate, Scalar, Swizzle), and triggers (Pressed, Released, Hold, Tap, Down). "
			"Use create_type to create new assets, or provide name/path of existing asset to edit.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// Asset creation
	UInputAction* CreateInputAction(const FString& Name, const FString& Path, FString& OutError);
	UInputMappingContext* CreateInputMappingContext(const FString& Name, const FString& Path, FString& OutError);

	// InputAction editing
	FToolResult EditInputAction(UInputAction* Action, const TSharedPtr<FJsonObject>& Args);

	// InputMappingContext editing
	FToolResult EditInputMappingContext(UInputMappingContext* Context, const TSharedPtr<FJsonObject>& Args);

	// Factory functions for creating instanced subobjects
	UInputTrigger* CreateTrigger(UObject* Outer, const TSharedPtr<FJsonObject>& Config, FString& OutError);
	UInputModifier* CreateModifier(UObject* Outer, const TSharedPtr<FJsonObject>& Config, FString& OutError);

	// Helper to resolve an InputAction by name or path
	UInputAction* ResolveInputAction(const FString& ActionRef, FString& OutError);

	// Parse and apply trigger/modifier operations shared between InputAction and mapping editing
	int32 AddTriggers(UObject* Outer, TArray<TObjectPtr<UInputTrigger>>& TargetArray,
		const TArray<TSharedPtr<FJsonValue>>* TriggersArray, TArray<FString>& OutResults);
	int32 RemoveTriggers(TArray<TObjectPtr<UInputTrigger>>& TargetArray,
		const TArray<TSharedPtr<FJsonValue>>* IndicesArray, TArray<FString>& OutResults);
	int32 AddModifiers(UObject* Outer, TArray<TObjectPtr<UInputModifier>>& TargetArray,
		const TArray<TSharedPtr<FJsonValue>>* ModifiersArray, TArray<FString>& OutResults);
	int32 RemoveModifiers(TArray<TObjectPtr<UInputModifier>>& TargetArray,
		const TArray<TSharedPtr<FJsonValue>>* IndicesArray, TArray<FString>& OutResults);
};
