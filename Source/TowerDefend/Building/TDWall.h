// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Building/TDBuildingBase.h"
#include "TDWall.generated.h"

/**
 * ATDWall - 城墙建筑。
 *
 * ATDBuildingBase 子类，阻挡/减速敌方单位的防御建筑。
 * 城墙不具备攻击能力，但拥有高血量。
 * 敌方单位通过城墙所在格子时移动消耗大幅增加。
 * 高级城墙可完全阻挡通行。
 */
UCLASS()
class TOWERDEFEND_API ATDWall : public ATDBuildingBase
{
    GENERATED_BODY()

public:

    // ---------------------------------------------------------------
    // Override from ATDBuildingBase
    // ---------------------------------------------------------------

    /** 城墙不攻击，始终返回 false。 */
    virtual bool CanAttack() const override;

    // ---------------------------------------------------------------
    // 城墙专属接口
    // ---------------------------------------------------------------

    /**
     * 获取敌方单位通过此格子时的移动消耗倍率。
     * 倍率作用于格子的基础移动消耗。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Wall")
    float GetMovementCostMultiplier() const { return MovementCostMultiplier; }

    /**
     * 是否完全阻挡通过（高级城墙特性）。
     * 为 true 时敌方普通单位无法通过，攻城器械除外。
     */
    UFUNCTION(BlueprintPure, Category = "Building|Wall")
    bool DoesBlockPassage() const { return bBlocksPassage; }

protected:

    // ---------------------------------------------------------------
    // 配置参数
    // ---------------------------------------------------------------

    /** 对敌方单位的移动消耗倍率（默认 10 倍，大幅减速）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building|Wall",
        meta = (ClampMin = "1.0"))
    float MovementCostMultiplier = 10.0f;

    /** 是否完全阻挡通过（高级城墙为 true）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Building|Wall")
    bool bBlocksPassage = false;
};
