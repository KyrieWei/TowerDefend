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
 * 使用距离驱动分层算法生成地形：中心平原、边缘深海、海岸过渡带，
 * 内陆区通过 Noise + 随机散布山地/丘陵/森林，
 * 沼泽/河流以 2-3 格连通集群形式放置。
 * 支持对称地图生成（PVP 公平性）和后处理约束（基地平坦化、高度平滑）。
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

    /** 地图半径（六边形格子数），仅在非矩形布局时使用。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "1", ClampMax = "50", EditCondition = "!bRectangularLayout"))
    int32 MapRadius = 15;

    /** 是否使用矩形布局（否则为传统六边形布局） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator")
    bool bRectangularLayout = false;

    /** 矩形布局的列数（每行格子数） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "1", ClampMax = "100", EditCondition = "bRectangularLayout"))
    int32 MapColumns = 20;

    /** 矩形布局的行数 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator",
        meta = (ClampMin = "1", ClampMax = "100", EditCondition = "bRectangularLayout"))
    int32 MapRows = 20;

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
    // 距离驱动地形生成配置
    // ---------------------------------------------------------------

    /** 深海区域起始的归一化距离阈值（距中心比例 0-1）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "0.5", ClampMax = "1.0"))
    float EdgeDeepWaterThreshold = 0.85f;

    /** 海岸过渡带起始的归一化距离阈值。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "0.3", ClampMax = "1.0"))
    float CoastalTransitionStart = 0.75f;

    /** 内陆区山地（Mountain）的生成概率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "0.0", ClampMax = "0.2"))
    float MountainSpawnChance = 0.02f;

    /** 内陆区丘陵（Hill）的生成概率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "0.0", ClampMax = "0.3"))
    float HillSpawnChance = 0.08f;

    /** 内陆区森林（Forest）的生成概率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "0.0", ClampMax = "0.3"))
    float ForestSpawnChance = 0.10f;

    /** 沼泽/河流连通集群的数量（对称化前）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "0", ClampMax = "20"))
    int32 WetlandClusterCount = 6;

    /** 沼泽/河流集群的最小格数。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "1", ClampMax = "5"))
    int32 WetlandClusterMinSize = 2;

    /** 沼泽/河流集群的最大格数。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TerrainGenerator|DistanceDriven",
        meta = (ClampMin = "1", ClampMax = "6"))
    int32 WetlandClusterMaxSize = 3;

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
     * 将连续的 Noise 高度值映射为离散高度等级 [1, 5]。
     *
     * @param NoiseValue  [-1, 1] 范围的 Noise 值。
     * @return            离散高度等级。
     */
    static int32 MapHeightLevel(float NoiseValue);

    /**
     * 基于高度等级和湿度值决定地形类型。
     * 仅保留用于后处理兼容，生成阶段已由距离驱动算法替代。
     *
     * @param InHeightLevel    高度等级 [1, 5]。
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
     * 计算矩形布局下的玩家基地坐标列表。
     * 基地放在矩形对角位置。
     *
     * @param InPlayerCount  玩家数量。
     * @param InColumns      矩形列数。
     * @param InRows         矩形行数。
     * @return               基地坐标数组。
     */
    static TArray<FTDHexCoord> CalculateBasePositionsRect(int32 InPlayerCount, int32 InColumns, int32 InRows);

    /** 生成矩形区域内的所有坐标（even-q offset 布局） */
    static TArray<FTDHexCoord> GenerateRectCoords(int32 Columns, int32 Rows);

    /**
     * 对称化处理：将坐标 (Q, R) 的数据镜像到 (-Q, -R)。
     * 仅在 bSymmetric 为 true 且 PlayerCount == 2 时使用。
     *
     * @param TileDataMap  坐标→格子数据的映射，将被原地修改。
     */
    static void ApplyPointSymmetry(TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap);

    /**
     * 将基地周围指定半径内的格子平坦化为平原 + 高度 1。
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

    // ---------------------------------------------------------------
    // 距离驱动地形生成内部方法
    // ---------------------------------------------------------------

    /**
     * 初始化坐标列表，计算地图中心和最大距离。
     *
     * @param OutAllCoords     输出所有坐标列表。
     * @param OutCenter        输出地图中心坐标。
     * @param OutMaxDistance    输出中心到最远坐标的距离。
     */
    void InitializeCoordinates(
        TArray<FTDHexCoord>& OutAllCoords,
        FTDHexCoord& OutCenter,
        int32& OutMaxDistance) const;

    /**
     * 计算单格到中心的归一化距离 [0, 1]。
     *
     * @param Coord         目标坐标。
     * @param Center        地图中心坐标。
     * @param MaxDistance    中心到最远坐标的距离。
     * @return              归一化距离，范围 [0, 1]。
     */
    static float ComputeNormalizedDistance(
        const FTDHexCoord& Coord,
        const FTDHexCoord& Center,
        int32 MaxDistance);

    /**
     * 根据归一化距离分层分配基础地形：深海/河流/平原。
     *
     * @param AllCoords         所有坐标列表。
     * @param Center            地图中心坐标。
     * @param MaxDistance        最大距离。
     * @param TileDataMap       输出的格子数据映射。
     */
    void AssignBaseLayerByDistance(
        const TArray<FTDHexCoord>& AllCoords,
        const FTDHexCoord& Center,
        int32 MaxDistance,
        TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap) const;

    /**
     * 在内陆平原区域散布山地、丘陵、森林地形。
     * 使用 Noise + 随机 Roll 双重门控。
     *
     * @param TileDataMap   格子数据映射，将被原地修改。
     * @param Center        地图中心坐标。
     * @param MaxDistance    最大距离。
     * @param RandStream    确定性随机流。
     * @param SeedOffset    Noise 偏移量。
     */
    void ScatterTerrainFeatures(
        TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        const FTDHexCoord& Center,
        int32 MaxDistance,
        FRandomStream& RandStream,
        float SeedOffset) const;

    /**
     * 放置多个沼泽/河流连通集群。
     * 先放置河流集群，再放置沼泽集群（沼泽优先在河流旁）。
     *
     * @param TileDataMap   格子数据映射，将被原地修改。
     * @param Center        地图中心坐标。
     * @param MaxDistance    最大距离。
     * @param RandStream    确定性随机流。
     */
    void PlaceWetlandClusters(
        TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        const FTDHexCoord& Center,
        int32 MaxDistance,
        FRandomStream& RandStream) const;

    /**
     * 放置指定类型的地形集群。
     * 沼泽类型会优先选取河流相邻的候选格作为种子。
     *
     * @param TileDataMap       格子数据映射，将被原地修改。
     * @param Candidates        候选种子坐标列表，将被原地修改。
     * @param WetlandType       要放置的地形类型（River 或 Swamp）。
     * @param ClusterCount      要放置的集群数量。
     * @param RandStream        确定性随机流。
     */
    void PlaceTypedClusters(
        TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        TArray<FTDHexCoord>& Candidates,
        ETDTerrainType WetlandType,
        int32 ClusterCount,
        FRandomStream& RandStream) const;

    /**
     * 从种子格向邻居扩展生成一个连通集群。
     *
     * @param SeedCoord     种子坐标。
     * @param TargetSize    目标集群大小。
     * @param TileDataMap   格子数据映射（用于检查邻居可用性）。
     * @param RandStream    确定性随机流。
     * @return              集群坐标列表（可能小于 TargetSize）。
     */
    static TArray<FTDHexCoord> GrowWetlandCluster(
        const FTDHexCoord& SeedCoord,
        int32 TargetSize,
        const TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        FRandomStream& RandStream);

    /**
     * 将集群坐标写入 TileDataMap，设置为指定的地形类型。
     * 高度保持为 1（基准高度）。
     *
     * @param ClusterCoords 集群坐标列表。
     * @param TileDataMap   格子数据映射，将被原地修改。
     * @param WetlandType   要设置的地形类型（River 或 Swamp）。
     */
    static void ApplyWetlandCluster(
        const TArray<FTDHexCoord>& ClusterCoords,
        TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        ETDTerrainType WetlandType);

    /**
     * 为湿地集群选取种子索引。
     * 沼泽类型优先选取邻接河流的候选格。
     *
     * @param Candidates    候选种子坐标列表。
     * @param TileDataMap   格子数据映射（用于查询邻居地形）。
     * @param WetlandType   要放置的地形类型。
     * @param RandStream    确定性随机流。
     * @return              候选列表中的索引。
     */
    static int32 PickWetlandSeed(
        const TArray<FTDHexCoord>& Candidates,
        const TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
        ETDTerrainType WetlandType,
        FRandomStream& RandStream);
};
