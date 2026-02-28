// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDTechTreeDataAsset.h"

const FTDTechNodeData* UTDTechTreeDataAsset::FindTechNode(FName TechID) const
{
    for (const FTDTechNodeData& Node : TechNodes)
    {
        if (Node.TechID == TechID)
        {
            return &Node;
        }
    }

    return nullptr;
}

TArray<const FTDTechNodeData*> UTDTechTreeDataAsset::GetTechsByEra(ETDTechEra Era) const
{
    TArray<const FTDTechNodeData*> Result;

    for (const FTDTechNodeData& Node : TechNodes)
    {
        if (Node.Era == Era)
        {
            Result.Add(&Node);
        }
    }

    return Result;
}

const TArray<FTDTechNodeData>& UTDTechTreeDataAsset::GetAllTechNodes() const
{
    return TechNodes;
}
