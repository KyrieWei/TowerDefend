// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDGamePhaseTypes.h"
#include "TDRewardCalculator.generated.h"

class UTDTechTreeIntegration;

// ===================================================================
// FTDRoundReward - 回合奖惩结果
// ===================================================================

/**
 * 单回合结算的奖惩数据。
 * 由 UTDRewardCalculator 计算，由 GameMode 应用到 PlayerState。
 */
USTRUCT(BlueprintType)
struct FTDRoundReward
{
    GENERATED_BODY()

    /** 金币变化（正数=获得，负数=扣除）。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoundReward")
    int32 GoldDelta = 0;

    /** 科研点变化。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoundReward")
    int32 ResearchPointDelta = 0;

    /** 血量变化（负数=扣血）。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoundReward")
    int32 HealthDelta = 0;

    /** 本回合是否胜利。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "RoundReward")
    bool bWon = false;
};

// ===================================================================
// UTDRewardCalculator - 回合奖惩计算器
// ===================================================================

/**
 * UTDRewardCalculator - 回合奖惩计算器。
 *
 * 纯计算类，不持有任何运行时状态，不直接修改任何对象。
 * 根据回合结果、对局配置和连胜/连败信息计算奖惩。
 *
 * 胜利奖励 = 基础金币 + 连胜加成。
 * 失败惩罚 = 扣除血量（随回合递增） + 连败补偿金币。
 */
UCLASS(Blueprintable, BlueprintType)
class UTDRewardCalculator : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 可配置参数
    // ---------------------------------------------------------------

    /** 每层连胜额外金币奖励。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RewardCalculator",
        meta = (ClampMin = "0"))
    int32 WinStreakBonusPerStack = 5;

    /** 连胜金币奖励上限。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RewardCalculator",
        meta = (ClampMin = "0"))
    int32 MaxWinStreakBonus = 30;

    /** 每层连败补偿金币。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RewardCalculator",
        meta = (ClampMin = "0"))
    int32 LoseStreakCompensationPerStack = 5;

    /** 连败补偿金币上限。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RewardCalculator",
        meta = (ClampMin = "0"))
    int32 MaxLoseStreakCompensation = 25;

    /** 每回合失败伤害递增量（叠加在基础 LoseDamage 上）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RewardCalculator",
        meta = (ClampMin = "0.0"))
    float LoseDamageEscalationPerRound = 0.5f;

    /** 胜利时获得的科研点奖励。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RewardCalculator",
        meta = (ClampMin = "0"))
    int32 WinResearchPointBonus = 5;

    // ---------------------------------------------------------------
    // 核心接口
    // ---------------------------------------------------------------

    /**
     * 计算回合奖惩结果。
     *
     * @param RoundResult   本回合战斗结果。
     * @param Config        对局配置。
     * @param CurrentRound  当前回合编号（从 1 开始）。
     * @param WinStreak     连胜场数（>= 0）。
     * @param LoseStreak    连败场数（>= 0）。
     * @return              奖惩结果数据。
     */
    UFUNCTION(BlueprintPure, Category = "RewardCalculator")
    FTDRoundReward CalculateRoundReward(
        const FTDRoundResult& RoundResult,
        const FTDMatchConfig& Config,
        int32 CurrentRound,
        int32 WinStreak,
        int32 LoseStreak) const;

    /**
     * 计算失败时的血量扣除值（含回合递增）。
     *
     * @param Config        对局配置。
     * @param CurrentRound  当前回合编号（从 1 开始）。
     * @return              应扣除的血量值（正数）。
     */
    UFUNCTION(BlueprintPure, Category = "RewardCalculator")
    int32 CalculateLoseDamage(
        const FTDMatchConfig& Config,
        int32 CurrentRound) const;

    // ---------------------------------------------------------------
    // 科技树集成
    // ---------------------------------------------------------------

    /** 科技树集成引用，用于应用科技奖励加成。 */
    UPROPERTY()
    UTDTechTreeIntegration* TechIntegration = nullptr;

    /** 设置科技树集成引用。 */
    void SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration);

    /** 计算回合奖惩（带科技加成）。 */
    UFUNCTION(BlueprintPure, Category = "RewardCalculator")
    FTDRoundReward CalculateRoundRewardWithTech(
        const FTDRoundResult& RoundResult,
        const FTDMatchConfig& Config,
        int32 CurrentRound,
        int32 WinStreak,
        int32 LoseStreak,
        int32 PlayerIndex) const;

private:
    /**
     * 计算连胜金币加成。
     * 连胜加成 = min(WinStreak * WinStreakBonusPerStack, MaxWinStreakBonus)。
     */
    int32 CalculateWinStreakBonus(int32 WinStreak) const;

    /**
     * 计算连败补偿金币。
     * 补偿 = min(LoseStreak * LoseStreakCompensationPerStack, MaxLoseStreakCompensation)。
     */
    int32 CalculateLoseStreakCompensation(int32 LoseStreak) const;
};
