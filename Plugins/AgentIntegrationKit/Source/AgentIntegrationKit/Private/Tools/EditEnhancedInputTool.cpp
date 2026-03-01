// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditEnhancedInputTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Enhanced Input
#include "InputAction.h"
#include "InputMappingContext.h"
#include "EnhancedActionKeyMapping.h"
#include "InputModifiers.h"
#include "InputTriggers.h"

// Editor/Asset support
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"

// ============================================================================
// SCHEMA
// ============================================================================

TSharedPtr<FJsonObject> FEditEnhancedInputTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Common parameters
	auto MakeProp = [](const FString& Type, const FString& Desc) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
		Prop->SetStringField(TEXT("type"), Type);
		Prop->SetStringField(TEXT("description"), Desc);
		return Prop;
	};

	Properties->SetObjectField(TEXT("name"), MakeProp(TEXT("string"),
		TEXT("Asset name or full path (e.g., 'IA_Jump' or '/Game/Input/IA_Jump')")));
	Properties->SetObjectField(TEXT("path"), MakeProp(TEXT("string"),
		TEXT("Folder path if name is not a full path. Default: /Game/Input")));
	Properties->SetObjectField(TEXT("create_type"), MakeProp(TEXT("string"),
		TEXT("Create a new asset: 'InputAction' or 'InputMappingContext'. Omit to edit an existing asset.")));

	// InputAction properties
	Properties->SetObjectField(TEXT("value_type"), MakeProp(TEXT("string"),
		TEXT("InputAction: Value type - Boolean, Axis1D, Axis2D, or Axis3D")));
	Properties->SetObjectField(TEXT("consume_input"), MakeProp(TEXT("boolean"),
		TEXT("InputAction: Whether to consume input to lower-priority actions")));
	Properties->SetObjectField(TEXT("trigger_when_paused"), MakeProp(TEXT("boolean"),
		TEXT("InputAction: Whether to allow triggering when the game is paused")));

	// Trigger/Modifier operations (shared - work on both InputAction and mapping modify)
	Properties->SetObjectField(TEXT("add_triggers"), MakeProp(TEXT("array"),
		TEXT("Add triggers. Each: {type (Down/Pressed/Released/Hold/HoldAndRelease/Tap/Pulse/ChordAction/Combo), "
		     "actuation_threshold (float), hold_time (float), tap_time (float), interval (float), "
		     "trigger_on_start (bool), is_one_shot (bool), chord_action (string - action name/path)}")));
	Properties->SetObjectField(TEXT("remove_triggers"), MakeProp(TEXT("array"),
		TEXT("Trigger indices to remove (integers). Removed in descending order to preserve indices.")));
	Properties->SetObjectField(TEXT("add_modifiers"), MakeProp(TEXT("array"),
		TEXT("Add modifiers. Each: {type (DeadZone/Negate/Scalar/ScaleByDeltaTime/Swizzle/Smooth/FOVScaling/"
		     "ToWorldSpace/ResponseCurveExponential), lower_threshold (float), upper_threshold (float), "
		     "dead_zone_type (Axial/Radial), scalar_x/scalar_y/scalar_z (float), "
		     "negate_x/negate_y/negate_z (bool), swizzle_order (YXZ/ZYX/XZY/YZX/ZXY), "
		     "fov_scale (float), exponent_x/exponent_y/exponent_z (float)}")));
	Properties->SetObjectField(TEXT("remove_modifiers"), MakeProp(TEXT("array"),
		TEXT("Modifier indices to remove (integers). Removed in descending order to preserve indices.")));

	// InputMappingContext operations
	Properties->SetObjectField(TEXT("add_mappings"), MakeProp(TEXT("array"),
		TEXT("InputMappingContext: Add key->action mappings. Each: {action (name or path of InputAction), "
		     "key (e.g., 'SpaceBar', 'W', 'Gamepad_FaceButton_Bottom', 'Gamepad_Left2D', 'Mouse2D'), "
		     "triggers: [{type, ...}], modifiers: [{type, ...}]}")));
	Properties->SetObjectField(TEXT("remove_mappings"), MakeProp(TEXT("array"),
		TEXT("InputMappingContext: Remove mappings. Each: {action, key} to remove specific mapping, or {index} to remove by position")));
	Properties->SetObjectField(TEXT("modify_mappings"), MakeProp(TEXT("array"),
		TEXT("InputMappingContext: Modify existing mappings. Each: {index (required), new_key, "
		     "add_triggers: [{type,...}], remove_triggers: [indices], "
		     "add_modifiers: [{type,...}], remove_modifiers: [indices]}")));

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ============================================================================
// EXECUTE (MAIN ENTRY POINT)
// ============================================================================

FToolResult FEditEnhancedInputTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path, CreateType;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("create_type"), CreateType);

	// Default path for Enhanced Input assets
	if (Path.IsEmpty() && !Name.Contains(TEXT("/")))
	{
		Path = TEXT("/Game/Input");
	}

	UObject* Asset = nullptr;

	// Create new asset if requested
	if (!CreateType.IsEmpty())
	{
		FString Error;

		if (CreateType.Equals(TEXT("InputAction"), ESearchCase::IgnoreCase))
		{
			UInputAction* NewAction = CreateInputAction(Name, Path, Error);
			if (!NewAction)
			{
				return FToolResult::Fail(Error);
			}
			Asset = NewAction;
		}
		else if (CreateType.Equals(TEXT("InputMappingContext"), ESearchCase::IgnoreCase))
		{
			UInputMappingContext* NewContext = CreateInputMappingContext(Name, Path, Error);
			if (!NewContext)
			{
				return FToolResult::Fail(Error);
			}
			Asset = NewContext;
		}
		else
		{
			return FToolResult::Fail(FString::Printf(
				TEXT("Invalid create_type: '%s'. Must be 'InputAction' or 'InputMappingContext'."), *CreateType));
		}
	}
	else
	{
		// Load existing asset
		FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
		Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

		if (!Asset)
		{
			return FToolResult::Fail(FString::Printf(
				TEXT("Asset not found: %s. Use create_type to create a new asset."), *FullAssetPath));
		}
	}

	// Create transaction for undo/redo
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditEnhancedInput", "AI Edit Enhanced Input: {0}"),
		FText::FromString(Name)));

	// Route based on asset type
	if (UInputAction* Action = Cast<UInputAction>(Asset))
	{
		return EditInputAction(Action, Args);
	}
	else if (UInputMappingContext* Context = Cast<UInputMappingContext>(Asset))
	{
		return EditInputMappingContext(Context, Args);
	}

	return FToolResult::Fail(FString::Printf(
		TEXT("Asset '%s' is not an Enhanced Input asset (got %s). Expected InputAction or InputMappingContext."),
		*Name, *Asset->GetClass()->GetName()));
}

// ============================================================================
// ASSET CREATION
// ============================================================================

UInputAction* FEditEnhancedInputTool::CreateInputAction(const FString& Name, const FString& Path, FString& OutError)
{
	FString SanitizedName;
	FString PackageName = NeoStackToolUtils::BuildPackageName(Name, Path, SanitizedName);

	// Check if already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		FString FullPath = PackageName + TEXT(".") + SanitizedName;
		UObject* Existing = LoadObject<UObject>(nullptr, *FullPath);
		if (UInputAction* ExistingAction = Cast<UInputAction>(Existing))
		{
			return ExistingAction;
		}
		OutError = FString::Printf(TEXT("Asset already exists at %s but is not an InputAction"), *PackageName);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	UInputAction* NewAction = NewObject<UInputAction>(
		Package, UInputAction::StaticClass(), FName(*SanitizedName),
		RF_Public | RF_Standalone | RF_Transactional);

	if (!NewAction)
	{
		OutError = TEXT("Failed to create InputAction object");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(NewAction);
	Package->MarkPackageDirty();

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewAction);
	}

	return NewAction;
}

UInputMappingContext* FEditEnhancedInputTool::CreateInputMappingContext(const FString& Name, const FString& Path, FString& OutError)
{
	FString SanitizedName;
	FString PackageName = NeoStackToolUtils::BuildPackageName(Name, Path, SanitizedName);

	// Check if already exists
	if (FPackageName::DoesPackageExist(PackageName))
	{
		FString FullPath = PackageName + TEXT(".") + SanitizedName;
		UObject* Existing = LoadObject<UObject>(nullptr, *FullPath);
		if (UInputMappingContext* ExistingContext = Cast<UInputMappingContext>(Existing))
		{
			return ExistingContext;
		}
		OutError = FString::Printf(TEXT("Asset already exists at %s but is not an InputMappingContext"), *PackageName);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = FString::Printf(TEXT("Failed to create package: %s"), *PackageName);
		return nullptr;
	}

	UInputMappingContext* NewContext = NewObject<UInputMappingContext>(
		Package, UInputMappingContext::StaticClass(), FName(*SanitizedName),
		RF_Public | RF_Standalone | RF_Transactional);

	if (!NewContext)
	{
		OutError = TEXT("Failed to create InputMappingContext object");
		return nullptr;
	}

	FAssetRegistryModule::AssetCreated(NewContext);
	Package->MarkPackageDirty();

	// Open in editor
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(NewContext);
	}

	return NewContext;
}

// ============================================================================
// INPUT ACTION EDITING
// ============================================================================

FToolResult FEditEnhancedInputTool::EditInputAction(UInputAction* Action, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	Action->Modify();

	// Set value type
	FString ValueTypeStr;
	if (Args->TryGetStringField(TEXT("value_type"), ValueTypeStr) && !ValueTypeStr.IsEmpty())
	{
		EInputActionValueType OldType = Action->ValueType;
		if (ValueTypeStr.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
		{
			Action->ValueType = EInputActionValueType::Boolean;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis1D"), ESearchCase::IgnoreCase))
		{
			Action->ValueType = EInputActionValueType::Axis1D;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis2D"), ESearchCase::IgnoreCase))
		{
			Action->ValueType = EInputActionValueType::Axis2D;
		}
		else if (ValueTypeStr.Equals(TEXT("Axis3D"), ESearchCase::IgnoreCase))
		{
			Action->ValueType = EInputActionValueType::Axis3D;
		}
		else
		{
			Results.Add(FString::Printf(TEXT("Warning: Unknown value_type '%s', expected Boolean/Axis1D/Axis2D/Axis3D"), *ValueTypeStr));
		}

		if (Action->ValueType != OldType)
		{
			Results.Add(FString::Printf(TEXT("Set value_type to %s"), *ValueTypeStr));
			TotalChanges++;
		}
	}

	// Set consume_input
	bool bConsumeInput;
	if (Args->TryGetBoolField(TEXT("consume_input"), bConsumeInput))
	{
		Action->bConsumeInput = bConsumeInput;
		Results.Add(FString::Printf(TEXT("Set consume_input to %s"), bConsumeInput ? TEXT("true") : TEXT("false")));
		TotalChanges++;
	}

	// Set trigger_when_paused
	bool bTriggerWhenPaused;
	if (Args->TryGetBoolField(TEXT("trigger_when_paused"), bTriggerWhenPaused))
	{
		Action->bTriggerWhenPaused = bTriggerWhenPaused;
		Results.Add(FString::Printf(TEXT("Set trigger_when_paused to %s"), bTriggerWhenPaused ? TEXT("true") : TEXT("false")));
		TotalChanges++;
	}

	// Add/Remove triggers
	const TArray<TSharedPtr<FJsonValue>>* AddTriggersArray;
	if (Args->TryGetArrayField(TEXT("add_triggers"), AddTriggersArray))
	{
		TotalChanges += AddTriggers(Action, Action->Triggers, AddTriggersArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveTriggersArray;
	if (Args->TryGetArrayField(TEXT("remove_triggers"), RemoveTriggersArray))
	{
		TotalChanges += RemoveTriggers(Action->Triggers, RemoveTriggersArray, Results);
	}

	// Add/Remove modifiers
	const TArray<TSharedPtr<FJsonValue>>* AddModifiersArray;
	if (Args->TryGetArrayField(TEXT("add_modifiers"), AddModifiersArray))
	{
		TotalChanges += AddModifiers(Action, Action->Modifiers, AddModifiersArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveModifiersArray;
	if (Args->TryGetArrayField(TEXT("remove_modifiers"), RemoveModifiersArray))
	{
		TotalChanges += RemoveModifiers(Action->Modifiers, RemoveModifiersArray, Results);
	}

	Action->GetPackage()->MarkPackageDirty();

	if (TotalChanges == 0)
	{
		// If we just created it, still report success
		FString CreateType;
		if (Args->TryGetStringField(TEXT("create_type"), CreateType) && !CreateType.IsEmpty())
		{
			return FToolResult::Ok(FString::Printf(TEXT("Created InputAction '%s'"), *Action->GetName()));
		}
		return FToolResult::Fail(TEXT("No operations specified for InputAction. Available: value_type, consume_input, trigger_when_paused, add_triggers, remove_triggers, add_modifiers, remove_modifiers"));
	}

	FString Output = FString::Printf(TEXT("Modified InputAction '%s' (%d changes)\n"), *Action->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

// ============================================================================
// INPUT MAPPING CONTEXT EDITING
// ============================================================================

FToolResult FEditEnhancedInputTool::EditInputMappingContext(UInputMappingContext* Context, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	Context->Modify();

	// Add mappings
	const TArray<TSharedPtr<FJsonValue>>* AddMappingsArray;
	if (Args->TryGetArrayField(TEXT("add_mappings"), AddMappingsArray))
	{
		for (const TSharedPtr<FJsonValue>& MappingVal : *AddMappingsArray)
		{
			const TSharedPtr<FJsonObject>* MappingObj;
			if (!MappingVal->TryGetObject(MappingObj) || !MappingObj->IsValid())
			{
				Results.Add(TEXT("Warning: Skipped invalid mapping entry (not an object)"));
				continue;
			}

			// Get action reference
			FString ActionRef;
			if (!(*MappingObj)->TryGetStringField(TEXT("action"), ActionRef) || ActionRef.IsEmpty())
			{
				Results.Add(TEXT("Warning: Skipped mapping with missing 'action' field"));
				continue;
			}

			// Get key
			FString KeyStr;
			if (!(*MappingObj)->TryGetStringField(TEXT("key"), KeyStr) || KeyStr.IsEmpty())
			{
				Results.Add(FString::Printf(TEXT("Warning: Skipped mapping for '%s' with missing 'key' field"), *ActionRef));
				continue;
			}

			// Resolve the InputAction
			FString ResolveError;
			UInputAction* Action = ResolveInputAction(ActionRef, ResolveError);
			if (!Action)
			{
				Results.Add(FString::Printf(TEXT("Warning: Could not resolve action '%s': %s"), *ActionRef, *ResolveError));
				continue;
			}

			// Parse the key
			FKey Key{FName(*KeyStr)};
			if (!Key.IsValid())
			{
				Results.Add(FString::Printf(TEXT("Warning: Invalid key '%s'. Use names like SpaceBar, W, Gamepad_FaceButton_Bottom, Mouse2D, etc."), *KeyStr));
				continue;
			}

			// Add the mapping
			FEnhancedActionKeyMapping& NewMapping = Context->MapKey(Action, Key);

			// Add per-mapping triggers
			const TArray<TSharedPtr<FJsonValue>>* MappingTriggersArray;
			if ((*MappingObj)->TryGetArrayField(TEXT("triggers"), MappingTriggersArray))
			{
				for (const TSharedPtr<FJsonValue>& TrigVal : *MappingTriggersArray)
				{
					const TSharedPtr<FJsonObject>* TrigObj;
					if (TrigVal->TryGetObject(TrigObj) && TrigObj->IsValid())
					{
						FString TrigError;
						UInputTrigger* Trigger = CreateTrigger(Context, *TrigObj, TrigError);
						if (Trigger)
						{
							NewMapping.Triggers.Add(Trigger);
						}
						else
						{
							Results.Add(FString::Printf(TEXT("Warning: trigger for %s+%s: %s"), *ActionRef, *KeyStr, *TrigError));
						}
					}
				}
			}

			// Add per-mapping modifiers
			const TArray<TSharedPtr<FJsonValue>>* MappingModifiersArray;
			if ((*MappingObj)->TryGetArrayField(TEXT("modifiers"), MappingModifiersArray))
			{
				for (const TSharedPtr<FJsonValue>& ModVal : *MappingModifiersArray)
				{
					const TSharedPtr<FJsonObject>* ModObj;
					if (ModVal->TryGetObject(ModObj) && ModObj->IsValid())
					{
						FString ModError;
						UInputModifier* Modifier = CreateModifier(Context, *ModObj, ModError);
						if (Modifier)
						{
							NewMapping.Modifiers.Add(Modifier);
						}
						else
						{
							Results.Add(FString::Printf(TEXT("Warning: modifier for %s+%s: %s"), *ActionRef, *KeyStr, *ModError));
						}
					}
				}
			}

			Results.Add(FString::Printf(TEXT("Added mapping: %s -> %s (triggers=%d, modifiers=%d)"),
				*Action->GetName(), *KeyStr, NewMapping.Triggers.Num(), NewMapping.Modifiers.Num()));
			TotalChanges++;
		}
	}

	// Remove mappings
	const TArray<TSharedPtr<FJsonValue>>* RemoveMappingsArray;
	if (Args->TryGetArrayField(TEXT("remove_mappings"), RemoveMappingsArray))
	{
		// Collect indices to remove (descending order to preserve indices)
		TArray<int32> IndicesToRemove;

		for (const TSharedPtr<FJsonValue>& RemoveVal : *RemoveMappingsArray)
		{
			// Try as index first
			int32 Index;
			if (RemoveVal->TryGetNumber(Index))
			{
				if (Index >= 0 && Index < Context->GetMappings().Num())
				{
					IndicesToRemove.AddUnique(Index);
				}
				else
				{
					Results.Add(FString::Printf(TEXT("Warning: Mapping index %d out of range (0-%d)"), Index, Context->GetMappings().Num() - 1));
				}
				continue;
			}

			// Try as {action, key} object
			const TSharedPtr<FJsonObject>* RemoveObj;
			if (RemoveVal->TryGetObject(RemoveObj) && RemoveObj->IsValid())
			{
				// Check for index field in object
				int32 ObjIndex;
				if ((*RemoveObj)->TryGetNumberField(TEXT("index"), ObjIndex))
				{
					if (ObjIndex >= 0 && ObjIndex < Context->GetMappings().Num())
					{
						IndicesToRemove.AddUnique(ObjIndex);
					}
					else
					{
						Results.Add(FString::Printf(TEXT("Warning: Mapping index %d out of range"), ObjIndex));
					}
					continue;
				}

				FString ActionRef, KeyStr;
				(*RemoveObj)->TryGetStringField(TEXT("action"), ActionRef);
				(*RemoveObj)->TryGetStringField(TEXT("key"), KeyStr);

				if (!ActionRef.IsEmpty() && !KeyStr.IsEmpty())
				{
					FString ResolveError;
					UInputAction* Action = ResolveInputAction(ActionRef, ResolveError);
					if (Action)
					{
						FKey Key{FName(*KeyStr)};
						// Find matching mapping
						const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
						for (int32 i = 0; i < Mappings.Num(); i++)
						{
							if (Mappings[i].Action == Action && Mappings[i].Key == Key)
							{
								IndicesToRemove.AddUnique(i);
								break;
							}
						}
					}
				}
			}
		}

		// Sort descending and remove
		IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });

		for (int32 Index : IndicesToRemove)
		{
			const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
			if (Index >= 0 && Index < Mappings.Num())
			{
				FString ActionName = Mappings[Index].Action ? Mappings[Index].Action->GetName() : TEXT("(none)");
				FString KeyName = Mappings[Index].Key.GetFName().ToString();

				Context->UnmapKey(Mappings[Index].Action, Mappings[Index].Key);
				Results.Add(FString::Printf(TEXT("Removed mapping at index %d: %s -> %s"), Index, *ActionName, *KeyName));
				TotalChanges++;
			}
		}
	}

	// Modify mappings
	const TArray<TSharedPtr<FJsonValue>>* ModifyMappingsArray;
	if (Args->TryGetArrayField(TEXT("modify_mappings"), ModifyMappingsArray))
	{
		for (const TSharedPtr<FJsonValue>& ModifyVal : *ModifyMappingsArray)
		{
			const TSharedPtr<FJsonObject>* ModifyObj;
			if (!ModifyVal->TryGetObject(ModifyObj) || !ModifyObj->IsValid())
			{
				Results.Add(TEXT("Warning: Skipped invalid modify_mappings entry"));
				continue;
			}

			int32 Index;
			if (!(*ModifyObj)->TryGetNumberField(TEXT("index"), Index))
			{
				Results.Add(TEXT("Warning: modify_mappings entry missing required 'index' field"));
				continue;
			}

			TArray<FEnhancedActionKeyMapping>& Mappings = const_cast<TArray<FEnhancedActionKeyMapping>&>(Context->GetMappings());
			if (Index < 0 || Index >= Mappings.Num())
			{
				Results.Add(FString::Printf(TEXT("Warning: Mapping index %d out of range (0-%d)"), Index, Mappings.Num() - 1));
				continue;
			}

			FEnhancedActionKeyMapping& Mapping = Mappings[Index];
			TArray<FString> ModResults;

			// Change key
			FString NewKeyStr;
			if ((*ModifyObj)->TryGetStringField(TEXT("new_key"), NewKeyStr) && !NewKeyStr.IsEmpty())
			{
				FKey NewKey{FName(*NewKeyStr)};
				if (NewKey.IsValid())
				{
					Mapping.Key = NewKey;
					ModResults.Add(FString::Printf(TEXT("key -> %s"), *NewKeyStr));
				}
				else
				{
					ModResults.Add(FString::Printf(TEXT("Warning: Invalid key '%s'"), *NewKeyStr));
				}
			}

			// Add/Remove triggers on this mapping
			const TArray<TSharedPtr<FJsonValue>>* MappingAddTriggers;
			if ((*ModifyObj)->TryGetArrayField(TEXT("add_triggers"), MappingAddTriggers))
			{
				int32 Count = AddTriggers(Context, Mapping.Triggers, MappingAddTriggers, ModResults);
				if (Count > 0) ModResults.Add(FString::Printf(TEXT("Added %d trigger(s)"), Count));
			}

			const TArray<TSharedPtr<FJsonValue>>* MappingRemoveTriggers;
			if ((*ModifyObj)->TryGetArrayField(TEXT("remove_triggers"), MappingRemoveTriggers))
			{
				int32 Count = RemoveTriggers(Mapping.Triggers, MappingRemoveTriggers, ModResults);
				if (Count > 0) ModResults.Add(FString::Printf(TEXT("Removed %d trigger(s)"), Count));
			}

			// Add/Remove modifiers on this mapping
			const TArray<TSharedPtr<FJsonValue>>* MappingAddModifiers;
			if ((*ModifyObj)->TryGetArrayField(TEXT("add_modifiers"), MappingAddModifiers))
			{
				int32 Count = AddModifiers(Context, Mapping.Modifiers, MappingAddModifiers, ModResults);
				if (Count > 0) ModResults.Add(FString::Printf(TEXT("Added %d modifier(s)"), Count));
			}

			const TArray<TSharedPtr<FJsonValue>>* MappingRemoveModifiers;
			if ((*ModifyObj)->TryGetArrayField(TEXT("remove_modifiers"), MappingRemoveModifiers))
			{
				int32 Count = RemoveModifiers(Mapping.Modifiers, MappingRemoveModifiers, ModResults);
				if (Count > 0) ModResults.Add(FString::Printf(TEXT("Removed %d modifier(s)"), Count));
			}

			if (ModResults.Num() > 0)
			{
				FString ActionName = Mapping.Action ? Mapping.Action->GetName() : TEXT("(none)");
				Results.Add(FString::Printf(TEXT("Modified mapping[%d] (%s): %s"),
					Index, *ActionName, *FString::Join(ModResults, TEXT(", "))));
				TotalChanges++;
			}
		}
	}

	Context->GetPackage()->MarkPackageDirty();

	if (TotalChanges == 0)
	{
		FString CreateType;
		if (Args->TryGetStringField(TEXT("create_type"), CreateType) && !CreateType.IsEmpty())
		{
			return FToolResult::Ok(FString::Printf(TEXT("Created InputMappingContext '%s'"), *Context->GetName()));
		}
		return FToolResult::Fail(TEXT("No operations specified for InputMappingContext. Available: add_mappings, remove_mappings, modify_mappings"));
	}

	FString Output = FString::Printf(TEXT("Modified InputMappingContext '%s' (%d changes)\n"), *Context->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

// ============================================================================
// TRIGGER/MODIFIER FACTORY FUNCTIONS
// ============================================================================

UInputTrigger* FEditEnhancedInputTool::CreateTrigger(UObject* Outer, const TSharedPtr<FJsonObject>& Config, FString& OutError)
{
	FString Type;
	if (!Config->TryGetStringField(TEXT("type"), Type) || Type.IsEmpty())
	{
		OutError = TEXT("Missing 'type' field in trigger config");
		return nullptr;
	}

	UInputTrigger* Trigger = nullptr;

	if (Type.Equals(TEXT("Down"), ESearchCase::IgnoreCase))
	{
		Trigger = NewObject<UInputTriggerDown>(Outer);
	}
	else if (Type.Equals(TEXT("Pressed"), ESearchCase::IgnoreCase))
	{
		Trigger = NewObject<UInputTriggerPressed>(Outer);
	}
	else if (Type.Equals(TEXT("Released"), ESearchCase::IgnoreCase))
	{
		Trigger = NewObject<UInputTriggerReleased>(Outer);
	}
	else if (Type.Equals(TEXT("Hold"), ESearchCase::IgnoreCase))
	{
		UInputTriggerHold* Hold = NewObject<UInputTriggerHold>(Outer);
		double HoldTime;
		if (Config->TryGetNumberField(TEXT("hold_time"), HoldTime))
		{
			Hold->HoldTimeThreshold = static_cast<float>(HoldTime);
		}
		bool bIsOneShot;
		if (Config->TryGetBoolField(TEXT("is_one_shot"), bIsOneShot))
		{
			Hold->bIsOneShot = bIsOneShot;
		}
		Trigger = Hold;
	}
	else if (Type.Equals(TEXT("HoldAndRelease"), ESearchCase::IgnoreCase))
	{
		UInputTriggerHoldAndRelease* HoldRelease = NewObject<UInputTriggerHoldAndRelease>(Outer);
		double HoldTime;
		if (Config->TryGetNumberField(TEXT("hold_time"), HoldTime))
		{
			HoldRelease->HoldTimeThreshold = static_cast<float>(HoldTime);
		}
		Trigger = HoldRelease;
	}
	else if (Type.Equals(TEXT("Tap"), ESearchCase::IgnoreCase))
	{
		UInputTriggerTap* Tap = NewObject<UInputTriggerTap>(Outer);
		double TapTime;
		if (Config->TryGetNumberField(TEXT("tap_time"), TapTime))
		{
			Tap->TapReleaseTimeThreshold = static_cast<float>(TapTime);
		}
		Trigger = Tap;
	}
	else if (Type.Equals(TEXT("Pulse"), ESearchCase::IgnoreCase))
	{
		UInputTriggerPulse* Pulse = NewObject<UInputTriggerPulse>(Outer);
		double Interval;
		if (Config->TryGetNumberField(TEXT("interval"), Interval))
		{
			Pulse->Interval = static_cast<float>(Interval);
		}
		bool bTriggerOnStart;
		if (Config->TryGetBoolField(TEXT("trigger_on_start"), bTriggerOnStart))
		{
			Pulse->bTriggerOnStart = bTriggerOnStart;
		}
		Trigger = Pulse;
	}
	else if (Type.Equals(TEXT("ChordAction"), ESearchCase::IgnoreCase))
	{
		UInputTriggerChordAction* Chord = NewObject<UInputTriggerChordAction>(Outer);
		FString ChordActionRef;
		if (Config->TryGetStringField(TEXT("chord_action"), ChordActionRef) && !ChordActionRef.IsEmpty())
		{
			FString ResolveError;
			UInputAction* ChordAction = ResolveInputAction(ChordActionRef, ResolveError);
			if (ChordAction)
			{
				Chord->ChordAction = ChordAction;
			}
			else
			{
				OutError = FString::Printf(TEXT("Could not resolve chord_action '%s': %s"), *ChordActionRef, *ResolveError);
				return nullptr;
			}
		}
		Trigger = Chord;
	}
	else if (Type.Equals(TEXT("Combo"), ESearchCase::IgnoreCase))
	{
		UInputTriggerCombo* Combo = NewObject<UInputTriggerCombo>(Outer);
		const TArray<TSharedPtr<FJsonValue>>* ComboActionsArray;
		if (Config->TryGetArrayField(TEXT("combo_actions"), ComboActionsArray))
		{
			for (const TSharedPtr<FJsonValue>& ActionVal : *ComboActionsArray)
			{
				FString ActionRef;
				if (ActionVal->TryGetString(ActionRef) && !ActionRef.IsEmpty())
				{
					FString ResolveError;
					UInputAction* ComboAction = ResolveInputAction(ActionRef, ResolveError);
					if (ComboAction)
					{
						FInputComboStepData StepData;
						StepData.ComboStepAction = ComboAction;
						Combo->ComboActions.Add(StepData);
					}
				}
			}
		}
		Trigger = Combo;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown trigger type: '%s'. Available: Down, Pressed, Released, Hold, HoldAndRelease, Tap, Pulse, ChordAction, Combo"), *Type);
		return nullptr;
	}

	// Set common actuation threshold
	if (Trigger)
	{
		double Actuation;
		if (Config->TryGetNumberField(TEXT("actuation_threshold"), Actuation))
		{
			Trigger->ActuationThreshold = static_cast<float>(Actuation);
		}
	}

	return Trigger;
}

UInputModifier* FEditEnhancedInputTool::CreateModifier(UObject* Outer, const TSharedPtr<FJsonObject>& Config, FString& OutError)
{
	FString Type;
	if (!Config->TryGetStringField(TEXT("type"), Type) || Type.IsEmpty())
	{
		OutError = TEXT("Missing 'type' field in modifier config");
		return nullptr;
	}

	UInputModifier* Modifier = nullptr;

	if (Type.Equals(TEXT("DeadZone"), ESearchCase::IgnoreCase))
	{
		UInputModifierDeadZone* DZ = NewObject<UInputModifierDeadZone>(Outer);
		double Lower, Upper;
		if (Config->TryGetNumberField(TEXT("lower_threshold"), Lower))
		{
			DZ->LowerThreshold = static_cast<float>(Lower);
		}
		if (Config->TryGetNumberField(TEXT("upper_threshold"), Upper))
		{
			DZ->UpperThreshold = static_cast<float>(Upper);
		}
		FString DZType;
		if (Config->TryGetStringField(TEXT("dead_zone_type"), DZType))
		{
			if (DZType.Equals(TEXT("Axial"), ESearchCase::IgnoreCase))
			{
				DZ->Type = EDeadZoneType::Axial;
			}
			else if (DZType.Equals(TEXT("Radial"), ESearchCase::IgnoreCase))
			{
				DZ->Type = EDeadZoneType::Radial;
			}
		}
		Modifier = DZ;
	}
	else if (Type.Equals(TEXT("Negate"), ESearchCase::IgnoreCase))
	{
		UInputModifierNegate* Negate = NewObject<UInputModifierNegate>(Outer);
		bool bVal;
		if (Config->TryGetBoolField(TEXT("negate_x"), bVal)) Negate->bX = bVal;
		if (Config->TryGetBoolField(TEXT("negate_y"), bVal)) Negate->bY = bVal;
		if (Config->TryGetBoolField(TEXT("negate_z"), bVal)) Negate->bZ = bVal;
		Modifier = Negate;
	}
	else if (Type.Equals(TEXT("Scalar"), ESearchCase::IgnoreCase))
	{
		UInputModifierScalar* Scalar = NewObject<UInputModifierScalar>(Outer);
		double Val;
		if (Config->TryGetNumberField(TEXT("scalar_x"), Val)) Scalar->Scalar.X = Val;
		if (Config->TryGetNumberField(TEXT("scalar_y"), Val)) Scalar->Scalar.Y = Val;
		if (Config->TryGetNumberField(TEXT("scalar_z"), Val)) Scalar->Scalar.Z = Val;
		Modifier = Scalar;
	}
	else if (Type.Equals(TEXT("ScaleByDeltaTime"), ESearchCase::IgnoreCase))
	{
		Modifier = NewObject<UInputModifierScaleByDeltaTime>(Outer);
	}
	else if (Type.Equals(TEXT("Swizzle"), ESearchCase::IgnoreCase))
	{
		UInputModifierSwizzleAxis* Swizzle = NewObject<UInputModifierSwizzleAxis>(Outer);
		FString Order;
		if (Config->TryGetStringField(TEXT("swizzle_order"), Order))
		{
			if (Order.Equals(TEXT("YXZ"), ESearchCase::IgnoreCase)) Swizzle->Order = EInputAxisSwizzle::YXZ;
			else if (Order.Equals(TEXT("ZYX"), ESearchCase::IgnoreCase)) Swizzle->Order = EInputAxisSwizzle::ZYX;
			else if (Order.Equals(TEXT("XZY"), ESearchCase::IgnoreCase)) Swizzle->Order = EInputAxisSwizzle::XZY;
			else if (Order.Equals(TEXT("YZX"), ESearchCase::IgnoreCase)) Swizzle->Order = EInputAxisSwizzle::YZX;
			else if (Order.Equals(TEXT("ZXY"), ESearchCase::IgnoreCase)) Swizzle->Order = EInputAxisSwizzle::ZXY;
		}
		Modifier = Swizzle;
	}
	else if (Type.Equals(TEXT("Smooth"), ESearchCase::IgnoreCase))
	{
		Modifier = NewObject<UInputModifierSmooth>(Outer);
	}
	else if (Type.Equals(TEXT("FOVScaling"), ESearchCase::IgnoreCase))
	{
		UInputModifierFOVScaling* FOV = NewObject<UInputModifierFOVScaling>(Outer);
		double Scale;
		if (Config->TryGetNumberField(TEXT("fov_scale"), Scale))
		{
			FOV->FOVScale = static_cast<float>(Scale);
		}
		Modifier = FOV;
	}
	else if (Type.Equals(TEXT("ToWorldSpace"), ESearchCase::IgnoreCase))
	{
		Modifier = NewObject<UInputModifierToWorldSpace>(Outer);
	}
	else if (Type.Equals(TEXT("ResponseCurveExponential"), ESearchCase::IgnoreCase))
	{
		UInputModifierResponseCurveExponential* Exp = NewObject<UInputModifierResponseCurveExponential>(Outer);
		double Val;
		if (Config->TryGetNumberField(TEXT("exponent_x"), Val)) Exp->CurveExponent.X = Val;
		if (Config->TryGetNumberField(TEXT("exponent_y"), Val)) Exp->CurveExponent.Y = Val;
		if (Config->TryGetNumberField(TEXT("exponent_z"), Val)) Exp->CurveExponent.Z = Val;
		Modifier = Exp;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown modifier type: '%s'. Available: DeadZone, Negate, Scalar, ScaleByDeltaTime, Swizzle, Smooth, FOVScaling, ToWorldSpace, ResponseCurveExponential"), *Type);
		return nullptr;
	}

	return Modifier;
}

// ============================================================================
// SHARED TRIGGER/MODIFIER OPERATIONS
// ============================================================================

int32 FEditEnhancedInputTool::AddTriggers(UObject* Outer, TArray<TObjectPtr<UInputTrigger>>& TargetArray,
	const TArray<TSharedPtr<FJsonValue>>* TriggersArray, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& TrigVal : *TriggersArray)
	{
		const TSharedPtr<FJsonObject>* TrigObj;
		if (!TrigVal->TryGetObject(TrigObj) || !TrigObj->IsValid())
		{
			OutResults.Add(TEXT("Warning: Skipped invalid trigger entry"));
			continue;
		}

		FString Error;
		UInputTrigger* Trigger = CreateTrigger(Outer, *TrigObj, Error);
		if (Trigger)
		{
			TargetArray.Add(Trigger);
			FString TypeName = Trigger->GetClass()->GetName();
			TypeName.RemoveFromStart(TEXT("InputTrigger"));
			OutResults.Add(FString::Printf(TEXT("Added trigger: %s"), *TypeName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Warning: %s"), *Error));
		}
	}
	return Count;
}

int32 FEditEnhancedInputTool::RemoveTriggers(TArray<TObjectPtr<UInputTrigger>>& TargetArray,
	const TArray<TSharedPtr<FJsonValue>>* IndicesArray, TArray<FString>& OutResults)
{
	TArray<int32> Indices;
	for (const TSharedPtr<FJsonValue>& IndexVal : *IndicesArray)
	{
		int32 Index;
		if (IndexVal->TryGetNumber(Index))
		{
			if (Index >= 0 && Index < TargetArray.Num())
			{
				Indices.AddUnique(Index);
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("Warning: Trigger index %d out of range (0-%d)"), Index, TargetArray.Num() - 1));
			}
		}
	}

	// Sort descending to remove from end first
	Indices.Sort([](int32 A, int32 B) { return A > B; });

	int32 Count = 0;
	for (int32 Index : Indices)
	{
		FString TypeName = TargetArray[Index] ? TargetArray[Index]->GetClass()->GetName() : TEXT("null");
		TypeName.RemoveFromStart(TEXT("InputTrigger"));
		TargetArray.RemoveAt(Index);
		OutResults.Add(FString::Printf(TEXT("Removed trigger[%d]: %s"), Index, *TypeName));
		Count++;
	}
	return Count;
}

int32 FEditEnhancedInputTool::AddModifiers(UObject* Outer, TArray<TObjectPtr<UInputModifier>>& TargetArray,
	const TArray<TSharedPtr<FJsonValue>>* ModifiersArray, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const TSharedPtr<FJsonValue>& ModVal : *ModifiersArray)
	{
		const TSharedPtr<FJsonObject>* ModObj;
		if (!ModVal->TryGetObject(ModObj) || !ModObj->IsValid())
		{
			OutResults.Add(TEXT("Warning: Skipped invalid modifier entry"));
			continue;
		}

		FString Error;
		UInputModifier* Modifier = CreateModifier(Outer, *ModObj, Error);
		if (Modifier)
		{
			TargetArray.Add(Modifier);
			FString TypeName = Modifier->GetClass()->GetName();
			TypeName.RemoveFromStart(TEXT("InputModifier"));
			OutResults.Add(FString::Printf(TEXT("Added modifier: %s"), *TypeName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Warning: %s"), *Error));
		}
	}
	return Count;
}

int32 FEditEnhancedInputTool::RemoveModifiers(TArray<TObjectPtr<UInputModifier>>& TargetArray,
	const TArray<TSharedPtr<FJsonValue>>* IndicesArray, TArray<FString>& OutResults)
{
	TArray<int32> Indices;
	for (const TSharedPtr<FJsonValue>& IndexVal : *IndicesArray)
	{
		int32 Index;
		if (IndexVal->TryGetNumber(Index))
		{
			if (Index >= 0 && Index < TargetArray.Num())
			{
				Indices.AddUnique(Index);
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("Warning: Modifier index %d out of range (0-%d)"), Index, TargetArray.Num() - 1));
			}
		}
	}

	Indices.Sort([](int32 A, int32 B) { return A > B; });

	int32 Count = 0;
	for (int32 Index : Indices)
	{
		FString TypeName = TargetArray[Index] ? TargetArray[Index]->GetClass()->GetName() : TEXT("null");
		TypeName.RemoveFromStart(TEXT("InputModifier"));
		TargetArray.RemoveAt(Index);
		OutResults.Add(FString::Printf(TEXT("Removed modifier[%d]: %s"), Index, *TypeName));
		Count++;
	}
	return Count;
}

// ============================================================================
// HELPERS
// ============================================================================

UInputAction* FEditEnhancedInputTool::ResolveInputAction(const FString& ActionRef, FString& OutError)
{
	if (ActionRef.IsEmpty())
	{
		OutError = TEXT("Empty action reference");
		return nullptr;
	}

	// Try as full asset path first
	if (ActionRef.Contains(TEXT("/")))
	{
		FString FullPath = ActionRef;
		if (!FullPath.Contains(TEXT(".")))
		{
			// Add asset name suffix: /Game/Input/IA_Jump -> /Game/Input/IA_Jump.IA_Jump
			FString AssetName = FPaths::GetBaseFilename(FullPath);
			FullPath = FullPath + TEXT(".") + AssetName;
		}

		UInputAction* Action = LoadObject<UInputAction>(nullptr, *FullPath);
		if (Action)
		{
			return Action;
		}
	}

	// Search by name in asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByClass(UInputAction::StaticClass()->GetClassPathName(), Assets);

	for (const FAssetData& AssetData : Assets)
	{
		if (AssetData.AssetName.ToString().Equals(ActionRef, ESearchCase::IgnoreCase))
		{
			UInputAction* Action = Cast<UInputAction>(AssetData.GetAsset());
			if (Action)
			{
				return Action;
			}
		}
	}

	// Try building path with common prefixes
	TArray<FString> Prefixes = { TEXT("/Game/Input/"), TEXT("/Game/Input/Actions/"), TEXT("/Game/") };
	for (const FString& Prefix : Prefixes)
	{
		FString TestPath = Prefix + ActionRef + TEXT(".") + ActionRef;
		UInputAction* Action = LoadObject<UInputAction>(nullptr, *TestPath);
		if (Action)
		{
			return Action;
		}
	}

	OutError = FString::Printf(TEXT("InputAction '%s' not found. Provide a full path or ensure the asset exists."), *ActionRef);
	return nullptr;
}
