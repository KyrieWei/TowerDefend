# 多人塔防 PVP 游戏 — 开发规划

## 〇、命名规范

> **所有 C++ 代码文件（.h / .cpp）均以 `TD` 前缀开头**，作为项目统一标识。
>
> - 类名示例：`ATDGameMode`、`UTDHexTile`、`FTDHexCoord`
> - 文件名示例：`TDGameMode.h`、`TDHexTile.cpp`
> - 结构体前缀：`FTD`（如 `FTDHexGridSaveData`）
> - 枚举前缀：`ETD`（如 `ETDTerrainType`）
> - 接口前缀：`ITD`（如 `ITDDamageable`）

---

## 一、核心概念拆解

| 维度 | 设计要点 |
|------|---------|
| **视角** | 俯视/斜45度策略视角（非第三人称），参考文明6 |
| **地图** | 六边形网格（Hex Grid），每个格子有地形属性，**地形高度可动态改变** |
| **对战模式** | 多人 PVP，每回合两两配对，攻守互换 |
| **经济系统** | 回合奖励 + 胜负奖惩 → 升级建筑/军队/科技 |
| **科技树** | 远古 → 古典 → 中世纪 → 文艺复兴 → 工业 → 现代 |
| **胜负条件** | 血量归零淘汰，最后存活者获胜 |

---

## 二、模块划分与开发阶段

### 第一阶段：核心框架（Foundation）

#### 1. 项目重构

- 移除第三人称模板代码（Character、GameMode）
- 创建策略游戏视角相机系统（俯视、缩放、平移、旋转）
- 搭建基础模块结构：

```
Source/TowerDefend/
├── Core/                    // 游戏核心框架
│   ├── TDGameMode.h/cpp           // 多人对战 GameMode
│   ├── TDGameState.h/cpp          // 全局对局状态（回合、玩家血量）
│   ├── TDPlayerState.h/cpp        // 玩家状态（资源、科技等级）
│   └── TDPlayerController.h/cpp   // 策略视角控制器
├── HexGrid/                 // 六边形地图系统
│   ├── TDHexGridManager.h/cpp     // 网格管理器（生成、查询、管理）
│   ├── TDHexTile.h/cpp            // 单个六边形格子（地形、高度、状态）
│   ├── TDHexCoord.h/cpp           // 轴向坐标系 (q, r, s)
│   ├── TDHexPathfinding.h/cpp     // A* 六边形寻路
│   ├── TDTerrainModifier.h/cpp    // 地形修改器（升高、降低、变更地形类型）
│   ├── TDTerrainGenerator.h/cpp   // 程序化地形生成（Noise + 规则约束）
│   └── TDHexGridSaveData.h/cpp    // 地形数据序列化与保存/加载
├── Building/                // 建筑/防御塔系统
│   ├── TDBuildingBase.h/cpp       // 建筑基类
│   ├── TDDefenseTower.h/cpp       // 防御塔
│   ├── TDWall.h/cpp               // 城墙
│   └── TDBuildingDataAsset.h/cpp  // 建筑数据资产
├── Unit/                    // 军队单位系统
│   ├── TDUnitBase.h/cpp           // 单位基类
│   ├── TDUnitSquad.h/cpp          // 编队
│   ├── TDUnitAIController.h/cpp   // 单位AI
│   └── TDUnitDataAsset.h/cpp      // 单位数据资产
├── Combat/                  // 战斗系统
│   ├── TDCombatManager.h/cpp      // 战斗流程管理
│   ├── TDDamageCalculator.h/cpp   // 伤害计算
│   └── TDProjectileBase.h/cpp     // 投射物基类
├── TechTree/                // 科技树系统
│   ├── TDTechTreeManager.h/cpp    // 科技树管理
│   ├── TDTechNode.h/cpp           // 科技节点
│   └── TDTechTreeDataAsset.h/cpp  // 科技树数据资产
├── Economy/                 // 经济系统
│   ├── TDResourceManager.h/cpp    // 资源管理
│   └── TDRewardCalculator.h/cpp   // 回合奖惩计算
├── Match/                   // 对局匹配系统
│   ├── TDMatchManager.h/cpp       // 多回合对局管理
│   ├── TDRoundManager.h/cpp       // 单回合攻防管理
│   └── TDMatchmakingManager.h/cpp // 配对算法
└── UI/                      // 用户界面
    ├── HUD/
    ├── Widgets/
    └── TechTreeUI/
```

#### 2. 六边形网格系统（Hex Grid）

这是整个游戏的地基，优先级最高：

- **坐标系统**：采用 Cube Coordinates（q, r, s 轴向坐标），参考 Red Blob Games 的经典 Hex Grid 方案
- **地形类型**：平原、丘陵、山地、森林、河流、沼泽 — 影响移动消耗、防御加成、建筑限制
- **地形高度**：每个格子拥有 `HeightLevel`（int32），支持动态升高/降低（详见 §2.1）
- **网格生成**：程序化生成 + 预设模板混合，支持对称地图（PVP 公平性）（详见 §2.2）
- **视觉表现**：每个 Hex Tile 使用独立 Mesh（而非 InstancedStaticMesh），以支持逐格子高度变化和地形动画；使用材质实例动态切换地形外观
- **邻居查询**：O(1) 邻居查找，支持距离计算、范围查询、视线检测（高度差影响视线）

##### 2.1 动态地形系统

地形在游戏过程中可以被改变（升高、降低、变更类型），这是核心玩法的一部分：

**地形高度模型：**

| 高度等级 | 名称 | 视觉表现 | 游戏效果 |
|---------|------|---------|---------|
| -2 | 深水 | 深蓝色水面，低于周围地面 | 不可通行、不可建造 |
| -1 | 浅水/沼泽 | 浅蓝/泥色，略低于地面 | 大幅减速、不可建造大型建筑 |
| 0 | 平原（基准） | 标准地面高度 | 无额外效果 |
| 1 | 丘陵 | 略微凸起 | 防御 +10%、远程射程 +1、移动消耗 +0.5 |
| 2 | 高地 | 明显凸起 | 防御 +20%、远程射程 +2、移动消耗 +1 |
| 3 | 山地 | 大幅凸起，带岩石纹理 | 不可通行（除特殊单位）、不可建造（除特殊建筑） |

**地形改变触发方式：**

- **主动技能**：玩家消耗资源对己方领地格子执行"抬升"或"挖掘"操作
- **科技解锁**：研究"土木工程"后解锁地形改造能力，高级科技可改造更多等级
- **建筑效果**：某些建筑（如"采石场"）建造时自动降低周围地形
- **战斗破坏**：重型攻城武器命中时有概率降低目标格子高度（炮击坑）

**地形改变规则：**

- 单次操作只能改变 ±1 高度等级
- 高度改变有冷却时间（防止同一格子被反复修改）
- 高度改变会影响该格子上已有建筑的稳定性（高度差过大建筑可能被摧毁）
- 相邻格子高度差不能超过 3（自然过渡约束）

**`TDTerrainModifier` 核心接口：**

```cpp
UCLASS()
class UTDTerrainModifier : public UObject
{
    GENERATED_BODY()
public:
    // 升高指定格子（返回是否成功）
    bool RaiseTerrain(const FTDHexCoord& Coord, int32 Amount = 1);

    // 降低指定格子
    bool LowerTerrain(const FTDHexCoord& Coord, int32 Amount = 1);

    // 变更地形类型（如平原→森林）
    bool ChangeTerrainType(const FTDHexCoord& Coord, ETDTerrainType NewType);

    // 批量修改（用于初始地形生成或大范围技能）
    bool ModifyTerrainBatch(const TArray<FTDTerrainModifyRequest>& Requests);

    // 验证修改是否合法（高度差约束、冷却、资源等）
    bool ValidateModification(const FTDHexCoord& Coord, int32 HeightDelta) const;
};
```

##### 2.2 地形生成系统

前期核心功能 — 程序化生成完整的六边形地图：

**生成算法：**

1. **基础地形层**：使用 Perlin Noise / Simplex Noise 生成连续高度场，映射到离散高度等级
2. **地形类型层**：基于高度 + 湿度（第二层 Noise）决定地形类型
   - 高度 ≥ 3 → 山地
   - 高度 2 + 低湿度 → 丘陵
   - 高度 0 + 高湿度 → 沼泽
   - 高度 ≤ -1 → 水域
   - 其余 → 平原/森林（随机）
3. **对称性保证**：PVP 地图按中心点/轴线做镜像对称，确保公平性
4. **规则约束后处理**：
   - 确保每位玩家的基地位置是平原（高度 0）
   - 确保基地周围 N 格内无山地/深水（保证可建造区域）
   - 确保地图连通性（A* 验证任意两个可通行格子之间存在路径）
   - 平滑相邻格子高度差超限的区域

**`TDTerrainGenerator` 核心接口：**

```cpp
UCLASS()
class UTDTerrainGenerator : public UObject
{
    GENERATED_BODY()
public:
    // 根据配置生成完整地图数据（不涉及渲染）
    FTDHexGridSaveData GenerateMap(const FTDMapGenerationConfig& Config);

    // 生成配置
    UPROPERTY(EditAnywhere)
    int32 MapRadius = 15;               // 地图半径（格子数）

    UPROPERTY(EditAnywhere)
    int32 Seed = 0;                     // 随机种子（0 = 随机）

    UPROPERTY(EditAnywhere)
    float HeightNoiseScale = 0.08f;     // 高度 Noise 缩放

    UPROPERTY(EditAnywhere)
    float MoistureNoiseScale = 0.12f;   // 湿度 Noise 缩放

    UPROPERTY(EditAnywhere)
    bool bSymmetric = true;             // 是否生成对称地图

    UPROPERTY(EditAnywhere)
    int32 PlayerCount = 2;              // 玩家数量（影响基地分布）
};
```

##### 2.3 地形保存与加载系统

地形数据需要可序列化，支持保存/加载完整地图状态：

**数据结构：**

```cpp
// 单个格子的保存数据
USTRUCT(BlueprintType)
struct FTDHexTileSaveData
{
    GENERATED_BODY()

    UPROPERTY() FTDHexCoord Coord;           // 格子坐标 (q, r)
    UPROPERTY() ETDTerrainType TerrainType;  // 地形类型
    UPROPERTY() int32 HeightLevel;           // 高度等级
    UPROPERTY() bool bHasBuilding;           // 是否有建筑
    UPROPERTY() FName BuildingID;            // 建筑标识（如有）
    UPROPERTY() int32 OwnerPlayerIndex;      // 所属玩家（-1 = 中立）
};

// 完整地图的保存数据
USTRUCT(BlueprintType)
struct FTDHexGridSaveData
{
    GENERATED_BODY()

    UPROPERTY() int32 MapRadius;                        // 地图半径
    UPROPERTY() int32 Seed;                             // 生成种子
    UPROPERTY() TArray<FTDHexTileSaveData> TileDataList; // 所有格子数据
    UPROPERTY() int32 Version;                          // 数据版本号（兼容性）
};
```

**保存/加载方案：**

| 方案 | 格式 | 用途 |
|------|------|------|
| UE SaveGame | 二进制（USaveGame 子类） | 运行时快速保存/加载，本地存档 |
| JSON 导出 | `.json` 文本 | 策划编辑、地图分享、版本管理 |

**`TDHexGridSaveData` 管理器核心接口：**

```cpp
UCLASS()
class UTDHexGridSaveManager : public UObject
{
    GENERATED_BODY()
public:
    // 从当前 HexGridManager 导出完整地图数据
    FTDHexGridSaveData ExportGridData(const ATDHexGridManager* GridManager) const;

    // 将保存数据应用到 HexGridManager（加载地图）
    bool ImportGridData(ATDHexGridManager* GridManager, const FTDHexGridSaveData& Data);

    // 保存到本地 SaveGame 槽位
    bool SaveToSlot(const FTDHexGridSaveData& Data, const FString& SlotName);

    // 从本地 SaveGame 槽位加载
    bool LoadFromSlot(FTDHexGridSaveData& OutData, const FString& SlotName);

    // 导出为 JSON 文件
    bool ExportToJson(const FTDHexGridSaveData& Data, const FString& FilePath);

    // 从 JSON 文件导入
    bool ImportFromJson(FTDHexGridSaveData& OutData, const FString& FilePath);
};
```

**保存/加载流程：**

```
[保存流程]
  HexGridManager → 遍历所有 TDHexTile
    → 收集 Coord / TerrainType / HeightLevel / Building 信息
    → 构建 FTDHexGridSaveData
    → 序列化到 USaveGame 或 JSON

[加载流程]
  读取 USaveGame 或 JSON → 反序列化为 FTDHexGridSaveData
    → 清空当前网格
    → 遍历 TileDataList 逐个创建 TDHexTile
    → 设置地形类型、高度、建筑
    → 刷新视觉表现（Mesh 高度、材质）

[回合间自动保存]
  每回合结算阶段结束后自动保存当前地图状态
    → 支持断线重连时恢复地形状态
    → 服务端保存权威数据，客户端保存缓存
```

#### 3. 策略相机系统

- 替换第三人称相机 → 俯视策略相机
- 支持：WASD/边缘平移、滚轮缩放、中键旋转、小地图点击跳转
- 限制相机在玩家领地范围内（攻守阶段可看对方领地）

---

### 第二阶段：核心玩法（Core Gameplay）

#### 4. 建筑系统

| 建筑类型 | 功能 | 升级方向 |
|----------|------|---------|
| 主基地 | 玩家核心，被攻破则当回合失败 | 血量、被动防御 |
| 箭塔/炮塔 | 自动攻击范围内敌军 | 射程、伤害、攻速 |
| 城墙 | 阻挡/减速敌军 | 血量、减速效果 |
| 资源建筑 | 每回合产出资源 | 产出量 |
| 兵营 | 训练军队单位 | 解锁高级兵种 |
| 陷阱 | 一次性或持续性地面效果 | 伤害、范围 |

- 建筑放置在六边形格子上，遵守地形限制（高度、地形类型）
- 建筑有朝向概念（影响射界）
- 使用 `UDataAsset` 驱动建筑属性，方便策划调参
- 建筑受地形高度影响：高地建筑获得射程加成，地形变化可能破坏建筑

#### 5. 军队单位系统

| 时代 | 典型单位 | 特点 |
|------|---------|------|
| 远古 | 棍棒战士、投石兵 | 低成本、弱战力 |
| 古典 | 剑士、弓箭手、骑兵 | 基础三角克制 |
| 中世纪 | 骑士、弩手、攻城车 | 可破城墙 |
| 文艺复兴 | 火枪手、大炮 | 远程火力跃升 |
| 工业 | 步兵、野战炮、装甲车 | 高机动性 |
| 现代 | 坦克、导弹兵、无人机 | 高科技碾压 |

- 单位以"编队"形式行动（一个 Hex 格内一个编队）
- AI 行为树驱动单位行为：寻路 → 接敌 → 攻击 → 绕路
- 兵种三角克制体系：近战 > 骑兵 > 远程 > 近战
- 地形高度差影响战斗：高处单位获得攻击加成，低处单位受防御惩罚

#### 6. 战斗系统

- **进攻阶段**：进攻方在限定时间内派兵从地图边缘进入防守方领地
- **路径规划**：军队沿六边形网格寻路，受地形（含高度）和建筑阻挡影响
- **自动战斗**：防御塔自动射击范围内单位，单位自动与敌方交战
- **胜负判定**：进攻方单位抵达/摧毁防守方主基地 = 进攻胜；时间耗尽或进攻方全灭 = 防守胜
- **地形破坏**：重型武器命中可降低地形高度（战术性改变地形）

---

### 第三阶段：战略层（Meta Game）

#### 7. 科技树系统

```
远古时代
├── 采矿 → 青铜冶炼 → 铁器
├── 畜牧 → 骑术
└── 制陶 → 砌砖 → 建筑学

古典时代
├── 数学 → 工程学（解锁攻城器械）
├── 战术 → 军事训练
└── 货币 → 贸易

中世纪
├── 铸造 → 机械（解锁弩、齿轮机关）
├── 骑士精神 → 重甲
└── 城堡建筑 → 防御工事

文艺复兴
├── 火药 → 弹道学
├── 印刷术 → 科学方法
└── 银行 → 经济学

工业时代
├── 蒸汽机 → 铁路 → 内燃机
├── 膛线 → 速射炮
└── 工业化 → 流水线

现代
├── 电子学 → 计算机 → 无人机技术
├── 核物理 → 导弹技术
└── 航空 → 隐形技术
```

- 每个科技节点解锁：新建筑 / 新兵种 / 建筑升级 / 兵种升级 / 被动加成
- **地形改造相关科技**：
  - 远古 — "制陶 → 砌砖"：解锁基础地形改造（±1 高度）
  - 古典 — "工程学"：扩展地形改造范围（可改造相邻格子）
  - 中世纪 — "城堡建筑"：解锁高级地形改造（±2 高度）
  - 工业 — "工业化"：解锁批量地形改造（一次改造多个格子）
- 研究科技消耗"科研点数"（每回合产出，可通过建筑/胜利加成）
- 科技树使用 `UDataAsset` + JSON 配表，策划可热更新
- UI 参考文明6的树状展开图

#### 8. 经济与回合系统

```
一场完整对局流程:

[匹配阶段] → N名玩家进入
    ↓
[准备阶段] (每回合开始)
  - 获得资源（基础 + 建筑产出 + 上回合奖励）
  - 建造/升级建筑
  - 训练/升级军队
  - 研究科技
  - 改造地形（消耗资源）
    ↓
[配对阶段]
  - 系统将玩家两两配对（蛇形/随机/积分匹配）
  - 随机决定攻守方（或双方各攻一次）
    ↓
[战斗阶段]
  - 进攻方派兵 → 防守方自动防御
  - 实时观战 / 有限干预（可选：使用技能卡）
    ↓
[结算阶段]
  - 胜者获得金币/科研点奖励
  - 败者扣除血量（可随回合数递增）
  - 淘汰血量归零的玩家
  - 自动保存当前地图状态
    ↓
[循环] → 回到准备阶段，直至决出冠军
```

资源类型：

| 资源 | 用途 |
|------|------|
| 金币 | 建造建筑、训练军队、改造地形 |
| 科研点 | 研究科技 |
| 木材/石材 | 建造特定建筑（可选，增加策略深度） |

---

### 第四阶段：多人网络（Multiplayer）

#### 9. 网络架构

```
                    ┌──────────────────┐
                    │  Dedicated Server │  ← 权威服务器
                    │  (UE5 DS)        │
                    └────────┬─────────┘
                             │
            ┌────────────────┼────────────────┐
            │                │                │
     ┌──────┴──────┐  ┌─────┴──────┐  ┌─────┴──────┐
     │  Client 1   │  │  Client 2  │  │  Client N  │
     │  (Player)   │  │  (Player)  │  │  (Player)  │
     └─────────────┘  └────────────┘  └────────────┘
```

- **采用 UE5 Dedicated Server 模式**（回合制策略游戏非常适合）
- 准备阶段：各客户端独立操作，操作结果通过 RPC 发送到服务端验证
- 战斗阶段：服务端运行战斗模拟，客户端播放结果（避免作弊）
- 需要同步的状态：玩家资源、建筑布局、军队配置、科技进度、血量、**地形状态**
- 使用 UE5 的 `Replication` + `GameState`/`PlayerState` 体系
- 地形变化通过 RPC 同步：客户端请求 → 服务端验证 → 广播结果

#### 10. 需要新增的模块依赖

在 `TowerDefend.Build.cs` 中需要添加：

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput",
    // 新增：
    "UMG",                    // UI 框架
    "Slate", "SlateCore",     // 底层 UI
    "AIModule",               // AI 行为树
    "NavigationSystem",       // 寻路（自定义 Hex 寻路可能不用）
    "GameplayAbilities",      // GAS（可选，用于技能系统）
    "GameplayTags",           // 标签系统
    "GameplayTasks",          // GAS 依赖
    "NetCore",                // 网络核心
    "OnlineSubsystem",        // 在线子系统
    "OnlineSubsystemUtils",   // 在线工具
    "Json",                   // JSON 序列化（地图导出）
    "JsonUtilities",          // JSON 工具
});
```

---

### 第五阶段：UI 与美术（UI & Art）

#### 11. UI 系统

| UI 模块 | 内容 |
|---------|------|
| 主菜单 | 匹配、设置、排行榜 |
| HUD | 资源显示、回合信息、小地图 |
| 建造面板 | 建筑列表、拖放放置 |
| 军队面板 | 兵种列表、编队管理 |
| 科技树界面 | 全屏树状图，可研究/查看 |
| 战斗界面 | 进攻/防守视角切换、战况显示 |
| 结算界面 | 回合结果、奖惩明细 |
| 排行/战绩 | 最终排名、MVP 数据 |
| **地形编辑面板** | 地形升降操作、消耗预览、可改造范围高亮 |

- 使用 UE5 的 UMG (Unreal Motion Graphics)
- 关键 Widget 用 C++ 写逻辑，蓝图做布局
- 支持 16:9 和 21:9 宽屏适配

#### 12. 美术风格

- 参考文明6的低多边形 (Low-poly) + 鲜明配色风格
- 六边形地块带高度差（丘陵凸起、水面凹陷），**高度变化时有平滑过渡动画**
- 单位使用风格化建模，不追求写实
- 建筑随科技时代改变外观（茅草屋 → 石堡 → 现代碉堡）

---

### 第六阶段：打磨与发布（Polish）

#### 13. 反作弊与平衡

- 服务端权威：所有关键逻辑在 DS 上执行
- 建筑放置验证、资源消耗验证、科技研究验证、**地形修改验证**
- 数值平衡：科技/兵种/建筑的属性全部走 DataAsset/DataTable，可热调
- 引入 ELO 或类似积分系统做匹配

#### 14. 优化

- Hex Grid：地形 Mesh 按高度分组，相同高度使用 `HISM` 批量渲染；高度变化时仅更新受影响的格子
- 单位：大量单位时考虑 Mass Entity / ECS 方案（UE5 的 MassEntity 框架）
- 网络：回合制游戏带宽需求低，但要注意战斗阶段的状态同步频率
- LOD：远距离格子降级显示
- 地形保存：大地图使用增量保存（仅保存变化的格子），减少 I/O 开销

---

## 三、技术选型决策

| 决策点 | 推荐方案 | 理由 |
|--------|---------|------|
| 寻路 | 自定义 Hex A*（含高度代价） | UE5 NavMesh 不适合六边形网格，需考虑高度差移动代价 |
| 战斗模拟 | 服务端 tick-based | 防作弊，回合制不需要帧同步 |
| 数据驱动 | UDataAsset + DataTable | UE5 原生支持，编辑器友好 |
| UI 框架 | UMG (C++ + Blueprint) | 成熟稳定，适合策略游戏大量 UI |
| AI | 行为树 (Behavior Tree) | UE5 原生支持，适合单位 AI |
| 网络 | Dedicated Server + Replication | 权威服务器防作弊，回合制带宽低 |
| 技能/效果 | GAS (可选) | 如果技能系统复杂则值得引入 |
| 地形生成 | Perlin/Simplex Noise + 后处理 | 成熟算法，可控性强，支持种子复现 |
| 地图保存 | USaveGame + JSON 双格式 | 运行时用二进制（快），编辑/分享用 JSON（可读） |

---

## 四、建议的开发优先级

```
[P0 — 必须先做，后续依赖]
 ├── 六边形网格系统 + 坐标系
 ├── 地形生成系统（程序化生成 + 可视化）
 ├── 地形保存/加载系统（SaveGame + JSON）
 ├── 动态地形修改（升高/降低基础功能）
 ├── 策略相机系统
 ├── 核心 GameMode / GameState / PlayerState 框架
 └── 基础 UI 框架

[P1 — 核心玩法循环]
 ├── 建筑系统（放置、升级、自动攻击）
 ├── 军队系统（生产、编队、寻路、战斗）
 ├── 回合管理（准备 → 战斗 → 结算）
 └── 资源经济系统

[P2 — 战略深度]
 ├── 科技树完整实现（含地形改造科技线）
 ├── 兵种克制与平衡
 ├── 地形高度对战斗的影响
 └── 多回合淘汰赛流程

[P3 — 多人联网]
 ├── Dedicated Server 架构
 ├── 状态同步与 Replication（含地形状态）
 ├── 匹配系统
 └── 反作弊验证

[P4 — 体验打磨]
 ├── 完整 UI/UX（含地形编辑面板）
 ├── 美术资源（模型、特效、音效）
 ├── 地形变化动画与视觉反馈
 ├── 数值平衡调优
 └── 性能优化
```

---

## 五、第一步行动建议

建议从以下顺序着手：

1. **清理模板代码** — 移除 ThirdPerson Character/GameMode，创建策略游戏基础框架
2. **实现 Hex Grid 坐标系** — `TDHexCoord`：轴向坐标、距离计算、邻居查询
3. **实现地形生成** — `TDTerrainGenerator`：Noise 生成高度场 + 地形类型分配
4. **实现地形保存/加载** — `TDHexGridSaveData` + `TDHexGridSaveManager`：序列化到 SaveGame / JSON
5. **实现网格可视化** — `TDHexGridManager` + `TDHexTile`：将生成的地形数据渲染为可见的六边形网格
6. **实现地形修改** — `TDTerrainModifier`：运行时升高/降低格子并更新视觉
7. **策略相机** — 有了地图和相机就能看到效果，快速迭代
8. **建筑放置** — 在格子上放东西，验证整个交互链路

---

## 六、多 Agent 并行开发协作规范

### 6.1 Worktree 分支结构

项目使用 Git Worktree 实现多 Agent 并行开发，各 Agent 在独立分支上工作互不干扰：

| Worktree 路径 | 分支 | 负责模块 |
|---------------|------|---------|
| `.claude/worktrees/terrain-system` | `feature/terrain-system` | TDHexTile, TDHexGridManager, TDTerrainGenerator, TDTerrainModifier, TDHexGridSaveData |
| `.claude/worktrees/strategy-camera` | `feature/strategy-camera` | TDPlayerController, TDCameraPawn, Input Actions |
| `.claude/worktrees/core-framework` | `feature/core-framework` | TDGameMode, TDGameState, TDPlayerState, TDGamePhaseTypes |

### 6.2 任务文件约定

每个 Worktree 根目录下有一个 `TASK.md` 文件，包含：
- 任务目标与分支信息
- 需要创建的文件清单及接口定义
- 与其他模块的边界约定
- 验证方式
- **进度追踪记录**（见 6.3）

Agent 启动后**必须先阅读** `TASK.md` 和项目根目录的 `DeveloperRules.md`。

### 6.3 进度追踪规范

每个 Agent **必须**在自己的 `TASK.md` 末尾维护一个 `## 进度记录` 章节，实时更新工作状态。

**格式要求**：

```markdown
## 进度记录

| 时间 | 文件 | 状态 | 备注 |
|------|------|------|------|
| 2026-02-28 10:00 | TDHexTile.h/cpp | 已完成 | 含 ETDTerrainType 枚举定义 |
| 2026-02-28 10:30 | TDHexGridManager.h/cpp | 进行中 | GenerateGrid 已实现，ApplySaveData 待完成 |
| 2026-02-28 11:00 | TDTerrainGenerator.h/cpp | 未开始 | - |
```

**状态值**：
- `未开始` — 尚未动手
- `进行中` — 正在编写
- `已完成` — 代码完成且编译通过
- `阻塞` — 因依赖或问题暂停（备注中说明原因）

**更新时机**：
- 每创建/完成一个文件时更新一次
- 遇到阻塞问题时立即更新
- 所有文件完成后更新最终状态

### 6.4 查看全局进度

在主仓库目录下执行以下命令可查看所有 Agent 进度：

```bash
# 查看所有 worktree 的进度记录
for wt in .claude/worktrees/*/; do echo "=== $(basename $wt) ==="; grep -A 100 "## 进度记录" "$wt/TASK.md" 2>/dev/null || echo "(暂无进度)"; echo; done
```

### 6.5 合并流程

所有 Agent 完成后，按以下顺序合并回 main：

```bash
# 1. 先合并无依赖的模块
git merge feature/core-framework
git merge feature/strategy-camera

# 2. 再合并依赖最多的模块
git merge feature/terrain-system

# 3. 解决可能的冲突（主要在 Build.cs）后提交
# 4. 清理 worktree
git worktree remove .claude/worktrees/terrain-system
git worktree remove .claude/worktrees/strategy-camera
git worktree remove .claude/worktrees/core-framework
```
