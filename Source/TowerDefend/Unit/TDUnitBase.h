// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexGrid/TDHexCoord.h"
#include "Unit/TDUnitDataAsset.h"
#include "TDUnitBase.generated.h"

class ATDHexGridManager;
class ATDHexTile;
class UStaticMeshComponent;

/** 单位死亡委托：死亡的单位 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnUnitDied, ATDUnitBase*, DeadUnit);

/** 单位受伤委托：受伤单位、伤害量、剩余血量 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FTDOnUnitDamaged, ATDUnitBase*, DamagedUnit, int32, DamageAmount, int32, RemainingHealth);

/**
 * ATDUnitBase - 军队单位基类。
 *
 * 代表地图上一个编队（一个 Hex 格内一个编队）的 Actor。
 * 持有对静态数据资产 (UTDUnitDataAsset) 的引用和运行时状态
 * （当前血量、坐标、移动点数等）。
 *
 * 主要职责：
 * - 初始化与世界位置同步
 * - 移动点数管理与坐标更新
 * - 伤害计算（含克制倍率和地形高度加成）
 * - 死亡检测与委托广播
 *
 * 世界位置通过 FTDHexCoord::ToWorldPosition() 计算，
 * 高度偏移由所在格子的 HeightLevel 决定。
 */
UCLASS()
class TOWERDEFEND_API ATDUnitBase : public AActor
{
    GENERATED_BODY()

public:
    ATDUnitBase();

    // ---------------------------------------------------------------
    // 初始化
    // ---------------------------------------------------------------

    /**
     * 初始化单位数据、出生坐标和所属玩家。
     * 设置满血、满移动点，并将 Actor 移动到对应世界坐标。
     * 必须在 Spawn 后立即调用。
     *
     * @param InData      单位数据资产，不可为空。
     * @param SpawnCoord  出生六边形坐标。
     * @param InOwner     所属玩家索引。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit")
    void InitializeUnit(UTDUnitDataAsset* InData, const FTDHexCoord& SpawnCoord, int32 InOwner);

    // ---------------------------------------------------------------
    // 属性查询
    // ---------------------------------------------------------------

    /** 获取当前所在的六边形坐标。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    FTDHexCoord GetCurrentCoord() const { return CurrentCoord; }

    /** 获取所属玩家索引。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    int32 GetOwnerPlayerIndex() const { return OwnerPlayerIndex; }

    /** 获取单位的兵种类型。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    ETDUnitType GetUnitType() const;

    /** 获取当前生命值。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    int32 GetCurrentHealth() const { return CurrentHealth; }

    /** 获取最大生命值。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    int32 GetMaxHealth() const;

    /** 获取基础攻击伤害。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    float GetAttackDamage() const;

    /** 获取攻击范围（格子数）。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    float GetAttackRange() const;

    /** 获取关联的单位数据资产。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    UTDUnitDataAsset* GetUnitData() const { return UnitData; }

    /** 单位是否已死亡（血量 <= 0）。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit")
    bool IsDead() const { return CurrentHealth <= 0; }

    // ---------------------------------------------------------------
    // 移动
    // ---------------------------------------------------------------

    /**
     * 判断是否可以移动到目标坐标。
     * 检查条件：目标格子存在且可通行、剩余移动点足够支付移动消耗。
     *
     * @param DestCoord  目标六边形坐标。
     * @param Grid       当前地图管理器，用于查询格子属性。
     * @return           是否可以移动。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Movement")
    bool CanMoveTo(const FTDHexCoord& DestCoord, const ATDHexGridManager* Grid) const;

    /**
     * 执行移动：更新坐标、扣除移动点、同步世界位置。
     * 调用方应先通过 CanMoveTo 验证合法性。
     *
     * @param DestCoord  目标六边形坐标。
     * @param MoveCost   本次移动消耗的移动点数。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Movement")
    void MoveTo(const FTDHexCoord& DestCoord, float MoveCost);

    /** 回合开始时重置移动点数到最大值。 */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Movement")
    void ResetMovePoints();

    /** 获取当前回合剩余的移动点数。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Movement")
    float GetRemainingMovePoints() const { return RemainingMovePoints; }

    // ---------------------------------------------------------------
    // 战斗
    // ---------------------------------------------------------------

    /**
     * 对此单位施加伤害。
     * 血量降至 0 时触发 OnUnitDied 委托。
     *
     * @param Damage  伤害量，必须 >= 0。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Combat")
    void ApplyDamage(int32 Damage);

    /**
     * 判断目标坐标是否在攻击范围内。
     *
     * @param InTargetCoord  目标六边形坐标。
     * @return             是否在攻击范围内。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Combat")
    bool IsInAttackRange(const FTDHexCoord& InTargetCoord) const;

    /**
     * 计算对目标单位的最终伤害值。
     * 综合考虑：基础伤害 × 克制倍率 × 高度攻击加成 - 目标护甲。
     *
     * @param Target  目标单位，不可为空。
     * @param Grid    地图管理器，用于查询地形高度。
     * @return        最终伤害值（至少为 1）。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Combat")
    float CalculateDamageAgainst(const ATDUnitBase* Target, const ATDHexGridManager* Grid) const;

    // ---------------------------------------------------------------
    // 地形加成查询
    // ---------------------------------------------------------------

    /**
     * 获取高地攻击加成倍率。
     * 攻击方在高处时获得加成：每高一级 +10%。
     *
     * @param Grid  地图管理器。
     * @return      攻击加成倍率（1.0 为无加成）。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Terrain")
    float GetHeightAttackBonus(const ATDHexGridManager* Grid) const;

    /**
     * 获取低地防御惩罚倍率。
     * 在低处受到攻击时增加受伤：每低一级 +5%。
     *
     * @param Grid  地图管理器。
     * @return      防御惩罚倍率（1.0 为无惩罚，>1.0 为受伤增加）。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Terrain")
    float GetHeightDefenseBonus(const ATDHexGridManager* Grid) const;

    /**
     * 获取当前所在地形的防御加成。
     * 直接读取所在格子的 GetDefenseBonus()。
     *
     * @param Grid  地图管理器。
     * @return      防御加成比例（0.0 为无加成）。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Terrain")
    float GetTerrainDefenseBonus(const ATDHexGridManager* Grid) const;

    // ---------------------------------------------------------------
    // 委托
    // ---------------------------------------------------------------

    /** 单位死亡时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Unit|Events")
    FTDOnUnitDied OnUnitDied;

    /** 单位受伤时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Unit|Events")
    FTDOnUnitDamaged OnUnitDamaged;

protected:
    // ---------------------------------------------------------------
    // 核心数据
    // ---------------------------------------------------------------

    /** 引用的静态数据资产，定义此单位的兵种属性。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    UTDUnitDataAsset* UnitData = nullptr;

    /** 当前所在的六边形坐标。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    FTDHexCoord CurrentCoord;

    /** 移动目标坐标（用于动画插值，非逻辑必须）。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    FTDHexCoord TargetCoord;

    /** 编队当前总生命值。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    int32 CurrentHealth = 0;

    /** 所属玩家索引，-1 为未分配。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    int32 OwnerPlayerIndex = -1;

    /** 当前回合剩余移动点数。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit|Movement")
    float RemainingMovePoints = 0.0f;

    /** 单位 Mesh 组件，作为根组件。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "TD|Unit|Visual")
    UStaticMeshComponent* UnitMeshComponent = nullptr;

private:
    /**
     * 将 Actor 世界位置同步到 CurrentCoord 对应的世界坐标。
     * 需要外部提供 HexSize 参数（从 GridManager 获取）。
     *
     * @param HexSize       六边形外接圆半径。
     * @param HeightLevel   当前格子的高度等级。
     */
    void SyncWorldPosition(float HexSize, int32 HeightLevel);
};
