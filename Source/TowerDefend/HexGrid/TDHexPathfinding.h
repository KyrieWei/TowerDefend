// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HexGrid/TDHexCoord.h"
#include "TDHexPathfinding.generated.h"

class ATDHexGridManager;
class ATDHexTile;

/**
 * UTDHexPathfinding - 六边形网格 A* 寻路。
 *
 * 在六边形网格上执行 A* 寻路，移动代价来自 ATDHexTile::GetMovementCost()，
 * 相邻格子高度差会叠加额外代价（上坡惩罚 / 下坡减免）。
 * 不可通行格子不参与搜索。
 *
 * 本类为纯查询组件，不修改任何网格或 Tile 数据。
 */
UCLASS(Blueprintable, BlueprintType)
class UTDHexPathfinding : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 可配置参数
    // ---------------------------------------------------------------

    /** 每级高度差的上坡额外移动代价。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pathfinding",
        meta = (ClampMin = "0.0"))
    float HeightPenaltyPerLevel = 0.5f;

    /** 每级高度差的下坡移动减免（减免后单步代价不低于 0.1）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pathfinding",
        meta = (ClampMin = "0.0"))
    float HeightBonusPerLevel = 0.2f;

    /** 搜索节点上限，防止超大地图卡死。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pathfinding",
        meta = (ClampMin = "100"))
    int32 MaxSearchNodes = 10000;

    // ---------------------------------------------------------------
    // 核心接口
    // ---------------------------------------------------------------

    /**
     * 寻找从 Start 到 End 的最短路径。
     * 返回有序坐标列表（含起点和终点）。空数组表示无可达路径。
     *
     * @param Grid   网格管理器（只读）。
     * @param Start  起点坐标。
     * @param End    终点坐标。
     * @return       路径坐标数组，找不到时返回空。
     */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding")
    TArray<FTDHexCoord> FindPath(
        ATDHexGridManager* Grid,
        const FTDHexCoord& Start,
        const FTDHexCoord& End) const;

    /**
     * 带过滤器的寻路。TileFilter 返回 true 表示该格子允许通过。
     * 过滤器在 IsPassable() 基础上额外叠加判断。
     *
     * @param Grid        网格管理器（只读）。
     * @param Start       起点坐标。
     * @param End         终点坐标。
     * @param TileFilter  格子过滤器。
     * @return            路径坐标数组，找不到时返回空。
     */
    TArray<FTDHexCoord> FindPathFiltered(
        const ATDHexGridManager* Grid,
        const FTDHexCoord& Start,
        const FTDHexCoord& End,
        const TFunction<bool(const ATDHexTile*)>& TileFilter) const;

    /**
     * 计算已知路径的总移动消耗（含高度差代价）。
     * 路径长度不足 2 时返回 0。
     *
     * @param Grid  网格管理器（只读）。
     * @param Path  有序坐标路径。
     * @return      总消耗。
     */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding")
    float CalculatePathCost(
        ATDHexGridManager* Grid,
        const TArray<FTDHexCoord>& Path) const;

    /**
     * 获取从 Start 出发、在 MaxMoveCost 内可到达的所有格子。
     * 使用 Dijkstra 扩展，返回 TMap<坐标, 到达该坐标的最低消耗>。
     *
     * @param Grid         网格管理器（只读）。
     * @param Start        起点坐标。
     * @param MaxMoveCost  最大移动预算。
     * @return             可达格子及其最低消耗映射。
     */
    UFUNCTION(BlueprintCallable, Category = "Pathfinding")
    TMap<FTDHexCoord, float> GetReachableTiles(
        ATDHexGridManager* Grid,
        const FTDHexCoord& Start,
        float MaxMoveCost) const;

private:
    // ---------------------------------------------------------------
    // 内部数据结构
    // ---------------------------------------------------------------

    /** A* 搜索节点。纯内部使用，不暴露给反射系统。 */
    struct FPathNode
    {
        FTDHexCoord Coord;
        float GCost = 0.0f;
        float HCost = 0.0f;

        float FCost() const { return GCost + HCost; }
    };

    // ---------------------------------------------------------------
    // 内部方法
    // ---------------------------------------------------------------

    /**
     * A* 核心搜索实现。FindPath 和 FindPathFiltered 都委托给此方法。
     * TileFilter 为 nullptr 时不做额外过滤。
     */
    TArray<FTDHexCoord> FindPathInternal(
        const ATDHexGridManager* Grid,
        const FTDHexCoord& Start,
        const FTDHexCoord& End,
        const TFunction<bool(const ATDHexTile*)>* TileFilter) const;

    /**
     * 计算从 FromTile 移动到 ToTile 的单步代价。
     * 包括目标格子的基础移动消耗和高度差修正。
     *
     * @param FromTile  出发格子。
     * @param ToTile    目标格子。
     * @return          单步移动代价（>= 0.1）。
     */
    float CalculateStepCost(const ATDHexTile* FromTile, const ATDHexTile* ToTile) const;

    /**
     * A* 启发函数：六边形距离乘以最低可能的单步代价。
     * 保证启发值不超过实际代价（admissible）。
     */
    static float Heuristic(const FTDHexCoord& From, const FTDHexCoord& To);

    /** 启发函数使用的最低单步移动代价常量（必须 <= MinStepCost 以保证 admissible）。 */
    static constexpr float MinMoveCost = 0.1f;

    /** 单步代价的下限，防止高度减免导致零或负代价。 */
    static constexpr float MinStepCost = 0.1f;

    /**
     * 从 CameFrom 映射中回溯构建完整路径。
     * 返回从 Start 到 End 的有序坐标数组。
     */
    static TArray<FTDHexCoord> ReconstructPath(
        const TMap<FTDHexCoord, FTDHexCoord>& CameFrom,
        const FTDHexCoord& Start,
        const FTDHexCoord& End);
};
