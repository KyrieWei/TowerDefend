// Copyright TowerDefend. All Rights Reserved.

#include "UI/TDTechTreeWidget.h"
#include "TechTree/TDTechTreeManager.h"

void UTDTechTreeWidget::SetTechTreeManager(UTDTechTreeManager* InManager)
{
    TechTreeManager = InManager;
    RefreshDisplay();
}

void UTDTechTreeWidget::RequestResearch(FName TechID)
{
    OnResearchRequested.Broadcast(TechID);
}

void UTDTechTreeWidget::RefreshDisplay()
{
    OnDisplayRefreshed();
}
