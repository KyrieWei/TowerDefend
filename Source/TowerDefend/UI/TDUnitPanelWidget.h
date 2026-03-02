// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TDUnitPanelWidget.generated.h"

class UTDUnitDataAsset;

/** Unit training request delegate. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTDOnTrainRequested, UTDUnitDataAsset*, UnitData, int32, Count);

/**
 * UTDUnitPanelWidget - Unit training panel.
 *
 * Displays available unit types and the current training queue.
 * Supports selecting a unit type and requesting training.
 */
UCLASS(Abstract, Blueprintable)
class TOWERDEFEND_API UTDUnitPanelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Set the list of units available for training. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Unit")
    void SetAvailableUnits(const TArray<UTDUnitDataAsset*>& InUnits);

    /** Request training of a unit type. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Unit")
    void RequestTraining(UTDUnitDataAsset* UnitData, int32 Count = 1);

    /** Broadcast when a training request is made. */
    UPROPERTY(BlueprintAssignable, Category = "TD|UI|Unit|Events")
    FTDOnTrainRequested OnTrainRequested;

protected:
    /** Blueprint-implementable: called when the unit list is updated. */
    UFUNCTION(BlueprintImplementableEvent, Category = "TD|UI|Unit")
    void OnUnitListUpdated();

    /** List of units available for training. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Unit")
    TArray<UTDUnitDataAsset*> AvailableUnits;
};
