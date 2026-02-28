// Copyright TowerDefend. All Rights Reserved.

#include "Building/TDBuildingBase.h"
#include "Building/TDBuildingDataAsset.h"
#include "Components/StaticMeshComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDBuilding, Log, All);

// ===================================================================
// 构造函数
// ===================================================================

ATDBuildingBase::ATDBuildingBase()
{
    PrimaryActorTick.bCanEverTick = false;

    // 建筑 Mesh 作为根组件
    BuildingMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(
        TEXT("BuildingMesh"));
    SetRootComponent(BuildingMeshComponent);

    // 建筑不参与物理模拟
    BuildingMeshComponent->SetSimulatePhysics(false);
    BuildingMeshComponent->SetCollisionProfileName(TEXT("BlockAll"));
}

// ===================================================================
// 初始化
// ===================================================================

void ATDBuildingBase::InitializeBuilding(
    UTDBuildingDataAsset* InData,
    const FTDHexCoord& InCoord,
    int32 InOwner)
{
    if (!ensureMsgf(InData,
            TEXT("ATDBuildingBase::InitializeBuilding: InData is null.")))
    {
        return;
    }

    BuildingData = InData;
    Coord = InCoord;
    OwnerPlayerIndex = InOwner;
    CurrentLevel = 1;
    CurrentHealth = InData->MaxHealth;

    UpdateVisualMesh();
}

// ===================================================================
// Getters
// ===================================================================

ETDBuildingType ATDBuildingBase::GetBuildingType() const
{
    if (!BuildingData)
    {
        return ETDBuildingType::None;
    }
    return BuildingData->BuildingType;
}

int32 ATDBuildingBase::GetMaxHealth() const
{
    if (!BuildingData)
    {
        return 0;
    }
    return BuildingData->MaxHealth;
}

// ===================================================================
// 升级
// ===================================================================

bool ATDBuildingBase::CanUpgrade() const
{
    if (!BuildingData)
    {
        return false;
    }
    return CurrentLevel < BuildingData->MaxLevel;
}

int32 ATDBuildingBase::GetUpgradeCost() const
{
    if (!BuildingData || !CanUpgrade())
    {
        return 0;
    }
    return BuildingData->GetUpgradeCost(CurrentLevel);
}

bool ATDBuildingBase::Upgrade()
{
    if (!CanUpgrade())
    {
        UE_LOG(LogTDBuilding, Warning,
            TEXT("ATDBuildingBase::Upgrade: Cannot upgrade. "
                 "CurrentLevel=%d, MaxLevel=%d"),
            CurrentLevel,
            BuildingData ? BuildingData->MaxLevel : 0);
        return false;
    }

    CurrentLevel++;

    UE_LOG(LogTDBuilding, Log,
        TEXT("Building '%s' upgraded to level %d."),
        BuildingData ? *BuildingData->BuildingID.ToString() : TEXT("Unknown"),
        CurrentLevel);

    return true;
}

// ===================================================================
// 受伤与销毁
// ===================================================================

void ATDBuildingBase::ApplyDamage(int32 Damage)
{
    if (Damage <= 0)
    {
        UE_LOG(LogTDBuilding, Warning,
            TEXT("ATDBuildingBase::ApplyDamage: Damage must be > 0, got %d."),
            Damage);
        return;
    }

    if (IsDestroyed())
    {
        return;
    }

    CurrentHealth = FMath::Max(CurrentHealth - Damage, 0);

    OnBuildingDamaged.Broadcast(this, Damage, CurrentHealth);

    UE_LOG(LogTDBuilding, Log,
        TEXT("Building at %s took %d damage, health: %d/%d"),
        *Coord.ToString(), Damage, CurrentHealth, GetMaxHealth());

    if (IsDestroyed())
    {
        OnBuildingDestroyed.Broadcast(this);

        UE_LOG(LogTDBuilding, Log,
            TEXT("Building at %s has been destroyed."),
            *Coord.ToString());
    }
}

void ATDBuildingBase::RepairToFull()
{
    CurrentHealth = GetMaxHealth();
}

// ===================================================================
// 攻击
// ===================================================================

bool ATDBuildingBase::CanAttack() const
{
    if (!BuildingData)
    {
        return false;
    }
    return BuildingData->AttackDamage > 0.0f
        && BuildingData->AttackRange > 0.0f;
}

float ATDBuildingBase::GetAttackRange() const
{
    if (!BuildingData)
    {
        return 0.0f;
    }
    // 基类使用基础范围，不含高度加成（子类可覆盖）
    return BuildingData->AttackRange;
}

float ATDBuildingBase::GetAttackDamage() const
{
    if (!BuildingData)
    {
        return 0.0f;
    }
    return BuildingData->AttackDamage;
}

float ATDBuildingBase::GetAttackInterval() const
{
    if (!BuildingData)
    {
        return 1.0f;
    }
    return BuildingData->AttackInterval;
}

// ===================================================================
// 经济
// ===================================================================

int32 ATDBuildingBase::GetGoldPerRound() const
{
    if (!BuildingData)
    {
        return 0;
    }
    return BuildingData->GoldPerRound;
}

int32 ATDBuildingBase::GetResearchPerRound() const
{
    if (!BuildingData)
    {
        return 0;
    }
    return BuildingData->ResearchPerRound;
}

// ===================================================================
// 内部方法
// ===================================================================

void ATDBuildingBase::UpdateVisualMesh()
{
    if (!BuildingMeshComponent)
    {
        return;
    }

    if (BuildingData && BuildingData->BuildingMesh)
    {
        BuildingMeshComponent->SetStaticMesh(BuildingData->BuildingMesh);
    }
}
