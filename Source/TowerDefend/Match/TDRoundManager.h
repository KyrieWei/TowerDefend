// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDGamePhaseTypes.h"
#include "TDRoundManager.generated.h"

class ATDPlayerState;
class UTDMatchmakingManager;
class UTDCombatManager;
class ATDHexGridManager;
class UTDBuildingManager;
class UTDUnitSquad;

/**
 * 回合配对数据
 * 描述一次攻防对战的双方玩家索引，用于配对阶段的结果传递。
 */
USTRUCT(BlueprintType)
struct FTDRoundPairing
{
    GENERATED_BODY()

    /** 攻方玩家索引 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 AttackerIndex = INDEX_NONE;

    /** 守方玩家索引 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    int32 DefenderIndex = INDEX_NONE;

    /** 配对是否有效（奇数玩家时一人轮空，该配对无效） */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
    bool bIsValid = false;
};

/**
 * 单回合管理器
 *
 * 管理一个回合内的完整攻防流程：配对生成、战斗执行、结果收集。
 * 由 ATDGameMode 在服务端创建并持有，不做网络同步。
 *
 * 战斗执行委托给 CombatManager，未注入时回退到随机模拟。
 *
 * 使用流程：
 *   1. InitializeRound() — 传入存活玩家和回合号，自动生成配对
 *   2. ExecuteBattles()  — 执行战斗（通过 CombatManager 或回退模拟）
 *   3. GetRoundResults() — 获取本回合所有战斗结果
 *   4. Reset()           — 清空状态，准备下一回合
 */
UCLASS()
class TOWERDEFEND_API UTDRoundManager : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 初始化本回合
     * 根据存活玩家列表生成配对，并缓存配对结果。
     * @param AlivePlayers 当前存活的玩家列表
     * @param RoundNumber 当前回合编号
     * @param MatchHistory 历史回合结果（供配对算法参考）
     */
    void InitializeRound(
        const TArray<ATDPlayerState*>& AlivePlayers,
        int32 RoundNumber,
        const TArray<FTDRoundResult>& MatchHistory
    );

    /** 获取本回合的配对结果 */
    const TArray<FTDRoundPairing>& GetCurrentPairings() const;

    /**
     * 执行战斗并生成结果
     * 当 CombatManager 和 GridManager 已注入时使用真实战斗逻辑，
     * 否则回退到随机模拟。
     */
    void ExecuteBattles();

    /** 获取本回合所有战斗结果 */
    const TArray<FTDRoundResult>& GetRoundResults() const;

    /** 重置状态，准备下一回合 */
    void Reset();

    /** 设置配对管理器（由外部注入，通常在对局初始化时设置） */
    void SetMatchmakingManager(UTDMatchmakingManager* InMatchmakingManager);

    /** Set the combat manager reference */
    void SetCombatManager(UTDCombatManager* InCombatManager);

    /** Set the hex grid manager reference */
    void SetGridManager(ATDHexGridManager* InGridManager);

    /** Set the building manager reference */
    void SetBuildingManager(UTDBuildingManager* InBuildingManager);

    /** Set the unit squad reference */
    void SetUnitSquad(UTDUnitSquad* InUnitSquad);

private:
    /** 本回合的配对列表 */
    TArray<FTDRoundPairing> CurrentPairings;

    /** 本回合的战斗结果 */
    TArray<FTDRoundResult> CurrentRoundResults;

    /** 当前回合编号 */
    int32 CurrentRoundNumber = 0;

    /** 本回合参与的存活玩家缓存（弱引用避免阻止 GC） */
    UPROPERTY()
    TArray<TObjectPtr<ATDPlayerState>> CachedAlivePlayers;

    /** 配对算法管理器引用（由外部注入） */
    UPROPERTY()
    TObjectPtr<UTDMatchmakingManager> MatchmakingManager;

    /** 战斗管理器引用（由外部注入） */
    UPROPERTY()
    TObjectPtr<UTDCombatManager> CombatManager;

    /** 六边形网格管理器引用（由外部注入） */
    TWeakObjectPtr<ATDHexGridManager> GridManager;

    /** 建筑管理器引用（由外部注入） */
    UPROPERTY()
    TObjectPtr<UTDBuildingManager> BuildingManager;

    /** 单位编队管理器引用（由外部注入） */
    UPROPERTY()
    TObjectPtr<UTDUnitSquad> UnitSquad;
};
