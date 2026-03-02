// Copyright TowerDefend. All Rights Reserved.

#include "UI/TDMatchResultWidget.h"

void UTDMatchResultWidget::ShowRoundResult(
    const FTDRoundResult& Result, bool bIsLocalPlayerAttacker)
{
    LastRoundResult = Result;
    bLocalPlayerIsAttacker = bIsLocalPlayerAttacker;

    OnRoundResultDisplayed();
}

void UTDMatchResultWidget::ShowGameOverResult(bool bIsWinner, int32 FinalRound)
{
    bIsMatchWinner = bIsWinner;
    FinalRoundNumber = FinalRound;

    OnGameOverDisplayed();
}
