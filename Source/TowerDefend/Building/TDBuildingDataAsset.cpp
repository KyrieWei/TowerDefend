// Copyright TowerDefend. All Rights Reserved.

#include "Building/TDBuildingDataAsset.h"

// ===================================================================
// 查询接口
// ===================================================================

int32 UTDBuildingDataAsset::GetUpgradeCost(int32 CurrentLevel) const
{
    // CurrentLevel 是 1-based，转为 0-based 索引
    const int32 Index = CurrentLevel - 1;

    if (Index < 0 || Index >= UpgradeCosts.Num())
    {
        return 0;
    }

    return UpgradeCosts[Index];
}

bool UTDBuildingDataAsset::CanBuildOnTerrain(
    ETDTerrainType InTerrainType, int32 InHeightLevel) const
{
    // 检查地形是否在禁止列表中
    if (ForbiddenTerrains.Contains(InTerrainType))
    {
        return false;
    }

    // 检查高度是否在允许范围内
    if (InHeightLevel < MinHeightLevel || InHeightLevel > MaxHeightLevel)
    {
        return false;
    }

    return true;
}

float UTDBuildingDataAsset::GetEffectiveAttackRange(int32 InHeightLevel) const
{
    // 基础范围
    float EffectiveRange = AttackRange;

    // 高度 > 0 时才获得加成
    if (InHeightLevel > 0)
    {
        EffectiveRange += HeightRangeBonus * static_cast<float>(InHeightLevel);
    }

    return EffectiveRange;
}
