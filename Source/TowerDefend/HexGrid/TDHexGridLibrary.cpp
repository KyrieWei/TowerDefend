// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexGridLibrary.h"

#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDHexGridManager.h"
#include "Core/TDBlueprintLibrary.h"

// ===============================================================
//  地块几何常量
// ===============================================================

float UTDHexGridLibrary::GetDefaultHexSize()
{
    return 100.0f;
}

float UTDHexGridLibrary::GetHeightLevelUnitZ()
{
    return ATDHexTile::HeightLevelUnitZ;
}

int32 UTDHexGridLibrary::GetMinHeightLevel()
{
    return ATDHexTile::MinHeightLevel;
}

int32 UTDHexGridLibrary::GetMaxHeightLevel()
{
    return ATDHexTile::MaxHeightLevel;
}

int32 UTDHexGridLibrary::GetMaxNeighborHeightDiff()
{
    // UTDTerrainModifier::MaxNeighborHeightDiff 为 private，
    // 此处直接使用相同常量值 3。
    return 3;
}

// ===============================================================
//  地形类型辅助
// ===============================================================

FText UTDHexGridLibrary::GetTerrainTypeDisplayName(ETDTerrainType TerrainType)
{
    switch (TerrainType)
    {
    case ETDTerrainType::Plain:
        return NSLOCTEXT("TD", "Terrain_Plain", "平原");
    case ETDTerrainType::Hill:
        return NSLOCTEXT("TD", "Terrain_Hill", "丘陵");
    case ETDTerrainType::Mountain:
        return NSLOCTEXT("TD", "Terrain_Mountain", "山地");
    case ETDTerrainType::Forest:
        return NSLOCTEXT("TD", "Terrain_Forest", "森林");
    case ETDTerrainType::River:
        return NSLOCTEXT("TD", "Terrain_River", "河流");
    case ETDTerrainType::Swamp:
        return NSLOCTEXT("TD", "Terrain_Swamp", "沼泽");
    case ETDTerrainType::DeepWater:
        return NSLOCTEXT("TD", "Terrain_DeepWater", "深水");
    default:
        return NSLOCTEXT("TD", "Terrain_Unknown", "未知");
    }
}

TArray<ETDTerrainType> UTDHexGridLibrary::GetAllTerrainTypes()
{
    TArray<ETDTerrainType> Result;
    const uint8 MaxValue = static_cast<uint8>(ETDTerrainType::MAX);

    Result.Reserve(MaxValue);
    for (uint8 Index = 0; Index < MaxValue; ++Index)
    {
        Result.Add(static_cast<ETDTerrainType>(Index));
    }

    return Result;
}

bool UTDHexGridLibrary::IsTerrainPassable(ETDTerrainType TerrainType)
{
    return TerrainType != ETDTerrainType::Mountain
        && TerrainType != ETDTerrainType::DeepWater;
}

bool UTDHexGridLibrary::IsTerrainBuildable(ETDTerrainType TerrainType)
{
    return TerrainType != ETDTerrainType::Mountain
        && TerrainType != ETDTerrainType::DeepWater
        && TerrainType != ETDTerrainType::Swamp
        && TerrainType != ETDTerrainType::River;
}

// ===============================================================
//  地图配置查询
// ===============================================================

int32 UTDHexGridLibrary::GetCurrentMapRadius(const UObject* WorldContextObject)
{
    const ATDHexGridManager* Grid = UTDBlueprintLibrary::GetHexGridManager(WorldContextObject);
    return Grid ? Grid->GetCurrentMapRadius() : 0;
}

int32 UTDHexGridLibrary::GetCurrentMapColumns(const UObject* WorldContextObject)
{
    const ATDHexGridManager* Grid = UTDBlueprintLibrary::GetHexGridManager(WorldContextObject);
    return Grid ? Grid->MapColumns : 0;
}

int32 UTDHexGridLibrary::GetCurrentMapRows(const UObject* WorldContextObject)
{
    const ATDHexGridManager* Grid = UTDBlueprintLibrary::GetHexGridManager(WorldContextObject);
    return Grid ? Grid->MapRows : 0;
}

bool UTDHexGridLibrary::IsRectangularLayout(const UObject* WorldContextObject)
{
    const ATDHexGridManager* Grid = UTDBlueprintLibrary::GetHexGridManager(WorldContextObject);
    return Grid ? Grid->bRectangularLayout : false;
}

float UTDHexGridLibrary::GetCurrentHexSize(const UObject* WorldContextObject)
{
    const ATDHexGridManager* Grid = UTDBlueprintLibrary::GetHexGridManager(WorldContextObject);
    return Grid ? Grid->GetHexSize() : 0.0f;
}

int32 UTDHexGridLibrary::GetCurrentTileCount(const UObject* WorldContextObject)
{
    const ATDHexGridManager* Grid = UTDBlueprintLibrary::GetHexGridManager(WorldContextObject);
    return Grid ? Grid->GetTileCount() : 0;
}
