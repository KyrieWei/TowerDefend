// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDTerrainGenerator.h"

UTDTerrainGenerator::UTDTerrainGenerator()
{
}

// ===================================================================
// 核心接口
// ===================================================================

FTDHexGridSaveData UTDTerrainGenerator::GenerateMap()
{
    const int32 ActualSeed = ResolveSeed();
    const float SeedOffset = static_cast<float>(ActualSeed);
    FRandomStream RandStream(ActualSeed);

    // 使用 TMap 便于按坐标随机访问（对称化、平滑等需要）
    TMap<FTDHexCoord, FTDHexTileSaveData> TileDataMap;

    // ---------------------------------------------------------------
    // Step 1: 距离驱动地形基础层
    // ---------------------------------------------------------------
    TArray<FTDHexCoord> AllCoords;
    FTDHexCoord Center(0, 0);
    int32 MaxDistance = 1;
    InitializeCoordinates(AllCoords, Center, MaxDistance);
    TileDataMap.Reserve(AllCoords.Num());

    // 1a-1b. 距离分层：深海 / 海岸河流 / 中心平原
    AssignBaseLayerByDistance(AllCoords, Center, MaxDistance, TileDataMap);

    // 1c. 散布 Mountain / Hill / Forest
    ScatterTerrainFeatures(TileDataMap, Center, MaxDistance, RandStream, SeedOffset);

    // 1d. 放置 Swamp / River 连通集群
    PlaceWetlandClusters(TileDataMap, Center, MaxDistance, RandStream);

    // ---------------------------------------------------------------
    // Step 2: 对称化处理（PVP 公平性）
    // ---------------------------------------------------------------
    if (bSymmetric && PlayerCount == 2)
    {
        ApplyPointSymmetry(TileDataMap);
    }

    // ---------------------------------------------------------------
    // Step 3: 计算基地位置并平坦化周围区域
    // ---------------------------------------------------------------
    TArray<FTDHexCoord> BasePositions;
    if (bRectangularLayout)
    {
        BasePositions = CalculateBasePositionsRect(PlayerCount, MapColumns, MapRows);
    }
    else
    {
        BasePositions = CalculateBasePositions(PlayerCount, MapRadius);
    }
    FlattenAroundBases(TileDataMap, BasePositions, BaseFlattenRadius);

    // 标记基地归属
    for (int32 PlayerIdx = 0; PlayerIdx < BasePositions.Num(); ++PlayerIdx)
    {
        FTDHexTileSaveData* BaseData = TileDataMap.Find(BasePositions[PlayerIdx]);
        if (BaseData)
        {
            BaseData->OwnerPlayerIndex = PlayerIdx;
        }
    }

    // ---------------------------------------------------------------
    // Step 4: 平滑高度差异（相邻格子高度差不超过 3）
    // ---------------------------------------------------------------
    SmoothHeightDifferences(TileDataMap);

    // ---------------------------------------------------------------
    // Step 5: 组装输出
    // ---------------------------------------------------------------
    FTDHexGridSaveData Result;
    Result.MapRadius = MapRadius;
    Result.Seed = ActualSeed;
    Result.Version = 1;
    if (bRectangularLayout)
    {
        Result.MapColumns = MapColumns;
        Result.MapRows = MapRows;
    }
    Result.TileDataList.Reserve(TileDataMap.Num());

    for (auto& Pair : TileDataMap)
    {
        Result.TileDataList.Add(Pair.Value);
    }

    return Result;
}

// ===================================================================
// 内部实现
// ===================================================================

int32 UTDTerrainGenerator::ResolveSeed() const
{
    if (Seed != 0)
    {
        return Seed;
    }

    return FMath::Rand();
}

float UTDTerrainGenerator::SampleNoise(float X, float Y, float Scale, float Offset)
{
    // UE FMath::PerlinNoise2D 返回 [0, 1]，重映射到 [-1, 1]
    const FVector2D NoiseInput(X * Scale + Offset, Y * Scale + Offset);
    return FMath::PerlinNoise2D(NoiseInput) * 2.0f - 1.0f;
}

int32 UTDTerrainGenerator::MapHeightLevel(float NoiseValue)
{
    // 将 [-1, 1] 映射到 [1, 5] 的离散等级
    // 大部分 Noise 范围映射到基准高度 1
    if (NoiseValue < 0.3f)
    {
        return 1;   // 基准高度（大部分地形）
    }
    if (NoiseValue < 0.5f)
    {
        return 2;   // 略高
    }
    if (NoiseValue < 0.7f)
    {
        return 3;   // 丘陵级
    }
    if (NoiseValue < 0.85f)
    {
        return 4;   // 高地
    }

    return 5;       // 山峰
}

ETDTerrainType UTDTerrainGenerator::DetermineTerrainType(int32 InHeightLevel, float MoistureValue)
{
    // 高度 >= 5 → 山地
    if (InHeightLevel >= 5)
    {
        return ETDTerrainType::Mountain;
    }

    // 高度 >= 3 → 丘陵或森林（湿度区分）
    if (InHeightLevel >= 3)
    {
        return (MoistureValue < 0.0f) ? ETDTerrainType::Hill : ETDTerrainType::Forest;
    }

    // 高度 == 2 → 丘陵或森林
    if (InHeightLevel == 2)
    {
        return (MoistureValue < 0.2f) ? ETDTerrainType::Hill : ETDTerrainType::Forest;
    }

    // 高度 == 1（基准） → 主要是平原
    if (MoistureValue > 0.5f)
    {
        return ETDTerrainType::Swamp;
    }
    if (MoistureValue > 0.1f)
    {
        return ETDTerrainType::Forest;
    }

    return ETDTerrainType::Plain;
}

TArray<FTDHexCoord> UTDTerrainGenerator::CalculateBasePositions(int32 InPlayerCount, int32 InMapRadius)
{
    TArray<FTDHexCoord> Positions;
    Positions.Reserve(InPlayerCount);

    // 基地放置在距中心 (MapRadius - 2) 的环上，均匀分布
    const int32 BaseRingRadius = FMath::Max(InMapRadius - 2, 1);

    if (InPlayerCount == 2)
    {
        // 两位玩家：对称放置在南北两端
        Positions.Add(FTDHexCoord(0, -BaseRingRadius));
        Positions.Add(FTDHexCoord(0, BaseRingRadius));
    }
    else
    {
        // 多位玩家：沿环均匀取点
        TArray<FTDHexCoord> Ring = FTDHexCoord(0, 0).GetRing(BaseRingRadius);
        const int32 RingCount = Ring.Num();

        if (RingCount > 0)
        {
            const int32 Step = FMath::Max(RingCount / InPlayerCount, 1);

            for (int32 I = 0; I < InPlayerCount && I * Step < RingCount; ++I)
            {
                Positions.Add(Ring[I * Step]);
            }
        }
    }

    return Positions;
}

TArray<FTDHexCoord> UTDTerrainGenerator::CalculateBasePositionsRect(int32 InPlayerCount, int32 InColumns, int32 InRows)
{
    TArray<FTDHexCoord> Positions;
    Positions.Reserve(InPlayerCount);

    // 矩形布局：基地放在对角位置
    // 左下角 offset (1, 1)，右上角 offset (Columns-2, Rows-2)
    const int32 MarginCol = FMath::Min(1, InColumns - 1);
    const int32 MarginRow = FMath::Min(1, InRows - 1);
    const int32 MaxCol = FMath::Max(InColumns - 2, 0);
    const int32 MaxRow = FMath::Max(InRows - 2, 0);

    // 使用 even-q offset → cube 转换，与 GenerateRectCoords 保持一致
    auto OffsetToCube = [](int32 Col, int32 Row) -> FTDHexCoord
    {
        const int32 Q = Col;
        const int32 R = Row - Col / 2;
        return FTDHexCoord(Q, R);
    };

    if (InPlayerCount == 2)
    {
        // 玩家1：左下角附近，玩家2：右上角附近
        Positions.Add(OffsetToCube(MarginCol, MarginRow));
        Positions.Add(OffsetToCube(MaxCol, MaxRow));
    }
    else
    {
        // 多位玩家：四角分布，最多 4 位
        Positions.Add(OffsetToCube(MarginCol, MarginRow));       // 左下
        Positions.Add(OffsetToCube(MaxCol, MaxRow));             // 右上
        if (InPlayerCount >= 3)
        {
            Positions.Add(OffsetToCube(MaxCol, MarginRow));      // 右下
        }
        if (InPlayerCount >= 4)
        {
            Positions.Add(OffsetToCube(MarginCol, MaxRow));      // 左上
        }
    }

    return Positions;
}

TArray<FTDHexCoord> UTDTerrainGenerator::GenerateRectCoords(int32 Columns, int32 Rows)
{
    TArray<FTDHexCoord> Coords;
    Coords.Reserve(Columns * Rows);
    for (int32 Col = 0; Col < Columns; ++Col)
    {
        for (int32 Row = 0; Row < Rows; ++Row)
        {
            const int32 Q = Col;
            const int32 R = Row - Col / 2;  // even-q offset → cube
            Coords.Add(FTDHexCoord(Q, R));
        }
    }
    return Coords;
}

void UTDTerrainGenerator::ApplyPointSymmetry(TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap)
{
    // 收集需要镜像的坐标对（仅处理 Q>0 或 Q==0&&R>0 的一半）
    TArray<TPair<FTDHexCoord, FTDHexCoord>> MirrorPairs;

    for (auto& Pair : TileDataMap)
    {
        const FTDHexCoord& Coord = Pair.Key;

        if (Coord.Q > 0 || (Coord.Q == 0 && Coord.R > 0))
        {
            FTDHexCoord MirrorCoord(-Coord.Q, -Coord.R);
            MirrorPairs.Add(TPair<FTDHexCoord, FTDHexCoord>(Coord, MirrorCoord));
        }
    }

    // 将正半部分的数据复制到镜像坐标
    for (const auto& MirrorPair : MirrorPairs)
    {
        FTDHexTileSaveData* SourceData = TileDataMap.Find(MirrorPair.Key);
        FTDHexTileSaveData* TargetData = TileDataMap.Find(MirrorPair.Value);

        if (SourceData && TargetData)
        {
            TargetData->TerrainType = SourceData->TerrainType;
            TargetData->HeightLevel = SourceData->HeightLevel;
        }
    }
}

void UTDTerrainGenerator::FlattenAroundBases(
    TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    const TArray<FTDHexCoord>& BasePositions,
    int32 FlattenRadius)
{
    for (const FTDHexCoord& BasePos : BasePositions)
    {
        TArray<FTDHexCoord> FlattenArea = BasePos.GetCoordsInRange(FlattenRadius);

        for (const FTDHexCoord& Coord : FlattenArea)
        {
            FTDHexTileSaveData* TileData = TileDataMap.Find(Coord);
            if (TileData)
            {
                TileData->HeightLevel = 1;
                TileData->TerrainType = ETDTerrainType::Plain;
            }
        }
    }
}

void UTDTerrainGenerator::SmoothHeightDifferences(TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap)
{
    // 迭代平滑，最多执行 10 轮防止死循环
    constexpr int32 MaxIterations = 10;
    constexpr int32 MaxAllowedDiff = 3;

    for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
    {
        bool bModified = false;

        for (auto& Pair : TileDataMap)
        {
            FTDHexTileSaveData& CurrentTile = Pair.Value;

            // 深水地块高度固定，不参与平滑
            if (CurrentTile.TerrainType == ETDTerrainType::DeepWater)
            {
                continue;
            }

            TArray<FTDHexCoord> Neighbors = CurrentTile.Coord.GetAllNeighbors();

            for (const FTDHexCoord& NeighborCoord : Neighbors)
            {
                FTDHexTileSaveData* NeighborTile = TileDataMap.Find(NeighborCoord);
                if (!NeighborTile)
                {
                    continue;
                }

                const int32 HeightDiff = FMath::Abs(
                    CurrentTile.HeightLevel - NeighborTile->HeightLevel);

                if (HeightDiff > MaxAllowedDiff)
                {
                    if (CurrentTile.HeightLevel > NeighborTile->HeightLevel)
                    {
                        CurrentTile.HeightLevel =
                            NeighborTile->HeightLevel + MaxAllowedDiff;
                    }
                    else
                    {
                        CurrentTile.HeightLevel =
                            NeighborTile->HeightLevel - MaxAllowedDiff;
                    }

                    // 钳制到有效范围 [1, 5]
                    CurrentTile.HeightLevel = FMath::Clamp(
                        CurrentTile.HeightLevel, 1, 5);

                    bModified = true;
                }
            }
        }

        if (!bModified)
        {
            break;
        }
    }
}

void UTDTerrainGenerator::SyncTerrainTypeWithHeight(FTDHexTileSaveData& TileData)
{
    // 深水地块高度强制为 1，不可改变
    if (TileData.TerrainType == ETDTerrainType::DeepWater)
    {
        TileData.HeightLevel = 1;
    }
}

// ===================================================================
// 距离驱动地形生成
// ===================================================================

void UTDTerrainGenerator::InitializeCoordinates(
    TArray<FTDHexCoord>& OutAllCoords,
    FTDHexCoord& OutCenter,
    int32& OutMaxDistance) const
{
    if (bRectangularLayout)
    {
        OutAllCoords = GenerateRectCoords(MapColumns, MapRows);
        // 矩形布局的中心：offset 坐标 (Columns/2, Rows/2) 转为 cube
        const int32 CenterCol = MapColumns / 2;
        const int32 CenterRow = MapRows / 2;
        OutCenter = FTDHexCoord(CenterCol, CenterRow - CenterCol / 2);
    }
    else
    {
        const FTDHexCoord Origin(0, 0);
        OutAllCoords = Origin.GetCoordsInRange(MapRadius);
        OutCenter = Origin;
    }

    // 计算最大距离（中心到最远格的距离）
    OutMaxDistance = 1;
    for (const FTDHexCoord& Coord : OutAllCoords)
    {
        const int32 Dist = Coord.DistanceTo(OutCenter);
        if (Dist > OutMaxDistance)
        {
            OutMaxDistance = Dist;
        }
    }
}

float UTDTerrainGenerator::ComputeNormalizedDistance(
    const FTDHexCoord& Coord,
    const FTDHexCoord& Center,
    int32 MaxDistance)
{
    if (MaxDistance <= 0)
    {
        return 0.0f;
    }

    const int32 Dist = Coord.DistanceTo(Center);
    return FMath::Clamp(
        static_cast<float>(Dist) / static_cast<float>(MaxDistance),
        0.0f, 1.0f);
}

void UTDTerrainGenerator::AssignBaseLayerByDistance(
    const TArray<FTDHexCoord>& AllCoords,
    const FTDHexCoord& Center,
    int32 MaxDistance,
    TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap) const
{
    for (const FTDHexCoord& Coord : AllCoords)
    {
        const float NormDist = ComputeNormalizedDistance(Coord, Center, MaxDistance);

        // 所有地块默认高度为 1（基准高度）
        ETDTerrainType Terrain = ETDTerrainType::Plain;
        constexpr int32 BaseHeight = 1;

        if (NormDist >= EdgeDeepWaterThreshold)
        {
            // 边缘深海：高度固定为 1，不可修改
            Terrain = ETDTerrainType::DeepWater;
        }
        else if (NormDist >= CoastalTransitionStart)
        {
            // 海岸过渡带 → 河流
            Terrain = ETDTerrainType::River;
        }

        TileDataMap.Add(Coord, FTDHexTileSaveData(Coord, Terrain, BaseHeight));
    }
}

void UTDTerrainGenerator::ScatterTerrainFeatures(
    TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    const FTDHexCoord& Center,
    int32 MaxDistance,
    FRandomStream& RandStream,
    float SeedOffset) const
{
    for (auto& Pair : TileDataMap)
    {
        FTDHexTileSaveData& TileData = Pair.Value;

        // 仅对内陆平原区域散布地形特征
        if (TileData.TerrainType != ETDTerrainType::Plain)
        {
            continue;
        }

        // 安全中心区不做散布
        const float NormDist = ComputeNormalizedDistance(
            TileData.Coord, Center, MaxDistance);
        if (NormDist < 0.1f)
        {
            continue;
        }

        // Noise 采样用于空间聚集性
        const float SampleX = static_cast<float>(TileData.Coord.Q);
        const float SampleY = static_cast<float>(TileData.Coord.R);
        const float HeightNoise = SampleNoise(
            SampleX, SampleY, HeightNoiseScale, SeedOffset);
        const float MoistureNoise = SampleNoise(
            SampleX, SampleY, MoistureNoiseScale, SeedOffset + 1000.0f);

        const float Roll = RandStream.FRand();

        // Mountain: 稀少但略微聚集（Noise 门控），高度保持 1
        if (Roll < MountainSpawnChance && HeightNoise > 0.3f)
        {
            TileData.TerrainType = ETDTerrainType::Mountain;
        }
        // Hill: 中等概率，高度保持 1
        else if (Roll < MountainSpawnChance + HillSpawnChance
            && HeightNoise > 0.0f)
        {
            TileData.TerrainType = ETDTerrainType::Hill;
        }
        // Forest: 湿润区域偏好，高度保持 1
        else if (Roll < MountainSpawnChance + HillSpawnChance + ForestSpawnChance
            && MoistureNoise > -0.2f)
        {
            TileData.TerrainType = ETDTerrainType::Forest;
        }
    }
}

void UTDTerrainGenerator::PlaceWetlandClusters(
    TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    const FTDHexCoord& Center,
    int32 MaxDistance,
    FRandomStream& RandStream) const
{
    // 收集内陆平原区的候选种子格
    TArray<FTDHexCoord> Candidates;
    for (const auto& Pair : TileDataMap)
    {
        if (Pair.Value.TerrainType != ETDTerrainType::Plain)
        {
            continue;
        }

        const float NormDist = ComputeNormalizedDistance(
            Pair.Key, Center, MaxDistance);
        if (NormDist >= 0.15f && NormDist < 0.70f)
        {
            Candidates.Add(Pair.Key);
        }
    }

    // 先放河流集群（约一半），再放沼泽集群（尽量靠近河流）
    const int32 RiverCount = FMath::Max(WetlandClusterCount / 2, 1);
    const int32 SwampCount = WetlandClusterCount - RiverCount;

    PlaceTypedClusters(TileDataMap, Candidates,
        ETDTerrainType::River, RiverCount, RandStream);
    PlaceTypedClusters(TileDataMap, Candidates,
        ETDTerrainType::Swamp, SwampCount, RandStream);
}

TArray<FTDHexCoord> UTDTerrainGenerator::GrowWetlandCluster(
    const FTDHexCoord& SeedCoord,
    int32 TargetSize,
    const TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    FRandomStream& RandStream)
{
    TArray<FTDHexCoord> Cluster;
    Cluster.Add(SeedCoord);

    for (int32 GrowStep = 1; GrowStep < TargetSize; ++GrowStep)
    {
        // 从当前集群最后一个格子向邻居扩展
        const FTDHexCoord& LastCoord = Cluster.Last();
        TArray<FTDHexCoord> Neighbors = LastCoord.GetAllNeighbors();

        // 筛选可用邻居：存在于地图中、是平原、且未在集群中
        TArray<FTDHexCoord> ValidNeighbors;
        for (const FTDHexCoord& Neighbor : Neighbors)
        {
            const FTDHexTileSaveData* NeighborTile = TileDataMap.Find(Neighbor);
            if (NeighborTile
                && NeighborTile->TerrainType == ETDTerrainType::Plain
                && !Cluster.Contains(Neighbor))
            {
                ValidNeighbors.Add(Neighbor);
            }
        }

        if (ValidNeighbors.Num() == 0)
        {
            break;
        }

        const int32 PickIdx = RandStream.RandRange(0, ValidNeighbors.Num() - 1);
        Cluster.Add(ValidNeighbors[PickIdx]);
    }

    return Cluster;
}

void UTDTerrainGenerator::PlaceTypedClusters(
    TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    TArray<FTDHexCoord>& Candidates,
    ETDTerrainType WetlandType,
    int32 ClusterCount,
    FRandomStream& RandStream) const
{
    int32 RetryBudget = ClusterCount * 3;

    for (int32 ClusterIdx = 0; ClusterIdx < ClusterCount; ++ClusterIdx)
    {
        if (Candidates.Num() == 0 || RetryBudget <= 0)
        {
            break;
        }

        int32 SeedIdx = PickWetlandSeed(
            Candidates, TileDataMap, WetlandType, RandStream);

        const FTDHexCoord SeedCoord = Candidates[SeedIdx];
        const FTDHexTileSaveData* SeedTile = TileDataMap.Find(SeedCoord);

        if (!SeedTile || SeedTile->TerrainType != ETDTerrainType::Plain)
        {
            Candidates.RemoveAtSwap(SeedIdx);
            --ClusterIdx;
            --RetryBudget;
            continue;
        }

        const int32 TargetSize = RandStream.RandRange(
            WetlandClusterMinSize, WetlandClusterMaxSize);
        TArray<FTDHexCoord> Cluster = GrowWetlandCluster(
            SeedCoord, TargetSize, TileDataMap, RandStream);
        ApplyWetlandCluster(Cluster, TileDataMap, WetlandType);

        for (const FTDHexCoord& UsedCoord : Cluster)
        {
            Candidates.RemoveSwap(UsedCoord);
        }
    }
}

void UTDTerrainGenerator::ApplyWetlandCluster(
    const TArray<FTDHexCoord>& ClusterCoords,
    TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    ETDTerrainType WetlandType)
{
    for (const FTDHexCoord& Coord : ClusterCoords)
    {
        FTDHexTileSaveData* TileData = TileDataMap.Find(Coord);
        if (TileData)
        {
            TileData->TerrainType = WetlandType;
            // 高度保持为 1（基准高度）
        }
    }
}

int32 UTDTerrainGenerator::PickWetlandSeed(
    const TArray<FTDHexCoord>& Candidates,
    const TMap<FTDHexCoord, FTDHexTileSaveData>& TileDataMap,
    ETDTerrainType WetlandType,
    FRandomStream& RandStream)
{
    // 沼泽优先选取邻接河流地块的候选格
    if (WetlandType == ETDTerrainType::Swamp)
    {
        TArray<int32> RiverAdjacentIndices;
        for (int32 Idx = 0; Idx < Candidates.Num(); ++Idx)
        {
            TArray<FTDHexCoord> Neighbors = Candidates[Idx].GetAllNeighbors();
            for (const FTDHexCoord& Neighbor : Neighbors)
            {
                const FTDHexTileSaveData* NeighborTile = TileDataMap.Find(Neighbor);
                if (NeighborTile
                    && NeighborTile->TerrainType == ETDTerrainType::River)
                {
                    RiverAdjacentIndices.Add(Idx);
                    break;
                }
            }
        }

        if (RiverAdjacentIndices.Num() > 0)
        {
            const int32 PickIdx = RandStream.RandRange(
                0, RiverAdjacentIndices.Num() - 1);
            return RiverAdjacentIndices[PickIdx];
        }
    }

    // 默认：随机选取
    return RandStream.RandRange(0, Candidates.Num() - 1);
}
