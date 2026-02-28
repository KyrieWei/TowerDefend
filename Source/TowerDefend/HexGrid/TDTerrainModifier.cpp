// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDTerrainModifier.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"

// ===================================================================
// 高度修改
// ===================================================================

bool UTDTerrainModifier::RaiseTerrain(ATDHexGridManager* Grid, const FTDHexCoord& Coord, int32 Amount)
{
    if (!Grid)
    {
        UE_LOG(LogTemp, Warning, TEXT("UTDTerrainModifier::RaiseTerrain: Grid is null."));
        return false;
    }

    // 钳制为单次最大变化量
    const int32 ClampedAmount = FMath::Clamp(Amount, 1, MaxSingleHeightChange);

    if (!ValidateModification(Grid, Coord, ClampedAmount))
    {
        return false;
    }

    return ApplyHeightChange(Grid, Coord, ClampedAmount);
}

bool UTDTerrainModifier::LowerTerrain(ATDHexGridManager* Grid, const FTDHexCoord& Coord, int32 Amount)
{
    if (!Grid)
    {
        UE_LOG(LogTemp, Warning, TEXT("UTDTerrainModifier::LowerTerrain: Grid is null."));
        return false;
    }

    const int32 ClampedAmount = FMath::Clamp(Amount, 1, MaxSingleHeightChange);

    if (!ValidateModification(Grid, Coord, -ClampedAmount))
    {
        return false;
    }

    return ApplyHeightChange(Grid, Coord, -ClampedAmount);
}

// ===================================================================
// 地形类型修改
// ===================================================================

bool UTDTerrainModifier::ChangeTerrainType(ATDHexGridManager* Grid,
    const FTDHexCoord& Coord, ETDTerrainType NewType)
{
    if (!Grid)
    {
        UE_LOG(LogTemp, Warning, TEXT("UTDTerrainModifier::ChangeTerrainType: Grid is null."));
        return false;
    }

    ATDHexTile* Tile = Grid->GetTileAt(Coord);

    if (!Tile)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDTerrainModifier::ChangeTerrainType: No tile at %s."), *Coord.ToString());
        return false;
    }

    Tile->SetTerrainType(NewType);
    return true;
}

// ===================================================================
// 验证
// ===================================================================

bool UTDTerrainModifier::ValidateModification(const ATDHexGridManager* Grid,
    const FTDHexCoord& Coord, int32 HeightDelta) const
{
    if (!Grid)
    {
        return false;
    }

    // 检查目标格子是否存在
    ATDHexTile* Tile = Grid->GetTileAt(Coord);
    if (!Tile)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("UTDTerrainModifier::ValidateModification: No tile at %s."), *Coord.ToString());
        return false;
    }

    // 计算新高度
    const int32 CurrentHeight = Tile->GetHeightLevel();
    const int32 NewHeight = CurrentHeight + HeightDelta;

    // 范围检查
    if (NewHeight < ATDHexTile::MinHeightLevel || NewHeight > ATDHexTile::MaxHeightLevel)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("UTDTerrainModifier::ValidateModification: Height %d out of range [%d, %d] at %s."),
            NewHeight, ATDHexTile::MinHeightLevel, ATDHexTile::MaxHeightLevel, *Coord.ToString());
        return false;
    }

    // 检查与所有邻居的高度差约束
    TArray<ATDHexTile*> Neighbors = Grid->GetNeighborTiles(Coord);

    for (const ATDHexTile* Neighbor : Neighbors)
    {
        if (!Neighbor)
        {
            continue;
        }

        const int32 NeighborHeight = Neighbor->GetHeightLevel();
        const int32 HeightDiff = FMath::Abs(NewHeight - NeighborHeight);

        if (HeightDiff > MaxNeighborHeightDiff)
        {
            UE_LOG(LogTemp, Verbose,
                TEXT("UTDTerrainModifier::ValidateModification: "
                     "Height diff %d exceeds max %d between %s (height %d) and neighbor %s (height %d)."),
                HeightDiff, MaxNeighborHeightDiff,
                *Coord.ToString(), NewHeight,
                *Neighbor->GetCoord().ToString(), NeighborHeight);
            return false;
        }
    }

    return true;
}

// ===================================================================
// 内部实现
// ===================================================================

bool UTDTerrainModifier::ApplyHeightChange(ATDHexGridManager* Grid,
    const FTDHexCoord& Coord, int32 HeightDelta)
{
    ATDHexTile* Tile = Grid->GetTileAt(Coord);

    if (!Tile)
    {
        return false;
    }

    const int32 NewHeight = Tile->GetHeightLevel() + HeightDelta;
    Tile->SetHeightLevel(NewHeight);

    // 高度变化后可能需要更新地形类型
    // 例如：升到 3 → 山地，降到 -2 → 深水
    if (NewHeight >= ATDHexTile::MaxHeightLevel)
    {
        Tile->SetTerrainType(ETDTerrainType::Mountain);
    }
    else if (NewHeight <= ATDHexTile::MinHeightLevel)
    {
        Tile->SetTerrainType(ETDTerrainType::DeepWater);
    }

    UE_LOG(LogTemp, Log,
        TEXT("UTDTerrainModifier: Height changed at %s by %d to %d."),
        *Coord.ToString(), HeightDelta, NewHeight);

    return true;
}
