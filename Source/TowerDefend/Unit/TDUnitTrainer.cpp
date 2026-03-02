// Copyright TowerDefend. All Rights Reserved.

#include "Unit/TDUnitTrainer.h"
#include "Unit/TDUnitDataAsset.h"
#include "Core/TDPlayerState.h"

bool UTDUnitTrainer::QueueTraining(
    UTDUnitDataAsset* UnitData,
    int32 Count,
    ATDPlayerState* Player)
{
    if (!UnitData || !Player || Count <= 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDUnitTrainer::QueueTraining - Invalid parameters."));
        return false;
    }

    const int32 TotalCost = UnitData->GoldCost * Count;

    if (!Player->CanAfford(TotalCost))
    {
        UE_LOG(LogTemp, Log,
            TEXT("UTDUnitTrainer::QueueTraining - Cannot afford %d gold for %d x %s."),
            TotalCost, Count, *UnitData->UnitID.ToString());
        return false;
    }

    if (!Player->SpendGold(TotalCost))
    {
        return false;
    }

    FTDTrainingEntry Entry;
    Entry.UnitData = UnitData;
    Entry.Count = Count;
    TrainingQueue.Add(Entry);

    UE_LOG(LogTemp, Log,
        TEXT("UTDUnitTrainer::QueueTraining - Queued %d x %s for player %d (cost: %d)."),
        Count, *UnitData->UnitID.ToString(), OwnerPlayerIndex, TotalCost);

    return true;
}

bool UTDUnitTrainer::CancelTraining(int32 EntryIndex, ATDPlayerState* Player)
{
    if (!Player || !TrainingQueue.IsValidIndex(EntryIndex))
    {
        return false;
    }

    const FTDTrainingEntry& Entry = TrainingQueue[EntryIndex];
    if (Entry.UnitData)
    {
        const int32 RefundAmount = Entry.UnitData->GoldCost * Entry.Count;
        Player->AddGold(RefundAmount);
    }

    TrainingQueue.RemoveAt(EntryIndex);
    return true;
}

int32 UTDUnitTrainer::GetTotalQueuedUnits() const
{
    int32 Total = 0;
    for (const FTDTrainingEntry& Entry : TrainingQueue)
    {
        Total += Entry.Count;
    }
    return Total;
}

TArray<FTDTrainingEntry> UTDUnitTrainer::ConsumeQueue()
{
    TArray<FTDTrainingEntry> Result = MoveTemp(TrainingQueue);
    TrainingQueue.Empty();
    return Result;
}

void UTDUnitTrainer::ClearQueue()
{
    TrainingQueue.Empty();
}
