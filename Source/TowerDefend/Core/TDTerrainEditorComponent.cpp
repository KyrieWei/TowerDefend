// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDTerrainEditorComponent.h"
#include "Core/TDPlayerController.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "HexGrid/TDTerrainModifier.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDTerrainEditor, Log, All);

// ===================================================================
// 构造函数
// ===================================================================

UTDTerrainEditorComponent::UTDTerrainEditorComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

// ===================================================================
// BeginPlay
// ===================================================================

void UTDTerrainEditorComponent::BeginPlay()
{
    Super::BeginPlay();

    // 创建内部 TerrainModifier
    TerrainModifier = NewObject<UTDTerrainModifier>(this);

    // 自动查找 GridManager
    FindGridManager();
}

// ===================================================================
// 编辑模式控制
// ===================================================================

bool UTDTerrainEditorComponent::EnterEditMode()
{
    if (bIsEditMode)
    {
        return true;
    }

    // 确保 GridManager 可用
    if (!GridManager)
    {
        FindGridManager();
    }

    if (!GridManager)
    {
        UE_LOG(LogTDTerrainEditor, Warning,
            TEXT("UTDTerrainEditorComponent::EnterEditMode: "
                 "No ATDHexGridManager found in the level."));
        return false;
    }

    bIsEditMode = true;
    OnEditModeChanged.Broadcast(true);

    UE_LOG(LogTDTerrainEditor, Log, TEXT("Terrain edit mode: ON"));
    return true;
}

void UTDTerrainEditorComponent::ExitEditMode()
{
    if (!bIsEditMode)
    {
        return;
    }

    // 退出编辑模式时清除选中状态
    DeselectTile();

    bIsEditMode = false;
    OnEditModeChanged.Broadcast(false);

    UE_LOG(LogTDTerrainEditor, Log, TEXT("Terrain edit mode: OFF"));
}

void UTDTerrainEditorComponent::ToggleEditMode()
{
    if (bIsEditMode)
    {
        ExitEditMode();
    }
    else
    {
        EnterEditMode();
    }
}

// ===================================================================
// 地块选中
// ===================================================================

void UTDTerrainEditorComponent::SelectTileUnderCursor()
{
    if (!bIsEditMode)
    {
        return;
    }

    const FTDHexCoord CursorCoord = GetCursorHexCoord();

    if (!CursorCoord.IsValid())
    {
        return;
    }

    // 点击同一地块时取消选中
    if (bHasSelection && SelectedCoord == CursorCoord)
    {
        DeselectTile();
        return;
    }

    SelectTile(CursorCoord);

    // 选中后，如果开启了自动应用地形类型，则修改地块
    if (bApplyTerrainTypeOnClick && bHasSelection)
    {
        ApplyActiveTerrainType();
    }
}

void UTDTerrainEditorComponent::SelectTile(const FTDHexCoord& Coord)
{
    if (!bIsEditMode || !GridManager)
    {
        return;
    }

    ATDHexTile* NewTile = GridManager->GetTileAt(Coord);
    if (!NewTile)
    {
        return;
    }

    // 取消旧选中的高亮
    if (bHasSelection)
    {
        ATDHexTile* OldTile = GridManager->GetTileAt(SelectedCoord);
        if (OldTile)
        {
            ApplySelectionHighlight(OldTile, false);
        }
    }

    // 更新选中状态
    SelectedCoord = Coord;
    bHasSelection = true;

    // 应用高亮
    ApplySelectionHighlight(NewTile, true);

    OnTileSelectionChanged.Broadcast(Coord, true);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Selected tile %s (Terrain=%d, Height=%d)"),
        *Coord.ToString(),
        static_cast<uint8>(NewTile->GetTerrainType()),
        NewTile->GetHeightLevel());
}

void UTDTerrainEditorComponent::DeselectTile()
{
    if (!bHasSelection)
    {
        return;
    }

    // 移除旧选中的高亮
    if (GridManager)
    {
        ATDHexTile* OldTile = GridManager->GetTileAt(SelectedCoord);
        if (OldTile)
        {
            ApplySelectionHighlight(OldTile, false);
        }
    }

    const FTDHexCoord OldCoord = SelectedCoord;
    SelectedCoord = FTDHexCoord::Invalid();
    bHasSelection = false;

    OnTileSelectionChanged.Broadcast(OldCoord, false);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Deselected tile %s"), *OldCoord.ToString());
}

FTDHexCoord UTDTerrainEditorComponent::GetSelectedCoord() const
{
    return bHasSelection ? SelectedCoord : FTDHexCoord::Invalid();
}

ATDHexTile* UTDTerrainEditorComponent::GetSelectedTile() const
{
    if (!bHasSelection || !GridManager)
    {
        return nullptr;
    }

    return GridManager->GetTileAt(SelectedCoord);
}

// ===================================================================
// 地形编辑操作
// ===================================================================

bool UTDTerrainEditorComponent::RaiseSelectedTile()
{
    if (!bIsEditMode || !bHasSelection)
    {
        return false;
    }

    if (!GridManager || !TerrainModifier)
    {
        return false;
    }

    ATDHexTile* Tile = GridManager->GetTileAt(SelectedCoord);
    if (!Tile)
    {
        return false;
    }

    const int32 OldHeight = Tile->GetHeightLevel();

    if (!TerrainModifier->RaiseTerrain(GridManager, SelectedCoord, 1))
    {
        UE_LOG(LogTDTerrainEditor, Log,
            TEXT("RaiseSelectedTile: Cannot raise tile %s (current height=%d)"),
            *SelectedCoord.ToString(), OldHeight);
        return false;
    }

    const int32 NewHeight = Tile->GetHeightLevel();
    OnTileHeightChanged.Broadcast(SelectedCoord, OldHeight, NewHeight);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Raised tile %s: %d -> %d"),
        *SelectedCoord.ToString(), OldHeight, NewHeight);
    return true;
}

bool UTDTerrainEditorComponent::LowerSelectedTile()
{
    if (!bIsEditMode || !bHasSelection)
    {
        return false;
    }

    if (!GridManager || !TerrainModifier)
    {
        return false;
    }

    ATDHexTile* Tile = GridManager->GetTileAt(SelectedCoord);
    if (!Tile)
    {
        return false;
    }

    const int32 OldHeight = Tile->GetHeightLevel();

    if (!TerrainModifier->LowerTerrain(GridManager, SelectedCoord, 1))
    {
        UE_LOG(LogTDTerrainEditor, Log,
            TEXT("LowerSelectedTile: Cannot lower tile %s (current height=%d)"),
            *SelectedCoord.ToString(), OldHeight);
        return false;
    }

    const int32 NewHeight = Tile->GetHeightLevel();
    OnTileHeightChanged.Broadcast(SelectedCoord, OldHeight, NewHeight);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Lowered tile %s: %d -> %d"),
        *SelectedCoord.ToString(), OldHeight, NewHeight);
    return true;
}

bool UTDTerrainEditorComponent::SetSelectedTileTerrainType(ETDTerrainType NewType)
{
    if (!bIsEditMode || !bHasSelection)
    {
        return false;
    }

    if (!GridManager || !TerrainModifier)
    {
        return false;
    }

    if (!TerrainModifier->ChangeTerrainType(GridManager, SelectedCoord, NewType))
    {
        return false;
    }

    OnTilePainted.Broadcast(SelectedCoord, NewType);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Changed terrain type of tile %s to %d"),
        *SelectedCoord.ToString(), static_cast<uint8>(NewType));

    // 地形类型变更后重新应用高亮（材质可能被替换）
    ATDHexTile* Tile = GridManager->GetTileAt(SelectedCoord);
    if (Tile)
    {
        ApplySelectionHighlight(Tile, true);
    }

    return true;
}

// ===================================================================
// 当前编辑地形类型
// ===================================================================

void UTDTerrainEditorComponent::SetActiveTerrainType(ETDTerrainType NewType)
{
    if (ActiveTerrainType == NewType)
    {
        return;
    }

    const ETDTerrainType OldType = ActiveTerrainType;
    ActiveTerrainType = NewType;
    OnActiveTerrainTypeChanged.Broadcast(OldType, NewType);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Active terrain type set to: %d"), static_cast<uint8>(NewType));
}

void UTDTerrainEditorComponent::SetApplyTerrainTypeOnClick(bool bEnable)
{
    bApplyTerrainTypeOnClick = bEnable;

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Apply terrain type on click: %s"),
        bEnable ? TEXT("ON") : TEXT("OFF"));
}

void UTDTerrainEditorComponent::ApplyActiveTerrainType()
{
    if (!bIsEditMode || !bHasSelection)
    {
        return;
    }

    if (!GridManager || !TerrainModifier)
    {
        return;
    }

    ATDHexTile* Tile = GridManager->GetTileAt(SelectedCoord);
    if (!Tile)
    {
        return;
    }

    // 地形相同则跳过
    if (Tile->GetTerrainType() == ActiveTerrainType)
    {
        return;
    }

    if (!TerrainModifier->ChangeTerrainType(GridManager, SelectedCoord, ActiveTerrainType))
    {
        return;
    }

    OnTilePainted.Broadcast(SelectedCoord, ActiveTerrainType);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Applied active terrain type %d to tile %s"),
        static_cast<uint8>(ActiveTerrainType), *SelectedCoord.ToString());

    // 地形类型变更后重新应用高亮（材质可能被替换）
    ApplySelectionHighlight(Tile, true);
}

// ===================================================================
// 笔刷
// ===================================================================

void UTDTerrainEditorComponent::SetBrushTerrainType(ETDTerrainType NewType)
{
    if (BrushTerrainType == NewType)
    {
        return;
    }

    BrushTerrainType = NewType;
    OnBrushChanged.Broadcast(NewType);

    UE_LOG(LogTDTerrainEditor, Log,
        TEXT("Terrain brush set to: %d"), static_cast<uint8>(NewType));
}

void UTDTerrainEditorComponent::PaintTileUnderCursor()
{
    if (!bIsEditMode)
    {
        return;
    }

    if (!GridManager || !TerrainModifier)
    {
        return;
    }

    const FTDHexCoord Coord = GetCursorHexCoord();

    if (!Coord.IsValid())
    {
        return;
    }

    if (!GridManager->GetTileAt(Coord))
    {
        return;
    }

    if (TerrainModifier->ChangeTerrainType(GridManager, Coord, BrushTerrainType))
    {
        OnTilePainted.Broadcast(Coord, BrushTerrainType);

        UE_LOG(LogTDTerrainEditor, Verbose,
            TEXT("Painted tile %s to type %d"),
            *Coord.ToString(), static_cast<uint8>(BrushTerrainType));
    }
}

// ===================================================================
// 选中高亮
// ===================================================================

void UTDTerrainEditorComponent::ApplySelectionHighlight(
    ATDHexTile* Tile, bool bHighlight)
{
    if (!Tile)
    {
        return;
    }

    // 获取 HexMeshComponent 上的动态材质实例
    UStaticMeshComponent* MeshComp = Cast<UStaticMeshComponent>(
        Tile->GetRootComponent());
    if (!MeshComp)
    {
        return;
    }

    UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(
        MeshComp->GetMaterial(0));

    // 如果当前没有 MID，创建一个
    if (!MID)
    {
        UMaterialInterface* BaseMat = MeshComp->GetMaterial(0);
        if (!BaseMat)
        {
            return;
        }
        MID = MeshComp->CreateDynamicMaterialInstance(0, BaseMat);
    }

    if (!MID)
    {
        return;
    }

    if (bHighlight)
    {
        // 混合高亮色到材质
        MID->SetScalarParameterValue(
            TEXT("SelectionIntensity"), SelectionHighlightIntensity);
        MID->SetVectorParameterValue(
            TEXT("SelectionColor"), SelectionHighlightColor);
    }
    else
    {
        // 清除高亮
        MID->SetScalarParameterValue(TEXT("SelectionIntensity"), 0.0f);
        MID->SetVectorParameterValue(
            TEXT("SelectionColor"), FLinearColor::Black);
    }
}

// ===================================================================
// 内部方法
// ===================================================================

void UTDTerrainEditorComponent::FindGridManager()
{
    AActor* FoundActor = UGameplayStatics::GetActorOfClass(
        GetWorld(), ATDHexGridManager::StaticClass());
    GridManager = Cast<ATDHexGridManager>(FoundActor);

    if (GridManager)
    {
        UE_LOG(LogTDTerrainEditor, Log,
            TEXT("UTDTerrainEditorComponent: Found GridManager '%s'."),
            *GridManager->GetName());
    }
}

FTDHexCoord UTDTerrainEditorComponent::GetCursorHexCoord() const
{
    if (!GridManager)
    {
        return FTDHexCoord::Invalid();
    }

    ATDPlayerController* PC = Cast<ATDPlayerController>(GetOwner());
    if (!PC)
    {
        UE_LOG(LogTDTerrainEditor, Warning,
            TEXT("UTDTerrainEditorComponent::GetCursorHexCoord: "
                 "Owner is not ATDPlayerController."));
        return FTDHexCoord::Invalid();
    }

    const float HexSize = GridManager->GetHexSize();
    return PC->GetHexCoordUnderCursor(HexSize);
}
