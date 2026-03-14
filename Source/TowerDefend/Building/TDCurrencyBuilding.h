// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/TDBuildingBase.h"
#include "TDCurrencyBuilding.generated.h"

class UTDBuildingManager;

/**
 * FTDCurrencyLevelData - 货币建筑单级别配置。
 *
 * 描述货币建筑在某一等级下的属性：
 * 显示名称、每回合金币产出、对应模型。
 * 例如 Lv1=市场, Lv2=银行, Lv3=证券交易所。
 */
USTRUCT(BlueprintType)
struct FTDCurrencyLevelData
{
    GENERATED_BODY()

    /** 该等级的显示名称（如 "Market"、"Bank"）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Currency")
    FText LevelDisplayName;

    /** 该等级的每回合金币产出。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Currency",
        meta = (ClampMin = "0"))
    int32 GoldPerRound = 0;

    /** 该等级的最大生命值。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Currency",
        meta = (ClampMin = "1"))
    int32 MaxHealth = 100;

    /** 该等级使用的静态模型。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Currency")
    TObjectPtr<UStaticMesh> LevelMesh;
};

/** 货币建筑升级时广播。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
    FTDOnCurrencyBuildingUpgraded,
    ATDBuildingBase*, UpgradedBuilding,
    int32, OldLevel,
    int32, NewLevel);

/**
 * ATDCurrencyBuilding - 货币类建筑。
 *
 * ATDBuildingBase 子类，实现经济产出型建筑。
 * 参考文明6的货币建筑升级路线：
 *   市场 (Market) → 银行 (Bank) → 证券交易所 (Stock Exchange)
 *
 * 核心特性：
 * - 每等级拥有独立的金币产出、生命值、模型
 * - 升级时自动切换外观和属性
 * - 不具备攻击能力
 * - 相邻货币建筑可提供协同加成（可配置）
 */
UCLASS()
class TOWERDEFEND_API ATDCurrencyBuilding : public ATDBuildingBase
{
    GENERATED_BODY()

public:

    ATDCurrencyBuilding();

    // ---------------------------------------------------------------
    // Override from ATDBuildingBase
    // ---------------------------------------------------------------

    /** 货币建筑不攻击，始终返回 false。 */
    virtual bool CanAttack() const override;

    /** 返回当前等级对应的金币产出（含协同加成）。 */
    virtual int32 GetGoldPerRound() const override;

    /** 执行升级，切换模型和属性。 */
    virtual bool Upgrade() override;

    // ---------------------------------------------------------------
    // 外部引用注入
    // ---------------------------------------------------------------

    /**
     * 设置建筑管理器引用（用于协同加成计算）。
     * 由 BuildingManager 在放置后调用。
     */
    void SetBuildingManager(UTDBuildingManager* InBuildingManager);

    /**
     * 初始化后应用等级数据。
     * 由 BuildingManager 在放置后调用，设置 Lv1 属性。
     */
    void ApplyLevelData();

    // ---------------------------------------------------------------
    // 查询
    // ---------------------------------------------------------------

    /** 获取当前等级的显示名称。 */
    UFUNCTION(BlueprintPure, Category = "Building|Currency")
    FText GetCurrentLevelDisplayName() const;

    /** 获取当前等级的基础金币产出（不含加成）。 */
    UFUNCTION(BlueprintPure, Category = "Building|Currency")
    int32 GetBaseGoldPerRound() const;

    /** 获取协同加成的额外金币产出。 */
    UFUNCTION(BlueprintPure, Category = "Building|Currency")
    int32 GetSynergyBonusGold() const;

    // ---------------------------------------------------------------
    // 事件委托
    // ---------------------------------------------------------------

    /** 货币建筑升级时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "Building|Currency|Events")
    FTDOnCurrencyBuildingUpgraded OnCurrencyBuildingUpgraded;

protected:

    // ---------------------------------------------------------------
    // 等级数据
    // ---------------------------------------------------------------

    /**
     * 每级别配置数组。
     * 索引 0 = Lv1（市场），索引 1 = Lv2（银行），索引 2 = Lv3（证券交易所）。
     * 数组大小应等于 BuildingData->MaxLevel。
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Currency")
    TArray<FTDCurrencyLevelData> LevelDataArray;

    /**
     * 相邻货币建筑的协同加成比例（百分比）。
     * 例如 10 表示每个相邻货币建筑增加 10% 金币产出。
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Currency",
        meta = (ClampMin = "0", ClampMax = "100"))
    int32 AdjacentSynergyBonusPercent = 10;

    // ---------------------------------------------------------------
    // 内部引用
    // ---------------------------------------------------------------

    /** 建筑管理器弱引用，用于查询相邻建筑。 */
    UPROPERTY()
    TWeakObjectPtr<UTDBuildingManager> BuildingManagerRef;

    // ---------------------------------------------------------------
    // 内部方法
    // ---------------------------------------------------------------

    /**
     * 根据当前等级更新模型和生命值。
     * 使用 LevelDataArray 中对应等级的数据。
     */
    void UpdateLevelVisualAndStats();

    /** 获取当前等级的数据（0-based 索引）。有效性已检查。 */
    const FTDCurrencyLevelData* GetCurrentLevelData() const;

    /**
     * 计算相邻同阵营货币建筑数量。
     * 使用 BuildingManagerRef 进行 O(1) 坐标查找。
     */
    int32 CountAdjacentCurrencyBuildings() const;
};
