// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TDUnitTrainer.generated.h"

class UTDUnitDataAsset;
class ATDPlayerState;

/**
 * Training queue entry.
 * Describes a pending unit and its cost information.
 */
USTRUCT(BlueprintType)
struct FTDTrainingEntry
{
    GENERATED_BODY()

    /** Unit data asset to train. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training")
    UTDUnitDataAsset* UnitData = nullptr;

    /** Number of units to train. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Training",
        meta = (ClampMin = "1"))
    int32 Count = 1;
};

/**
 * UTDUnitTrainer - Unit training manager.
 *
 * Manages the unit training queue during the preparation phase.
 * Validates gold balance and tech unlock conditions before adding
 * units to the training list. At round settlement the TDUnitDeployer
 * reads and spawns the actual Actors.
 *
 * Each player holds one instance.
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDUnitTrainer : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Add a unit to the training queue.
     * Validates gold sufficiency and deducts on success.
     *
     * @param UnitData   Unit data asset to train.
     * @param Count      Number of units to train.
     * @param Player     Player state (for payment).
     * @return           Whether the entry was successfully queued.
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Training")
    bool QueueTraining(UTDUnitDataAsset* UnitData, int32 Count, ATDPlayerState* Player);

    /**
     * Cancel a specific entry in the training queue.
     * Refunds gold to the player.
     *
     * @param EntryIndex  Queue index.
     * @param Player      Player state (for refund).
     * @return            Whether the cancellation succeeded.
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Training")
    bool CancelTraining(int32 EntryIndex, ATDPlayerState* Player);

    /** Get the current training queue. */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Training")
    const TArray<FTDTrainingEntry>& GetTrainingQueue() const { return TrainingQueue; }

    /** Get the total number of units across all queue entries. */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Training")
    int32 GetTotalQueuedUnits() const;

    /** Consume and clear the training queue (called by Deployer). */
    TArray<FTDTrainingEntry> ConsumeQueue();

    /** Clear the training queue without refunding. */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Training")
    void ClearQueue();

    /** Set the owning player index. */
    void SetOwnerPlayerIndex(int32 InIndex) { OwnerPlayerIndex = InIndex; }

    /** Get the owning player index. */
    int32 GetOwnerPlayerIndex() const { return OwnerPlayerIndex; }

private:
    /** Training queue entries. */
    TArray<FTDTrainingEntry> TrainingQueue;

    /** Owning player index. */
    int32 OwnerPlayerIndex = -1;
};
