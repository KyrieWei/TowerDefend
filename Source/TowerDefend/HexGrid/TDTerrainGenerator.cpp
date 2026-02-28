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

    // 使用 TMap 便于按坐标随机访问（对称化、平滑等需要）
    TMap<FTDHexCoord, FTDHexTileSaveData> TileDataMap;

    // 预估格子总数: 3*R^2 + 3*R + 1
    const int32 EstimatedCount = 3 * MapRadius * MapRadius + 3 * MapRadius + 1;
    TileDataMap.Reserve(EstimatedCount);

    // ---------------------------------------------------------------
    // Step 1: Noise 生成高度场和湿度场
    // ---------------------------------------------------------------
    const FTDHexCoord Origin(0, 0);
    TArray<FTDHexCoord> AllCoords = Origin.GetCoordsInRange(MapRadius);

    for (const FTDHexCoord& Coord : AllCoords)
    {
        const float SampleX = static_cast<float>(Coord.Q);
        const float SampleY = static_cast<float>(Coord.R);

        const float HeightNoise = SampleNoise(SampleX, SampleY, HeightNoiseScale, SeedOffset);
        const float MoistureNoise = SampleNoise(SampleX, SampleY, MoistureNoiseScale, SeedOffset + 1000.0f);

        const int32 Height = MapHeightLevel(HeightNoise);
        const ETDTerrainType Terrain = DetermineTerrainType(Height, MoistureNoise);

        FTDHexTileSaveData TileData(Coord, Terrain, Height);
        TileDataMap.Add(Coord, TileData);
    }

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
    TArray<FTDHexCoord> BasePositions = CalculateBasePositions(PlayerCount, MapRadius);
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
    // 将 [-1, 1] 映射到 [-2, 3] 的离散等级
    // 分段阈值设计：偏向中间高度，极端高度较少
    if (NoiseValue < -0.6f)
    {
        return -2;  // 深水
    }
    if (NoiseValue < -0.3f)
    {
        return -1;  // 浅水/沼泽
    }
    if (NoiseValue < 0.15f)
    {
        return 0;   // 平原
    }
    if (NoiseValue < 0.4f)
    {
        return 1;   // 丘陵
    }
    if (NoiseValue < 0.7f)
    {
        return 2;   // 高地
    }

    return 3;       // 山地
}

ETDTerrainType UTDTerrainGenerator::DetermineTerrainType(int32 InHeightLevel, float MoistureValue)
{
    // 高度 >= 3 → 山地
    if (InHeightLevel >= 3)
    {
        return ETDTerrainType::Mountain;
    }

    // 高度 <= -2 → 深水
    if (InHeightLevel <= -2)
    {
        return ETDTerrainType::DeepWater;
    }

    // 高度 == -1 → 水域（湿度高则沼泽，否则河流）
    if (InHeightLevel == -1)
    {
        return (MoistureValue > 0.0f) ? ETDTerrainType::Swamp : ETDTerrainType::River;
    }

    // 高度 == 2 → 丘陵（低湿度）或森林（高湿度）
    if (InHeightLevel == 2)
    {
        return (MoistureValue < 0.0f) ? ETDTerrainType::Hill : ETDTerrainType::Forest;
    }

    // 高度 == 1 → 丘陵（低湿度）或森林（高湿度）
    if (InHeightLevel == 1)
    {
        return (MoistureValue < 0.2f) ? ETDTerrainType::Hill : ETDTerrainType::Forest;
    }

    // 高度 == 0 → 平原（低湿度）、森林（中湿度）、沼泽（高湿度）
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
                TileData->HeightLevel = 0;
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
            TArray<FTDHexCoord> Neighbors = CurrentTile.Coord.GetAllNeighbors();

            for (const FTDHexCoord& NeighborCoord : Neighbors)
            {
                FTDHexTileSaveData* NeighborTile = TileDataMap.Find(NeighborCoord);
                if (!NeighborTile)
                {
                    continue;
                }

                const int32 HeightDiff = FMath::Abs(CurrentTile.HeightLevel - NeighborTile->HeightLevel);

                if (HeightDiff > MaxAllowedDiff)
                {
                    // 将较高的一方向较低方靠拢
                    if (CurrentTile.HeightLevel > NeighborTile->HeightLevel)
                    {
                        CurrentTile.HeightLevel = NeighborTile->HeightLevel + MaxAllowedDiff;
                    }
                    else
                    {
                        CurrentTile.HeightLevel = NeighborTile->HeightLevel - MaxAllowedDiff;
                    }

                    // 钳制到有效范围
                    CurrentTile.HeightLevel = FMath::Clamp(CurrentTile.HeightLevel, -2, 3);

                    // 同步地形类型
                    SyncTerrainTypeWithHeight(CurrentTile);

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
    // 高度改变后，强制保证地形类型与高度的一致性
    if (TileData.HeightLevel >= 3
        && TileData.TerrainType != ETDTerrainType::Mountain)
    {
        TileData.TerrainType = ETDTerrainType::Mountain;
    }
    else if (TileData.HeightLevel <= -2
        && TileData.TerrainType != ETDTerrainType::DeepWater)
    {
        TileData.TerrainType = ETDTerrainType::DeepWater;
    }
    else if (TileData.HeightLevel == -1
        && TileData.TerrainType != ETDTerrainType::River
        && TileData.TerrainType != ETDTerrainType::Swamp)
    {
        TileData.TerrainType = ETDTerrainType::River;
    }
    else if (TileData.HeightLevel >= 0
        && (TileData.TerrainType == ETDTerrainType::DeepWater
            || TileData.TerrainType == ETDTerrainType::River))
    {
        TileData.TerrainType = ETDTerrainType::Plain;
    }
}
