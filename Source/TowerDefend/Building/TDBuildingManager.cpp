// Copyright TowerDefend. All Rights Reserved.

#include "Building/TDBuildingManager.h"
#include "Building/TDBuildingBase.h"
#include "Building/TDBuildingDataAsset.h"
#include "Building/TDDefenseTower.h"
#include "Building/TDWall.h"
#include "Building/TDCurrencyBuilding.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDHexCoord.h"
#include "TechTree/TDTechTreeIntegration.h"
#include "Core/TDPlayerState.h"
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

    // 为货币建筑注入管理器引用并应用等级数据
    ATDCurrencyBuilding* CurrencyBuilding =
        Cast<ATDCurrencyBuilding>(NewBuilding);
    if (CurrencyBuilding)
    {
        CurrencyBuilding->SetBuildingManager(this);
        CurrencyBuilding->ApplyLevelData();
    }

    // 注册到映射表
    BuildingMap.Add(InCoord, NewBuilding);

    UE_LOG(LogTDBuildingMgr, Log,
        TEXT("Placed building '%s' at %s for player %d."),
        *InBuildingData->BuildingID.ToString(),
        *InCoord.ToString(),
        InOwnerPlayerIndex);

    OnBuildingPlaced.Broadcast(NewBuilding, InCoord, InOwnerPlayerIndex);

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

    // 7. Tech tree unlock check
    if (TechIntegration && InBuildingData)
    {
        if (!TechIntegration->IsBuildingUnlockedForPlayer(InOwnerPlayerIndex, InBuildingData->BuildingID))
        {
            UE_LOG(LogTDBuildingMgr, Warning,
                TEXT("CanPlaceBuilding: Building '%s' not unlocked for player %d."),
                *InBuildingData->BuildingID.ToString(), InOwnerPlayerIndex);
            return false;
        }
    }

    return true;
}

void UTDBuildingManager::SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration)
{
    TechIntegration = InTechIntegration;
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
// 拆除（带退款）
// ===================================================================

bool UTDBuildingManager::DemolishBuilding(
    const FTDHexCoord& InCoord,
    ATDPlayerState* InPlayer,
    int32 RefundPercent)
{
    if (!InPlayer)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("DemolishBuilding: Player is null."));
        return false;
    }

    ATDBuildingBase* Building = GetBuildingAt(InCoord);
    if (!Building)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("DemolishBuilding: No building at %s."),
            *InCoord.ToString());
        return false;
    }

    // 验证归属
    if (Building->GetOwnerPlayerIndex() != InPlayer->GetPlayerId())
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("DemolishBuilding: Building at %s belongs to player %d, "
                 "not player %d."),
            *InCoord.ToString(),
            Building->GetOwnerPlayerIndex(),
            InPlayer->GetPlayerId());
        return false;
    }

    // 计算退款
    RefundPercent = FMath::Clamp(RefundPercent, 0, 100);
    int32 RefundGold = 0;
    const UTDBuildingDataAsset* BuildingData = Building->GetBuildingData();
    if (BuildingData)
    {
        RefundGold = FMath::FloorToInt32(
            static_cast<float>(BuildingData->GoldCost * RefundPercent)
            / 100.0f);
    }

    const int32 OwnerIndex = Building->GetOwnerPlayerIndex();

    // 退款
    if (RefundGold > 0)
    {
        InPlayer->AddGold(RefundGold);
    }

    // 移除建筑
    RemoveBuilding(InCoord);

    // 广播拆除事件
    OnBuildingDemolished.Broadcast(InCoord, OwnerIndex, RefundGold);

    UE_LOG(LogTDBuildingMgr, Log,
        TEXT("Demolished building at %s, refunded %d gold to player %d."),
        *InCoord.ToString(), RefundGold, OwnerIndex);

    return true;
}

// ===================================================================
// 升级（带扣费）
// ===================================================================

bool UTDBuildingManager::UpgradeBuilding(
    const FTDHexCoord& InCoord,
    ATDPlayerState* InPlayer)
{
    if (!InPlayer)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("UpgradeBuilding: Player is null."));
        return false;
    }

    ATDBuildingBase* Building = GetBuildingAt(InCoord);
    if (!Building)
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("UpgradeBuilding: No building at %s."),
            *InCoord.ToString());
        return false;
    }

    // 验证归属
    if (Building->GetOwnerPlayerIndex() != InPlayer->GetPlayerId())
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("UpgradeBuilding: Building at %s belongs to player %d, "
                 "not player %d."),
            *InCoord.ToString(),
            Building->GetOwnerPlayerIndex(),
            InPlayer->GetPlayerId());
        return false;
    }

    if (!Building->CanUpgrade())
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("UpgradeBuilding: Building at %s cannot upgrade."),
            *InCoord.ToString());
        return false;
    }

    const int32 UpgradeCost = Building->GetUpgradeCost();
    if (!InPlayer->CanAfford(UpgradeCost))
    {
        UE_LOG(LogTDBuildingMgr, Warning,
            TEXT("UpgradeBuilding: Player %d cannot afford %d gold."),
            InPlayer->GetPlayerId(), UpgradeCost);
        return false;
    }

    // 扣费并升级
    InPlayer->SpendGold(UpgradeCost);
    Building->Upgrade();

    UE_LOG(LogTDBuildingMgr, Log,
        TEXT("Upgraded building at %s to level %d, cost %d gold."),
        *InCoord.ToString(),
        Building->GetCurrentLevel(),
        UpgradeCost);

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
// 存档
// ===================================================================

TArray<FTDBuildingSaveData> UTDBuildingManager::ExportBuildingData() const
{
    TArray<FTDBuildingSaveData> Result;
    Result.Reserve(BuildingMap.Num());

    for (const auto& Pair : BuildingMap)
    {
        const ATDBuildingBase* Building = Pair.Value.Get();
        if (!IsValid(Building))
        {
            continue;
        }

        const UTDBuildingDataAsset* Data = Building->GetBuildingData();
        if (!Data)
        {
            continue;
        }

        FTDBuildingSaveData SaveData;
        SaveData.Coord = Pair.Key;
        SaveData.BuildingID = Data->BuildingID;
        SaveData.Level = Building->GetCurrentLevel();
        SaveData.CurrentHealth = Building->GetCurrentHealth();
        SaveData.OwnerPlayerIndex = Building->GetOwnerPlayerIndex();

        Result.Add(SaveData);
    }

    return Result;
}

int32 UTDBuildingManager::ImportBuildingData(
    UWorld* World,
    ATDHexGridManager* Grid,
    const TArray<FTDBuildingSaveData>& InBuildingDataList,
    const TArray<UTDBuildingDataAsset*>& InDataAssets)
{
    if (!World || !Grid)
    {
        UE_LOG(LogTDBuildingMgr, Error,
            TEXT("ImportBuildingData: World or Grid is null."));
        return 0;
    }

    // 构建 BuildingID -> DataAsset 查找表
    TMap<FName, UTDBuildingDataAsset*> DataAssetLookup;
    for (UTDBuildingDataAsset* Asset : InDataAssets)
    {
        if (Asset)
        {
            DataAssetLookup.Add(Asset->BuildingID, Asset);
        }
    }

    int32 RestoredCount = 0;

    for (const FTDBuildingSaveData& SaveData : InBuildingDataList)
    {
        UTDBuildingDataAsset** FoundAsset = DataAssetLookup.Find(SaveData.BuildingID);
        if (!FoundAsset || !(*FoundAsset))
        {
            UE_LOG(LogTDBuildingMgr, Warning,
                TEXT("ImportBuildingData: Unknown BuildingID '%s', skipping."),
                *SaveData.BuildingID.ToString());
            continue;
        }

        // 跳过放置验证直接生成（存档数据视为可信）
        ATDHexTile* Tile = Grid->GetTileAt(SaveData.Coord);
        if (!Tile)
        {
            UE_LOG(LogTDBuildingMgr, Warning,
                TEXT("ImportBuildingData: No tile at %s for building '%s', skipping."),
                *SaveData.Coord.ToString(),
                *SaveData.BuildingID.ToString());
            continue;
        }

        TSubclassOf<ATDBuildingBase> SpawnClass = DetermineSpawnClass(*FoundAsset);

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

        if (!NewBuilding)
        {
            UE_LOG(LogTDBuildingMgr, Warning,
                TEXT("ImportBuildingData: Failed to spawn building '%s' at %s."),
                *SaveData.BuildingID.ToString(),
                *SaveData.Coord.ToString());
            continue;
        }

        // 初始化
        NewBuilding->InitializeBuilding(
            *FoundAsset, SaveData.Coord, SaveData.OwnerPlayerIndex);

        // 恢复等级（逐级升级以触发子类逻辑）
        for (int32 i = 1; i < SaveData.Level; ++i)
        {
            NewBuilding->Upgrade();
        }

        // 恢复生命值（存档值 > 0 时覆盖默认值）
        if (SaveData.CurrentHealth > 0)
        {
            NewBuilding->SetCurrentHealth(SaveData.CurrentHealth);
        }

        // 防御塔高度缓存
        ATDDefenseTower* Tower = Cast<ATDDefenseTower>(NewBuilding);
        if (Tower)
        {
            Tower->SetCachedHeightLevel(Tile->GetHeightLevel());
        }

        // 货币建筑管理器引用
        ATDCurrencyBuilding* CurrencyBuilding =
            Cast<ATDCurrencyBuilding>(NewBuilding);
        if (CurrencyBuilding)
        {
            CurrencyBuilding->SetBuildingManager(this);
        }

        // 注册到映射表
        BuildingMap.Add(SaveData.Coord, NewBuilding);

        RestoredCount++;
    }

    UE_LOG(LogTDBuildingMgr, Log,
        TEXT("ImportBuildingData: Restored %d/%d buildings."),
        RestoredCount, InBuildingDataList.Num());

    return RestoredCount;
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

    // 优先使用 DataAsset 上配置的蓝图类
    if (InBuildingData->BuildingActorClass)
    {
        return InBuildingData->BuildingActorClass;
    }

    // 回退：按建筑类型推断 C++ 基类
    switch (InBuildingData->BuildingType)
    {
    case ETDBuildingType::ArrowTower:
    case ETDBuildingType::CannonTower:
    case ETDBuildingType::MageTower:
        return ATDDefenseTower::StaticClass();

    case ETDBuildingType::Wall:
        return ATDWall::StaticClass();

    case ETDBuildingType::ResourceBuilding:
        return ATDCurrencyBuilding::StaticClass();

    default:
        return ATDBuildingBase::StaticClass();
    }
}
