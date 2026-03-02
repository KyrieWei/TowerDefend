// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TDHUDWidget.generated.h"

class ATDPlayerState;
class ATDGameState;

/**
 * UTDHUDWidget - Main HUD widget.
 *
 * Displays the local player's core information:
 * - Gold and research point totals
 * - Current round number and max rounds
 * - Current phase name and countdown timer
 * - Player health bar
 *
 * Subclass in Blueprint and bind UI controls to implement the concrete layout.
 */
UCLASS(Abstract, Blueprintable)
class TOWERDEFEND_API UTDHUDWidget : public UUserWidget
{
    GENERATED_BODY()

public:
    /**
     * Bind player and game state references.
     * Called by TDPlayerHUDComponent after widget creation.
     */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void InitializeHUD(ATDPlayerState* InPlayerState, ATDGameState* InGameState);

protected:
    virtual void NativeConstruct() override;
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

    /** Blueprint-implementable update callback. */
    UFUNCTION(BlueprintImplementableEvent, Category = "TD|UI")
    void OnHUDUpdated();

    /** Currently bound player state. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI")
    ATDPlayerState* PlayerState = nullptr;

    /** Currently bound game state. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI")
    ATDGameState* GameState = nullptr;

    // --- Blueprint-readable cached values ---

    /** Current gold amount. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    int32 CachedGold = 0;

    /** Current research points. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    int32 CachedResearchPoints = 0;

    /** Current health. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    int32 CachedHealth = 0;

    /** Maximum health. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    int32 CachedMaxHealth = 0;

    /** Current round number. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    int32 CachedCurrentRound = 0;

    /** Phase remaining time in seconds. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    float CachedPhaseRemainingTime = 0.0f;

    /** Current phase display name. */
    UPROPERTY(BlueprintReadOnly, Category = "TD|UI|Data")
    FText CachedPhaseName;

private:
    /** Refresh cached values from the bound state objects. */
    void RefreshCachedValues();
};
