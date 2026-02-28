// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TDTechNode.generated.h"

struct FTDTechNodeData;

/**
 * 科技研究状态枚举
 *
 * 描述单个科技节点在运行时的研究进度。
 * 状态流转：Locked -> Available -> Researching -> Completed
 */
UENUM(BlueprintType)
enum class ETDTechResearchState : uint8
{
    /** 前置科技未满足，不可研究 */
    Locked,

    /** 可以研究（前置已满足，但尚未研究） */
    Available,

    /** 正在研究中（预留，当前研究为瞬时完成） */
    Researching,

    /** 已研究完成 */
    Completed,
};

/**
 * 科技节点运行时状态
 *
 * UObject 子类，代表一个科技节点在某位玩家视角下的运行时研究状态。
 * 持有对配置数据 FTDTechNodeData 的只读引用，由 TDTechTreeManager 创建和管理。
 */
UCLASS()
class TOWERDEFEND_API UTDTechNode : public UObject
{
    GENERATED_BODY()

public:
    /** 根据配置数据初始化节点，设置 TechID 和初始状态为 Locked */
    void Initialize(const FTDTechNodeData* InNodeData);

    /** 获取科技唯一标识 */
    FName GetTechID() const;

    /** 获取当前研究状态 */
    ETDTechResearchState GetResearchState() const;

    /** 设置研究状态 */
    void SetResearchState(ETDTechResearchState NewState);

    /** 获取配置数据指针 */
    const FTDTechNodeData* GetNodeData() const;

    /** 是否已研究完成 */
    bool IsCompleted() const;

    /** 获取研究所需科研点 */
    int32 GetResearchCost() const;

private:
    /** 对应的科技标识 */
    UPROPERTY()
    FName TechID;

    /** 研究状态 */
    UPROPERTY()
    ETDTechResearchState ResearchState = ETDTechResearchState::Locked;

    /**
     * 指向配置数据（DataAsset 内的数据）
     * 非 UPROPERTY：DataAsset 的生命周期由 TDTechTreeManager 持有的 UPROPERTY 保证
     */
    const FTDTechNodeData* NodeData = nullptr;
};
