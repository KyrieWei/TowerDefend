// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDTerrainGenerator.generated.h"

/**
 * UTDTerrainGenerator - 程序化地形生成器。
 *
 * 纯数据逻辑组件，不涉及渲染。
 * 使用 Perlin/Simplex Noise 生成高度场和湿度场，
 * 基于高度+湿度决定地形类型，
 * 支持对称地图生成（PVP 公平性）和后处理约束（连通性、基地平坦化）。
 *
 * 输出 FTDHexGridSaveData，供 TDHexGridManager 消费。
 */
UCLASS(Blueprintable, BlueprintType, DefaultToInstanced, EditInlineNew)
class UTDTerrainGenerator : public UObject
{
    GENERATED_BODY()

public:
    UTDTerrainGenerator();

    // ---------------------------------------------------------------
    // 生成配置（UPROPERTY 可在编辑器 / 蓝图中配置）
    // ---------------------------------------------------------------

    /** 地图半径（六边形格子数）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "1", ClampMax = "50"))
    int32 MapRadius = 15;

    /** 随机种子。0 = 使用随机种子。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator")
    int32 Seed = 0;

    /** 高度 Noise 缩放因子，值越小地形越平滑。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float HeightNoiseScale = 0.08f;

    /** 湿度 Noise 缩放因子。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "0.01", ClampMax = "1.0"))
    float MoistureNoiseScale = 0.12f;

    /** 是否生成中心对称地图（PVP 公平性）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator")
    bool bSymmetric = true;

    /** 玩家数量，影响基地点位分布。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "2", ClampMax = "8"))
    int32 PlayerCount = 2;

    /** 基地周围强制平坦化的半径。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "1", ClampMax = "5"))
    int32 BaseFlattenRadius = 2;

    // ---------------------------------------------------------------
    // 核心接口
    // ---------------------------------------------------------------

    /**
     * 根据当前配置生成完整地图数据。
     * 包括 Noise 生成、地形类型分配、对称化、后处理。
     *
     * @return  完整的地图保存数据。
     */
    UFUNCTION(BlueprintCallable, Category = "TerrainGenerator")
    FTDHexGridSaveData GenerateMap();

private:
    // ---------------------------------------------------------------
    // 内部生成流程
    // ---------------------------------------------------------------

    /** 确定实际使用的种子值（处理 Seed==0 的随机情况）。 */
    int32 ResolveSeed() const;

    /**
     * 采样 2D Perlin Noise，返回 [-1, 1] 范围的值。
     * 使用 UE 内置 FMath::PerlinNoise2D。
     *
     * @param X      采样 X 坐标。
     * @param Y      采样 Y 坐标。
     * @param Scale  Noise 缩放因子。
     * @param Offset Noise 偏移量（用于不同 Noise 层区分）。
     * @return       [-1, 1] 范围内的 Noise 值。
     */
    static float SampleNoise(float X, float Y, float Scale, float Offset);

    /**
     * 将连续的 Noise 高度值映射为离散高度等级 [-2, 3]。
     *
     * @param NoiseValue  [-1, 1] 范围的 Noise 值。
     * @return            离散高度等级。
     */
    static int32 MapHeightLevel(float NoiseValue);

    /**
     * 基于高度等级和湿度值决定地形类型。
     *
     * @param InHeightLevel    高度等级 [-2, 3]。
     * @param MoistureValue    [-1, 1] 范围的湿度值。
     * @return                 对应的地形类型。
     */
    static ETDTerrainType DetermineTerrainType(int32 InHeightLevel, float MoistureValue);

    /**
     * 计算玩家基地坐标列表。
     * 基地均匀分布在地图边缘环上。
     *
     * @param InPlayerCount  玩家数量。
     * @param InMapRadius    地图半径。
     * @return               基地坐标数组。
     */
    static TArray<FTDHexCoord> CalculateBasePositions(int32 InPlayerCount, int32 InMapRadius);

    /**
     * 对称化处理：将坐标 (Q, R) 的数据镜像到 (-Q, -R)。
     * 仅在 bSymmetric 为 true 且 PlayerCount == 2 时使用。
     *
     * @param TileDataMap  坐标→格子数据的映射，将被原地修改。
     */
    static void ApplyPointSymmetry(TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap);

    /**
     * 将基地周围指定半径内的格子平坦化为平原 + 高度 0。
     *
     * @param TileDataMap   坐标→格子数据的映射，将被原地修改。
     * @param BasePositions 所有基地坐标。
     * @param FlattenRadius 平坦化半径。
     */
    static void FlattenAroundBases(
        TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        const TArray<FTDHexCoord>& BasePositions,
        int32 FlattenRadius);

    /**
     * 平滑相邻格子高度差超过 3 的区域。
     * 迭代执行直到不再有违规高度差。
     *
     * @param TileDataMap  坐标→格子数据的映射，将被原地修改。
     */
    static void SmoothHeightDifferences(TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap);

    /**
     * 高度变化后同步更新地形类型（保持高度与类型的一致性）。
     *
     * @param TileData  需要更新的格子数据。
     */
    static void SyncTerrainTypeWithHeight(FTDHexTileSaveData& TileData);
};
