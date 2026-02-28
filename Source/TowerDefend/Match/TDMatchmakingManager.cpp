// Copyright Epic Games, Inc. All Rights Reserved.

#include "TDMatchmakingManager.h"
#include "Core/TDPlayerState.h"

// ─── 公开接口 ─────────────────────────────────────────

TArray<FTDRoundPairing> UTDMatchmakingManager::GeneratePairings(
    const TArray<ATDPlayerState*>& AlivePlayers,
    const TArray<FTDRoundResult>& MatchHistory) const
{
    if (AlivePlayers.Num() < 2)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDMatchmakingManager::GeneratePairings - Not enough players (%d)"),
            AlivePlayers.Num());
        return TArray<FTDRoundPairing>();
    }

    switch (Strategy)
    {
    case ETDMatchmakingStrategy::Random:
        return GenerateRandomPairings(AlivePlayers);

    case ETDMatchmakingStrategy::Swiss:
        return GenerateSwissPairings(AlivePlayers, MatchHistory);

    case ETDMatchmakingStrategy::Serpentine:
        return GenerateSerpentinePairings(AlivePlayers);

    default:
        UE_LOG(LogTemp, Error,
            TEXT("UTDMatchmakingManager::GeneratePairings - Unknown strategy %d, fallback to Random"),
            static_cast<uint8>(Strategy));
        return GenerateRandomPairings(AlivePlayers);
    }
}

void UTDMatchmakingManager::SetStrategy(ETDMatchmakingStrategy NewStrategy)
{
    Strategy = NewStrategy;
}

ETDMatchmakingStrategy UTDMatchmakingManager::GetStrategy() const
{
    return Strategy;
}

// ─── 随机配对 ─────────────────────────────────────────

TArray<FTDRoundPairing> UTDMatchmakingManager::GenerateRandomPairings(
    const TArray<ATDPlayerState*>& Players) const
{
    // 构建索引列表并随机打乱
    TArray<int32> Indices;
    Indices.Reserve(Players.Num());
    for (int32 i = 0; i < Players.Num(); ++i)
    {
        Indices.Add(i);
    }

    // Fisher-Yates 洗牌
    for (int32 i = Indices.Num() - 1; i > 0; --i)
    {
        const int32 SwapTarget = FMath::RandRange(0, i);
        Indices.Swap(i, SwapTarget);
    }

    return BuildPairingsFromOrder(Indices);
}

// ─── 瑞士轮配对 ───────────────────────────────────────

TArray<FTDRoundPairing> UTDMatchmakingManager::GenerateSwissPairings(
    const TArray<ATDPlayerState*>& Players,
    const TArray<FTDRoundResult>& History) const
{
    // 按胜场数降序排列索引
    TArray<int32> Indices;
    Indices.Reserve(Players.Num());
    for (int32 i = 0; i < Players.Num(); ++i)
    {
        Indices.Add(i);
    }

    Indices.Sort([&Players](int32 A, int32 B)
    {
        const int32 WinsA = IsValid(Players[A]) ? Players[A]->GetWinCount() : 0;
        const int32 WinsB = IsValid(Players[B]) ? Players[B]->GetWinCount() : 0;
        return WinsA > WinsB;
    });

    // 贪心配对：尝试将相邻排名的玩家配对，避免重复对手
    TArray<bool> Paired;
    Paired.SetNumZeroed(Players.Num());

    TArray<int32> FinalOrder;
    FinalOrder.Reserve(Players.Num());

    for (int32 i = 0; i < Indices.Num(); ++i)
    {
        if (Paired[Indices[i]])
        {
            continue;
        }

        // 寻找最佳对手：从下一个未配对的人开始，优先选未交手过的
        int32 BestOpponent = INDEX_NONE;
        int32 FallbackOpponent = INDEX_NONE;

        for (int32 j = i + 1; j < Indices.Num(); ++j)
        {
            if (Paired[Indices[j]])
            {
                continue;
            }

            if (FallbackOpponent == INDEX_NONE)
            {
                FallbackOpponent = j;
            }

            if (!HaveFoughtBefore(Indices[i], Indices[j], History))
            {
                BestOpponent = j;
                break;
            }
        }

        // 没找到未交手过的对手，退而求其次
        if (BestOpponent == INDEX_NONE)
        {
            BestOpponent = FallbackOpponent;
        }

        // 配对成功
        if (BestOpponent != INDEX_NONE)
        {
            FinalOrder.Add(Indices[i]);
            FinalOrder.Add(Indices[BestOpponent]);
            Paired[Indices[i]] = true;
            Paired[Indices[BestOpponent]] = true;
        }
        else
        {
            // 只剩自己，轮空
            FinalOrder.Add(Indices[i]);
            Paired[Indices[i]] = true;
        }
    }

    return BuildPairingsFromOrder(FinalOrder);
}

// ─── 蛇形配对 ─────────────────────────────────────────

TArray<FTDRoundPairing> UTDMatchmakingManager::GenerateSerpentinePairings(
    const TArray<ATDPlayerState*>& Players) const
{
    // 按血量降序排列索引
    TArray<int32> Indices;
    Indices.Reserve(Players.Num());
    for (int32 i = 0; i < Players.Num(); ++i)
    {
        Indices.Add(i);
    }

    Indices.Sort([&Players](int32 A, int32 B)
    {
        const int32 HealthA = IsValid(Players[A]) ? Players[A]->GetHealth() : 0;
        const int32 HealthB = IsValid(Players[B]) ? Players[B]->GetHealth() : 0;
        return HealthA > HealthB;
    });

    // 蛇形：排序后直接依次配对（1v2, 3v4, ...）
    return BuildPairingsFromOrder(Indices);
}

// ─── 静态辅助 ─────────────────────────────────────────

void UTDMatchmakingManager::AssignAttackerDefender(FTDRoundPairing& Pairing)
{
    // 50% 概率交换攻守方
    if (FMath::RandBool())
    {
        const int32 Temp = Pairing.AttackerIndex;
        Pairing.AttackerIndex = Pairing.DefenderIndex;
        Pairing.DefenderIndex = Temp;
    }
}

TArray<FTDRoundPairing> UTDMatchmakingManager::BuildPairingsFromOrder(
    const TArray<int32>& OrderedIndices)
{
    TArray<FTDRoundPairing> Pairings;

    const int32 Count = OrderedIndices.Num();
    const int32 PairCount = Count / 2;
    Pairings.Reserve(PairCount + (Count % 2));

    // 两两配对
    for (int32 i = 0; i < PairCount * 2; i += 2)
    {
        FTDRoundPairing Pairing;
        Pairing.AttackerIndex = OrderedIndices[i];
        Pairing.DefenderIndex = OrderedIndices[i + 1];
        Pairing.bIsValid = true;
        AssignAttackerDefender(Pairing);
        Pairings.Add(Pairing);
    }

    // 奇数玩家：最后一人轮空
    if (Count % 2 != 0)
    {
        FTDRoundPairing ByePairing;
        ByePairing.AttackerIndex = OrderedIndices.Last();
        ByePairing.DefenderIndex = INDEX_NONE;
        ByePairing.bIsValid = false;
        Pairings.Add(ByePairing);

        UE_LOG(LogTemp, Log,
            TEXT("UTDMatchmakingManager::BuildPairingsFromOrder - Player[%d] gets a bye"),
            OrderedIndices.Last());
    }

    return Pairings;
}

bool UTDMatchmakingManager::HaveFoughtBefore(
    int32 IndexA,
    int32 IndexB,
    const TArray<FTDRoundResult>& History)
{
    for (const FTDRoundResult& Result : History)
    {
        const bool bMatchAB =
            (Result.AttackerPlayerIndex == IndexA && Result.DefenderPlayerIndex == IndexB);
        const bool bMatchBA =
            (Result.AttackerPlayerIndex == IndexB && Result.DefenderPlayerIndex == IndexA);

        if (bMatchAB || bMatchBA)
        {
            return true;
        }
    }

    return false;
}
