// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDPlayerState.h"
#include "Net/UnrealNetwork.h"

ATDPlayerState::ATDPlayerState()
    : Health(100)
    , MaxHealth(100)
    , Gold(0)
    , ResearchPoints(0)
    , CurrentTechEra(0)
    , bIsAlive(true)
    , WinCount(0)
    , LossCount(0)
    , WinStreak(0)
    , LoseStreak(0)
{
}

void ATDPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(ATDPlayerState, Health);
    DOREPLIFETIME(ATDPlayerState, MaxHealth);
    DOREPLIFETIME(ATDPlayerState, Gold);
    DOREPLIFETIME(ATDPlayerState, ResearchPoints);
    DOREPLIFETIME(ATDPlayerState, CurrentTechEra);
    DOREPLIFETIME(ATDPlayerState, bIsAlive);
    DOREPLIFETIME(ATDPlayerState, WinCount);
    DOREPLIFETIME(ATDPlayerState, LossCount);
    DOREPLIFETIME(ATDPlayerState, WinStreak);
    DOREPLIFETIME(ATDPlayerState, LoseStreak);
}

// ─── 查询接口 ─────────────────────────────────────────

bool ATDPlayerState::CanAfford(int32 Cost) const
{
    return Gold >= Cost;
}

bool ATDPlayerState::IsDead() const
{
    return !bIsAlive;
}

// ─── 资源操作 ─────────────────────────────────────────

void ATDPlayerState::AddGold(int32 Amount)
{
    if (!HasAuthority())
    {
        return;
    }

    if (Amount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDPlayerState::AddGold - Invalid amount: %d"), Amount);
        return;
    }

    const int32 OldGold = Gold;
    Gold += Amount;
    OnRep_Gold(OldGold);
}

bool ATDPlayerState::SpendGold(int32 Amount)
{
    if (!HasAuthority())
    {
        return false;
    }

    if (Amount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDPlayerState::SpendGold - Invalid amount: %d"), Amount);
        return false;
    }

    if (!CanAfford(Amount))
    {
        UE_LOG(LogTemp, Verbose, TEXT("ATDPlayerState::SpendGold - Cannot afford %d (have %d)"), Amount, Gold);
        return false;
    }

    const int32 OldGold = Gold;
    Gold -= Amount;
    OnRep_Gold(OldGold);
    return true;
}

void ATDPlayerState::AddResearchPoints(int32 Amount)
{
    if (!HasAuthority())
    {
        return;
    }

    if (Amount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDPlayerState::AddResearchPoints - Invalid amount: %d"), Amount);
        return;
    }

    ResearchPoints += Amount;
}

bool ATDPlayerState::SpendResearchPoints(int32 Amount)
{
    if (!HasAuthority())
    {
        return false;
    }

    if (Amount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDPlayerState::SpendResearchPoints - Invalid amount: %d"), Amount);
        return false;
    }

    if (ResearchPoints < Amount)
    {
        UE_LOG(LogTemp, Verbose,
            TEXT("ATDPlayerState::SpendResearchPoints - Cannot afford %d (have %d)"),
            Amount, ResearchPoints);
        return false;
    }

    ResearchPoints -= Amount;
    return true;
}

// ─── 血量操作 ─────────────────────────────────────────

void ATDPlayerState::ApplyDamage(int32 Damage)
{
    if (!HasAuthority())
    {
        return;
    }

    if (Damage <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDPlayerState::ApplyDamage - Invalid damage: %d"), Damage);
        return;
    }

    if (!bIsAlive)
    {
        return;
    }

    const int32 NewHealth = FMath::Max(0, Health - Damage);
    SetHealth(NewHealth);

    if (Health <= 0)
    {
        bIsAlive = false;
        OnRep_bIsAlive();
    }
}

void ATDPlayerState::HealHealth(int32 Amount)
{
    if (!HasAuthority())
    {
        return;
    }

    if (Amount <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDPlayerState::HealHealth - Invalid amount: %d"), Amount);
        return;
    }

    if (!bIsAlive)
    {
        return;
    }

    const int32 NewHealth = FMath::Min(MaxHealth, Health + Amount);
    SetHealth(NewHealth);
}

// ─── 回合结算 ─────────────────────────────────────────

void ATDPlayerState::ApplyRoundReward(bool bWon, const FTDMatchConfig& MatchConfig)
{
    if (!HasAuthority())
    {
        return;
    }

    // 基础回合收入
    AddGold(MatchConfig.GoldPerRound);

    if (bWon)
    {
        WinCount++;
        AddGold(MatchConfig.WinBonusGold);
    }
    else
    {
        LossCount++;
        ApplyDamage(MatchConfig.LoseDamage);
    }
}

void ATDPlayerState::ResetForNewMatch(const FTDMatchConfig& MatchConfig)
{
    if (!HasAuthority())
    {
        return;
    }

    MaxHealth = MatchConfig.StartingHealth;
    SetHealth(MaxHealth);

    const int32 OldGold = Gold;
    Gold = MatchConfig.StartingGold;
    OnRep_Gold(OldGold);

    ResearchPoints = 0;
    CurrentTechEra = 0;
    bIsAlive = true;
    WinCount = 0;
    LossCount = 0;
    WinStreak = 0;
    LoseStreak = 0;
}

// ─── RepNotify 回调 ───────────────────────────────────

void ATDPlayerState::SetWinStreak(int32 InStreak)
{
    if (!HasAuthority())
    {
        return;
    }
    WinStreak = FMath::Max(0, InStreak);
}

void ATDPlayerState::SetLoseStreak(int32 InStreak)
{
    if (!HasAuthority())
    {
        return;
    }
    LoseStreak = FMath::Max(0, InStreak);
}

void ATDPlayerState::OnRep_Health(int32 OldValue)
{
    OnHealthChanged.Broadcast(OldValue, Health);
}

void ATDPlayerState::OnRep_Gold(int32 OldValue)
{
    OnGoldChanged.Broadcast(OldValue, Gold);
}

void ATDPlayerState::OnRep_bIsAlive()
{
    if (!bIsAlive)
    {
        OnPlayerDied.Broadcast(this);
    }
}

// ─── 内部辅助 ─────────────────────────────────────────

void ATDPlayerState::SetHealth(int32 NewHealth)
{
    const int32 OldHealth = Health;
    Health = FMath::Clamp(NewHealth, 0, MaxHealth);

    if (OldHealth != Health)
    {
        OnRep_Health(OldHealth);
    }
}
