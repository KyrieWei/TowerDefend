// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDMatchManager.h"
#include "Core/TDPlayerState.h"

void UTDMatchManager::InitializeMatch(
    const TArray<ATDPlayerState*>& Players,
    const FTDMatchConfig& Config)
{
    Reset();

    MatchConfig = Config;

    RegisteredPlayers.Empty(Players.Num());
    for (int32 i = 0; i < Players.Num(); ++i)
    {
        ATDPlayerState* Player = Players[i];
        RegisteredPlayers.Add(Player);

        AliveStatus.Add(i, true);
        PlayerWinStreaks.Add(i, 0);
        PlayerLoseStreaks.Add(i, 0);
    }

    UE_LOG(LogTemp, Log,
        TEXT("UTDMatchManager::InitializeMatch - Match initialized with %d players, MaxRounds=%d"),
        Players.Num(), Config.MaxRounds);
}

void UTDMatchManager::RecordRoundResult(const FTDRoundResult& Result)
{
    RoundHistory.Add(Result);

    // 确定本次对战的胜者和败者索引
    const int32 WinnerIndex = Result.bAttackerWon
        ? Result.AttackerPlayerIndex
        : Result.DefenderPlayerIndex;
    const int32 LoserIndex = Result.bAttackerWon
        ? Result.DefenderPlayerIndex
        : Result.AttackerPlayerIndex;

    UpdateStreaks(WinnerIndex, LoserIndex);

    UE_LOG(LogTemp, Log,
        TEXT("UTDMatchManager::RecordRoundResult - Round %d: Winner=Player[%d], Loser=Player[%d], Damage=%d"),
        Result.RoundNumber, WinnerIndex, LoserIndex, Result.DamageDealt);
}

bool UTDMatchManager::ShouldEndMatch() const
{
    // 存活人数 <= 1
    if (GetAlivePlayerCount() <= 1)
    {
        return true;
    }

    // 回合数达到上限
    if (CurrentRound >= MatchConfig.MaxRounds)
    {
        return true;
    }

    return false;
}

ATDPlayerState* UTDMatchManager::GetWinner() const
{
    ATDPlayerState* LastAlive = nullptr;
    int32 AliveCount = 0;

    for (const auto& Pair : AliveStatus)
    {
        if (Pair.Value && RegisteredPlayers.IsValidIndex(Pair.Key))
        {
            ATDPlayerState* Player = RegisteredPlayers[Pair.Key];
            if (IsValid(Player))
            {
                LastAlive = Player;
                AliveCount++;
            }
        }
    }

    // 恰好一人存活才返回胜者，多人存活时返回 nullptr
    return (AliveCount == 1) ? LastAlive : nullptr;
}

int32 UTDMatchManager::GetCurrentRound() const
{
    return CurrentRound;
}

const TArray<FTDRoundResult>& UTDMatchManager::GetRoundHistory() const
{
    return RoundHistory;
}

int32 UTDMatchManager::GetPlayerWinStreak(int32 PlayerIndex) const
{
    const int32* StreakPtr = PlayerWinStreaks.Find(PlayerIndex);
    return StreakPtr ? *StreakPtr : 0;
}

int32 UTDMatchManager::GetPlayerLoseStreak(int32 PlayerIndex) const
{
    const int32* StreakPtr = PlayerLoseStreaks.Find(PlayerIndex);
    return StreakPtr ? *StreakPtr : 0;
}

int32 UTDMatchManager::GetAlivePlayerCount() const
{
    int32 Count = 0;
    for (const auto& Pair : AliveStatus)
    {
        if (Pair.Value)
        {
            Count++;
        }
    }
    return Count;
}

void UTDMatchManager::AdvanceRound()
{
    CurrentRound++;
}

void UTDMatchManager::MarkPlayerEliminated(int32 PlayerIndex)
{
    if (bool* Status = AliveStatus.Find(PlayerIndex))
    {
        if (*Status)
        {
            *Status = false;
            UE_LOG(LogTemp, Log,
                TEXT("UTDMatchManager::MarkPlayerEliminated - Player[%d] eliminated. Alive: %d"),
                PlayerIndex, GetAlivePlayerCount());
        }
    }
    else
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDMatchManager::MarkPlayerEliminated - Unknown player index: %d"),
            PlayerIndex);
    }
}

void UTDMatchManager::Reset()
{
    RoundHistory.Empty();
    PlayerWinStreaks.Empty();
    PlayerLoseStreaks.Empty();
    AliveStatus.Empty();
    RegisteredPlayers.Empty();
    CurrentRound = 0;
    MatchConfig = FTDMatchConfig();
}

// ─── 内部辅助 ─────────────────────────────────────────

void UTDMatchManager::UpdateStreaks(int32 WinnerIndex, int32 LoserIndex)
{
    // 胜者：连胜 +1，连败归零
    if (int32* WinStreak = PlayerWinStreaks.Find(WinnerIndex))
    {
        (*WinStreak)++;
    }
    if (int32* LoseStreak = PlayerLoseStreaks.Find(WinnerIndex))
    {
        *LoseStreak = 0;
    }

    // 败者：连败 +1，连胜归零
    if (int32* LoseStreak = PlayerLoseStreaks.Find(LoserIndex))
    {
        (*LoseStreak)++;
    }
    if (int32* WinStreak = PlayerWinStreaks.Find(LoserIndex))
    {
        *WinStreak = 0;
    }
}
