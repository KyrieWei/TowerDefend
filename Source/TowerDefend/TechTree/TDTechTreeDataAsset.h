// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "TowerDefend/Core/TDSharedTypes.h"
#include "TDTechTreeDataAsset.generated.h"

/**
 * 科技节点配置数据
 *
 * 描述单个科技节点的全部静态配置：所属时代、研究费用、前置依赖、
 * 解锁内容（建筑/单位/地形改造）以及被动加成数值。
 * 由策划在 TDTechTreeDataAsset 编辑器中配置。
 */
USTRUCT(BlueprintType)
struct FTDTechNodeData
{
    GENERATED_BODY()

    /** 科技唯一标识 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName TechID;

    /** 显示名称 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText DisplayName;

    /** 描述文本 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FText Description;

    /** 所属时代 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    ETDTechEra Era = ETDTechEra::Ancient;

    /** 研究所需科研点 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
    int32 ResearchCost = 50;

    /** 前置科技 ID 列表（全部研究完才能解锁本科技） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FName> Prerequisites;

    /** 解锁的建筑 ID 列表 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FName> UnlockedBuildingIDs;

    /** 解锁的单位 ID 列表 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TArray<FName> UnlockedUnitIDs;

    /** 解锁的地形改造等级（0=不解锁，1=基础+-1，2=高级+-2） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0", ClampMax = "3"))
    int32 UnlockedTerrainModifyLevel = 0;

    /** 被动加成：攻击加成百分比 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float AttackBonusPercent = 0.0f;

    /** 被动加成：防御加成百分比 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float DefenseBonusPercent = 0.0f;

    /** 被动加成：资源产出加成百分比 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float ResourceBonusPercent = 0.0f;

    /** UI 位置（科技树界面中的 X,Y 坐标） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FVector2D UIPosition = FVector2D::ZeroVector;

    /** 图标 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<UTexture2D> Icon;
};

/**
 * 科技树数据资产
 *
 * UDataAsset 子类，承载一棵完整科技树的全部节点配置。
 * 策划可在编辑器中创建多个实例以定义不同的科技树方案。
 * 运行时由 TDTechTreeManager 加载并据此创建运行时节点。
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDTechTreeDataAsset : public UDataAsset
{
    GENERATED_BODY()

public:
    /** 根据 TechID 查找节点数据，找不到返回 nullptr */
    const FTDTechNodeData* FindTechNode(FName TechID) const;

    /** 获取指定时代的所有科技 */
    TArray<const FTDTechNodeData*> GetTechsByEra(ETDTechEra Era) const;

    /** 获取所有科技节点 */
    const TArray<FTDTechNodeData>& GetAllTechNodes() const;

protected:
    /** 所有科技节点配置 */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "TechTree")
    TArray<FTDTechNodeData> TechNodes;
};
