// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexPathfinding.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"

// ===================================================================
// 核心接口
// ===================================================================

TArray<FTDHexCoord> UTDHexPathfinding::FindPath(
    ATDHexGridManager* Grid,
    const FTDHexCoord& Start,
    const FTDHexCoord& End) const
{
    return FindPathInternal(Grid, Start, End, nullptr);
}

TArray<FTDHexCoord> UTDHexPathfinding::FindPathFiltered(
    const ATDHexGridManager* Grid,
    const FTDHexCoord& Start,
    const FTDHexCoord& End,
    const TFunction<bool(const ATDHexTile*)>& TileFilter) const
{
    return FindPathInternal(Grid, Start, End, &TileFilter);
}

float UTDHexPathfinding::CalculatePathCost(
    ATDHexGridManager* Grid,
    const TArray<FTDHexCoord>& Path) const
{
    if (!Grid || Path.Num() < 2)
    {
        return 0.0f;
    }

    float TotalCost = 0.0f;

    for (int32 I = 0; I < Path.Num() - 1; ++I)
    {
        const ATDHexTile* FromTile = Grid->GetTileAt(Path[I]);
        const ATDHexTile* ToTile = Grid->GetTileAt(Path[I + 1]);

        if (!FromTile || !ToTile)
        {
            return BIG_NUMBER;
        }

        TotalCost += CalculateStepCost(FromTile, ToTile);
    }

    return TotalCost;
}

TMap<FTDHexCoord, float> UTDHexPathfinding::GetReachableTiles(
    ATDHexGridManager* Grid,
    const FTDHexCoord& Start,
    float MaxMoveCost) const
{
    TMap<FTDHexCoord, float> CostSoFar;

    if (!Grid || MaxMoveCost < 0.0f)
    {
        return CostSoFar;
    }

    const ATDHexTile* StartTile = Grid->GetTileAt(Start);
    if (!StartTile)
    {
        return CostSoFar;
    }

    // Dijkstra 扩展：使用简单的优先队列（TArray + 排序）
    // 对于 Hex 网格规模，性能足够
    struct FDijkstraNode
    {
        FTDHexCoord Coord;
        float Cost;
    };

    TArray<FDijkstraNode> OpenList;
    OpenList.Add({Start, 0.0f});
    CostSoFar.Add(Start, 0.0f);

    while (OpenList.Num() > 0)
    {
        // 取代价最小的节点
        int32 BestIndex = 0;
        float BestCost = OpenList[0].Cost;

        for (int32 I = 1; I < OpenList.Num(); ++I)
        {
            if (OpenList[I].Cost < BestCost)
            {
                BestCost = OpenList[I].Cost;
                BestIndex = I;
            }
        }

        const FDijkstraNode Current = OpenList[BestIndex];
        OpenList.RemoveAtSwap(BestIndex);

        // 如果当前代价已超出记录的最优值，跳过（延迟删除的惰性处理）
        const float* RecordedCost = CostSoFar.Find(Current.Coord);
        if (RecordedCost && Current.Cost > *RecordedCost)
        {
            continue;
        }

        const ATDHexTile* CurrentTile = Grid->GetTileAt(Current.Coord);
        if (!CurrentTile)
        {
            continue;
        }

        // 扩展邻居
        TArray<FTDHexCoord> Neighbors = Current.Coord.GetAllNeighbors();

        for (const FTDHexCoord& NeighborCoord : Neighbors)
        {
            const ATDHexTile* NeighborTile = Grid->GetTileAt(NeighborCoord);
            if (!NeighborTile || !NeighborTile->IsPassable())
            {
                continue;
            }

            const float StepCost = CalculateStepCost(CurrentTile, NeighborTile);
            const float NewCost = Current.Cost + StepCost;

            if (NewCost > MaxMoveCost)
            {
                continue;
            }

            const float* ExistingCost = CostSoFar.Find(NeighborCoord);
            if (!ExistingCost || NewCost < *ExistingCost)
            {
                CostSoFar.Add(NeighborCoord, NewCost);
                OpenList.Add({NeighborCoord, NewCost});
            }
        }
    }

    return CostSoFar;
}

// ===================================================================
// 内部实现
// ===================================================================

TArray<FTDHexCoord> UTDHexPathfinding::FindPathInternal(
    const ATDHexGridManager* Grid,
    const FTDHexCoord& Start,
    const FTDHexCoord& End,
    const TFunction<bool(const ATDHexTile*)>* TileFilter) const
{
    TArray<FTDHexCoord> EmptyPath;

    if (!Grid)
    {
        UE_LOG(LogTemp, Warning, TEXT("UTDHexPathfinding::FindPathInternal: Grid is null."));
        return EmptyPath;
    }

    // 起点终点相同
    if (Start == End)
    {
        return {Start};
    }

    // 验证起点和终点
    const ATDHexTile* StartTile = Grid->GetTileAt(Start);
    const ATDHexTile* EndTile = Grid->GetTileAt(End);

    if (!StartTile || !EndTile)
    {
        return EmptyPath;
    }

    if (!StartTile->IsPassable() || !EndTile->IsPassable())
    {
        return EmptyPath;
    }

    if (TileFilter && !(*TileFilter)(StartTile))
    {
        return EmptyPath;
    }

    if (TileFilter && !(*TileFilter)(EndTile))
    {
        return EmptyPath;
    }

    // A* 数据结构
    // OpenList: 使用 TArray 模拟最小堆（对 hex 网格规模足够）
    TArray<FPathNode> OpenList;
    TMap<FTDHexCoord, float> GCosts;
    TMap<FTDHexCoord, FTDHexCoord> CameFrom;
    TSet<FTDHexCoord> ClosedSet;

    // 初始化起点
    FPathNode StartNode;
    StartNode.Coord = Start;
    StartNode.GCost = 0.0f;
    StartNode.HCost = Heuristic(Start, End);

    OpenList.Add(StartNode);
    GCosts.Add(Start, 0.0f);

    int32 NodesExplored = 0;

    while (OpenList.Num() > 0)
    {
        // 节点上限检查
        if (NodesExplored >= MaxSearchNodes)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("UTDHexPathfinding: Search node limit (%d) reached for path %s -> %s."),
                MaxSearchNodes, *Start.ToString(), *End.ToString());
            return EmptyPath;
        }

        // 从 OpenList 中取 FCost 最小的节点
        int32 BestIndex = 0;
        float BestFCost = OpenList[0].FCost();

        for (int32 I = 1; I < OpenList.Num(); ++I)
        {
            const float CurrentFCost = OpenList[I].FCost();
            if (CurrentFCost < BestFCost)
            {
                BestFCost = CurrentFCost;
                BestIndex = I;
            }
        }

        FPathNode Current = OpenList[BestIndex];
        OpenList.RemoveAtSwap(BestIndex);

        // 到达终点
        if (Current.Coord == End)
        {
            return ReconstructPath(CameFrom, Start, End);
        }

        // 已在关闭集则跳过
        if (ClosedSet.Contains(Current.Coord))
        {
            continue;
        }

        ClosedSet.Add(Current.Coord);
        ++NodesExplored;

        const ATDHexTile* CurrentTile = Grid->GetTileAt(Current.Coord);
        if (!CurrentTile)
        {
            continue;
        }

        // 扩展邻居
        TArray<FTDHexCoord> Neighbors = Current.Coord.GetAllNeighbors();

        for (const FTDHexCoord& NeighborCoord : Neighbors)
        {
            if (ClosedSet.Contains(NeighborCoord))
            {
                continue;
            }

            const ATDHexTile* NeighborTile = Grid->GetTileAt(NeighborCoord);
            if (!NeighborTile || !NeighborTile->IsPassable())
            {
                continue;
            }

            // 额外过滤器
            if (TileFilter && !(*TileFilter)(NeighborTile))
            {
                continue;
            }

            const float StepCost = CalculateStepCost(CurrentTile, NeighborTile);
            const float TentativeG = Current.GCost + StepCost;

            const float* ExistingG = GCosts.Find(NeighborCoord);
            if (ExistingG && TentativeG >= *ExistingG)
            {
                continue;
            }

            // 更新最优路径
            GCosts.Add(NeighborCoord, TentativeG);
            CameFrom.Add(NeighborCoord, Current.Coord);

            FPathNode NeighborNode;
            NeighborNode.Coord = NeighborCoord;
            NeighborNode.GCost = TentativeG;
            NeighborNode.HCost = Heuristic(NeighborCoord, End);

            OpenList.Add(NeighborNode);
        }
    }

    // OpenList 耗尽仍未到达终点
    return EmptyPath;
}

float UTDHexPathfinding::CalculateStepCost(
    const ATDHexTile* FromTile,
    const ATDHexTile* ToTile) const
{
    if (!FromTile || !ToTile)
    {
        return BIG_NUMBER;
    }

    // 基础代价来自目标格子
    float BaseCost = ToTile->GetMovementCost();

    // 高度差修正
    const int32 HeightDiff = ToTile->GetHeightLevel() - FromTile->GetHeightLevel();

    if (HeightDiff > 0)
    {
        // 上坡：每级加 HeightPenaltyPerLevel
        BaseCost += static_cast<float>(HeightDiff) * HeightPenaltyPerLevel;
    }
    else if (HeightDiff < 0)
    {
        // 下坡：每级减 HeightBonusPerLevel
        BaseCost -= static_cast<float>(-HeightDiff) * HeightBonusPerLevel;
    }

    // 代价下限保护
    return FMath::Max(BaseCost, MinStepCost);
}

float UTDHexPathfinding::Heuristic(const FTDHexCoord& From, const FTDHexCoord& To)
{
    // admissible: hex distance * 最低可能单步代价
    return static_cast<float>(FTDHexCoord::Distance(From, To)) * MinMoveCost;
}

TArray<FTDHexCoord> UTDHexPathfinding::ReconstructPath(
    const TMap<FTDHexCoord, FTDHexCoord>& CameFrom,
    const FTDHexCoord& Start,
    const FTDHexCoord& End)
{
    TArray<FTDHexCoord> Path;

    FTDHexCoord Current = End;

    // 安全上限防止无限循环
    constexpr int32 MaxPathLength = 10000;
    int32 Steps = 0;

    while (!(Current == Start) && Steps < MaxPathLength)
    {
        Path.Add(Current);

        const FTDHexCoord* Parent = CameFrom.Find(Current);
        if (!Parent)
        {
            // 路径断裂，不应发生
            UE_LOG(LogTemp, Error,
                TEXT("UTDHexPathfinding::ReconstructPath: Path reconstruction broken at %s."),
                *Current.ToString());
            return TArray<FTDHexCoord>();
        }

        Current = *Parent;
        ++Steps;
    }

    Path.Add(Start);

    // 反转为正序（Start → End）
    Algo::Reverse(Path);

    return Path;
}
