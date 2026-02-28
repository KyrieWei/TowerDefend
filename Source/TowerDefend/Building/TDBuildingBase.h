// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexGrid/TDHexCoord.h"
#include "Building/TDBuildingDataAsset.h"
#include "TDBuildingBase.generated.h"

// ===================================================================
// 建筑事件委托
// ===================================================================

/** 建筑被销毁时广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FTDOnBuildingDestroyed, ATDBuildingBase*, DestroyedBuilding);

/** 建筑受到伤害时广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FTDOnBuildingDamaged, ATDBuildingBase*, DamagedBuilding,
    int32, DamageAmount, int32, RemainingHealth);

// ===================================================================
// ATDBuildingBase - 建筑基类
// ===================================================================

/**
 * ATDBuildingBase - 所有建筑的运行时实体基类。
 *
 * 持有建筑的运行时状态（血量、等级、坐标、归属），
 * 引用 UTDBuildingDataAsset 作为只读数据源。
 * 提供升级、受伤、销毁等通用逻辑，
 * 攻击相关接口声明为虚函数供子类覆盖。
 */
UCLASS()
class TOWERDEFEND_API ATDBuildingBase : public AActor
{
    GENERATED_BODY()

public:

    ATDBuildingBase();

    // ---------------------------------------------------------------
    // 初始化
    // ---------------------------------------------------------------

    /**
     * 初始化建筑（放置时由 BuildingManager 调用）。
     * 设置数据资产引用、格子坐标、归属玩家，
     * 并将血量初始化为最大值、更新 Mesh 外观。
     *
     * @param InData   建筑数据资产。
     * @param InCoord  所在六边形坐标。
     * @param InOwner  所属玩家索引。
     */
    void InitializeBuilding(
        UTDBuildingDataAsset* InData,
        const FTDHexCoord& InCoord,
        int32 InOwner);

    // ---------------------------------------------------------------
    // Getters
    // ---------------------------------------------------------------

    /** 获取所在格子坐标。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    FTDHexCoord GetCoord() const { return Coord; }

    /** 获取所属玩家索引。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    int32 GetOwnerPlayerIndex() const { return OwnerPlayerIndex; }

    /** 获取建筑类型。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    ETDBuildingType GetBuildingType() const;

    /** 获取当前等级。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    int32 GetCurrentLevel() const { return CurrentLevel; }

    /** 获取当前生命值。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    int32 GetCurrentHealth() const { return CurrentHealth; }

    /** 获取最大生命值。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    int32 GetMaxHealth() const;

    /** 获取引用的数据资产。 */
    UFUNCTION(BlueprintPure, Category = "Building")
    UTDBuildingDataAsset* GetBuildingData() const { return BuildingData; }

    // ---------------------------------------------------------------
    // 升级
    // ---------------------------------------------------------------

    /** 是否可以升级（未达最大等级且数据有效）。 */
    UFUNCTION(BlueprintPure, Category = "Building|Upgrade")
    bool CanUpgrade() const;

    /** 获取当前等级升级所需金币。无法升级时返回 0。 */
    UFUNCTION(BlueprintPure, Category = "Building|Upgrade")
    int32 GetUpgradeCost() const;

    /**
     * 执行升级。等级 +1。
     * 调用方负责事先检查 CanUpgrade() 并扣费。
     *
     * @return  升级是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "Building|Upgrade")
    bool Upgrade();

    // ---------------------------------------------------------------
    // 受伤与销毁
    // ---------------------------------------------------------------

    /**
     * 扣除指定伤害值。血量归零时广播销毁事件。
     *
     * @param Damage  伤害值，必须 > 0。
     */
    UFUNCTION(BlueprintCallable, Category = "Building|Combat")
    void ApplyDamage(int32 Damage);

    /** 生命值是否 <= 0。 */
    UFUNCTION(BlueprintPure, Category = "Building|Combat")
    bool IsDestroyed() const { return CurrentHealth <= 0; }

    /** 将血量恢复到最大值。 */
    UFUNCTION(BlueprintCallable, Category = "Building|Combat")
    void RepairToFull();

    // ---------------------------------------------------------------
    // 攻击（虚函数，子类可覆盖）
    // ---------------------------------------------------------------

    /** 是否有攻击能力。基类默认根据 AttackDamage > 0 判断。 */
    UFUNCTION(BlueprintPure, Category = "Building|Combat")
    virtual bool CanAttack() const;

    /** 获取实际攻击范围（含高度加成）。需外部传入高度或由子类自行处理。 */
    UFUNCTION(BlueprintPure, Category = "Building|Combat")
    virtual float GetAttackRange() const;

    /** 获取攻击伤害。 */
    UFUNCTION(BlueprintPure, Category = "Building|Combat")
    virtual float GetAttackDamage() const;

    /** 获取攻击间隔（秒）。 */
    UFUNCTION(BlueprintPure, Category = "Building|Combat")
    virtual float GetAttackInterval() const;

    // ---------------------------------------------------------------
    // 经济
    // ---------------------------------------------------------------

    /** 每回合金币产出。 */
    UFUNCTION(BlueprintPure, Category = "Building|Economy")
    int32 GetGoldPerRound() const;

    /** 每回合科研点产出。 */
    UFUNCTION(BlueprintPure, Category = "Building|Economy")
    int32 GetResearchPerRound() const;

    // ---------------------------------------------------------------
    // 事件委托
    // ---------------------------------------------------------------

    /** 建筑被销毁（血量归零）时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "Building|Events")
    FTDOnBuildingDestroyed OnBuildingDestroyed;

    /** 建筑受到伤害时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "Building|Events")
    FTDOnBuildingDamaged OnBuildingDamaged;

protected:

    // ---------------------------------------------------------------
    // 数据
    // ---------------------------------------------------------------

    /** 引用的建筑数据资产（只读配置源）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building")
    TObjectPtr<UTDBuildingDataAsset> BuildingData;

    /** 所在六边形格子坐标。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building")
    FTDHexCoord Coord;

    /** 当前生命值。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building")
    int32 CurrentHealth = 0;

    /** 当前等级（1-based）。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building")
    int32 CurrentLevel = 1;

    /** 所属玩家索引，-1 为中立。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building")
    int32 OwnerPlayerIndex = -1;

    // ---------------------------------------------------------------
    // 视觉组件
    // ---------------------------------------------------------------

    /** 建筑 Mesh 组件。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Building|Visual")
    TObjectPtr<UStaticMeshComponent> BuildingMeshComponent;

    // ---------------------------------------------------------------
    // 内部方法
    // ---------------------------------------------------------------

    /** 根据 BuildingData 更新 Mesh 外观。 */
    void UpdateVisualMesh();
};
