// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexGridReplication.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"

void UTDHexGridReplication::SetGridManager(ATDHexGridManager* InGrid)
{
    GridManager = InGrid;
}

void UTDHexGridReplication::RecordDelta(const FTDGridDelta& Delta)
{
    PendingDeltas.Add(Delta);
}

bool UTDHexGridReplication::ApplyDelta(const FTDGridDelta& Delta)
{
    ATDHexGridManager* Grid = GridManager.Get();
    if (!Grid)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDHexGridReplication::ApplyDelta - Grid manager is null."));
        return false;
    }

    ATDHexTile* Tile = Grid->GetTileAt(Delta.Coord);
    if (!Tile)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDHexGridReplication::ApplyDelta - No tile at %s."),
            *Delta.Coord.ToString());
        return false;
    }

    if (Delta.NewTerrainType >= 0)
    {
        Tile->SetTerrainType(static_cast<ETDTerrainType>(Delta.NewTerrainType));
    }

    if (Delta.NewHeightLevel != INT_MIN)
    {
        Tile->SetHeightLevel(Delta.NewHeightLevel);
    }

    if (Delta.NewOwnerPlayerIndex != -2)
    {
        Tile->SetOwnerPlayerIndex(Delta.NewOwnerPlayerIndex);
    }

    OnGridDeltaApplied.Broadcast(Delta);
    return true;
}

void UTDHexGridReplication::ClearPendingDeltas()
{
    PendingDeltas.Empty();
}

TArray<FTDGridDelta> UTDHexGridReplication::GenerateFullSnapshot() const
{
    TArray<FTDGridDelta> Snapshot;

    ATDHexGridManager* Grid = GridManager.Get();
    if (!Grid)
    {
        return Snapshot;
    }

    TArray<ATDHexTile*> AllTiles = Grid->GetAllTiles();
    Snapshot.Reserve(AllTiles.Num());

    for (ATDHexTile* Tile : AllTiles)
    {
        if (!Tile)
        {
            continue;
        }

        FTDGridDelta Delta;
        Delta.Coord = Tile->GetCoord();
        Delta.NewTerrainType = static_cast<int32>(Tile->GetTerrainType());
        Delta.NewHeightLevel = Tile->GetHeightLevel();
        Delta.NewOwnerPlayerIndex = Tile->GetOwnerPlayerIndex();

        Snapshot.Add(Delta);
    }

    return Snapshot;
}
