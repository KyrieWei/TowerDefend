// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDHexGridManager.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"

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
    CachedHexSize = HexSize;

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
    // 深水地块高度固定为 1，不可修改
    if (TerrainType == ETDTerrainType::DeepWater)
    {
        return;
    }

    const int32 ClampedHeight = FMath::Clamp(NewHeight, MinHeightLevel, MaxHeightLevel);

    if (HeightLevel == ClampedHeight)
    {
        return;
    }

    HeightLevel = ClampedHeight;
    UpdateVisualHeight();

    // 通知 GridManager 更新本格及邻居的侧面裙边
    if (ATDHexGridManager* Grid = OwnerGridManager.Get())
    {
        Grid->NotifyTileHeightChanged(Coord);
    }
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

    // 高度加成：高度 >= 3 → +20%，高度 == 2 → +10%
    float HeightBonus = 0.0f;
    if (HeightLevel >= 3)
    {
        HeightBonus = 0.2f;
    }
    else if (HeightLevel == 2)
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

// ===================================================================
// GridManager 引用
// ===================================================================

void ATDHexTile::SetGridManager(ATDHexGridManager* InGridManager)
{
    OwnerGridManager = InGridManager;
}

// ===================================================================
// 侧面裙边（Side Skirt）
// ===================================================================

void ATDHexTile::EnsureSideSkirtMesh()
{
    if (!SideSkirtMesh)
    {
        SideSkirtMesh = NewObject<UProceduralMeshComponent>(this, TEXT("SideSkirtMesh"));
        SideSkirtMesh->SetupAttachment(GetRootComponent());
        SideSkirtMesh->RegisterComponent();

        // 侧面无需碰撞
        SideSkirtMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        SideSkirtMesh->bUseAsyncCooking = true;
        SideSkirtMesh->SetCastShadow(false);
    }
}

FVector2D ATDHexTile::GetHexVertex(int32 Index, float InHexSize)
{
    // 平顶六边形：顶点 i 的角度 = 60 * i 度
    const float AngleDeg = 60.0f * static_cast<float>(Index);
    const float AngleRad = FMath::DegreesToRadians(AngleDeg);
    return FVector2D(
        InHexSize * FMath::Cos(AngleRad),
        InHexSize * FMath::Sin(AngleRad)
    );
}

TPair<int32, int32> ATDHexTile::GetEdgeVertexIndices(int32 DirIndex)
{
    // 方向-边对应关系（平顶六边形）：
    // Dir 0 (E):  V5 - V0
    // Dir 1 (NE): V0 - V1
    // Dir 2 (NW): V1 - V2
    // Dir 3 (W):  V2 - V3
    // Dir 4 (SW): V3 - V4
    // Dir 5 (SE): V4 - V5
    const int32 Start = (DirIndex + 5) % 6;  // = DirIndex - 1 mod 6
    const int32 End = DirIndex % 6;
    return TPair<int32, int32>(Start, End);
}

void ATDHexTile::RebuildSideSkirt()
{
    ATDHexGridManager* Grid = OwnerGridManager.Get();
    if (!Grid)
    {
        return;
    }

    // 收集需要生成侧面的边
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FColor> VertexColors;

    int32 VertexCount = 0;
    const float MyZ = 0.0f;  // 本格顶面在本地空间 Z=0

    for (int32 Dir = 0; Dir < 6; ++Dir)
    {
        // 获取该方向的邻居
        const FTDHexCoord NeighborCoord = Coord.GetNeighbor(Dir);
        ATDHexTile* Neighbor = Grid->GetTileAt(NeighborCoord);

        // 计算邻居高度（无邻居时取 MinHeightLevel - 1）
        const int32 NeighborHeight = Neighbor ? Neighbor->GetHeightLevel() : (MinHeightLevel - 1);

        // 只在本格高于邻格时生成侧面
        if (HeightLevel <= NeighborHeight)
        {
            continue;
        }

        // 高度差对应的本地 Z 偏移（邻格相对于本格的 Z 差，为负值）
        const float BottomZ = static_cast<float>(NeighborHeight - HeightLevel) * HeightLevelUnitZ;

        // 获取边的两个顶点
        const TPair<int32, int32> Edge = GetEdgeVertexIndices(Dir);
        const FVector2D V0_2D = GetHexVertex(Edge.Key, CachedHexSize);
        const FVector2D V1_2D = GetHexVertex(Edge.Value, CachedHexSize);

        // 四个顶点（本地空间，Tile 原点在顶面中心）
        // P0: 顶面左端, P1: 顶面右端, P2: 底面右端, P3: 底面左端
        const FVector P0(V0_2D.X, V0_2D.Y, MyZ);
        const FVector P1(V1_2D.X, V1_2D.Y, MyZ);
        const FVector P2(V1_2D.X, V1_2D.Y, BottomZ);
        const FVector P3(V0_2D.X, V0_2D.Y, BottomZ);

        // 计算外法线（从中心指向边中点的 XY 方向）
        const FVector EdgeMid = (P0 + P1) * 0.5f;
        FVector Normal(EdgeMid.X, EdgeMid.Y, 0.0f);
        Normal.Normalize();

        // UV 坐标：简单映射
        const float HeightDiffUnits = static_cast<float>(HeightLevel - NeighborHeight);
        const FVector2D UV0(0.0f, 0.0f);
        const FVector2D UV1(1.0f, 0.0f);
        const FVector2D UV2(1.0f, HeightDiffUnits);
        const FVector2D UV3(0.0f, HeightDiffUnits);

        // 顶点颜色：使用当前地形的基础颜色
        const FLinearColor BaseColor = GetTerrainBaseColor(TerrainType);
        const FColor VColor = BaseColor.ToFColor(true);

        // 添加 4 个顶点
        const int32 BaseIdx = VertexCount;
        Vertices.Add(P0);
        Vertices.Add(P1);
        Vertices.Add(P2);
        Vertices.Add(P3);

        Normals.Add(Normal);
        Normals.Add(Normal);
        Normals.Add(Normal);
        Normals.Add(Normal);

        UVs.Add(UV0);
        UVs.Add(UV1);
        UVs.Add(UV2);
        UVs.Add(UV3);

        VertexColors.Add(VColor);
        VertexColors.Add(VColor);
        VertexColors.Add(VColor);
        VertexColors.Add(VColor);

        // 两个三角形（从外侧看顺时针绕序，法线朝外）
        // Triangle A: P0, P2, P1
        Triangles.Add(BaseIdx + 0);
        Triangles.Add(BaseIdx + 2);
        Triangles.Add(BaseIdx + 1);
        // Triangle B: P0, P3, P2
        Triangles.Add(BaseIdx + 0);
        Triangles.Add(BaseIdx + 3);
        Triangles.Add(BaseIdx + 2);

        VertexCount += 4;
    }

    // 如果没有需要生成的侧面，清除已有 Mesh
    if (Vertices.Num() == 0)
    {
        if (SideSkirtMesh)
        {
            SideSkirtMesh->ClearAllMeshSections();
        }
        return;
    }

    // 确保组件存在
    EnsureSideSkirtMesh();

    // 写入 ProceduralMesh Section 0
    SideSkirtMesh->ClearAllMeshSections();
    SideSkirtMesh->CreateMeshSection(
        0,                   // SectionIndex
        Vertices,
        Triangles,
        Normals,
        UVs,
        VertexColors,
        TArray<FProcMeshTangent>(),
        false                // bCreateCollision
    );

    // 应用材质：使用 HexMeshComponent 当前的材质
    if (HexMeshComponent)
    {
        UMaterialInterface* CurrentMat = HexMeshComponent->GetMaterial(0);
        if (CurrentMat)
        {
            SideSkirtMesh->SetMaterial(0, CurrentMat);
        }
    }
}
