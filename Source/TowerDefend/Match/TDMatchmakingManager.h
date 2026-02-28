// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TDRoundManager.h"
#include "TDMatchmakingManager.generated.h"

class ATDPlayerState;

/**
 * 配对策略枚举
 * 决定每回合玩家之间的配对方式。
 */
UENUM(BlueprintType)
enum class ETDMatchmakingStrategy : uint8
{
    /** 完全随机配对 */
    Random      UMETA(DisplayName = "Random"),

    /** 瑞士轮 — 尽量避免重复对手，按胜场接近的人配对 */
    Swiss       UMETA(DisplayName = "Swiss"),

    /** 蛇形 — 按血量排序，1v2, 3v4... */
    Serpentine  UMETA(DisplayName = "Serpentine"),
};

/**
 * 配对算法管理器
 *
 * 负责每回合的玩家两两配对逻辑，支持三种策略：
 * - Random：完全随机打乱后依次配对
 * - Swiss：按胜场数排序，相近胜场的玩家优先配对，尽量避免重复对手
 * - Serpentine：按血量降序排序，1v2, 3v4...
 *
 * 统一处理奇数玩家的轮空逻辑：最后一名无对手的玩家生成一个 bIsValid=false 的占位配对。
 * 每对配对中随机决定攻守方。
 *
 * 由 ATDGameMode 在服务端创建并持有，不做网络同步。
 */
UCLASS()
class TOWERDEFEND_API UTDMatchmakingManager : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 根据当前策略生成配对
     * @param AlivePlayers 当前存活的玩家列表
     * @param MatchHistory 历史回合结果（Swiss 策略用于避免重复对手）
     * @return 本回合的配对列表
     */
    TArray<FTDRoundPairing> GeneratePairings(
        const TArray<ATDPlayerState*>& AlivePlayers,
        const TArray<FTDRoundResult>& MatchHistory
    ) const;

    /** 设置配对策略 */
    void SetStrategy(ETDMatchmakingStrategy NewStrategy);

    /** 获取当前配对策略 */
    ETDMatchmakingStrategy GetStrategy() const;

private:
    /** 当前使用的配对策略 */
    UPROPERTY()
    ETDMatchmakingStrategy Strategy = ETDMatchmakingStrategy::Swiss;

    /** 随机配对：打乱顺序后依次配对 */
    TArray<FTDRoundPairing> GenerateRandomPairings(
        const TArray<ATDPlayerState*>& Players
    ) const;

    /**
     * 瑞士轮配对：
     * 按胜场数排序，相邻玩家配对，尽量避免与历史中已交手的对手重复。
     */
    TArray<FTDRoundPairing> GenerateSwissPairings(
        const TArray<ATDPlayerState*>& Players,
        const TArray<FTDRoundResult>& History
    ) const;

    /** 蛇形配对：按血量降序排序，1v2, 3v4... */
    TArray<FTDRoundPairing> GenerateSerpentinePairings(
        const TArray<ATDPlayerState*>& Players
    ) const;

    /** 随机决定一对配对中的攻守方 */
    static void AssignAttackerDefender(FTDRoundPairing& Pairing);

    /**
     * 将玩家列表转为索引列表后按规则两两配对
     * 公共辅助：处理奇数玩家轮空，调用 AssignAttackerDefender
     */
    static TArray<FTDRoundPairing> BuildPairingsFromOrder(
        const TArray<int32>& OrderedIndices
    );

    /** 检查两名玩家在历史中是否已交手 */
    static bool HaveFoughtBefore(
        int32 IndexA,
        int32 IndexB,
        const TArray<FTDRoundResult>& History
    );
};
