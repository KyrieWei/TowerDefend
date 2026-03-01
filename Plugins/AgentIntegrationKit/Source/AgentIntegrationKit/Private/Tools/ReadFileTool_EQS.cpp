// Copyright 2026 Betide Studio. All Rights Reserved.
// EQS (Environment Query System) asset reading for ReadFileTool

#include "Tools/ReadFileTool.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Tools/NeoStackToolUtils.h"

// EQS runtime
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/EnvQueryGenerator.h"
#include "EnvironmentQuery/EnvQueryTest.h"

// EQS editor graph (for GUID map)
#include "EnvironmentQueryGraph.h"
#include "EnvironmentQueryGraphNode.h"
#include "EnvironmentQueryGraphNode_Option.h"
#include "EnvironmentQueryGraphNode_Test.h"
#include "AIGraphNode.h"

// Property reflection
#include "UObject/UnrealType.h"

// ---------------------------------------------------------------------------
// Helper: get short GUID from graph node for a runtime instance
// ---------------------------------------------------------------------------
static void BuildEQSGuidMap(UEnvQuery* Query, TMap<UObject*, FGuid>& OutGuidMap, TMap<UObject*, FVector2D>& OutPositionMap)
{
	if (!Query || !Query->EdGraph)
	{
		return;
	}

	UEnvironmentQueryGraph* EQSGraph = Cast<UEnvironmentQueryGraph>(Query->EdGraph);
	if (!EQSGraph)
	{
		return;
	}

	for (UEdGraphNode* Node : EQSGraph->Nodes)
	{
		UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
		if (!AINode)
		{
			continue;
		}

		if (AINode->NodeInstance)
		{
			OutGuidMap.Add(AINode->NodeInstance, AINode->NodeGuid);
			OutPositionMap.Add(AINode->NodeInstance, FVector2D(AINode->NodePosX, AINode->NodePosY));
		}

		for (UAIGraphNode* SubNode : AINode->SubNodes)
		{
			if (SubNode && SubNode->NodeInstance)
			{
				OutGuidMap.Add(SubNode->NodeInstance, SubNode->NodeGuid);
			}
		}
	}
}

static FString GetEQSShortGuid(const UObject* Instance, const TMap<UObject*, FGuid>& GuidMap)
{
	if (!Instance)
	{
		return TEXT("");
	}
	const FGuid* Found = GuidMap.Find(const_cast<UObject*>(Instance));
	if (Found)
	{
		return Found->ToString().Left(8);
	}
	return TEXT("");
}

// ---------------------------------------------------------------------------
// Helper: get property summary for an EQS node (generator or test)
// Shows editable properties with their values
// ---------------------------------------------------------------------------
static FString GetEQSNodePropertySummary(UObject* NodeObj, const FString& Indent)
{
	if (!NodeObj)
	{
		return TEXT("");
	}

	FString Output;

	// Get the base class to skip internal properties
	// For generators, skip UEnvQueryGenerator base props; for tests, skip UEnvQueryTest base
	UClass* BaseClass = nullptr;
	if (NodeObj->IsA<UEnvQueryTest>())
	{
		BaseClass = UEnvQueryTest::StaticClass();
	}
	else if (NodeObj->IsA<UEnvQueryGenerator>())
	{
		BaseClass = UEnvQueryGenerator::StaticClass();
	}

	for (TFieldIterator<FProperty> PropIt(NodeObj->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Only show editable properties
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		// Skip properties from base class (internal engine fields)
		if (BaseClass && Property->GetOwnerClass() == BaseClass)
		{
			continue;
		}

		// Skip delegate properties
		if (Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>())
		{
			continue;
		}

		FString PropName = Property->GetName();
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(NodeObj);
		if (!ValuePtr)
		{
			continue;
		}

		// FAIDataProvider*Value — show the DefaultValue
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct)
			{
				FString StructName = StructProp->Struct->GetName();
				if (StructName == TEXT("AIDataProviderFloatValue") ||
					StructName == TEXT("AIDataProviderIntValue") ||
					StructName == TEXT("AIDataProviderBoolValue"))
				{
					FProperty* DefaultProp = StructProp->Struct->FindPropertyByName(TEXT("DefaultValue"));
					if (DefaultProp)
					{
						void* DefaultPtr = DefaultProp->ContainerPtrToValuePtr<void>(const_cast<void*>(ValuePtr));
						if (DefaultPtr)
						{
							FString DefaultStr;
							DefaultProp->ExportTextItem_Direct(DefaultStr, DefaultPtr, nullptr, nullptr, PPF_None);

							// Skip zero/default values for cleaner output
							if (DefaultStr == TEXT("0") || DefaultStr == TEXT("0.000000") || DefaultStr == TEXT("false"))
							{
								continue;
							}

							Output += FString::Printf(TEXT("%s  %s=%s\n"), *Indent, *PropName, *DefaultStr);
							continue;
						}
					}
				}

				// FEnvTraceData — show trace shape info
				if (StructName == TEXT("EnvTraceData"))
				{
					FString TraceStr;
					Property->ExportTextItem_Direct(TraceStr, ValuePtr, nullptr, nullptr, PPF_None);
					if (!TraceStr.IsEmpty() && TraceStr.Len() < 200)
					{
						Output += FString::Printf(TEXT("%s  %s=%s\n"), *Indent, *PropName, *TraceStr);
					}
					continue;
				}
			}
		}

		// Enum properties
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			if (UEnum* Enum = EnumProp->GetEnum())
			{
				FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
				int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(
					EnumProp->ContainerPtrToValuePtr<void>(NodeObj));
				FString ValueName = Enum->GetNameStringByValue(EnumValue);
				Output += FString::Printf(TEXT("%s  %s=%s\n"), *Indent, *PropName, *ValueName);
			}
			continue;
		}

		// TEnumAsByte
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
			{
				uint8 ByteValue = ByteProp->GetPropertyValue_InContainer(NodeObj);
				FString ValueName = Enum->GetNameStringByValue(ByteValue);
				Output += FString::Printf(TEXT("%s  %s=%s\n"), *Indent, *PropName, *ValueName);
				continue;
			}
		}

		// Class references (context classes)
		if (FClassProperty* ClassProp = CastField<FClassProperty>(Property))
		{
			UClass* ClassValue = Cast<UClass>(ClassProp->GetObjectPropertyValue(ValuePtr));
			if (ClassValue)
			{
				Output += FString::Printf(TEXT("%s  %s=%s\n"), *Indent, *PropName, *ClassValue->GetName());
			}
			continue;
		}

		// Generic export for other properties
		FString ValueStr;
		Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);

		if (ValueStr.Len() > 120)
		{
			ValueStr = ValueStr.Left(117) + TEXT("...");
		}

		// Skip empty/default values
		if (ValueStr.IsEmpty() || ValueStr == TEXT("None") || ValueStr == TEXT("0") || ValueStr == TEXT("0.000000"))
		{
			continue;
		}

		Output += FString::Printf(TEXT("%s  %s=%s\n"), *Indent, *PropName, *ValueStr);
	}

	return Output;
}

// ---------------------------------------------------------------------------
// GetEQSSummary — quick overview of the EQS asset
// ---------------------------------------------------------------------------
FString FReadFileTool::GetEQSSummary(UEnvQuery* Query)
{
	if (!Query)
	{
		return TEXT("# ENVIRONMENT_QUERY (null)\n");
	}

	int32 OptionCount = Query->GetOptions().Num();
	int32 TotalTests = 0;
	for (const UEnvQueryOption* Option : Query->GetOptions())
	{
		if (Option)
		{
			TotalTests += Option->Tests.Num();
		}
	}

	FString Output = FString::Printf(TEXT("# ENVIRONMENT_QUERY %s\n"), *Query->GetName());
	Output += FString::Printf(TEXT("options=%d total_tests=%d\n"), OptionCount, TotalTests);
	return Output;
}

// ---------------------------------------------------------------------------
// GetEQSDetails — full structure with options, generators, tests, properties
// ---------------------------------------------------------------------------
FString FReadFileTool::GetEQSDetails(UEnvQuery* Query)
{
	if (!Query)
	{
		return TEXT("(null query)\n");
	}

	// Build GUID and position maps for node references
	TMap<UObject*, FGuid> GuidMap;
	TMap<UObject*, FVector2D> PositionMap;
	BuildEQSGuidMap(Query, GuidMap, PositionMap);

	FString Output;

	const TArray<UEnvQueryOption*>& Options = Query->GetOptions();
	if (Options.Num() == 0)
	{
		Output += TEXT("(no options)\n");
		return Output;
	}

	for (int32 i = 0; i < Options.Num(); i++)
	{
		const UEnvQueryOption* Option = Options[i];
		if (!Option)
		{
			Output += FString::Printf(TEXT("\n## Option %d: (null)\n"), i);
			continue;
		}

		// Generator info
		FString GeneratorClass = TEXT("(none)");
		FString GeneratorTitle;
		if (Option->Generator)
		{
			GeneratorClass = Option->Generator->GetClass()->GetName();
			GeneratorClass.RemoveFromStart(TEXT("EnvQueryGenerator_"));
			GeneratorTitle = Option->GetDescriptionTitle().ToString();
		}

		FString OptionGuid = GetEQSShortGuid(Option, GuidMap);
		const FVector2D* OptionPos = PositionMap.Find(const_cast<UEnvQueryOption*>(Option));
		Output += FString::Printf(TEXT("\n## Option %d: %s"), i, *GeneratorClass);
		if (!OptionGuid.IsEmpty())
		{
			Output += FString::Printf(TEXT("  guid=%s"), *OptionGuid);
		}
		if (OptionPos)
		{
			Output += FString::Printf(TEXT("  pos=%.0f,%.0f"), OptionPos->X, OptionPos->Y);
		}
		Output += TEXT("\n");

		if (!GeneratorTitle.IsEmpty() && GeneratorTitle != GeneratorClass)
		{
			Output += FString::Printf(TEXT("  title=%s\n"), *GeneratorTitle);
		}

		// Generator properties
		if (Option->Generator)
		{
			FString GenProps = GetEQSNodePropertySummary(Option->Generator, TEXT(""));
			if (!GenProps.IsEmpty())
			{
				Output += GenProps;
			}
		}

		// Tests
		Output += FString::Printf(TEXT("  tests=%d\n"), Option->Tests.Num());

		for (int32 t = 0; t < Option->Tests.Num(); t++)
		{
			const UEnvQueryTest* Test = Option->Tests[t];
			if (!Test)
			{
				Output += FString::Printf(TEXT("  [%d] (null)\n"), t);
				continue;
			}

			FString TestClass = Test->GetClass()->GetName();
			TestClass.RemoveFromStart(TEXT("EnvQueryTest_"));

			FString TestGuid = GetEQSShortGuid(Test, GuidMap);

			// Get key test properties inline
			FString PurposeStr;
			switch (Test->TestPurpose)
			{
			case EEnvTestPurpose::Filter: PurposeStr = TEXT("Filter"); break;
			case EEnvTestPurpose::Score: PurposeStr = TEXT("Score"); break;
			case EEnvTestPurpose::FilterAndScore: PurposeStr = TEXT("FilterAndScore"); break;
			default: PurposeStr = TEXT("Unknown"); break;
			}

			Output += FString::Printf(TEXT("  [%d] %s  purpose=%s"), t, *TestClass, *PurposeStr);
			if (!TestGuid.IsEmpty())
			{
				Output += FString::Printf(TEXT("  guid=%s"), *TestGuid);
			}
			Output += TEXT("\n");

			// Additional test properties (filter type, scoring equation, etc.)
			FString TestProps = GetEQSNodePropertySummary(const_cast<UEnvQueryTest*>(Test), TEXT("    "));
			if (!TestProps.IsEmpty())
			{
				Output += TestProps;
			}
		}
	}

	return Output;
}

#else // ENGINE_MINOR_VERSION < 6

#include "EnvironmentQuery/EnvQuery.h"

FString FReadFileTool::GetEQSSummary(UEnvQuery*) { return TEXT("EQS reading requires UE 5.6+\n"); }
FString FReadFileTool::GetEQSDetails(UEnvQuery*) { return TEXT(""); }

#endif
