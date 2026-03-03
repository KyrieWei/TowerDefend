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

### Save Data Structures

- `FTDHexTileSaveData`: Per-tile data (Coord, TerrainType, HeightLevel, bHasBuilding, BuildingID, OwnerPlayerIndex)
- `FTDHexGridSaveData`: Full map data (MapRadius, Seed, TileDataList, Version)

### Save Flow
```
HexGridManager -> iterate all TDHexTile -> collect Coord/Terrain/Height/Building
  -> build FTDHexGridSaveData -> serialize to USaveGame or JSON
```

### Load Flow
```
Read USaveGame or JSON -> deserialize to FTDHexGridSaveData
  -> clear current grid -> iterate TileDataList creating TDHexTile
  -> set terrain type, height, buildings -> refresh visuals (mesh height, material)
```

### Auto-Save
Each settlement phase end auto-saves current map state. Supports disconnect reconnection with terrain state recovery. Server saves authoritative data; client saves cache.
