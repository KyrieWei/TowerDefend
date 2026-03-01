// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "PCGGraph.h"
#include "PCGNode.h"
#include "PCGEdge.h"
#include "PCGPin.h"
#include "PCGSettings.h"

FToolResult FReadFileTool::ReadPCGGraph(UPCGGraphInterface* PCGGraphInterface)
{
	UPCGGraph* PCGGraph = PCGGraphInterface->GetGraph();
	if (!PCGGraph)
	{
		return FToolResult::Fail(TEXT("Failed to get PCG graph from interface"));
	}

	FString PCGSummary;
	PCGSummary += FString::Printf(TEXT("PCG Graph: %s\n"), *PCGGraph->GetName());
	PCGSummary += TEXT("================\n\n");

	const TArray<UPCGNode*>& Nodes = PCGGraph->GetNodes();
	PCGSummary += FString::Printf(TEXT("Nodes (%d):\n"), Nodes.Num());

	for (UPCGNode* Node : Nodes)
	{
		if (!Node)
		{
			continue;
		}

		FString NodeName = Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
		FString NodeGuid = Node->GetFName().ToString();

		int32 PcgPosX = 0, PcgPosY = 0;
		Node->GetNodePosition(PcgPosX, PcgPosY);
		PCGSummary += FString::Printf(TEXT("\n  [%s] %s  pos=%d,%d\n"), *NodeGuid, *NodeName, PcgPosX, PcgPosY);

		if (const UPCGSettings* Settings = Node->GetSettings())
		{
			PCGSummary += FString::Printf(TEXT("    Type: %s\n"), *Settings->GetClass()->GetName());

			TArray<FPCGPinProperties> InputPins = Settings->AllInputPinProperties();
			if (InputPins.Num() > 0)
			{
				PCGSummary += TEXT("    Inputs: ");
				for (int32 i = 0; i < InputPins.Num(); i++)
				{
					if (i > 0) PCGSummary += TEXT(", ");
					PCGSummary += InputPins[i].Label.ToString();
				}
				PCGSummary += TEXT("\n");
			}

			TArray<FPCGPinProperties> OutputPins = Settings->AllOutputPinProperties();
			if (OutputPins.Num() > 0)
			{
				PCGSummary += TEXT("    Outputs: ");
				for (int32 i = 0; i < OutputPins.Num(); i++)
				{
					if (i > 0) PCGSummary += TEXT(", ");
					PCGSummary += OutputPins[i].Label.ToString();
				}
				PCGSummary += TEXT("\n");
			}
		}

		for (const TObjectPtr<UPCGPin>& OutputPin : Node->GetOutputPins())
		{
			if (!OutputPin)
			{
				continue;
			}
			for (const TObjectPtr<UPCGEdge>& Edge : OutputPin->Edges)
			{
				if (Edge && Edge->InputPin && Edge->InputPin->Node)
				{
					FString TargetNodeName = Edge->InputPin->Node->GetNodeTitle(EPCGNodeTitleType::ListView).ToString();
					PCGSummary += FString::Printf(TEXT("    -> %s:%s -> %s:%s\n"),
						*OutputPin->Properties.Label.ToString(),
						*NodeName,
						*Edge->InputPin->Properties.Label.ToString(),
						*TargetNodeName);
				}
			}
		}
	}

	return FToolResult::Ok(PCGSummary);
}
#endif // UE 5.7+
