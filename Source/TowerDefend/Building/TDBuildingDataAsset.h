// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDBuildingDataAsset.generated.h"

// ===================================================================
// ETDBuildingType - 建筑类型枚举
// ===================================================================

/**
 * 建筑类型分类。
 * 用于区分建筑的功能定位，影响放置规则和 UI 显示。
 */
UENUM(BlueprintType)
enum class ETDBuildingType : uint8
{
    /** 无类型（默认值）。 */
    None            UMETA(DisplayName = "None"),

    /** 主基地 — 玩家核心，被攻破当回合失败。 */
    Base            UMETA(DisplayName = "Base"),

    /** 箭塔 — 自动攻击范围内敌军，射程较远。 */
    ArrowTower      UMETA(DisplayName = "ArrowTower"),

    /** 炮塔 — 自动攻击范围内敌军，伤害较高。 */
    CannonTower     UMETA(DisplayName = "CannonTower"),

    /** 城墙 — 阻挡/减速敌军。 */
    Wall            UMETA(DisplayName = "Wall"),

    /** 资源建筑 — 每回合产出资源。 */
    ResourceBuilding UMETA(DisplayName = "ResourceBuilding"),

    /** 兵营 — 训练军队单位。 */
    Barracks        UMETA(DisplayName = "Barracks"),

    /** 陷阱 — 一次性或持续性地面效果。 */
    Trap            UMETA(DisplayName = "Trap"),
};

// ===================================================================
// UTDBuildingDataAsset - 建筑数据资产
// ===================================================================

/**
 * UTDBuildingDataAsset - 建筑的静态属性数据资产。
 *
 * 定义单种建筑的所有数值属性（费用、血量、攻击、经济产出等）。
 * 策划可在编辑器中创建不同的 DataAsset 实例来配置不同建筑。
 * 运行时由 ATDBuildingBase 引用，作为只读数据源。
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDBuildingDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:

    // ---------------------------------------------------------------
    // 基础标识
    // ---------------------------------------------------------------

    /** 建筑唯一标识（如 "ArrowTower_Lv1"），用于存档和网络同步。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Identity")
    FName BuildingID;

    /** 建筑显示名称（本地化友好）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Identity")
    FText DisplayName;

    /** 建筑功能类型。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Identity")
    ETDBuildingType BuildingType = ETDBuildingType::None;

    // ---------------------------------------------------------------
    // 经济属性
    // ---------------------------------------------------------------

    /** 建造所需金币。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Economy",
        meta = (ClampMin = "0"))
    int32 GoldCost = 50;

    /**
     * 每级升级费用数组。
     * 索引 0 = 从 1 级升到 2 级的费用，索引 1 = 从 2 级升到 3 级，以此类推。
     * 数组大小应等于 MaxLevel - 1。
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Economy")
    TArray<int32> UpgradeCosts;

    /** 每回合产出金币（资源建筑用，其他建筑为 0）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Economy",
        meta = (ClampMin = "0"))
    int32 GoldPerRound = 0;

    /** 每回合产出科研点。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Economy",
        meta = (ClampMin = "0"))
    int32 ResearchPerRound = 0;

    // ---------------------------------------------------------------
    // 战斗属性
    // ---------------------------------------------------------------

    /** 最大生命值。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Combat",
        meta = (ClampMin = "1"))
    int32 MaxHealth = 100;

    /** 最大等级。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Combat",
        meta = (ClampMin = "1"))
    int32 MaxLevel = 3;

    /** 攻击伤害（非攻击建筑为 0）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Combat",
        meta = (ClampMin = "0.0"))
    float AttackDamage = 0.0f;

    /** 攻击范围（六边形格子数，非攻击建筑为 0）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Combat",
        meta = (ClampMin = "0.0"))
    float AttackRange = 0.0f;

    /** 攻击间隔（秒）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Combat",
        meta = (ClampMin = "0.1"))
    float AttackInterval = 1.0f;

    /** 每级高度的射程加成（如高地箭塔每高一级 +1 射程）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Combat",
        meta = (ClampMin = "0.0"))
    float HeightRangeBonus = 0.0f;

    // ---------------------------------------------------------------
    // 放置限制
    // ---------------------------------------------------------------

    /** 解锁所需的最低科技时代（0 = 远古，无限制）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Placement",
        meta = (ClampMin = "0"))
    int32 MinTechEra = 0;

    /** 不可建造的地形类型列表。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Placement")
    TArray<ETDTerrainType> ForbiddenTerrains;

    /** 可建造的最低高度等级。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Placement",
        meta = (ClampMin = "1", ClampMax = "5"))
    int32 MinHeightLevel = 1;

    /** 可建造的最高高度等级。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Placement",
        meta = (ClampMin = "1", ClampMax = "5"))
    int32 MaxHeightLevel = 4;

    // ---------------------------------------------------------------
    // 视觉资源
    // ---------------------------------------------------------------

    /** 建筑静态模型。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Building|Visual")
    TObjectPtr<UStaticMesh> BuildingMesh;

    // ---------------------------------------------------------------
    // 查询接口
    // ---------------------------------------------------------------

    /**
     * 获取指定等级的升级费用。
     * CurrentLevel 为 1-based 当前等级：
     * 例如 CurrentLevel=1 对应从等级 1 升到等级 2 的费用（UpgradeCosts[0]）。
     *
     * @param CurrentLevel  当前等级（1-based），内部转为 0-based 索引。
     * @return              升级费用，超出范围返回 0。
     */
    int32 GetUpgradeCost(int32 CurrentLevel) const;

    /**
     * 检查是否可以在指定地形和高度上建造。
     *
     * @param InTerrainType  地形类型。
     * @param InHeightLevel  高度等级。
     * @return               是否允许建造。
     */
    bool CanBuildOnTerrain(ETDTerrainType InTerrainType, int32 InHeightLevel) const;

    /**
     * 获取在指定高度下的实际攻击范围（含高度加成）。
     * 高度 <= 0 时不获得加成。
     *
     * @param InHeightLevel  所在格子高度等级。
     * @return               实际攻击范围（格子数）。
     */
    float GetEffectiveAttackRange(int32 InHeightLevel) const;
};
