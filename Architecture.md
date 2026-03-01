# TowerDefend — 项目架构文档

> 自动生成于 2026-02-28 | 源码总行数 ~12,363 | 73 源文件 + 1 Build.cs

---

## 1. 项目概览

| 项目 | 值 |
|------|----|
| 引擎 | Unreal Engine 5 |
| 语言 | C++ (UE5 Reflection) |
| 类型 | 多人 PVP 塔防策略 (8人淘汰赛) |
| 模块数 | 9 (Core / HexGrid / Building / Unit / Combat / Match / Economy / TechTree / Legacy) |
| 文件总数 | 74 (37 .h + 35 .cpp + 1 Build.cs + 1 header-only) |
| 编码规范 | TD 前缀、Allman 大括号、4 空格缩进、函数 ≤50 行、类 ≤500 行 |
| 网络模型 | 服务端权威 (Dedicated/Listen Server)，客户端通过 Replication 同步 |

---

## 2. 目录结构

```
Source/TowerDefend/
├── TowerDefend.Build.cs           # UBT 构建规则
├── TowerDefend.h / .cpp           # 模块入口
├── TowerDefendGameMode.h / .cpp   # Legacy 游戏模式（过渡用）
├── TowerDefendCharacter.h / .cpp  # Legacy 角色（引擎模板残留）
│
├── Core/                          # 核心框架层
│   ├── TDSharedTypes.h            # 全模块共享枚举 (ETDTechEra)
│   ├── TDGamePhaseTypes.h         # 阶段/回合/配置数据类型
│   ├── TDGameMode.h / .cpp        # 对局生命周期管理
│   ├── TDGameState.h / .cpp       # 全局同步状态
│   ├── TDPlayerState.h / .cpp     # 玩家持久状态
│   ├── TDPlayerController.h / .cpp# 策略相机输入控制
│   └── TDCameraPawn.h / .cpp      # 策略相机 Pawn
│
├── HexGrid/                       # 六边形网格系统
│   ├── TDHexCoord.h / .cpp        # 立方坐标结构体
│   ├── TDHexGridSaveData.h / .cpp # 地形枚举 + 序列化数据
│   ├── TDHexTile.h / .cpp         # 六边形格子 Actor
│   ├── TDHexGridManager.h / .cpp  # 网格管理器
│   ├── TDHexPathfinding.h / .cpp  # A* 寻路
│   ├── TDTerrainGenerator.h / .cpp# Perlin Noise 地形生成
│   └── TDTerrainModifier.h / .cpp # 运行时地形修改
│
├── Building/                      # 建筑系统
│   ├── TDBuildingDataAsset.h / .cpp # 建筑静态数据
│   ├── TDBuildingBase.h / .cpp    # 建筑基类 Actor
│   ├── TDDefenseTower.h / .cpp    # 防御塔（自动攻击）
│   ├── TDWall.h / .cpp            # 城墙（阻挡减速）
│   └── TDBuildingManager.h / .cpp # 建筑管理器
│
├── Unit/                          # 军队单位系统
│   ├── TDUnitDataAsset.h / .cpp   # 单位静态数据
│   ├── TDUnitBase.h / .cpp        # 单位基类 Actor
│   ├── TDUnitSquad.h / .cpp       # 编队管理器
│   └── TDUnitAIController.h / .cpp# 单位 AI 控制器
│
├── Combat/                        # 战斗系统
│   ├── TDDamageCalculator.h / .cpp# 伤害计算器
│   ├── TDProjectileBase.h / .cpp  # 投射物基类
│   └── TDCombatManager.h / .cpp   # 战斗流程管理器
│
├── Match/                         # 对局管理
│   ├── TDMatchManager.h / .cpp    # 对局宏观管理
│   ├── TDRoundManager.h / .cpp    # 单回合管理
│   └── TDMatchmakingManager.h / .cpp # 配对算法
│
├── Economy/                       # 经济系统
│   ├── TDResourceManager.h / .cpp # 资源收入管理
│   └── TDRewardCalculator.h / .cpp# 回合奖惩计算
│
└── TechTree/                      # 科技树系统
    ├── TDTechTreeDataAsset.h / .cpp # 科技树数据资产
    ├── TDTechNode.h / .cpp        # 科技节点运行时状态
    └── TDTechTreeManager.h / .cpp # 科技树管理器
```

---

## 3. 模块依赖关系

```
                    ┌─────────────────┐
                    │   Core 框架层    │
                    │ GameMode/State  │
                    │ PlayerState     │
                    │ SharedTypes     │
                    └───────┬─────────┘
                            │
              ┌─────────────┼─────────────┐
              │             │             │
              ▼             ▼             ▼
        ┌──────────┐ ┌──────────┐ ┌──────────────┐
        │ HexGrid  │ │ Economy  │ │   Match      │
        │ 地形/寻路 │ │ 资源/奖惩│ │ 对局/配对/回合│
        └────┬─────┘ └──────────┘ └──────────────┘
             │
      ┌──────┼──────┐
      │      │      │
      ▼      ▼      ▼
 ┌────────┐┌────────┐┌──────────┐
 │Building││ Unit   ││ TechTree │
 │ 建筑   ││ 单位   ││ 科技树   │
 └───┬────┘└───┬────┘└──────────┘
     │         │
     └────┬────┘
          ▼
    ┌──────────┐
    │ Combat   │
    │ 战斗管理  │
    └──────────┘
```

**依赖规则：**
- **Core** 不依赖任何游戏逻辑模块
- **HexGrid** 仅依赖 Core（共享类型）
- **Building / Unit** 依赖 HexGrid（坐标和格子查询）
- **Combat** 依赖 HexGrid + Building（前向引用）+ Unit（前向引用）
- **Economy** 仅依赖 Core（PlayerState + GamePhaseTypes），不引用 HexGrid
- **Match** 仅依赖 Core（PlayerState + GamePhaseTypes）
- **TechTree** 依赖 Core（SharedTypes + PlayerState）

---

## 4. 模块详细说明

### 4.1 Core — 核心框架层

**职责：** 对局生命周期管理、阶段流转、玩家状态存储、策略相机控制。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDTechEra` | TDSharedTypes.h | 科技时代 (Ancient ~ Modern)，全模块共享 |
| `ETDGamePhase` | TDGamePhaseTypes.h | 游戏阶段 (None → Preparation → Matchmaking → Battle → Settlement → GameOver) |

#### 结构体
| 结构体 | 文件 | 说明 |
|--------|------|------|
| `FTDRoundResult` | TDGamePhaseTypes.h | 单回合战斗结果（攻守方索引、胜负、伤害） |
| `FTDMatchConfig` | TDGamePhaseTypes.h | 对局配置（玩家数、回合数、金币、血量、时间） |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `ATDGameMode` | AGameModeBase | 服务端对局控制器，驱动阶段流转和计时器 |
| `ATDGameState` | AGameStateBase | 全局同步状态（阶段、回合、倒计时、存活列表） |
| `ATDPlayerState` | APlayerState | 玩家持久数据（血量、金币、科研点、胜负统计） |
| `ATDPlayerController` | APlayerController | 策略相机输入（WASD/边缘/滚轮/中键，Enhanced Input） |
| `ATDCameraPawn` | APawn | 相机载体（Root → SpringArm → Camera） |

#### 关键接口
```
ATDGameMode::StartMatch() / EndMatch() / AdvanceToNextPhase()
ATDGameState::SetCurrentPhase() / EliminatePlayer() / GetPhaseRemainingTime()
ATDPlayerState::AddGold() / SpendGold() / SpendResearchPoints() / ApplyDamage()
ATDPlayerController::MoveCamera() / RotateCamera() / ZoomCamera() / GetHexCoordUnderCursor()
```

#### 委托
```
FTDOnPhaseChanged(OldPhase, NewPhase)          — ATDGameState
FTDOnRoundChanged(NewRound)                     — ATDGameState
FTDOnPlayerEliminated(EliminatedPlayer)         — ATDGameState
FTDOnHealthChanged(OldHealth, NewHealth)        — ATDPlayerState
FTDOnGoldChanged(OldGold, NewGold)              — ATDPlayerState
FTDOnPlayerDied(DeadPlayer)                     — ATDPlayerState
```

---

### 4.2 HexGrid — 六边形网格系统

**职责：** 坐标运算、地形生成、格子管理、A* 寻路、地形修改、存档序列化。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDTerrainType` | TDHexGridSaveData.h | 地形类型 (Plain / Hill / Mountain / Forest / River / Swamp / DeepWater) |

#### 结构体
| 结构体 | 文件 | 说明 |
|--------|------|------|
| `FTDHexCoord` | TDHexCoord.h | 立方坐标 (Q,R,S)，支持距离/邻居/范围/世界坐标转换，可作 TMap 键 |
| `FTDHexTileSaveData` | TDHexGridSaveData.h | 单格子序列化数据 |
| `FTDHexGridSaveData` | TDHexGridSaveData.h | 完整地图序列化数据 |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `ATDHexTile` | AActor | 六边形格子实体，持有地形/高度/归属，驱动 Mesh 视觉 |
| `ATDHexGridManager` | AActor | 网格管理器，TMap O(1) 查询，生成/清除/存档 |
| `UTDTerrainGenerator` | UObject | Perlin Noise 程序化地形生成（对称、基地平坦化、高度平滑） |
| `UTDTerrainModifier` | UObject | 运行时地形修改（升降高度±1，合法性验证） |
| `UTDHexPathfinding` | UObject | A* 寻路 + Dijkstra 可达范围，含高度差代价修正 |
| `UTDHexGridSaveGame` | USaveGame | 本地存档 + JSON 导入导出 |

#### 关键接口
```
FTDHexCoord::DistanceTo() / GetAllNeighbors() / ToWorldPosition() / FromWorldPosition()
ATDHexTile::GetMovementCost() / GetDefenseBonus() / IsPassable() / IsBuildable()
ATDHexGridManager::GetTileAt() / GetNeighborTiles() / GetTilesInRange() / GenerateGrid()
UTDHexPathfinding::FindPath() / FindPathFiltered() / GetReachableTiles() / CalculatePathCost()
UTDTerrainModifier::RaiseTerrain() / LowerTerrain() / ValidateModification()
UTDTerrainGenerator::GenerateMap()
```

---

### 4.3 Building — 建筑系统

**职责：** 建筑数据定义、放置验证、攻击/防御逻辑、经济产出、管理器。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDBuildingType` | TDBuildingDataAsset.h | 建筑类型 (Base / ArrowTower / CannonTower / Wall / ResourceBuilding / Barracks / Trap) |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `UTDBuildingDataAsset` | UDataAsset | 建筑静态数据（费用、血量、攻击、产出、地形限制） |
| `ATDBuildingBase` | AActor | 建筑基类（初始化、升级、受伤、虚函数攻击接口） |
| `ATDDefenseTower` | ATDBuildingBase | 防御塔（FTimerHandle 定时攻击，高度射程加成） |
| `ATDWall` | ATDBuildingBase | 城墙（移动消耗倍率 10x，可阻挡通行） |
| `UTDBuildingManager` | UObject | 建筑管理（TMap 映射，6 步放置验证，经济汇总） |

#### 关键接口
```
UTDBuildingDataAsset::CanBuildOnTerrain() / GetEffectiveAttackRange() / GetUpgradeCost()
ATDBuildingBase::InitializeBuilding() / ApplyDamage() / Upgrade() / CanAttack() / GetAttackRange()
ATDDefenseTower::StartAutoAttack() / StopAutoAttack()
ATDWall::GetMovementCostMultiplier() / DoesBlockPassage()
UTDBuildingManager::PlaceBuilding() / CanPlaceBuilding() / RemoveBuilding() / CalculateTotalGoldIncome()
```

#### 委托
```
FTDOnBuildingDestroyed(DestroyedBuilding)
FTDOnBuildingDamaged(DamagedBuilding, DamageAmount, RemainingHealth)
```

---

### 4.4 Unit — 军队单位系统

**职责：** 单位数据定义、编队管理、移动/战斗逻辑、AI 行为决策。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDUnitType` | TDUnitDataAsset.h | 兵种类型 (Melee / Ranged / Cavalry / Siege / Special) |
| `ETDAIActionResult` | TDUnitAIController.h | AI 行为结果 (None / Moved / Attacked / Idle) |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `UTDUnitDataAsset` | UDataAsset | 单位静态数据（费用、属性、克制倍率、科技时代门槛） |
| `ATDUnitBase` | AActor | 单位基类（血量、坐标、移动点、伤害计算含克制+高度） |
| `UTDUnitSquad` | UObject | 编队管理器（TMap 映射，按玩家/范围查询，批量操作） |
| `UTDUnitAIController` | UObject | AI 控制器（攻击→移动→待命 决策链，贪心策略） |

#### 关键接口
```
UTDUnitDataAsset::GetDamageMultiplierVs(TargetType)
ATDUnitBase::InitializeUnit() / MoveTo() / ApplyDamage() / CalculateDamageAgainst()
ATDUnitBase::IsInAttackRange() / GetHeightAttackBonus() / GetTerrainDefenseBonus()
UTDUnitSquad::AddUnit() / GetUnitAt() / GetUnitsByOwner() / RemoveDeadUnits() / ResetAllMovePoints()
UTDUnitAIController::ExecuteTurn() / FindNearestEnemy() / FindMoveTarget()
```

#### 委托
```
FTDOnUnitDied(DeadUnit)
FTDOnUnitDamaged(DamagedUnit, DamageAmount, RemainingHealth)
```

---

### 4.5 Combat — 战斗系统

**职责：** 伤害数值计算、投射物飞行、战斗流程编排（回合制状态机）。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDCombatState` | TDCombatManager.h | 战斗状态 (None / Deploying / InProgress / Finished) |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `UTDDamageCalculator` | UObject | 纯计算：单位vs单位、建筑vs单位、单位vs建筑，含高度+地形+保底 |
| `ATDProjectileBase` | AActor | 投射物（UProjectileMovementComponent，命中伤害+地形破坏概率） |
| `UTDCombatManager` | UObject | 战斗管理器（初始化→回合执行→胜负判定→结果输出） |

#### 关键接口
```
UTDDamageCalculator::CalculateUnitDamage() / CalculateBuildingDamage() / GetHeightModifier()
ATDProjectileBase::InitializeProjectile()
UTDCombatManager::InitializeCombat() / ExecuteCombatTurn() / ExecuteFullCombat() / GetCombatResult()
```

#### 伤害公式
```
基础伤害 = 攻击力 × 兵种克制倍率
高度修正 = 1.0 + (AttackerHeight - DefenderHeight) × 0.15   (攻方高时)
         = 1.0 - (DefenderHeight - AttackerHeight) × 0.10   (守方高时)
地形防御 = 1.0 - DefenseBonus
最终伤害 = max(基础伤害 × 高度修正 × 地形防御, 基础伤害 × 0.1, 1)
```

#### 委托
```
FTDOnCombatStateChanged(NewState)
FTDOnCombatFinished(Result)
```

---

### 4.6 Match — 对局管理

**职责：** 多回合淘汰赛流程管理、玩家配对算法、连胜/连败追踪。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDMatchmakingStrategy` | TDMatchmakingManager.h | 配对策略 (Random / Swiss / Serpentine) |

#### 结构体
| 结构体 | 文件 | 说明 |
|--------|------|------|
| `FTDRoundPairing` | TDRoundManager.h | 回合配对数据（攻守方索引，是否有效） |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `UTDMatchManager` | UObject | 对局管理（回合历史、连胜/连败、淘汰判定、胜者查询） |
| `UTDRoundManager` | UObject | 单回合管理（配对生成→战斗执行→结果收集） |
| `UTDMatchmakingManager` | UObject | 配对算法（Random/Swiss/Serpentine，奇数轮空处理） |

#### 关键接口
```
UTDMatchManager::InitializeMatch() / RecordRoundResult() / ShouldEndMatch() / GetWinner()
UTDRoundManager::InitializeRound() / ExecuteBattles() / GetRoundResults()
UTDMatchmakingManager::GeneratePairings() / SetStrategy()
```

---

### 4.7 Economy — 经济系统

**职责：** 回合收入计算、消耗验证、资源发放、胜负奖惩。

#### 结构体
| 结构体 | 文件 | 说明 |
|--------|------|------|
| `FTDRoundReward` | TDRewardCalculator.h | 回合奖惩结果（金币/科研/血量变化，胜负） |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `UTDResourceManager` | UObject | 资源管理（回合收入计算、消耗验证、资源发放） |
| `UTDRewardCalculator` | UObject | 奖惩计算（连胜加成、连败补偿、失败伤害递增） |

#### 关键接口
```
UTDResourceManager::CalculateRoundIncome() / CanAffordCost() / GrantRoundResources()
UTDRewardCalculator::CalculateRoundReward() / CalculateLoseDamage()
```

#### 奖惩公式
```
胜利金币 = Config.WinBonusGold + min(WinStreak × 5, 30)
失败扣血 = Config.LoseDamage + Round × 0.5
连败补偿 = min(LoseStreak × 5, 25)
```

---

### 4.8 TechTree — 科技树系统

**职责：** 科技数据配置、前置依赖检查、研究进度管理、被动加成汇总、解锁查询。

#### 枚举
| 枚举 | 文件 | 说明 |
|------|------|------|
| `ETDTechResearchState` | TDTechNode.h | 研究状态 (Locked → Available → Researching → Completed) |

#### 结构体
| 结构体 | 文件 | 说明 |
|--------|------|------|
| `FTDTechNodeData` | TDTechTreeDataAsset.h | 科技节点配置（时代、费用、前置、解锁内容、被动加成） |

#### 类
| 类 | 基类 | 说明 |
|----|------|------|
| `UTDTechTreeDataAsset` | UDataAsset | 科技树配置（策划编辑，按时代/ID 查询） |
| `UTDTechNode` | UObject | 科技节点运行时状态（研究状态管理） |
| `UTDTechTreeManager` | UObject | 科技树管理器（研究操作、加成汇总、解锁查询） |

#### 关键接口
```
UTDTechTreeDataAsset::FindTechNode() / GetTechsByEra()
UTDTechNode::Initialize() / GetResearchState() / SetResearchState()
UTDTechTreeManager::Initialize() / ResearchTech() / CanResearchTech()
UTDTechTreeManager::GetTotalAttackBonus() / GetTotalDefenseBonus() / GetTotalResourceBonus()
UTDTechTreeManager::IsBuildingUnlocked() / IsUnitUnlocked() / GetUnlockedTerrainModifyLevel()
```

#### 委托
```
FTDOnTechResearched(TechID, Era)
FTDOnEraAdvanced(NewEra)
```

---

## 5. 跨模块共享类型

| 类型 | 定义位置 | 使用模块 |
|------|----------|----------|
| `ETDTechEra` | Core/TDSharedTypes.h | Unit, TechTree |
| `ETDGamePhase` | Core/TDGamePhaseTypes.h | Core, Match |
| `FTDRoundResult` | Core/TDGamePhaseTypes.h | Core, Combat, Match, Economy |
| `FTDMatchConfig` | Core/TDGamePhaseTypes.h | Core, Match, Economy |
| `FTDHexCoord` | HexGrid/TDHexCoord.h | HexGrid, Building, Unit, Combat |
| `ETDTerrainType` | HexGrid/TDHexGridSaveData.h | HexGrid, Building |
| `FTDRoundPairing` | Match/TDRoundManager.h | Match |
| `FTDRoundReward` | Economy/TDRewardCalculator.h | Economy |
| `FTDTechNodeData` | TechTree/TDTechTreeDataAsset.h | TechTree |
| `ETDBuildingType` | Building/TDBuildingDataAsset.h | Building, Combat (前向引用) |
| `ETDUnitType` | Unit/TDUnitDataAsset.h | Unit, Combat (前向引用) |

---

## 6. 数据流图

### 6.1 对局生命周期（阶段状态机）

```
              GameMode::StartMatch()
                      │
                      ▼
               ┌──────────────┐
          ┌───►│ Preparation  │ (建造/训练/研究/地形改造，倒计时 60s)
          │    └──────┬───────┘
          │           │ OnPhaseTimeExpired
          │           ▼
          │    ┌──────────────┐
          │    │ Matchmaking  │ (MatchmakingManager 生成配对)
          │    └──────┬───────┘
          │           │ AdvanceToNextPhase
          │           ▼
          │    ┌──────────────┐
          │    │   Battle     │ (CombatManager 执行战斗，倒计时 90s)
          │    └──────┬───────┘
          │           │ OnPhaseTimeExpired / 战斗结束
          │           ▼
          │    ┌──────────────┐
          │    │ Settlement   │ (奖惩结算、淘汰判定、自动保存)
          │    └──────┬───────┘
          │           │
          │     ShouldEndMatch?
          │    ┌──────┴──────┐
          │  No│             │Yes
          │    ▼             ▼
          └────┘      ┌──────────┐
                      │ GameOver │
                      └──────────┘
```

### 6.2 战斗流程

```
CombatManager::InitializeCombat(Attacker, Defender, Grid)
    │
    ▼ State = Deploying
    │ (进攻方放置单位到 UnitSquad)
    │
    ▼ State = InProgress
    │
    ├─── ExecuteCombatTurn() ◄────────────────────┐
    │       │                                      │
    │       ├── ProcessBuildingAttacks(Grid)        │
    │       │     └── DamageCalculator.CalculateBuildingDamage()
    │       │         └── Unit.ApplyDamage()        │
    │       │                                      │
    │       ├── UnitSquad.RemoveDeadUnits()         │
    │       │                                      │
    │       ├── ProcessUnitActions(Grid)            │
    │       │     └── UnitAI.ExecuteTurn()          │
    │       │         ├── FindEnemyInRange → Attack │
    │       │         └── FindNearestEnemy → Move   │
    │       │                                      │
    │       ├── UnitSquad.RemoveDeadUnits()         │
    │       │                                      │
    │       └── CheckCombatEnd()                   │
    │           ├── false ─────────────────────────┘
    │           └── true
    │
    ▼ State = Finished
    │
    └── FTDRoundResult = GetCombatResult()
```

### 6.3 经济流程

```
回合开始 (Preparation Phase)
    │
    ▼
ResourceManager::GrantRoundResources(Player, Config)
    │
    ├── 金币 += Config.GoldPerRound + BuildingIncome (预留)
    └── 科研点 += BaseResearchPointsPerRound
    │
    ▼
玩家操作：建造建筑 / 训练单位 / 研究科技 / 改造地形
    │
    ├── PlayerState.SpendGold(Cost)
    └── PlayerState.SpendResearchPoints(Cost)
    │
    ▼
回合结算 (Settlement Phase)
    │
    ▼
RewardCalculator::CalculateRoundReward(Result, Config, Round, WinStreak, LoseStreak)
    │
    ├── 胜利: 金币 += WinBonusGold + WinStreakBonus
    │         科研点 += WinResearchPointBonus
    │
    └── 失败: 血量 -= LoseDamage + Round × Escalation
              金币 += LoseStreakCompensation
```

### 6.4 科技研究流程

```
TechTreeManager::ResearchTech(TechID, Player)
    │
    ├── CanResearchTech(TechID, Player)?
    │     ├── ArePrerequisitesMet(TechID)?  ── 前置全部 Completed?
    │     ├── TechNode.State == Available?
    │     └── Player.ResearchPoints >= Cost?
    │
    ├── 失败 → return false
    │
    └── 成功:
        ├── Player.SpendResearchPoints(Cost)
        ├── TechNode.State = Completed
        ├── RefreshAvailability()  ── 更新后续节点 Locked→Available
        ├── OnTechResearched.Broadcast(TechID, Era)
        └── 如果 Era > CurrentEra → OnEraAdvanced.Broadcast(NewEra)
```

---

## 7. 构建配置

**TowerDefend.Build.cs:**

```csharp
PublicDependencyModuleNames:
  Core, CoreUObject, Engine, InputCore, EnhancedInput, Json, JsonUtilities

PCHUsage: UseExplicitOrSharedPCHs
PublicIncludePaths: ModuleDirectory (Source/TowerDefend)
```

**TOWERDEFEND_API 导出宏** 已添加至以下需要跨模块链接的类：
`ATDHexTile`, `ATDHexGridManager`, `ATDGameMode`, `ATDGameState`, `ATDPlayerState`,
`ATDPlayerController`, `ATDCameraPawn`, `ATDBuildingBase`, `ATDBuildingDataAsset`,
`ATDDefenseTower`, `ATDWall`, `UTDBuildingManager`, `ATDUnitBase`, `UTDUnitDataAsset`,
`UTDUnitSquad`, `UTDUnitAIController`, `UTDTechTreeDataAsset`, `UTDTechNode`,
`UTDTechTreeManager`, `UTDMatchManager`, `UTDRoundManager`, `UTDMatchmakingManager`.

---

## 8. 完整文件清单

### Core (13 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 1 | Core/TDSharedTypes.h | Header-only | ETDTechEra 枚举 |
| 2 | Core/TDGamePhaseTypes.h | Header-only | ETDGamePhase / FTDRoundResult / FTDMatchConfig |
| 3 | Core/TDGameMode.h | Header | 对局生命周期管理 |
| 4 | Core/TDGameMode.cpp | Source | 阶段流转、计时器、玩家初始化 |
| 5 | Core/TDGameState.h | Header | 全局同步状态 (Replicated) |
| 6 | Core/TDGameState.cpp | Source | RepNotify、存活列表管理 |
| 7 | Core/TDPlayerState.h | Header | 玩家资源/血量/统计 |
| 8 | Core/TDPlayerState.cpp | Source | 资源操作、回合结算 |
| 9 | Core/TDPlayerController.h | Header | 策略相机输入 (Enhanced Input) |
| 10 | Core/TDPlayerController.cpp | Source | 输入回调、边缘滚动、射线拾取 |
| 11 | Core/TDCameraPawn.h | Header | 相机 Pawn 组件层级 |
| 12 | Core/TDCameraPawn.cpp | Source | 组件初始化 |

### HexGrid (14 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 13 | HexGrid/TDHexCoord.h | Header | 立方坐标 (Q,R,S)，距离/邻居/世界坐标 |
| 14 | HexGrid/TDHexCoord.cpp | Source | 坐标运算、CubeRound、世界坐标转换 |
| 15 | HexGrid/TDHexGridSaveData.h | Header | ETDTerrainType / 格子&地图序列化数据 |
| 16 | HexGrid/TDHexGridSaveData.cpp | Source | Reset / GetTileCount |
| 17 | HexGrid/TDHexTile.h | Header | 六边形格子 Actor |
| 18 | HexGrid/TDHexTile.cpp | Source | 初始化/地形属性/视觉更新 |
| 19 | HexGrid/TDHexGridManager.h | Header | 网格管理器 |
| 20 | HexGrid/TDHexGridManager.cpp | Source | 生成/查询/存档 |
| 21 | HexGrid/TDTerrainGenerator.h | Header | Perlin Noise 地形生成 |
| 22 | HexGrid/TDTerrainGenerator.cpp | Source | Noise采样/地形类型映射/对称/平坦化 |
| 23 | HexGrid/TDTerrainModifier.h | Header | 运行时地形修改 |
| 24 | HexGrid/TDTerrainModifier.cpp | Source | 升降/验证/类型同步 |
| 25 | HexGrid/TDHexPathfinding.h | Header | A* 寻路 + Dijkstra 可达范围 |
| 26 | HexGrid/TDHexPathfinding.cpp | Source | 搜索实现/代价计算/路径回溯 |

### Building (10 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 27 | Building/TDBuildingDataAsset.h | Header | ETDBuildingType / 建筑静态数据 |
| 28 | Building/TDBuildingDataAsset.cpp | Source | 升级费用/地形检查/射程加成 |
| 29 | Building/TDBuildingBase.h | Header | 建筑基类 Actor |
| 30 | Building/TDBuildingBase.cpp | Source | 初始化/升级/受伤/经济 |
| 31 | Building/TDDefenseTower.h | Header | 防御塔子类 |
| 32 | Building/TDDefenseTower.cpp | Source | 定时器攻击/高度射程 |
| 33 | Building/TDWall.h | Header | 城墙子类 |
| 34 | Building/TDWall.cpp | Source | 移动消耗倍率/阻挡 |
| 35 | Building/TDBuildingManager.h | Header | 建筑管理器 |
| 36 | Building/TDBuildingManager.cpp | Source | 6步验证/Spawn分派/经济汇总 |

### Unit (8 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 37 | Unit/TDUnitDataAsset.h | Header | ETDUnitType / 单位静态数据 / 克制倍率 |
| 38 | Unit/TDUnitDataAsset.cpp | Source | GetDamageMultiplierVs 实现 |
| 39 | Unit/TDUnitBase.h | Header | 单位基类 Actor |
| 40 | Unit/TDUnitBase.cpp | Source | 移动/伤害/地形加成 |
| 41 | Unit/TDUnitSquad.h | Header | 编队管理器 |
| 42 | Unit/TDUnitSquad.cpp | Source | TMap 管理/批量操作 |
| 43 | Unit/TDUnitAIController.h | Header | ETDAIActionResult / AI 控制器 |
| 44 | Unit/TDUnitAIController.cpp | Source | 攻击→移动→待命 决策链 |

### Combat (6 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 45 | Combat/TDDamageCalculator.h | Header | 伤害计算器 |
| 46 | Combat/TDDamageCalculator.cpp | Source | 三种伤害计算/高度修正/保底 |
| 47 | Combat/TDProjectileBase.h | Header | 投射物基类 |
| 48 | Combat/TDProjectileBase.cpp | Source | 飞行/碰撞/地形破坏 |
| 49 | Combat/TDCombatManager.h | Header | ETDCombatState / 战斗管理器 |
| 50 | Combat/TDCombatManager.cpp | Source | 回合执行/胜负判定 |

### Match (6 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 51 | Match/TDMatchManager.h | Header | 对局管理器 |
| 52 | Match/TDMatchManager.cpp | Source | 回合历史/连胜追踪/淘汰 |
| 53 | Match/TDRoundManager.h | Header | FTDRoundPairing / 回合管理 |
| 54 | Match/TDRoundManager.cpp | Source | 配对生成/战斗模拟 |
| 55 | Match/TDMatchmakingManager.h | Header | ETDMatchmakingStrategy / 配对算法 |
| 56 | Match/TDMatchmakingManager.cpp | Source | Random/Swiss/Serpentine 实现 |

### Economy (4 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 57 | Economy/TDResourceManager.h | Header | 资源管理器 |
| 58 | Economy/TDResourceManager.cpp | Source | 收入计算/发放 |
| 59 | Economy/TDRewardCalculator.h | Header | FTDRoundReward / 奖惩计算器 |
| 60 | Economy/TDRewardCalculator.cpp | Source | 连胜/连败/递增伤害 |

### TechTree (6 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 61 | TechTree/TDTechTreeDataAsset.h | Header | FTDTechNodeData / 科技树数据资产 |
| 62 | TechTree/TDTechTreeDataAsset.cpp | Source | 查询接口 |
| 63 | TechTree/TDTechNode.h | Header | ETDTechResearchState / 节点运行时 |
| 64 | TechTree/TDTechNode.cpp | Source | 状态管理 |
| 65 | TechTree/TDTechTreeManager.h | Header | 科技树管理器 |
| 66 | TechTree/TDTechTreeManager.cpp | Source | 研究/加成/解锁/前置检查 |

### Legacy & Build (8 files)

| # | 文件 | 类型 | 主要内容 |
|---|------|------|----------|
| 67 | TowerDefend.h | Header | 模块入口头文件 |
| 68 | TowerDefend.cpp | Source | 模块实现 |
| 69 | TowerDefendGameMode.h | Header | 过渡用游戏模式 |
| 70 | TowerDefendGameMode.cpp | Source | 默认 Pawn/Controller 配置 |
| 71 | TowerDefendCharacter.h | Header | 引擎模板角色（残留） |
| 72 | TowerDefendCharacter.cpp | Source | 角色实现 |
| 73 | TowerDefend.Build.cs | Build | UBT 构建规则 |

**总计：73 文件** (37 .h + 35 .cpp + 1 .Build.cs)

> 注：Core/TDSharedTypes.h 和 Core/TDGamePhaseTypes.h 为纯头文件（无对应 .cpp）。
