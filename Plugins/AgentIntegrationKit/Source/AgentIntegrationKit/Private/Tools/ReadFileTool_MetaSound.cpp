// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Metasound.h"
#include "MetasoundSource.h"
#include "MetasoundEditorGraphSchema.h"
#include "UObject/UObjectIterator.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EdGraph/EdGraph.h"

FToolResult FReadFileTool::ReadMetaSound(UObject* Asset, const FString& AssetPath,
	const TArray<FString>& Include, const FString& GraphName, int32 Offset, int32 Limit)
{
	bool bIsSource = Cast<UMetaSoundSource>(Asset) != nullptr;
	FString AssetType = bIsSource ? TEXT("MetaSoundSource") : TEXT("MetaSoundPatch");

	UEdGraph* MetaSoundGraph = nullptr;
	if (UMetaSoundSource* MSSource = Cast<UMetaSoundSource>(Asset))
	{
		MetaSoundGraph = MSSource->GetGraph();
	}
	else if (UMetaSoundPatch* MSPatch = Cast<UMetaSoundPatch>(Asset))
	{
		MetaSoundGraph = MSPatch->GetGraph();
	}

	if (!MetaSoundGraph && GEditor)
	{
		UAssetEditorSubsystem* AssetEditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>();
		if (AssetEditorSubsystem)
		{
			if (!AssetEditorSubsystem->FindEditorForAsset(Asset, false))
			{
				AssetEditorSubsystem->OpenEditorForAsset(Asset);
				FPlatformProcess::Sleep(0.2f);
			}

			if (UMetaSoundSource* MSSource = Cast<UMetaSoundSource>(Asset))
			{
				MetaSoundGraph = MSSource->GetGraph();
			}
			else if (UMetaSoundPatch* MSPatch = Cast<UMetaSoundPatch>(Asset))
			{
				MetaSoundGraph = MSPatch->GetGraph();
			}

			if (!MetaSoundGraph)
			{
				for (TObjectIterator<UEdGraph> It; It; ++It)
				{
					UEdGraph* TestGraph = *It;
					if (TestGraph && TestGraph->GetSchema() && TestGraph->GetSchema()->IsA<UMetasoundEditorGraphSchema>())
					{
						UObject* Outer = TestGraph->GetOuter();
						while (Outer)
						{
							if (Outer == Asset)
							{
								MetaSoundGraph = TestGraph;
								break;
							}
							Outer = Outer->GetOuter();
						}
						if (MetaSoundGraph)
						{
							break;
						}
					}
				}
			}
		}
	}

	FString Summary = FString::Printf(TEXT("# %s: %s\n"), *AssetType, *Asset->GetName());
	Summary += FString::Printf(TEXT("Path: %s\n"), *AssetPath);

	if (MetaSoundGraph)
	{
		Summary += FString::Printf(TEXT("Nodes: %d\n\n"), MetaSoundGraph->Nodes.Num());
	}
	else
	{
		Summary += TEXT("Graph: not available (open asset in editor first)\n");
		return FToolResult::Ok(Summary);
	}

	if (!GraphName.IsEmpty())
	{
		if (MetaSoundGraph->GetName().Equals(GraphName, ESearchCase::IgnoreCase))
		{
			FString Output = GetGraphWithNodes(MetaSoundGraph, TEXT("MetaSound"), TEXT(""), Offset, Limit);
			Output += TEXT("\n") + GetGraphConnections(MetaSoundGraph);
			return FToolResult::Ok(Output);
		}
		return FToolResult::Fail(FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	FString Output = Summary;
	if (Include.Contains(TEXT("graphs")) || Include.Contains(TEXT("graph")))
	{
		if (!Output.IsEmpty()) Output += TEXT("\n");
		Output += GetGraphWithNodes(MetaSoundGraph, TEXT("MetaSound"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(MetaSoundGraph);
	}

	if (Output.IsEmpty())
	{
		Output = FString::Printf(TEXT("# %s %s (no data)\n"), *AssetType, *Asset->GetName());
	}

	return FToolResult::Ok(Output);
}
