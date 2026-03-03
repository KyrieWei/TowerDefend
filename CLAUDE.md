# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository. Subsystem-specific rules are in `.claude/rules/` and are auto-injected when editing files in corresponding directories.

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

---

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

- **Core** never imports game-logic modules.
- **Cross-module references** in Combat/Building/Unit use forward declarations in headers, `#include` only in `.cpp`.
- **Economy, Match, TechTree** are isolated from each other and from HexGrid/Building/Unit/Combat.

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
| File names | `TD` prefix | `TDGameMode.h`, `TDHexTile.cpp` |

### Key Entry Points

- **GameMode**: `Core/TDGameMode.h` — match lifecycle, phase state machine
- **GameState**: `Core/TDGameState.h` — globally replicated state, player elimination
- **PlayerState**: `Core/TDPlayerState.h` — per-player gold, HP, research points
- **Grid**: `HexGrid/TDHexGridManager.h` — hex grid generation, TMap O(1) tile lookup
- **Coordinates**: `HexGrid/TDHexCoord.h` — cube coordinate math (Q,R,S), distance, world<->hex conversion
- **Pathfinding**: `HexGrid/TDHexPathfinding.h` — A* + Dijkstra reachable range
- **Buildings**: `Building/TDBuildingManager.h` — 6-step placement validation, economy integration
- **Combat**: `Combat/TDCombatManager.h` — combat state machine (Deploying->InProgress->Finished)

### Cross-Module Shared Types

| Type | Defined In | Used By |
|------|-----------|---------|
| `ETDTechEra` | Core/TDSharedTypes.h | Unit, TechTree |
| `ETDGamePhase` | Core/TDGamePhaseTypes.h | Core, Match |
| `FTDRoundResult` | Core/TDGamePhaseTypes.h | Core, Combat, Match, Economy |
| `FTDMatchConfig` | Core/TDGamePhaseTypes.h | Core, Match, Economy |
| `FTDHexCoord` | HexGrid/TDHexCoord.h | HexGrid, Building, Unit, Combat |
| `ETDTerrainType` | HexGrid/TDHexGridSaveData.h | HexGrid, Building |
| `ETDBuildingType` | Building/TDBuildingDataAsset.h | Building, Combat (forward ref) |
| `ETDUnitType` | Unit/TDUnitDataAsset.h | Unit, Combat (forward ref) |

---

## Coding Standards

These rules are **mandatory** and enforced on all code changes.

### Code Style

- **Naming**: PascalCase everywhere (classes, functions, local variables), following UE naming conventions
- **Semantic naming**: Function parameters must reflect their business role, not abbreviated type names
- **Braces**: Allman style (opening brace on its own line)
- **Indentation**: 4 spaces, no tabs
- **File encoding**: UTF-8

### Size Limits

- **Functions**: Maximum 50 lines
- **Classes**: Maximum 500 lines
- **Cyclomatic complexity**: Maximum 10 per function

### Single Responsibility

- Each function/class does one thing only
- High cohesion within modules, low coupling between modules
- Interfaces should be small and focused

### Comment Rules

- **Comment indentation** must match the code below
- **Class comments**: Overall functionality overview, placed in `.h` file only (not in `.cpp`)
- **Function header comments**: Required for complex functions or when the name doesn't convey meaning; placed in `.h` file only
- **Inline comments**: Required for complex logic inside function bodies
- **UPROPERTY comments**: Every `UPROPERTY` attribute must have a usage description comment

### Error Handling

- Handle all possible exceptions
- Use RAII for non-UObject resources; UObject resources follow UE GC lifecycle
- All pointers must be validity-checked before use (`check`/`ensure` macro system)
- All pointers must have verified lifetimes — no dangling pointers

### Zero Warnings Policy

All code must compile with **zero errors and zero warnings**.

### UE Memory Safety

- **Weak references**: Prefer `TWeakObjectPtr` / `TSoftObjectPtr` to avoid dangling references
- **GC safety**: All `UObject*` must be `UPROPERTY`-marked or manually `AddToRoot`'d to prevent GC collection
- **Lifetime checks**: Cross-frame Actor references must use `IsValid()` checks
- **RAII**: Non-UObject resources use RAII; UObject resources follow UE GC lifecycle

### Input Validation

- All external input must be strictly validated
- All pointers checked before use (`check`/`ensure`)
- Numeric parameters must have range checks (e.g., FOV, BlendWeight) to prevent illegal value propagation

### Performance Requirements

- **Memory**: Zero tolerance for leaks; use object pools for frequent small allocations; cache-friendly data layout
- **Algorithms**: Core algorithms must be optimal or near-optimal complexity
- **Threading**: Thread-safe data access in multi-threaded contexts; fine-grained locks; async I/O
- **UE Profiling**: Use `SCOPE_CYCLE_COUNTER` / `SCOPED_NAMED_EVENT` for critical paths
- **Hot path budget**: Critical evaluation chains must complete within 0.5ms

### Architecture Principles

- **SOLID**: Strictly follow Single Responsibility, Open-Closed, etc.
- **Dependency injection** to reduce coupling
- **Interface isolation**: Interfaces small and specialized
- **Data range**: All variables must have defined data ranges
- **Edge cases**: Consider all edge cases for user inputs
- **Multi-level validation**: Distinguish shipping checks, compile-time checks, runtime checks — each with complete log output

### Testing & Verification

- **Compile verification**: All changes must compile on target platform with zero errors, zero warnings
- **Logic verification**: Modules that can't be directly run must be verified through code review + simulated input analysis
- **Incremental verification**: Iterate on compiler feedback until zero errors, zero warnings
- **Boundary testing**: Must include boundary condition tests
- **Simulated testing**: After implementation, simulate inputs and analyze execution flow — only deliver when logic is verified correct
- **Error correction**: If tests fail, summarize and analyze root cause, fix, then re-test

---

## AI Collaboration Rules (Mandatory for All Code Modifications)

These rules are extracted from engineering practice and are a **mandatory executable checklist** for AI assistants performing code modifications.

### 8.1 "Search Before Modify"

Any batch modification must be preceded by:
1. Grep/search all files containing the target string
2. Classify results by semantic type (type name ref vs variable ref vs comment ref)
3. Determine per-category strategy (modify / keep / special handling)
4. Choose a safe replacement strategy (e.g., Placeholder isolation)

**Prevents**: Type names being accidentally modified, comments being corrupted, cross-semantic boundary replacement side effects.

### 8.2 "Signature Is Truth"

When fixing variable references inside a function body:
1. **Read the signature**: Confirm the function's complete signature (parameter types and names)
2. **Search references**: Find all references to the old variable name in the function body
3. **Replace individually**: Confirm replacement target against signature parameter names, then execute

**Three-level constraint chain**: `TypeName (e.g., FXxxContext) → ParameterName (e.g., XxxContext) → BodyReference (XxxContext)`

**Prevents**: Parameter name confusion, cross-function naming inertia causing wrong replacements.

### 8.3 "Full Scan"

When a pattern error is discovered:
1. Determine if it's a **systematic pattern** (do similar files/functions have the same issue?)
2. If systematic, immediately full-scan the entire codebase
3. Build a complete modification list, fix all instances at once
4. After fixing, full-scan again to confirm no omissions

**Prevents**: Missing similar files, multiple round-trips (passive compiler-driven vs proactive sweep).

### 8.4 "Classified Replacement"

Different replacement targets must be processed separately. Never use one-shot global replace.

**Placeholder technique example**:
```
Step 1: FVisionerViewContext → __PLACEHOLDER_TYPE__  (protect type name)
Step 2: ViewContext → EvaluateContext                 (replace variable name)
Step 3: __PLACEHOLDER_TYPE__ → FVisionerViewContext   (restore type name)
```

**Prevents**: Cross-function mis-replacement, type name / variable name confusion.

### 8.5 "Post-Replace Verification"

After any batch replacement, verify:
1. Search confirms all target replacements are complete (no omissions)
2. Search confirms type names / comments that shouldn't change are intact
3. Search confirms no new compilation error patterns
4. Per-file check that function body variable names match function signatures

### 8.6 "Type Is Contract"

In type-layered frameworks, the context type itself is the stage contract. Variable naming must align with the type's contract semantics.

- Function parameter names must reflect the pipeline stage
- Different stages must not use the same generic parameter name
- Base type parameters may use generic names (e.g., `Context`); derived types must use specific stage names

### 8.7 "Error-Driven Cognitive Convergence"

Use compilation error repair to reverse-engineer the framework's design space:

| Stage | Characteristic | Behavior |
|-------|---------------|----------|
| Stage 1: Mechanical Fix | Only knows "change A to B" | Passively responds to compiler errors |
| Stage 2: Pattern Recognition | Identifies cross-file patterns | Can infer modification targets from function signatures |
| Stage 3: Architectural Insight | Understands the "why" behind design decisions | Can predict new module structures |

**Self-check**: After each fix, ask: which stage am I at? If still at Stage 1, proactively search more code to understand the full picture. Goal: reach at least Stage 2 by end of each task.

### 8.8 "Predict-Verify" Self-Driven Learning Loop

```
Encounter new module → Predict based on known model (what interfaces/data flows/stages should it have?)
    → Read actual code to verify
    → Match → Expand trust region
    → Mismatch → Locate deviation (which assumption was falsified?) → Correct model
    → Check if it reveals deeper design patterns
    → Update mental model → Return to start
```

**Key belief**: Mismatches are more valuable than matches — errors are learning fuel, not cost.

### 8.9 "Three Questions Before Editing"

Before every code modification, mandatory answers to:

1. What is the **complete scope** of this problem? (Not just current file — all related files)
2. What is the **correct target value** for each modification point? (Not from memory — read the signature/definition)
3. Does my modification strategy have **side effects**? (Will type names/comments/other modules be accidentally modified?)

**Only begin modification after all three questions can be clearly answered.**

---

## Cheat Manager Console Commands (non-Shipping only)

Access via `~` key in-editor:

```
TD.Map.Save / TD.Map.Load / TD.Map.ExportJson / TD.Map.ImportJson / TD.Map.Generate
TD.SetGold <Amount> / TD.AddGold <Amount>
TD.SetResearch <Amount> / TD.AddResearch <Amount>
TD.Phase.Next / TD.Phase.Set <PhaseName>
TD.StartMatch / TD.EndMatch
TD.SetHealth <Amount> / TD.God
```

---

## AgentIntegrationKit Plugin

Editor-only plugin providing AI agent integration via Agent Client Protocol (ACP). MCP server on port 9315. Includes tools for file operations, Blueprint editing, asset editing, and animation. WebUI built with SvelteKit + TailwindCSS (uses Bun runtime — Win64 binary excluded from git due to size).

---

## Content Structure

Blueprints mirror C++ classes at `Content/TowerDefend/Blueprints/{Core,Building,Combat,HexGrid,Unit}/`. Data assets at `Content/TowerDefend/DataAssets/{Buildings,TechTree,Units}/`. Input mappings at `Content/TowerDefend/Input/` (Enhanced Input: `IA_CameraMove`, `IA_CameraRotate`, `IA_CameraZoom`, `IA_CameraFastMove`, `IMC_Strategy`).

---

## Multi-Agent Worktree Collaboration

### Worktree Branch Structure

The project uses Git Worktree for parallel multi-agent development. Each agent works on an isolated branch:

| Worktree Path | Branch | Responsible Module |
|---------------|--------|-------------------|
| `.claude/worktrees/terrain-system` | `feature/terrain-system` | TDHexTile, TDHexGridManager, TDTerrainGenerator, TDTerrainModifier, TDHexGridSaveData |
| `.claude/worktrees/strategy-camera` | `feature/strategy-camera` | TDPlayerController, TDCameraPawn, Input Actions |
| `.claude/worktrees/core-framework` | `feature/core-framework` | TDGameMode, TDGameState, TDPlayerState, TDGamePhaseTypes |

### Task File Convention

Each worktree root has a `TASK.md` file containing:
- Task objectives and branch info
- File list and interface definitions to create
- Boundary agreements with other modules
- Verification methods
- **Progress tracking records** (see below)

Agents **must read** `TASK.md` and `DeveloperRules.md` before starting work.

### Progress Tracking

Each agent **must** maintain a `## Progress Log` section at the end of their `TASK.md`:

```markdown
## Progress Log

| Time | File | Status | Notes |
|------|------|--------|-------|
| 2026-02-28 10:00 | TDHexTile.h/cpp | Completed | Contains ETDTerrainType enum |
| 2026-02-28 10:30 | TDHexGridManager.h/cpp | In Progress | GenerateGrid done, ApplySaveData pending |
```

Status values: `Not Started`, `In Progress`, `Completed`, `Blocked` (note reason)

Update timing: after each file creation/completion, on encountering blockers, on final completion.

### View Global Progress

```bash
for wt in .claude/worktrees/*/; do echo "=== $(basename $wt) ==="; grep -A 100 "## Progress" "$wt/TASK.md" 2>/dev/null || echo "(no progress yet)"; echo; done
```

### Merge Order

After all agents finish, merge back to main in dependency order:
1. Merge independent modules first: `feature/core-framework`, `feature/strategy-camera`
2. Merge dependency-heavy modules last: `feature/terrain-system`
3. Resolve conflicts (mainly in Build.cs), then clean up worktrees

---

## Reference Documents

The following documents are preserved as complete reference archives (not auto-loaded by Claude Code):

- `Architecture.md` — Full architecture documentation (705 lines, module details, API, data flow diagrams)
- `GameDesign.md` — Game design and requirements (677 lines, terrain system, tech tree, economy formulas)
- `DeveloperRules.md` — Developer standards (262 lines, coding standards, AI collaboration methodology)
