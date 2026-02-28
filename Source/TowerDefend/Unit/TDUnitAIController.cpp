// Copyright TowerDefend. All Rights Reserved.

#include "Unit/TDUnitAIController.h"
#include "Unit/TDUnitBase.h"
#include "Unit/TDUnitSquad.h"
#include "Unit/TDUnitDataAsset.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"

// ===================================================================
// 核心决策
// ===================================================================

ETDAIActionResult UTDUnitAIController::ExecuteTurn(
    ATDUnitBase* Unit,
    const ATDHexGridManager* Grid,
    const UTDHexPathfinding* Pathfinding,
    UTDUnitSquad* AllUnits)
{
    if (!ensure(Unit != nullptr) || !ensure(Grid != nullptr) || !ensure(AllUnits != nullptr))
    {
        UE_LOG(LogTemp, Error, TEXT("UTDUnitAIController::ExecuteTurn - Invalid parameters!"));
        return ETDAIActionResult::None;
    }

    if (Unit->IsDead())
    {
        return ETDAIActionResult::None;
    }

    // 优先级 1：攻击范围内有敌人 → 攻击
    ATDUnitBase* EnemyInRange = FindEnemyInRange(Unit, AllUnits);
    if (EnemyInRange)
    {
        PerformAttack(Unit, EnemyInRange, Grid);
        return ETDAIActionResult::Attacked;
    }

    // 优先级 2：无攻击目标 → 向最近敌人移动
    ATDUnitBase* NearestEnemy = FindNearestEnemy(Unit, AllUnits);
    if (NearestEnemy && Unit->GetRemainingMovePoints() > 0.0f)
    {
        FTDHexCoord MoveTarget = FindMoveTarget(Unit, NearestEnemy, Grid, Pathfinding);
        if (MoveTarget.IsValid())
        {
            PerformMove(Unit, MoveTarget, Grid, AllUnits);

            // 移动后再尝试攻击
            ATDUnitBase* NewEnemyInRange = FindEnemyInRange(Unit, AllUnits);
            if (NewEnemyInRange)
            {
                PerformAttack(Unit, NewEnemyInRange, Grid);
                return ETDAIActionResult::Attacked;
            }

            return ETDAIActionResult::Moved;
        }
    }

    // 优先级 3：无法行动 → 待命
    return ETDAIActionResult::Idle;
}

// ===================================================================
// 目标搜索
// ===================================================================

ATDUnitBase* UTDUnitAIController::FindEnemyInRange(
    const ATDUnitBase* Unit,
    const UTDUnitSquad* AllUnits) const
{
    if (!Unit || !AllUnits)
    {
        return nullptr;
    }

    ATDUnitBase* ClosestEnemy = nullptr;
    int32 ClosestDistance = TNumericLimits<int32>::Max();

    TArray<ATDUnitBase*> AllUnitList = AllUnits->GetAllUnits();
    for (ATDUnitBase* OtherUnit : AllUnitList)
    {
        if (!IsValid(OtherUnit) || OtherUnit->IsDead())
        {
            continue;
        }

        // 跳过己方单位
        if (OtherUnit->GetOwnerPlayerIndex() == Unit->GetOwnerPlayerIndex())
        {
            continue;
        }

        // 检查是否在攻击范围内
        if (!Unit->IsInAttackRange(OtherUnit->GetCurrentCoord()))
        {
            continue;
        }

        // 选择最近的敌人
        int32 Distance = Unit->GetCurrentCoord().DistanceTo(OtherUnit->GetCurrentCoord());
        if (Distance < ClosestDistance)
        {
            ClosestDistance = Distance;
            ClosestEnemy = OtherUnit;
        }
    }

    return ClosestEnemy;
}

ATDUnitBase* UTDUnitAIController::FindNearestEnemy(
    const ATDUnitBase* Unit,
    const UTDUnitSquad* AllUnits) const
{
    if (!Unit || !AllUnits)
    {
        return nullptr;
    }

    ATDUnitBase* NearestEnemy = nullptr;
    int32 MinDistance = TNumericLimits<int32>::Max();

    TArray<ATDUnitBase*> AllUnitList = AllUnits->GetAllUnits();
    for (ATDUnitBase* OtherUnit : AllUnitList)
    {
        if (!IsValid(OtherUnit) || OtherUnit->IsDead())
        {
            continue;
        }

        // 跳过己方单位
        if (OtherUnit->GetOwnerPlayerIndex() == Unit->GetOwnerPlayerIndex())
        {
            continue;
        }

        int32 Distance = Unit->GetCurrentCoord().DistanceTo(OtherUnit->GetCurrentCoord());
        if (Distance < MinDistance)
        {
            MinDistance = Distance;
            NearestEnemy = OtherUnit;
        }
    }

    return NearestEnemy;
}

FTDHexCoord UTDUnitAIController::FindMoveTarget(
    const ATDUnitBase* Unit,
    const ATDUnitBase* TargetEnemy,
    const ATDHexGridManager* Grid,
    const UTDHexPathfinding* Pathfinding) const
{
    if (!Unit || !TargetEnemy || !Grid)
    {
        return FTDHexCoord::Invalid();
    }

    // 贪心策略：在所有可到达的邻居格子中，选择离敌人最近的
    // 未来接入 Pathfinding 时可替换为 A* 最优路径
    FTDHexCoord CurrentCoord = Unit->GetCurrentCoord();
    FTDHexCoord EnemyCoord = TargetEnemy->GetCurrentCoord();

    FTDHexCoord BestCoord = FTDHexCoord::Invalid();
    int32 BestDistance = CurrentCoord.DistanceTo(EnemyCoord);
    float BestCost = BIG_NUMBER;

    TArray<FTDHexCoord> Neighbors = CurrentCoord.GetAllNeighbors();
    for (const FTDHexCoord& NeighborCoord : Neighbors)
    {
        // 检查格子是否存在且可通行
        ATDHexTile* Tile = Grid->GetTileAt(NeighborCoord);
        if (!Tile || !Tile->IsPassable())
        {
            continue;
        }

        // 检查移动点是否足够
        float MoveCost = Tile->GetMovementCost();
        if (Unit->GetRemainingMovePoints() < MoveCost)
        {
            continue;
        }

        // 检查目标格子上没有其他单位（避免重叠）
        // 注意：这里暂不检查，因为 Squad 的查询需要非 const 版本
        // 后续由调用方保证

        // 选择离敌人最近的格子，距离相同时选移动消耗最低的
        int32 DistToEnemy = NeighborCoord.DistanceTo(EnemyCoord);
        if (DistToEnemy < BestDistance ||
            (DistToEnemy == BestDistance && MoveCost < BestCost))
        {
            BestDistance = DistToEnemy;
            BestCost = MoveCost;
            BestCoord = NeighborCoord;
        }
    }

    return BestCoord;
}

// ===================================================================
// 行为执行
// ===================================================================

void UTDUnitAIController::PerformAttack(
    ATDUnitBase* Attacker,
    ATDUnitBase* Target,
    const ATDHexGridManager* Grid)
{
    if (!Attacker || !Target || !Grid)
    {
        return;
    }

    float Damage = Attacker->CalculateDamageAgainst(Target, Grid);
    int32 IntDamage = FMath::RoundToInt32(Damage);
    IntDamage = FMath::Max(1, IntDamage);

    Target->ApplyDamage(IntDamage);
}

void UTDUnitAIController::PerformMove(
    ATDUnitBase* Unit,
    const FTDHexCoord& DestCoord,
    const ATDHexGridManager* Grid,
    UTDUnitSquad* AllUnits)
{
    if (!Unit || !Grid || !AllUnits)
    {
        return;
    }

    ATDHexTile* DestTile = Grid->GetTileAt(DestCoord);
    if (!DestTile)
    {
        return;
    }

    FTDHexCoord OldCoord = Unit->GetCurrentCoord();
    float MoveCost = DestTile->GetMovementCost();

    Unit->MoveTo(DestCoord, MoveCost);
    AllUnits->UpdateUnitPosition(Unit, OldCoord);
}
