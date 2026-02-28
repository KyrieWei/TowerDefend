// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDGamePhaseTypes.h"
#include "TDMatchManager.generated.h"

class ATDPlayerState;

/**
 * 对局管理器
 *
 * 管理一场完整多回合淘汰赛的宏观流程数据，包括：
 * - 回合历史记录
 * - 每位玩家的连胜/连败追踪
 * - 对局结束条件判定
 * - 胜利者查询
 *
 * 由 ATDGameMode 在服务端创建并持有，不做网络同步。
 * 本类仅维护数据和判定逻辑，不直接操作 PlayerState，
 * 具体奖惩执行由 GameMode 根据本类提供的数据调用 PlayerState 接口。
 */
UCLASS()
class TOWERDEFEND_API UTDMatchManager : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 初始化对局
     * 清空历史数据，注册参赛玩家，缓存配置。
     * @param Players 本场对局的所有参赛玩家
     * @param Config 对局配置
     */
    void InitializeMatch(const TArray<ATDPlayerState*>& Players, const FTDMatchConfig& Config);

    /**
     * 记录一个回合结果
     * 更新回合历史、连胜/连败计数。
     * @param Result 回合结果数据
     */
    void RecordRoundResult(const FTDRoundResult& Result);

    /**
     * 检查对局是否应该结束
     * 条件：存活玩家 <= 1 或 回合数达到上限。
     */
    bool ShouldEndMatch() const;

    /**
     * 获取胜利者
     * 对局结束时调用。如果恰好一人存活则返回该玩家，否则返回 nullptr。
     */
    ATDPlayerState* GetWinner() const;

    /** 获取当前回合编号 */
    int32 GetCurrentRound() const;

    /** 获取全部回合历史 */
    const TArray<FTDRoundResult>& GetRoundHistory() const;

    /** 获取指定玩家的当前连胜数（0 表示无连胜） */
    int32 GetPlayerWinStreak(int32 PlayerIndex) const;

    /** 获取指定玩家的当前连败数（0 表示无连败） */
    int32 GetPlayerLoseStreak(int32 PlayerIndex) const;

    /** 获取存活玩家数 */
    int32 GetAlivePlayerCount() const;

    /** 推进回合计数（由 GameMode 在开始新回合时调用） */
    void AdvanceRound();

    /** 将指定玩家标记为淘汰 */
    void MarkPlayerEliminated(int32 PlayerIndex);

    /** 重置所有数据（准备新对局） */
    void Reset();

private:
    /** 更新连胜/连败记录 */
    void UpdateStreaks(int32 WinnerIndex, int32 LoserIndex);

    /** 所有回合结果记录 */
    TArray<FTDRoundResult> RoundHistory;

    /** 玩家索引 → 当前连胜数 */
    TMap<int32, int32> PlayerWinStreaks;

    /** 玩家索引 → 当前连败数 */
    TMap<int32, int32> PlayerLoseStreaks;

    /** 当前回合编号 */
    int32 CurrentRound = 0;

    /** 对局配置缓存 */
    FTDMatchConfig MatchConfig;

    /** 参赛玩家列表（保持原始顺序，用于按索引查找） */
    UPROPERTY()
    TArray<TObjectPtr<ATDPlayerState>> RegisteredPlayers;

    /** 存活状态位图：玩家索引 → 是否存活 */
    TMap<int32, bool> AliveStatus;
};
