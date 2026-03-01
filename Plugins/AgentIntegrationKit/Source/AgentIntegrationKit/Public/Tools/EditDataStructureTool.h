// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UUserDefinedStruct;
class UUserDefinedEnum;
class UDataTable;
class UChooserTable;
class UStringTable;

/**
 * Tool for editing User Defined Structs, Enums, DataTables, ChooserTables, and StringTables
 *
 * Parameters:
 *   - name: Asset name (required)
 *   - path: Asset path (optional, defaults to /Game)
 *
 * Struct operations (target="Struct"):
 *   - add_fields: Array of field definitions to add [{name, type, default_value, description}]
 *   - remove_fields: Array of field names to remove
 *   - modify_fields: Array of field modifications [{name, new_name, type, default_value, description}]
 *
 * Enum operations (target="Enum"):
 *   - add_values: Array of value definitions to add [{name, display_name}]
 *   - remove_values: Array of value names to remove
 *   - modify_values: Array of value modifications [{index, display_name}]
 *
 * DataTable operations (target="DataTable"):
 *   - add_rows: Array of row definitions [{row_name, values: {column: value, ...}}]
 *   - remove_rows: Array of row names to remove
 *   - modify_rows: Array of row modifications [{row_name, values: {column: value, ...}}]
 *   - import_json: JSON string for bulk import (REPLACES all rows) - format: [{"Name":"Row1","Col":"val"}, ...]
 *   - append_json: JSON array for bulk append (ADDS to existing rows) - same format as import_json
 *
 * ChooserTable operations (target="ChooserTable"):
 *   - replace_references: Replace object references by pattern [{search, replace, prefix, suffix, limit}]
 *       search: Substring to find in asset names (e.g., "M_Neutral")
 *       replace: (optional) Replacement string
 *       prefix: (optional) Add prefix to asset name (e.g., "RTG_Rap_")
 *       suffix: (optional) Add suffix to asset name
 *       limit: (optional) Max replacements (default: all)
 *   - list_references: If true, lists all object references without modifying
 *
 * StringTable operations (target="StringTable"):
 *   - add_entries: Array of entries to add [{key, value}]
 *   - remove_entries: Array of keys to remove
 *   - modify_entries: Array of entries to modify [{key, value}] (key must exist)
 *   - set_namespace: Set the string table namespace
 *
 * Supported field types for structs:
 *   Boolean, Integer, Int64, Float, Double, String, Name, Text,
 *   Vector, Rotator, Transform, LinearColor, Color, Object, Class,
 *   SoftObject, SoftClass, Byte
 */
class AGENTINTEGRATIONKIT_API FEditDataStructureTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_data_structure"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit User Defined Structs, Enums, DataTables, and StringTables. "
			"StringTable operations: add_entries [{key, value}], remove_entries [keys], modify_entries [{key, value}], set_namespace.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Struct field definition for adding/modifying */
	struct FStructFieldOp
	{
		FString Name;
		FString NewName;       // For renaming
		FString Type;
		FString DefaultValue;
		FString Description;
	};

	/** Enum value definition for adding/modifying */
	struct FEnumValueOp
	{
		FString Name;
		FString DisplayName;
		int32 Index;           // For modify by index
	};

	/** DataTable row operation */
	struct FRowOp
	{
		FString RowName;
		TSharedPtr<FJsonObject> Values;  // Column -> Value mapping
	};

	// Struct operations
	FToolResult EditStruct(UUserDefinedStruct* Struct, const TSharedPtr<FJsonObject>& Args);
	int32 AddStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults);
	int32 RemoveStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults);
	int32 ModifyStructFields(UUserDefinedStruct* Struct, const TArray<TSharedPtr<FJsonValue>>* FieldsArray, TArray<FString>& OutResults);

	// Enum operations
	FToolResult EditEnum(UUserDefinedEnum* Enum, const TSharedPtr<FJsonObject>& Args);
	int32 AddEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults);
	int32 RemoveEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults);
	int32 ModifyEnumValues(UUserDefinedEnum* Enum, const TArray<TSharedPtr<FJsonValue>>* ValuesArray, TArray<FString>& OutResults);

	// DataTable operations
	FToolResult EditDataTable(UDataTable* DataTable, const TSharedPtr<FJsonObject>& Args);
	int32 AddDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults);
	int32 RemoveDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults);
	int32 ModifyDataTableRows(UDataTable* DataTable, const TArray<TSharedPtr<FJsonValue>>* RowsArray, TArray<FString>& OutResults);

	/** Bulk import JSON - replaces all existing rows */
	FToolResult ImportDataTableJSON(UDataTable* DataTable, const FString& JSONString, TArray<FString>& OutResults);

	/** Bulk append JSON - adds rows to existing data */
	int32 AppendDataTableJSON(UDataTable* DataTable, const FString& JSONString, TArray<FString>& OutResults);

	// ChooserTable operations
	FToolResult EditChooserTable(UChooserTable* ChooserTable, const TSharedPtr<FJsonObject>& Args);
	int32 ReplaceChooserReferences(UChooserTable* ChooserTable, const TArray<TSharedPtr<FJsonValue>>* ReplacementsArray, TArray<FString>& OutResults);

	/** Parse struct field operation from JSON */
	FStructFieldOp ParseStructFieldOp(const TSharedPtr<FJsonObject>& FieldObj);

	/** Parse enum value operation from JSON */
	FEnumValueOp ParseEnumValueOp(const TSharedPtr<FJsonObject>& ValueObj);

	/** Parse row operation from JSON */
	FRowOp ParseRowOp(const TSharedPtr<FJsonObject>& RowObj);

	/** Convert type name to FEdGraphPinType */
	FEdGraphPinType TypeNameToPinType(const FString& TypeName);

	/** Find struct field by name */
	int32 FindStructFieldIndex(UUserDefinedStruct* Struct, const FString& FieldName);

	// StringTable operations
	FToolResult EditStringTable(UStringTable* StringTable, const TSharedPtr<FJsonObject>& Args);
	int32 AddStringTableEntries(UStringTable* StringTable, const TArray<TSharedPtr<FJsonValue>>* EntriesArray, TArray<FString>& OutResults);
	int32 RemoveStringTableEntries(UStringTable* StringTable, const TArray<TSharedPtr<FJsonValue>>* KeysArray, TArray<FString>& OutResults);
	int32 ModifyStringTableEntries(UStringTable* StringTable, const TArray<TSharedPtr<FJsonValue>>* EntriesArray, TArray<FString>& OutResults);
};
