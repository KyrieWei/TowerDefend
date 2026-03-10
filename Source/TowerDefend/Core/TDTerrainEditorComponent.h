// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDTerrainEditorComponent.generated.h"

class ATDHexGridManager;
class ATDHexTile;
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

/** 地块选中变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTileSelectionChanged, FTDHexCoord, NewCoord, bool, bIsSelected);

/** 地块高度变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnTileHeightChanged, FTDHexCoord, Coord, int32, OldHeight, int32, NewHeight);

/** 当前编辑地形类型变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActiveTerrainTypeChanged, ETDTerrainType, OldType, ETDTerrainType, NewType);

/** 当前编辑高度变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnActiveHeightLevelChanged, int32, OldHeight, int32, NewHeight);

/**
 * UTDTerrainEditorComponent - 运行时地形编辑器组件。
 *
 * 挂载在 ATDPlayerController 上，负责：
 * - 编辑模式状态管理（开关切换）
 * - 点击地块选中，显示高亮
 * - 对选中地块修改地形类型和高度
 * - 笔刷地形类型选择
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
    // 编辑模式控制
    // ---------------------------------------------------------------

    /** 进入编辑模式。如果无法找到 GridManager 则返回 false。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    bool EnterEditMode();

    /** 退出编辑模式，清除选中状态。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    void ExitEditMode();

    /** 切换编辑模式。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor")
    void ToggleEditMode();

    /** 查询是否处于编辑模式。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor")
    bool IsInEditMode() const { return bIsEditMode; }

    // ---------------------------------------------------------------
    // 地块选中
    // ---------------------------------------------------------------

    /**
     * 选中光标下的地块。
     * 编辑模式下，点击地块进行选中。再次点击同一地块取消选中。
     * 选中新地块时自动取消旧选中。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Selection")
    void SelectTileUnderCursor();

    /**
     * 选中指定坐标的地块。
     *
     * @param Coord  目标六边形坐标。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Selection")
    void SelectTile(const FTDHexCoord& Coord);

    /** 取消当前选中。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Selection")
    void DeselectTile();

    /** 查询当前是否有选中地块。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|Selection")
    bool HasSelectedTile() const { return bHasSelection; }

    /** 获取当前选中地块的坐标。无选中时返回 Invalid。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|Selection")
    FTDHexCoord GetSelectedCoord() const;

    /** 获取当前选中地块的 Actor。无选中时返回 nullptr。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|Selection")
    ATDHexTile* GetSelectedTile() const;

    // ---------------------------------------------------------------
    // 地形编辑操作（作用于选中地块）
    // ---------------------------------------------------------------

    /**
     * 升高选中地块的高度。
     * 仅在编辑模式下且有选中地块时生效。
     *
     * @return 是否成功修改。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Modify")
    bool RaiseSelectedTile();

    /**
     * 降低选中地块的高度。
     * 仅在编辑模式下且有选中地块时生效。
     *
     * @return 是否成功修改。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Modify")
    bool LowerSelectedTile();

    /**
     * 设置选中地块的地形类型。
     * 仅在编辑模式下且有选中地块时生效。
     *
     * @param NewType  新的地形类型。
     * @return         是否成功修改。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Modify")
    bool SetSelectedTileTerrainType(ETDTerrainType NewType);

    // ---------------------------------------------------------------
    // 当前编辑地形类型（UI 可绑定）
    // ---------------------------------------------------------------

    /**
     * 设置当前编辑地形类型。
     * UI 界面更改此值后，点击地块时自动将地块设置为该类型。
     *
     * @param NewType  新的地形类型。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|ActiveType")
    void SetActiveTerrainType(ETDTerrainType NewType);

    /** 获取当前编辑地形类型。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|ActiveType")
    ETDTerrainType GetActiveTerrainType() const { return ActiveTerrainType; }

    /**
     * 设置是否在点击选中地块时自动应用当前编辑地形类型。
     *
     * @param bEnable  true 为启用，false 为禁用。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|ActiveType")
    void SetApplyTerrainTypeOnClick(bool bEnable);

    /** 查询是否在点击时自动应用地形类型。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|ActiveType")
    bool IsApplyTerrainTypeOnClick() const { return bApplyTerrainTypeOnClick; }

    // ---------------------------------------------------------------
    // 当前编辑高度（UI 可绑定）
    // ---------------------------------------------------------------

    /**
     * 设置当前编辑高度值。
     * UI 界面更改此值后，点击地块时自动将地块设置为该高度。
     *
     * @param NewHeight  新的高度值。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|ActiveHeight")
    void SetActiveHeightLevel(int32 NewHeight);

    /** 获取当前编辑高度值。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|ActiveHeight")
    int32 GetActiveHeightLevel() const { return ActiveHeightLevel; }

    /**
     * 设置是否在点击选中地块时自动应用当前编辑高度。
     *
     * @param bEnable  true 为启用，false 为禁用。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|ActiveHeight")
    void SetApplyHeightOnClick(bool bEnable);

    /** 查询是否在点击时自动应用高度。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|ActiveHeight")
    bool IsApplyHeightOnClick() const { return bApplyHeightOnClick; }

    // ---------------------------------------------------------------
    // 笔刷（快速绘制模式）
    // ---------------------------------------------------------------

    /** 设置笔刷地形类型。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Brush")
    void SetBrushTerrainType(ETDTerrainType NewType);

    /** 获取当前笔刷地形类型。 */
    UFUNCTION(BlueprintPure, Category = "TerrainEditor|Brush")
    ETDTerrainType GetBrushTerrainType() const { return BrushTerrainType; }

    /** 将笔刷地形类型应用到光标下的地块（不选中）。 */
    UFUNCTION(BlueprintCallable, Category = "TerrainEditor|Brush")
    void PaintTileUnderCursor();

    // ---------------------------------------------------------------
    // 杂项
    // ---------------------------------------------------------------

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

    /** 地块选中状态变化时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnTileSelectionChanged OnTileSelectionChanged;

    /** 选中地块高度变化时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnTileHeightChanged OnTileHeightChanged;

    /** 当前编辑地形类型变化时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnActiveTerrainTypeChanged OnActiveTerrainTypeChanged;

    /** 当前编辑高度变化时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TerrainEditor|Events")
    FOnActiveHeightLevelChanged OnActiveHeightLevelChanged;

protected:
    virtual void BeginPlay() override;

private:
    // ---------------------------------------------------------------
    // 编辑模式状态
    // ---------------------------------------------------------------

    /** 编辑模式开关。 */
    bool bIsEditMode = false;

    /** 当前笔刷地形类型。 */
    ETDTerrainType BrushTerrainType = ETDTerrainType::Plain;

    /** 当前编辑地形类型，UI 可读写。点击地块时自动应用此类型。 */
    UPROPERTY(BlueprintReadWrite, Category = "TerrainEditor|ActiveType",
        meta = (AllowPrivateAccess = "true"))
    ETDTerrainType ActiveTerrainType = ETDTerrainType::Plain;

    /** 是否在点击选中地块时自动应用 ActiveTerrainType。 */
    UPROPERTY(BlueprintReadWrite, Category = "TerrainEditor|ActiveType",
        meta = (AllowPrivateAccess = "true"))
    bool bApplyTerrainTypeOnClick = false;

    /** 当前编辑高度值，UI 可读写。点击地块时自动应用此高度。 */
    UPROPERTY(BlueprintReadWrite, Category = "TerrainEditor|ActiveHeight",
        meta = (AllowPrivateAccess = "true"))
    int32 ActiveHeightLevel = 1;

    /** 是否在点击选中地块时自动应用 ActiveHeightLevel。 */
    UPROPERTY(BlueprintReadWrite, Category = "TerrainEditor|ActiveHeight",
        meta = (AllowPrivateAccess = "true"))
    bool bApplyHeightOnClick = false;

    // ---------------------------------------------------------------
    // 选中状态
    // ---------------------------------------------------------------

    /** 是否有选中地块。 */
    bool bHasSelection = false;

    /** 当前选中地块的坐标。 */
    FTDHexCoord SelectedCoord;

    // ---------------------------------------------------------------
    // 引用
    // ---------------------------------------------------------------

    /** 网格管理器引用，BeginPlay 自动查找。 */
    UPROPERTY()
    TObjectPtr<ATDHexGridManager> GridManager;

    /** 内部持有的地形修改器。 */
    UPROPERTY()
    TObjectPtr<UTDTerrainModifier> TerrainModifier;

    // ---------------------------------------------------------------
    // 选中高亮
    // ---------------------------------------------------------------

    /** 选中高亮颜色。 */
    UPROPERTY(EditAnywhere, Category = "TerrainEditor|Visual")
    FLinearColor SelectionHighlightColor = FLinearColor(1.0f, 0.85f, 0.0f, 1.0f);

    /** 高亮强度（叠加到基础颜色上的比例）。 */
    UPROPERTY(EditAnywhere, Category = "TerrainEditor|Visual",
        meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SelectionHighlightIntensity = 0.3f;

    /**
     * 对指定 Tile 应用选中高亮效果。
     * 通过材质参数 "SelectionTint" 实现。
     *
     * @param Tile      目标地块。
     * @param bHighlight 是否高亮。
     */
    void ApplySelectionHighlight(ATDHexTile* Tile, bool bHighlight);

    // ---------------------------------------------------------------
    // 内部方法
    // ---------------------------------------------------------------

    /** 自动查找场景中的 GridManager。 */
    void FindGridManager();

    /** 获取所属 PlayerController 的光标下六边形坐标。 */
    FTDHexCoord GetCursorHexCoord() const;

    /** 将当前 ActiveTerrainType 应用到选中地块。 */
    void ApplyActiveTerrainType();

    /** 将当前 ActiveHeightLevel 应用到选中地块。 */
    void ApplyActiveHeightLevel();
};
