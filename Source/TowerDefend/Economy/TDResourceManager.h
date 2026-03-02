// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGamePhaseTypes.h"
#include "TDResourceManager.generated.h"

class UTDBuildingManager;
class UTDTechTreeIntegration;

/**
 * UTDResourceManager - 资源管理器。
 *
 * 负责计算单个玩家的回合收入和消耗验证。
 * 不直接存储资源（资源存在 ATDPlayerState 中），
 * 本类是纯计算组件。
 *
 * 回合收入 = 基础金币 + 建筑产出 + 胜负奖励。
 * 建筑产出当前预留接口（返回 0），待 Building 模块完成后对接。
 */
UCLASS(Blueprintable, BlueprintType)
class UTDResourceManager : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 可配置参数
    // ---------------------------------------------------------------

    /** 每回合基础科研点产出。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Economy",
        meta = (ClampMin = "0"))
    int32 BaseResearchPointsPerRound = 10;

    // ---------------------------------------------------------------
    // 收入计算
    // ---------------------------------------------------------------

    /**
     * 计算玩家每回合的金币总收入。
     * 总收入 = 基础金币（Config.GoldPerRound） + 建筑产出。
     *
     * @param Player  玩家状态（只读）。
     * @param Config  当前对局配置。
     * @return        金币总收入。
     */
    UFUNCTION(BlueprintPure, Category = "Economy")
    int32 CalculateRoundIncome(
        ATDPlayerState* Player,
        const FTDMatchConfig& Config) const;

    /**
     * 计算建筑产出的金币。
     * 预留接口，当前返回 0。待 Building 模块完成后实现。
     *
     * @param Player  玩家状态（只读）。
     * @return        建筑金币产出。
     */
    UFUNCTION(BlueprintPure, Category = "Economy")
    int32 CalculateBuildingIncome(ATDPlayerState* Player) const;

    /**
     * 计算科研点每回合的产出。
     * 基础产出 + 建筑/科技加成（当前仅返回基础值）。
     *
     * @param Player  玩家状态（只读）。
     * @return        科研点产出。
     */
    UFUNCTION(BlueprintPure, Category = "Economy")
    int32 CalculateResearchPointIncome(ATDPlayerState* Player) const;

    // ---------------------------------------------------------------
    // 消耗验证
    // ---------------------------------------------------------------

    /**
     * 验证玩家是否能支付指定消耗。
     * 同时检查金币和科研点（可选）。
     *
     * @param Player        玩家状态（只读）。
     * @param GoldCost      金币消耗。
     * @param ResearchCost  科研点消耗（默认 0）。
     * @return              是否足够支付。
     */
    UFUNCTION(BlueprintPure, Category = "Economy")
    bool CanAffordCost(
        ATDPlayerState* Player,
        int32 GoldCost,
        int32 ResearchCost = 0) const;

    // ---------------------------------------------------------------
    // 资源发放
    // ---------------------------------------------------------------

    /**
     * 执行回合资源发放。在回合开始时由 GameMode 调用。
     * 发放金币和科研点到 ATDPlayerState。
     *
     * @param Player  玩家状态（可写）。
     * @param Config  当前对局配置。
     */
    UFUNCTION(BlueprintCallable, Category = "Economy")
    void GrantRoundResources(
        ATDPlayerState* Player,
        const FTDMatchConfig& Config) const;

    // ---------------------------------------------------------------
    // 外部系统引用
    // ---------------------------------------------------------------

    /** 建筑管理器引用，用于计算建筑产出。 */
    UPROPERTY()
    UTDBuildingManager* BuildingManagerRef = nullptr;

    /** 科技树集成引用，用于应用科技加成。 */
    UPROPERTY()
    UTDTechTreeIntegration* TechIntegration = nullptr;

    /** 设置建筑管理器引用。 */
    void SetBuildingManager(UTDBuildingManager* InBuildingManager);

    /** 设置科技树集成引用。 */
    void SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration);
};
