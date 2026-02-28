// Copyright TowerDefend. All Rights Reserved.

#include "Unit/TDUnitDataAsset.h"

float UTDUnitDataAsset::GetDamageMultiplierVs(ETDUnitType TargetType) const
{
    switch (TargetType)
    {
    case ETDUnitType::Melee:
        return VsMeleeMultiplier;

    case ETDUnitType::Ranged:
        return VsRangedMultiplier;

    case ETDUnitType::Cavalry:
        return VsCavalryMultiplier;

    case ETDUnitType::Siege:
        return VsSiegeMultiplier;

    case ETDUnitType::None:
    case ETDUnitType::Special:
    default:
        return 1.0f;
    }
}
