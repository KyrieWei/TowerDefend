// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/TDGamePhaseTypes.h"
#include "TDMatchResultWidget.generated.h"

/**
 * UTDMatchResultWidget - Round result / game over panel.
 *
 * Displays round combat results or the final game outcome.
 * Includes win/loss info, damage data, and reward/penalty details.
 */
UCLASS(Abstract, Blueprintable)
class TOWERDEFEND_API UTDMatchResultWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /** Display the result of a single round. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Result")
    void ShowRoundResult(const FTDRoundResult& Result, bool bIsLocalPlayerAttacker);

    /** Display the game over result. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI|Result")
    void ShowGameOverResult(bool bIsWinner, int32 FinalRound);

protected:
    /** Blueprint-implementable: called when a round result is displayed. */
    UFUNCTION(BlueprintImplementableEvent, Category = "TD|UI|Result")
    void OnRoundResultDisplayed();

    /** Blueprint-implementable: called when the game over result is displayed. */
    UFUNCTION(BlueprintImplementableEvent, Category = "TD|UI|Result")
    void OnGameOverDisplayed();

    /** Most recent round result. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Result")
    FTDRoundResult LastRoundResult;

    /** Whether the local player was the attacker. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Result")
    bool bLocalPlayerIsAttacker = false;

    /** Whether the local player is the final match winner. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Result")
    bool bIsMatchWinner = false;

    /** The final round number. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Result")
    int32 FinalRoundNumber = 0;
};
