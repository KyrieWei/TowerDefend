// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDRoundManager.h"
#include "TDMatchmakingManager.h"
#include "Core/TDPlayerState.h"
#include "Combat/TDCombatManager.h"
#include "HexGrid/TDHexGridManager.h"
#include "Building/TDBuildingManager.h"
#include "Unit/TDUnitSquad.h"

void UTDRoundManager::InitializeRound(
    const TArray<ATDPlayerState*>& AlivePlayers,
    int32 RoundNumber,
    const TArray<FTDRoundResult>& MatchHistory)
{
    Reset();

    CurrentRoundNumber = RoundNumber;

    // 缓存存活玩家列表
    CachedAlivePlayers.Empty(AlivePlayers.Num());
    for (ATDPlayerState* Player : AlivePlayers)
    {
        if (IsValid(Player))
        {
            CachedAlivePlayers.Add(Player);
        }
    }

    if (CachedAlivePlayers.Num() < 2)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDRoundManager::InitializeRound - Not enough players (%d) for round %d"),
            CachedAlivePlayers.Num(), RoundNumber);
        return;
    }

    // 构建原始指针数组以匹配 MatchmakingManager 接口签名
    TArray<ATDPlayerState*> RawPlayerPtrs;
    RawPlayerPtrs.Reserve(CachedAlivePlayers.Num());
    for (const TObjectPtr<ATDPlayerState>& PlayerPtr : CachedAlivePlayers)
    {
        RawPlayerPtrs.Add(PlayerPtr.Get());
    }

    // 通过配对管理器生成配对
    if (IsValid(MatchmakingManager))
    {
        CurrentPairings = MatchmakingManager->GeneratePairings(RawPlayerPtrs, MatchHistory);
    }
    else
    {
        UE_LOG(LogTemp, Error,
            TEXT("UTDRoundManager::InitializeRound - MatchmakingManager is null, cannot generate pairings"));
    }

    UE_LOG(LogTemp, Log,
        TEXT("UTDRoundManager::InitializeRound - Round %d initialized with %d pairings"),
        RoundNumber, CurrentPairings.Num());
}

const TArray<FTDRoundPairing>& UTDRoundManager::GetCurrentPairings() const
{
    return CurrentPairings;
}

void UTDRoundManager::ExecuteBattles()
{
    CurrentRoundResults.Empty(CurrentPairings.Num());

    for (const FTDRoundPairing& Pairing : CurrentPairings)
    {
        if (!Pairing.bIsValid)
        {
            continue;
        }

        FTDRoundResult Result;
        Result.RoundNumber = CurrentRoundNumber;
        Result.AttackerPlayerIndex = Pairing.AttackerIndex;
        Result.DefenderPlayerIndex = Pairing.DefenderIndex;

        ATDHexGridManager* Grid = GridManager.Get();

        if (CombatManager && Grid)
        {
            CombatManager->InitializeCombat(
                Pairing.AttackerIndex,
                Pairing.DefenderIndex,
                Grid);

            CombatManager->SetBuildingManager(BuildingManager);
            CombatManager->SetUnitSquad(UnitSquad);

            FTDRoundResult CombatResult = CombatManager->ExecuteFullCombat();
            Result.bAttackerWon = CombatResult.bAttackerWon;
            Result.DamageDealt = CombatResult.DamageDealt;

            CombatManager->Reset();
        }
        else
        {
            Result.bAttackerWon = FMath::RandBool();
            Result.DamageDealt = FMath::RandRange(5, 15);
            UE_LOG(LogTemp, Warning,
                TEXT("UTDRoundManager::ExecuteBattles - "
                     "Using simulated combat (CombatManager or Grid unavailable)"));
        }

        CurrentRoundResults.Add(Result);

        UE_LOG(LogTemp, Log,
            TEXT("UTDRoundManager::ExecuteBattles - "
                 "Round %d: Player[%d] vs Player[%d] -> %s wins, Damage=%d"),
            CurrentRoundNumber,
            Pairing.AttackerIndex,
            Pairing.DefenderIndex,
            Result.bAttackerWon ? TEXT("Attacker") : TEXT("Defender"),
            Result.DamageDealt);
    }
}

const TArray<FTDRoundResult>& UTDRoundManager::GetRoundResults() const
{
    return CurrentRoundResults;
}

void UTDRoundManager::Reset()
{
    CurrentPairings.Empty();
    CurrentRoundResults.Empty();
    CachedAlivePlayers.Empty();
    CurrentRoundNumber = 0;
}

void UTDRoundManager::SetMatchmakingManager(UTDMatchmakingManager* InMatchmakingManager)
{
    MatchmakingManager = InMatchmakingManager;
}

void UTDRoundManager::SetCombatManager(UTDCombatManager* InCombatManager)
{
    CombatManager = InCombatManager;
}

void UTDRoundManager::SetGridManager(ATDHexGridManager* InGridManager)
{
    GridManager = InGridManager;
}

void UTDRoundManager::SetBuildingManager(UTDBuildingManager* InBuildingManager)
{
    BuildingManager = InBuildingManager;
}

void UTDRoundManager::SetUnitSquad(UTDUnitSquad* InUnitSquad)
{
    UnitSquad = InUnitSquad;
}
