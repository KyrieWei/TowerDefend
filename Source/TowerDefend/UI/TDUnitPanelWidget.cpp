// Copyright TowerDefend. All Rights Reserved.

#include "UI/TDUnitPanelWidget.h"

void UTDUnitPanelWidget::SetAvailableUnits(
    const TArray<UTDUnitDataAsset*>& InUnits)
{
    AvailableUnits = InUnits;
    OnUnitListUpdated();
}

void UTDUnitPanelWidget::RequestTraining(UTDUnitDataAsset* UnitData, int32 Count)
{
    if (UnitData && Count > 0)
    {
        OnTrainRequested.Broadcast(UnitData, Count);
    }
}
