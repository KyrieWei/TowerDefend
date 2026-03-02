// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDServerValidation.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGameState.h"

FTDNetworkActionResult UTDServerValidation::ValidateAction(
    const ATDPlayerState* Player,
    const ATDGameState* GameState,
    ETDNetworkAction ActionType) const
{
    FTDNetworkActionResult Result;
    Result.ActionType = ActionType;

    if (!Player)
    {
        Result.bSuccess = false;
        Result.FailReason = TEXT("Player state is null.");
        return Result;
    }

    if (!IsPlayerAlive(Player))
    {
        Result.bSuccess = false;
        Result.FailReason = TEXT("Player is eliminated.");
        return Result;
    }

    if (!GameState)
    {
        Result.bSuccess = false;
        Result.FailReason = TEXT("Game state is null.");
        return Result;
    }

    switch (ActionType)
    {
    case ETDNetworkAction::PlaceBuilding:
    case ETDNetworkAction::TrainUnit:
    case ETDNetworkAction::ResearchTech:
    case ETDNetworkAction::ModifyTerrain:
        if (!IsPreparationPhase(GameState))
        {
            Result.bSuccess = false;
            Result.FailReason = TEXT("Action only allowed during Preparation phase.");
            return Result;
        }
        break;
    }

    Result.bSuccess = true;
    return Result;
}

bool UTDServerValidation::IsPlayerAlive(const ATDPlayerState* Player) const
{
    return Player && Player->IsAlive();
}

bool UTDServerValidation::IsPreparationPhase(const ATDGameState* GameState) const
{
    return GameState && GameState->GetCurrentPhase() == ETDGamePhase::Preparation;
}
