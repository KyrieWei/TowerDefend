// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDTerrainModifier.generated.h"

class ATDHexGridManager;
class UTDTechTreeIntegration;

/**
 * UTDTerrainModifier - 运行时地形修改器。
 *
 * 负责在游戏运行时安全地修改六边形格子的高度和地形类型。
 * 所有修改操作都会先经过合法性验证：
 * - 单次操作 ±1 高度等级
 * - 高度范围 [-2, 3]
 * - 相邻格子高度差不超过 3
 *
 * 修改成功后自动更新目标 Tile 的视觉表现。
 */
UCLASS(Blueprintable, BlueprintType)
class UTDTerrainModifier : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 高度修改
    // ---------------------------------------------------------------

    /**
     * 升高指定格子的高度。
     * 验证通过后才执行修改并更新视觉。
     *
     * @param Grid   网格管理器。
     * @param Coord  目标格子坐标。
     * @param Amount 升高量，默认 1。会被钳制到单次最大 1。
     * @return       修改是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainModifier")
    bool RaiseTerrain(ATDHexGridManager* Grid, const FTDHexCoord& Coord, int32 Amount = 1);

    /**
     * 降低指定格子的高度。
     * 验证通过后才执行修改并更新视觉。
     *
     * @param Grid   网格管理器。
     * @param Coord  目标格子坐标。
     * @param Amount 降低量，默认 1。会被钳制到单次最大 1。
     * @return       修改是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainModifier")
    bool LowerTerrain(ATDHexGridManager* Grid, const FTDHexCoord& Coord, int32 Amount = 1);

    // ---------------------------------------------------------------
    // 地形类型修改
    // ---------------------------------------------------------------

    /**
     * 变更指定格子的地形类型。
     *
     * @param Grid    网格管理器。
     * @param Coord   目标格子坐标。
     * @param NewType 新的地形类型。
     * @return        修改是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainModifier")
    bool ChangeTerrainType(ATDHexGridManager* Grid, const FTDHexCoord& Coord, ETDTerrainType NewType);

    // ---------------------------------------------------------------
    // 验证
    // ---------------------------------------------------------------

    /**
     * 验证对指定格子施加高度变化是否合法。
     * 检查项：
     * - 格子是否存在
     * - 高度变化后是否在 [-2, 3] 范围内
     * - 高度变化后与所有邻居的高度差是否不超过 3
     *
     * @param Grid        网格管理器。
     * @param Coord       目标格子坐标。
     * @param HeightDelta 高度变化量（正=升高，负=降低）。
     * @return            修改是否合法。
     */
    UFUNCTION(BlueprintPure, Category = "TerrainModifier")
    bool ValidateModification(const ATDHexGridManager* Grid,
        const FTDHexCoord& Coord, int32 HeightDelta) const;

    // ---------------------------------------------------------------
    // 科技树集成
    // ---------------------------------------------------------------

    /** 科技树集成引用，用于限制地形修改等级。 */
    UPROPERTY()
    UTDTechTreeIntegration* TechIntegration = nullptr;

    /** 设置科技树集成引用。 */
    void SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration);

    /**
     * 验证玩家是否有足够的科技等级执行地形修改。
     * @param PlayerIndex 执行修改的玩家索引。
     * @param HeightDelta 高度变化量的绝对值。
     * @return 是否允许修改。
     */
    bool ValidateTechLevel(int32 PlayerIndex, int32 HeightDelta) const;

private:
    /** 单次操作允许的最大高度变化绝对值。 */
    static constexpr int32 MaxSingleHeightChange = 1;

    /** 相邻格子允许的最大高度差。 */
    static constexpr int32 MaxNeighborHeightDiff = 3;

    /**
     * 执行高度修改的内部实现。
     * 假设验证已通过。
     *
     * @param Grid        网格管理器。
     * @param Coord       目标格子坐标。
     * @param HeightDelta 高度变化量。
     * @return            修改是否成功。
     */
    bool ApplyHeightChange(ATDHexGridManager* Grid, const FTDHexCoord& Coord, int32 HeightDelta);
};
