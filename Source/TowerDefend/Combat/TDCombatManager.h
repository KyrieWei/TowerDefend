// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDGamePhaseTypes.h"
#include "TDCombatManager.generated.h"

class ATDUnitBase;
class UTDUnitSquad;
class UTDUnitAIController;
class ATDBuildingBase;
class UTDBuildingManager;
class ATDHexGridManager;
class UTDDamageCalculator;

/**
 * 战斗状态枚举。
 * 驱动战斗流程的状态机流转。
 */
UENUM(BlueprintType)
enum class ETDCombatState : uint8
{
    /** 未开始。 */
    None            UMETA(DisplayName = "None"),

    /** 部署阶段 — 进攻方放置单位。 */
    Deploying       UMETA(DisplayName = "Deploying"),

    /** 战斗进行中 — 所有单位和建筑执行行动。 */
    InProgress      UMETA(DisplayName = "InProgress"),

    /** 战斗结束 — 胜负已判定。 */
    Finished        UMETA(DisplayName = "Finished"),
};

// ---------------------------------------------------------------
// 委托声明
// ---------------------------------------------------------------

/** 战斗状态变更委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnCombatStateChanged, ETDCombatState, NewState);

/** 战斗结束委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnCombatFinished, const FTDRoundResult&, Result);

/**
 * UTDCombatManager - 战斗流程管理器。
 *
 * 由 GameMode 持有，管理一场战斗的完整流程。
 * 运行在服务端，协调所有战斗逻辑：
 * - 管理进攻方单位的 AI 行为
 * - 管理防守方建筑的自动攻击
 * - 按回合制运行：每个战斗回合所有单位执行一次行动
 * - 胜负判定：攻方抵达/摧毁守方基地 = 攻方胜；
 *             攻方全灭或回合耗尽 = 守方胜
 * - 战斗结束后输出 FTDRoundResult
 */
UCLASS(Blueprintable, BlueprintType)
class UTDCombatManager : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 战斗流程控制
    // ---------------------------------------------------------------

    /**
     * 初始化一场战斗。
     * 设置攻守双方索引、关联网格管理器，创建内部子系统。
     * 调用后状态变为 Deploying。
     *
     * @param InAttackerIndex 进攻方玩家索引。
     * @param InDefenderIndex 防守方玩家索引。
     * @param Grid            六边形网格管理器。
     */
    UFUNCTION(BlueprintCallable, Category = "CombatManager")
    void InitializeCombat(
        int32 InAttackerIndex,
        int32 InDefenderIndex,
        ATDHexGridManager* Grid
    );

    /**
     * 执行一个战斗回合。
     * 所有存活单位和可攻击建筑各行动一次。
     * 回合结束后检查胜负条件。
     */
    UFUNCTION(BlueprintCallable, Category = "CombatManager")
    void ExecuteCombatTurn();

    /**
     * 执行完整战斗流程。
     * 循环调用 ExecuteCombatTurn 直到战斗结束。
     *
     * @return 战斗结果。
     */
    UFUNCTION(BlueprintCallable, Category = "CombatManager")
    FTDRoundResult ExecuteFullCombat();

    // ---------------------------------------------------------------
    // 状态查询
    // ---------------------------------------------------------------

    /** 获取当前战斗状态。 */
    UFUNCTION(BlueprintPure, Category = "CombatManager")
    ETDCombatState GetCombatState() const { return CurrentState; }

    /** 战斗是否已结束。 */
    UFUNCTION(BlueprintPure, Category = "CombatManager")
    bool IsCombatFinished() const { return CurrentState == ETDCombatState::Finished; }

    /** 获取当前战斗回合计数。 */
    UFUNCTION(BlueprintPure, Category = "CombatManager")
    int32 GetCombatTurnCount() const { return CombatTurnCount; }

    // ---------------------------------------------------------------
    // 结果获取
    // ---------------------------------------------------------------

    /**
     * 获取战斗结果。
     * 仅在战斗结束后（Finished 状态）调用有效。
     *
     * @return 本次战斗的回合结果数据。
     */
    UFUNCTION(BlueprintPure, Category = "CombatManager")
    FTDRoundResult GetCombatResult() const;

    // ---------------------------------------------------------------
    // 重置
    // ---------------------------------------------------------------

    /** 重置战斗管理器到初始状态，清除所有运行时数据。 */
    UFUNCTION(BlueprintCallable, Category = "CombatManager")
    void Reset();

    // ---------------------------------------------------------------
    // 外部注入
    // ---------------------------------------------------------------

    /** Set the building manager for building attack queries. */
    void SetBuildingManager(UTDBuildingManager* InBuildingManager);

    /** Set the unit squad for unit queries. */
    void SetUnitSquad(UTDUnitSquad* InSquad);

    // ---------------------------------------------------------------
    // 委托
    // ---------------------------------------------------------------

    /** 战斗状态变更时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "CombatManager|Events")
    FTDOnCombatStateChanged OnCombatStateChanged;

    /** 战斗结束时广播，携带战斗结果。 */
    UPROPERTY(BlueprintAssignable, Category = "CombatManager|Events")
    FTDOnCombatFinished OnCombatFinished;

protected:
    // ---------------------------------------------------------------
    // 战斗状态
    // ---------------------------------------------------------------

    /** 当前战斗状态。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CombatManager")
    ETDCombatState CurrentState = ETDCombatState::None;

    /** 进攻方玩家索引。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CombatManager")
    int32 AttackerPlayerIndex = INDEX_NONE;

    /** 防守方玩家索引。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CombatManager")
    int32 DefenderPlayerIndex = INDEX_NONE;

    /** 战斗内回合计数。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "CombatManager")
    int32 CombatTurnCount = 0;

    /** 最大战斗回合数，超过则防守方胜。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CombatManager",
        meta = (ClampMin = "1", ClampMax = "100"))
    int32 MaxCombatTurns = 30;

    // ---------------------------------------------------------------
    // 子系统引用
    // ---------------------------------------------------------------

    /** 伤害计算器实例。 */
    UPROPERTY()
    UTDDamageCalculator* DamageCalculator = nullptr;

    /** 单位 AI 控制器（前向引用，由 Unit 模块提供）。 */
    UPROPERTY()
    UTDUnitAIController* UnitAI = nullptr;

    /** 单位编队管理（前向引用，由 Unit 模块提供）。 */
    UPROPERTY()
    UTDUnitSquad* UnitSquad = nullptr;

    /** 关联的六边形网格管理器（弱引用，不可作为 UPROPERTY 反射）。 */
    TWeakObjectPtr<ATDHexGridManager> GridManager;

    /** 建筑管理器引用（由外部注入）。 */
    UPROPERTY()
    UTDBuildingManager* BuildingManager = nullptr;

    /** 缓存的战斗结果（战斗结束时填充）。 */
    FTDRoundResult CachedResult;

private:
    // ---------------------------------------------------------------
    // 内部逻辑
    // ---------------------------------------------------------------

    /**
     * 设置战斗状态并广播委托。
     *
     * @param NewState 新的战斗状态。
     */
    void SetCombatState(ETDCombatState NewState);

    /**
     * 执行单回合内所有战斗行动。
     * 包括建筑攻击、死亡清理、单位AI行动。
     *
     * @param Grid 六边形网格管理器。
     */
    void ExecuteTurnActions(ATDHexGridManager* Grid);

    /**
     * 检查并执行战斗结束流程。
     * 若满足结束条件，填充结果、切换状态、广播委托。
     */
    void TryFinishCombat();

    /**
     * 检查战斗是否应该结束。
     * 满足以下任一条件即结束：
     * - 攻方全灭
     * - 守方基地被摧毁
     * - 回合数耗尽
     *
     * @return 战斗是否应结束。
     */
    bool CheckCombatEnd() const;

    /**
     * 处理所有防御建筑的自动攻击。
     * 遍历防守方所有可攻击建筑，对范围内的攻方单位施加伤害。
     *
     * @param Grid 六边形网格管理器。
     */
    void ProcessBuildingAttacks(ATDHexGridManager* Grid);

    /**
     * 处理所有单位的 AI 行动。
     * 遍历攻方所有存活单位，通过 UnitAI 执行行动。
     *
     * @param Grid 六边形网格管理器。
     */
    void ProcessUnitActions(ATDHexGridManager* Grid);

    /**
     * 判定攻方是否获胜。
     * 条件：守方基地（主基地建筑）被摧毁。
     *
     * @return 攻方是否获胜。
     */
    bool IsAttackerVictory() const;

    /**
     * 判定守方是否获胜。
     * 条件：攻方单位全灭，或战斗回合耗尽。
     *
     * @return 守方是否获胜。
     */
    bool IsDefenderVictory() const;

    /**
     * 填充战斗结果数据。
     * 在战斗结束时调用。
     */
    void FillCombatResult();

    /**
     * 计算本次战斗造成的总伤害（用于结算时扣血）。
     *
     * @return 总伤害值。
     */
    int32 CalculateTotalDamage() const;
};
