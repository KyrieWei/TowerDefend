// Copyright TowerDefend. All Rights Reserved.

#include "Building/TDCurrencyBuilding.h"
#include "Building/TDBuildingDataAsset.h"
#include "Building/TDBuildingManager.h"
#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDCurrencyBuilding, Log, All);

// ===================================================================
// 构造函数
// ===================================================================

ATDCurrencyBuilding::ATDCurrencyBuilding()
{
}

// ===================================================================
// Override from ATDBuildingBase
// ===================================================================

bool ATDCurrencyBuilding::CanAttack() const
{
    return false;
}

int32 ATDCurrencyBuilding::GetGoldPerRound() const
{
    return GetBaseGoldPerRound() + GetSynergyBonusGold();
}

bool ATDCurrencyBuilding::Upgrade()
{
    if (!CanUpgrade())
    {
        UE_LOG(LogTDCurrencyBuilding, Warning,
            TEXT("ATDCurrencyBuilding::Upgrade: Cannot upgrade. "
                 "CurrentLevel=%d, MaxLevel=%d"),
            GetCurrentLevel(),
            GetBuildingData() ? GetBuildingData()->MaxLevel : 0);
        return false;
    }

    const int32 OldLevel = GetCurrentLevel();

    // 调用基类升级逻辑（CurrentLevel++）
    if (!Super::Upgrade())
    {
        return false;
    }

    // 应用新等级的视觉和属性
    UpdateLevelVisualAndStats();

    // 广播升级事件
    OnCurrencyBuildingUpgraded.Broadcast(this, OldLevel, GetCurrentLevel());

    UE_LOG(LogTDCurrencyBuilding, Log,
        TEXT("Currency building upgraded: Lv%d -> Lv%d at %s."),
        OldLevel, GetCurrentLevel(), *GetCoord().ToString());

    return true;
}

// ===================================================================
// 外部引用注入
// ===================================================================

void ATDCurrencyBuilding::SetBuildingManager(
    UTDBuildingManager* InBuildingManager)
{
    BuildingManagerRef = InBuildingManager;
}

void ATDCurrencyBuilding::ApplyLevelData()
{
    UpdateLevelVisualAndStats();
}

// ===================================================================
// 查询
// ===================================================================

FText ATDCurrencyBuilding::GetCurrentLevelDisplayName() const
{
    const FTDCurrencyLevelData* Data = GetCurrentLevelData();
    if (Data)
    {
        return Data->LevelDisplayName;
    }
    return FText::FromString(TEXT("Unknown"));
}

int32 ATDCurrencyBuilding::GetBaseGoldPerRound() const
{
    const FTDCurrencyLevelData* Data = GetCurrentLevelData();
    if (Data)
    {
        return Data->GoldPerRound;
    }

    // 回退到 DataAsset 的 GoldPerRound
    if (GetBuildingData())
    {
        return GetBuildingData()->GoldPerRound;
    }
    return 0;
}

int32 ATDCurrencyBuilding::GetSynergyBonusGold() const
{
    if (AdjacentSynergyBonusPercent <= 0)
    {
        return 0;
    }

    const int32 BaseGold = GetBaseGoldPerRound();
    if (BaseGold <= 0)
    {
        return 0;
    }

    const int32 AdjacentCount = CountAdjacentCurrencyBuildings();
    if (AdjacentCount <= 0)
    {
        return 0;
    }

    // 加成 = 基础产出 * 相邻数量 * 加成百分比 / 100
    return FMath::FloorToInt32(
        static_cast<float>(BaseGold * AdjacentCount
            * AdjacentSynergyBonusPercent) / 100.0f);
}

// ===================================================================
// 内部方法
// ===================================================================

void ATDCurrencyBuilding::UpdateLevelVisualAndStats()
{
    const FTDCurrencyLevelData* Data = GetCurrentLevelData();
    if (!Data)
    {
        UE_LOG(LogTDCurrencyBuilding, Warning,
            TEXT("UpdateLevelVisualAndStats: No level data for level %d."),
            GetCurrentLevel());
        return;
    }

    // 更新模型
    if (BuildingMeshComponent && Data->LevelMesh)
    {
        BuildingMeshComponent->SetStaticMesh(Data->LevelMesh);

        UE_LOG(LogTDCurrencyBuilding, Log,
            TEXT("Updated mesh for level %d."),
            GetCurrentLevel());
    }

    // 更新生命值：设置为新等级的最大值
    CurrentHealth = Data->MaxHealth;

    UE_LOG(LogTDCurrencyBuilding, Log,
        TEXT("Level %d applied: HP=%d, GoldPerRound=%d, Name='%s'."),
        GetCurrentLevel(),
        Data->MaxHealth,
        Data->GoldPerRound,
        *Data->LevelDisplayName.ToString());
}

const FTDCurrencyLevelData* ATDCurrencyBuilding::GetCurrentLevelData() const
{
    // CurrentLevel 是 1-based，LevelDataArray 是 0-based
    const int32 Index = GetCurrentLevel() - 1;
    if (!LevelDataArray.IsValidIndex(Index))
    {
        return nullptr;
    }
    return &LevelDataArray[Index];
}

int32 ATDCurrencyBuilding::CountAdjacentCurrencyBuildings() const
{
    if (!BuildingManagerRef.IsValid())
    {
        return 0;
    }

    const TArray<FTDHexCoord> Neighbors = GetCoord().GetAllNeighbors();
    int32 Count = 0;

    for (const FTDHexCoord& NeighborCoord : Neighbors)
    {
        ATDBuildingBase* NeighborBuilding =
            BuildingManagerRef->GetBuildingAt(NeighborCoord);

        if (!NeighborBuilding || NeighborBuilding == this)
        {
            continue;
        }

        // 同阵营、未被摧毁、且为资源建筑类型
        if (NeighborBuilding->GetOwnerPlayerIndex() == GetOwnerPlayerIndex()
            && !NeighborBuilding->IsDestroyed()
            && NeighborBuilding->GetBuildingType()
                == ETDBuildingType::ResourceBuilding)
        {
            Count++;
        }
    }

    return Count;
}
