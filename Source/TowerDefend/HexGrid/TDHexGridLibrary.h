// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDHexGridLibrary.generated.h"

/**
 * UTDHexGridLibrary - HexGrid 蓝图常量与辅助工具库。
 *
 * 将 HexGrid 模块中散落在各类的 static constexpr 常量和
 * 地形类型辅助查询统一暴露给蓝图，无需手动获取 C++ 类实例。
 *
 * 分为三组：
 * - Tile 几何常量（无需 WorldContext）
 * - 地形类型辅助（无需 WorldContext）
 * - 地图配置查询（需要 WorldContext 获取 GridManager）
 */
UCLASS()
class TOWERDEFEND_API UTDHexGridLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    // ===============================================================
    //  地块几何常量（TD|HexGrid|Tile）
    // ===============================================================

    /** 六边形外接圆默认半径（世界单位，厘米）。 */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Tile")
    static float GetDefaultHexSize();

    /** 每个高度等级在世界空间中对应的 Z 轴偏移量（厘米）。 */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Tile")
    static float GetHeightLevelUnitZ();

    /** 高度等级最小值。 */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Tile")
    static int32 GetMinHeightLevel();

    /** 高度等级最大值。 */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Tile")
    static int32 GetMaxHeightLevel();

    /** 相邻格子允许的最大高度差。 */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Tile")
    static int32 GetMaxNeighborHeightDiff();

    // ===============================================================
    //  地形类型辅助（TD|HexGrid|Terrain）
    // ===============================================================

    /**
     * 获取地形类型的本地化显示名称。
     *
     * @param TerrainType  地形类型枚举值。
     * @return             本地化显示名称。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Terrain")
    static FText GetTerrainTypeDisplayName(ETDTerrainType TerrainType);

    /**
     * 获取所有有效地形类型（排除 MAX 哨兵）。
     *
     * @return  包含所有有效地形类型的数组。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Terrain")
    static TArray<ETDTerrainType> GetAllTerrainTypes();

    /**
     * 判断指定地形类型是否可通行。
     * Mountain 和 DeepWater 不可通行。
     *
     * @param TerrainType  地形类型枚举值。
     * @return             是否可通行。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Terrain")
    static bool IsTerrainPassable(ETDTerrainType TerrainType);

    /**
     * 判断指定地形类型是否可建造。
     * Mountain、DeepWater、Swamp、River 不可建造。
     *
     * @param TerrainType  地形类型枚举值。
     * @return             是否可建造。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Terrain")
    static bool IsTerrainBuildable(ETDTerrainType TerrainType);

    // ===============================================================
    //  地图配置查询（TD|HexGrid|Map）— 需要 WorldContext
    // ===============================================================

    /**
     * 获取当前地图的有效半径（已生成地图的实际半径）。
     *
     * @param WorldContextObject  世界上下文。
     * @return                    地图半径，获取失败返回 0。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Map",
        meta = (WorldContext = "WorldContextObject"))
    static int32 GetCurrentMapRadius(const UObject* WorldContextObject);

    /**
     * 获取矩形布局的列数。
     *
     * @param WorldContextObject  世界上下文。
     * @return                    列数，获取失败返回 0。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Map",
        meta = (WorldContext = "WorldContextObject"))
    static int32 GetCurrentMapColumns(const UObject* WorldContextObject);

    /**
     * 获取矩形布局的行数。
     *
     * @param WorldContextObject  世界上下文。
     * @return                    行数，获取失败返回 0。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Map",
        meta = (WorldContext = "WorldContextObject"))
    static int32 GetCurrentMapRows(const UObject* WorldContextObject);

    /**
     * 是否使用矩形布局。
     *
     * @param WorldContextObject  世界上下文。
     * @return                    是否矩形布局，获取失败返回 false。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Map",
        meta = (WorldContext = "WorldContextObject"))
    static bool IsRectangularLayout(const UObject* WorldContextObject);

    /**
     * 获取当前实际使用的六边形外接圆半径（世界单位）。
     *
     * @param WorldContextObject  世界上下文。
     * @return                    HexSize，获取失败返回 0.0。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Map",
        meta = (WorldContext = "WorldContextObject"))
    static float GetCurrentHexSize(const UObject* WorldContextObject);

    /**
     * 获取当前地图中格子总数。
     *
     * @param WorldContextObject  世界上下文。
     * @return                    格子数量，获取失败返回 0。
     */
    UFUNCTION(BlueprintPure, Category = "TD|HexGrid|Map",
        meta = (WorldContext = "WorldContextObject"))
    static int32 GetCurrentTileCount(const UObject* WorldContextObject);
};
