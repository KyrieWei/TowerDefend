// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexTile.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"

ATDHexTile::ATDHexTile()
{
    PrimaryActorTick.bCanEverTick = false;

    HexMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HexMesh"));
    SetRootComponent(HexMeshComponent);

    // 关闭复杂碰撞，仅使用简单碰撞用于鼠标拾取
    HexMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    HexMeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
    HexMeshComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
}

// ===================================================================
// 初始化
// ===================================================================

void ATDHexTile::InitFromSaveData(const FTDHexTileSaveData& InSaveData, float HexSize)
{
    Coord = InSaveData.Coord;
    TerrainType = InSaveData.TerrainType;
    HeightLevel = FMath::Clamp(InSaveData.HeightLevel, MinHeightLevel, MaxHeightLevel);
    OwnerPlayerIndex = InSaveData.OwnerPlayerIndex;

    // 不在此处设置位置 —— 位置由 GridManager::SpawnTilesFromData 在 Spawn 时决定。
    // 仅确保 Z 轴高度与内部数据一致。
    UpdateVisualHeight();
    UpdateVisualMaterial();
}

// ===================================================================
// 地形属性修改
// ===================================================================

void ATDHexTile::SetTerrainType(ETDTerrainType NewType)
{
    if (TerrainType == NewType)
    {
        return;
    }

    TerrainType = NewType;
    UpdateVisualMaterial();
}

void ATDHexTile::SetHeightLevel(int32 NewHeight)
{
    const int32 ClampedHeight = FMath::Clamp(NewHeight, MinHeightLevel, MaxHeightLevel);

    if (HeightLevel == ClampedHeight)
    {
        return;
    }

    HeightLevel = ClampedHeight;
    UpdateVisualHeight();
}

void ATDHexTile::SetOwnerPlayerIndex(int32 NewOwner)
{
    OwnerPlayerIndex = NewOwner;
}

// ===================================================================
// 游戏逻辑查询
// ===================================================================

float ATDHexTile::GetMovementCost() const
{
    if (!IsPassable())
    {
        return BIG_NUMBER;
    }

    switch (TerrainType)
    {
    case ETDTerrainType::Plain:
        return 1.0f;

    case ETDTerrainType::Hill:
        return 1.5f;

    case ETDTerrainType::Forest:
        return 1.5f;

    case ETDTerrainType::River:
        return 2.0f;

    case ETDTerrainType::Swamp:
        return 3.0f;

    default:
        return 1.0f;
    }
}

float ATDHexTile::GetDefenseBonus() const
{
    float TerrainBonus = 0.0f;

    switch (TerrainType)
    {
    case ETDTerrainType::Hill:
        TerrainBonus = 0.1f;
        break;

    case ETDTerrainType::Forest:
        TerrainBonus = 0.15f;
        break;

    case ETDTerrainType::Mountain:
        TerrainBonus = 0.3f;
        break;

    default:
        break;
    }

    // 高度加成：高度 2 → +20%，取地形加成和高度加成中的较大值
    float HeightBonus = 0.0f;
    if (HeightLevel >= 2)
    {
        HeightBonus = 0.2f;
    }
    else if (HeightLevel == 1)
    {
        HeightBonus = 0.1f;
    }

    return FMath::Max(TerrainBonus, HeightBonus);
}

bool ATDHexTile::IsPassable() const
{
    return TerrainType != ETDTerrainType::Mountain
        && TerrainType != ETDTerrainType::DeepWater;
}

bool ATDHexTile::IsBuildable() const
{
    return TerrainType != ETDTerrainType::Mountain
        && TerrainType != ETDTerrainType::DeepWater
        && TerrainType != ETDTerrainType::Swamp
        && TerrainType != ETDTerrainType::River;
}

// ===================================================================
// 序列化
// ===================================================================

FTDHexTileSaveData ATDHexTile::ExportSaveData() const
{
    return FTDHexTileSaveData(Coord, TerrainType, HeightLevel, OwnerPlayerIndex);
}

// ===================================================================
// 内部视觉更新
// ===================================================================

void ATDHexTile::UpdateVisualHeight()
{
    FVector Location = GetActorLocation();
    // 仅更新 Z 轴分量，保留 Spawn 时设置的 X/Y（含 Manager 偏移）
    // Z 轴基于当前位置的整数倍高度单元重新计算
    // 注意：由于 Manager 偏移也在 Z 上累加，需要从 spawn 位置获取基准 Z
    // 此处简化处理：直接设置高度等级对应的 Z 值
    Location.Z = static_cast<float>(HeightLevel) * HeightLevelUnitZ;
    SetActorLocation(Location);
}

void ATDHexTile::UpdateVisualMaterial()
{
    if (!HexMeshComponent)
    {
        return;
    }

    // 尝试从 TerrainMaterials 映射表中加载对应地形的 MaterialInstance
    if (const TSoftObjectPtr<UMaterialInterface>* Found = TerrainMaterials.Find(TerrainType))
    {
        UMaterialInterface* MaterialAsset = Found->LoadSynchronous();
        if (MaterialAsset)
        {
            // 从地形专属 MaterialInstance 创建动态材质实例
            TerrainMaterial = HexMeshComponent->CreateDynamicMaterialInstance(0, MaterialAsset);
            return;
        }
    }

    // 回退方案：使用 Mesh 上已有的基础材质创建 MID，通过 BaseColor 参数区分地形
    if (!TerrainMaterial)
    {
        UMaterialInterface* BaseMaterial = HexMeshComponent->GetMaterial(0);
        if (BaseMaterial)
        {
            TerrainMaterial = HexMeshComponent->CreateDynamicMaterialInstance(0, BaseMaterial);
        }
    }

    if (TerrainMaterial)
    {
        const FLinearColor BaseColor = GetTerrainBaseColor(TerrainType);
        TerrainMaterial->SetVectorParameterValue(TEXT("BaseColor"), BaseColor);
    }
}

FLinearColor ATDHexTile::GetTerrainBaseColor(ETDTerrainType Type)
{
    switch (Type)
    {
    case ETDTerrainType::Plain:
        return FLinearColor(0.45f, 0.65f, 0.25f);  // 草绿

    case ETDTerrainType::Hill:
        return FLinearColor(0.55f, 0.50f, 0.30f);  // 土黄

    case ETDTerrainType::Mountain:
        return FLinearColor(0.50f, 0.50f, 0.50f);  // 灰色

    case ETDTerrainType::Forest:
        return FLinearColor(0.15f, 0.40f, 0.15f);  // 深绿

    case ETDTerrainType::River:
        return FLinearColor(0.30f, 0.55f, 0.80f);  // 浅蓝

    case ETDTerrainType::Swamp:
        return FLinearColor(0.35f, 0.40f, 0.25f);  // 暗绿

    case ETDTerrainType::DeepWater:
        return FLinearColor(0.10f, 0.20f, 0.55f);  // 深蓝

    default:
        return FLinearColor::White;
    }
}
