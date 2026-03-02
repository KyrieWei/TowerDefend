// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TDTechTreeIntegration.generated.h"

class UTDTechTreeManager;

/**
 * UTDTechTreeIntegration - 科技树集成桥接器。
 *
 * 集中管理所有玩家的 TechTreeManager 实例引用，
 * 为其他子系统提供统一的科技查询接口。
 * 避免各子系统直接持有 TechTreeManager 引用。
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDTechTreeIntegration : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 注册玩家的科技树管理器。
     * @param PlayerIndex 玩家索引。
     * @param InManager 该玩家的科技树管理器。
     */
    void RegisterPlayerTechTree(int32 PlayerIndex, UTDTechTreeManager* InManager);

    /**
     * 获取指定玩家的科技树管理器。
     * @param PlayerIndex 玩家索引。
     * @return 科技树管理器，未注册时返回 nullptr。
     */
    UTDTechTreeManager* GetPlayerTechTree(int32 PlayerIndex) const;

    /** 查询指定玩家是否解锁了某建筑。 */
    bool IsBuildingUnlockedForPlayer(int32 PlayerIndex, FName BuildingID) const;

    /** 查询指定玩家是否解锁了某单位。 */
    bool IsUnitUnlockedForPlayer(int32 PlayerIndex, FName UnitID) const;

    /** 获取指定玩家的资源产出加成百分比。 */
    float GetResourceBonusForPlayer(int32 PlayerIndex) const;

    /** 获取指定玩家的攻击加成百分比。 */
    float GetAttackBonusForPlayer(int32 PlayerIndex) const;

    /** 获取指定玩家的防御加成百分比。 */
    float GetDefenseBonusForPlayer(int32 PlayerIndex) const;

    /** 获取指定玩家解锁的地形改造等级。 */
    int32 GetTerrainModifyLevelForPlayer(int32 PlayerIndex) const;

    /** 清空所有注册。 */
    void Reset();

private:
    /** 玩家索引 → 科技树管理器映射。 */
    UPROPERTY()
    TMap<int32, UTDTechTreeManager*> PlayerTechTrees;
};
