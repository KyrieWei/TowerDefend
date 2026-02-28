// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDGameState.h"
#include "TDPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/GameStateBase.h"

ATDGameState::ATDGameState()
    : CurrentPhase(ETDGamePhase::None)
    , CurrentRound(0)
    , MaxRounds(20)
    , PhaseEndTime(0.0f)
{
}

void ATDGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ATDGameState, CurrentPhase);
    DOREPLIFETIME(ATDGameState, CurrentRound);
    DOREPLIFETIME(ATDGameState, MaxRounds);
    DOREPLIFETIME(ATDGameState, PhaseEndTime);
    DOREPLIFETIME(ATDGameState, AlivePlayers);
    DOREPLIFETIME(ATDGameState, MatchConfig);
}

// ─── 查询接口 ─────────────────────────────────────────

float ATDGameState::GetPhaseRemainingTime() const
{
    if (PhaseEndTime <= 0.0f)
    {
        return 0.0f;
    }

    const UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return 0.0f;
    }

    const float Remaining = PhaseEndTime - World->GetTimeSeconds();
    return FMath::Max(0.0f, Remaining);
}

int32 ATDGameState::GetAlivePlayerCount() const
{
    return AlivePlayers.Num();
}

bool ATDGameState::IsPlayerAlive(APlayerState* Player) const
{
    if (!IsValid(Player))
    {
        return false;
    }

    ATDPlayerState* TDPlayer = Cast<ATDPlayerState>(Player);
    if (!IsValid(TDPlayer))
    {
        return false;
    }

    return AlivePlayers.Contains(TDPlayer);
}

// ─── 写操作 ───────────────────────────────────────────

void ATDGameState::SetCurrentPhase(ETDGamePhase NewPhase)
{
    if (!HasAuthority())
    {
        return;
    }

    if (CurrentPhase == NewPhase)
    {
        return;
    }

    const ETDGamePhase OldPhase = CurrentPhase;
    CurrentPhase = NewPhase;
    OnRep_CurrentPhase(OldPhase);
}

void ATDGameState::SetCurrentRound(int32 NewRound)
{
    if (!HasAuthority())
    {
        return;
    }

    if (CurrentRound == NewRound)
    {
        return;
    }

    const int32 OldRound = CurrentRound;
    CurrentRound = NewRound;
    OnRep_CurrentRound(OldRound);
}

void ATDGameState::SetMaxRounds(int32 InMaxRounds)
{
    if (!HasAuthority())
    {
        return;
    }

    MaxRounds = FMath::Max(1, InMaxRounds);
}

void ATDGameState::SetPhaseEndTime(float ServerTime)
{
    if (!HasAuthority())
    {
        return;
    }

    PhaseEndTime = ServerTime;
}

void ATDGameState::SetMatchConfig(const FTDMatchConfig& InConfig)
{
    if (!HasAuthority())
    {
        return;
    }

    MatchConfig = InConfig;
}

void ATDGameState::EliminatePlayer(ATDPlayerState* Player)
{
    if (!HasAuthority())
    {
        return;
    }

    if (!IsValid(Player))
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDGameState::EliminatePlayer - Invalid player"));
        return;
    }

    const int32 RemovedCount = AlivePlayers.Remove(Player);
    if (RemovedCount > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("ATDGameState::EliminatePlayer - Player %s eliminated. Alive: %d"),
            *Player->GetPlayerName(), AlivePlayers.Num());
        OnPlayerEliminated.Broadcast(Player);
    }
}

void ATDGameState::AddAlivePlayer(ATDPlayerState* Player)
{
    if (!HasAuthority())
    {
        return;
    }

    if (!IsValid(Player))
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDGameState::AddAlivePlayer - Invalid player"));
        return;
    }

    AlivePlayers.AddUnique(Player);
}

void ATDGameState::ClearAlivePlayers()
{
    if (!HasAuthority())
    {
        return;
    }

    AlivePlayers.Empty();
}

// ─── RepNotify 回调 ───────────────────────────────────

void ATDGameState::OnRep_CurrentPhase(ETDGamePhase OldPhase)
{
    OnPhaseChanged.Broadcast(OldPhase, CurrentPhase);
}

void ATDGameState::OnRep_CurrentRound(int32 OldRound)
{
    OnRoundChanged.Broadcast(CurrentRound);
}
