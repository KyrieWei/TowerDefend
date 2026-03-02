# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TowerDefend is an Unreal Engine 5.5 multiplayer PVP tower defense strategy game (8-player elimination tournament). Built with C++ using UE5 reflection. Server-authoritative networking (Dedicated/Listen Server with Replication).

- Engine: UE 5.5 (source build, `IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_5`)
- Single runtime module: `TowerDefend`
- Project file: `TowerDefend.uproject`
- Default map: `/Game/TowerDefend/Maps/L_Tower_Defend_Main`
- Rendering: DX12 SM6, Lumen GI, Virtual Shadow Maps

## Build Commands

This is a standard UBT (Unreal Build Tool) project. No CMake/Makefile/npm at root.
Engine location: `F:/UnrealEngine`

**Generate project files:**
```
F:/UnrealEngine/Engine/Build/BatchFiles/GenerateProjectFiles.bat -project="F:/TowerDefend/TowerDefend.uproject" -game -engine
```

**Build editor target (Development):**
```
F:/UnrealEngine/Engine/Build/BatchFiles/Build.bat TowerDefendEditor Win64 Development -project="F:/TowerDefend/TowerDefend.uproject" -waitmutex
```

**Build game target (Development):**
```
F:/UnrealEngine/Engine/Build/BatchFiles/Build.bat TowerDefend Win64 Development -project="F:/TowerDefend/TowerDefend.uproject" -waitmutex
```

**Launch editor:**
```
"F:/UnrealEngine/Engine/Binaries/Win64/UnrealEditor.exe" "F:/TowerDefend/TowerDefend.uproject"
```

**Shortcuts:**
- `/build` — generate project files and compile (Development)
- `/launch` — launch Unreal Editor with the project
- `/build-and-launch` — build then launch in one step

**Module dependencies** (`TowerDefend.Build.cs`):
`Core`, `CoreUObject`, `Engine`, `InputCore`, `EnhancedInput`, `Json`, `JsonUtilities`

Include paths use `ModuleDirectory` (flat includes — no subdirectory prefixes needed in `#include`).

## Architecture

### Module Layout

All game C++ lives in `Source/TowerDefend/` organized into subsystem directories:

```
Source/TowerDefend/
├── Core/          Game framework: GameMode, GameState, PlayerState, PlayerController, CameraPawn, CheatManager
├── HexGrid/       Hex grid: cube coordinates (Q,R,S), tile actors, grid manager, A* pathfinding, terrain generation/modification
├── Building/      Building system: data assets, base building, defense tower, wall, placement manager
├── Unit/          Army units: data assets, base unit, squad manager, AI controller
├── Combat/        Combat: damage calculator, projectiles, combat state machine
├── Match/         Match management: multi-round match, round manager, matchmaking (Random/Swiss/Serpentine)
├── Economy/       Economy: round income, win/loss rewards with streak bonuses
└── TechTree/      Tech tree: research nodes, era progression, bonus aggregation
```

### Dependency Rules

```
Core  (no game-logic dependencies)
  └── HexGrid  (depends on Core shared types only)
        ├── Building  (depends on HexGrid for coords/tiles)
        ├── Unit      (depends on HexGrid for coords/tiles)
        └── Combat    (depends on HexGrid; forward-declares Building/Unit types)
Economy   (depends on Core only)
Match     (depends on Core only)
TechTree  (depends on Core only)
```

Core never imports game logic modules. Cross-module references in Combat/Building/Unit use forward declarations.

### Game Phase State Machine

```
StartMatch → Preparation (60s) → Matchmaking → Battle (90s) → Settlement → [loop or GameOver]
```

Phases: `None → Preparation → Matchmaking → Battle → Settlement → GameOver`

Key flow: build/train/research during Preparation → pair players → execute combat → apply rewards/damage/elimination → repeat until 1 player remains.

### Naming Conventions

| Category | Prefix | Example |
|----------|--------|---------|
| Actor classes | `ATD` | `ATDGameMode`, `ATDHexTile` |
| UObject classes | `UTD` | `UTDBuildingManager` |
| Structs | `FTD` | `FTDHexCoord`, `FTDMatchConfig` |
| Enums | `ETD` | `ETDTerrainType`, `ETDGamePhase` |
| Interfaces | `ITD` | `ITDDamageable` |
| Delegates | `FTDOn` | `FTDOnPhaseChanged` |
| Blueprint categories | `"TD\|Module"` | `"TD\|Match"` |

### Key Entry Points

- **GameMode**: `Core/TDGameMode.h` — match lifecycle, phase state machine
- **GameState**: `Core/TDGameState.h` — globally replicated state, player elimination
- **PlayerState**: `Core/TDPlayerState.h` — per-player gold, HP, research points
- **Grid**: `HexGrid/TDHexGridManager.h` — hex grid generation, TMap O(1) tile lookup
- **Coordinates**: `HexGrid/TDHexCoord.h` — cube coordinate math (Q,R,S), distance, world↔hex conversion
- **Pathfinding**: `HexGrid/TDHexPathfinding.h` — A* + Dijkstra reachable range
- **Buildings**: `Building/TDBuildingManager.h` — 6-step placement validation, economy integration
- **Combat**: `Combat/TDCombatManager.h` — combat state machine (Deploying→InProgress→Finished)

## Coding Standards (from DeveloperRules.md)

These rules are **mandatory** and enforced on all code changes:

- **Style**: PascalCase everywhere, Allman braces, 4-space indent (no tabs), UTF-8
- **Size limits**: Functions ≤50 lines, classes ≤500 lines, cyclomatic complexity ≤10
- **Comments in headers only**: Class-level and function-header comments go in `.h`, not `.cpp`. All `UPROPERTY` attributes must have a comment.
- **Zero warnings policy**: All code must compile with zero errors and zero warnings.
- **Pointer safety**: `TWeakObjectPtr`/`TSoftObjectPtr` for cross-frame refs; all `UObject*` must be `UPROPERTY`-marked or `AddToRoot`'d; check `IsValid()` for cross-frame Actor refs.

### AI Collaboration Rules (Section 8 of DeveloperRules.md)

These are **mandatory** when making code modifications:

1. **"Search before modify"** — grep all affected files before batch changes; classify matches by semantic type (type refs vs variable refs vs comments) before replacing.
2. **"Signature is truth"** — always read the actual function signature before modifying a function body; never rely on memory.
3. **"Full scan"** — when a pattern error is found, search the entire codebase for all instances before fixing.
4. **"Classified replacement"** — never use one-shot global replace; process each semantic category separately. Use Placeholder technique to protect type names during variable renames.
5. **"Post-replace verification"** — after any batch replace, grep to confirm no missed instances and no collateral damage.
6. **"Three questions before editing"**: (1) What is the full scope? (2) What is the correct target value per the signature/definition? (3) Does my strategy have side effects?

## Cheat Manager Console Commands (non-Shipping only)

Access via `~` key in-editor:

```
TD.Map.Save/Load/ExportJson/ImportJson/Generate
TD.SetGold/AddGold/SetResearch/AddResearch <Amount>
TD.Phase.Next / TD.Phase.Set <PhaseName>
TD.StartMatch / TD.EndMatch
TD.SetHealth <Amount> / TD.God
```

## AgentIntegrationKit Plugin

Editor-only plugin providing AI agent integration via Agent Client Protocol (ACP). MCP server on port 9315. Includes tools for file operations, Blueprint editing, asset editing, and animation. WebUI built with SvelteKit + TailwindCSS (uses Bun runtime — Win64 binary excluded from git due to size).

## Content Structure

Blueprints mirror C++ classes at `Content/TowerDefend/Blueprints/{Core,Building,Combat,HexGrid,Unit}/`. Data assets at `Content/TowerDefend/DataAssets/{Buildings,TechTree,Units}/`. Input mappings at `Content/TowerDefend/Input/` (Enhanced Input: `IA_CameraMove`, `IA_CameraRotate`, `IA_CameraZoom`, `IA_CameraFastMove`, `IMC_Strategy`).
