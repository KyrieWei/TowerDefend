---
paths:
  - "Source/TowerDefend/Unit/**"
---

# Unit System Rules

Army unit system handles data definitions, squad management, movement/combat logic, and AI behavior.

## Dependency Constraint

Unit depends on **HexGrid** (coordinates and tile queries) and **Core** (shared types). It must not directly import Building or Combat — use forward declarations for cross-module references.

## Unit Types (ETDUnitType in TDUnitDataAsset.h)

| Type | Role | Counter Relationship |
|------|------|---------------------|
| Melee | Close combat, frontline | Beats Cavalry |
| Ranged | Distance attack, squishy | Beats Melee |
| Cavalry | Fast flanking | Beats Ranged |
| Siege | Destroy buildings/walls | Slow, vulnerable to units |
| Special | Unique abilities | Varies |

**Rock-Paper-Scissors**: Melee > Cavalry > Ranged > Melee

## Era Progression

| Era | Typical Units | Characteristics |
|-----|--------------|-----------------|
| Ancient | Club warrior, slinger | Low cost, weak |
| Classical | Swordsman, archer, cavalry | Basic triangle counter |
| Medieval | Knight, crossbowman, siege ram | Can breach walls |
| Renaissance | Musketeer, cannon | Ranged power leap |
| Industrial | Infantry, field cannon, armored car | High mobility |
| Modern | Tank, missile soldier, drone | High-tech dominance |

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDUnitDataAsset` | UDataAsset | Static data: cost, attributes, counter multipliers, tech era threshold |
| `ATDUnitBase` | AActor | Unit base: HP, coord, move points, damage calc with counter + height |
| `UTDUnitSquad` | UObject | Squad manager: TMap mapping, query by player/range, batch ops |
| `UTDUnitAIController` | UObject | AI controller: Attack->Move->Idle decision chain, greedy strategy |

## Enums

- `ETDUnitType` (TDUnitDataAsset.h) — Melee / Ranged / Cavalry / Siege / Special
- `ETDAIActionResult` (TDUnitAIController.h) — None / Moved / Attacked / Idle

## Key APIs

```
UTDUnitDataAsset::GetDamageMultiplierVs(TargetType)
ATDUnitBase::InitializeUnit() / MoveTo() / ApplyDamage() / CalculateDamageAgainst()
ATDUnitBase::IsInAttackRange() / GetHeightAttackBonus() / GetTerrainDefenseBonus()
UTDUnitSquad::AddUnit() / GetUnitAt() / GetUnitsByOwner() / RemoveDeadUnits() / ResetAllMovePoints()
UTDUnitAIController::ExecuteTurn() / FindNearestEnemy() / FindMoveTarget()
```

## Delegates

```
FTDOnUnitDied(DeadUnit)
FTDOnUnitDamaged(DamagedUnit, DamageAmount, RemainingHealth)
```

## AI Decision Chain

```
ExecuteTurn(Unit):
  1. Find enemy in attack range -> Attack (highest priority target)
  2. If no enemy in range -> Find nearest enemy -> Move toward it
  3. If no movement possible -> Idle
```

## Terrain Height Effects on Combat

- Units on higher ground gain attack bonus
- Units on lower ground suffer defense penalty
- Height difference affects line-of-sight for ranged units
- Specific per-tile defense bonuses via `GetTerrainDefenseBonus()`

## Squad Behavior

- Units move as squads (one squad per hex tile)
- Squad manager tracks all units per player via TMap
- Batch operations: `RemoveDeadUnits()` after combat, `ResetAllMovePoints()` each round
