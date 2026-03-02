// Copyright TowerDefend. All Rights Reserved.

#include "Unit/TDUnitDeployer.h"
#include "Unit/TDUnitTrainer.h"
#include "Unit/TDUnitSquad.h"
#include "Unit/TDUnitBase.h"
#include "Unit/TDUnitDataAsset.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "Engine/World.h"

int32 UTDUnitDeployer::DeployUnits(
    UWorld* World,
    UTDUnitTrainer* Trainer,
    UTDUnitSquad* Squad,
    ATDHexGridManager* Grid,
    int32 AttackerIndex)
{
    if (!World || !Trainer || !Squad || !Grid)
    {
        UE_LOG(LogTemp, Error,
            TEXT("UTDUnitDeployer::DeployUnits - Invalid parameters."));
        return 0;
    }

    TArray<FTDTrainingEntry> Entries = Trainer->ConsumeQueue();
    if (Entries.Num() == 0)
    {
        return 0;
    }

    TArray<FTDHexCoord> DeployCoords = GetEdgeDeployCoords(Grid);
    if (DeployCoords.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDUnitDeployer::DeployUnits - No deploy coordinates available."));
        return 0;
    }

    int32 DeployedCount = 0;
    int32 CoordIndex = 0;

    for (const FTDTrainingEntry& Entry : Entries)
    {
        if (!Entry.UnitData)
        {
            continue;
        }

        for (int32 i = 0; i < Entry.Count; ++i)
        {
            if (CoordIndex >= DeployCoords.Num())
            {
                UE_LOG(LogTemp, Warning,
                    TEXT("UTDUnitDeployer::DeployUnits - Ran out of deploy coords."));
                return DeployedCount;
            }

            ATDUnitBase* Unit = SpawnUnit(
                World, Entry.UnitData, DeployCoords[CoordIndex], AttackerIndex);

            if (Unit)
            {
                Squad->AddUnit(Unit);
                DeployedCount++;
            }

            CoordIndex++;
        }
    }

    UE_LOG(LogTemp, Log,
        TEXT("UTDUnitDeployer::DeployUnits - Deployed %d units for player %d."),
        DeployedCount, AttackerIndex);

    return DeployedCount;
}

TArray<FTDHexCoord> UTDUnitDeployer::GetEdgeDeployCoords(
    const ATDHexGridManager* Grid) const
{
    TArray<FTDHexCoord> Result;

    if (!Grid)
    {
        return Result;
    }

    TArray<ATDHexTile*> AllTiles = Grid->GetAllTiles();
    for (ATDHexTile* Tile : AllTiles)
    {
        if (!Tile || !Tile->IsPassable())
        {
            continue;
        }

        FTDHexCoord Coord = Tile->GetCoord();
        TArray<ATDHexTile*> Neighbors = Grid->GetNeighborTiles(Coord);

        if (Neighbors.Num() < 6)
        {
            Result.Add(Coord);
        }
    }

    return Result;
}

ATDUnitBase* UTDUnitDeployer::SpawnUnit(
    UWorld* World,
    UTDUnitDataAsset* UnitData,
    const FTDHexCoord& SpawnCoord,
    int32 OwnerIndex) const
{
    if (!World || !UnitData)
    {
        return nullptr;
    }

    const FVector SpawnLocation = SpawnCoord.ToWorldPosition(100.0f);

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride =
        ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    ATDUnitBase* Unit = World->SpawnActor<ATDUnitBase>(
        ATDUnitBase::StaticClass(),
        SpawnLocation,
        FRotator::ZeroRotator,
        SpawnParams);

    if (Unit)
    {
        Unit->InitializeUnit(UnitData, SpawnCoord, OwnerIndex);
    }

    return Unit;
}
