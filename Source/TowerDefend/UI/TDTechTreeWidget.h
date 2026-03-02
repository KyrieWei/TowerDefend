// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TDTechTreeWidget.generated.h"

class UTDTechTreeManager;
class ATDPlayerState;

/** Technology research request delegate. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnResearchRequested, FName, TechID);

/**
 * UTDTechTreeWidget - Tech tree panel.
 *
 * Displays tech tree nodes and their research status.
 * Supports selecting a node to request research.
 */
UCLASS(Abstract, Blueprintable)
class TOWERDEFEND_API UTDTechTreeWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Bind the tech tree manager reference. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|TechTree")
    void SetTechTreeManager(UTDTechTreeManager* InManager);

    /** Request research on the specified tech node. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|TechTree")
    void RequestResearch(FName TechID);

    /** Refresh the UI display. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|TechTree")
    void RefreshDisplay();

    /** Broadcast when a research request is made. */
    UPROPERTY(BlueprintAssignable, Category = "TD|UI|TechTree|Events")
    FTDOnResearchRequested OnResearchRequested;

protected:
    /** Blueprint-implementable: called when the display is refreshed. */
    UFUNCTION(BlueprintImplementableEvent, Category = "TD|UI|TechTree")
    void OnDisplayRefreshed();

    /** Tech tree manager reference. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|TechTree")
    UTDTechTreeManager* TechTreeManager = nullptr;
};
