// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/TDBuildingBase.h"
#include "TDDefenseTower.generated.h"

/**
 * ATDDefenseTower - 防御塔建筑。
 *
 * ATDBuildingBase 子类，实现自动攻击行为。
 * 在战斗阶段通过定时器周期性搜索范围内的敌方单位并攻击。
 *
 * 攻击范围受所在格子高度影响：
 *   实际射程 = 基础射程 + HeightRangeBonus * max(HeightLevel, 0)
 *
 * 当前 Unit 系统尚未完成，FindNearestTarget() 返回 nullptr，
 * 攻击框架已搭建完毕，待 Unit 模块对接后即可生效。
 */
UCLASS()
class TOWERDEFEND_API ATDDefenseTower : public ATDBuildingBase
{
    GENERATED_BODY()

public:

    ATDDefenseTower();

    // ---------------------------------------------------------------
    // Override from ATDBuildingBase
    // ---------------------------------------------------------------

    virtual bool CanAttack() const override;
    virtual float GetAttackRange() const override;

    // ---------------------------------------------------------------
    // 自动攻击控制
    // ---------------------------------------------------------------

    /** 启动自动攻击定时器（战斗阶段开始时调用）。 */
    UFUNCTION(BlueprintCallable, Category = "Building|DefenseTower")
    void StartAutoAttack();

    /** 停止自动攻击定时器（战斗阶段结束时调用）。 */
    UFUNCTION(BlueprintCallable, Category = "Building|DefenseTower")
    void StopAutoAttack();

    /** 是否正在自动攻击中。 */
    UFUNCTION(BlueprintPure, Category = "Building|DefenseTower")
    bool IsAutoAttacking() const { return bIsAutoAttacking; }

protected:

    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    // ---------------------------------------------------------------
    // 攻击逻辑
    // ---------------------------------------------------------------

    /** 定时器回调 — 搜索并攻击最近目标。 */
    UFUNCTION()
    void OnAttackTimerFired();

    /**
     * 搜索攻击范围内最近的敌方目标。
     * 当前 Unit 系统未完成，始终返回 nullptr。
     * 待 Unit 模块完成后接入实际搜索逻辑。
     *
     * @return  最近的敌方目标 Actor，无目标时返回 nullptr。
     */
    AActor* FindNearestTarget() const;

    /**
     * 对目标执行攻击。
     * 当前仅输出日志，待 Combat 系统对接后执行实际伤害。
     *
     * @param Target  攻击目标。
     */
    void AttackTarget(AActor* Target);

private:

    /** 攻击定时器句柄。 */
    FTimerHandle AttackTimerHandle;

    /** 是否正在自动攻击。 */
    bool bIsAutoAttacking = false;

    /**
     * 所在格子高度等级（缓存值）。
     * 初始化时由外部设置或从格子查询。
     * 用于计算含高度加成的实际射程。
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building|DefenseTower",
        meta = (AllowPrivateAccess = "true"))
    int32 CachedHeightLevel = 0;

public:

    /** 设置缓存的高度等级（由 BuildingManager 在放置时调用）。 */
    void SetCachedHeightLevel(int32 InHeightLevel);
};
