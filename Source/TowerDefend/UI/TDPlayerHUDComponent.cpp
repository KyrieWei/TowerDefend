// Copyright TowerDefend. All Rights Reserved.

#include "UI/TDPlayerHUDComponent.h"
#include "UI/TDHUDWidget.h"
#include "UI/TDBuildPanelWidget.h"
#include "UI/TDTechTreeWidget.h"
#include "UI/TDUnitPanelWidget.h"
#include "UI/TDMatchResultWidget.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"

UTDPlayerHUDComponent::UTDPlayerHUDComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UTDPlayerHUDComponent::BeginPlay()
{
    Super::BeginPlay();
}

void UTDPlayerHUDComponent::InitializeHUD()
{
    APlayerController* PC = Cast<APlayerController>(GetOwner());
    if (!PC || !PC->IsLocalController())
    {
        return;
    }

    // Create main HUD
    if (HUDWidgetClass)
    {
        HUDWidget = CreateWidget<UTDHUDWidget>(PC, HUDWidgetClass);
        if (HUDWidget)
        {
            HUDWidget->AddToViewport(0);
        }
    }

    // Create build panel (hidden by default)
    if (BuildPanelWidgetClass)
    {
        BuildPanelWidget = CreateWidget<UTDBuildPanelWidget>(PC, BuildPanelWidgetClass);
        if (BuildPanelWidget)
        {
            BuildPanelWidget->AddToViewport(10);
            BuildPanelWidget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    // Create tech tree panel (hidden by default)
    if (TechTreeWidgetClass)
    {
        TechTreeWidget = CreateWidget<UTDTechTreeWidget>(PC, TechTreeWidgetClass);
        if (TechTreeWidget)
        {
            TechTreeWidget->AddToViewport(10);
            TechTreeWidget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    // Create unit panel (hidden by default)
    if (UnitPanelWidgetClass)
    {
        UnitPanelWidget = CreateWidget<UTDUnitPanelWidget>(PC, UnitPanelWidgetClass);
        if (UnitPanelWidget)
        {
            UnitPanelWidget->AddToViewport(10);
            UnitPanelWidget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }

    // Create match result panel (hidden by default)
    if (MatchResultWidgetClass)
    {
        MatchResultWidget = CreateWidget<UTDMatchResultWidget>(PC, MatchResultWidgetClass);
        if (MatchResultWidget)
        {
            MatchResultWidget->AddToViewport(20);
            MatchResultWidget->SetVisibility(ESlateVisibility::Collapsed);
        }
    }
}

void UTDPlayerHUDComponent::ShowBuildPanel()
{
    if (BuildPanelWidget)
    {
        BuildPanelWidget->SetVisibility(ESlateVisibility::Visible);
    }
}

void UTDPlayerHUDComponent::HideBuildPanel()
{
    if (BuildPanelWidget)
    {
        BuildPanelWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UTDPlayerHUDComponent::ShowTechTreePanel()
{
    if (TechTreeWidget)
    {
        TechTreeWidget->SetVisibility(ESlateVisibility::Visible);
    }
}

void UTDPlayerHUDComponent::HideTechTreePanel()
{
    if (TechTreeWidget)
    {
        TechTreeWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UTDPlayerHUDComponent::ShowUnitPanel()
{
    if (UnitPanelWidget)
    {
        UnitPanelWidget->SetVisibility(ESlateVisibility::Visible);
    }
}

void UTDPlayerHUDComponent::HideUnitPanel()
{
    if (UnitPanelWidget)
    {
        UnitPanelWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}

void UTDPlayerHUDComponent::ShowMatchResult()
{
    if (MatchResultWidget)
    {
        MatchResultWidget->SetVisibility(ESlateVisibility::Visible);
    }
}

void UTDPlayerHUDComponent::HideMatchResult()
{
    if (MatchResultWidget)
    {
        MatchResultWidget->SetVisibility(ESlateVisibility::Collapsed);
    }
}
