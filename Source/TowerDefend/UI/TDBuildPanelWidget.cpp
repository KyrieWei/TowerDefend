// Copyright TowerDefend. All Rights Reserved.

#include "UI/TDBuildPanelWidget.h"

void UTDBuildPanelWidget::SetAvailableBuildings(
    const TArray<UTDBuildingDataAsset*>& InBuildings)
{
    AvailableBuildings = InBuildings;
    OnBuildingListUpdated();
}

void UTDBuildPanelWidget::SelectBuilding(UTDBuildingDataAsset* Building)
{
    SelectedBuilding = Building;
    OnBuildingSelected.Broadcast(Building);
}

void UTDBuildPanelWidget::ClearSelection()
{
    SelectedBuilding = nullptr;
}
