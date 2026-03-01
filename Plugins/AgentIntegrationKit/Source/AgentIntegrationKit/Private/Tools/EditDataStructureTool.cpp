// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/EditDataStructureTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// For struct editing
#include "Engine/UserDefinedStruct.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"
#include "EdGraphSchema_K2.h"

// For enum editing
#include "Engine/UserDefinedEnum.h"
#include "Kismet2/EnumEditorUtils.h"

// For datatable editing
#include "Engine/DataTable.h"
#include "Serialization/JsonSerializer.h"

// For ChooserTable editing
#include "Chooser.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "OutputObjectColumn.h"

// For StringTable editing
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

// For editor support
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "AssetRegistry/AssetRegistryModule.h"

// Transaction support for undo/redo
#include "ScopedTransaction.h"

TSharedPtr<FJsonObject> FEditDataStructureTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Struct, Enum, DataTable, or StringTable asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> AddFieldsProp = MakeShared<FJsonObject>();
	AddFieldsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddFieldsProp->SetStringField(TEXT("description"), TEXT("Struct fields to add: [{name, type (Boolean/Integer/Float/String/Vector/etc), default_value, description}]"));
	Properties->SetObjectField(TEXT("add_fields"), AddFieldsProp);

	TSharedPtr<FJsonObject> RemoveFieldsProp = MakeShared<FJsonObject>();
	RemoveFieldsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveFieldsProp->SetStringField(TEXT("description"), TEXT("Struct field names to remove"));
	Properties->SetObjectField(TEXT("remove_fields"), RemoveFieldsProp);

	TSharedPtr<FJsonObject> ModifyFieldsProp = MakeShared<FJsonObject>();
	ModifyFieldsProp->SetStringField(TEXT("type"), TEXT("array"));
	ModifyFieldsProp->SetStringField(TEXT("description"), TEXT("Struct fields to modify: [{name, new_name, type, default_value, description}]"));
	Properties->SetObjectField(TEXT("modify_fields"), ModifyFieldsProp);

	TSharedPtr<FJsonObject> AddValuesProp = MakeShared<FJsonObject>();
	AddValuesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddValuesProp->SetStringField(TEXT("description"), TEXT("Enum values to add: [{name, display_name}]"));
	Properties->SetObjectField(TEXT("add_values"), AddValuesProp);

	TSharedPtr<FJsonObject> RemoveValuesProp = MakeShared<FJsonObject>();
	RemoveValuesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveValuesProp->SetStringField(TEXT("description"), TEXT("Enum value names to remove"));
	Properties->SetObjectField(TEXT("remove_values"), RemoveValuesProp);

	TSharedPtr<FJsonObject> ModifyValuesProp = MakeShared<FJsonObject>();
	ModifyValuesProp->SetStringField(TEXT("type"), TEXT("array"));
	ModifyValuesProp->SetStringField(TEXT("description"), TEXT("Enum values to modify: [{index, display_name}]"));
	Properties->SetObjectField(TEXT("modify_values"), ModifyValuesProp);

	TSharedPtr<FJsonObject> AddRowsProp = MakeShared<FJsonObject>();
	AddRowsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddRowsProp->SetStringField(TEXT("description"), TEXT("DataTable rows to add: [{row_name, values:{column:value}}]"));
	Properties->SetObjectField(TEXT("add_rows"), AddRowsProp);

	TSharedPtr<FJsonObject> RemoveRowsProp = MakeShared<FJsonObject>();
	RemoveRowsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveRowsProp->SetStringField(TEXT("description"), TEXT("DataTable row names to remove"));
	Properties->SetObjectField(TEXT("remove_rows"), RemoveRowsProp);

	TSharedPtr<FJsonObject> ModifyRowsProp = MakeShared<FJsonObject>();
	ModifyRowsProp->SetStringField(TEXT("type"), TEXT("array"));
	ModifyRowsProp->SetStringField(TEXT("description"), TEXT("DataTable rows to modify: [{row_name, values:{column:value}}]"));
	Properties->SetObjectField(TEXT("modify_rows"), ModifyRowsProp);

	TSharedPtr<FJsonObject> ImportJsonProp = MakeShared<FJsonObject>();
	ImportJsonProp->SetStringField(TEXT("type"), TEXT("string"));
	ImportJsonProp->SetStringField(TEXT("description"), TEXT("Bulk import JSON string for DataTable (REPLACES all existing rows). Format: [{\"Name\":\"Row1\",\"Column1\":\"value\"}, ...]. More efficient for 50+ rows."));
	Properties->SetObjectField(TEXT("import_json"), ImportJsonProp);

	TSharedPtr<FJsonObject> AppendJsonProp = MakeShared<FJsonObject>();
	AppendJsonProp->SetStringField(TEXT("type"), TEXT("string"));
	AppendJsonProp->SetStringField(TEXT("description"), TEXT("Bulk append JSON string for DataTable (ADDS to existing rows). Same format as import_json. Use this to add many rows without clearing existing data."));
	Properties->SetObjectField(TEXT("append_json"), AppendJsonProp);

	// ChooserTable operations
	TSharedPtr<FJsonObject> ReplaceRefsProp = MakeShared<FJsonObject>();
	ReplaceRefsProp->SetStringField(TEXT("type"), TEXT("array"));
	ReplaceRefsProp->SetStringField(TEXT("description"), TEXT("ChooserTable: Replace object references by pattern. Each entry: {search (substring to find), replace (replacement string), prefix (add prefix like 'RTG_Rap_'), suffix (add suffix), limit (max replacements)}. Example: [{\"prefix\": \"RTG_Rap_\", \"limit\": 10}] adds prefix to first 10 animation names."));
	Properties->SetObjectField(TEXT("replace_references"), ReplaceRefsProp);

	// StringTable operations
	TSharedPtr<FJsonObject> AddEntriesProp = MakeShared<FJsonObject>();
	AddEntriesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddEntriesProp->SetStringField(TEXT("description"), TEXT("StringTable entries to add: [{key, value}]. Each entry is a localization key-value pair."));
	Properties->SetObjectField(TEXT("add_entries"), AddEntriesProp);

	TSharedPtr<FJsonObject> RemoveEntriesProp = MakeShared<FJsonObject>();
	RemoveEntriesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveEntriesProp->SetStringField(TEXT("description"), TEXT("StringTable entry keys to remove"));
	Properties->SetObjectField(TEXT("remove_entries"), RemoveEntriesProp);

	TSharedPtr<FJsonObject> ModifyEntriesProp = MakeShared<FJsonObject>();
	ModifyEntriesProp->SetStringField(TEXT("type"), TEXT("array"));
	ModifyEntriesProp->SetStringField(TEXT("description"), TEXT("StringTable entries to modify: [{key, value}]. Key must already exist."));
	Properties->SetObjectField(TEXT("modify_entries"), ModifyEntriesProp);

	TSharedPtr<FJsonObject> SetNamespaceProp = MakeShared<FJsonObject>();
	SetNamespaceProp->SetStringField(TEXT("type"), TEXT("string"));
	SetNamespaceProp->SetStringField(TEXT("description"), TEXT("StringTable: Set the namespace for all entries (used for localization grouping)"));
	Properties->SetObjectField(TEXT("set_namespace"), SetNamespaceProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditDataStructureTool::Execute(const TSharedPtr<FJsonObject>& Args)
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

	// Build asset path and load
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);

	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *FullAssetPath));
	}

	// Create transaction for undo/redo support
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditDataStructure", "AI Edit Data Structure: {0}"),
		FText::FromString(Name)));

	// Route based on asset type
	if (UUserDefinedStruct* Struct = Cast<UUserDefinedStruct>(Asset))
	{
		return EditStruct(Struct, Args);
	}
	else if (UUserDefinedEnum* Enum = Cast<UUserDefinedEnum>(Asset))
	{
		return EditEnum(Enum, Args);
	}
	else if (UDataTable* DataTable = Cast<UDataTable>(Asset))
	{
		return EditDataTable(DataTable, Args);
	}
	else if (UChooserTable* ChooserTable = Cast<UChooserTable>(Asset))
	{
		return EditChooserTable(ChooserTable, Args);
	}
	else if (UStringTable* StringTable = Cast<UStringTable>(Asset))
	{
		return EditStringTable(StringTable, Args);
	}

	return FToolResult::Fail(FString::Printf(TEXT("Unsupported asset type for editing: %s"), *Asset->GetClass()->GetName()));
}

// ============================================================================
// STRUCT OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditStruct(UUserDefinedStruct* Struct, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process add_fields
	const TArray<TSharedPtr<FJsonValue>>* AddFieldsArray;
	if (Args->TryGetArrayField(TEXT("add_fields"), AddFieldsArray))
	{
		TotalChanges += AddStructFields(Struct, AddFieldsArray, Results);
	}

	// Process remove_fields
	const TArray<TSharedPtr<FJsonValue>>* RemoveFieldsArray;
	if (Args->TryGetArrayField(TEXT("remove_fields"), RemoveFieldsArray))
	{
		TotalChanges += RemoveStructFields(Struct, RemoveFieldsArray, Results);
	}

	// Process modify_fields
	const TArray<TSharedPtr<FJsonValue>>* ModifyFieldsArray;
	if (Args->TryGetArrayField(TEXT("modify_fields"), ModifyFieldsArray))
	{
		TotalChanges += ModifyStructFields(Struct, ModifyFieldsArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_fields, remove_fields, or modify_fields."));
	}

	// Mark dirty
	Struct->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified struct %s (%d changes)\n"), *Struct->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults)
{
	if (!FieldsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		const TSharedPtr<FJsonObject>* FieldObj;
		if (!FieldValue->TryGetObject(FieldObj))
		{
			continue;
		}

		FStructFieldOp Op = ParseStructFieldOp(*FieldObj);
		if (Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped field with no name"));
			continue;
		}

		// Check if field already exists
		if (FindStructFieldIndex(Struct, Op.Name) >= 0)
		{
			OutResults.Add(FString::Printf(TEXT("Field '%s' already exists"), *Op.Name));
			continue;
		}

		// Convert type to pin type
		FEdGraphPinType PinType = TypeNameToPinType(Op.Type);

		// Add the field
		if (FStructureEditorUtils::AddVariable(Struct, PinType))
		{
			TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
			if (VarDescArray.Num() > 0)
			{
				FStructVariableDescription& NewVar = VarDescArray.Last();

				// Rename the variable
				FStructureEditorUtils::RenameVariable(Struct, NewVar.VarGuid, Op.Name);

				// Set default value if provided
				if (!Op.DefaultValue.IsEmpty())
				{
					FStructureEditorUtils::ChangeVariableDefaultValue(Struct, NewVar.VarGuid, Op.DefaultValue);
				}

				// Set description if provided
				if (!Op.Description.IsEmpty())
				{
					NewVar.ToolTip = Op.Description;
				}

				OutResults.Add(FString::Printf(TEXT("Added field '%s' (%s)"), *Op.Name, *Op.Type));
				Added++;
			}
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add field '%s'"), *Op.Name));
		}
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults)
{
	if (!FieldsArray) return 0;

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		FString FieldName;
		if (!FieldValue->TryGetString(FieldName))
		{
			continue;
		}

		TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
		bool bFound = false;

		for (const FStructVariableDescription& VarDesc : VarDescArray)
		{
			if (VarDesc.VarName.ToString().Equals(FieldName, ESearchCase::IgnoreCase))
			{
				if (FStructureEditorUtils::RemoveVariable(Struct, VarDesc.VarGuid))
				{
					OutResults.Add(FString::Printf(TEXT("Removed field '%s'"), *FieldName));
					Removed++;
					bFound = true;
				}
				break;
			}
		}

		if (!bFound)
		{
			OutResults.Add(FString::Printf(TEXT("Field '%s' not found"), *FieldName));
		}
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults)
{
	if (!FieldsArray) return 0;

	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& FieldValue : *FieldsArray)
	{
		const TSharedPtr<FJsonObject>* FieldObj;
		if (!FieldValue->TryGetObject(FieldObj))
		{
			continue;
		}

		FStructFieldOp Op = ParseStructFieldOp(*FieldObj);
		if (Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped modification with no field name"));
			continue;
		}

		TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
		bool bFound = false;

		for (FStructVariableDescription& VarDesc : VarDescArray)
		{
			if (VarDesc.VarName.ToString().Equals(Op.Name, ESearchCase::IgnoreCase))
			{
				bFound = true;
				TArray<FString> Changes;

				// Rename if new_name is provided
				if (!Op.NewName.IsEmpty() && !Op.NewName.Equals(Op.Name))
				{
					FStructureEditorUtils::RenameVariable(Struct, VarDesc.VarGuid, Op.NewName);
					Changes.Add(FString::Printf(TEXT("renamed to '%s'"), *Op.NewName));
				}

				// Change type if provided
				if (!Op.Type.IsEmpty())
				{
					FEdGraphPinType NewPinType = TypeNameToPinType(Op.Type);
					if (FStructureEditorUtils::ChangeVariableType(Struct, VarDesc.VarGuid, NewPinType))
					{
						Changes.Add(FString::Printf(TEXT("type -> %s"), *Op.Type));
					}
				}

				// Change default value if provided
				if (!Op.DefaultValue.IsEmpty())
				{
					FStructureEditorUtils::ChangeVariableDefaultValue(Struct, VarDesc.VarGuid, Op.DefaultValue);
					Changes.Add(FString::Printf(TEXT("default -> %s"), *Op.DefaultValue));
				}

				// Change description if provided
				if (!Op.Description.IsEmpty())
				{
					VarDesc.ToolTip = Op.Description;
					Changes.Add(TEXT("description updated"));
				}

				if (Changes.Num() > 0)
				{
					OutResults.Add(FString::Printf(TEXT("Modified '%s': %s"), *Op.Name, *FString::Join(Changes, TEXT(", "))));
					Modified++;
				}
				break;
			}
		}

		if (!bFound)
		{
			OutResults.Add(FString::Printf(TEXT("Field '%s' not found for modification"), *Op.Name));
		}
	}

	return Modified;
}

// ============================================================================
// ENUM OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditEnum(UUserDefinedEnum* Enum, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process add_values
	const TArray<TSharedPtr<FJsonValue>>* AddValuesArray;
	if (Args->TryGetArrayField(TEXT("add_values"), AddValuesArray))
	{
		TotalChanges += AddEnumValues(Enum, AddValuesArray, Results);
	}

	// Process remove_values
	const TArray<TSharedPtr<FJsonValue>>* RemoveValuesArray;
	if (Args->TryGetArrayField(TEXT("remove_values"), RemoveValuesArray))
	{
		TotalChanges += RemoveEnumValues(Enum, RemoveValuesArray, Results);
	}

	// Process modify_values
	const TArray<TSharedPtr<FJsonValue>>* ModifyValuesArray;
	if (Args->TryGetArrayField(TEXT("modify_values"), ModifyValuesArray))
	{
		TotalChanges += ModifyEnumValues(Enum, ModifyValuesArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_values, remove_values, or modify_values."));
	}

	// Mark dirty
	Enum->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified enum %s (%d changes)\n"), *Enum->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults)
{
	if (!ValuesArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		const TSharedPtr<FJsonObject>* ValueObj;
		if (!ValueEntry->TryGetObject(ValueObj))
		{
			continue;
		}

		FEnumValueOp Op = ParseEnumValueOp(*ValueObj);
		if (Op.Name.IsEmpty() && Op.DisplayName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped value with no name"));
			continue;
		}

		// Add a new enumerator (returns void in UE 5.7+)
		int32 NumBefore = Enum->NumEnums();
		FEnumEditorUtils::AddNewEnumeratorForUserDefinedEnum(Enum);

		if (Enum->NumEnums() > NumBefore)
		{
			int32 NewIndex = Enum->NumEnums() - 2;  // -1 for MAX, -1 for zero-based
			if (NewIndex >= 0)
			{
				// Set display name
				FString DisplayName = Op.DisplayName.IsEmpty() ? Op.Name : Op.DisplayName;
				FEnumEditorUtils::SetEnumeratorDisplayName(Enum, NewIndex, FText::FromString(DisplayName));

				OutResults.Add(FString::Printf(TEXT("Added value '%s' at index %d"), *DisplayName, NewIndex));
				Added++;
			}
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add enum value '%s'"), *Op.Name));
		}
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults)
{
	if (!ValuesArray) return 0;

	// Collect indices to remove (work backwards to avoid index shifting)
	TArray<int32> IndicesToRemove;

	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		FString ValueName;
		int32 ValueIndex = -1;

		if (ValueEntry->TryGetString(ValueName))
		{
			// Find by name
			for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
			{
				FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
				if (DisplayName.Equals(ValueName, ESearchCase::IgnoreCase))
				{
					ValueIndex = i;
					break;
				}
			}
		}
		else if (ValueEntry->TryGetNumber(ValueIndex))
		{
			// Use index directly
		}

		if (ValueIndex >= 0 && ValueIndex < Enum->NumEnums() - 1)
		{
			IndicesToRemove.AddUnique(ValueIndex);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Enum value '%s' not found"), *ValueName));
		}
	}

	// Sort descending to remove from end first
	IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });

	int32 Removed = 0;
	for (int32 Index : IndicesToRemove)
	{
		FString DisplayName = Enum->GetDisplayNameTextByIndex(Index).ToString();
		int32 NumBefore = Enum->NumEnums();
		FEnumEditorUtils::RemoveEnumeratorFromUserDefinedEnum(Enum, Index);
		if (Enum->NumEnums() < NumBefore)
		{
			OutResults.Add(FString::Printf(TEXT("Removed value '%s' (was index %d)"), *DisplayName, Index));
			Removed++;
		}
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults)
{
	if (!ValuesArray) return 0;

	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& ValueEntry : *ValuesArray)
	{
		const TSharedPtr<FJsonObject>* ValueObj;
		if (!ValueEntry->TryGetObject(ValueObj))
		{
			continue;
		}

		FEnumValueOp Op = ParseEnumValueOp(*ValueObj);

		// Find the value by index or name
		int32 TargetIndex = Op.Index;
		if (TargetIndex < 0 && !Op.Name.IsEmpty())
		{
			// Find by name
			for (int32 i = 0; i < Enum->NumEnums() - 1; i++)
			{
				FString DisplayName = Enum->GetDisplayNameTextByIndex(i).ToString();
				if (DisplayName.Equals(Op.Name, ESearchCase::IgnoreCase))
				{
					TargetIndex = i;
					break;
				}
			}
		}

		if (TargetIndex < 0 || TargetIndex >= Enum->NumEnums() - 1)
		{
			OutResults.Add(FString::Printf(TEXT("Enum value not found for modification")));
			continue;
		}

		// Set new display name
		if (!Op.DisplayName.IsEmpty())
		{
			FEnumEditorUtils::SetEnumeratorDisplayName(Enum, TargetIndex, FText::FromString(Op.DisplayName));
			OutResults.Add(FString::Printf(TEXT("Modified value at index %d -> '%s'"), TargetIndex, *Op.DisplayName));
			Modified++;
		}
	}

	return Modified;
}

// ============================================================================
// DATATABLE OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditDataTable(UDataTable* DataTable, const TSharedPtr<FJsonObject>& Args)
{
	if (!DataTable->RowStruct)
	{
		return FToolResult::Fail(TEXT("DataTable has no row struct defined"));
	}

	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process import_json (bulk replace - must be processed first as it clears the table)
	FString ImportJsonString;
	if (Args->TryGetStringField(TEXT("import_json"), ImportJsonString) && !ImportJsonString.IsEmpty())
	{
		FToolResult ImportResult = ImportDataTableJSON(DataTable, ImportJsonString, Results);
		if (!ImportResult.bSuccess)
		{
			return ImportResult;
		}
		// import_json replaces all rows, so we return immediately after
		DataTable->GetPackage()->MarkPackageDirty();
		return ImportResult;
	}

	// Process append_json (bulk add without clearing)
	FString AppendJsonString;
	if (Args->TryGetStringField(TEXT("append_json"), AppendJsonString) && !AppendJsonString.IsEmpty())
	{
		TotalChanges += AppendDataTableJSON(DataTable, AppendJsonString, Results);
	}

	// Process add_rows
	const TArray<TSharedPtr<FJsonValue>>* AddRowsArray;
	if (Args->TryGetArrayField(TEXT("add_rows"), AddRowsArray))
	{
		TotalChanges += AddDataTableRows(DataTable, AddRowsArray, Results);
	}

	// Process remove_rows
	const TArray<TSharedPtr<FJsonValue>>* RemoveRowsArray;
	if (Args->TryGetArrayField(TEXT("remove_rows"), RemoveRowsArray))
	{
		TotalChanges += RemoveDataTableRows(DataTable, RemoveRowsArray, Results);
	}

	// Process modify_rows
	const TArray<TSharedPtr<FJsonValue>>* ModifyRowsArray;
	if (Args->TryGetArrayField(TEXT("modify_rows"), ModifyRowsArray))
	{
		TotalChanges += ModifyDataTableRows(DataTable, ModifyRowsArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_rows, remove_rows, modify_rows, import_json, or append_json."));
	}

	// Mark dirty
	DataTable->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified DataTable %s (%d changes)\n"), *DataTable->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults)
{
	if (!RowsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& RowEntry : *RowsArray)
	{
		const TSharedPtr<FJsonObject>* RowObj;
		if (!RowEntry->TryGetObject(RowObj))
		{
			continue;
		}

		FRowOp Op = ParseRowOp(*RowObj);
		if (Op.RowName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped row with no name"));
			continue;
		}

		// Check if row already exists
		if (DataTable->FindRowUnchecked(FName(*Op.RowName)))
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' already exists"), *Op.RowName));
			continue;
		}

		// Allocate and initialize a new row with default values
		uint8* NewRowData = (uint8*)FMemory::Malloc(DataTable->RowStruct->GetStructureSize());
		DataTable->RowStruct->InitializeStruct(NewRowData);
		DataTable->AddRow(FName(*Op.RowName), *(FTableRowBase*)NewRowData);
		FMemory::Free(NewRowData);

		// Get the new row and set values
		uint8* RowData = DataTable->FindRowUnchecked(FName(*Op.RowName));
		if (RowData && Op.Values.IsValid())
		{
			TArray<FString> SetValues;
			for (const auto& ValuePair : Op.Values->Values)
			{
				FString ColumnName = ValuePair.Key;
				FString ValueStr;
				ValuePair.Value->TryGetString(ValueStr);

				// Find the property
				FProperty* Property = DataTable->RowStruct->FindPropertyByName(FName(*ColumnName));
				if (Property)
				{
					void* PropertyValue = Property->ContainerPtrToValuePtr<void>(RowData);
					Property->ImportText_Direct(*ValueStr, PropertyValue, nullptr, PPF_None);
					SetValues.Add(FString::Printf(TEXT("%s=%s"), *ColumnName, *ValueStr));
				}
			}

			OutResults.Add(FString::Printf(TEXT("Added row '%s' (%s)"), *Op.RowName, *FString::Join(SetValues, TEXT(", "))));
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Added row '%s'"), *Op.RowName));
		}
		Added++;
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults)
{
	if (!RowsArray) return 0;

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& RowEntry : *RowsArray)
	{
		FString RowName;
		if (!RowEntry->TryGetString(RowName))
		{
			continue;
		}

		if (DataTable->FindRowUnchecked(FName(*RowName)))
		{
			DataTable->RemoveRow(FName(*RowName));
			OutResults.Add(FString::Printf(TEXT("Removed row '%s'"), *RowName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' not found"), *RowName));
		}
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults)
{
	if (!RowsArray) return 0;

	int32 Modified = 0;
	for (const TSharedPtr<FJsonValue>& RowEntry : *RowsArray)
	{
		const TSharedPtr<FJsonObject>* RowObj;
		if (!RowEntry->TryGetObject(RowObj))
		{
			continue;
		}

		FRowOp Op = ParseRowOp(*RowObj);
		if (Op.RowName.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped modification with no row name"));
			continue;
		}

		uint8* RowData = DataTable->FindRowUnchecked(FName(*Op.RowName));
		if (!RowData)
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' not found"), *Op.RowName));
			continue;
		}

		if (!Op.Values.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' has no values to modify"), *Op.RowName));
			continue;
		}

		TArray<FString> ModifiedValues;
		for (const auto& ValuePair : Op.Values->Values)
		{
			FString ColumnName = ValuePair.Key;
			FString ValueStr;
			ValuePair.Value->TryGetString(ValueStr);

			// Find the property
			FProperty* Property = DataTable->RowStruct->FindPropertyByName(FName(*ColumnName));
			if (Property)
			{
				void* PropertyValue = Property->ContainerPtrToValuePtr<void>(RowData);
				Property->ImportText_Direct(*ValueStr, PropertyValue, nullptr, PPF_None);
				ModifiedValues.Add(FString::Printf(TEXT("%s=%s"), *ColumnName, *ValueStr));
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("Column '%s' not found in row struct"), *ColumnName));
			}
		}

		if (ModifiedValues.Num() > 0)
		{
			OutResults.Add(FString::Printf(TEXT("Modified row '%s': %s"), *Op.RowName, *FString::Join(ModifiedValues, TEXT(", "))));
			Modified++;
		}
	}

	return Modified;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

FEditDataStructureTool::FStructFieldOp FEditDataStructureTool::ParseStructFieldOp(const TSharedPtr<FJsonObject>& FieldObj)
{
	FStructFieldOp Op;
	FieldObj->TryGetStringField(TEXT("name"), Op.Name);
	FieldObj->TryGetStringField(TEXT("new_name"), Op.NewName);
	FieldObj->TryGetStringField(TEXT("type"), Op.Type);
	FieldObj->TryGetStringField(TEXT("default_value"), Op.DefaultValue);
	FieldObj->TryGetStringField(TEXT("description"), Op.Description);
	return Op;
}

FEditDataStructureTool::FEnumValueOp FEditDataStructureTool::ParseEnumValueOp(const TSharedPtr<FJsonObject>& ValueObj)
{
	FEnumValueOp Op;
	Op.Index = -1;
	ValueObj->TryGetStringField(TEXT("name"), Op.Name);
	ValueObj->TryGetStringField(TEXT("display_name"), Op.DisplayName);
	ValueObj->TryGetNumberField(TEXT("index"), Op.Index);
	return Op;
}

FEditDataStructureTool::FRowOp FEditDataStructureTool::ParseRowOp(const TSharedPtr<FJsonObject>& RowObj)
{
	FRowOp Op;
	RowObj->TryGetStringField(TEXT("row_name"), Op.RowName);

	const TSharedPtr<FJsonObject>* ValuesObj;
	if (RowObj->TryGetObjectField(TEXT("values"), ValuesObj))
	{
		Op.Values = *ValuesObj;
	}

	return Op;
}

FEdGraphPinType FEditDataStructureTool::TypeNameToPinType(const FString& TypeName)
{
	FEdGraphPinType PinType;

	if (TypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (TypeName.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (TypeName.Equals(TEXT("Int64"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
	}
	else if (TypeName.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
		PinType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FString"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FName"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (TypeName.Equals(TEXT("Text"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FText"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FVector"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FRotator"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (TypeName.Equals(TEXT("Transform"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FTransform"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (TypeName.Equals(TEXT("LinearColor"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FLinearColor"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else if (TypeName.Equals(TEXT("Color"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("FColor"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = TBaseStructure<FColor>::Get();
	}
	else if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("UObject"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("UClass"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Class;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("SoftObject"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("TSoftObjectPtr"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftObject;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("SoftClass"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("TSoftClassPtr"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_SoftClass;
		PinType.PinSubCategoryObject = UObject::StaticClass();
	}
	else if (TypeName.Equals(TEXT("Byte"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("uint8"), ESearchCase::IgnoreCase))
	{
		PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else
	{
		// Default to string for unknown types
		PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	}

	return PinType;
}

int32 FEditDataStructureTool::FindStructFieldIndex(UUserDefinedStruct* Struct, const FString& FieldName)
{
	TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);

	for (int32 i = 0; i < VarDescArray.Num(); i++)
	{
		if (VarDescArray[i].VarName.ToString().Equals(FieldName, ESearchCase::IgnoreCase))
		{
			return i;
		}
	}

	return -1;
}

// ============================================================================
// JSON BULK IMPORT OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::ImportDataTableJSON(UDataTable* DataTable, const FString& JSONString, TArray<FString>& OutResults)
{
	if (JSONString.IsEmpty())
	{
		return FToolResult::Fail(TEXT("import_json string is empty"));
	}

	// Use the engine's built-in JSON import (this REPLACES all existing rows)
	TArray<FString> ImportProblems = DataTable->CreateTableFromJSONString(JSONString);

	// Check for problems
	if (ImportProblems.Num() > 0)
	{
		for (const FString& Problem : ImportProblems)
		{
			OutResults.Add(FString::Printf(TEXT("Warning: %s"), *Problem));
		}
	}

	// Count the rows that were imported
	int32 RowCount = DataTable->GetRowNames().Num();

	// Build result message
	FString Output = FString::Printf(TEXT("Bulk imported %d rows into DataTable %s (replaced all existing data)"), RowCount, *DataTable->GetName());
	if (OutResults.Num() > 0)
	{
		Output += TEXT("\n");
		for (const FString& Result : OutResults)
		{
			Output += FString::Printf(TEXT("  %s\n"), *Result);
		}
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AppendDataTableJSON(UDataTable* DataTable, const FString& JSONString, TArray<FString>& OutResults)
{
	if (JSONString.IsEmpty())
	{
		OutResults.Add(TEXT("append_json string is empty"));
		return 0;
	}

	// Parse the JSON array
	TArray<TSharedPtr<FJsonValue>> ParsedRows;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(JSONString);
	if (!FJsonSerializer::Deserialize(JsonReader, ParsedRows))
	{
		OutResults.Add(FString::Printf(TEXT("Failed to parse JSON: %s"), *JsonReader->GetErrorMessage()));
		return 0;
	}

	if (ParsedRows.Num() == 0)
	{
		OutResults.Add(TEXT("JSON array is empty"));
		return 0;
	}

	// Get the key field name (defaults to "Name")
	FString KeyField = DataTable->ImportKeyField.IsEmpty() ? TEXT("Name") : DataTable->ImportKeyField;

	int32 Added = 0;
	int32 Skipped = 0;

	// Process each row object
	for (int32 RowIdx = 0; RowIdx < ParsedRows.Num(); ++RowIdx)
	{
		TSharedPtr<FJsonObject> RowObject = ParsedRows[RowIdx]->AsObject();
		if (!RowObject.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Row %d is not a valid JSON object"), RowIdx));
			Skipped++;
			continue;
		}

		// Get row name
		FString RowNameStr;
		if (!RowObject->TryGetStringField(KeyField, RowNameStr) || RowNameStr.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("Row %d missing '%s' field"), RowIdx, *KeyField));
			Skipped++;
			continue;
		}

		FName RowName(*RowNameStr);

		// Check if row already exists
		if (DataTable->FindRowUnchecked(RowName))
		{
			OutResults.Add(FString::Printf(TEXT("Row '%s' already exists, skipped"), *RowNameStr));
			Skipped++;
			continue;
		}

		// Allocate new row with default values
		uint8* NewRowData = (uint8*)FMemory::Malloc(DataTable->RowStruct->GetStructureSize());
		DataTable->RowStruct->InitializeStruct(NewRowData);

		// Set property values from JSON
		for (const auto& JsonField : RowObject->Values)
		{
			// Skip the key field
			if (JsonField.Key.Equals(KeyField, ESearchCase::IgnoreCase))
			{
				continue;
			}

			// Find the property
			FProperty* Property = DataTable->RowStruct->FindPropertyByName(FName(*JsonField.Key));
			if (!Property)
			{
				continue;  // Silently skip unknown properties (like the engine does)
			}

			// Get value as string and import it
			FString ValueStr;
			if (JsonField.Value->TryGetString(ValueStr))
			{
				void* PropertyValue = Property->ContainerPtrToValuePtr<void>(NewRowData);
				Property->ImportText_Direct(*ValueStr, PropertyValue, nullptr, PPF_None);
			}
			else if (JsonField.Value->Type == EJson::Number)
			{
				// Handle numeric values
				double NumValue = JsonField.Value->AsNumber();
				void* PropertyValue = Property->ContainerPtrToValuePtr<void>(NewRowData);
				Property->ImportText_Direct(*FString::Printf(TEXT("%g"), NumValue), PropertyValue, nullptr, PPF_None);
			}
			else if (JsonField.Value->Type == EJson::Boolean)
			{
				// Handle boolean values
				bool BoolValue = JsonField.Value->AsBool();
				void* PropertyValue = Property->ContainerPtrToValuePtr<void>(NewRowData);
				Property->ImportText_Direct(BoolValue ? TEXT("true") : TEXT("false"), PropertyValue, nullptr, PPF_None);
			}
		}

		// Add the row to the table
		DataTable->AddRow(RowName, *(FTableRowBase*)NewRowData);
		FMemory::Free(NewRowData);
		Added++;
	}

	OutResults.Add(FString::Printf(TEXT("Appended %d rows from JSON (%d skipped)"), Added, Skipped));
	return Added;
}

// ============================================================================
// CHOOSERTABLE OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditChooserTable(UChooserTable* ChooserTable, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process replace_references
	const TArray<TSharedPtr<FJsonValue>>* ReplaceRefsArray;
	if (Args->TryGetArrayField(TEXT("replace_references"), ReplaceRefsArray))
	{
		TotalChanges += ReplaceChooserReferences(ChooserTable, ReplaceRefsArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use replace_references to modify ChooserTable references."));
	}

	// Recompile the ChooserTable after modifications
	ChooserTable->Compile(true);

	// Mark dirty
	ChooserTable->GetPackage()->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("Modified ChooserTable %s (%d changes)\n"), *ChooserTable->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::ReplaceChooserReferences(UChooserTable* ChooserTable, const TArray<TSharedPtr<FJsonValue>>* ReplacementsArray, TArray<FString>& OutResults)
{
	if (!ReplacementsArray || ReplacementsArray->Num() == 0)
	{
		return 0;
	}

	int32 TotalReplaced = 0;

	// Process each replacement rule
	for (const TSharedPtr<FJsonValue>& ReplacementValue : *ReplacementsArray)
	{
		const TSharedPtr<FJsonObject>* ReplacementObj;
		if (!ReplacementValue->TryGetObject(ReplacementObj))
		{
			continue;
		}

		// Parse replacement parameters
		FString SearchPattern, ReplaceWith, Prefix, Suffix;
		int32 Limit = INT32_MAX;

		(*ReplacementObj)->TryGetStringField(TEXT("search"), SearchPattern);
		(*ReplacementObj)->TryGetStringField(TEXT("replace"), ReplaceWith);
		(*ReplacementObj)->TryGetStringField(TEXT("prefix"), Prefix);
		(*ReplacementObj)->TryGetStringField(TEXT("suffix"), Suffix);
		(*ReplacementObj)->TryGetNumberField(TEXT("limit"), Limit);

		if (Limit <= 0)
		{
			Limit = INT32_MAX;
		}

		int32 ReplacedThisRule = 0;

		// Mark for modification
		ChooserTable->Modify();

		// Iterate all columns in the ChooserTable
		for (FInstancedStruct& ColumnStruct : ChooserTable->ColumnsStructs)
		{
			if (!ColumnStruct.IsValid())
			{
				continue;
			}

			// Try to get as output object column (where animation references live)
			if (FOutputObjectColumn* OutputCol = ColumnStruct.GetMutablePtr<FOutputObjectColumn>())
			{
				// Process each row value
				for (FChooserOutputObjectRowData& RowData : OutputCol->RowValues)
				{
					if (ReplacedThisRule >= Limit)
					{
						break;
					}

					if (!RowData.Value.IsValid())
					{
						continue;
					}

					// Try FAssetChooser (hard reference)
					if (FAssetChooser* AssetChooser = RowData.Value.GetMutablePtr<FAssetChooser>())
					{
						UObject* CurrentAsset = AssetChooser->Asset.Get();
						if (!CurrentAsset)
						{
							continue;
						}

						FString CurrentName = CurrentAsset->GetName();
						FString CurrentPath = CurrentAsset->GetPathName();
						FString PackagePath = FPackageName::GetLongPackagePath(CurrentPath);

						// Check if this asset matches our criteria
						bool bShouldReplace = SearchPattern.IsEmpty() || CurrentName.Contains(SearchPattern);

						if (bShouldReplace)
						{
							// Build new asset name
							FString NewName = CurrentName;

							// Apply search/replace if specified
							if (!SearchPattern.IsEmpty() && !ReplaceWith.IsEmpty())
							{
								NewName = NewName.Replace(*SearchPattern, *ReplaceWith);
							}

							// Apply prefix
							if (!Prefix.IsEmpty())
							{
								NewName = Prefix + NewName;
							}

							// Apply suffix
							if (!Suffix.IsEmpty())
							{
								NewName = NewName + Suffix;
							}

							// Try to find the new asset
							FString NewAssetPath = PackagePath / NewName + TEXT(".") + NewName;
							UObject* NewAsset = LoadObject<UObject>(nullptr, *NewAssetPath);

							if (NewAsset)
							{
								FString OldAssetName = CurrentAsset->GetName();
								AssetChooser->Asset = NewAsset;
								OutResults.Add(FString::Printf(TEXT("Replaced '%s' -> '%s'"), *OldAssetName, *NewAsset->GetName()));
								ReplacedThisRule++;
								TotalReplaced++;
							}
							else
							{
								OutResults.Add(FString::Printf(TEXT("Warning: New asset not found: %s"), *NewAssetPath));
							}
						}
					}
					// Try FSoftAssetChooser (soft reference)
					else if (FSoftAssetChooser* SoftChooser = RowData.Value.GetMutablePtr<FSoftAssetChooser>())
					{
						UObject* CurrentAsset = SoftChooser->Asset.Get();
						if (!CurrentAsset)
						{
							// For soft refs, try to get from the path
							FSoftObjectPath SoftPath = SoftChooser->Asset.ToSoftObjectPath();
							if (!SoftPath.IsValid())
							{
								continue;
							}

							FString CurrentPath = SoftPath.ToString();
							FString CurrentName = FPackageName::GetShortName(CurrentPath);
							FString PackagePath = FPackageName::GetLongPackagePath(CurrentPath);

							bool bShouldReplace = SearchPattern.IsEmpty() || CurrentName.Contains(SearchPattern);

							if (bShouldReplace)
							{
								FString NewName = CurrentName;

								if (!SearchPattern.IsEmpty() && !ReplaceWith.IsEmpty())
								{
									NewName = NewName.Replace(*SearchPattern, *ReplaceWith);
								}
								if (!Prefix.IsEmpty())
								{
									NewName = Prefix + NewName;
								}
								if (!Suffix.IsEmpty())
								{
									NewName = NewName + Suffix;
								}

								FString NewAssetPath = PackagePath / NewName + TEXT(".") + NewName;
								SoftChooser->Asset = TSoftObjectPtr<UObject>(FSoftObjectPath(NewAssetPath));
								OutResults.Add(FString::Printf(TEXT("Replaced (soft) '%s' -> '%s'"), *CurrentName, *NewName));
								ReplacedThisRule++;
								TotalReplaced++;
							}
							continue;
						}

						FString CurrentName = CurrentAsset->GetName();
						FString CurrentPath = CurrentAsset->GetPathName();
						FString PackagePath = FPackageName::GetLongPackagePath(CurrentPath);

						bool bShouldReplace = SearchPattern.IsEmpty() || CurrentName.Contains(SearchPattern);

						if (bShouldReplace)
						{
							FString NewName = CurrentName;

							if (!SearchPattern.IsEmpty() && !ReplaceWith.IsEmpty())
							{
								NewName = NewName.Replace(*SearchPattern, *ReplaceWith);
							}
							if (!Prefix.IsEmpty())
							{
								NewName = Prefix + NewName;
							}
							if (!Suffix.IsEmpty())
							{
								NewName = NewName + Suffix;
							}

							FString NewAssetPath = PackagePath / NewName + TEXT(".") + NewName;
							UObject* NewAsset = LoadObject<UObject>(nullptr, *NewAssetPath);

							if (NewAsset)
							{
								FString OldAssetName = CurrentAsset->GetName();
								SoftChooser->Asset = NewAsset;
								OutResults.Add(FString::Printf(TEXT("Replaced (soft) '%s' -> '%s'"), *OldAssetName, *NewAsset->GetName()));
								ReplacedThisRule++;
								TotalReplaced++;
							}
							else
							{
								OutResults.Add(FString::Printf(TEXT("Warning: New asset not found: %s"), *NewAssetPath));
							}
						}
					}
				}

				// Also check fallback value
				if (ReplacedThisRule < Limit && OutputCol->FallbackValue.Value.IsValid())
				{
					if (FAssetChooser* AssetChooser = OutputCol->FallbackValue.Value.GetMutablePtr<FAssetChooser>())
					{
						UObject* CurrentAsset = AssetChooser->Asset.Get();
						if (CurrentAsset)
						{
							FString CurrentName = CurrentAsset->GetName();
							FString CurrentPath = CurrentAsset->GetPathName();
							FString PackagePath = FPackageName::GetLongPackagePath(CurrentPath);

							bool bShouldReplace = SearchPattern.IsEmpty() || CurrentName.Contains(SearchPattern);

							if (bShouldReplace)
							{
								FString NewName = CurrentName;

								if (!SearchPattern.IsEmpty() && !ReplaceWith.IsEmpty())
								{
									NewName = NewName.Replace(*SearchPattern, *ReplaceWith);
								}
								if (!Prefix.IsEmpty())
								{
									NewName = Prefix + NewName;
								}
								if (!Suffix.IsEmpty())
								{
									NewName = NewName + Suffix;
								}

								FString NewAssetPath = PackagePath / NewName + TEXT(".") + NewName;
								UObject* NewAsset = LoadObject<UObject>(nullptr, *NewAssetPath);

								if (NewAsset)
								{
									FString OldAssetName = CurrentAsset->GetName();
									AssetChooser->Asset = NewAsset;
									OutResults.Add(FString::Printf(TEXT("Replaced fallback '%s' -> '%s'"), *OldAssetName, *NewAsset->GetName()));
									ReplacedThisRule++;
									TotalReplaced++;
								}
							}
						}
					}
				}
			}

			if (ReplacedThisRule >= Limit)
			{
				OutResults.Add(FString::Printf(TEXT("Reached limit of %d replacements for this rule"), Limit));
				break;
			}
		}
	}

	return TotalReplaced;
}

// ============================================================================
// STRING TABLE OPERATIONS
// ============================================================================

FToolResult FEditDataStructureTool::EditStringTable(UStringTable* StringTable, const TSharedPtr<FJsonObject>& Args)
{
	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process set_namespace first (affects all entries)
	FString NewNamespace;
	if (Args->TryGetStringField(TEXT("set_namespace"), NewNamespace))
	{
		StringTable->Modify();
		FStringTableRef MutableTable = StringTable->GetMutableStringTable();
		FString OldNamespace = MutableTable->GetNamespace();
		MutableTable->SetNamespace(FTextKey(NewNamespace));
		Results.Add(FString::Printf(TEXT("Set namespace: '%s' -> '%s'"), *OldNamespace, *NewNamespace));
		TotalChanges++;
	}

	// Process add_entries
	const TArray<TSharedPtr<FJsonValue>>* AddEntriesArray;
	if (Args->TryGetArrayField(TEXT("add_entries"), AddEntriesArray))
	{
		TotalChanges += AddStringTableEntries(StringTable, AddEntriesArray, Results);
	}

	// Process remove_entries
	const TArray<TSharedPtr<FJsonValue>>* RemoveEntriesArray;
	if (Args->TryGetArrayField(TEXT("remove_entries"), RemoveEntriesArray))
	{
		TotalChanges += RemoveStringTableEntries(StringTable, RemoveEntriesArray, Results);
	}

	// Process modify_entries
	const TArray<TSharedPtr<FJsonValue>>* ModifyEntriesArray;
	if (Args->TryGetArrayField(TEXT("modify_entries"), ModifyEntriesArray))
	{
		TotalChanges += ModifyStringTableEntries(StringTable, ModifyEntriesArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No valid StringTable operations specified. Use add_entries, remove_entries, modify_entries, or set_namespace."));
	}

	// Mark package dirty for save
	StringTable->GetPackage()->MarkPackageDirty();

	FString Output = FString::Printf(TEXT("Modified StringTable %s (%d changes)\n"), *StringTable->GetName(), TotalChanges);
	for (const FString& Result : Results)
	{
		Output += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Output);
}

int32 FEditDataStructureTool::AddStringTableEntries(UStringTable* StringTable, const TArray<TSharedPtr<FJsonValue>>* EntriesArray, TArray<FString>& OutResults)
{
	if (!EntriesArray) return 0;

	StringTable->Modify();
	FStringTableRef MutableTable = StringTable->GetMutableStringTable();
	int32 Added = 0;

	for (const TSharedPtr<FJsonValue>& Item : *EntriesArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Item->TryGetObject(EntryObj) || !EntryObj->IsValid())
		{
			OutResults.Add(TEXT("Skipped invalid entry (not an object)"));
			continue;
		}

		FString Key, Value;
		if (!(*EntryObj)->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped entry with missing or empty 'key'"));
			continue;
		}

		if (!(*EntryObj)->TryGetStringField(TEXT("value"), Value))
		{
			OutResults.Add(FString::Printf(TEXT("Skipped entry '%s': missing 'value'"), *Key));
			continue;
		}

		// Check if key already exists
		FString ExistingValue;
		if (MutableTable->GetSourceString(FTextKey(Key), ExistingValue))
		{
			OutResults.Add(FString::Printf(TEXT("Skipped '%s': key already exists (use modify_entries to update)"), *Key));
			continue;
		}

		MutableTable->SetSourceString(FTextKey(Key), Value);
		OutResults.Add(FString::Printf(TEXT("Added '%s' = '%s'"),
			*Key, *(Value.Len() > 60 ? Value.Left(57) + TEXT("...") : Value)));
		Added++;
	}

	return Added;
}

int32 FEditDataStructureTool::RemoveStringTableEntries(UStringTable* StringTable, const TArray<TSharedPtr<FJsonValue>>* KeysArray, TArray<FString>& OutResults)
{
	if (!KeysArray) return 0;

	StringTable->Modify();
	FStringTableRef MutableTable = StringTable->GetMutableStringTable();
	int32 Removed = 0;

	for (const TSharedPtr<FJsonValue>& Item : *KeysArray)
	{
		FString Key;
		if (!Item->TryGetString(Key) || Key.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped invalid key (not a string or empty)"));
			continue;
		}

		// Check if key exists before removing
		FString ExistingValue;
		if (!MutableTable->GetSourceString(FTextKey(Key), ExistingValue))
		{
			OutResults.Add(FString::Printf(TEXT("Skipped '%s': key not found"), *Key));
			continue;
		}

		MutableTable->RemoveSourceString(FTextKey(Key));
		OutResults.Add(FString::Printf(TEXT("Removed '%s'"), *Key));
		Removed++;
	}

	return Removed;
}

int32 FEditDataStructureTool::ModifyStringTableEntries(UStringTable* StringTable, const TArray<TSharedPtr<FJsonValue>>* EntriesArray, TArray<FString>& OutResults)
{
	if (!EntriesArray) return 0;

	StringTable->Modify();
	FStringTableRef MutableTable = StringTable->GetMutableStringTable();
	int32 Modified = 0;

	for (const TSharedPtr<FJsonValue>& Item : *EntriesArray)
	{
		const TSharedPtr<FJsonObject>* EntryObj;
		if (!Item->TryGetObject(EntryObj) || !EntryObj->IsValid())
		{
			OutResults.Add(TEXT("Skipped invalid entry (not an object)"));
			continue;
		}

		FString Key, Value;
		if (!(*EntryObj)->TryGetStringField(TEXT("key"), Key) || Key.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped entry with missing or empty 'key'"));
			continue;
		}

		if (!(*EntryObj)->TryGetStringField(TEXT("value"), Value))
		{
			OutResults.Add(FString::Printf(TEXT("Skipped entry '%s': missing 'value'"), *Key));
			continue;
		}

		// Check if key exists
		FString ExistingValue;
		if (!MutableTable->GetSourceString(FTextKey(Key), ExistingValue))
		{
			OutResults.Add(FString::Printf(TEXT("Skipped '%s': key not found (use add_entries to create)"), *Key));
			continue;
		}

		MutableTable->SetSourceString(FTextKey(Key), Value);
		OutResults.Add(FString::Printf(TEXT("Modified '%s': '%s' -> '%s'"),
			*Key,
			*(ExistingValue.Len() > 30 ? ExistingValue.Left(27) + TEXT("...") : ExistingValue),
			*(Value.Len() > 30 ? Value.Left(27) + TEXT("...") : Value)));
		Modified++;
	}

	return Modified;
}
