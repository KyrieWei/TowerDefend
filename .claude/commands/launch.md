---
description: Launch Unreal Editor with the TowerDefend project
allowed-tools: Bash
disable-model-invocation: true
---

Launch the Unreal Editor with the TowerDefend project in the background.

Run this command in the background (non-blocking):

```
"F:/UnrealEngine/Engine/Binaries/Win64/UnrealEditor.exe" "F:/TowerDefend/TowerDefend.uproject"
```

The editor process runs asynchronously — do not wait for it to exit. Report that the editor is launching.
