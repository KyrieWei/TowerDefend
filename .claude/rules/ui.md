---
paths:
  - "Source/TowerDefend/UI/**"
  - "Content/Script/UI/**"
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

## Lua Script Naming Convention

- UnLua widget script 的模块变量必须使用与文件名相同的名称，禁止使用 `M` 等缩写。例如 `WBP_MapEditor.lua` 中应写 `local WBP_MapEditor = UnLua.Class()`，函数声明为 `function WBP_MapEditor:Construct()`，文件末尾 `return WBP_MapEditor`。

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

## Lua Script Directory Structure

```
Content/Script/UI/
├── Common/                 -- 通用工具与常量
│   ├── TDUIDefines.lua     -- UI 常量定义（Layer、State、AnimationType、BindingMode）
│   └── TDUIUtils.lua       -- UI 通用工具（SafeCall、Widget 操作、类型转换、日志）
├── Framework/              -- MVVM 框架
│   ├── TDBaseModel.lua
│   ├── TDBaseView.lua
│   ├── TDBaseViewModel.lua
│   ├── TDDataBinding.lua
│   ├── TDEventSystem.lua
│   └── TDUIManager.lua
├── Helper/                 -- 游戏数据访问工具（封装 UTDBlueprintLibrary）
│   ├── TDCoreAccessor.lua         -- 核心对象（GameState/GameMode/PlayerState/PlayerController）
│   ├── TDPhaseAccessor.lua        -- 游戏阶段与回合（阶段查询/回合数/倒计时）
│   ├── TDLocalPlayerAccessor.lua  -- 本地玩家数据（金币/血量/科研/存活/胜负）
│   ├── TDMatchAccessor.lua        -- 对局信息（配置/玩家列表/存活人数）
│   └── TDMapAccessor.lua          -- 地图管理（GridManager/存取/重生成）
├── UMG/                    -- UnLua Widget Blueprint 绑定脚本
│   └── WBP_MapEditor.lua
└── Examples/               -- 示例代码
    ├── TDTestModel.lua
    ├── TDTestView.lua
    └── TDTestViewModel.lua
```

## Content Structure

Blueprints mirror C++ classes at `Content/TowerDefend/Blueprints/`. Data assets at `Content/TowerDefend/DataAssets/`. Input mappings at `Content/TowerDefend/Input/`.

## Art Style Reference

- Low-poly + vibrant color palette (Civilization 6 reference)
- Hex tiles with height variation (hills raised, water sunken), smooth transition animations on height change
- Stylized unit models, not photorealistic
- Building appearance changes with tech era (thatch hut -> stone castle -> modern bunker)
