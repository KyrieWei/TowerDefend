---
paths:
  - "Source/TowerDefend/Economy/**"
---

# Economy System Rules

Economy system handles round income calculation, spending validation, resource distribution, and win/loss rewards/punishments.

## Dependency Constraint

Economy depends **only on Core** (PlayerState + GamePhaseTypes). It must not import HexGrid, Building, or any other game-logic module.

## Resource Types

| Resource | Use |
|----------|-----|
| Gold | Build buildings, train army, modify terrain |
| Research Points | Research technologies |
| Wood/Stone | Build specific buildings (optional, adds strategic depth) |

## Structs

- `FTDRoundReward` (TDRewardCalculator.h) — Round reward result: gold/research/HP change, win/loss flag

## Classes

| Class | Base | Role |
|-------|------|------|
| `UTDResourceManager` | UObject | Resource management: round income calculation, spending validation, resource distribution |
| `UTDRewardCalculator` | UObject | Reward calculation: win streak bonus, lose streak compensation, escalating loss damage |

## Key APIs

```
UTDResourceManager::CalculateRoundIncome() / CanAffordCost() / GrantRoundResources()
UTDRewardCalculator::CalculateRoundReward() / CalculateLoseDamage()
```

## Reward/Punishment Formulas

```
Win Gold    = Config.WinBonusGold + min(WinStreak x 5, 30)
Lose Damage = Config.LoseDamage + Round x 0.5
Lose Streak Compensation = min(LoseStreak x 5, 25)
```

- **Win streak bonus**: Capped at 30 gold (6+ consecutive wins)
- **Loss damage escalation**: Increases by 0.5 per round (later rounds hurt more)
- **Lose streak compensation**: Capped at 25 gold (5+ consecutive losses), helps losing players catch up

## Economy Flow

```
Round Start (Preparation Phase)
    |
    v
ResourceManager::GrantRoundResources(Player, Config)
    |
    +-- Gold += Config.GoldPerRound + BuildingIncome (reserved)
    +-- ResearchPoints += BaseResearchPointsPerRound
    |
    v
Player Actions: Build / Train / Research / Modify Terrain
    |
    +-- PlayerState.SpendGold(Cost)
    +-- PlayerState.SpendResearchPoints(Cost)
    |
    v
Round Settlement (Settlement Phase)
    |
    v
RewardCalculator::CalculateRoundReward(Result, Config, Round, WinStreak, LoseStreak)
    |
    +-- Win:  Gold += WinBonusGold + WinStreakBonus
    |         ResearchPoints += WinResearchPointBonus
    |
    +-- Lose: HP -= LoseDamage + Round x Escalation
              Gold += LoseStreakCompensation
```
