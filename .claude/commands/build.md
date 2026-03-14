---
description: Generate project files and build TowerDefend (Editor, Development, Win64) using the local UE5 source engine
allowed-tools: Bash
disable-model-invocation: true
---

Use the local Unreal Engine at `F:/UnrealEngine` to generate project files and compile TowerDefend.

## Step 1: Generate Project Files

Run the following command to generate VS project files:

```
F:/UnrealEngine/Engine/Build/BatchFiles/GenerateProjectFiles.bat -project="F:/TowerDefend/TowerDefend.uproject" -game -engine
```

## Step 2: Build Editor Target (Development)

Run the following command to build the editor target:

```
F:/UnrealEngine/Engine/Build/BatchFiles/Build.bat TowerDefendEditor Win64 Development -project="F:/TowerDefend/TowerDefend.uproject" -waitmutex
```

Run both steps sequentially. If Step 1 fails, stop and report the error. If Step 2 fails, check the UBT log at `F:/UnrealEngine/Engine/Programs/UnrealBuildTool/Log.txt` for the actual compiler errors, report them, and attempt to fix any code issues. Then rebuild.

The build must finish with **zero errors and zero warnings** per project coding standards.
