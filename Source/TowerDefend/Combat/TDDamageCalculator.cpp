// Copyright TowerDefend. All Rights Reserved.

#include "Combat/TDDamageCalculator.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDHexCoord.h"

// Unit/Building 模块并行开发中，使用前向声明的接口
// 编译时需要完整类型定义，此处 include 占位
// TODO: 当 Unit/Building 模块就绪后取消注释
// #include "Unit/TDUnitBase.h"
// #include "Building/TDBuildingBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDDamage, Log, All);

// ===================================================================
// 单位 vs 单位
// ===================================================================

int32 UTDDamageCalculator::CalculateUnitDamage(
    const ATDUnitBase* Attacker,
    const ATDUnitBase* Defender,
    const ATDHexGridManager* Grid) const
{
    if (!Attacker || !Defender || !Grid)
    {
        UE_LOG(LogTDDamage, Warning,
            TEXT("UTDDamageCalculator::CalculateUnitDamage: Null parameter."));
        return 0;
    }

    // 基础伤害 = 攻击者的 CalculateDamageAgainst（含兵种克制）
    const float BaseDamage = Attacker->CalculateDamageAgainst(Defender);

    if (BaseDamage <= 0.0f)
    {
        return 0;
    }

    // 查询双方所在格子
    const ATDHexTile* AttackerTile = Grid->GetTileAt(Attacker->GetCurrentCoord());
    const ATDHexTile* DefenderTile = Grid->GetTileAt(Defender->GetCurrentCoord());

    return ApplyModifiers(BaseDamage, AttackerTile, DefenderTile, true);
}

// ===================================================================
// 建筑 vs 单位
// ===================================================================

int32 UTDDamageCalculator::CalculateBuildingDamage(
    const ATDBuildingBase* Building,
    const ATDUnitBase* Target,
    const ATDHexGridManager* Grid) const
{
    if (!Building || !Target || !Grid)
    {
        UE_LOG(LogTDDamage, Warning,
            TEXT("UTDDamageCalculator::CalculateBuildingDamage: Null parameter."));
        return 0;
    }

    const float BaseDamage = Building->GetAttackDamage();

    if (BaseDamage <= 0.0f)
    {
        return 0;
    }

    const ATDHexTile* BuildingTile = Grid->GetTileAt(Building->GetCoord());
    const ATDHexTile* TargetTile = Grid->GetTileAt(Target->GetCurrentCoord());

    return ApplyModifiers(BaseDamage, BuildingTile, TargetTile, true);
}

// ===================================================================
// 单位 vs 建筑
// ===================================================================

int32 UTDDamageCalculator::CalculateUnitVsBuildingDamage(
    const ATDUnitBase* Attacker,
    const ATDBuildingBase* Target,
    const ATDHexGridManager* Grid) const
{
    if (!Attacker || !Target || !Grid)
    {
        UE_LOG(LogTDDamage, Warning,
            TEXT("UTDDamageCalculator::CalculateUnitVsBuildingDamage: Null parameter."));
        return 0;
    }

    // 单位对建筑使用基础攻击力（无兵种克制）
    const float BaseDamage = Attacker->GetAttackDamage();

    if (BaseDamage <= 0.0f)
    {
        return 0;
    }

    const ATDHexTile* AttackerTile = Grid->GetTileAt(Attacker->GetCurrentCoord());
    const ATDHexTile* TargetTile = Grid->GetTileAt(Target->GetCoord());

    // 建筑不享受地形防御加成
    return ApplyModifiers(BaseDamage, AttackerTile, TargetTile, false);
}

// ===================================================================
// 高度修正
// ===================================================================

float UTDDamageCalculator::GetHeightModifier(int32 AttackerHeight, int32 DefenderHeight) const
{
    const int32 HeightDiff = AttackerHeight - DefenderHeight;

    if (HeightDiff > 0)
    {
        // 攻击者在高处：每级高度差 +15% 攻击加成
        return 1.0f + HeightAttackBonusPerLevel * static_cast<float>(HeightDiff);
    }
    else if (HeightDiff < 0)
    {
        // 目标在高处：每级高度差 -10% 攻击减免
        return FMath::Max(0.1f, 1.0f - HeightDefensePenaltyPerLevel * static_cast<float>(-HeightDiff));
    }

    // 同高度无修正
    return 1.0f;
}

// ===================================================================
// 内部实现
// ===================================================================

int32 UTDDamageCalculator::ApplyModifiers(
    float BaseDamage,
    const ATDHexTile* AttackerTile,
    const ATDHexTile* DefenderTile,
    bool bApplyDefenseBonus) const
{
    // 格子不存在时跳过地形修正，仅应用保底伤害
    if (!AttackerTile || !DefenderTile)
    {
        UE_LOG(LogTDDamage, Warning,
            TEXT("UTDDamageCalculator::ApplyModifiers: Tile not found, skipping terrain modifiers."));
        return ApplyMinDamage(BaseDamage, BaseDamage);
    }

    // 高度修正
    const float HeightMod = GetHeightModifier(
        AttackerTile->GetHeightLevel(),
        DefenderTile->GetHeightLevel());

    float ModifiedDamage = BaseDamage * HeightMod;

    // 地形防御减免
    if (bApplyDefenseBonus)
    {
        const float DefenseBonus = DefenderTile->GetDefenseBonus();
        ModifiedDamage *= (1.0f - FMath::Clamp(DefenseBonus, 0.0f, 1.0f));
    }

    return ApplyMinDamage(ModifiedDamage, BaseDamage);
}

int32 UTDDamageCalculator::ApplyMinDamage(float CalculatedDamage, float BaseDamage) const
{
    // 保底伤害 = 基础伤害 * 最低比例
    const float MinDamage = BaseDamage * MinDamageRatio;
    const float FinalDamage = FMath::Max(CalculatedDamage, MinDamage);

    // 至少造成 1 点伤害
    return FMath::Max(1, FMath::RoundToInt32(FinalDamage));
}
