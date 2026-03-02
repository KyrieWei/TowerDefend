// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDHexGridManager.generated.h"

class ATDHexTile;
class UTDTerrainGenerator;

/**
 * ATDHexGridManager - 六边形网格管理器。
 *
 * 负责管理整个六边形地图的生命周期：
 * - 生成网格（通过 TDTerrainGenerator 获取数据，实例化 ATDHexTile）
 * - O(1) 坐标查询
 * - 范围查询、邻居查询
 * - 存档导入导出
 *
 * 将来会被 ATDGameMode 持有，当前可独立放入关卡运行。
 * 可在编辑器中拖入场景，通过 GenerateGrid 生成网格。
 */
UCLASS()
class TOWERDEFEND_API ATDHexGridManager : public AActor
{
    GENERATED_BODY()

public:
    ATDHexGridManager();

    // ---------------------------------------------------------------
    // 配置参数
    // ---------------------------------------------------------------

    /** 地图半径（格子数），传入 GenerateGrid 时使用。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexGrid",
        meta = (ClampMin = "1", ClampMax = "50", EditCondition = "!bRectangularLayout"))
    int32 MapRadius = 15;

    /** 是否使用矩形布局 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexGrid")
    bool bRectangularLayout = false;

    /** 矩形布局的列数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexGrid",
        meta = (ClampMin = "1", ClampMax = "100", EditCondition = "bRectangularLayout"))
    int32 MapColumns = 20;

    /** 矩形布局的行数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexGrid",
        meta = (ClampMin = "1", ClampMax = "100", EditCondition = "bRectangularLayout"))
    int32 MapRows = 20;

    /** 六边形外接圆半径（世界单位，厘米）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexGrid",
        meta = (ClampMin = "10.0", ClampMax = "500.0"))
    float HexSize = 100.0f;

    /** 用于生成地形的生成器配置对象。运行时自动创建，也可在蓝图中预先配置。 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "HexGrid")
    UTDTerrainGenerator* TerrainGenerator = nullptr;

    /** 生成 Tile 所用的 Actor 类。默认为 ATDHexTile，可在蓝图中替换为子类。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexGrid")
    TSubclassOf<ATDHexTile> TileActorClass;

    // ---------------------------------------------------------------
    // 网格生成 / 清除
    // ---------------------------------------------------------------

    /**
     * 生成完整六边形网格。
     * 先清空旧网格，然后使用 TerrainGenerator 生成数据并实例化 Tile。
     *
     * @param Radius  地图半径。0 表示使用 MapRadius 属性值。
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "HexGrid")
    void GenerateGrid(int32 Radius = 0);

    /**
     * 清空当前网格，销毁所有 Tile Actor。
     */
    UFUNCTION(BlueprintCallable, CallInEditor, Category = "HexGrid")
    void ClearGrid();

    // ---------------------------------------------------------------
    // 查询接口
    // ---------------------------------------------------------------

    /**
     * O(1) 根据坐标查找格子。
     *
     * @param Coord  六边形坐标。
     * @return       对应的 Tile Actor，不存在时返回 nullptr。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    ATDHexTile* GetTileAt(const FTDHexCoord& Coord) const;

    /**
     * 获取指定坐标的所有邻居格子（最多 6 个，边缘可能少于 6）。
     *
     * @param Coord  中心格子坐标。
     * @return       存在于地图中的邻居 Tile 数组。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    TArray<ATDHexTile*> GetNeighborTiles(const FTDHexCoord& Coord) const;

    /**
     * 获取指定中心和半径内的所有格子（含中心格子，边缘可能不完整）。
     *
     * @param Center  中心坐标。
     * @param Range   查询半径。
     * @return        范围内存在于地图中的 Tile 数组。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    TArray<ATDHexTile*> GetTilesInRange(const FTDHexCoord& Center, int32 Range) const;

    /**
     * Get all tile actors in the current grid.
     *
     * @return  Array of all tile actors.
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    TArray<ATDHexTile*> GetAllTiles() const;

    /**
     * 获取当前地图中所有格子的数量。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    int32 GetTileCount() const;

    /**
     * 获取当前地图的有效半径。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    int32 GetCurrentMapRadius() const { return CurrentMapRadius; }

    /**
     * 获取六边形外接圆半径。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid")
    float GetHexSize() const { return HexSize; }

    // ---------------------------------------------------------------
    // 存档接口
    // ---------------------------------------------------------------

    /**
     * 从保存数据恢复网格。清空当前网格后重建。
     *
     * @param Data  完整的地图保存数据。
     */
    UFUNCTION(BlueprintCallable, Category = "HexGrid|Save")
    void ApplySaveData(const FTDHexGridSaveData& Data);

    /**
     * 导出当前网格状态为保存数据。
     *
     * @return  完整的地图保存数据。
     */
    UFUNCTION(BlueprintPure, Category = "HexGrid|Save")
    FTDHexGridSaveData ExportSaveData() const;

protected:
    virtual void BeginPlay() override;

private:
    // ---------------------------------------------------------------
    // 内部数据
    // ---------------------------------------------------------------

    /** 坐标 → Tile Actor 映射表，支持 O(1) 查找。 */
    UPROPERTY()
    TMap<FTDHexCoord, ATDHexTile*> TileMap;

    /** 当前已生成地图的实际半径。 */
    int32 CurrentMapRadius = 0;

    /** 上次生成地图时使用的种子。 */
    int32 LastUsedSeed = 0;

    // ---------------------------------------------------------------
    // 内部方法
    // ---------------------------------------------------------------

    /**
     * 根据保存数据实例化所有 Tile Actor 并注册到 TileMap。
     *
     * @param Data  完整的地图保存数据。
     */
    void SpawnTilesFromData(const FTDHexGridSaveData& Data);

    /** 确保 TerrainGenerator 存在，不存在则创建默认实例。 */
    void EnsureTerrainGenerator();
};
