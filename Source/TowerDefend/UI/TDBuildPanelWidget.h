// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TDBuildPanelWidget.generated.h"

class UTDBuildingDataAsset;
class ATDPlayerState;

/** Building selection delegate: the selected building data asset. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnBuildingSelected, UTDBuildingDataAsset*, SelectedBuilding);

/**
 * UTDBuildPanelWidget - Building selection panel.
 *
 * Displays the list of available buildings and supports selection / deselection.
 * Subclass in Blueprint to implement the concrete list layout and button interactions.
 */
UCLASS(Abstract, Blueprintable)
class TOWERDEFEND_API UTDBuildPanelWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Set the list of buildings available for construction. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Build")
    void SetAvailableBuildings(const TArray<UTDBuildingDataAsset*>& InBuildings);

    /** Select a building for placement. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Build")
    void SelectBuilding(UTDBuildingDataAsset* Building);

    /** Clear the current selection. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Build")
    void ClearSelection();

    /** Get the currently selected building. */
    UFUNCTION(BlueprintPure, Category = "TD|UI|Build")
    UTDBuildingDataAsset* GetSelectedBuilding() const { return SelectedBuilding; }

    /** Broadcast when a building is selected. */
    UPROPERTY(BlueprintAssignable, Category = "TD|UI|Build|Events")
    FTDOnBuildingSelected OnBuildingSelected;

protected:
    /** Blueprint-implementable: called when the building list is updated. */
    UFUNCTION(BlueprintImplementableEvent, Category = "TD|UI|Build")
    void OnBuildingListUpdated();

    /** List of buildings available for construction. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Build")
    TArray<UTDBuildingDataAsset*> AvailableBuildings;

    /** Currently selected building. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Build")
    UTDBuildingDataAsset* SelectedBuilding = nullptr;
};
