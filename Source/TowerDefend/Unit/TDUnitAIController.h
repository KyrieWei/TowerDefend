// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HexGrid/TDHexCoord.h"
#include "TDUnitAIController.generated.h"

class ATDUnitBase;
class ATDHexGridManager;
class UTDHexPathfinding;
class UTDUnitSquad;

/**
 * ETDAIActionResult - AI 单次决策的行为结果。
 *
 * 用于向调用方（TDCombatManager）报告本轮 AI 执行了什么操作，
 * 以便战斗管理器决定是否需要播放动画或推进流程。
 */
UENUM(BlueprintType)
enum class ETDAIActionResult : uint8
{
    None,       // 无操作
    Moved,      // 执行了移动
    Attacked,   // 执行了攻击
    Idle,       // 待命（无可行动作）
};

/**
 * UTDUnitAIController - 单位 AI 控制器。
 *
 * UObject 子类（非 AAIController，因为单位不使用 UE5 导航系统），
 * 负责单位在战斗阶段的自动行为决策。
 *
 * 决策优先级：
 * 1. 攻击范围内有敌人 → 攻击最近的敌人
 * 2. 无可攻击目标 → 向最近的敌人移动
 * 3. 无法移动也无法攻击 → 待命
 *
 * 每回合每单位执行一次完整的决策循环，
 * 由 TDCombatManager 在战斗阶段逐单位调用 ExecuteTurn()。
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDUnitAIController : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 核心决策
    // ---------------------------------------------------------------

    /**
     * 为指定单位执行一轮完整的 AI 决策。
     *
     * 决策流程：
     * 1. 检查是否有攻击范围内的敌方单位
     * 2. 如果有，对最近的敌人发起攻击
     * 3. 如果没有，寻找最近敌人并尝试向其移动
     * 4. 如果也无法移动，返回待命
     *
     * @param Unit         要执行决策的单位，不可为空。
     * @param Grid         地图管理器，用于地形查询。
     * @param Pathfinding  寻路系统，用于路径计算。可为空（此时跳过移动）。
     * @param AllUnits     所有单位的集合，用于敌我识别。
     * @return             本轮 AI 的行为结果。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|AI")
    ETDAIActionResult ExecuteTurn(
        ATDUnitBase* Unit,
        const ATDHexGridManager* Grid,
        const UTDHexPathfinding* Pathfinding,
        UTDUnitSquad* AllUnits
    );

    /**
     * 在攻击范围内寻找最近的敌方单位。
     *
     * @param Unit      当前单位。
     * @param AllUnits  所有单位集合。
     * @return          攻击范围内最近的敌方单位，没有时返回 nullptr。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|AI")
    ATDUnitBase* FindEnemyInRange(
        const ATDUnitBase* Unit,
        const UTDUnitSquad* AllUnits
    ) const;

    /**
     * 在全图范围内寻找最近的敌方单位。
     *
     * @param Unit      当前单位。
     * @param AllUnits  所有单位集合。
     * @return          最近的敌方单位，没有时返回 nullptr。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|AI")
    ATDUnitBase* FindNearestEnemy(
        const ATDUnitBase* Unit,
        const UTDUnitSquad* AllUnits
    ) const;

    /**
     * 计算向目标敌人方向移动的最佳目标坐标。
     * 在单位的移动点范围内，选择离敌人最近的可通行格子。
     *
     * @param Unit          当前单位。
     * @param TargetEnemy   目标敌人。
     * @param Grid          地图管理器。
     * @param Pathfinding   寻路系统。可为空（此时使用简单贪心策略）。
     * @return              最佳移动目标坐标，无法移动时返回 Invalid()。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|AI")
    FTDHexCoord FindMoveTarget(
        const ATDUnitBase* Unit,
        const ATDUnitBase* TargetEnemy,
        const ATDHexGridManager* Grid,
        const UTDHexPathfinding* Pathfinding
    ) const;

private:
    /**
     * 执行攻击行为：对目标造成伤害。
     *
     * @param Attacker  攻击方。
     * @param Target    被攻击方。
     * @param Grid      地图管理器，用于地形加成计算。
     */
    void PerformAttack(ATDUnitBase* Attacker, ATDUnitBase* Target, const ATDHexGridManager* Grid);

    /**
     * 执行移动行为：将单位移动到目标坐标。
     *
     * @param Unit      要移动的单位。
     * @param DestCoord 目标坐标。
     * @param Grid      地图管理器。
     * @param AllUnits  所有单位集合，用于更新映射。
     */
    void PerformMove(
        ATDUnitBase* Unit,
        const FTDHexCoord& DestCoord,
        const ATDHexGridManager* Grid,
        UTDUnitSquad* AllUnits
    );
};
