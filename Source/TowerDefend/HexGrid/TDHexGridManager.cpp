// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDTerrainGenerator.h"
#include "Engine/World.h"

ATDHexGridManager::ATDHexGridManager()
{
    PrimaryActorTick.bCanEverTick = false;

    // 默认 Tile 类
    TileActorClass = ATDHexTile::StaticClass();
}

// ===================================================================
// BeginPlay
// ===================================================================

void ATDHexGridManager::BeginPlay()
{
    Super::BeginPlay();

    EnsureTerrainGenerator();

    GenerateGrid(0);
}

// ===================================================================
// 网格生成 / 清除
// ===================================================================

void ATDHexGridManager::GenerateGrid(int32 Radius)
{
    ClearGrid();

    EnsureTerrainGenerator();

    const int32 EffectiveRadius = (Radius > 0) ? Radius : MapRadius;

    TerrainGenerator->MapRadius = EffectiveRadius;
    TerrainGenerator->bRectangularLayout = bRectangularLayout;
    TerrainGenerator->MapColumns = MapColumns;
    TerrainGenerator->MapRows = MapRows;

    FTDHexGridSaveData GeneratedData = TerrainGenerator->GenerateMap();

    // 保存种子用于后续导出
    LastUsedSeed = GeneratedData.Seed;

    SpawnTilesFromData(GeneratedData);

    CurrentMapRadius = EffectiveRadius;

    UE_LOG(LogTemp, Log,
        TEXT("ATDHexGridManager::GenerateGrid: Generated grid with radius %d, %d tiles."),
        CurrentMapRadius, TileMap.Num());
}

void ATDHexGridManager::ClearGrid()
{
    for (auto& Pair : TileMap)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->Destroy();
        }
    }

    TileMap.Empty();
    CurrentMapRadius = 0;
}

// ===================================================================
// 查询接口
// ===================================================================

ATDHexTile* ATDHexGridManager::GetTileAt(const FTDHexCoord& Coord) const
{
    ATDHexTile* const* FoundTile = TileMap.Find(Coord);

    if (FoundTile && IsValid(*FoundTile))
    {
        return *FoundTile;
    }

    return nullptr;
}

TArray<ATDHexTile*> ATDHexGridManager::GetNeighborTiles(const FTDHexCoord& Coord) const
{
    TArray<ATDHexTile*> Result;
    Result.Reserve(6);

    TArray<FTDHexCoord> Neighbors = Coord.GetAllNeighbors();

    for (const FTDHexCoord& NeighborCoord : Neighbors)
    {
        ATDHexTile* Tile = GetTileAt(NeighborCoord);
        if (Tile)
        {
            Result.Add(Tile);
        }
    }

    return Result;
}

TArray<ATDHexTile*> ATDHexGridManager::GetTilesInRange(const FTDHexCoord& Center, int32 Range) const
{
    TArray<FTDHexCoord> Coords = Center.GetCoordsInRange(Range);

    TArray<ATDHexTile*> Result;
    Result.Reserve(Coords.Num());

    for (const FTDHexCoord& Coord : Coords)
    {
        ATDHexTile* Tile = GetTileAt(Coord);
        if (Tile)
        {
            Result.Add(Tile);
        }
    }

    return Result;
}

int32 ATDHexGridManager::GetTileCount() const
{
    return TileMap.Num();
}

TArray<ATDHexTile*> ATDHexGridManager::GetAllTiles() const
{
    TArray<ATDHexTile*> Result;
    TileMap.GenerateValueArray(Result);
    return Result;
}

// ===================================================================
// 存档接口
// ===================================================================

void ATDHexGridManager::ApplySaveData(const FTDHexGridSaveData& Data)
{
    ClearGrid();

    SpawnTilesFromData(Data);

    CurrentMapRadius = Data.MapRadius;
    LastUsedSeed = Data.Seed;

    // 恢复矩形布局配置
    if (Data.MapColumns > 0 && Data.MapRows > 0)
    {
        bRectangularLayout = true;
        MapColumns = Data.MapColumns;
        MapRows = Data.MapRows;
    }

    UE_LOG(LogTemp, Log,
        TEXT("ATDHexGridManager::ApplySaveData: Applied save data with radius %d, %d tiles."),
        CurrentMapRadius, TileMap.Num());
}

FTDHexGridSaveData ATDHexGridManager::ExportSaveData() const
{
    FTDHexGridSaveData Result;
    Result.MapRadius = CurrentMapRadius;
    Result.Seed = LastUsedSeed;
    Result.Version = 1;
    if (bRectangularLayout)
    {
        Result.MapColumns = MapColumns;
        Result.MapRows = MapRows;
    }
    Result.TileDataList.Reserve(TileMap.Num());

    for (const auto& Pair : TileMap)
    {
        if (IsValid(Pair.Value))
        {
            Result.TileDataList.Add(Pair.Value->ExportSaveData());
        }
    }

    return Result;
}

// ===================================================================
// 内部方法
// ===================================================================

void ATDHexGridManager::SpawnTilesFromData(const FTDHexGridSaveData& Data)
{
    UWorld* World = GetWorld();

    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("ATDHexGridManager::SpawnTilesFromData: World is null."));
        return;
    }

    if (!TileActorClass)
    {
        UE_LOG(LogTemp, Error, TEXT("ATDHexGridManager::SpawnTilesFromData: TileActorClass is null."));
        return;
    }

    TileMap.Reserve(Data.TileDataList.Num());

    FActorSpawnParameters SpawnParams;
    SpawnParams.Owner = this;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (const FTDHexTileSaveData& TileData : Data.TileDataList)
    {
        // 计算世界位置
        FVector SpawnLocation = TileData.Coord.ToWorldPosition(HexSize);
        SpawnLocation.Z = static_cast<float>(TileData.HeightLevel) * ATDHexTile::HeightLevelUnitZ;

        // 加上 Manager 自身的世界偏移
        SpawnLocation += GetActorLocation();

        const FRotator SpawnRotation = FRotator::ZeroRotator;

        ATDHexTile* NewTile = World->SpawnActor<ATDHexTile>(
            TileActorClass,
            SpawnLocation,
            SpawnRotation,
            SpawnParams);

        if (!NewTile)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("ATDHexGridManager::SpawnTilesFromData: Failed to spawn tile at %s."),
                *TileData.Coord.ToString());
            continue;
        }

        NewTile->InitFromSaveData(TileData, HexSize);

        TileMap.Add(TileData.Coord, NewTile);
    }
}

void ATDHexGridManager::EnsureTerrainGenerator()
{
    if (!TerrainGenerator)
    {
        TerrainGenerator = NewObject<UTDTerrainGenerator>(this, TEXT("DefaultTerrainGenerator"));
    }
}
