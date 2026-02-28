// Copyright TowerDefend. All Rights Reserved.

#include "Economy/TDResourceManager.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGamePhaseTypes.h"

// ===================================================================
// 收入计算
// ===================================================================

int32 UTDResourceManager::CalculateRoundIncome(
    ATDPlayerState* Player,
    const FTDMatchConfig& Config) const
{
    if (!Player)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDResourceManager::CalculateRoundIncome: Player is null."));
        return 0;
    }

    const int32 BaseIncome = Config.GoldPerRound;
    const int32 BuildingIncome = CalculateBuildingIncome(Player);

    return BaseIncome + BuildingIncome;
}

int32 UTDResourceManager::CalculateBuildingIncome(ATDPlayerState* Player) const
{
    if (!Player)
    {
        return 0;
    }

    // 预留接口：待 Building 模块完成后，遍历玩家的建筑并累加产出
    // 当前返回 0
    return 0;
}

int32 UTDResourceManager::CalculateResearchPointIncome(ATDPlayerState* Player) const
{
    if (!Player)
    {
        return 0;
    }

    // 基础产出，将来可叠加建筑/科技加成
    return BaseResearchPointsPerRound;
}

// ===================================================================
// 消耗验证
// ===================================================================

bool UTDResourceManager::CanAffordCost(
    ATDPlayerState* Player,
    int32 GoldCost,
    int32 ResearchCost) const
{
    if (!Player)
    {
        return false;
    }

    if (GoldCost > 0 && !Player->CanAfford(GoldCost))
    {
        return false;
    }

    if (ResearchCost > 0 && Player->GetResearchPoints() < ResearchCost)
    {
        return false;
    }

    return true;
}

// ===================================================================
// 资源发放
// ===================================================================

void UTDResourceManager::GrantRoundResources(
    ATDPlayerState* Player,
    const FTDMatchConfig& Config) const
{
    if (!Player)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDResourceManager::GrantRoundResources: Player is null."));
        return;
    }

    // 发放金币
    const int32 GoldIncome = CalculateRoundIncome(Player, Config);
    if (GoldIncome > 0)
    {
        Player->AddGold(GoldIncome);
    }

    // 发放科研点
    const int32 ResearchIncome = CalculateResearchPointIncome(Player);
    if (ResearchIncome > 0)
    {
        Player->AddResearchPoints(ResearchIncome);
    }

    UE_LOG(LogTemp, Log,
        TEXT("UTDResourceManager: Granted %d gold, %d research points to player."),
        GoldIncome, ResearchIncome);
}
