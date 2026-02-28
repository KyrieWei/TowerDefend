// Copyright TowerDefend. All Rights Reserved.

#include "TowerDefendGameMode.h"
#include "Core/TDCameraPawn.h"
#include "Core/TDPlayerController.h"

ATowerDefendGameMode::ATowerDefendGameMode()
{
    DefaultPawnClass = ATDCameraPawn::StaticClass();
    PlayerControllerClass = ATDPlayerController::StaticClass();
}
