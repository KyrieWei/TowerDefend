// Copyright TowerDefend. All Rights Reserved.

#include "Economy/TDResourceManager.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGamePhaseTypes.h"
#include "Building/TDBuildingManager.h"
#include "TechTree/TDTechTreeIntegration.h"

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
    int32 TotalIncome = BaseIncome + BuildingIncome;

    // Apply tech resource bonus
    if (TechIntegration)
    {
        const float ResourceBonus = TechIntegration->GetResourceBonusForPlayer(Player->GetPlayerId());
        if (ResourceBonus > 0.0f)
        {
            TotalIncome += FMath::FloorToInt32(static_cast<float>(TotalIncome) * ResourceBonus / 100.0f);
        }
    }

    return TotalIncome;
}

int32 UTDResourceManager::CalculateBuildingIncome(ATDPlayerState* Player) const
{
    if (!Player || !BuildingManagerRef)
    {
        return 0;
    }

    // Use player's PlayerId as the player index for building lookup
    const int32 PlayerIndex = Player->GetPlayerId();
    return BuildingManagerRef->CalculateTotalGoldIncome(PlayerIndex);
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
// 外部系统引用
// ===================================================================

void UTDResourceManager::SetBuildingManager(UTDBuildingManager* InBuildingManager)
{
    BuildingManagerRef = InBuildingManager;
}

void UTDResourceManager::SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration)
{
    TechIntegration = InTechIntegration;
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
