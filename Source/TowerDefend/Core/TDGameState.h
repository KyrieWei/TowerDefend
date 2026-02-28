// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "TDGamePhaseTypes.h"
#include "TDGameState.generated.h"

class ATDPlayerState;

/** 阶段变化委托：旧阶段、新阶段 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTDOnPhaseChanged, ETDGamePhase, OldPhase, ETDGamePhase, NewPhase);

/** 回合变化委托：新回合编号 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnRoundChanged, int32, NewRound);

/** 玩家被淘汰委托：被淘汰的 PlayerState */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnPlayerEliminated, ATDPlayerState*, EliminatedPlayer);

/**
 * 全局对局状态
 *
 * 存储需要网络同步的对局级数据，包括当前阶段、回合数、
 * 阶段倒计时和存活玩家列表。所有客户端可通过此类获取对局实时状态。
 *
 * 设计约定：
 * - 所有写操作仅在服务端（由 TDGameMode 驱动）执行
 * - 客户端通过 RepNotify 回调获取状态变化通知
 * - PhaseEndTime 使用服务器时间戳，客户端据此计算本地倒计时
 */
UCLASS()
class TOWERDEFEND_API ATDGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    ATDGameState();

    // ─── 查询接口 ─────────────────────────────────────

    /** 获取当前阶段剩余时间（秒），客户端基于服务器时间计算 */
    UFUNCTION(BlueprintPure, Category = "TD|Phase")
    float GetPhaseRemainingTime() const;

    /** 获取存活玩家数量 */
    UFUNCTION(BlueprintPure, Category = "TD|Match")
    int32 GetAlivePlayerCount() const;

    /** 指定玩家是否仍存活 */
    UFUNCTION(BlueprintPure, Category = "TD|Match")
    bool IsPlayerAlive(APlayerState* Player) const;

    /** 获取当前阶段 */
    UFUNCTION(BlueprintPure, Category = "TD|Phase")
    ETDGamePhase GetCurrentPhase() const { return CurrentPhase; }

    /** 获取当前回合数 */
    UFUNCTION(BlueprintPure, Category = "TD|Phase")
    int32 GetCurrentRound() const { return CurrentRound; }

    /** 获取最大回合数 */
    UFUNCTION(BlueprintPure, Category = "TD|Phase")
    int32 GetMaxRounds() const { return MaxRounds; }

    /** 获取对局配置 */
    UFUNCTION(BlueprintPure, Category = "TD|Match")
    const FTDMatchConfig& GetMatchConfig() const { return MatchConfig; }

    // ─── 写操作（仅服务端，由 GameMode 调用） ─────────

    /** 设置当前阶段 */
    void SetCurrentPhase(ETDGamePhase NewPhase);

    /** 设置当前回合数 */
    void SetCurrentRound(int32 NewRound);

    /** 设置最大回合数 */
    void SetMaxRounds(int32 InMaxRounds);

    /** 设置阶段结束的服务器时间戳 */
    void SetPhaseEndTime(float ServerTime);

    /** 设置对局配置 */
    void SetMatchConfig(const FTDMatchConfig& InConfig);

    /** 淘汰玩家 — 从存活列表移除并广播事件 */
    void EliminatePlayer(ATDPlayerState* Player);

    /** 将玩家加入存活列表 */
    void AddAlivePlayer(ATDPlayerState* Player);

    /** 清空存活玩家列表 */
    void ClearAlivePlayers();

    // ─── 多播事件（蓝图可绑定） ─────────────────────

    /** 阶段变化时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Events")
    FTDOnPhaseChanged OnPhaseChanged;

    /** 回合变化时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Events")
    FTDOnRoundChanged OnRoundChanged;

    /** 玩家被淘汰时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Events")
    FTDOnPlayerEliminated OnPlayerEliminated;

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    // ─── RepNotify 回调 ──────────────────────────────

    UFUNCTION()
    void OnRep_CurrentPhase(ETDGamePhase OldPhase);

    UFUNCTION()
    void OnRep_CurrentRound(int32 OldRound);

private:
    /** 当前游戏阶段 */
    UPROPERTY(ReplicatedUsing = OnRep_CurrentPhase)
    ETDGamePhase CurrentPhase;

    /** 当前回合编号（从 1 开始） */
    UPROPERTY(ReplicatedUsing = OnRep_CurrentRound)
    int32 CurrentRound;

    /** 最大回合数 */
    UPROPERTY(Replicated)
    int32 MaxRounds;

    /** 当前阶段结束的服务器时间戳（客户端据此计算倒计时） */
    UPROPERTY(Replicated)
    float PhaseEndTime;

    /** 存活玩家列表 */
    UPROPERTY(Replicated)
    TArray<TObjectPtr<ATDPlayerState>> AlivePlayers;

    /** 对局配置（Replicated 以便客户端读取） */
    UPROPERTY(Replicated)
    FTDMatchConfig MatchConfig;
};
