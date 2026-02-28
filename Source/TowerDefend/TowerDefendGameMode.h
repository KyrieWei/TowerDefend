// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TowerDefendGameMode.generated.h"

/**
 * ATowerDefendGameMode - 游戏模式基类。
 *
 * 当前阶段配置策略相机系统：
 *   DefaultPawnClass = ATDCameraPawn
 *   PlayerControllerClass = ATDPlayerController
 *
 * 后续由 ATDGameMode 替换。
 */
UCLASS(minimalapi)
class ATowerDefendGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:

    ATowerDefendGameMode();
};



