// Copyright TowerDefend. All Rights Reserved.

#include "Building/TDBuildingManager.h"
#include "Building/TDBuildingBase.h"
#include "Building/TDBuildingDataAsset.h"
#include "Building/TDDefenseTower.h"
#include "Building/TDWall.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDHexCoord.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDBuildingMgr, Log, All);

// ===================================================================
// 放置与移除
// ===================================================================

ATDBuildingBase* UTDBuildingManager::PlaceBuilding(
    UWorld* World,
    UTDBuildingDataAsset* InBuildingData,
    ATDHexGridManager* Grid,
    const FTDHexCoord& InCoord,
    int32 InOwnerPlayerIndex)
{
    // 参数有效性检查
    if (!World)
    {
        UE_LOG(LogTDBuildingMgr, Error,
            TEXT("PlaceBuilding: World is null."));
        return nullptr;
    }

    if (!CanPlaceBuilding(InBuildingData, Grid, InCoord, InOwnerPlayerIndex))
    {
        return nullptr;
    }

    // 获取格子以确定世界坐标和高度
    ATDHexTile* Tile = Grid->GetTileAt(InCoord);
    if (!ensureMsgf(Tile,
            TEXT("PlaceBuilding: Tile at %s passed validation but is null."),
            *InCoord.ToString()))
    {
        return nullptr;
    }

    // 根据建筑类型选择生成的类
    TSubclassOf<ATDBuildingBase> SpawnClass = DetermineSpawnClass(InBuildingData);

    // 计算放置位置：格子世界位置 + 高度偏移
    const FVector TileLocation = Tile->GetActorLocation();
    const FVector SpawnLocation(
        TileLocation.X,
        TileLocation.Y,
        TileLocation.Z + ATDHexTile::HeightLevelUnitZ * 0.5f);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ATDBuildingBase* NewBuilding = World->SpawnActor<ATDBuildingBase>(
        SpawnClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);

    if (!ensureMsgf(NewBuilding,
            TEXT("PlaceBuilding: Failed to spawn building actor.")))
    {
        return nullptr;
    }

    // 初始化建筑数据
    NewBuilding->InitializeBuilding(InBuildingData, InCoord, InOwnerPlayerIndex);

    // 为防御塔设置高度缓存
    ATDDefenseTower* Tower = Cast<ATDDefenseTower>(NewBuilding);
    if (Tower)
    {
        Tower->SetCachedHeightLevel(Tile->GetHeightLevel());
    }

    // 注册到映射表
    BuildingMap.Add(InCoord, NewBuilding);

    UE_LOG(LogTDBuildingMgr, Log,
        TEXT("Placed building '%s' at %s for player %d."),
        *InBuildingData->BuildingID.ToString(),
        *InCoord.ToString(),
        InOwnerPlayerIndex);

    return NewBuilding;
}

bool UTDBuildingManager::CanPlaceBuilding(
    const UTDBuildingDataAsset* InBuildingData,
    const ATDHexGridManager* Grid,
    const FTDHexCoord& InCoord,
    int32 InOwnerPlayerIndex) const
{
    // 1. 参数有效性
    if (!InBuildingData)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: BuildingData is null."));
        return false;
    }

    if (!Grid)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: Grid is null."));
        return false;
    }

    if (!InCoord.IsValid())
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: Invalid coordinate."));
        return false;
    }

    // 2. 目标格子存在
    ATDHexTile* Tile = Grid->GetTileAt(InCoord);
    if (!Tile)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: No tile at %s."),
            *InCoord.ToString());
        return false;
    }

    // 3. 格子上没有已有建筑
    if (BuildingMap.Contains(InCoord))
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: Tile at %s already has a building."),
            *InCoord.ToString());
        return false;
    }

    // 4. 格子可建造
    if (!Tile->IsBuildable())
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: Tile at %s is not buildable."),
            *InCoord.ToString());
        return false;
    }

    // 5. 格子归属检查（格子属于建造者或为中立）
    const int32 TileOwner = Tile->GetOwnerPlayerIndex();
    if (TileOwner != -1 && TileOwner != InOwnerPlayerIndex)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: Tile at %s belongs to player %d, "
                 "not player %d."),
            *InCoord.ToString(), TileOwner, InOwnerPlayerIndex);
        return false;
    }

    // 6. 建筑数据的地形和高度限制
    if (!InBuildingData->CanBuildOnTerrain(
            Tile->GetTerrainType(), Tile->GetHeightLevel()))
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("CanPlaceBuilding: Building '%s' cannot be built on "
                 "terrain=%d, height=%d at %s."),
            *InBuildingData->BuildingID.ToString(),
            static_cast<uint8>(Tile->GetTerrainType()),
            Tile->GetHeightLevel(),
            *InCoord.ToString());
        return false;
    }

    return true;
}

bool UTDBuildingManager::RemoveBuilding(const FTDHexCoord& InCoord)
{
    TObjectPtr<ATDBuildingBase> RemovedBuilding;
    if (!BuildingMap.RemoveAndCopyValue(InCoord, RemovedBuilding))
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("RemoveBuilding: No building at %s."),
            *InCoord.ToString());
        return false;
    }

    if (IsValid(RemovedBuilding))
    {
        UE_LOG(LogTDBuildingMgr, Log,
            TEXT("Removed building at %s."),
            *InCoord.ToString());
        RemovedBuilding->Destroy();
    }

    return true;
}

// ===================================================================
// 查询
// ===================================================================

ATDBuildingBase* UTDBuildingManager::GetBuildingAt(
    const FTDHexCoord& InCoord) const
{
    const TObjectPtr<ATDBuildingBase>* Found = BuildingMap.Find(InCoord);
    if (!Found)
    {
        return nullptr;
    }
    return Found->Get();
}

TArray<ATDBuildingBase*> UTDBuildingManager::GetBuildingsByOwner(
    int32 InOwnerPlayerIndex) const
{
    TArray<ATDBuildingBase*> Result;

    for (const auto& Pair : BuildingMap)
    {
        ATDBuildingBase* Building = Pair.Value.Get();
        if (IsValid(Building)
            && Building->GetOwnerPlayerIndex() == InOwnerPlayerIndex)
        {
            Result.Add(Building);
        }
    }

    return Result;
}

TArray<ATDBuildingBase*> UTDBuildingManager::GetBuildingsInRange(
    const FTDHexCoord& Center, int32 Range) const
{
    TArray<ATDBuildingBase*> Result;

    // 遍历范围内的所有坐标
    const TArray<FTDHexCoord> CoordsInRange = Center.GetCoordsInRange(Range);
    for (const FTDHexCoord& Coord : CoordsInRange)
    {
        ATDBuildingBase* Building = GetBuildingAt(Coord);
        if (Building)
        {
            Result.Add(Building);
        }
    }

    return Result;
}

int32 UTDBuildingManager::GetBuildingCount() const
{
    return BuildingMap.Num();
}

int32 UTDBuildingManager::GetBuildingCountByOwner(
    int32 InOwnerPlayerIndex) const
{
    int32 Count = 0;

    for (const auto& Pair : BuildingMap)
    {
        const ATDBuildingBase* Building = Pair.Value.Get();
        if (IsValid(Building)
            && Building->GetOwnerPlayerIndex() == InOwnerPlayerIndex)
        {
            Count++;
        }
    }

    return Count;
}

// ===================================================================
// 经济查询
// ===================================================================

int32 UTDBuildingManager::CalculateTotalGoldIncome(
    int32 InOwnerPlayerIndex) const
{
    int32 TotalGold = 0;

    for (const auto& Pair : BuildingMap)
    {
        const ATDBuildingBase* Building = Pair.Value.Get();
        if (IsValid(Building)
            && Building->GetOwnerPlayerIndex() == InOwnerPlayerIndex
            && !Building->IsDestroyed())
        {
            TotalGold += Building->GetGoldPerRound();
        }
    }

    return TotalGold;
}

int32 UTDBuildingManager::CalculateTotalResearchIncome(
    int32 InOwnerPlayerIndex) const
{
    int32 TotalResearch = 0;

    for (const auto& Pair : BuildingMap)
    {
        const ATDBuildingBase* Building = Pair.Value.Get();
        if (IsValid(Building)
            && Building->GetOwnerPlayerIndex() == InOwnerPlayerIndex
            && !Building->IsDestroyed())
        {
            TotalResearch += Building->GetResearchPerRound();
        }
    }

    return TotalResearch;
}

// ===================================================================
// 清空
// ===================================================================

void UTDBuildingManager::ClearAllBuildings()
{
    for (auto& Pair : BuildingMap)
    {
        if (IsValid(Pair.Value))
        {
            Pair.Value->Destroy();
        }
    }

    BuildingMap.Empty();

    UE_LOG(LogTDBuildingMgr, Log,
        TEXT("All buildings cleared."));
}

// ===================================================================
// 内部方法
// ===================================================================

TSubclassOf<ATDBuildingBase> UTDBuildingManager::DetermineSpawnClass(
    const UTDBuildingDataAsset* InBuildingData) const
{
    if (!InBuildingData)
    {
        return ATDBuildingBase::StaticClass();
    }

    switch (InBuildingData->BuildingType)
    {
    case ETDBuildingType::ArrowTower:
    case ETDBuildingType::CannonTower:
        return ATDDefenseTower::StaticClass();

    case ETDBuildingType::Wall:
        return ATDWall::StaticClass();

    default:
        return ATDBuildingBase::StaticClass();
    }
}
