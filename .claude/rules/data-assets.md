---
paths:
  - "Content/TowerDefend/DataAssets/**"
  - "Source/TowerDefend/**/TD*DataAsset.*"
---

# Data Asset Usage Rules

All gameplay data is driven by `UDataAsset` subclasses for designer-friendly tuning without code changes.

## Data Asset Classes

| Class | Location | Purpose |
|-------|----------|---------|
| `UTDBuildingDataAsset` | Building/TDBuildingDataAsset.h | Building static data: cost, HP, attack, output, terrain restrictions, upgrade costs |
| `UTDUnitDataAsset` | Unit/TDUnitDataAsset.h | Unit static data: cost, attributes, counter multipliers, tech era threshold |
| `UTDTechTreeDataAsset` | TechTree/TDTechTreeDataAsset.h | Tech tree config: nodes, prerequisites, era grouping, unlock definitions, passive bonuses |

## Content Locations

```
Content/TowerDefend/DataAssets/
├── Buildings/     # Building data assets (one per building type)
├── Units/         # Unit data assets (one per unit type)
└── TechTree/      # Tech tree data asset(s)
```

## Design Principles

- **Data-driven**: All numerical values (cost, HP, damage, range, etc.) live in DataAssets, not hardcoded
- **Hot-tunable**: Designers can adjust values in-editor without recompilation
- **JSON compatible**: Tech tree supports JSON config tables for external editing
- **Blueprint accessible**: All DataAssets are `BlueprintReadOnly` for BP widget display

## Building DataAsset Fields

Key attributes per building:
- `BuildingType` (ETDBuildingType)
- `BaseCost` (gold cost to build)
- `BaseHealth` (starting HP)
- `AttackDamage`, `AttackRange`, `AttackSpeed` (for towers)
- `GoldIncomePerRound` (for resource buildings)
- `AllowedTerrainTypes` (TArray of ETDTerrainType)
- `UpgradeCosts` (TArray per level)
- `CanBuildOnTerrain()` — checks terrain type restriction
- `GetEffectiveAttackRange()` — base range + height bonus
- `GetUpgradeCost()` — cost for next level

## Unit DataAsset Fields

Key attributes per unit:
- `UnitType` (ETDUnitType)
- `Cost` (gold cost to train)
- `Health`, `AttackPower`, `MoveRange`, `AttackRange`
- `DamageMultipliers` (TMap<ETDUnitType, float>) — counter multipliers
- `RequiredTechEra` (ETDTechEra) — minimum era to unlock
- `GetDamageMultiplierVs(TargetType)` — lookup counter multiplier

## TechTree DataAsset Fields

Key attributes per tech node (`FTDTechNodeData`):
- `TechID` (FName)
- `Era` (ETDTechEra)
- `ResearchCost` (research points)
- `Prerequisites` (TArray<FName>)
- `UnlockedBuildings`, `UnlockedUnits` (TArray<FName>)
- `PassiveBonuses` (attack/defense/resource multipliers)

## Validation

- All DataAsset pointers should be validated at initialization (`check`/`ensure`)
- Missing DataAssets should produce clear error logs
- DataAsset references should use `TSoftObjectPtr` for async loading where appropriate
