---
paths:
  - "Source/TowerDefend/Building/**"
---

# Building System Rules

Building system handles data definitions, placement validation, attack/defense logic, economic output, and management.

## Dependency Constraint

Building depends on **HexGrid** (coordinates and tile queries) and **Core** (shared types). It must not directly import Unit or Combat — use forward declarations for cross-module references.

## Building Types (ETDBuildingType in TDBuildingDataAsset.h)

| Type | Function | Upgrade Direction |
|------|----------|-------------------|
| Base | Player core; breach = round loss | HP, passive defense |
| ArrowTower | Auto-attack enemies in range | Range, damage, attack speed |
| CannonTower | Heavy auto-attack | Range, damage, attack speed |
| Wall | Block/slow enemies | HP, slow effect |
| ResourceBuilding | Produce resources each round | Output amount |
| Barracks | Train army units | Unlock advanced unit types |
| Trap | One-shot or persistent ground effect | Damage, area |

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDBuildingDataAsset` | UDataAsset | Static data: cost, HP, attack, output, terrain restrictions |
| `ATDBuildingBase` | AActor | Building base: init, upgrade, take damage, virtual attack interface |
| `ATDDefenseTower` | ATDBuildingBase | Defense tower: FTimerHandle timed attack, height range bonus |
| `ATDWall` | ATDBuildingBase | Wall: movement cost multiplier 10x, can block passage |
| `UTDBuildingManager` | UObject | Building management: TMap mapping, 6-step placement validation, economy aggregation |

## Key APIs

```
UTDBuildingDataAsset::CanBuildOnTerrain() / GetEffectiveAttackRange() / GetUpgradeCost()
ATDBuildingBase::InitializeBuilding() / ApplyDamage() / Upgrade() / CanAttack() / GetAttackRange()
ATDDefenseTower::StartAutoAttack() / StopAutoAttack()
ATDWall::GetMovementCostMultiplier() / DoesBlockPassage()
UTDBuildingManager::PlaceBuilding() / CanPlaceBuilding() / RemoveBuilding() / CalculateTotalGoldIncome()
```

## Delegates

```
FTDOnBuildingDestroyed(DestroyedBuilding)
FTDOnBuildingDamaged(DamagedBuilding, DamageAmount, RemainingHealth)
```

## Placement Rules

Buildings are placed on hex tiles, obeying terrain restrictions:

1. Tile must exist and be valid
2. Tile must be buildable (`IsBuildable()` — terrain type check)
3. Tile must not already have a building
4. Tile must belong to the player (ownership check)
5. Player must have sufficient gold (`SpendGold()`)
6. Building-specific terrain restrictions (e.g., no towers on water, walls require flat or hill terrain)

Height affects buildings:
- Highland buildings gain range bonuses
- Terrain height changes may destroy buildings (stability check)
- Building orientation affects firing arc

## Data-Driven Design

All building attributes use `UDataAsset` for designer tuning:
- Cost, HP, attack power, attack range, attack speed
- Terrain restrictions (allowed terrain types)
- Economic output (gold per round)
- Upgrade costs and stat scaling
