// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDTerrainEditorComponent.generated.h"

class ATDHexGridManager;
class UTDTerrainModifier;
class ATDPlayerController;

// ===================================================================
// 委托声明
// ===================================================================

/** 编辑模式变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEditModeChanged, bool, bNewEditMode);

/** 笔刷变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBrushChanged, ETDTerrainType, NewType);

/** 地块绘制委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTilePainted, FTDHexCoord, Coord, ETDTerrainType, NewType);

/**
 * UTDTerrainEditorComponent - 运行时地形编辑器组件。
 *
 * 挂载在 ATDPlayerController 上，负责：
 * - 编辑模式状态管理（开关切换）
 * - 笔刷地形类型选择
 * - 点击地块后调用 TerrainModifier 修改地形
 *
 * 所有状态变化通过动态多播委托通知 UI 层。
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TOWERDEFEND_API UTDTerrainEditorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTDTerrainEditorComponent();

    // ---------------------------------------------------------------
    // Blueprint 可调用接口
    // ---------------------------------------------------------------

    /** 进入编辑模式。如果无法找到 GridManager 则返回 false。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    bool EnterEditMode();

    /** 退出编辑模式。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    void ExitEditMode();

    /** 切换编辑模式。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    void ToggleEditMode();

    /** 设置笔刷地形类型。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    void SetBrushTerrainType(ETDTerrainType NewType);

    /** 获取当前笔刷地形类型。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor")
    ETDTerrainType GetBrushTerrainType() const { return BrushTerrainType; }

    /** 查询是否处于编辑模式。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor")
    bool IsInEditMode() const { return bIsEditMode; }

    /** 将笔刷应用到光标下的地块。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    void PaintTileUnderCursor();

    /** 获取当前关联的 GridManager。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor")
    ATDHexGridManager* GetGridManager() const { return GridManager; }

    // ---------------------------------------------------------------
    // Blueprint 可绑定委托
    // ---------------------------------------------------------------

    /** 编辑模式变化时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnEditModeChanged OnEditModeChanged;

    /** 笔刷变化时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnBrushChanged OnBrushChanged;

    /** 地块被绘制时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnTilePainted OnTilePainted;

protected:
    virtual void BeginPlay() override;

private:
    /** 编辑模式开关。 */
    bool bIsEditMode = false;

    /** 当前笔刷地形类型。 */
    ETDTerrainType BrushTerrainType = ETDTerrainType::Plain;

    /** 网格管理器引用，BeginPlay 自动查找。 */
    UPROPERTY()
    TObjectPtr<ATDHexGridManager> GridManager;

    /** 内部持有的地形修改器。 */
    UPROPERTY()
    TObjectPtr<UTDTerrainModifier> TerrainModifier;

    /** 自动查找场景中的 GridManager。 */
    void FindGridManager();
};
