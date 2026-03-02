// Copyright TowerDefend. All Rights Reserved.

#include "TechTree/TDTechTreeIntegration.h"
#include "TechTree/TDTechTreeManager.h"

void UTDTechTreeIntegration::RegisterPlayerTechTree(int32 PlayerIndex, UTDTechTreeManager* InManager)
{
    if (!InManager)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDTechTreeIntegration::RegisterPlayerTechTree - Null manager for player %d"),
            PlayerIndex);
        return;
    }
    PlayerTechTrees.Add(PlayerIndex, InManager);
}

UTDTechTreeManager* UTDTechTreeIntegration::GetPlayerTechTree(int32 PlayerIndex) const
{
    UTDTechTreeManager* const* Found = PlayerTechTrees.Find(PlayerIndex);
    return Found ? *Found : nullptr;
}

bool UTDTechTreeIntegration::IsBuildingUnlockedForPlayer(int32 PlayerIndex, FName BuildingID) const
{
    const UTDTechTreeManager* Manager = GetPlayerTechTree(PlayerIndex);
    if (!Manager) return true; // No tech tree = no restrictions
    return Manager->IsBuildingUnlocked(BuildingID);
}

bool UTDTechTreeIntegration::IsUnitUnlockedForPlayer(int32 PlayerIndex, FName UnitID) const
{
    const UTDTechTreeManager* Manager = GetPlayerTechTree(PlayerIndex);
    if (!Manager) return true;
    return Manager->IsUnitUnlocked(UnitID);
}

float UTDTechTreeIntegration::GetResourceBonusForPlayer(int32 PlayerIndex) const
{
    const UTDTechTreeManager* Manager = GetPlayerTechTree(PlayerIndex);
    if (!Manager) return 0.0f;
    return Manager->GetTotalResourceBonus();
}

float UTDTechTreeIntegration::GetAttackBonusForPlayer(int32 PlayerIndex) const
{
    const UTDTechTreeManager* Manager = GetPlayerTechTree(PlayerIndex);
    if (!Manager) return 0.0f;
    return Manager->GetTotalAttackBonus();
}

float UTDTechTreeIntegration::GetDefenseBonusForPlayer(int32 PlayerIndex) const
{
    const UTDTechTreeManager* Manager = GetPlayerTechTree(PlayerIndex);
    if (!Manager) return 0.0f;
    return Manager->GetTotalDefenseBonus();
}

int32 UTDTechTreeIntegration::GetTerrainModifyLevelForPlayer(int32 PlayerIndex) const
{
    const UTDTechTreeManager* Manager = GetPlayerTechTree(PlayerIndex);
    if (!Manager) return 0;
    return Manager->GetUnlockedTerrainModifyLevel();
}

void UTDTechTreeIntegration::Reset()
{
    PlayerTechTrees.Empty();
}
