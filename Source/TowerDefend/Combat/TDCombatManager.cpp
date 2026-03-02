// Copyright TowerDefend. All Rights Reserved.

#include "Combat/TDCombatManager.h"
#include "Combat/TDDamageCalculator.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDHexCoord.h"
#include "Building/TDBuildingManager.h"
#include "Building/TDBuildingBase.h"

#include "Unit/TDUnitBase.h"
#include "Unit/TDUnitSquad.h"
#include "Unit/TDUnitAIController.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDCombat, Log, All);

// ===================================================================
// 战斗流程控制
// ===================================================================

void UTDCombatManager::InitializeCombat(
    int32 InAttackerIndex,
    int32 InDefenderIndex,
    ATDHexGridManager* Grid)
{
    if (!Grid)
    {
        UE_LOG(LogTDCombat, Error,
            TEXT("UTDCombatManager::InitializeCombat: Grid is null."));
        return;
    }

    if (InAttackerIndex == InDefenderIndex)
    {
        UE_LOG(LogTDCombat, Error,
            TEXT("UTDCombatManager::InitializeCombat: "
                 "Attacker and Defender cannot be the same player (index=%d)."),
            InAttackerIndex);
        return;
    }

    Reset();

    AttackerPlayerIndex = InAttackerIndex;
    DefenderPlayerIndex = InDefenderIndex;
    GridManager = Grid;

    // 创建伤害计算器
    DamageCalculator = NewObject<UTDDamageCalculator>(this);

    UE_LOG(LogTDCombat, Log,
        TEXT("UTDCombatManager::InitializeCombat: "
             "Combat initialized. Attacker=%d, Defender=%d."),
        AttackerPlayerIndex, DefenderPlayerIndex);

    SetCombatState(ETDCombatState::Deploying);
}

void UTDCombatManager::ExecuteCombatTurn()
{
    // 如果当前为部署状态，自动推进到战斗中
    if (CurrentState == ETDCombatState::Deploying)
    {
        SetCombatState(ETDCombatState::InProgress);
    }

    if (CurrentState != ETDCombatState::InProgress)
    {
        UE_LOG(LogTDCombat, Warning,
            TEXT("UTDCombatManager::ExecuteCombatTurn: "
                 "Cannot execute turn in state %d."),
            static_cast<int32>(CurrentState));
        return;
    }

    ATDHexGridManager* Grid = GridManager.Get();

    if (!Grid)
    {
        UE_LOG(LogTDCombat, Error,
            TEXT("UTDCombatManager::ExecuteCombatTurn: Grid is no longer valid."));
        SetCombatState(ETDCombatState::Finished);
        return;
    }

    CombatTurnCount++;

    UE_LOG(LogTDCombat, Log,
        TEXT("UTDCombatManager: === Combat Turn %d ==="), CombatTurnCount);

    // 执行本回合的战斗逻辑
    ExecuteTurnActions(Grid);

    // 检查战斗结束条件
    TryFinishCombat();
}

FTDRoundResult UTDCombatManager::ExecuteFullCombat()
{
    // 如果尚未进入战斗状态，自动推进
    if (CurrentState == ETDCombatState::Deploying)
    {
        SetCombatState(ETDCombatState::InProgress);
    }

    if (CurrentState != ETDCombatState::InProgress)
    {
        UE_LOG(LogTDCombat, Warning,
            TEXT("UTDCombatManager::ExecuteFullCombat: "
                 "Cannot execute combat in state %d."),
            static_cast<int32>(CurrentState));
        return CachedResult;
    }

    // 重置所有单位的行动点
    if (UnitSquad)
    {
        UnitSquad->ResetAllMovePoints();
    }

    // 循环执行回合直到战斗结束
    while (CurrentState == ETDCombatState::InProgress)
    {
        ExecuteCombatTurn();
    }

    return CachedResult;
}

// ===================================================================
// 结果获取
// ===================================================================

FTDRoundResult UTDCombatManager::GetCombatResult() const
{
    if (CurrentState != ETDCombatState::Finished)
    {
        UE_LOG(LogTDCombat, Warning,
            TEXT("UTDCombatManager::GetCombatResult: "
                 "Combat not finished yet, returning partial result."));
    }

    return CachedResult;
}

// ===================================================================
// 重置
// ===================================================================

void UTDCombatManager::Reset()
{
    CurrentState = ETDCombatState::None;
    AttackerPlayerIndex = INDEX_NONE;
    DefenderPlayerIndex = INDEX_NONE;
    CombatTurnCount = 0;
    GridManager.Reset();
    DamageCalculator = nullptr;
    UnitAI = nullptr;
    UnitSquad = nullptr;
    BuildingManager = nullptr;
    CachedResult = FTDRoundResult();

    UE_LOG(LogTDCombat, Verbose,
        TEXT("UTDCombatManager::Reset: Combat manager reset."));
}

// ===================================================================
// 外部注入
// ===================================================================

void UTDCombatManager::SetBuildingManager(UTDBuildingManager* InBuildingManager)
{
    BuildingManager = InBuildingManager;
}

void UTDCombatManager::SetUnitSquad(UTDUnitSquad* InSquad)
{
    UnitSquad = InSquad;
}

// ===================================================================
// 内部逻辑
// ===================================================================

void UTDCombatManager::SetCombatState(ETDCombatState NewState)
{
    if (CurrentState == NewState)
    {
        return;
    }

    const ETDCombatState OldState = CurrentState;
    CurrentState = NewState;

    UE_LOG(LogTDCombat, Log,
        TEXT("UTDCombatManager: State changed %d -> %d."),
        static_cast<int32>(OldState), static_cast<int32>(NewState));

    OnCombatStateChanged.Broadcast(NewState);
}

void UTDCombatManager::ExecuteTurnActions(ATDHexGridManager* Grid)
{
    // 1. 防御建筑自动攻击
    ProcessBuildingAttacks(Grid);

    // 2. 清理被建筑击杀的单位
    if (UnitSquad)
    {
        UnitSquad->RemoveDeadUnits();
    }

    // 3. 攻方单位 AI 行动
    ProcessUnitActions(Grid);

    // 4. 清理被单位击杀的单位
    if (UnitSquad)
    {
        UnitSquad->RemoveDeadUnits();
    }
}

void UTDCombatManager::TryFinishCombat()
{
    if (!CheckCombatEnd())
    {
        return;
    }

    FillCombatResult();
    SetCombatState(ETDCombatState::Finished);
    OnCombatFinished.Broadcast(CachedResult);

    UE_LOG(LogTDCombat, Log,
        TEXT("UTDCombatManager: Combat ended at turn %d. AttackerWon=%d."),
        CombatTurnCount, CachedResult.bAttackerWon);
}

bool UTDCombatManager::CheckCombatEnd() const
{
    return IsAttackerVictory() || IsDefenderVictory();
}

void UTDCombatManager::ProcessBuildingAttacks(ATDHexGridManager* Grid)
{
    if (!Grid || !DamageCalculator || !UnitSquad || !BuildingManager)
    {
        return;
    }

    TArray<ATDBuildingBase*> DefenderBuildings =
        BuildingManager->GetBuildingsByOwner(DefenderPlayerIndex);

    for (ATDBuildingBase* Building : DefenderBuildings)
    {
        if (!IsValid(Building) || Building->IsDestroyed() || !Building->CanAttack())
        {
            continue;
        }

        FTDHexCoord BuildingCoord = Building->GetCoord();
        int32 AttackRange = FMath::CeilToInt32(Building->GetAttackRange());
        TArray<ATDUnitBase*> UnitsInRange =
            UnitSquad->GetUnitsInRange(BuildingCoord, AttackRange);

        ATDUnitBase* ClosestTarget = nullptr;
        int32 ClosestDist = INT_MAX;

        for (ATDUnitBase* Unit : UnitsInRange)
        {
            if (!IsValid(Unit) || Unit->IsDead()
                || Unit->GetOwnerPlayerIndex() != AttackerPlayerIndex)
            {
                continue;
            }

            int32 Dist = BuildingCoord.DistanceTo(Unit->GetCurrentCoord());
            if (Dist < ClosestDist)
            {
                ClosestDist = Dist;
                ClosestTarget = Unit;
            }
        }

        if (ClosestTarget)
        {
            int32 Damage = DamageCalculator->CalculateBuildingDamage(
                Building, ClosestTarget, Grid);
            ClosestTarget->ApplyDamage(Damage);

            UE_LOG(LogTDCombat, Verbose,
                TEXT("Building at %s dealt %d damage to unit at %s"),
                *BuildingCoord.ToString(), Damage,
                *ClosestTarget->GetCurrentCoord().ToString());
        }
    }
}

void UTDCombatManager::ProcessUnitActions(ATDHexGridManager* Grid)
{
    if (!Grid || !UnitSquad)
    {
        return;
    }

    TArray<ATDUnitBase*> AttackerUnits = UnitSquad->GetUnitsByOwner(AttackerPlayerIndex);

    if (AttackerUnits.Num() == 0)
    {
        UE_LOG(LogTDCombat, Verbose,
            TEXT("UTDCombatManager::ProcessUnitActions: No attacker units remaining."));
        return;
    }

    if (!UnitAI)
    {
        UE_LOG(LogTDCombat, Warning,
            TEXT("UTDCombatManager::ProcessUnitActions: UnitAI is null."));
        return;
    }

    for (ATDUnitBase* Unit : AttackerUnits)
    {
        if (Unit && !Unit->IsDead())
        {
            UnitAI->ExecuteTurn(Unit, Grid, nullptr, UnitSquad);
        }
    }
}

bool UTDCombatManager::IsAttackerVictory() const
{
    if (!BuildingManager)
    {
        return false;
    }

    TArray<ATDBuildingBase*> DefenderBuildings =
        BuildingManager->GetBuildingsByOwner(DefenderPlayerIndex);

    if (DefenderBuildings.Num() == 0)
    {
        UE_LOG(LogTDCombat, Log,
            TEXT("UTDCombatManager: Attacker victory - defender has no buildings."));
        return true;
    }

    bool bAllDestroyed = true;

    for (const ATDBuildingBase* Building : DefenderBuildings)
    {
        if (IsValid(Building) && !Building->IsDestroyed())
        {
            bAllDestroyed = false;
            break;
        }
    }

    if (bAllDestroyed)
    {
        UE_LOG(LogTDCombat, Log,
            TEXT("UTDCombatManager: Attacker victory - "
                 "all defender buildings destroyed."));
        return true;
    }

    return false;
}

bool UTDCombatManager::IsDefenderVictory() const
{
    // 条件1：攻方全灭
    if (UnitSquad)
    {
        const int32 AttackerCount = UnitSquad->GetUnitCountByOwner(AttackerPlayerIndex);

        if (AttackerCount <= 0)
        {
            UE_LOG(LogTDCombat, Log,
                TEXT("UTDCombatManager: Defender victory - all attacker units destroyed."));
            return true;
        }
    }

    // 条件2：回合数耗尽
    if (CombatTurnCount >= MaxCombatTurns)
    {
        UE_LOG(LogTDCombat, Log,
            TEXT("UTDCombatManager: Defender victory - max turns (%d) reached."),
            MaxCombatTurns);
        return true;
    }

    return false;
}

void UTDCombatManager::FillCombatResult()
{
    CachedResult.AttackerPlayerIndex = AttackerPlayerIndex;
    CachedResult.DefenderPlayerIndex = DefenderPlayerIndex;
    CachedResult.bAttackerWon = IsAttackerVictory();
    CachedResult.DamageDealt = CalculateTotalDamage();

    UE_LOG(LogTDCombat, Log,
        TEXT("UTDCombatManager: Result - AttackerWon=%d, DamageDealt=%d, Turns=%d."),
        CachedResult.bAttackerWon, CachedResult.DamageDealt, CombatTurnCount);
}

int32 UTDCombatManager::CalculateTotalDamage() const
{
    if (!UnitSquad)
    {
        return 0;
    }

    // 存活的攻方单位越多，对守方造成的伤害越大
    const int32 SurvivingAttackers = UnitSquad->GetUnitCountByOwner(AttackerPlayerIndex);

    return FMath::Max(0, SurvivingAttackers);
}
