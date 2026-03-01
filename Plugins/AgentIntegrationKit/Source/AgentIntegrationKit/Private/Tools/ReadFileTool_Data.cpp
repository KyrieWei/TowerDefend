// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Engine/UserDefinedStruct.h"
#include "Engine/UserDefinedEnum.h"
#include "Engine/DataTable.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Curves/CurveFloat.h"
#include "Curves/CurveVector.h"
#include "Curves/CurveLinearColor.h"
#include "Curves/RichCurve.h"
#include "Engine/CurveTable.h"

// StringTable support
#include "Internationalization/StringTable.h"
#include "Internationalization/StringTableCore.h"

// ChooserTable support
#include "Chooser.h"
#include "IObjectChooser.h"
#include "ObjectChooser_Asset.h"
#include "OutputObjectColumn.h"

FString FReadFileTool::GetStructSummary(UUserDefinedStruct* Struct)
{
	TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);
	int32 FieldCount = VarDescArray.Num();

	FString Output = FString::Printf(TEXT("# STRUCT %s fields=%d\n"),
		*Struct->GetName(), FieldCount);

	// Get struct size if available
	int32 StructSize = Struct->GetStructureSize();
	Output += FString::Printf(TEXT("size=%d bytes\n"), StructSize);

	return Output;
}

FString FReadFileTool::GetStructFields(UUserDefinedStruct* Struct)
{
	TArray<FStructVariableDescription>& VarDescArray = FStructureEditorUtils::GetVarDesc(Struct);

	if (VarDescArray.Num() == 0)
	{
		return TEXT("# FIELDS 0\n");
	}

	FString Output = FString::Printf(TEXT("# FIELDS %d\n"), VarDescArray.Num());

	for (const FStructVariableDescription& VarDesc : VarDescArray)
	{
		// Get field name
		FString FieldName = VarDesc.VarName.ToString();

		// Get type from pin type (use ToPinType() method in UE5.7+)
		FEdGraphPinType PinType = VarDesc.ToPinType();
		FString TypeName = PinType.PinCategory.ToString();
		if (PinType.PinSubCategoryObject.IsValid())
		{
			TypeName = PinType.PinSubCategoryObject->GetName();
		}
		else if (!PinType.PinSubCategory.IsNone())
		{
			TypeName = PinType.PinSubCategory.ToString();
		}

		// Get default value
		FString DefaultValue = VarDesc.DefaultValue.IsEmpty() ? TEXT("None") : VarDesc.DefaultValue;

		// Get tooltip/description
		FString Description = VarDesc.ToolTip.IsEmpty() ? TEXT("") : VarDesc.ToolTip;

		// Format: name	type	default	[description]
		if (Description.IsEmpty())
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *FieldName, *TypeName, *DefaultValue);
		}
		else
		{
			Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\n"), *FieldName, *TypeName, *DefaultValue, *Description);
		}
	}

	return Output;
}

FString FReadFileTool::GetEnumSummary(UUserDefinedEnum* Enum)
{
	int32 ValueCount = Enum->NumEnums() - 1; // Exclude MAX value

	FString Output = FString::Printf(TEXT("# ENUM %s values=%d\n"),
		*Enum->GetName(), ValueCount);

	return Output;
}

FString FReadFileTool::GetEnumValues(UUserDefinedEnum* Enum)
{
	int32 ValueCount = Enum->NumEnums() - 1; // Exclude MAX value

	if (ValueCount == 0)
	{
		return TEXT("# VALUES 0\n");
	}

	FString Output = FString::Printf(TEXT("# VALUES %d\n"), ValueCount);

	for (int32 i = 0; i < ValueCount; i++)
	{
		// Get the enum value name
		FString ValueName = Enum->GetNameStringByIndex(i);

		// Get the display name
		FText DisplayNameText = Enum->GetDisplayNameTextByIndex(i);
		FString DisplayName = DisplayNameText.ToString();

		// Format: index	name	display_name
		Output += FString::Printf(TEXT("%d\t%s\t%s\n"), i, *ValueName, *DisplayName);
	}

	return Output;
}

FString FReadFileTool::GetDataTableSummary(UDataTable* DataTable)
{
	FString RowStructName = TEXT("None");
	if (DataTable->RowStruct)
	{
		RowStructName = DataTable->RowStruct->GetName();
	}

	// Get row names to count rows
	TArray<FName> RowNames = DataTable->GetRowNames();
	int32 RowCount = RowNames.Num();

	FString Output = FString::Printf(TEXT("# DATATABLE %s row_struct=%s rows=%d\n"),
		*DataTable->GetName(), *RowStructName, RowCount);

	// List column names (struct properties)
	if (DataTable->RowStruct)
	{
		Output += TEXT("\n# COLUMNS\n");
		for (TFieldIterator<FProperty> PropIt(DataTable->RowStruct); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			FString PropName = Property->GetName();
			FString PropType = Property->GetCPPType();

			Output += FString::Printf(TEXT("%s\t%s\n"), *PropName, *PropType);
		}
	}

	return Output;
}

FString FReadFileTool::GetDataTableRows(UDataTable* DataTable, int32 Offset, int32 Limit)
{
	TArray<FName> RowNames = DataTable->GetRowNames();
	int32 TotalRows = RowNames.Num();

	if (TotalRows == 0)
	{
		return TEXT("# ROWS 0\n");
	}

	int32 StartIdx = Offset - 1; // Convert to 0-based
	int32 EndIdx = FMath::Min(StartIdx + Limit, TotalRows);

	FString Output = FString::Printf(TEXT("# ROWS %d-%d/%d\n"), Offset, EndIdx, TotalRows);

	// Get column names for header
	TArray<FString> ColumnNames;
	if (DataTable->RowStruct)
	{
		for (TFieldIterator<FProperty> PropIt(DataTable->RowStruct); PropIt; ++PropIt)
		{
			ColumnNames.Add((*PropIt)->GetName());
		}
	}

	// Output rows
	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		FName RowName = RowNames[i];
		uint8* RowData = DataTable->FindRowUnchecked(RowName);

		if (!RowData)
		{
			Output += FString::Printf(TEXT("%s\t(no data)\n"), *RowName.ToString());
			continue;
		}

		Output += RowName.ToString();

		// Get property values
		if (DataTable->RowStruct)
		{
			for (TFieldIterator<FProperty> PropIt(DataTable->RowStruct); PropIt; ++PropIt)
			{
				FProperty* Property = *PropIt;

				// Export property value to string
				FString ValueStr;
				const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(RowData);
				Property->ExportTextItem_Direct(ValueStr, PropertyValue, nullptr, nullptr, PPF_None);

				// Truncate long values
				if (ValueStr.Len() > 50)
				{
					ValueStr = ValueStr.Left(47) + TEXT("...");
				}

				Output += FString::Printf(TEXT("\t%s"), *ValueStr);
			}
		}

		Output += TEXT("\n");
	}

	return Output;
}

FString FReadFileTool::FormatRichCurve(const FRichCurve& Curve, const FString& CurveName)
{
	FString Output;
	if (!CurveName.IsEmpty())
	{
		Output += FString::Printf(TEXT("### %s\n"), *CurveName);
	}

	Output += FString::Printf(TEXT("Keys: %d\n"), Curve.GetNumKeys());

	auto ExtrapStr = [](ERichCurveExtrapolation E) -> FString
	{
		switch (E)
		{
			case RCCE_Cycle: return TEXT("Cycle");
			case RCCE_CycleWithOffset: return TEXT("CycleWithOffset");
			case RCCE_Oscillate: return TEXT("Oscillate");
			case RCCE_Linear: return TEXT("Linear");
			case RCCE_Constant: return TEXT("Constant");
			case RCCE_None: return TEXT("None");
			default: return TEXT("Unknown");
		}
	};

	Output += FString::Printf(TEXT("Pre-Infinity: %s\n"), *ExtrapStr(Curve.PreInfinityExtrap));
	Output += FString::Printf(TEXT("Post-Infinity: %s\n"), *ExtrapStr(Curve.PostInfinityExtrap));

	if (Curve.DefaultValue < MAX_flt)
	{
		Output += FString::Printf(TEXT("Default Value: %.4f\n"), Curve.DefaultValue);
	}

	if (Curve.GetNumKeys() == 0) return Output;

	auto InterpStr = [](ERichCurveInterpMode M) -> FString
	{
		switch (M)
		{
			case RCIM_Linear: return TEXT("Linear");
			case RCIM_Constant: return TEXT("Constant");
			case RCIM_Cubic: return TEXT("Cubic");
			default: return TEXT("None");
		}
	};

	const TArray<FRichCurveKey>& Keys = Curve.GetConstRefOfKeys();
	for (const FRichCurveKey& Key : Keys)
	{
		Output += FString::Printf(TEXT("  T=%.4f V=%.4f %s"),
			Key.Time, Key.Value, *InterpStr(Key.InterpMode));

		if (Key.InterpMode == RCIM_Cubic)
		{
			Output += FString::Printf(TEXT(" Arrive=%.3f Leave=%.3f"), Key.ArriveTangent, Key.LeaveTangent);
		}
		Output += TEXT("\n");
	}

	return Output;
}

FToolResult FReadFileTool::ReadCurveAsset(UCurveBase* CurveAsset)
{
	FString Output;

	if (UCurveFloat* FloatCurve = Cast<UCurveFloat>(CurveAsset))
	{
		Output = FString::Printf(TEXT("# CURVE_FLOAT: %s\n"), *CurveAsset->GetName());
		Output += FString::Printf(TEXT("Path: %s\n\n"), *CurveAsset->GetPathName());
		Output += FormatRichCurve(FloatCurve->FloatCurve, TEXT(""));
	}
	else if (UCurveVector* VecCurve = Cast<UCurveVector>(CurveAsset))
	{
		Output = FString::Printf(TEXT("# CURVE_VECTOR: %s\n"), *CurveAsset->GetName());
		Output += FString::Printf(TEXT("Path: %s\n\n"), *CurveAsset->GetPathName());
		// FloatCurves is a fixed-size array [3], always has X/Y/Z
		Output += FormatRichCurve(VecCurve->FloatCurves[0], TEXT("X"));
		Output += FormatRichCurve(VecCurve->FloatCurves[1], TEXT("Y"));
		Output += FormatRichCurve(VecCurve->FloatCurves[2], TEXT("Z"));
	}
	else if (UCurveLinearColor* ColorCurve = Cast<UCurveLinearColor>(CurveAsset))
	{
		Output = FString::Printf(TEXT("# CURVE_LINEAR_COLOR: %s\n"), *CurveAsset->GetName());
		Output += FString::Printf(TEXT("Path: %s\n\n"), *CurveAsset->GetPathName());
		// FloatCurves is a fixed-size array [4], always has R/G/B/A
		Output += FormatRichCurve(ColorCurve->FloatCurves[0], TEXT("R"));
		Output += FormatRichCurve(ColorCurve->FloatCurves[1], TEXT("G"));
		Output += FormatRichCurve(ColorCurve->FloatCurves[2], TEXT("B"));
		Output += FormatRichCurve(ColorCurve->FloatCurves[3], TEXT("A"));
	}
	else
	{
		return ReadGenericAsset(CurveAsset);
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadFileTool::ReadCurveTable(UCurveTable* CurveTableAsset)
{
	FString Output = FString::Printf(TEXT("# CURVE_TABLE: %s\n"), *CurveTableAsset->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *CurveTableAsset->GetPathName());

	ECurveTableMode Mode = CurveTableAsset->GetCurveTableMode();
	FString ModeStr;
	switch (Mode)
	{
		case ECurveTableMode::Empty: ModeStr = TEXT("Empty"); break;
		case ECurveTableMode::SimpleCurves: ModeStr = TEXT("SimpleCurves"); break;
		case ECurveTableMode::RichCurves: ModeStr = TEXT("RichCurves"); break;
		default: ModeStr = TEXT("Unknown"); break;
	}

	const TMap<FName, FRealCurve*>& RowMap = CurveTableAsset->GetRowMap();
	Output += FString::Printf(TEXT("Mode: %s\n"), *ModeStr);
	Output += FString::Printf(TEXT("Rows: %d\n"), RowMap.Num());

	if (RowMap.Num() == 0) return FToolResult::Ok(Output);

	Output += TEXT("\n## Rows\n");

	for (const auto& Pair : RowMap)
	{
		FRealCurve* Curve = Pair.Value;
		if (!Curve) continue;

		if (Mode == ECurveTableMode::RichCurves)
		{
			FRichCurve* RichCurve = static_cast<FRichCurve*>(Curve);
			Output += FormatRichCurve(*RichCurve, Pair.Key.ToString());
		}
		else
		{
			float MinTime, MaxTime;
			Curve->GetTimeRange(MinTime, MaxTime);
			float MinVal, MaxVal;
			Curve->GetValueRange(MinVal, MaxVal);
			Output += FString::Printf(TEXT("### %s\n"), *Pair.Key.ToString());
			Output += FString::Printf(TEXT("Keys: %d  Time=[%.3f, %.3f]  Values=[%.3f, %.3f]\n"),
				Curve->GetNumKeys(), MinTime, MaxTime, MinVal, MaxVal);
		}
	}

	return FToolResult::Ok(Output);
}

// ============================================================================
// STRINGTABLE SUPPORT
// ============================================================================

FToolResult FReadFileTool::ReadStringTable(UStringTable* StringTable, int32 Offset, int32 Limit)
{
	FStringTableConstRef Table = StringTable->GetStringTable();

	// Get namespace
	FString Namespace = Table->GetNamespace();

	// Count entries
	int32 TotalEntries = 0;
	Table->EnumerateSourceStrings([&](const FString& Key, const FString& SourceString)
	{
		TotalEntries++;
		return true;
	});

	// Header
	FString Output = FString::Printf(TEXT("# STRING_TABLE %s entries=%d\n"),
		*StringTable->GetName(), TotalEntries);
	Output += FString::Printf(TEXT("Path: %s\n"), *StringTable->GetPathName());
	Output += FString::Printf(TEXT("Namespace: %s\n"), *Namespace);

	if (TotalEntries == 0)
	{
		Output += TEXT("\n# ENTRIES 0\n");
		return FToolResult::Ok(Output);
	}

	// Collect all entries (need to materialize for pagination)
	struct FSTEntry
	{
		FString Key;
		FString Value;
	};
	TArray<FSTEntry> Entries;
	Entries.Reserve(TotalEntries);

	Table->EnumerateSourceStrings([&](const FString& Key, const FString& SourceString)
	{
		Entries.Add({ Key, SourceString });
		return true;
	});

	// Sort by key for consistent output
	Entries.Sort([](const FSTEntry& A, const FSTEntry& B) { return A.Key < B.Key; });

	// Pagination
	int32 StartIdx = Offset - 1; // Convert to 0-based
	int32 EndIdx = FMath::Min(StartIdx + Limit, Entries.Num());

	Output += FString::Printf(TEXT("\n# ENTRIES %d-%d/%d\n"), Offset, EndIdx, TotalEntries);
	Output += TEXT("# Key\tValue\n");

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		// Truncate very long values for readability
		FString DisplayValue = Entries[i].Value;
		if (DisplayValue.Len() > 200)
		{
			DisplayValue = DisplayValue.Left(197) + TEXT("...");
		}
		Output += FString::Printf(TEXT("%s\t%s\n"), *Entries[i].Key, *DisplayValue);
	}

	// Check if any entries have metadata
	bool bHasMetadata = false;
	for (int32 i = StartIdx; i < EndIdx && !bHasMetadata; i++)
	{
		Table->EnumerateMetaData(FTextKey(Entries[i].Key), [&](FName MetaId, const FString& MetaValue)
		{
			bHasMetadata = true;
			return false; // Stop on first find
		});
	}

	if (bHasMetadata)
	{
		Output += TEXT("\n# METADATA\n");
		Output += TEXT("# Key\tMetaDataId\tMetaDataValue\n");

		for (int32 i = StartIdx; i < EndIdx; i++)
		{
			Table->EnumerateMetaData(FTextKey(Entries[i].Key), [&](FName MetaId, const FString& MetaValue)
			{
				Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *Entries[i].Key, *MetaId.ToString(), *MetaValue);
				return true;
			});
		}
	}

	return FToolResult::Ok(Output);
}

// ============================================================================
// CHOOSERTABLE SUPPORT
// ============================================================================

FString FReadFileTool::GetChooserTableSummary(UChooserTable* ChooserTable)
{
	int32 ColumnCount = ChooserTable->ColumnsStructs.Num();

	// Count rows by finding the max row count across all columns
	int32 RowCount = 0;
	int32 OutputColumnCount = 0;
	int32 FilterColumnCount = 0;

	for (const FInstancedStruct& ColumnStruct : ChooserTable->ColumnsStructs)
	{
		if (!ColumnStruct.IsValid())
		{
			continue;
		}

		if (const FChooserColumnBase* ColumnBase = ColumnStruct.GetPtr<FChooserColumnBase>())
		{
			if (ColumnBase->HasOutputs())
			{
				OutputColumnCount++;
			}
			else
			{
				FilterColumnCount++;
			}
		}

		// Try to get row count from output column
		if (const FOutputObjectColumn* OutputCol = ColumnStruct.GetPtr<FOutputObjectColumn>())
		{
			RowCount = FMath::Max(RowCount, OutputCol->RowValues.Num());
		}
	}

	FString Output = FString::Printf(TEXT("# CHOOSERTABLE %s\n"), *ChooserTable->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *ChooserTable->GetPathName());
	Output += FString::Printf(TEXT("Columns: %d (filters=%d, outputs=%d)\n"), ColumnCount, FilterColumnCount, OutputColumnCount);
	Output += FString::Printf(TEXT("Rows: %d\n"), RowCount);

	// Check for fallback
	if (ChooserTable->FallbackResult.IsValid())
	{
		Output += TEXT("Fallback: Yes\n");
	}

	return Output;
}

FString FReadFileTool::GetChooserTableReferences(UChooserTable* ChooserTable, int32 Offset, int32 Limit)
{
	TArray<FString> References;
	int32 TotalRefs = 0;

	// Collect all references first
	for (const FInstancedStruct& ColumnStruct : ChooserTable->ColumnsStructs)
	{
		if (!ColumnStruct.IsValid())
		{
			continue;
		}

		// Try to get as output object column
		if (const FOutputObjectColumn* OutputCol = ColumnStruct.GetPtr<FOutputObjectColumn>())
		{
			// Process each row value
			for (int32 RowIdx = 0; RowIdx < OutputCol->RowValues.Num(); ++RowIdx)
			{
				const FChooserOutputObjectRowData& RowData = OutputCol->RowValues[RowIdx];

				if (!RowData.Value.IsValid())
				{
					continue;
				}

				// Try FAssetChooser (hard reference)
				if (const FAssetChooser* AssetChooser = RowData.Value.GetPtr<FAssetChooser>())
				{
					if (UObject* Asset = AssetChooser->Asset.Get())
					{
						References.Add(FString::Printf(TEXT("%d\t%s\t%s\t%s"),
							RowIdx,
							*Asset->GetName(),
							*Asset->GetClass()->GetName(),
							*Asset->GetPathName()));
						TotalRefs++;
					}
				}
				// Try FSoftAssetChooser (soft reference)
				else if (const FSoftAssetChooser* SoftChooser = RowData.Value.GetPtr<FSoftAssetChooser>())
				{
					FSoftObjectPath SoftPath = SoftChooser->Asset.ToSoftObjectPath();
					if (SoftPath.IsValid())
					{
						References.Add(FString::Printf(TEXT("%d\t%s\t(soft)\t%s"),
							RowIdx,
							*FPackageName::GetShortName(SoftPath.ToString()),
							*SoftPath.ToString()));
						TotalRefs++;
					}
				}
			}

			// Check fallback
			if (OutputCol->FallbackValue.Value.IsValid())
			{
				if (const FAssetChooser* AssetChooser = OutputCol->FallbackValue.Value.GetPtr<FAssetChooser>())
				{
					if (UObject* Asset = AssetChooser->Asset.Get())
					{
						References.Add(FString::Printf(TEXT("FB\t%s\t%s\t%s"),
							*Asset->GetName(),
							*Asset->GetClass()->GetName(),
							*Asset->GetPathName()));
						TotalRefs++;
					}
				}
			}
		}
	}

	// Apply pagination
	int32 StartIdx = Offset - 1;  // Convert to 0-based
	int32 EndIdx = FMath::Min(StartIdx + Limit, References.Num());

	FString Output = FString::Printf(TEXT("# REFERENCES %d-%d/%d\n"), Offset, EndIdx, TotalRefs);
	Output += TEXT("# Row\tAssetName\tType\tPath\n");

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		Output += References[i] + TEXT("\n");
	}

	return Output;
}

FString FReadFileTool::GetChooserTableColumns(UChooserTable* ChooserTable)
{
	FString Output = FString::Printf(TEXT("# COLUMNS %d\n"), ChooserTable->ColumnsStructs.Num());

	int32 ColIdx = 0;
	for (const FInstancedStruct& ColumnStruct : ChooserTable->ColumnsStructs)
	{
		if (!ColumnStruct.IsValid())
		{
			Output += FString::Printf(TEXT("%d\t(invalid)\n"), ColIdx);
			ColIdx++;
			continue;
		}

		// Get the struct type name
		FString TypeName = ColumnStruct.GetScriptStruct() ? ColumnStruct.GetScriptStruct()->GetName() : TEXT("Unknown");

		if (const FChooserColumnBase* ColumnBase = ColumnStruct.GetPtr<FChooserColumnBase>())
		{
			FString ColumnType = ColumnBase->HasOutputs() ? TEXT("Output") : TEXT("Filter");
			Output += FString::Printf(TEXT("%d\t%s\t%s\n"), ColIdx, *TypeName, *ColumnType);
		}
		else
		{
			Output += FString::Printf(TEXT("%d\t%s\n"), ColIdx, *TypeName);
		}

		ColIdx++;
	}

	return Output;
}
