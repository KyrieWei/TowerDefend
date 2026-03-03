---
paths:
  - "Source/TowerDefend/TechTree/**"
---

# TechTree System Rules

Tech tree system handles tech data configuration, prerequisite dependency checking, research progress management, passive bonus aggregation, and unlock queries.

## Dependency Constraint

TechTree depends on **Core** (SharedTypes + PlayerState). It must not import HexGrid, Building, Unit, or Combat.

## Tech Eras (ETDTechEra in Core/TDSharedTypes.h)

Ancient -> Classical -> Medieval -> Renaissance -> Industrial -> Modern

## Research States (ETDTechResearchState in TDTechNode.h)

```
Locked -> Available -> Researching -> Completed
```

- **Locked**: Prerequisites not met
- **Available**: Prerequisites met, can be researched
- **Researching**: Currently being researched (if multi-turn research is used)
- **Completed**: Research finished, bonuses active

## Tech Tree Structure

```
Ancient Era
+-- Mining -> Bronze Smelting -> Iron Working
+-- Animal Husbandry -> Horsemanship
+-- Pottery -> Masonry -> Architecture

Classical Era
+-- Mathematics -> Engineering (unlocks siege weapons)
+-- Tactics -> Military Training
+-- Currency -> Trade

Medieval Era
+-- Foundry -> Machinery (unlocks crossbow, gear mechanisms)
+-- Chivalry -> Heavy Armor
+-- Castle Architecture -> Fortifications

Renaissance Era
+-- Gunpowder -> Ballistics
+-- Printing Press -> Scientific Method
+-- Banking -> Economics

Industrial Era
+-- Steam Engine -> Railroad -> Internal Combustion
+-- Rifling -> Rapid-Fire Cannon
+-- Industrialization -> Assembly Line

Modern Era
+-- Electronics -> Computers -> Drone Technology
+-- Nuclear Physics -> Missile Technology
+-- Aviation -> Stealth Technology
```

## Terrain Modification Unlocks

| Era | Tech | Unlock |
|-----|------|--------|
| Ancient | Pottery -> Masonry | Basic terrain modification (±1 height) |
| Classical | Engineering | Extended range (modify adjacent tiles) |
| Medieval | Castle Architecture | Advanced modification (±2 height) |
| Industrial | Industrialization | Batch modification (multiple tiles at once) |

## Structs

- `FTDTechNodeData` (TDTechTreeDataAsset.h) — Tech node config: era, cost, prerequisites, unlocks, passive bonuses

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDTechTreeDataAsset` | UDataAsset | Tech tree config: designer-editable, query by era/ID |
| `UTDTechNode` | UObject | Tech node runtime state: research state management |
| `UTDTechTreeManager` | UObject | Tech tree manager: research ops, bonus aggregation, unlock queries |

## Key APIs

```
UTDTechTreeDataAsset::FindTechNode() / GetTechsByEra()
UTDTechNode::Initialize() / GetResearchState() / SetResearchState()
UTDTechTreeManager::Initialize() / ResearchTech() / CanResearchTech()
UTDTechTreeManager::GetTotalAttackBonus() / GetTotalDefenseBonus() / GetTotalResourceBonus()
UTDTechTreeManager::IsBuildingUnlocked() / IsUnitUnlocked() / GetUnlockedTerrainModifyLevel()
```

## Delegates

```
FTDOnTechResearched(TechID, Era)
FTDOnEraAdvanced(NewEra)
```

## Research Flow

```
TechTreeManager::ResearchTech(TechID, Player)
    |
    +-- CanResearchTech(TechID, Player)?
    |     +-- ArePrerequisitesMet(TechID)?  -- all prerequisites Completed?
    |     +-- TechNode.State == Available?
    |     +-- Player.ResearchPoints >= Cost?
    |
    +-- Fail -> return false
    |
    +-- Success:
        +-- Player.SpendResearchPoints(Cost)
        +-- TechNode.State = Completed
        +-- RefreshAvailability()  -- update subsequent nodes Locked->Available
        +-- OnTechResearched.Broadcast(TechID, Era)
        +-- If Era > CurrentEra -> OnEraAdvanced.Broadcast(NewEra)
```

## Data-Driven Design

- Tech tree uses `UDataAsset` + JSON config tables
- Designers can hot-update tech tree data
- Each tech node defines: unlocked buildings, unlocked units, building upgrades, unit upgrades, passive bonuses
- Research costs scale with era
