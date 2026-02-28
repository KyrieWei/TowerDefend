// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TowerDefend/Core/TDSharedTypes.h"
#include "TDTechNode.h"
#include "TDTechTreeManager.generated.h"

class UTDTechTreeDataAsset;
class UTDTechNode;
class ATDPlayerState;

/** 科技研究完成委托：科技ID、所属时代 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FTDOnTechResearched, FName, TechID, ETDTechEra, Era);

/** 时代推进委托：新时代 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnEraAdvanced, ETDTechEra, NewEra);

/**
 * 科技树管理器
 *
 * 每个玩家持有一个实例，管理该玩家的全部科技研究进度。
 * 负责：
 * - 根据 DataAsset 初始化运行时科技节点
 * - 验证前置条件并执行研究操作
 * - 汇总已研究科技的被动加成
 * - 追踪当前科技时代
 * - 查询解锁的建筑/单位/地形改造等级
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDTechTreeManager : public UObject
{
    GENERATED_BODY()

public:
    // ─── 初始化 ─────────────────────────────────────────

    /**
     * 初始化科技树管理器
     * 加载科技树配置并为每个节点创建运行时状态对象，
     * 然后刷新所有节点的可用性。
     */
    void Initialize(UTDTechTreeDataAsset* InDataAsset, int32 InPlayerIndex);

    // ─── 研究操作 ───────────────────────────────────────

    /**
     * 研究指定科技
     * 验证前置条件和科研点余额，成功时扣除科研点并标记为 Completed。
     * @return 是否研究成功
     */
    UFUNCTION(BlueprintCallable, Category = "TD|TechTree")
    bool ResearchTech(FName TechID, ATDPlayerState* Player);

    /**
     * 查询科技是否可以研究
     * 条件：前置已完成 + 科研点足够 + 当前状态为 Available
     */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    bool CanResearchTech(FName TechID, const ATDPlayerState* Player) const;

    // ─── 状态查询 ───────────────────────────────────────

    /** 查询指定科技的研究状态 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    ETDTechResearchState GetTechState(FName TechID) const;

    /** 查询指定科技是否已完成 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    bool IsTechCompleted(FName TechID) const;

    /** 获取当前玩家的科技时代（根据已研究的最高时代科技决定） */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    ETDTechEra GetCurrentEra() const;

    /** 获取所有可研究的科技 ID 列表 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    TArray<FName> GetAvailableTechs() const;

    /** 获取所有已完成的科技 ID 列表 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    TArray<FName> GetCompletedTechs() const;

    // ─── 被动加成查询 ───────────────────────────────────

    /** 汇总所有已研究科技的攻击加成百分比 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    float GetTotalAttackBonus() const;

    /** 汇总所有已研究科技的防御加成百分比 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    float GetTotalDefenseBonus() const;

    /** 汇总所有已研究科技的资源产出加成百分比 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    float GetTotalResourceBonus() const;

    // ─── 解锁查询 ───────────────────────────────────────

    /** 获取解锁的地形改造等级（取已研究科技中的最大值） */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    int32 GetUnlockedTerrainModifyLevel() const;

    /** 查询是否解锁了指定建筑 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    bool IsBuildingUnlocked(FName BuildingID) const;

    /** 查询是否解锁了指定单位 */
    UFUNCTION(BlueprintPure, Category = "TD|TechTree")
    bool IsUnitUnlocked(FName UnitID) const;

    // ─── 重置 ───────────────────────────────────────────

    /** 重置所有科技进度（新对局） */
    UFUNCTION(BlueprintCallable, Category = "TD|TechTree")
    void Reset();

    // ─── 多播事件（蓝图可绑定） ─────────────────────────

    /** 科技研究完成时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|TechTree|Events")
    FTDOnTechResearched OnTechResearched;

    /** 时代推进时广播 */
    UPROPERTY(BlueprintAssignable, Category = "TD|TechTree|Events")
    FTDOnEraAdvanced OnEraAdvanced;

private:
    /** 更新所有节点的可用状态（前置检查） */
    void RefreshAvailability();

    /** 检查指定科技的前置是否全部完成 */
    bool ArePrerequisitesMet(FName TechID) const;

    /** 所有科技节点的运行时状态 */
    UPROPERTY()
    TMap<FName, UTDTechNode*> TechNodes;

    /** 科技树配置数据 */
    UPROPERTY()
    UTDTechTreeDataAsset* TreeDataAsset = nullptr;

    /** 所属玩家索引 */
    int32 OwnerPlayerIndex = -1;
};
