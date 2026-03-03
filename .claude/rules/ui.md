---
paths:
  - "Source/TowerDefend/UI/**"
---

# UI System Rules

UI system uses UE5 UMG (Unreal Motion Graphics). Key widgets have C++ logic with Blueprint layouts.

## UI Modules

| Module | Content |
|--------|---------|
| Main Menu | Matchmaking, settings, leaderboard |
| HUD | Resource display, round info, minimap |
| Build Panel | Building list, drag-drop placement |
| Army Panel | Unit type list, squad management |
| Tech Tree UI | Full-screen tree view, research/inspect |
| Battle UI | Attack/defense view toggle, battle status |
| Settlement UI | Round results, reward/punishment details |
| Leaderboard | Final rankings, MVP stats |
| Terrain Edit Panel | Terrain raise/lower controls, cost preview, modifiable range highlight |

## Design Guidelines

- Use UE5 UMG (C++ + Blueprint hybrid)
- Key Widget logic in C++, layout in Blueprint
- Support 16:9 and 21:9 widescreen adaptation
- Follow the strategy game reference style (Civilization 6 inspiration)

## Module Dependencies (Future)

When UI module is implemented, `TowerDefend.Build.cs` will need:
```
"UMG", "Slate", "SlateCore"
```

## Content Structure

Blueprints mirror C++ classes at `Content/TowerDefend/Blueprints/`. Data assets at `Content/TowerDefend/DataAssets/`. Input mappings at `Content/TowerDefend/Input/`.

## Art Style Reference

- Low-poly + vibrant color palette (Civilization 6 reference)
- Hex tiles with height variation (hills raised, water sunken), smooth transition animations on height change
- Stylized unit models, not photorealistic
- Building appearance changes with tech era (thatch hut -> stone castle -> modern bunker)
