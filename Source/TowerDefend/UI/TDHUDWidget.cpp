// Copyright TowerDefend. All Rights Reserved.

#include "UI/TDHUDWidget.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGameState.h"

void UTDHUDWidget::InitializeHUD(ATDPlayerState* InPlayerState, ATDGameState* InGameState)
{
    PlayerState = InPlayerState;
    GameState = InGameState;

    RefreshCachedValues();
}

void UTDHUDWidget::NativeConstruct()
{
    Super::NativeConstruct();
}

void UTDHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
    Super::NativeTick(MyGeometry, InDeltaTime);

    RefreshCachedValues();
    OnHUDUpdated();
}

void UTDHUDWidget::RefreshCachedValues()
{
    if (PlayerState)
    {
        CachedGold = PlayerState->GetGold();
        CachedResearchPoints = PlayerState->GetResearchPoints();
        CachedHealth = PlayerState->GetHealth();
        CachedMaxHealth = PlayerState->GetMaxHealth();
    }

    if (GameState)
    {
        CachedCurrentRound = GameState->GetCurrentRound();
        CachedPhaseRemainingTime = GameState->GetPhaseRemainingTime();

        switch (GameState->GetCurrentPhase())
        {
        case ETDGamePhase::None:        CachedPhaseName = FText::FromString(TEXT("Waiting")); break;
        case ETDGamePhase::Preparation: CachedPhaseName = FText::FromString(TEXT("Preparation")); break;
        case ETDGamePhase::Matchmaking: CachedPhaseName = FText::FromString(TEXT("Matchmaking")); break;
        case ETDGamePhase::Battle:      CachedPhaseName = FText::FromString(TEXT("Battle")); break;
        case ETDGamePhase::Settlement:  CachedPhaseName = FText::FromString(TEXT("Settlement")); break;
        case ETDGamePhase::GameOver:    CachedPhaseName = FText::FromString(TEXT("Game Over")); break;
        }
    }
}
