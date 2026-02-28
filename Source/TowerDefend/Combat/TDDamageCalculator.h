// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TDDamageCalculator.generated.h"

class ATDUnitBase;
class ATDBuildingBase;
class ATDHexGridManager;
class ATDHexTile;

/**
 * UTDDamageCalculator - 伤害计算器。
 *
 * 纯计算类，不持有可变状态，不修改任何外部对象。
 * 负责所有战斗中的伤害数值计算，包括：
 * - 基础伤害 = 攻击者攻击力 x 兵种克制倍率
 * - 高度修正：攻击者在高处 +15%/级，目标在高处 -10%/级
 * - 地形防御加成：目标所在格子的 DefenseBonus 减伤
 * - 保底伤害：最终伤害不低于基础伤害 * MinDamageRatio
 */
UCLASS(Blueprintable, BlueprintType)
class UTDDamageCalculator : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 单位 vs 单位
    // ---------------------------------------------------------------

    /**
     * 计算单位对单位的最终伤害。
     * 综合基础攻击力、高度修正、地形防御、保底伤害。
     *
     * @param Attacker 攻击方单位。
     * @param Defender 防御方单位。
     * @param Grid     六边形网格管理器，用于查询高度和地形。
     * @return         最终整数伤害值（>= 1）。
     */
    int32 CalculateUnitDamage(
        const ATDUnitBase* Attacker,
        const ATDUnitBase* Defender,
        const ATDHexGridManager* Grid
    ) const;

    // ---------------------------------------------------------------
    // 建筑 vs 单位
    // ---------------------------------------------------------------

    /**
     * 计算建筑（防御塔）对单位的最终伤害。
     * 建筑攻击不受兵种克制影响，但受高度修正和地形防御影响。
     *
     * @param Building 攻击方建筑。
     * @param Target   被攻击的单位。
     * @param Grid     六边形网格管理器。
     * @return         最终整数伤害值（>= 1）。
     */
    int32 CalculateBuildingDamage(
        const ATDBuildingBase* Building,
        const ATDUnitBase* Target,
        const ATDHexGridManager* Grid
    ) const;

    // ---------------------------------------------------------------
    // 单位 vs 建筑
    // ---------------------------------------------------------------

    /**
     * 计算单位对建筑的最终伤害。
     * 建筑不享受地形防御加成，但高度修正仍然生效。
     *
     * @param Attacker 攻击方单位。
     * @param Target   被攻击的建筑。
     * @param Grid     六边形网格管理器。
     * @return         最终整数伤害值（>= 1）。
     */
    int32 CalculateUnitVsBuildingDamage(
        const ATDUnitBase* Attacker,
        const ATDBuildingBase* Target,
        const ATDHexGridManager* Grid
    ) const;

    // ---------------------------------------------------------------
    // 高度修正查询
    // ---------------------------------------------------------------

    /**
     * 获取高度差修正系数。
     * > 1.0 表示攻击加成（攻击者在高处）；
     * < 1.0 表示攻击减免（目标在高处）；
     * = 1.0 表示同高度，无修正。
     *
     * @param AttackerHeight 攻击者所在格子高度等级。
     * @param DefenderHeight 防御者所在格子高度等级。
     * @return               伤害倍率系数。
     */
    UFUNCTION(BlueprintPure, Category = "DamageCalculator")
    float GetHeightModifier(int32 AttackerHeight, int32 DefenderHeight) const;

protected:
    // ---------------------------------------------------------------
    // 可配置参数
    // ---------------------------------------------------------------

    /** 每级高度差的攻击加成比例（攻击者在高处时生效）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageCalculator",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HeightAttackBonusPerLevel = 0.15f;

    /** 每级高度差的防御减免比例（目标在高处时生效）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageCalculator",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float HeightDefensePenaltyPerLevel = 0.10f;

    /** 最低伤害比例，保证伤害不会低于基础伤害的此百分比。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DamageCalculator",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MinDamageRatio = 0.1f;

private:
    /**
     * 对基础伤害应用高度修正和地形防御减免。
     *
     * @param BaseDamage        基础伤害值。
     * @param AttackerTile      攻击者所在格子。
     * @param DefenderTile      防御者所在格子。
     * @param bApplyDefenseBonus 是否应用地形防御加成。
     * @return                  经过修正后的最终伤害值（>= 1）。
     */
    int32 ApplyModifiers(
        float BaseDamage,
        const ATDHexTile* AttackerTile,
        const ATDHexTile* DefenderTile,
        bool bApplyDefenseBonus
    ) const;

    /**
     * 应用保底伤害逻辑。
     * 确保最终伤害不低于 BaseDamage * MinDamageRatio，且至少为 1。
     *
     * @param CalculatedDamage 经过各种修正后的伤害值。
     * @param BaseDamage       原始基础伤害值。
     * @return                 最终伤害值（>= 1）。
     */
    int32 ApplyMinDamage(float CalculatedDamage, float BaseDamage) const;
};
