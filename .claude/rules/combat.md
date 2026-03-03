---
paths:
  - "Source/TowerDefend/Combat/**"
---

# Combat System Rules

Combat system handles damage calculation, projectile flight, and combat flow orchestration (turn-based state machine).

## Dependency Constraint

Combat depends on **HexGrid** and uses **forward declarations** for Building and Unit types. It must not directly `#include` Building or Unit headers in its own headers — use forward declarations and include only in `.cpp` files.

## Combat State Machine (ETDCombatState in TDCombatManager.h)

```
None -> Deploying -> InProgress -> Finished
```

- **Deploying**: Attacker places units into UnitSquad
- **InProgress**: Turn-by-turn combat execution
- **Finished**: Winner determined, result output

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDDamageCalculator` | UObject | Pure calculation: unit-vs-unit, building-vs-unit, unit-vs-building; includes height + terrain + minimum damage |
| `ATDProjectileBase` | AActor | Projectile: UProjectileMovementComponent, hit damage + terrain destruction probability |
| `UTDCombatManager` | UObject | Combat manager: init -> turn execution -> win/loss determination -> result output |

## Key APIs

```
UTDDamageCalculator::CalculateUnitDamage() / CalculateBuildingDamage() / GetHeightModifier()
ATDProjectileBase::InitializeProjectile()
UTDCombatManager::InitializeCombat() / ExecuteCombatTurn() / ExecuteFullCombat() / GetCombatResult()
```

## Damage Formula

```
BaseDamage = AttackPower x UnitCounterMultiplier
HeightMod  = 1.0 + (AttackerHeight - DefenderHeight) x 0.15   (attacker higher)
           = 1.0 - (DefenderHeight - AttackerHeight) x 0.10   (defender higher)
TerrainDef = 1.0 - DefenseBonus
FinalDmg   = max(BaseDamage x HeightMod x TerrainDef, BaseDamage x 0.1, 1)
```

**Important**: Minimum damage is always `max(BaseDamage * 0.1, 1)` — attacks always deal at least 1 damage.

## Delegates

```
FTDOnCombatStateChanged(NewState)
FTDOnCombatFinished(Result)
```

## Combat Flow (Detailed)

```
CombatManager::InitializeCombat(Attacker, Defender, Grid)
    |
    v State = Deploying
    | (attacker places units into UnitSquad)
    |
    v State = InProgress
    |
    +--- ExecuteCombatTurn() <--------------------+
    |       |                                      |
    |       +-- ProcessBuildingAttacks(Grid)        |
    |       |     -> DamageCalculator               |
    |       |        .CalculateBuildingDamage()      |
    |       |     -> Unit.ApplyDamage()              |
    |       |                                      |
    |       +-- UnitSquad.RemoveDeadUnits()         |
    |       |                                      |
    |       +-- ProcessUnitActions(Grid)            |
    |       |     -> UnitAI.ExecuteTurn()           |
    |       |        +- FindEnemyInRange -> Attack  |
    |       |        +- FindNearestEnemy -> Move    |
    |       |                                      |
    |       +-- UnitSquad.RemoveDeadUnits()         |
    |       |                                      |
    |       +-- CheckCombatEnd()                   |
    |           +- false ---------------------------+
    |           +- true
    |
    v State = Finished
    |
    +-- FTDRoundResult = GetCombatResult()
```

## Win/Loss Determination

- **Attacker wins**: Attacker units reach/destroy defender's Base building
- **Defender wins**: Time expires OR all attacker units eliminated
- **Terrain destruction**: Heavy siege weapons hitting tiles have a probability of lowering terrain height (creating craters)
