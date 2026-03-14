// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Core/TDSharedTypes.h"
#include "TDUnitDataAsset.generated.h"

class ATDUnitBase;

/**
 * ETDUnitType - 军队单位类型。
 *
 * 定义兵种的大类，用于克制关系计算和 AI 行为分类。
 */
UENUM(BlueprintType)
enum class ETDUnitType : uint8
{
    None,
    Melee,      // 近战单位
    Ranged,     // 远程单位
    Cavalry,    // 骑兵
    Siege,      // 攻城器械
    Special,    // 特殊单位
};

/**
 * UTDUnitDataAsset - 单位静态数据资产。
 *
 * UDataAsset 子类，定义一个兵种的所有静态属性。
 * 策划可在编辑器中创建多个实例来配置不同兵种（如 Swordsman_Lv1、Archer_Lv2 等）。
 *
 * 属性分为三组：
 * - 基础属性：生命、攻击、移动、费用等
 * - 克制倍率：对不同兵种的伤害修正
 * - 外观配置：单位模型引用
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDUnitDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 标识
    // ---------------------------------------------------------------

    /** 唯一标识符（如 "Swordsman_Lv1"），用于数据索引和日志。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    FName UnitID;

    /** 在 UI 中显示的本地化名称。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    FText DisplayName;

    /** 单位所属类型，影响克制关系和 AI 行为选择。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    ETDUnitType UnitType = ETDUnitType::None;

    /** 解锁此单位所需的最低科技时代。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit")
    ETDTechEra RequiredEra = ETDTechEra::Ancient;

    // ---------------------------------------------------------------
    // 基础属性
    // ---------------------------------------------------------------

    /** 训练此单位需要消耗的金币。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "0"))
    int32 GoldCost = 20;

    /** 编队总生命值上限。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "1"))
    int32 MaxHealth = 50;

    /** 单次攻击的基础伤害值。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "0.0"))
    float AttackDamage = 10.0f;

    /** 攻击范围（六边形格子数），近战单位为 1。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "1.0"))
    float AttackRange = 1.0f;

    /** 两次攻击之间的最小间隔（秒）。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "0.1"))
    float AttackInterval = 1.5f;

    /** 移动速度（格子/秒概念），影响移动动画速率。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "0.1"))
    float MoveSpeed = 1.0f;

    /** 每回合可消耗的最大移动点数。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "0.0"))
    float MaxMovePoints = 3.0f;

    /** 编队中的个体人数，影响视觉表现。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "1"))
    int32 SquadSize = 5;

    /** 护甲值，减免受到的伤害。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Stats",
        meta = (ClampMin = "0.0"))
    float ArmorValue = 0.0f;

    // ---------------------------------------------------------------
    // 外观
    // ---------------------------------------------------------------

    /** 单位使用的静态模型，nullptr 时使用默认占位模型。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Visual")
    UStaticMesh* UnitMesh = nullptr;

    // ---------------------------------------------------------------
    // Actor 类映射
    // ---------------------------------------------------------------

    /**
     * 生成单位时使用的 Actor 蓝图类。
     * 设置后，ImportUnitData 将生成此蓝图 Actor
     * 而非纯 C++ 基类，从而保留蓝图上配置的默认属性。
     * 为空时回退到 ATDUnitBase::StaticClass()。
     */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Actor")
    TSubclassOf<ATDUnitBase> UnitActorClass;

    // ---------------------------------------------------------------
    // 克制关系
    // ---------------------------------------------------------------

    /** 攻击近战单位时的伤害倍率。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Counter",
        meta = (ClampMin = "0.0"))
    float VsMeleeMultiplier = 1.0f;

    /** 攻击远程单位时的伤害倍率。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Counter",
        meta = (ClampMin = "0.0"))
    float VsRangedMultiplier = 1.0f;

    /** 攻击骑兵时的伤害倍率。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Counter",
        meta = (ClampMin = "0.0"))
    float VsCavalryMultiplier = 1.0f;

    /** 攻击攻城器械时的伤害倍率。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Unit|Counter",
        meta = (ClampMin = "0.0"))
    float VsSiegeMultiplier = 1.0f;

    // ---------------------------------------------------------------
    // 查询接口
    // ---------------------------------------------------------------

    /**
     * 获取对指定兵种类型的伤害倍率。
     * None 和 Special 类型返回 1.0（无克制修正）。
     *
     * @param TargetType  目标单位的兵种类型。
     * @return            伤害倍率（>1 为克制，<1 为被克制）。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Counter")
    float GetDamageMultiplierVs(ETDUnitType TargetType) const;
};
