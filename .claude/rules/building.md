---
paths:
  - "Source/TowerDefend/Building/**"
---

# Building System Rules

Building system handles data definitions, placement validation, construction/demolition, attack/defense logic, economic output, and management.

## Dependency Constraint

Building depends on **HexGrid** (coordinates and tile queries) and **Core** (shared types). It must not directly import Unit or Combat — use forward declarations for cross-module references.

## Building Types (ETDBuildingType in TDBuildingDataAsset.h)

| Type | C++ Class | Function | Upgrade Direction |
|------|-----------|----------|-------------------|
| Base | ATDBuildingBase | Player core; breach = round loss | HP, passive defense |
| ArrowTower | ATDDefenseTower | Auto-attack enemies in range | Range, damage, attack speed |
| CannonTower | ATDDefenseTower | Heavy auto-attack | Range, damage, attack speed |
| MageTower | ATDDefenseTower | Magic auto-attack, area damage | Range, damage, attack speed |
| Wall | ATDWall | Block/slow enemies | HP, slow effect |
| ResourceBuilding | ATDCurrencyBuilding | Produce resources each round (Market→Bank→Stock Exchange) | Output amount, synergy |
| Barracks | ATDBuildingBase | Train army units | Unlock advanced unit types |
| Trap | ATDBuildingBase | One-shot or persistent ground effect | Damage, area |

Type-to-class mapping is defined in `UTDBuildingManager::DetermineSpawnClass()`. Each DataAsset can specify a `BuildingActorClass` (Blueprint class) to override the default C++ class mapping.

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDBuildingDataAsset` | UDataAsset | Static data: cost, HP, attack, output, terrain restrictions, Actor class mapping |
| `ATDBuildingBase` | AActor | Building base: init, upgrade, take damage, virtual attack/economy interface |
| `ATDDefenseTower` | ATDBuildingBase | Defense tower: FTimerHandle timed attack, height range bonus |
| `ATDWall` | ATDBuildingBase | Wall: movement cost multiplier 10x, can block passage |
| `ATDCurrencyBuilding` | ATDBuildingBase | Currency building: per-level gold output, mesh swap on upgrade, adjacency synergy |
| `UTDBuildingManager` | UObject | Building management: TMap mapping, 7-step placement validation, economy aggregation |

## Key APIs

```
UTDBuildingDataAsset::CanBuildOnTerrain() / GetEffectiveAttackRange() / GetUpgradeCost()
ATDBuildingBase::InitializeBuilding() / ApplyDamage() / Upgrade() / CanAttack() / GetAttackRange()
ATDBuildingBase::GetGoldPerRound() / GetResearchPerRound()  (virtual, overridable by subclasses)
ATDDefenseTower::StartAutoAttack() / StopAutoAttack() / SetCachedHeightLevel()
ATDWall::GetMovementCostMultiplier() / DoesBlockPassage()
ATDCurrencyBuilding::GetBaseGoldPerRound() / GetSynergyBonusGold() / ApplyLevelData()
UTDBuildingManager::PlaceBuilding() / CanPlaceBuilding() / RemoveBuilding() / UpgradeBuilding()
UTDBuildingManager::CalculateTotalGoldIncome() / CalculateTotalResearchIncome()
UTDBuildingManager::DemolishBuilding()
UTDBuildingManager::ExportBuildingData() / ImportBuildingData()
UTDBuildingManager::DetermineSpawnClass()
```

## Delegates

```
FTDOnBuildingDestroyed(DestroyedBuilding)
FTDOnBuildingDamaged(DamagedBuilding, DamageAmount, RemainingHealth)
FTDOnCurrencyBuildingUpgraded(UpgradedBuilding, OldLevel, NewLevel)
FTDOnBuildingPlaced(PlacedBuilding, Coord, OwnerPlayerIndex)
FTDOnBuildingDemolished(DemolishedCoord, OwnerPlayerIndex, RefundGold)
```

---

## Construction (Building Placement)

### Terrain Buildability

Buildings can only be placed on specific terrain types. The hex tile's `IsBuildable()` is the first gate:

| Terrain Type | Buildable | Passable | Reason |
|-------------|-----------|----------|--------|
| Plain | YES | YES | Standard buildable terrain |
| Hill | YES | YES | Buildable, defense +10%, ranged towers gain height bonus |
| Forest | YES | YES | Buildable, defense +15%, blocks line of sight |
| Mountain | NO | NO | Impassable, unbuildable — terrain too steep |
| River | NO | YES | Unbuildable — water terrain, units can cross |
| Swamp | NO | YES | Unbuildable — unstable ground, units can cross slowly |
| DeepWater | NO | NO | Impassable, unbuildable — open water, height locked at 1 |

`IsBuildable()` implementation (TDHexTile.cpp):
```cpp
return TerrainType != ETDTerrainType::Mountain
    && TerrainType != ETDTerrainType::DeepWater
    && TerrainType != ETDTerrainType::Swamp
    && TerrainType != ETDTerrainType::River;
```

### Per-Building Terrain Restrictions

Beyond the global `IsBuildable()` check, each building has additional restrictions via `UTDBuildingDataAsset`:

- **`ForbiddenTerrains`** — array of `ETDTerrainType` values this building cannot be placed on (e.g., a specific tower might forbid Forest placement)
- **`MinHeightLevel` / `MaxHeightLevel`** — height range constraint (default 1–4, clamped 1–5)

These are checked by `UTDBuildingDataAsset::CanBuildOnTerrain(TerrainType, HeightLevel)`:
```cpp
return !ForbiddenTerrains.Contains(InTerrainType)
    && InHeightLevel >= MinHeightLevel
    && InHeightLevel <= MaxHeightLevel;
```

### One Building Per Tile

Each hex tile can hold at most one building. Enforced by `BuildingMap.Contains(InCoord)` check in `CanPlaceBuilding()`. The `BuildingMap` is a `TMap<FTDHexCoord, ATDBuildingBase*>` providing O(1) lookup.

### 7-Step Placement Validation (CanPlaceBuilding)

All validation is in `UTDBuildingManager::CanPlaceBuilding()`, executed in strict order:

| Step | Check | Condition | Failure Behavior |
|------|-------|-----------|-----------------|
| 1 | Parameter validity | `InBuildingData != null`, `Grid != null`, `InCoord.IsValid()` | Warning log, return false |
| 2 | Tile exists | `Grid->GetTileAt(InCoord) != nullptr` | Warning log, return false |
| 3 | Tile unoccupied | `!BuildingMap.Contains(InCoord)` — one building per tile | Warning log, return false |
| 4 | Tile buildable | `Tile->IsBuildable() == true` — terrain type check | Warning log, return false |
| 5 | Tile ownership | `TileOwner == -1` (neutral) OR `TileOwner == InOwnerPlayerIndex` | Warning log, return false |
| 6 | Building terrain/height | `BuildingData->CanBuildOnTerrain(TerrainType, HeightLevel)` | Warning log, return false |
| 7 | Tech tree unlock | `TechIntegration->IsBuildingUnlockedForPlayer(PlayerIndex, BuildingID)` | Warning log, return false |

Step 7 is **optional** — only executed if `TechIntegration` has been injected via `SetTechTreeIntegration()`. If null, the check is silently skipped (all buildings are allowed).

### Gold Cost

**`PlaceBuilding()` does NOT deduct gold.** Gold spending is the caller's responsibility.

Expected caller flow:
```
1. Check: PlayerState->CanAfford(BuildingData->GoldCost)
2. Check: BuildingManager->CanPlaceBuilding(Data, Grid, Coord, PlayerIndex)
3. Spend: PlayerState->SpendGold(BuildingData->GoldCost)
4. Place: BuildingManager->PlaceBuilding(World, Data, Grid, Coord, PlayerIndex)
```

Gold cost is defined in `UTDBuildingDataAsset::GoldCost` (default 50, ClampMin=0).

### PlaceBuilding Post-Spawn Actions

After spawning the actor, `PlaceBuilding()` performs type-specific initialization:

| Building Type | Post-Spawn Action |
|--------------|-------------------|
| ATDDefenseTower | `SetCachedHeightLevel(Tile->GetHeightLevel())` — caches height for range bonus calculation |
| ATDCurrencyBuilding | `SetBuildingManager(this)` + `ApplyLevelData()` — injects manager ref for synergy, applies Lv1 mesh/stats |
| Others | No extra initialization |

Spawn location: `Tile->GetActorLocation() + (0, 0, HeightLevelUnitZ * 0.5f)` — building sits half a height unit above the tile surface.

---

## Demolition (Building Removal)

### RemoveBuilding

`UTDBuildingManager::RemoveBuilding(Coord)` removes a building from the map and destroys the actor. **No gold refund is issued** — the caller handles refund if needed.

### DemolishBuilding (with refund)

`UTDBuildingManager::DemolishBuilding(Coord, PlayerState)` is the high-level demolition API:

Expected caller flow:
```
1. Validate: building exists at coord, owned by player
2. Calculate refund: BuildingData->GoldCost * RefundRate (e.g., 50%)
3. Refund: PlayerState->AddGold(RefundGold)
4. Remove: RemoveBuilding(Coord)
5. Broadcast: OnBuildingDemolished delegate
```

Refund rate is configurable per building type, preventing economy exploits while allowing tactical repositioning.

---

## Tech Tree Unlock

Buildings can require a specific tech era before they become available. Two layers of control:

### Layer 1: MinTechEra (Data Asset)

`UTDBuildingDataAsset::MinTechEra` — integer field (default 0 = no restriction). This is metadata for UI display and tooltip purposes. It is **not checked** directly in `CanPlaceBuilding()`.

### Layer 2: TechIntegration (Runtime Check)

Step 7 of `CanPlaceBuilding()` calls `TechIntegration->IsBuildingUnlockedForPlayer(PlayerIndex, BuildingID)`. This delegates to the player's `UTDTechTreeManager::IsBuildingUnlocked()` for a full unlock check based on research progress.

The tech tree integration is injected via `UTDBuildingManager::SetTechTreeIntegration()`. If not injected, all buildings are buildable regardless of tech era.

### Current Tech Era Requirements

| Building | MinTechEra | Notes |
|----------|-----------|-------|
| Market (CurrencyLine) | 0 | Available from start |
| ArrowTower | 0 | Available from start |
| WoodArrowTower | 0 | Available from start |
| CannonTower | 2 | Requires era 2 |
| MageTower | 2 | Requires era 2 |
| Wall | 0 | Available from start |
| Base | 0 | Auto-placed |

---

## Height and Building Interaction

### Height Range Restrictions

Each building has `MinHeightLevel` (default 1) and `MaxHeightLevel` (default 4), checked in Step 6 of placement validation. Height levels range from 1 to 5.

### Defense Tower Height Bonus

`ATDDefenseTower` caches the tile's height at placement time via `CachedHeightLevel`. The effective attack range is:

```
EffectiveRange = BaseRange + HeightRangeBonus * max(HeightLevel - 1, 0)
```

This is calculated in `UTDBuildingDataAsset::GetEffectiveAttackRange(HeightLevel)` and called by `ATDDefenseTower::GetAttackRange()`.

**Important**: The height is cached at placement time and NOT updated dynamically. If `UTDTerrainModifier` changes the tile height after placement, the tower's range will be stale until `SetCachedHeightLevel()` is called again.

### Height Level Constants

| Constant | Value | Defined In |
|----------|-------|-----------|
| MinHeightLevel | 1 | ATDHexTile |
| MaxHeightLevel | 5 | ATDHexTile |
| HeightLevelUnitZ | 50.0f cm | ATDHexTile |
| MaxNeighborHeightDiff | 3 | UTDTerrainModifier |

---

## Upgrade System

### Base Upgrade

`ATDBuildingBase::Upgrade()` is `virtual`. Base implementation:
- Checks `CanUpgrade()` — `BuildingData != null && CurrentLevel < MaxLevel`
- Increments `CurrentLevel++`
- Does NOT change health, mesh, or broadcast delegates
- Caller must check `CanUpgrade()` and call `PlayerState->SpendGold(GetUpgradeCost())` beforehand

Upgrade costs are defined in `UTDBuildingDataAsset::UpgradeCosts` array (index 0 = Lv1→Lv2, index 1 = Lv2→Lv3).

### Currency Building Upgrade (Override)

`ATDCurrencyBuilding::Upgrade()` extends the base:
- Calls `Super::Upgrade()` for level increment
- Calls `UpdateLevelVisualAndStats()` — switches mesh and sets health from `LevelDataArray`
- Broadcasts `OnCurrencyBuildingUpgraded(this, OldLevel, NewLevel)`

Upgrade line (Civ 6 inspired): Market (Lv1) → Bank (Lv2) → Stock Exchange (Lv3)

---

## Economy Integration

### Building Income

`UTDBuildingManager::CalculateTotalGoldIncome(PlayerIndex)` sums `GetGoldPerRound()` for all valid, non-destroyed buildings owned by the player. Called by `UTDResourceManager::CalculateBuildingIncome()`.

`GetGoldPerRound()` is `virtual`:
- **ATDBuildingBase**: returns `BuildingData->GoldPerRound` directly
- **ATDCurrencyBuilding**: returns `GetBaseGoldPerRound() + GetSynergyBonusGold()` (level-scaled + adjacency bonus)

### Currency Building Synergy

Adjacent same-owner currency buildings provide a gold bonus:
```
SynergyBonus = BaseGold * AdjacentCurrencyCount * AdjacentSynergyBonusPercent / 100
```

Default `AdjacentSynergyBonusPercent = 10`. Uses `BuildingManager->GetBuildingAt()` for O(1) neighbor lookup.

### Round Income Formula

```
TotalGold = Config.GoldPerRound (base 50)
          + BuildingManager->CalculateTotalGoldIncome(PlayerIndex)
          + floor(TotalIncome * TechBonus% / 100)
```

Computed by `UTDResourceManager::CalculateRoundIncome()` and distributed via `GrantRoundResources()`.

---

## Data-Driven Design

All building attributes use `UTDBuildingDataAsset` for designer tuning:

| Category | Fields |
|----------|--------|
| Identity | `BuildingID`, `DisplayName`, `BuildingType` |
| Economy | `GoldCost`, `UpgradeCosts[]`, `GoldPerRound`, `ResearchPerRound` |
| Combat | `MaxHealth`, `MaxLevel`, `AttackDamage`, `AttackRange`, `AttackInterval`, `HeightRangeBonus` |
| Placement | `MinTechEra`, `ForbiddenTerrains[]`, `MinHeightLevel`, `MaxHeightLevel` |
| Visual | `BuildingMesh` |
| Actor | `BuildingActorClass` |

For `ATDCurrencyBuilding`, additional per-level data is in `LevelDataArray` (on the Blueprint):
- `LevelDisplayName`, `GoldPerRound`, `MaxHealth`, `LevelMesh`

---

## Actor Class Mapping (BuildingActorClass)

`UTDBuildingDataAsset::BuildingActorClass` is a `TSubclassOf<ATDBuildingBase>` field that maps a DataAsset to a specific Blueprint actor class.

### Spawn Priority

`DetermineSpawnClass()` resolves the actor class in this order:
1. **`BuildingActorClass`** — if set on the DataAsset, use this Blueprint class directly
2. **Type-based fallback** — if `BuildingActorClass` is null, infer from `BuildingType`:
   - ArrowTower / CannonTower / MageTower → `ATDDefenseTower`
   - Wall → `ATDWall`
   - ResourceBuilding → `ATDCurrencyBuilding`
   - Others → `ATDBuildingBase`

### Why This Matters

Spawning the Blueprint class (not the C++ base class) ensures:
- Blueprint-configured default properties are preserved (e.g., `LevelDataArray` on BP_TDCurrencyBuilding)
- Blueprint EventGraph logic executes correctly
- Any Blueprint-added components are present on the spawned actor

### Current Configuration

| DataAsset | BuildingID | BuildingActorClass |
|-----------|-----------|-------------------|
| DA_Building_WoodArrowTower | `WoodArrowTower` | BP_TDWoodArrowTower |
| DA_Building_MageTower | `MageTower` | BP_TDMageTower |
| DA_Building_CurrencyLine | `CurrencyLine` | BP_TDCurrencyBuilding |
| DA_Building_ArrowTower | `ArrowTower` | *(unset — falls back to ATDDefenseTower)* |
| DA_Building_CannonTower | `CannonTower` | *(unset — falls back to ATDDefenseTower)* |
| DA_Building_Wall | `Wall` | *(unset — falls back to ATDWall)* |
| DA_Building_Base | `Base` | *(unset — falls back to ATDBuildingBase)* |

### Adding a New Building

To add a new building type that spawns a Blueprint actor:
1. Create Blueprint (e.g., BP_TDNewBuilding) with appropriate C++ parent
2. Create DataAsset (e.g., DA_Building_NewBuilding) with `BuildingID`
3. Set `BuildingActorClass` on the DataAsset to the Blueprint class
4. The building will spawn as the Blueprint actor in both `PlaceBuilding()` and `ImportBuildingData()`

---

## Save/Load (Building Serialization)

### Save Data Structure

`FTDBuildingSaveData` (defined in TDHexGridSaveData.h):

| Field | Type | Description |
|-------|------|-------------|
| `Coord` | FTDHexCoord | Hex tile coordinate |
| `BuildingID` | FName | Matches `UTDBuildingDataAsset::BuildingID` |
| `Level` | int32 | Current level (1-based, ClampMin=1) |
| `CurrentHealth` | int32 | Current HP (0 = use default max) |
| `OwnerPlayerIndex` | int32 | Owning player (-1 = neutral) |

### Export Flow

```
UTDBuildingManager::ExportBuildingData()
  → iterate BuildingMap
  → for each valid building: read Coord, BuildingID, Level, CurrentHealth, OwnerPlayerIndex
  → return TArray<FTDBuildingSaveData>
```

### Import Flow

```
UTDBuildingManager::ImportBuildingData(World, Grid, DataList, DataAssets)
  → build FName→DataAsset lookup table from DataAssets
  → for each FTDBuildingSaveData:
    1. Find DataAsset by BuildingID (skip if unknown)
    2. Find tile at Coord (skip if missing)
    3. DetermineSpawnClass → Blueprint class or C++ fallback
    4. SpawnActor at tile location
    5. InitializeBuilding(DataAsset, Coord, Owner)
    6. Restore level via iterative Upgrade() calls (triggers subclass logic)
    7. Restore health if CurrentHealth > 0
    8. Type-specific post-spawn: DefenseTower height cache, CurrencyBuilding manager ref
    9. Register to BuildingMap
```

**Note**: `ImportBuildingData` skips `CanPlaceBuilding` validation — save data is treated as trusted.

### JSON Format (Version 2)

```json
"Buildings": [
  {
    "Q": 3, "R": 0,
    "BuildingID": "WoodArrowTower",
    "Level": 1,
    "CurrentHealth": 0,
    "OwnerPlayerIndex": 0
  }
]
```
