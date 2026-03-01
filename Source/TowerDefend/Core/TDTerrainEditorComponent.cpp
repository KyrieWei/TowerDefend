// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDTerrainEditorComponent.h"
#include "Core/TDPlayerController.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDTerrainModifier.h"
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

// ===================================================================
// 绘制
// ===================================================================

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

    // 获取所属 PlayerController
    ATDPlayerController* PC = Cast<ATDPlayerController>(GetOwner());
    if (!PC)
    {
        UE_LOG(LogTDTerrainEditor, Warning,
            TEXT("UTDTerrainEditorComponent::PaintTileUnderCursor: "
                 "Owner is not ATDPlayerController."));
        return;
    }

    // 获取光标下的六边形坐标
    const float HexSize = GridManager->GetHexSize();
    const FTDHexCoord Coord = PC->GetHexCoordUnderCursor(HexSize);

    if (!Coord.IsValid())
    {
        return;
    }

    // 检查坐标上是否存在 Tile
    if (!GridManager->GetTileAt(Coord))
    {
        return;
    }

    // 应用地形修改
    if (TerrainModifier->ChangeTerrainType(GridManager, Coord, BrushTerrainType))
    {
        OnTilePainted.Broadcast(Coord, BrushTerrainType);

        UE_LOG(LogTDTerrainEditor, Verbose,
            TEXT("Painted tile %s to type %d"),
            *Coord.ToString(), static_cast<uint8>(BrushTerrainType));
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
