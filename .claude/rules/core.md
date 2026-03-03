---
paths:
  - "Source/TowerDefend/Core/**"
---

# Core Framework Rules

Core is the foundational layer. It manages match lifecycle, phase transitions, player state, and the strategy camera.

## Dependency Constraint

Core **must not** import any game-logic module (HexGrid, Building, Unit, Combat, Match, Economy, TechTree). It only provides shared types and framework classes consumed by other modules.

## Shared Types

| Type | File | Consumers |
|------|------|-----------|
| `ETDTechEra` | TDSharedTypes.h | Unit, TechTree |
| `ETDGamePhase` | TDGamePhaseTypes.h | Core, Match |
| `FTDRoundResult` | TDGamePhaseTypes.h | Core, Combat, Match, Economy |
| `FTDMatchConfig` | TDGamePhaseTypes.h | Core, Match, Economy |

## Classes

| Class | Base | Role |
|-------|------|------|
| `ATDGameMode` | AGameModeBase | Server-side match controller; drives phase transitions and timers |
| `ATDGameState` | AGameStateBase | Globally replicated state: phase, round, countdown, alive list |
| `ATDPlayerState` | APlayerState | Per-player persistent data: HP, gold, research points, win/loss stats |
| `ATDPlayerController` | APlayerController | Strategy camera input (WASD/edge/scroll/middle-click, Enhanced Input) |
| `ATDCameraPawn` | APawn | Camera carrier (Root -> SpringArm -> Camera) |

## Enums

- `ETDTechEra` (TDSharedTypes.h) — Tech era: Ancient, Classical, Medieval, Renaissance, Industrial, Modern
- `ETDGamePhase` (TDGamePhaseTypes.h) — Game phase: None -> Preparation -> Matchmaking -> Battle -> Settlement -> GameOver

## Structs

- `FTDRoundResult` (TDGamePhaseTypes.h) — Single round combat result (attacker/defender indices, win/loss, damage)
- `FTDMatchConfig` (TDGamePhaseTypes.h) — Match configuration (player count, rounds, gold, HP, timers)

## Key APIs

```
ATDGameMode::StartMatch() / EndMatch() / AdvanceToNextPhase()
ATDGameState::SetCurrentPhase() / EliminatePlayer() / GetPhaseRemainingTime()
ATDPlayerState::AddGold() / SpendGold() / SpendResearchPoints() / ApplyDamage()
ATDPlayerController::MoveCamera() / RotateCamera() / ZoomCamera() / GetHexCoordUnderCursor()
```

## Delegates

```
FTDOnPhaseChanged(OldPhase, NewPhase)          — ATDGameState
FTDOnRoundChanged(NewRound)                     — ATDGameState
FTDOnPlayerEliminated(EliminatedPlayer)         — ATDGameState
FTDOnHealthChanged(OldHealth, NewHealth)        — ATDPlayerState
FTDOnGoldChanged(OldGold, NewGold)              — ATDPlayerState
FTDOnPlayerDied(DeadPlayer)                     — ATDPlayerState
```

## Phase State Machine (Detailed)

```
              GameMode::StartMatch()
                      |
                      v
               +--------------+
          +--->| Preparation  | (build/train/research/terrain, 60s countdown)
          |    +------+-------+
          |           | OnPhaseTimeExpired
          |           v
          |    +--------------+
          |    | Matchmaking  | (MatchmakingManager generates pairings)
          |    +------+-------+
          |           | AdvanceToNextPhase
          |           v
          |    +--------------+
          |    |   Battle     | (CombatManager executes combat, 90s countdown)
          |    +------+-------+
          |           | OnPhaseTimeExpired / combat ended
          |           v
          |    +--------------+
          |    | Settlement   | (reward/punishment, elimination check, auto-save)
          |    +------+-------+
          |           |
          |     ShouldEndMatch?
          |    +------+------+
          |  No|             |Yes
          |    v             v
          +----+      +----------+
                      | GameOver |
                      +----------+
```

## Strategy Camera

- Replace third-person camera with top-down strategy camera
- Supports: WASD/edge panning, scroll zoom, middle-click rotation, minimap click-to-jump
- Camera constrained to player territory (except during battle phase when opponent territory is visible)
- Enhanced Input: `IA_CameraMove`, `IA_CameraRotate`, `IA_CameraZoom`, `IA_CameraFastMove`, `IMC_Strategy`
