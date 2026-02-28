// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDGameMode.h"
#include "TDGameState.h"
#include "TDPlayerState.h"
#include "TimerManager.h"

ATDGameMode::ATDGameMode()
    : CurrentPhase(ETDGamePhase::None)
    , CurrentRound(0)
{
    // 设置默认类
    GameStateClass = ATDGameState::StaticClass();
    PlayerStateClass = ATDPlayerState::StaticClass();

    // 当前阶段暂不设置 DefaultPawnClass，后续由策略相机 Pawn 替代
    DefaultPawnClass = nullptr;
}

void ATDGameMode::BeginPlay()
{
    Super::BeginPlay();

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::BeginPlay - GameMode initialized."));
}

void ATDGameMode::PostLogin(APlayerController* NewPlayer)
{
    Super::PostLogin(NewPlayer);

    if (!IsValid(NewPlayer))
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::PostLogin - Player joined: %s"),
        *NewPlayer->GetName());
}

// ─── 对局控制 ─────────────────────────────────────────

void ATDGameMode::StartMatch()
{
    if (CurrentPhase != ETDGamePhase::None)
    {
        UE_LOG(LogTemp, Warning, TEXT("ATDGameMode::StartMatch - Match already in progress (Phase: %d)"),
            static_cast<uint8>(CurrentPhase));
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::StartMatch - Starting match."));

    // 同步配置到 GameState
    ATDGameState* TDGameState = GetTDGameState();
    if (IsValid(TDGameState))
    {
        TDGameState->SetMatchConfig(MatchConfig);
        TDGameState->SetMaxRounds(MatchConfig.MaxRounds);
        TDGameState->ClearAlivePlayers();
    }

    // 初始化所有已连接的玩家
    for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
    {
        APlayerController* PC = It->Get();
        if (!IsValid(PC))
        {
            continue;
        }

        ATDPlayerState* PlayerState = Cast<ATDPlayerState>(PC->PlayerState);
        if (IsValid(PlayerState))
        {
            InitializePlayerForMatch(PlayerState);
        }
    }

    // 重置回合计数，开始第一回合
    CurrentRound = 0;
    StartNewRound();
}

void ATDGameMode::EndMatch()
{
    if (CurrentPhase == ETDGamePhase::GameOver || CurrentPhase == ETDGamePhase::None)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::EndMatch - Match ending at round %d."), CurrentRound);

    StopPhaseTimer();
    SetPhase(ETDGamePhase::GameOver);
}

// ─── 阶段控制 ─────────────────────────────────────────

void ATDGameMode::AdvanceToNextPhase()
{
    StopPhaseTimer();

    switch (CurrentPhase)
    {
    case ETDGamePhase::None:
        // 不应从 None 直接推进，需通过 StartMatch
        UE_LOG(LogTemp, Warning, TEXT("ATDGameMode::AdvanceToNextPhase - Cannot advance from None. Use StartMatch()."));
        break;

    case ETDGamePhase::Preparation:
        SetPhase(ETDGamePhase::Matchmaking);
        break;

    case ETDGamePhase::Matchmaking:
        SetPhase(ETDGamePhase::Battle);
        break;

    case ETDGamePhase::Battle:
        SetPhase(ETDGamePhase::Settlement);
        break;

    case ETDGamePhase::Settlement:
        // 结算后判断是否继续
        if (ShouldEndMatch())
        {
            EndMatch();
        }
        else
        {
            StartNewRound();
        }
        break;

    case ETDGamePhase::GameOver:
        UE_LOG(LogTemp, Log, TEXT("ATDGameMode::AdvanceToNextPhase - Match is over. No further phases."));
        break;
    }
}

void ATDGameMode::StartNewRound()
{
    CurrentRound++;

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::StartNewRound - Round %d / %d"),
        CurrentRound, MatchConfig.MaxRounds);

    // 同步回合数到 GameState
    ATDGameState* TDGameState = GetTDGameState();
    if (IsValid(TDGameState))
    {
        TDGameState->SetCurrentRound(CurrentRound);
    }

    // 进入准备阶段
    SetPhase(ETDGamePhase::Preparation);
}

// ─── 阶段计时回调 ─────────────────────────────────────

void ATDGameMode::OnPhaseTimeExpired()
{
    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::OnPhaseTimeExpired - Phase %d time expired."),
        static_cast<uint8>(CurrentPhase));

    AdvanceToNextPhase();
}

// ─── 阶段生命周期钩子 ─────────────────────────────────

void ATDGameMode::OnPreparationPhaseStarted()
{
    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::OnPreparationPhaseStarted - Round %d"), CurrentRound);

    // 发放回合基础资源（第一回合不发放，因为 ResetForNewMatch 已设置初始资源）
    if (CurrentRound > 1)
    {
        for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
        {
            APlayerController* PC = It->Get();
            if (!IsValid(PC))
            {
                continue;
            }

            ATDPlayerState* PlayerState = Cast<ATDPlayerState>(PC->PlayerState);
            if (IsValid(PlayerState) && PlayerState->IsAlive())
            {
                PlayerState->AddGold(MatchConfig.GoldPerRound);
            }
        }
    }

    StartPhaseTimer(MatchConfig.PreparationTime);
}

void ATDGameMode::OnMatchmakingPhaseStarted()
{
    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::OnMatchmakingPhaseStarted - Round %d"), CurrentRound);

    // 配对逻辑当前为占位，后续由 MatchmakingManager 实现
    // 配对阶段无倒计时，自动推进到战斗
    AdvanceToNextPhase();
}

void ATDGameMode::OnBattlePhaseStarted()
{
    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::OnBattlePhaseStarted - Round %d"), CurrentRound);

    StartPhaseTimer(MatchConfig.BattleTime);
}

void ATDGameMode::OnSettlementPhaseStarted()
{
    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::OnSettlementPhaseStarted - Round %d"), CurrentRound);

    // 结算逻辑当前为占位，后续根据战斗结果执行 ApplyRoundReward
    // 结算阶段无倒计时，处理完毕后自动推进
    AdvanceToNextPhase();
}

void ATDGameMode::OnGameOverPhaseStarted()
{
    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::OnGameOverPhaseStarted - Match ended at round %d."), CurrentRound);
}

// ─── 内部辅助 ─────────────────────────────────────────

void ATDGameMode::SetPhase(ETDGamePhase NewPhase)
{
    if (CurrentPhase == NewPhase)
    {
        return;
    }

    const ETDGamePhase OldPhase = CurrentPhase;
    CurrentPhase = NewPhase;

    // 同步到 GameState
    ATDGameState* TDGameState = GetTDGameState();
    if (IsValid(TDGameState))
    {
        TDGameState->SetCurrentPhase(NewPhase);
    }

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::SetPhase - %d -> %d"),
        static_cast<uint8>(OldPhase), static_cast<uint8>(NewPhase));

    // 派发到对应的阶段钩子
    switch (NewPhase)
    {
    case ETDGamePhase::Preparation:
        OnPreparationPhaseStarted();
        break;
    case ETDGamePhase::Matchmaking:
        OnMatchmakingPhaseStarted();
        break;
    case ETDGamePhase::Battle:
        OnBattlePhaseStarted();
        break;
    case ETDGamePhase::Settlement:
        OnSettlementPhaseStarted();
        break;
    case ETDGamePhase::GameOver:
        OnGameOverPhaseStarted();
        break;
    default:
        break;
    }
}

void ATDGameMode::StartPhaseTimer(float Duration)
{
    if (Duration <= 0.0f)
    {
        return;
    }

    UWorld* World = GetWorld();
    if (!IsValid(World))
    {
        return;
    }

    // 设置 GameState 的阶段结束时间（供客户端倒计时）
    ATDGameState* TDGameState = GetTDGameState();
    if (IsValid(TDGameState))
    {
        TDGameState->SetPhaseEndTime(World->GetTimeSeconds() + Duration);
    }

    // 启动本地计时器
    World->GetTimerManager().SetTimer(
        PhaseTimerHandle,
        this,
        &ATDGameMode::OnPhaseTimeExpired,
        Duration,
        false
    );

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::StartPhaseTimer - %.1f seconds"), Duration);
}

void ATDGameMode::StopPhaseTimer()
{
    UWorld* World = GetWorld();
    if (IsValid(World))
    {
        World->GetTimerManager().ClearTimer(PhaseTimerHandle);
    }
}

ATDGameState* ATDGameMode::GetTDGameState() const
{
    return GetGameState<ATDGameState>();
}

void ATDGameMode::InitializePlayerForMatch(ATDPlayerState* Player)
{
    if (!IsValid(Player))
    {
        return;
    }

    Player->ResetForNewMatch(MatchConfig);

    // 加入存活玩家列表
    ATDGameState* TDGameState = GetTDGameState();
    if (IsValid(TDGameState))
    {
        TDGameState->AddAlivePlayer(Player);
    }

    UE_LOG(LogTemp, Log, TEXT("ATDGameMode::InitializePlayerForMatch - Player %s initialized."),
        *Player->GetPlayerName());
}

bool ATDGameMode::ShouldEndMatch() const
{
    const ATDGameState* TDGameState = GetTDGameState();

    // 仅剩一人或无人存活
    if (IsValid(TDGameState) && TDGameState->GetAlivePlayerCount() <= 1)
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
