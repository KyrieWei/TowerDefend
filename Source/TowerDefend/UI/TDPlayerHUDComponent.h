// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TDPlayerHUDComponent.generated.h"

class UTDHUDWidget;
class UTDBuildPanelWidget;
class UTDTechTreeWidget;
class UTDUnitPanelWidget;
class UTDMatchResultWidget;

/**
 * UTDPlayerHUDComponent - Player HUD management component.
 *
 * Attached to the PlayerController. Manages the creation, show/hide,
 * and lifecycle of all UI widgets. Automatically switches visible panels
 * based on the current game phase.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class TOWERDEFEND_API UTDPlayerHUDComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UTDPlayerHUDComponent();

    /** Initialize and create all UI widgets. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void InitializeHUD();

    /** Show the build panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void ShowBuildPanel();

    /** Hide the build panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void HideBuildPanel();

    /** Show the tech tree panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void ShowTechTreePanel();

    /** Hide the tech tree panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void HideTechTreePanel();

    /** Show the unit training panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void ShowUnitPanel();

    /** Hide the unit training panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void HideUnitPanel();

    /** Show the match result panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void ShowMatchResult();

    /** Hide the match result panel. */
    UFUNCTION(BlueprintCallable, Category = "TD|UI")
    void HideMatchResult();

    // --- Widget class references (editor-configurable) ---

    /** Main HUD widget class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|UI|Classes")
    TSubclassOf<UTDHUDWidget> HUDWidgetClass;

    /** Build panel widget class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|UI|Classes")
    TSubclassOf<UTDBuildPanelWidget> BuildPanelWidgetClass;

    /** Tech tree panel widget class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|UI|Classes")
    TSubclassOf<UTDTechTreeWidget> TechTreeWidgetClass;

    /** Unit training panel widget class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|UI|Classes")
    TSubclassOf<UTDUnitPanelWidget> UnitPanelWidgetClass;

    /** Match result panel widget class. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TD|UI|Classes")
    TSubclassOf<UTDMatchResultWidget> MatchResultWidgetClass;

    // --- Widget instance accessors ---

    /** Get the main HUD widget instance. */
    UFUNCTION(BlueprintPure, Category = "TD|UI")
    UTDHUDWidget* GetHUDWidget() const { return HUDWidget; }

    /** Get the build panel widget instance. */
    UFUNCTION(BlueprintPure, Category = "TD|UI")
    UTDBuildPanelWidget* GetBuildPanel() const { return BuildPanelWidget; }

protected:
    virtual void BeginPlay() override;

private:
    /** Main HUD widget instance. */
    UPROPERTY()
    UTDHUDWidget* HUDWidget = nullptr;

    /** Build panel widget instance. */
    UPROPERTY()
    UTDBuildPanelWidget* BuildPanelWidget = nullptr;

    /** Tech tree panel widget instance. */
    UPROPERTY()
    UTDTechTreeWidget* TechTreeWidget = nullptr;

    /** Unit training panel widget instance. */
    UPROPERTY()
    UTDUnitPanelWidget* UnitPanelWidget = nullptr;

    /** Match result panel widget instance. */
    UPROPERTY()
    UTDMatchResultWidget* MatchResultWidget = nullptr;
};
