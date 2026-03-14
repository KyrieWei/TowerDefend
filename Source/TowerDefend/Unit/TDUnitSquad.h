// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "TDUnitSquad.generated.h"

class ATDUnitBase;
class UTDUnitDataAsset;

/**
 * UTDUnitSquad - 编队管理器。
 *
 * UObject 子类，管理同一场战斗中所有单位的集合。
 * 使用 TMap<FTDHexCoord, ATDUnitBase*> 维护坐标到单位的映射，
 * 支持 O(1) 坐标查询和按玩家/范围筛选。
 *
 * 主要用途：
 * - 被 TDCombatManager 持有，管理战场上所有单位
 * - 提供批量操作（回合重置移动点、清除死亡单位等）
 * - 为 TDUnitAIController 提供敌我单位查询
 *
 * 注意：本类不负责 Spawn/Destroy Actor，仅维护引用关系。
 * Actor 的生命周期由外部（CombatManager 或调用方）管理。
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDUnitSquad : public UObject
{
    GENERATED_BODY()

public:
    // ---------------------------------------------------------------
    // 添加 / 移除
    // ---------------------------------------------------------------

    /**
     * 将单位注册到管理器中。
     * 使用单位的当前坐标作为映射键。
     * 如果该坐标已有单位，将覆盖旧映射并输出警告。
     *
     * @param Unit  要添加的单位，不可为空。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Squad")
    void AddUnit(ATDUnitBase* Unit);

    /**
     * 从管理器中移除指定单位。
     * 如果单位不在映射中则忽略。
     *
     * @param Unit  要移除的单位。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Squad")
    void RemoveUnit(ATDUnitBase* Unit);

    // ---------------------------------------------------------------
    // 查询
    // ---------------------------------------------------------------

    /** 获取所有已注册的单位。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    TArray<ATDUnitBase*> GetAllUnits() const;

    /**
     * 获取指定玩家拥有的所有单位。
     *
     * @param OwnerPlayerIndex  玩家索引。
     * @return                  该玩家拥有的单位数组。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    TArray<ATDUnitBase*> GetUnitsByOwner(int32 OwnerPlayerIndex) const;

    /**
     * 获取以指定坐标为中心、指定范围内的所有单位。
     *
     * @param Center  中心六边形坐标。
     * @param Range   查询半径（格子数）。
     * @return        范围内的单位数组。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    TArray<ATDUnitBase*> GetUnitsInRange(const FTDHexCoord& Center, int32 Range) const;

    /**
     * 获取指定坐标上的单位。
     *
     * @param Coord  六边形坐标。
     * @return       该坐标上的单位，不存在时返回 nullptr。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    ATDUnitBase* GetUnitAt(const FTDHexCoord& Coord) const;

    /** 获取当前管理的单位总数。 */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    int32 GetUnitCount() const;

    /**
     * 获取指定玩家的单位数量。
     *
     * @param OwnerPlayerIndex  玩家索引。
     * @return                  该玩家的单位数量。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    int32 GetUnitCountByOwner(int32 OwnerPlayerIndex) const;

    /**
     * Get the total combat strength of all alive units.
     * Strength = sum of current health of all alive units.
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    int32 GetTotalStrength() const;

    /**
     * Get the number of alive units for a given player.
     *
     * @param OwnerPlayerIndex Player index.
     * @return Number of alive units.
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Squad")
    int32 GetRemainingUnitCount(int32 OwnerPlayerIndex) const;

    // ---------------------------------------------------------------
    // 批量操作
    // ---------------------------------------------------------------

    /** 回合开始时重置所有存活单位的移动点数。 */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Squad")
    void ResetAllMovePoints();

    /** 从映射中移除所有已死亡的单位引用（不销毁 Actor）。 */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Squad")
    void RemoveDeadUnits();

    /** 清空所有单位引用（不销毁 Actor）。 */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Squad")
    void ClearAllUnits();

    /**
     * 当单位移动后更新其在映射中的键。
     * 应在 ATDUnitBase::MoveTo 调用后调用此方法保持映射同步。
     *
     * @param Unit     已移动的单位。
     * @param OldCoord 移动前的坐标。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Squad")
    void UpdateUnitPosition(ATDUnitBase* Unit, const FTDHexCoord& OldCoord);

    // ---------------------------------------------------------------
    // 存档接口
    // ---------------------------------------------------------------

    /**
     * 导出所有单位的保存数据。
     * 遍历 UnitMap，将每个有效单位转换为 FTDUnitSaveData。
     *
     * @return  单位保存数据数组。
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Save")
    TArray<FTDUnitSaveData> ExportUnitData() const;

    /**
     * 从保存数据恢复所有单位。
     * 根据 UnitID 查找对应的 DataAsset，在指定坐标生成单位并注册。
     *
     * @param World            世界上下文。
     * @param InUnitDataList   单位保存数据数组。
     * @param InDataAssets     可用的单位数据资产列表（用于按 ID 查找）。
     * @return                 成功恢复的单位数量。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Save")
    int32 ImportUnitData(
        UWorld* World,
        const TArray<FTDUnitSaveData>& InUnitDataList,
        const TArray<UTDUnitDataAsset*>& InDataAssets);

private:
    /** 坐标到单位的映射，支持 O(1) 坐标查询。 */
    UPROPERTY()
    TMap<FTDHexCoord, ATDUnitBase*> UnitMap;
};
