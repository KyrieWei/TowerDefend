# TowerDefend MVVM UI Framework

基于UnLua实现的完整MVVM UI管理框架，为UE5项目提供结构化的UI开发模式。

## 版本信息

- **版本**: 1.0.0-Alpha
- **UnLua版本**: 2.3.6+
- **UE版本**: 5.5+

## 功能特性

### 核心功能
- ✅ **完整的MVVM架构**: Model-View-ViewModel三层分离
- ✅ **数据绑定系统**: 支持单向、双向绑定和属性通知
- ✅ **UI管理器**: 窗口堆栈、层级管理、缓存机制
- ✅ **事件系统**: 统一的事件处理和分发
- ✅ **命令模式**: 解耦UI操作和业务逻辑
- ✅ **生命周期管理**: Create->Show->Hide->Destroy

### 高级功能
- ✅ **计算属性**: 自动依赖追踪和缓存
- ✅ **数据转换器**: 内置常用转换器，支持自定义
- ✅ **多值绑定**: 支持多个属性绑定到一个目标
- ✅ **属性验证**: 数据模型验证规则
- ✅ **脏标记系统**: 优化性能的变更追踪
- ✅ **UI缓存池**: 避免频繁创建销毁Widget

## 快速开始

### 1. 初始化框架

```lua
local TDUIInit = require("TDUIInit")

-- 初始化框架
TDUIInit.Initialize({
    EnableLogging = true,
    EnableCache = true,
    EnableDebugMode = true
})

-- 获取UI管理器
local uiManager = TDUIInit.GetUIManager()
```

### 2. 创建数据模型

```lua
local TDBaseModel = require("UI.Framework.TDBaseModel")

local PlayerModel = UnLua.Class()
setmetatable(PlayerModel, {__index = TDBaseModel})

function PlayerModel:Initialize()
    TDBaseModel.Initialize(self)
    
    -- 设置初始数据
    self:SetProperty("Name", "Player")
    self:SetProperty("Level", 1)
    self:SetProperty("Health", 100)
    self:SetProperty("MaxHealth", 100)
    
    -- 添加验证规则
    self:AddValidationRule("Health", function(value)
        local maxHealth = self:GetProperty("MaxHealth", 100)
        return value >= 0 and value <= maxHealth
    end)
end

return PlayerModel
```

### 3. 创建视图模型

```lua
local TDBaseViewModel = require("UI.Framework.TDBaseViewModel")

local PlayerViewModel = UnLua.Class()
setmetatable(PlayerViewModel, {__index = TDBaseViewModel})

function PlayerViewModel:Initialize()
    TDBaseViewModel.Initialize(self)
    
    -- 绑定属性
    self:BindProperty("PlayerName", "Name")
    self:BindProperty("PlayerLevel", "Level")
    self:BindProperty("Health", "Health")
    self:BindProperty("MaxHealth", "MaxHealth")
    
    -- 定义计算属性
    self:DefineComputedProperty("HealthPercent", function()
        local health = self:GetProperty("Health", 0)
        local maxHealth = self:GetProperty("MaxHealth", 100)
        return maxHealth > 0 and (health / maxHealth) or 0
    end, {"Health", "MaxHealth"})
    
    -- 创建命令
    self:CreateCommand("HealCommand", function()
        local model = self:GetModel()
        if model then
            local currentHealth = model:GetProperty("Health", 0)
            local maxHealth = model:GetProperty("MaxHealth", 100)
            model:SetProperty("Health", math.min(maxHealth, currentHealth + 20))
        end
    end, function()
        local health = self:GetProperty("Health", 0)
        local maxHealth = self:GetProperty("MaxHealth", 100)
        return health < maxHealth
    end)
end

return PlayerViewModel
```

### 4. 创建视图

```lua
local TDBaseView = require("UI.Framework.TDBaseView")

local PlayerView = UnLua.Class()
setmetatable(PlayerView, {__index = TDBaseView})

function PlayerView:OnBindViewModel()
    -- 绑定Widget属性
    self:BindWidgetProperty("PlayerNameText", "Text", "PlayerName")
    self:BindWidgetProperty("PlayerLevelText", "Text", "PlayerLevel", function(value)
        return string.format("Level %d", value)
    end)
    self:BindWidgetProperty("HealthBar", "Percent", "HealthPercent")
    
    -- 绑定命令
    self:BindCommand("HealButton", "OnClicked", "HealCommand")
end

return PlayerView
```

### 5. 注册和显示UI

```lua
-- 注册UI
uiManager:RegisterUI("PlayerUI", PlayerView, PlayerWidgetClass, {
    layer = TDUIDefines.UILayer.Normal,
    singleton = true,
    cacheable = true
})

-- 显示UI
local playerView = uiManager:ShowUI("PlayerUI")
```

## 架构设计

### 目录结构

```
Content/Script/
├── UI/
│   ├── Framework/           # 框架核心
│   │   ├── TDUIManager.lua
│   │   ├── TDBaseView.lua
│   │   ├── TDBaseViewModel.lua
│   │   ├── TDBaseModel.lua
│   │   ├── TDDataBinding.lua
│   │   └── TDEventSystem.lua
│   ├── Common/              # 公共工具
│   │   ├── TDUIDefines.lua
│   │   └── TDUIUtils.lua
│   └── Examples/            # 示例代码
│       ├── TDTestView.lua
│       ├── TDTestViewModel.lua
│       └── TDTestModel.lua
├── TDUIInit.lua             # 框架入口
└── TDUIFrameworkTest.lua    # 测试脚本
```

### 核心组件

#### TDUIManager
- UI注册和管理
- 窗口堆栈和层级控制
- UI缓存和生命周期管理

#### TDBaseModel
- 数据封装和属性管理
- 属性变更通知
- 数据验证和脏标记

#### TDBaseViewModel
- View和Model之间的桥梁
- 数据绑定和转换
- 命令处理和计算属性

#### TDBaseView
- Widget操作封装
- 数据绑定到UI元素
- 事件处理和动画

#### TDDataBinding
- 高级数据绑定功能
- 内置转换器
- 多值绑定支持

#### TDEventSystem
- 统一事件处理
- 事件总线和聚合器
- 异步事件队列

## API参考

### TDBaseModel API

```lua
-- 属性操作
model:SetProperty(name, value)
model:GetProperty(name, defaultValue)
model:HasProperty(name)
model:RemoveProperty(name)

-- 事件监听
model:OnPropertyChanged(callback, target)
model:OnSpecificPropertyChanged(propertyName, callback, target)

-- 验证
model:AddValidationRule(propertyName, validator, errorMessage)
model:ValidateProperty(propertyName, value)
model:ValidateAll()

-- 脏标记
model:IsDirty(propertyName)
model:ClearDirty(propertyName)
model:GetDirtyProperties()
```

### TDBaseViewModel API

```lua
-- 模型绑定
viewModel:SetModel(model)
viewModel:GetModel()

-- 属性绑定
viewModel:BindProperty(vmProperty, modelProperty, converter, mode)
viewModel:UnbindProperty(vmProperty)

-- 计算属性
viewModel:DefineComputedProperty(propertyName, computeFunc, dependencies)
viewModel:GetComputedProperty(propertyName, defaultValue)

-- 命令
viewModel:CreateCommand(name, executeFunc, canExecuteFunc)
viewModel:GetCommand(name)
viewModel:ExecuteCommand(name, parameter)
```

### TDBaseView API

```lua
-- 生命周期
view:Create(widgetClass, ownerPlayer)
view:Show(layer, animation)
view:Hide(animation)
view:Destroy()

-- ViewModel绑定
view:SetViewModel(viewModel)
view:GetViewModel()

-- Widget绑定
view:BindWidgetProperty(widgetName, widgetProperty, vmProperty, converter, mode)
view:BindCommand(widgetName, eventName, commandName, parameter)

-- 工具方法
view:FindWidget(widgetName)
view:RefreshAllBindings()
```

### TDUIManager API

```lua
-- UI注册
uiManager:RegisterUI(uiName, viewClass, widgetClass, config)
uiManager:UnregisterUI(uiName)

-- UI控制
uiManager:ShowUI(uiName, params, playerController)
uiManager:HideUI(uiName, destroy)
uiManager:CloseUI(uiName)
uiManager:GetUI(uiName)

-- 管理功能
uiManager:CloseAllUI(excludeList)
uiManager:SetUILayer(uiName, layer)
uiManager:GetStatistics()
```

## 数据绑定

### 绑定模式

```lua
-- 单向绑定（默认）
view:BindWidgetProperty("HealthText", "Text", "Health")

-- 双向绑定
view:BindWidgetProperty("NameInput", "Text", "PlayerName", nil, TDUIDefines.BindingMode.TwoWay)

-- 一次性绑定
view:BindWidgetProperty("MaxHealthText", "Text", "MaxHealth", nil, TDUIDefines.BindingMode.OneTime)
```

### 内置转换器

```lua
local TDDataBinding = require("UI.Framework.TDDataBinding")

-- 布尔转可见性
TDDataBinding.Converters.BooleanToVisibility
TDDataBinding.Converters.InverseBooleanToVisibility

-- 数值转换
TDDataBinding.Converters.NumberToPercent
TDDataBinding.Converters.NumberToText

-- 其他转换器
TDDataBinding.Converters.BooleanToText
TDDataBinding.Converters.NullToDefault
TDDataBinding.Converters.StringFormat
```

### 自定义转换器

```lua
-- 健康值颜色转换器
local healthColorConverter = function(healthPercent)
    if healthPercent > 0.6 then
        return UE.FLinearColor(0, 1, 0, 1) -- 绿色
    elseif healthPercent > 0.3 then
        return UE.FLinearColor(1, 1, 0, 1) -- 黄色
    else
        return UE.FLinearColor(1, 0, 0, 1) -- 红色
    end
end

view:BindWidgetProperty("HealthBar", "FillColorAndOpacity", "HealthPercent", healthColorConverter)
```

## 事件系统

### 基本用法

```lua
local TDEventSystem = require("UI.Framework.TDEventSystem")

-- 订阅事件
TDEventSystem.Subscribe("PlayerLevelUp", function(target, eventType, newLevel)
    print("Player leveled up to:", newLevel)
end, self)

-- 发布事件
TDEventSystem.Publish("PlayerLevelUp", 5)

-- 异步事件
TDEventSystem.PublishAsync("BackgroundTask", taskData)
TDEventSystem.ProcessQueue() -- 处理队列
```

### 事件总线

```lua
-- 创建专用事件总线
local gameBus = TDEventSystem.EventBus:New()

-- 高级事件总线（支持过滤器）
local advancedBus = TDEventSystem.AdvancedEventBus:New()

-- 添加过滤器
local filter = TDEventSystem.EventFilter:New(function(eventType)
    return eventType:find("Game") ~= nil -- 只处理包含"Game"的事件
end)
advancedBus:AddFilter(filter)
```

## 最佳实践

### 1. 模型设计

```lua
-- ✅ 好的做法
local PlayerModel = UnLua.Class()
setmetatable(PlayerModel, {__index = TDBaseModel})

function PlayerModel:Initialize()
    TDBaseModel.Initialize(self)
    
    -- 明确的初始值
    self:SetProperty("Health", 100)
    self:SetProperty("MaxHealth", 100)
    
    -- 业务验证规则
    self:AddValidationRule("Health", function(value)
        return value >= 0 and value <= self:GetProperty("MaxHealth", 100)
    end)
end

-- 业务逻辑方法
function PlayerModel:TakeDamage(damage)
    local currentHealth = self:GetProperty("Health", 0)
    self:SetProperty("Health", math.max(0, currentHealth - damage))
    
    if self:GetProperty("Health", 0) <= 0 then
        self:SetProperty("IsDead", true)
    end
end
```

### 2. ViewModel设计

```lua
-- ✅ 计算属性用于派生数据
viewModel:DefineComputedProperty("HealthPercent", function()
    local health = self:GetProperty("Health", 0)
    local maxHealth = self:GetProperty("MaxHealth", 100)
    return maxHealth > 0 and (health / maxHealth) or 0
end, {"Health", "MaxHealth"})

-- ✅ 命令封装用户操作
viewModel:CreateCommand("AttackCommand", function(target)
    local model = self:GetModel()
    if model then
        model:Attack(target)
    end
end, function(target)
    return not self:GetProperty("IsDead", false) and target ~= nil
end)
```

### 3. View设计

```lua
-- ✅ 在OnBindViewModel中设置绑定
function PlayerView:OnBindViewModel()
    -- 简单属性绑定
    self:BindWidgetProperty("NameText", "Text", "PlayerName")
    
    -- 带转换器的绑定
    self:BindWidgetProperty("HealthBar", "Percent", "HealthPercent")
    self:BindWidgetProperty("HealthText", "Text", "Health", function(value)
        return string.format("HP: %d", value or 0)
    end)
    
    -- 命令绑定
    self:BindCommand("AttackButton", "OnClicked", "AttackCommand")
    
    -- 可见性绑定
    self:BindWidgetProperty("DeadOverlay", "Visibility", "IsDead", 
        TDDataBinding.Converters.BooleanToVisibility)
end
```

### 4. 性能优化

```lua
-- ✅ 使用计算属性缓存复杂计算
viewModel:DefineComputedProperty("FormattedStats", function()
    -- 复杂的格式化逻辑
    return self:FormatPlayerStats()
end, {"Health", "Mana", "Level", "Experience"})

-- ✅ 批量更新减少通知
model:SetProperties({
    Health = newHealth,
    Mana = newMana,
    Experience = newExp
}, true) -- 跳过验证提高性能

-- ✅ 及时清理资源
function MyView:Destroy()
    -- 清理定时器、动画等
    self:CleanupTimers()
    
    -- 调用父类销毁
    TDBaseView.Destroy(self)
end
```

## 测试

### 运行测试

```lua
-- 运行完整测试套件
local TDUIFrameworkTest = require("TDUIFrameworkTest")
TDUIFrameworkTest.RunAllTests()

-- 运行性能测试
TDUIFrameworkTest.RunPerformanceTest()

-- 快速测试
TDUIFrameworkTest.QuickTest()
```

### 自定义测试

```lua
-- 创建测试用例
local function TestMyFeature()
    local model = MyModel()
    local viewModel = MyViewModel()
    
    viewModel:SetModel(model)
    
    -- 测试业务逻辑
    model:DoSomething()
    assert(model:GetProperty("Result") == "Expected")
    
    -- 清理
    viewModel:Destroy()
    model:Destroy()
end
```

## 故障排除

### 常见问题

1. **绑定不生效**
   - 检查属性名是否正确
   - 确认ViewModel已正确绑定到View
   - 验证转换器是否返回正确类型

2. **命令无法执行**
   - 检查CanExecute条件
   - 确认命令已正确注册
   - 验证参数类型

3. **内存泄漏**
   - 确保调用Destroy方法
   - 移除事件监听器
   - 清理定时器和动画

### 调试工具

```lua
-- 启用调试模式
TDUIInit.Initialize({EnableDebugMode = true})

-- 获取框架统计信息
local stats = TDUIInit.GetStatistics()
print("UI Count:", stats.uiManager.activeUICount)

-- 获取View调试信息
local debugInfo = view:GetDebugInfo()
print(debugInfo)
```

## 许可证

本框架基于MIT许可证开源。

## 贡献

欢迎提交Issue和Pull Request来改进这个框架。

---

**TowerDefend MVVM UI Framework** - 让UE5 UI开发更加结构化和高效！