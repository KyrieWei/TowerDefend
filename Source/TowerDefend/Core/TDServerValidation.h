// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDNetworkTypes.h"
#include "TDServerValidation.generated.h"

class ATDPlayerState;
class ATDGameState;

/**
 * UTDServerValidation - 服务端操作验证器。
 *
 * 集中所有客户端请求的服务端验证逻辑。
 * 在执行操作前检查：
 * - 玩家是否存活
 * - 当前游戏阶段是否允许此操作
 * - 玩家资源是否足够
 * - 操作目标是否合法
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDServerValidation : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 验证玩家在当前阶段是否可以执行指定操作。
     *
     * @param Player      玩家状态。
     * @param GameState   游戏全局状态。
     * @param ActionType  操作类型。
     * @return            验证结果。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Network")
    FTDNetworkActionResult ValidateAction(
        const ATDPlayerState* Player,
        const ATDGameState* GameState,
        ETDNetworkAction ActionType) const;

    /**
     * 验证玩家是否存活。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Network")
    bool IsPlayerAlive(const ATDPlayerState* Player) const;

    /**
     * 验证当前阶段是否为准备阶段。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Network")
    bool IsPreparationPhase(const ATDGameState* GameState) const;
};
