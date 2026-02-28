// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDHexTile.generated.h"

/**
 * ATDHexTile - 六边形格子 Actor。
 *
 * 代表地图上一个六边形格子的可视化实体。
 * 持有地形类型、高度等级等逻辑属性，
 * 并负责驱动对应的 Mesh 高度和材质外观更新。
 *
 * 每个 Tile 使用独立 StaticMeshComponent + 动态材质实例，
 * 以支持逐格子高度变化和地形外观切换。
 * 坐标由外部（TDHexGridManager）在创建时设置，不可运行时修改。
 */
UCLASS()
class TOWERDEFEND_API ATDHexTile : public AActor
{
    GENERATED_BODY()

public:
    ATDHexTile();

    // ---------------------------------------------------------------
    // 初始化
    // ---------------------------------------------------------------

    /**
     * 使用保存数据初始化此格子。
     * 设置坐标、地形类型、高度等级和归属玩家，
     * 并更新视觉表现。应在 Spawn 后立即调用一次。
     *
     * @param InSaveData  格子保存数据。
     * @param HexSize     六边形外接圆半径（世界单位），用于坐标→世界位置转换。
     */
    void InitFromSaveData(const FTDHexTileSaveData& InSaveData, float HexSize);

    // ---------------------------------------------------------------
    // 地形属性访问
    // ---------------------------------------------------------------

    /** 获取此格子的六边形坐标。 */
    UFUNCTION(BlueprintPure, Category = "HexTile")
    FTDHexCoord GetCoord() const { return Coord; }

    /** 获取当前地形类型。 */
    UFUNCTION(BlueprintPure, Category = "HexTile")
    ETDTerrainType GetTerrainType() const { return TerrainType; }

    /** 获取当前高度等级。 */
    UFUNCTION(BlueprintPure, Category = "HexTile")
    int32 GetHeightLevel() const { return HeightLevel; }

    /** 获取所属玩家索引，-1 为中立。 */
    UFUNCTION(BlueprintPure, Category = "HexTile")
    int32 GetOwnerPlayerIndex() const { return OwnerPlayerIndex; }

    // ---------------------------------------------------------------
    // 地形属性修改
    // ---------------------------------------------------------------

    /**
     * 设置地形类型并更新材质外观。
     *
     * @param NewType  新的地形类型。
     */
    UFUNCTION(BlueprintCallable, Category = "HexTile")
    void SetTerrainType(ETDTerrainType NewType);

    /**
     * 设置高度等级并更新 Mesh 的 Z 轴位置。
     * 高度会被钳制到 [-2, 3] 范围内。
     *
     * @param NewHeight  新的高度等级。
     */
    UFUNCTION(BlueprintCallable, Category = "HexTile")
    void SetHeightLevel(int32 NewHeight);

    /**
     * 设置所属玩家索引。
     *
     * @param NewOwner  玩家索引，-1 表示中立。
     */
    UFUNCTION(BlueprintCallable, Category = "HexTile")
    void SetOwnerPlayerIndex(int32 NewOwner);

    // ---------------------------------------------------------------
    // 游戏逻辑查询
    // ---------------------------------------------------------------

    /**
     * 返回此格子的移动消耗。
     * 不可通行格子返回极大值 (BIG_NUMBER)。
     */
    UFUNCTION(BlueprintPure, Category = "HexTile|Gameplay")
    float GetMovementCost() const;

    /**
     * 返回此格子的防御加成百分比（0.0 = 无加成，0.2 = +20%）。
     */
    UFUNCTION(BlueprintPure, Category = "HexTile|Gameplay")
    float GetDefenseBonus() const;

    /**
     * 此格子是否可通行。
     * 山地和深水默认不可通行。
     */
    UFUNCTION(BlueprintPure, Category = "HexTile|Gameplay")
    bool IsPassable() const;

    /**
     * 此格子是否可建造普通建筑。
     * 山地、深水、沼泽不可建造。
     */
    UFUNCTION(BlueprintPure, Category = "HexTile|Gameplay")
    bool IsBuildable() const;

    // ---------------------------------------------------------------
    // 序列化
    // ---------------------------------------------------------------

    /** 导出当前状态为保存数据。 */
    FTDHexTileSaveData ExportSaveData() const;

    // ---------------------------------------------------------------
    // 视觉参数
    // ---------------------------------------------------------------

    /** 每个高度等级在世界空间中对应的 Z 轴偏移量（厘米）。 */
    static constexpr float HeightLevelUnitZ = 50.0f;

    /** 高度等级最小值。 */
    static constexpr int32 MinHeightLevel = -2;

    /** 高度等级最大值。 */
    static constexpr int32 MaxHeightLevel = 3;

protected:
    // ---------------------------------------------------------------
    // 核心数据
    // ---------------------------------------------------------------

    /** 格子在六边形网格中的坐标。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HexTile")
    FTDHexCoord Coord;

    /** 当前地形类型。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HexTile")
    ETDTerrainType TerrainType = ETDTerrainType::Plain;

    /** 当前高度等级，有效范围 [-2, 3]。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HexTile",
        meta = (ClampMin = "-2", ClampMax = "3"))
    int32 HeightLevel = 0;

    /** 所属玩家索引，-1 表示中立。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HexTile")
    int32 OwnerPlayerIndex = -1;

    // ---------------------------------------------------------------
    // 视觉组件
    // ---------------------------------------------------------------

    /** 六边形 Mesh 组件，作为根组件。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HexTile|Visual")
    UStaticMeshComponent* HexMeshComponent = nullptr;

    /** 运行时创建的动态材质实例，用于切换地形外观颜色。 */
    UPROPERTY(Transient)
    UMaterialInstanceDynamic* TerrainMaterial = nullptr;

    // ---------------------------------------------------------------
    // 内部视觉更新
    // ---------------------------------------------------------------

    /** 根据当前 HeightLevel 更新 Actor 的 Z 轴位置。 */
    void UpdateVisualHeight();

    /** 根据当前 TerrainType 更新动态材质参数。 */
    void UpdateVisualMaterial();

    /**
     * 获取地形类型对应的基础颜色。
     * 用于在没有纹理资源时以纯色区分地形。
     */
    static FLinearColor GetTerrainBaseColor(ETDTerrainType Type);
};
