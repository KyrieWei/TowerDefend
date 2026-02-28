// Copyright TowerDefend. All Rights Reserved.

#include "Unit/TDUnitBase.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "Components/StaticMeshComponent.h"

ATDUnitBase::ATDUnitBase()
{
    PrimaryActorTick.bCanEverTick = false;

    UnitMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("UnitMesh"));
    SetRootComponent(UnitMeshComponent);

    // 单位使用简单碰撞用于选取
    UnitMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    UnitMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    UnitMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

// ===================================================================
// 初始化
// ===================================================================

void ATDUnitBase::InitializeUnit(UTDUnitDataAsset* InData, const FTDHexCoord& SpawnCoord, int32 InOwner)
{
    if (!ensure(InData != nullptr))
    {
        UE_LOG(LogTemp, Error, TEXT("ATDUnitBase::InitializeUnit - InData is null!"));
        return;
    }

    UnitData = InData;
    CurrentCoord = SpawnCoord;
    TargetCoord = SpawnCoord;
    OwnerPlayerIndex = InOwner;
    CurrentHealth = UnitData->MaxHealth;
    RemainingMovePoints = UnitData->MaxMovePoints;

    // 设置模型
    if (UnitMeshComponent && UnitData->UnitMesh)
    {
        UnitMeshComponent->SetStaticMesh(UnitData->UnitMesh);
    }
}

// ===================================================================
// 属性查询
// ===================================================================

ETDUnitType ATDUnitBase::GetUnitType() const
{
    if (UnitData)
    {
        return UnitData->UnitType;
    }
    return ETDUnitType::None;
}

int32 ATDUnitBase::GetMaxHealth() const
{
    if (UnitData)
    {
        return UnitData->MaxHealth;
    }
    return 0;
}

float ATDUnitBase::GetAttackDamage() const
{
    if (UnitData)
    {
        return UnitData->AttackDamage;
    }
    return 0.0f;
}

float ATDUnitBase::GetAttackRange() const
{
    if (UnitData)
    {
        return UnitData->AttackRange;
    }
    return 0.0f;
}

// ===================================================================
// 移动
// ===================================================================

bool ATDUnitBase::CanMoveTo(const FTDHexCoord& DestCoord, const ATDHexGridManager* Grid) const
{
    if (!Grid)
    {
        return false;
    }

    if (IsDead())
    {
        return false;
    }

    // 目标格子必须存在
    ATDHexTile* DestTile = Grid->GetTileAt(DestCoord);
    if (!DestTile)
    {
        return false;
    }

    // 目标格子必须可通行
    if (!DestTile->IsPassable())
    {
        return false;
    }

    // 移动消耗不能超过剩余移动点
    float MoveCost = DestTile->GetMovementCost();
    if (RemainingMovePoints < MoveCost)
    {
        return false;
    }

    return true;
}

void ATDUnitBase::MoveTo(const FTDHexCoord& DestCoord, float MoveCost)
{
    if (IsDead())
    {
        return;
    }

    CurrentCoord = DestCoord;
    TargetCoord = DestCoord;
    RemainingMovePoints = FMath::Max(0.0f, RemainingMovePoints - MoveCost);
}

void ATDUnitBase::ResetMovePoints()
{
    if (UnitData)
    {
        RemainingMovePoints = UnitData->MaxMovePoints;
    }
}

// ===================================================================
// 战斗
// ===================================================================

void ATDUnitBase::ApplyDamage(int32 Damage)
{
    if (Damage < 0 || IsDead())
    {
        return;
    }

    const int32 OldHealth = CurrentHealth;
    CurrentHealth = FMath::Max(0, CurrentHealth - Damage);

    // 广播受伤事件
    OnUnitDamaged.Broadcast(this, Damage, CurrentHealth);

    // 检查死亡
    if (CurrentHealth <= 0 && OldHealth > 0)
    {
        OnUnitDied.Broadcast(this);
    }
}

bool ATDUnitBase::IsInAttackRange(const FTDHexCoord& TargetCoord) const
{
    if (!UnitData)
    {
        return false;
    }

    int32 Distance = CurrentCoord.DistanceTo(TargetCoord);
    return static_cast<float>(Distance) <= UnitData->AttackRange;
}

float ATDUnitBase::CalculateDamageAgainst(const ATDUnitBase* Target, const ATDHexGridManager* Grid) const
{
    if (!Target || !UnitData || !Grid)
    {
        return 0.0f;
    }

    // 基础伤害
    float BaseDamage = UnitData->AttackDamage;

    // 克制倍率
    float CounterMultiplier = UnitData->GetDamageMultiplierVs(Target->GetUnitType());

    // 高地攻击加成
    float HeightBonus = GetHeightAttackBonus(Grid);

    // 计算原始伤害
    float RawDamage = BaseDamage * CounterMultiplier * HeightBonus;

    // 减去目标护甲（从目标数据资产获取）
    float TargetArmor = 0.0f;
    if (Target->GetUnitData())
    {
        TargetArmor = Target->GetUnitData()->ArmorValue;
    }

    // 地形防御加成降低伤害
    float TerrainDefense = Target->GetTerrainDefenseBonus(Grid);
    float DamageReduction = TargetArmor + (RawDamage * TerrainDefense);

    float FinalDamage = RawDamage - DamageReduction;

    // 保证最低 1 点伤害
    return FMath::Max(1.0f, FinalDamage);
}

// ===================================================================
// 地形加成查询
// ===================================================================

float ATDUnitBase::GetHeightAttackBonus(const ATDHexGridManager* Grid) const
{
    if (!Grid)
    {
        return 1.0f;
    }

    ATDHexTile* CurrentTile = Grid->GetTileAt(CurrentCoord);
    if (!CurrentTile)
    {
        return 1.0f;
    }

    int32 MyHeight = CurrentTile->GetHeightLevel();

    // 每高一级 +10% 攻击加成，基于绝对高度
    if (MyHeight > 0)
    {
        return 1.0f + static_cast<float>(MyHeight) * 0.1f;
    }

    return 1.0f;
}

float ATDUnitBase::GetHeightDefenseBonus(const ATDHexGridManager* Grid) const
{
    if (!Grid)
    {
        return 1.0f;
    }

    ATDHexTile* CurrentTile = Grid->GetTileAt(CurrentCoord);
    if (!CurrentTile)
    {
        return 1.0f;
    }

    int32 MyHeight = CurrentTile->GetHeightLevel();

    // 在低地时受到额外伤害：每低一级 +5%
    if (MyHeight < 0)
    {
        return 1.0f + static_cast<float>(FMath::Abs(MyHeight)) * 0.05f;
    }

    return 1.0f;
}

float ATDUnitBase::GetTerrainDefenseBonus(const ATDHexGridManager* Grid) const
{
    if (!Grid)
    {
        return 0.0f;
    }

    ATDHexTile* CurrentTile = Grid->GetTileAt(CurrentCoord);
    if (!CurrentTile)
    {
        return 0.0f;
    }

    return CurrentTile->GetDefenseBonus();
}

// ===================================================================
// 内部方法
// ===================================================================

void ATDUnitBase::SyncWorldPosition(float HexSize, int32 HeightLevel)
{
    FVector WorldPos = CurrentCoord.ToWorldPosition(HexSize);
    // 加上高度偏移，单位站在格子表面上方
    WorldPos.Z = static_cast<float>(HeightLevel) * ATDHexTile::HeightLevelUnitZ + ATDHexTile::HeightLevelUnitZ;
    SetActorLocation(WorldPos);
}
