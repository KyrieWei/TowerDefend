---
paths:
  - "Source/TowerDefend/HexGrid/**"
---

# HexGrid System Rules

HexGrid is the game's foundation: coordinate math, terrain generation, tile management, A* pathfinding, terrain modification, and save/load serialization.

## Dependency Constraint

HexGrid depends **only on Core** (shared types). It must not import Building, Unit, Combat, or any other game-logic module.

## Coordinate System

Cube coordinates (Q, R, S) where Q + R + S = 0. Reference: Red Blob Games hex grid guide.

### FTDHexCoord (TDHexCoord.h)

- Can be used as TMap key (has GetTypeHash)
- Core operations: `DistanceTo()`, `GetAllNeighbors()`, `ToWorldPosition()`, `FromWorldPosition()`, `CubeRound()`
- All coordinate math must maintain the Q + R + S = 0 invariant

## Terrain Types (ETDTerrainType in TDHexGridSaveData.h)

Plain, Hill, Mountain, Forest, River, Swamp, DeepWater

## Height Model

| Level | Name | Visual | Gameplay Effect |
|-------|------|--------|-----------------|
| -2 | Deep Water | Dark blue, below ground | Impassable, unbuildable |
| -1 | Shallow Water/Swamp | Light blue/mud, slightly below | Heavy slow, no large buildings |
| 0 | Plain (baseline) | Standard ground level | No extra effect |
| 1 | Hill | Slightly raised | Defense +10%, ranged range +1, move cost +0.5 |
| 2 | Highland | Noticeably raised | Defense +20%, ranged range +2, move cost +1 |
| 3 | Mountain | Greatly raised, rocky texture | Impassable (except special units), unbuildable (except special buildings) |

## Terrain Modification Rules

- Single operation changes height by +/-1 only
- Height change has cooldown (prevent spamming same tile)
- Height change affects building stability (excessive height diff may destroy buildings)
- Adjacent tile height difference cannot exceed 3 (natural transition constraint)
- Modification unlocked through TechTree: basic at Ancient "Masonry", extended at Classical "Engineering", advanced at Medieval "Castle Architecture", batch at Industrial "Industrialization"

## Classes

| Class | Base | Role |
|-------|------|------|
| `ATDHexTile` | AActor | Hex tile entity: terrain, height, ownership, drives mesh visuals |
| `ATDHexGridManager` | AActor | Grid manager: TMap O(1) lookup, generation/clear/save |
| `UTDTerrainGenerator` | UObject | Perlin Noise procedural terrain (symmetry, base flattening, height smoothing) |
| `UTDTerrainModifier` | UObject | Runtime terrain modification (raise/lower ±1, validation) |
| `UTDHexPathfinding` | UObject | A* pathfinding + Dijkstra reachable range, with height-cost correction |
| `UTDHexGridSaveGame` | USaveGame | Local save + JSON import/export |

## Key APIs

```
FTDHexCoord::DistanceTo() / GetAllNeighbors() / ToWorldPosition() / FromWorldPosition()
ATDHexTile::GetMovementCost() / GetDefenseBonus() / IsPassable() / IsBuildable()
ATDHexGridManager::GetTileAt() / GetNeighborTiles() / GetTilesInRange() / GenerateGrid()
UTDHexPathfinding::FindPath() / FindPathFiltered() / GetReachableTiles() / CalculatePathCost()
UTDTerrainModifier::RaiseTerrain() / LowerTerrain() / ValidateModification()
UTDTerrainGenerator::GenerateMap()
```

## Terrain Generation Algorithm

1. **Base terrain layer**: Perlin/Simplex Noise generates continuous height field, mapped to discrete height levels
2. **Terrain type layer**: Height + moisture (second Noise layer) determines terrain type:
   - Height >= 3 -> Mountain
   - Height 2 + low moisture -> Hill
   - Height 0 + high moisture -> Swamp
   - Height <= -1 -> Water
   - Otherwise -> Plain/Forest (random)
3. **Symmetry guarantee**: PVP maps mirrored around center point/axis for fairness
4. **Rule constraint post-processing**:
   - Ensure each player's base position is Plain (height 0)
   - Ensure N tiles around base have no mountains/deep water (guaranteed buildable area)
   - Ensure map connectivity (A* verifies path exists between any two passable tiles)
   - Smooth areas where adjacent tile height diff exceeds limit

### Generator Configuration

```
MapRadius = 15           // Map radius in tiles
Seed = 0                 // Random seed (0 = random)
HeightNoiseScale = 0.08  // Height noise scale
MoistureNoiseScale = 0.12// Moisture noise scale
bSymmetric = true        // Generate symmetric map
PlayerCount = 2          // Player count (affects base distribution)
```

## Save/Load System

| Format | Method | Use Case |
|--------|--------|----------|
| UE SaveGame | Binary (USaveGame subclass) | Runtime fast save/load, local saves |
| JSON export | `.json` text | Designer editing, map sharing, version control |

### Save Data Structures (TDHexGridSaveData.h)

**`FTDHexTileSaveData`** — per-tile data:

| Field | Type | Description |
|-------|------|-------------|
| `Coord` | FTDHexCoord | Hex coordinate (Q, R) |
| `TerrainType` | ETDTerrainType | Terrain type enum |
| `HeightLevel` | int32 | Height level |
| `OwnerPlayerIndex` | int32 | Owning player (-1 = neutral) |

**`FTDBuildingSaveData`** — per-building data (Version 2+):

| Field | Type | Description |
|-------|------|-------------|
| `Coord` | FTDHexCoord | Hex coordinate |
| `BuildingID` | FName | Matches `UTDBuildingDataAsset::BuildingID` for DataAsset lookup |
| `Level` | int32 | Current level (1-based, ClampMin=1) |
| `CurrentHealth` | int32 | Current HP (0 = use default max) |
| `OwnerPlayerIndex` | int32 | Owning player (-1 = neutral) |

**`FTDUnitSaveData`** — per-unit data (Version 2+):

| Field | Type | Description |
|-------|------|-------------|
| `Coord` | FTDHexCoord | Hex coordinate |
| `UnitID` | FName | Matches `UTDUnitDataAsset::UnitID` for DataAsset lookup |
| `CurrentHealth` | int32 | Current HP (0 = use default max) |
| `OwnerPlayerIndex` | int32 | Owning player (-1 = neutral) |

**`FTDHexGridSaveData`** — full map container:

| Field | Type | Description |
|-------|------|-------------|
| `MapRadius` | int32 | Map radius in tiles |
| `Seed` | int32 | Random generation seed |
| `Version` | int32 | Save format version (1 or 2) |
| `TileDataList` | TArray\<FTDHexTileSaveData\> | All tile data |
| `BuildingDataList` | TArray\<FTDBuildingSaveData\> | Building data (Version 2+) |
| `UnitDataList` | TArray\<FTDUnitSaveData\> | Unit data (Version 2+) |

### Save Format Versions

| Version | Content | Notes |
|---------|---------|-------|
| 1 | Tiles only | Original format, terrain data only |
| 2 | Tiles + Buildings + Units | Added BuildingDataList and UnitDataList arrays |

Version 2 is backward compatible — Version 1 JSON files are loaded normally with empty building/unit arrays.

### JSON Format

```json
{
  "MapRadius": 15,
  "Seed": 2016941473,
  "Version": 2,
  "Tiles": [
    { "Q": 0, "R": 0, "TerrainType": "Plain", "HeightLevel": 1, "OwnerPlayerIndex": -1 }
  ],
  "Buildings": [
    { "Q": 3, "R": 0, "BuildingID": "WoodArrowTower", "Level": 1, "CurrentHealth": 0, "OwnerPlayerIndex": 0 }
  ],
  "Units": [
    { "Q": 1, "R": 2, "UnitID": "Swordsman", "CurrentHealth": 0, "OwnerPlayerIndex": 0 }
  ]
}
```

`Buildings` and `Units` arrays are optional — omitted in Version 1 files and parsed with `HasField()` guard for backward compatibility.

### JSON Serialization (TDSaveDataInternal namespace)

| Function | Direction | Description |
|----------|-----------|-------------|
| `TileSaveDataToJson` / `JsonToTileSaveData` | ↔ | Per-tile JSON conversion, terrain type as string |
| `BuildingSaveDataToJson` / `JsonToBuildingSaveData` | ↔ | Per-building JSON conversion, BuildingID as string |
| `UnitSaveDataToJson` / `JsonToUnitSaveData` | ↔ | Per-unit JSON conversion, UnitID as string |
| `GridSaveDataToJson` / `JsonToGridSaveData` | ↔ | Full map JSON, conditionally includes Buildings/Units |

### ID-Based Entity Resolution

Buildings and units are serialized by **FName ID**, not by asset path or object reference:

```
JSON "BuildingID": "WoodArrowTower"
  → caller provides TArray<UTDBuildingDataAsset*>
  → ImportBuildingData builds TMap<FName, UTDBuildingDataAsset*> lookup
  → DataAsset found → DetermineSpawnClass() resolves Blueprint or C++ class
  → SpawnActor with correct class
```

This design decouples save data from asset paths. Renaming or moving a Blueprint does not break existing saves as long as the `BuildingID` / `UnitID` remains unchanged.

### Save Flow

```
Tiles only (Version 1):
  HexGridManager->ExportSaveData() -> FTDHexGridSaveData
    -> UTDHexGridSaveGame::ExportToJsonFile()
    -> UTDMapFileManager::SaveMapToFile()

With entities (Version 2):
  HexGridManager->ExportSaveData() -> FTDHexGridSaveData (Version=2)
  BuildingManager->ExportBuildingData() -> BuildingDataList
  UnitSquad->ExportUnitData() -> UnitDataList
  -> UTDMapFileManager::SaveMapToFileWithEntities() combines all data
```

### Load Flow

```
Tiles only (Version 1):
  UTDMapFileManager::LoadMapFromFile()
    -> UTDHexGridSaveGame::ImportFromJsonFile()
    -> Grid->ApplySaveData() creates/updates tiles

With entities (Version 2):
  UTDMapFileManager::LoadMapFromFileWithEntities()
    -> UTDHexGridSaveGame::ImportFromJsonFile()
    -> Grid->ApplySaveData() restores terrain
    -> BuildingManager->ClearAllBuildings()
    -> BuildingManager->ImportBuildingData(World, Grid, DataList, DataAssets)
       -> per entry: DataAsset lookup → DetermineSpawnClass → SpawnActor
       -> InitializeBuilding → iterative Upgrade() → restore health
       -> type-specific: tower height cache, currency building manager ref
    -> UnitSquad->ClearAllUnits()
    -> UnitSquad->ImportUnitData(World, DataList, DataAssets)
       -> per entry: DataAsset lookup → SpawnActor (UnitActorClass or base)
       -> InitializeUnit → restore health
```

### File Paths

| Path | Method | Description |
|------|--------|-------------|
| `Content/SavedMaps/{MapName}.json` | `SaveMapToFile` / `LoadMapFromFile` | Named map saves |
| `Content/TowerDefend/SerializationMaps/SerializationMaps.json` | `SaveMapToDefaultPath` / `LoadMapFromDefaultPath` | Default serialization path with history rotation |
| `SerializationMaps_01.json` ~ `_09.json` | `RotateHistoryFiles` | Backup history (max 10 files total) |

### Auto-Save

Each settlement phase end auto-saves current map state. Supports disconnect reconnection with terrain state recovery. Server saves authoritative data; client saves cache.
