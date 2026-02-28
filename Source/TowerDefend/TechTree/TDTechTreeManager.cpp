// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDTechTreeManager.h"
#include "TDTechTreeDataAsset.h"
#include "TDTechNode.h"
#include "TowerDefend/Core/TDPlayerState.h"

// ─── 初始化 ─────────────────────────────────────────────

void UTDTechTreeManager::Initialize(UTDTechTreeDataAsset* InDataAsset, int32 InPlayerIndex)
{
    if (!ensureMsgf(InDataAsset != nullptr, TEXT("UTDTechTreeManager::Initialize - InDataAsset is null")))
    {
        return;
    }

    TreeDataAsset = InDataAsset;
    OwnerPlayerIndex = InPlayerIndex;

    // 清除可能的旧数据
    TechNodes.Empty();

    // 为每个配置节点创建运行时状态对象
    const TArray<FTDTechNodeData>& AllNodes = TreeDataAsset->GetAllTechNodes();
    for (const FTDTechNodeData& NodeData : AllNodes)
    {
        if (NodeData.TechID.IsNone())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("UTDTechTreeManager::Initialize - Skipping node with empty TechID (PlayerIndex=%d)"),
                OwnerPlayerIndex);
            continue;
        }

        if (TechNodes.Contains(NodeData.TechID))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("UTDTechTreeManager::Initialize - Duplicate TechID '%s' (PlayerIndex=%d)"),
                *NodeData.TechID.ToString(), OwnerPlayerIndex);
            continue;
        }

        UTDTechNode* NewNode = NewObject<UTDTechNode>(this);
        NewNode->Initialize(&NodeData);
        TechNodes.Add(NodeData.TechID, NewNode);
    }

    // 初始化后刷新节点可用状态
    RefreshAvailability();
}

// ─── 研究操作 ───────────────────────────────────────────

bool UTDTechTreeManager::ResearchTech(FName TechID, ATDPlayerState* Player)
{
    if (!ensureMsgf(Player != nullptr, TEXT("UTDTechTreeManager::ResearchTech - Player is null")))
    {
        return false;
    }

    if (!CanResearchTech(TechID, Player))
    {
        return false;
    }

    UTDTechNode** FoundNode = TechNodes.Find(TechID);
    if (!FoundNode || !(*FoundNode))
    {
        return false;
    }

    UTDTechNode* Node = *FoundNode;
    const FTDTechNodeData* NodeData = Node->GetNodeData();
    if (!ensureMsgf(NodeData != nullptr, TEXT("UTDTechTreeManager::ResearchTech - NodeData is null for '%s'"), *TechID.ToString()))
    {
        return false;
    }

    // 扣除科研点
    const int32 Cost = NodeData->ResearchCost;
    if (!Player->SpendResearchPoints(Cost))
    {
        return false;
    }

    // 标记为已完成
    const ETDTechEra OldEra = GetCurrentEra();
    Node->SetResearchState(ETDTechResearchState::Completed);

    // 刷新后续节点的可用状态
    RefreshAvailability();

    // 广播科技研究完成事件
    OnTechResearched.Broadcast(TechID, NodeData->Era);

    // 检查是否推进了时代
    const ETDTechEra NewEra = GetCurrentEra();
    if (NewEra != OldEra)
    {
        OnEraAdvanced.Broadcast(NewEra);
    }

    return true;
}

bool UTDTechTreeManager::CanResearchTech(FName TechID, const ATDPlayerState* Player) const
{
    if (!Player)
    {
        return false;
    }

    const UTDTechNode* const* FoundNode = TechNodes.Find(TechID);
    if (!FoundNode || !(*FoundNode))
    {
        return false;
    }

    const UTDTechNode* Node = *FoundNode;

    // 必须处于 Available 状态
    if (Node->GetResearchState() != ETDTechResearchState::Available)
    {
        return false;
    }

    // 科研点必须足够
    const int32 Cost = Node->GetResearchCost();
    if (Player->GetResearchPoints() < Cost)
    {
        return false;
    }

    return true;
}

// ─── 状态查询 ───────────────────────────────────────────

ETDTechResearchState UTDTechTreeManager::GetTechState(FName TechID) const
{
    const UTDTechNode* const* FoundNode = TechNodes.Find(TechID);
    if (!FoundNode || !(*FoundNode))
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDTechTreeManager::GetTechState - TechID '%s' not found"),
            *TechID.ToString());
        return ETDTechResearchState::Locked;
    }

    return (*FoundNode)->GetResearchState();
}

bool UTDTechTreeManager::IsTechCompleted(FName TechID) const
{
    const UTDTechNode* const* FoundNode = TechNodes.Find(TechID);
    if (!FoundNode || !(*FoundNode))
    {
        return false;
    }

    return (*FoundNode)->IsCompleted();
}

ETDTechEra UTDTechTreeManager::GetCurrentEra() const
{
    ETDTechEra HighestEra = ETDTechEra::Ancient;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData && NodeData->Era > HighestEra)
        {
            HighestEra = NodeData->Era;
        }
    }

    return HighestEra;
}

TArray<FName> UTDTechTreeManager::GetAvailableTechs() const
{
    TArray<FName> Result;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (Node && Node->GetResearchState() == ETDTechResearchState::Available)
        {
            Result.Add(Pair.Key);
        }
    }

    return Result;
}

TArray<FName> UTDTechTreeManager::GetCompletedTechs() const
{
    TArray<FName> Result;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (Node && Node->IsCompleted())
        {
            Result.Add(Pair.Key);
        }
    }

    return Result;
}

// ─── 被动加成查询 ───────────────────────────────────────

float UTDTechTreeManager::GetTotalAttackBonus() const
{
    float TotalBonus = 0.0f;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData)
        {
            TotalBonus += NodeData->AttackBonusPercent;
        }
    }

    return TotalBonus;
}

float UTDTechTreeManager::GetTotalDefenseBonus() const
{
    float TotalBonus = 0.0f;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData)
        {
            TotalBonus += NodeData->DefenseBonusPercent;
        }
    }

    return TotalBonus;
}

float UTDTechTreeManager::GetTotalResourceBonus() const
{
    float TotalBonus = 0.0f;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData)
        {
            TotalBonus += NodeData->ResourceBonusPercent;
        }
    }

    return TotalBonus;
}

// ─── 解锁查询 ───────────────────────────────────────────

int32 UTDTechTreeManager::GetUnlockedTerrainModifyLevel() const
{
    int32 MaxLevel = 0;

    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData && NodeData->UnlockedTerrainModifyLevel > MaxLevel)
        {
            MaxLevel = NodeData->UnlockedTerrainModifyLevel;
        }
    }

    return MaxLevel;
}

bool UTDTechTreeManager::IsBuildingUnlocked(FName BuildingID) const
{
    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData && NodeData->UnlockedBuildingIDs.Contains(BuildingID))
        {
            return true;
        }
    }

    return false;
}

bool UTDTechTreeManager::IsUnitUnlocked(FName UnitID) const
{
    for (const auto& Pair : TechNodes)
    {
        const UTDTechNode* Node = Pair.Value;
        if (!Node || !Node->IsCompleted())
        {
            continue;
        }

        const FTDTechNodeData* NodeData = Node->GetNodeData();
        if (NodeData && NodeData->UnlockedUnitIDs.Contains(UnitID))
        {
            return true;
        }
    }

    return false;
}

// ─── 重置 ───────────────────────────────────────────────

void UTDTechTreeManager::Reset()
{
    for (auto& Pair : TechNodes)
    {
        UTDTechNode* Node = Pair.Value;
        if (Node)
        {
            Node->SetResearchState(ETDTechResearchState::Locked);
        }
    }

    RefreshAvailability();
}

// ─── 内部辅助 ───────────────────────────────────────────

void UTDTechTreeManager::RefreshAvailability()
{
    for (auto& Pair : TechNodes)
    {
        UTDTechNode* Node = Pair.Value;
        if (!Node)
        {
            continue;
        }

        // 只更新 Locked 状态的节点（已完成/正在研究的节点不回退）
        if (Node->GetResearchState() != ETDTechResearchState::Locked)
        {
            continue;
        }

        if (ArePrerequisitesMet(Pair.Key))
        {
            Node->SetResearchState(ETDTechResearchState::Available);
        }
    }
}

bool UTDTechTreeManager::ArePrerequisitesMet(FName TechID) const
{
    if (!TreeDataAsset)
    {
        return false;
    }

    const FTDTechNodeData* NodeData = TreeDataAsset->FindTechNode(TechID);
    if (!NodeData)
    {
        return false;
    }

    // 没有前置条件，直接可用
    if (NodeData->Prerequisites.Num() == 0)
    {
        return true;
    }

    // 所有前置科技必须已完成
    for (const FName& PrereqID : NodeData->Prerequisites)
    {
        if (!IsTechCompleted(PrereqID))
        {
            return false;
        }
    }

    return true;
}
