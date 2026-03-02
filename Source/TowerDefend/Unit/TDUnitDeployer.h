// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "HexGrid/TDHexCoord.h"
#include "TDUnitDeployer.generated.h"

class UTDUnitTrainer;
class UTDUnitSquad;
class ATDHexGridManager;
class ATDUnitBase;
class UTDUnitDataAsset;

/**
 * UTDUnitDeployer - Unit deployment manager.
 *
 * At battle start, reads the TDUnitTrainer training queue,
 * spawns actual unit Actors at grid edge tiles for the attacker,
 * and registers them into TDUnitSquad.
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDUnitDeployer : public UObject
{
    GENERATED_BODY()

public:
    /**
     * Deploy the attacker's trained units onto the battlefield.
     * Consumes all entries from the training queue and spawns Actors at grid edges.
     *
     * @param World          World context.
     * @param Trainer        Attacker's training manager.
     * @param Squad          Unit squad manager.
     * @param Grid           Hex grid manager.
     * @param AttackerIndex  Attacker player index.
     * @return               Number of units successfully deployed.
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Unit|Deploy")
    int32 DeployUnits(
        UWorld* World,
        UTDUnitTrainer* Trainer,
        UTDUnitSquad* Squad,
        ATDHexGridManager* Grid,
        int32 AttackerIndex);

    /**
     * Get available edge deployment coordinates on the grid.
     *
     * @param Grid   Hex grid manager.
     * @return       Array of edge coordinates.
     */
    UFUNCTION(BlueprintPure, Category = "TD|Unit|Deploy")
    TArray<FTDHexCoord> GetEdgeDeployCoords(const ATDHexGridManager* Grid) const;

private:
    /**
     * Spawn a single unit Actor.
     *
     * @param World         World context.
     * @param UnitData      Unit data asset.
     * @param SpawnCoord    Spawn coordinate.
     * @param OwnerIndex    Owning player index.
     * @return              The spawned unit Actor.
     */
    ATDUnitBase* SpawnUnit(
        UWorld* World,
        UTDUnitDataAsset* UnitData,
        const FTDHexCoord& SpawnCoord,
        int32 OwnerIndex) const;
};
