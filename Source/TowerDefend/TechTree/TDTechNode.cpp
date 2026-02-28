// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDTechNode.h"
#include "TDTechTreeDataAsset.h"

void UTDTechNode::Initialize(const FTDTechNodeData* InNodeData)
{
    if (!ensureMsgf(InNodeData != nullptr, TEXT("UTDTechNode::Initialize - InNodeData is null")))
    {
        return;
    }

    NodeData = InNodeData;
    TechID = InNodeData->TechID;
    ResearchState = ETDTechResearchState::Locked;
}

FName UTDTechNode::GetTechID() const
{
    return TechID;
}

ETDTechResearchState UTDTechNode::GetResearchState() const
{
    return ResearchState;
}

void UTDTechNode::SetResearchState(ETDTechResearchState NewState)
{
    ResearchState = NewState;
}

const FTDTechNodeData* UTDTechNode::GetNodeData() const
{
    return NodeData;
}

bool UTDTechNode::IsCompleted() const
{
    return ResearchState == ETDTechResearchState::Completed;
}

int32 UTDTechNode::GetResearchCost() const
{
    if (!ensureMsgf(NodeData != nullptr, TEXT("UTDTechNode::GetResearchCost - NodeData is null")))
    {
        return 0;
    }

    return NodeData->ResearchCost;
}
