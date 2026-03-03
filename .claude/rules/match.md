---
paths:
  - "Source/TowerDefend/Match/**"
---

# Match Management Rules

Match system manages multi-round elimination tournament flow, player pairing algorithms, and win/loss streak tracking.

## Dependency Constraint

Match depends **only on Core** (PlayerState + GamePhaseTypes). It must not import HexGrid, Building, Unit, or Combat.

## Matchmaking Strategies (ETDMatchmakingStrategy in TDMatchmakingManager.h)

| Strategy | Algorithm | Use Case |
|----------|-----------|----------|
| Random | Random pairing each round | Early/casual games |
| Swiss | Pair players with similar records | Competitive fairness |
| Serpentine | Snake-order by ranking | Tournament seeding |

## Structs

- `FTDRoundPairing` (TDRoundManager.h) — Round pairing data: attacker/defender indices, validity flag

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDMatchManager` | UObject | Match management: round history, win/loss streaks, elimination check, winner query |
| `UTDRoundManager` | UObject | Single round management: generate pairings -> execute battles -> collect results |
| `UTDMatchmakingManager` | UObject | Pairing algorithms: Random/Swiss/Serpentine, odd-player bye handling |

## Key APIs

```
UTDMatchManager::InitializeMatch() / RecordRoundResult() / ShouldEndMatch() / GetWinner()
UTDRoundManager::InitializeRound() / ExecuteBattles() / GetRoundResults()
UTDMatchmakingManager::GeneratePairings() / SetStrategy()
```

## Match Flow

```
[Matchup Phase] -> N players enter
    |
[Preparation Phase] (each round start)
  - Receive resources (base + building output + previous round reward)
  - Build/upgrade buildings
  - Train/upgrade army
  - Research tech
  - Modify terrain (spend resources)
    |
[Pairing Phase]
  - System pairs players (serpentine/random/Swiss)
  - Random attack/defense assignment (or each side attacks once)
    |
[Battle Phase]
  - Attacker deploys -> Defender auto-defends
  - Real-time spectating / limited intervention
    |
[Settlement Phase]
  - Winner gets gold/research point reward
  - Loser loses HP (escalates with round number)
  - Eliminate players with 0 HP
  - Auto-save current map state
    |
[Loop] -> Back to Preparation, until champion is decided
```

## Odd Player Handling

When player count is odd, one player gets a "bye" (no opponent). The bye player:
- Neither wins nor loses
- Still receives base round income
- Does not take damage
