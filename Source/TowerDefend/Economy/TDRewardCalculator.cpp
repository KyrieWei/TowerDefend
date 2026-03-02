// Copyright TowerDefend. All Rights Reserved.

#include "Economy/TDRewardCalculator.h"
#include "Core/TDGamePhaseTypes.h"
#include "TechTree/TDTechTreeIntegration.h"

// ===================================================================
// 核心接口
// ===================================================================

FTDRoundReward UTDRewardCalculator::CalculateRoundReward(
    const FTDRoundResult& RoundResult,
    const FTDMatchConfig& Config,
    int32 CurrentRound,
    int32 WinStreak,
    int32 LoseStreak) const
{
    FTDRoundReward Reward;
    Reward.bWon = RoundResult.bAttackerWon;

    if (Reward.bWon)
    {
        // 胜利奖励 = 基础胜利金币 + 连胜加成
        const int32 BaseWinGold = Config.WinBonusGold;
        const int32 StreakBonus = CalculateWinStreakBonus(WinStreak);

        Reward.GoldDelta = BaseWinGold + StreakBonus;
        Reward.ResearchPointDelta = WinResearchPointBonus;
        Reward.HealthDelta = 0;
    }
    else
    {
        // 失败惩罚 = 扣血 + 连败补偿金币
        const int32 Damage = CalculateLoseDamage(Config, CurrentRound);
        const int32 Compensation = CalculateLoseStreakCompensation(LoseStreak);

        Reward.GoldDelta = Compensation;
        Reward.ResearchPointDelta = 0;
        Reward.HealthDelta = -Damage;
    }

    return Reward;
}

int32 UTDRewardCalculator::CalculateLoseDamage(
    const FTDMatchConfig& Config,
    int32 CurrentRound) const
{
    // 基础伤害 + 回合递增
    // 递增公式：LoseDamage + floor(CurrentRound * EscalationPerRound)
    const float Escalation = static_cast<float>(FMath::Max(CurrentRound - 1, 0))
        * LoseDamageEscalationPerRound;

    const int32 TotalDamage = Config.LoseDamage + FMath::FloorToInt32(Escalation);

    return FMath::Max(TotalDamage, 0);
}

// ===================================================================
// 内部计算
// ===================================================================

int32 UTDRewardCalculator::CalculateWinStreakBonus(int32 WinStreak) const
{
    if (WinStreak <= 0)
    {
        return 0;
    }

    const int32 RawBonus = WinStreak * WinStreakBonusPerStack;
    return FMath::Min(RawBonus, MaxWinStreakBonus);
}

int32 UTDRewardCalculator::CalculateLoseStreakCompensation(int32 LoseStreak) const
{
    if (LoseStreak <= 0)
    {
        return 0;
    }

    const int32 RawCompensation = LoseStreak * LoseStreakCompensationPerStack;
    return FMath::Min(RawCompensation, MaxLoseStreakCompensation);
}

// ===================================================================
// 科技树集成
// ===================================================================

void UTDRewardCalculator::SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration)
{
    TechIntegration = InTechIntegration;
}

FTDRoundReward UTDRewardCalculator::CalculateRoundRewardWithTech(
    const FTDRoundResult& RoundResult,
    const FTDMatchConfig& Config,
    int32 CurrentRound,
    int32 WinStreak,
    int32 LoseStreak,
    int32 PlayerIndex) const
{
    // Start with base calculation
    FTDRoundReward Reward = CalculateRoundReward(
        RoundResult, Config, CurrentRound, WinStreak, LoseStreak);

    // Apply tech resource bonus to gold rewards
    if (TechIntegration && Reward.GoldDelta > 0)
    {
        const float ResourceBonus = TechIntegration->GetResourceBonusForPlayer(PlayerIndex);
        if (ResourceBonus > 0.0f)
        {
            const int32 BonusGold = FMath::FloorToInt32(
                static_cast<float>(Reward.GoldDelta) * ResourceBonus / 100.0f);
            Reward.GoldDelta += BonusGold;
        }
    }

    return Reward;
}
