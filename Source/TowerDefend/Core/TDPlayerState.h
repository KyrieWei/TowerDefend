// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "TDGamePhaseTypes.h"
#include "TDPlayerState.generated.h"

/** 血量变化委托：旧值、新值 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTDOnHealthChanged, int32, OldHealth, int32, NewHealth);

/** 金币变化委托：旧值、新值 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTDOnGoldChanged, int32, OldGold, int32, NewGold);

/** 玩家死亡委托：死亡的 PlayerState */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnPlayerDied, ATDPlayerState*, DeadPlayer);

/**
 * 玩家持久状态
 *
 * 存储单个玩家在整局游戏中的持久数据，包括血量、经济资源、
 * 科技进度和战绩统计。所有关键属性通过 Replication 同步到客户端。
 *
 * 设计约定：
 * - 资源修改接口仅在服务端调用，通过 Authority 守卫保护
 * - RepNotify 回调触发蓝图可绑定的多播委托
 * - 经济系统独立于其他模块，不依赖外部系统
 */
UCLASS()
class TOWERDEFEND_API ATDPlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    ATDPlayerState();

    // ─── 查询接口 ─────────────────────────────────────

    /** 是否能支付指定费用 */
    UFUNCTION(BlueprintPure, Category = "TD|Economy")
    bool CanAfford(int32 Cost) const;

    /** 玩家是否已死亡（血量 <= 0） */
    UFUNCTION(BlueprintPure, Category = "TD|Health")
    bool IsDead() const;

    // ─── 资源操作接口（仅服务端调用） ─────────────────

    /** 增加金币 */
    UFUNCTION(BlueprintCallable, Category = "TD|Economy")
    void AddGold(int32 Amount);

    /**
     * 消费金币
     * @return 是否扣除成功（余额不足时返回 false 且不扣除）
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Economy")
    bool SpendGold(int32 Amount);

    /** 增加科研点 */
    UFUNCTION(BlueprintCallable, Category = "TD|Economy")
    void AddResearchPoints(int32 Amount);

    /**
     * 消费科研点
     * @return 是否扣除成功（余额不足时返回 false 且不扣除）
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Economy")
    bool SpendResearchPoints(int32 Amount);

    // ─── 血量操作接口（仅服务端调用） ─────────────────

    /** 扣除血量，到 0 时触发死亡 */
    UFUNCTION(BlueprintCallable, Category = "TD|Health")
    void ApplyDamage(int32 Damage);

    /** 恢复血量，不超过最大值 */
    UFUNCTION(BlueprintCallable, Category = "TD|Health")
    void HealHealth(int32 Amount);

    // ─── 回合结算 ─────────────────────────────────────

    /**
     * 回合结算奖惩
     * @param bWon 本回合是否获胜
     * @param MatchConfig 当前对局配置（提供奖惩数值）
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Match")
    void ApplyRoundReward(bool bWon, const FTDMatchConfig& MatchConfig);

    /** 新对局重置所有数据到初始值 */
    UFUNCTION(BlueprintCallable, Category = "TD|Match")
    void ResetForNewMatch(const FTDMatchConfig& MatchConfig);

    // ─── 多播事件（蓝图可绑定） ─────────────────────

    /** 血量发生变化时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Events")
    FTDOnHealthChanged OnHealthChanged;

    /** 金币发生变化时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Events")
    FTDOnGoldChanged OnGoldChanged;

    /** 玩家死亡时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Events")
    FTDOnPlayerDied OnPlayerDied;

    // ─── Getter ───────────────────────────────────────

    UFUNCTION(BlueprintPure, Category = "TD|Health")
    int32 GetHealth() const { return Health; }

    UFUNCTION(BlueprintPure, Category = "TD|Health")
    int32 GetMaxHealth() const { return MaxHealth; }

    UFUNCTION(BlueprintPure, Category = "TD|Economy")
    int32 GetGold() const { return Gold; }

    UFUNCTION(BlueprintPure, Category = "TD|Economy")
    int32 GetResearchPoints() const { return ResearchPoints; }

    UFUNCTION(BlueprintPure, Category = "TD|Tech")
    int32 GetCurrentTechEra() const { return CurrentTechEra; }

    UFUNCTION(BlueprintPure, Category = "TD|Match")
    bool IsAlive() const { return bIsAlive; }

    UFUNCTION(BlueprintPure, Category = "TD|Match")
    int32 GetWinCount() const { return WinCount; }

    UFUNCTION(BlueprintPure, Category = "TD|Match")
    int32 GetLossCount() const { return LossCount; }

    UFUNCTION(BlueprintPure, Category = "TD|Match")
    int32 GetWinStreak() const { return WinStreak; }

    UFUNCTION(BlueprintPure, Category = "TD|Match")
    int32 GetLoseStreak() const { return LoseStreak; }

    /** 设置连胜数（仅服务端调用）。 */
    void SetWinStreak(int32 InStreak);

    /** 设置连败数（仅服务端调用）。 */
    void SetLoseStreak(int32 InStreak);

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ─── RepNotify 回调 ──────────────────────────────

    UFUNCTION()
    void OnRep_Health(int32 OldValue);

    UFUNCTION()
    void OnRep_Gold(int32 OldValue);

    UFUNCTION()
    void OnRep_bIsAlive();

private:
    /** 当前血量 */
    UPROPERTY(ReplicatedUsing = OnRep_Health)
    int32 Health;

    /** 最大血量 */
    UPROPERTY(Replicated)
    int32 MaxHealth;

    /** 持有金币 */
    UPROPERTY(ReplicatedUsing = OnRep_Gold)
    int32 Gold;

    /** 科研点数 */
    UPROPERTY(Replicated)
    int32 ResearchPoints;

    /** 当前科技时代（0=远古, 1=古典, ..., 5=现代） */
    UPROPERTY(Replicated)
    int32 CurrentTechEra;

    /** 是否存活 */
    UPROPERTY(ReplicatedUsing = OnRep_bIsAlive)
    bool bIsAlive;

    /** 胜利次数 */
    UPROPERTY(Replicated)
    int32 WinCount;

    /** 失败次数 */
    UPROPERTY(Replicated)
    int32 LossCount;

    /** 当前连胜数。 */
    UPROPERTY(Replicated)
    int32 WinStreak;

    /** 当前连败数。 */
    UPROPERTY(Replicated)
    int32 LoseStreak;

    /** 内部：设置血量并触发通知 */
    void SetHealth(int32 NewHealth);
};
