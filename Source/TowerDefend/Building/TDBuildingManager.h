// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexGrid/TDHexCoord.h"
#include "TDBuildingManager.generated.h"

class ATDBuildingBase;
class UTDBuildingDataAsset;
class ATDHexGridManager;
class ATDHexTile;
class UTDTechTreeIntegration;

/**
 * UTDBuildingManager - 建筑管理器。
 *
 * 管理所有建筑的放置、查询、移除和经济产出汇总。
 * 使用 TMap<FTDHexCoord, ATDBuildingBase*> 维护坐标到建筑的映射，
 * 支持 O(1) 坐标查找。
 *
 * 将来由 ATDGameMode 持有。
 * 放置验证依赖 HexGrid 模块（格子查询），但不修改地形数据。
 * 放置操作不扣除玩家金币，由调用方负责扣费。
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDBuildingManager : public UObject
{
    GENERATED_BODY()

public:

    // ---------------------------------------------------------------
    // 放置与移除
    // ---------------------------------------------------------------

    /**
     * 在指定位置放置建筑。
     * 先执行 CanPlaceBuilding 验证，通过后生成 Actor 并注册。
     *
     * @param World               世界上下文。
     * @param InBuildingData      建筑数据资产。
     * @param Grid                六边形网格管理器。
     * @param InCoord             放置的六边形坐标。
     * @param InOwnerPlayerIndex  建造者玩家索引。
     * @return                    生成的建筑 Actor，验证失败返回 nullptr。
     */
    UFUNCTION(BlueprintCallable, Category = "Building|Manager")
    ATDBuildingBase* PlaceBuilding(
        UWorld* World,
        UTDBuildingDataAsset* InBuildingData,
        ATDHexGridManager* Grid,
        const FTDHexCoord& InCoord,
        int32 InOwnerPlayerIndex);

    /**
     * 验证是否可以在指定位置放置建筑。
     *
     * 验证规则：
     *   1. 参数有效性（非空指针、有效坐标）
     *   2. 目标格子存在
     *   3. 格子上没有已有建筑
     *   4. 格子 IsBuildable() == true
     *   5. 格子归属 == 建造者（或格子为中立）
     *   6. BuildingData->CanBuildOnTerrain() 通过
     *
     * @param InBuildingData      建筑数据资产。
     * @param Grid                六边形网格管理器。
     * @param InCoord             目标六边形坐标。
     * @param InOwnerPlayerIndex  建造者玩家索引。
     * @return                    是否可以放置。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Manager")
    bool CanPlaceBuilding(
        const UTDBuildingDataAsset* InBuildingData,
        const ATDHexGridManager* Grid,
        const FTDHexCoord& InCoord,
        int32 InOwnerPlayerIndex) const;

    /**
     * 移除指定坐标上的建筑。
     * 从映射中移除并销毁 Actor。
     *
     * @param InCoord  目标坐标。
     * @return         是否成功移除（该坐标上有建筑时返回 true）。
     */
    UFUNCTION(BlueprintCallable, Category = "Building|Manager")
    bool RemoveBuilding(const FTDHexCoord& InCoord);

    // ---------------------------------------------------------------
    // 查询
    // ---------------------------------------------------------------

    /**
     * O(1) 获取指定坐标上的建筑。
     *
     * @param InCoord  六边形坐标。
     * @return         建筑 Actor，不存在时返回 nullptr。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Manager")
    ATDBuildingBase* GetBuildingAt(const FTDHexCoord& InCoord) const;

    /**
     * 获取指定玩家拥有的所有建筑。
     *
     * @param InOwnerPlayerIndex  玩家索引。
     * @return                    该玩家的建筑数组。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Manager")
    TArray<ATDBuildingBase*> GetBuildingsByOwner(int32 InOwnerPlayerIndex) const;

    /**
     * 获取指定中心和半径内的所有建筑。
     *
     * @param Center  中心坐标。
     * @param Range   半径（格子数）。
     * @return        范围内的建筑数组。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Manager")
    TArray<ATDBuildingBase*> GetBuildingsInRange(
        const FTDHexCoord& Center, int32 Range) const;

    /** 获取当前建筑总数。 */
    UFUNCTION(BlueprintPure, Category = "Building|Manager")
    int32 GetBuildingCount() const;

    /** 获取指定玩家的建筑数量。 */
    UFUNCTION(BlueprintPure, Category = "Building|Manager")
    int32 GetBuildingCountByOwner(int32 InOwnerPlayerIndex) const;

    // ---------------------------------------------------------------
    // 经济查询
    // ---------------------------------------------------------------

    /**
     * 汇总指定玩家所有建筑的每回合金币产出。
     *
     * @param InOwnerPlayerIndex  玩家索引。
     * @return                    总金币产出。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Economy")
    int32 CalculateTotalGoldIncome(int32 InOwnerPlayerIndex) const;

    /**
     * 汇总指定玩家所有建筑的每回合科研点产出。
     *
     * @param InOwnerPlayerIndex  玩家索引。
     * @return                    总科研点产出。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Economy")
    int32 CalculateTotalResearchIncome(int32 InOwnerPlayerIndex) const;

    // ---------------------------------------------------------------
    // 清空
    // ---------------------------------------------------------------

    /** 移除并销毁所有建筑。 */
    UFUNCTION(BlueprintCallable, Category = "Building|Manager")
    void ClearAllBuildings();

    // ---------------------------------------------------------------
    // 科技树集成
    // ---------------------------------------------------------------

    /** 科技树集成引用（可选，由外部注入）。 */
    UPROPERTY()
    UTDTechTreeIntegration* TechIntegration = nullptr;

    /** 设置科技树集成引用。 */
    void SetTechTreeIntegration(UTDTechTreeIntegration* InTechIntegration);

private:

    /** 坐标 → 建筑映射表。 */
    UPROPERTY()
    TMap<FTDHexCoord, TObjectPtr<ATDBuildingBase>> BuildingMap;

    /**
     * 根据建筑数据资产的类型决定实际生成的 Actor 子类。
     * ArrowTower/CannonTower -> ATDDefenseTower
     * Wall -> ATDWall
     * 其他 -> ATDBuildingBase
     */
    TSubclassOf<ATDBuildingBase> DetermineSpawnClass(
        const UTDBuildingDataAsset* InBuildingData) const;
};
