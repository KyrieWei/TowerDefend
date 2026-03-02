// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TDGamePhaseTypes.h"
#include "TDGameMode.generated.h"

class ATDGameState;
class ATDPlayerState;
class UTDMatchManager;
class UTDRoundManager;
class UTDResourceManager;
class UTDRewardCalculator;
class UTDMatchmakingManager;
class ATDHexGridManager;

/**
 * 对局游戏模式
 *
 * 管理完整对局的生命周期，驱动阶段（Phase）和回合（Round）流转。
 * 作为服务端权威控制器，负责：
 * - 对局的开始与结束
 * - 回合阶段的定时推进
 * - 阶段超时回调处理
 * - 初始化玩家资源和状态
 *
 * 阶段流程：
 *   None -> Preparation -> Matchmaking -> Battle -> Settlement
 *        -> Preparation（下一回合）或 GameOver
 *
 * 设计约定：
 * - 仅在 Dedicated Server / Listen Server 上执行逻辑
 * - 通过 TDGameState 同步状态到客户端
 * - 当前阶段不引用地形、相机等外部模块
 */
UCLASS()
class TOWERDEFEND_API ATDGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    ATDGameMode();

    // ─── 对局控制 ─────────────────────────────────────

    /** 开始一场新对局，初始化所有玩家并进入第一回合 */
    UFUNCTION(BlueprintCallable, Category = "TD|Match")
    void StartMatch();

    /** 结束对局，进入 GameOver 阶段 */
    UFUNCTION(BlueprintCallable, Category = "TD|Match")
    void EndMatch();

    // ─── 阶段控制 ─────────────────────────────────────

    /** 推进到下一个阶段 */
    UFUNCTION(BlueprintCallable, Category = "TD|Phase")
    void AdvanceToNextPhase();

    /** 开始新回合（回合数 +1，进入 Preparation） */
    UFUNCTION(BlueprintCallable, Category = "TD|Phase")
    void StartNewRound();

#if !UE_BUILD_SHIPPING
    /** 作弊专用：直接跳转到指定阶段 */
    void CheatSetPhase(ETDGamePhase NewPhase);
#endif

    // ─── 查询接口 ─────────────────────────────────────

    /** 获取当前阶段 */
    UFUNCTION(BlueprintPure, Category = "TD|Phase")
    ETDGamePhase GetCurrentPhase() const { return CurrentPhase; }

    /** 获取当前回合数 */
    UFUNCTION(BlueprintPure, Category = "TD|Phase")
    int32 GetCurrentRound() const { return CurrentRound; }

    /** 获取对局配置（可在编辑器中调整） */
    UFUNCTION(BlueprintPure, Category = "TD|Match")
    const FTDMatchConfig& GetMatchConfig() const { return MatchConfig; }

protected:
    virtual void BeginPlay() override;

    /** 玩家登录时的初始化处理 */
    virtual void PostLogin(APlayerController* NewPlayer) override;

    // ─── 阶段生命周期钩子（子类可覆写扩展） ─────────

    /** 进入准备阶段时调用 */
    virtual void OnPreparationPhaseStarted();

    /** 进入配对阶段时调用 */
    virtual void OnMatchmakingPhaseStarted();

    /** 进入战斗阶段时调用 */
    virtual void OnBattlePhaseStarted();

    /** 进入结算阶段时调用 */
    virtual void OnSettlementPhaseStarted();

    /** 进入 GameOver 阶段时调用 */
    virtual void OnGameOverPhaseStarted();

    /** 阶段时间耗尽回调 */
    UFUNCTION()
    void OnPhaseTimeExpired();

    /** 对局配置（编辑器可调） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TD|Config")
    FTDMatchConfig MatchConfig;

    /** 对局管理器 -- 追踪回合历史和连胜/连败 */
    UPROPERTY()
    UTDMatchManager* MatchManager = nullptr;

    /** 回合管理器 -- 配对和战斗执行 */
    UPROPERTY()
    UTDRoundManager* RoundManager = nullptr;

    /** 资源管理器 -- 计算和发放回合收入 */
    UPROPERTY()
    UTDResourceManager* ResourceManager = nullptr;

    /** 奖惩计算器 -- 计算胜负奖惩 */
    UPROPERTY()
    UTDRewardCalculator* RewardCalculator = nullptr;

    /** 配对管理器 -- 生成玩家配对 */
    UPROPERTY()
    UTDMatchmakingManager* MatchmakingManager = nullptr;

private:
    /** 创建并初始化所有 Manager 子系统 */
    void CreateManagers();

    /** 获取场景中的 HexGridManager */
    ATDHexGridManager* FindHexGridManager() const;

    /** 处理单场战斗结果，应用奖惩和淘汰 */
    void ProcessBattleResult(const FTDRoundResult& Result, ATDGameState* TDGameState);

    /** 通过玩家索引查找 PlayerState */
    ATDPlayerState* FindPlayerStateByIndex(int32 PlayerIndex) const;

    /** 收集所有已连接的玩家 PlayerState */
    TArray<ATDPlayerState*> GatherAllPlayerStates() const;

    /** 收集所有存活的玩家 PlayerState */
    TArray<ATDPlayerState*> GatherAlivePlayerStates() const;

    /** 设置当前阶段并同步到 GameState */
    void SetPhase(ETDGamePhase NewPhase);

    /** 启动阶段计时器 */
    void StartPhaseTimer(float Duration);

    /** 停止阶段计时器 */
    void StopPhaseTimer();

    /** 获取 TDGameState 的类型安全引用 */
    ATDGameState* GetTDGameState() const;

    /** 初始化单个玩家的起始数据 */
    void InitializePlayerForMatch(ATDPlayerState* Player);

    /** 检查是否应结束对局（仅剩一人或回合耗尽） */
    bool ShouldEndMatch() const;

    /** 当前阶段 */
    ETDGamePhase CurrentPhase;

    /** 当前回合数 */
    int32 CurrentRound;

    /** 阶段计时器句柄 */
    FTimerHandle PhaseTimerHandle;
};
