---
description: Build TowerDefend then launch Unreal Editor (full workflow)
allowed-tools: Bash
disable-model-invocation: true
---

Full build-and-launch workflow using the local Unreal Engine at `F:/UnrealEngine`.

## Step 1: Generate Project Files

```
F:/UnrealEngine/Engine/Build/BatchFiles/GenerateProjectFiles.bat -project="F:/TowerDefend/TowerDefend.uproject" -game -engine
```

## Step 2: Build Editor Target (Development)

```
F:/UnrealEngine/Engine/Build/BatchFiles/Build.bat TowerDefendEditor Win64 Development -project="F:/TowerDefend/TowerDefend.uproject" -waitmutex
```

Run Step 1 and Step 2 sequentially. If Step 1 fails, stop and report the error. If Step 2 fails, check the UBT log at `F:/UnrealEngine/Engine/Programs/UnrealBuildTool/Log.txt` for the actual compiler errors, report them, and attempt to fix any code issues. Then rebuild.

The build must finish with **zero errors and zero warnings** per project coding standards.

## Step 3: Launch Editor

Only after Step 2 succeeds with zero errors, launch the editor in the background (non-blocking):

```
"F:/UnrealEngine/Engine/Binaries/Win64/UnrealEditor.exe" "F:/TowerDefend/TowerDefend.uproject"
```

Report that the editor is launching after the build succeeded.
