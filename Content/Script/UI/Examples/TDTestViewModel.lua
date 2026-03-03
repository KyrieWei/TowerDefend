-- TowerDefend UI Framework - Test ViewModel
-- 测试视图模型，展示MVVM框架中ViewModel层的使用

local TDTestViewModel = UnLua.Class()

-- 导入依赖
local TDBaseViewModel = require("UI.Framework.TDBaseViewModel")
local TDTestModel = require("UI.Examples.TDTestModel")
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDDataBinding = require("UI.Framework.TDDataBinding")

-- 继承基类
setmetatable(TDTestViewModel, {__index = TDBaseViewModel})

-- 构造函数
function TDTestViewModel:Initialize()
    -- 调用父类构造函数
    TDBaseViewModel.Initialize(self)
    
    -- 创建数据模型
    self.testModel = TDTestModel()
    self:SetModel(self.testModel)
    
    -- 设置属性绑定
    self:SetupPropertyBindings()
    
    -- 创建命令
    self:SetupCommands()
    
    -- 定义计算属性
    self:SetupComputedProperties()
    
    TDUIUtils.LogInfo("TDTestViewModel initialized")
end

-- 设置属性绑定
function TDTestViewModel:SetupPropertyBindings()
    -- 玩家信息绑定
    self:BindProperty("PlayerName", "PlayerName")
    self:BindProperty("PlayerLevel", "PlayerLevel")
    self:BindProperty("PlayerExp", "PlayerExp")
    self:BindProperty("PlayerExpMax", "PlayerExpMax")
    
    -- 资源绑定
    self:BindProperty("Gold", "Gold")
    self:BindProperty("Gems", "Gems")
    self:BindProperty("Energy", "Energy")
    self:BindProperty("EnergyMax", "EnergyMax")
    
    -- 游戏状态绑定
    self:BindProperty("IsPlaying", "IsPlaying")
    self:BindProperty("GameScore", "GameScore")
    self:BindProperty("GameTime", "GameTime")
    self:BindProperty("WaveNumber", "WaveNumber")
    self:BindProperty("IsPaused", "IsPaused")
    
    -- 统计信息绑定
    self:BindProperty("TotalKills", "TotalKills")
    self:BindProperty("TotalDamage", "TotalDamage")
    self:BindProperty("BestScore", "BestScore")
    
    -- UI状态绑定
    self:BindProperty("ShowDebugInfo", "ShowDebugInfo")
    self:BindProperty("IsMenuOpen", "IsMenuOpen")
end

-- 设置命令
function TDTestViewModel:SetupCommands()
    -- 开始游戏命令
    self:CreateCommand("StartGameCommand", function()
        return self.testModel:StartGame()
    end, function()
        return not self:GetProperty("IsPlaying", false) and self:GetProperty("Energy", 0) >= 10
    end)
    
    -- 结束游戏命令
    self:CreateCommand("EndGameCommand", function()
        return self.testModel:EndGame()
    end, function()
        return self:GetProperty("IsPlaying", false)
    end)
    
    -- 暂停/恢复游戏命令
    self:CreateCommand("TogglePauseCommand", function()
        return self.testModel:TogglePause()
    end, function()
        return self:GetProperty("IsPlaying", false)
    end)
    
    -- 下一波命令
    self:CreateCommand("NextWaveCommand", function()
        return self.testModel:NextWave()
    end, function()
        return self:GetProperty("IsPlaying", false) and not self:GetProperty("IsPaused", false)
    end)
    
    -- 购买金币命令
    self:CreateCommand("BuyGoldCommand", function(amount)
        amount = amount or 100
        local gemCost = math.floor(amount / 10)
        if self.testModel:SpendGems(gemCost) then
            self.testModel:SetProperty("Gold", self:GetProperty("Gold", 0) + amount)
            TDUIUtils.LogInfo("Bought %d gold for %d gems", amount, gemCost)
            return true
        end
        return false
    end, function(amount)
        amount = amount or 100
        local gemCost = math.floor(amount / 10)
        return self:GetProperty("Gems", 0) >= gemCost
    end)
    
    -- 恢复能量命令
    self:CreateCommand("RestoreEnergyCommand", function(amount)
        amount = amount or 50
        local gemCost = math.floor(amount / 5)
        if self.testModel:SpendGems(gemCost) then
            self.testModel:RestoreEnergy(amount)
            TDUIUtils.LogInfo("Restored %d energy for %d gems", amount, gemCost)
            return true
        end
        return false
    end, function(amount)
        amount = amount or 50
        local gemCost = math.floor(amount / 5)
        return self:GetProperty("Gems", 0) >= gemCost and 
               self:GetProperty("Energy", 0) < self:GetProperty("EnergyMax", 100)
    end)
    
    -- 切换调试信息命令
    self:CreateCommand("ToggleDebugCommand", function()
        return self.testModel:ToggleDebugInfo()
    end)
    
    -- 切换菜单命令
    self:CreateCommand("ToggleMenuCommand", function()
        return self.testModel:ToggleMenu()
    end)
    
    -- 重置数据命令
    self:CreateCommand("ResetDataCommand", function()
        self.testModel:ResetAllData()
        self:RefreshAllBindings()
        TDUIUtils.LogInfo("Data reset completed")
        return true
    end)
    
    -- 模拟数据变化命令（用于测试）
    self:CreateCommand("SimulateCommand", function()
        self.testModel:SimulateDataChanges()
        return true
    end)
end

-- 设置计算属性
function TDTestViewModel:SetupComputedProperties()
    -- 经验百分比
    self:DefineComputedProperty("ExpPercent", function()
        local exp = self:GetProperty("PlayerExp", 0)
        local maxExp = self:GetProperty("PlayerExpMax", 100)
        return maxExp > 0 and (exp / maxExp) or 0
    end, {"PlayerExp", "PlayerExpMax"})
    
    -- 能量百分比
    self:DefineComputedProperty("EnergyPercent", function()
        local energy = self:GetProperty("Energy", 0)
        local maxEnergy = self:GetProperty("EnergyMax", 100)
        return maxEnergy > 0 and (energy / maxEnergy) or 0
    end, {"Energy", "EnergyMax"})
    
    -- 玩家信息文本
    self:DefineComputedProperty("PlayerInfoText", function()
        local name = self:GetProperty("PlayerName", "Unknown")
        local level = self:GetProperty("PlayerLevel", 1)
        return string.format("%s (Lv.%d)", name, level)
    end, {"PlayerName", "PlayerLevel"})
    
    -- 经验信息文本
    self:DefineComputedProperty("ExpInfoText", function()
        local exp = self:GetProperty("PlayerExp", 0)
        local maxExp = self:GetProperty("PlayerExpMax", 100)
        return string.format("%d / %d", exp, maxExp)
    end, {"PlayerExp", "PlayerExpMax"})
    
    -- 能量信息文本
    self:DefineComputedProperty("EnergyInfoText", function()
        local energy = self:GetProperty("Energy", 0)
        local maxEnergy = self:GetProperty("EnergyMax", 100)
        return string.format("%d / %d", energy, maxEnergy)
    end, {"Energy", "EnergyMax"})
    
    -- 游戏时间文本
    self:DefineComputedProperty("GameTimeText", function()
        local time = self:GetProperty("GameTime", 0)
        local minutes = math.floor(time / 60)
        local seconds = math.floor(time % 60)
        return string.format("%02d:%02d", minutes, seconds)
    end, {"GameTime"})
    
    -- 游戏状态文本
    self:DefineComputedProperty("GameStatusText", function()
        local isPlaying = self:GetProperty("IsPlaying", false)
        local isPaused = self:GetProperty("IsPaused", false)
        
        if not isPlaying then
            return "Ready to Start"
        elseif isPaused then
            return "Paused"
        else
            return "Playing"
        end
    end, {"IsPlaying", "IsPaused"})
    
    -- 统计信息文本
    self:DefineComputedProperty("StatsText", function()
        local kills = self:GetProperty("TotalKills", 0)
        local damage = self:GetProperty("TotalDamage", 0)
        local avgDamage = kills > 0 and math.floor(damage / kills) or 0
        return string.format("Kills: %d | Damage: %d | Avg: %d", kills, damage, avgDamage)
    end, {"TotalKills", "TotalDamage"})
    
    -- 开始按钮文本
    self:DefineComputedProperty("StartButtonText", function()
        local isPlaying = self:GetProperty("IsPlaying", false)
        return isPlaying and "End Game" or "Start Game"
    end, {"IsPlaying"})
    
    -- 暂停按钮文本
    self:DefineComputedProperty("PauseButtonText", function()
        local isPaused = self:GetProperty("IsPaused", false)
        return isPaused and "Resume" or "Pause"
    end, {"IsPaused"})
    
    -- 调试按钮文本
    self:DefineComputedProperty("DebugButtonText", function()
        local showDebug = self:GetProperty("ShowDebugInfo", false)
        return showDebug and "Hide Debug" or "Show Debug"
    end, {"ShowDebugInfo"})
    
    -- 菜单按钮文本
    self:DefineComputedProperty("MenuButtonText", function()
        local isMenuOpen = self:GetProperty("IsMenuOpen", false)
        return isMenuOpen and "Close Menu" or "Open Menu"
    end, {"IsMenuOpen"})
    
    -- 能量不足警告
    self:DefineComputedProperty("LowEnergyWarning", function()
        local energy = self:GetProperty("Energy", 0)
        local maxEnergy = self:GetProperty("EnergyMax", 100)
        return energy < (maxEnergy * 0.2) -- 低于20%显示警告
    end, {"Energy", "EnergyMax"})
    
    -- 可以开始游戏
    self:DefineComputedProperty("CanStartGame", function()
        return not self:GetProperty("IsPlaying", false) and self:GetProperty("Energy", 0) >= 10
    end, {"IsPlaying", "Energy"})
    
    -- 可以购买金币
    self:DefineComputedProperty("CanBuyGold", function()
        return self:GetProperty("Gems", 0) >= 10 -- 需要至少10宝石
    end, {"Gems"})
    
    -- 可以恢复能量
    self:DefineComputedProperty("CanRestoreEnergy", function()
        local gems = self:GetProperty("Gems", 0)
        local energy = self:GetProperty("Energy", 0)
        local maxEnergy = self:GetProperty("EnergyMax", 100)
        return gems >= 10 and energy < maxEnergy
    end, {"Gems", "Energy", "EnergyMax"})
end

-- 重写初始化方法
function TDTestViewModel:OnInitialize()
    -- 执行初始化
    self:DoInitialize()
    
    -- 启动定时器模拟游戏更新
    self:StartGameUpdateTimer()
end

-- 启动游戏更新定时器
function TDTestViewModel:StartGameUpdateTimer()
    -- 这里可以使用UE的定时器系统
    -- 简化实现，实际项目中应该使用更合适的定时机制
    self.updateTimer = 0
    self.lastUpdateTime = os.clock()
end

-- 更新游戏逻辑（应该从外部调用）
function TDTestViewModel:UpdateGame(deltaTime)
    if not deltaTime then
        local currentTime = os.clock()
        deltaTime = currentTime - (self.lastUpdateTime or currentTime)
        self.lastUpdateTime = currentTime
    end
    
    -- 更新游戏时间
    if self:GetProperty("IsPlaying", false) and not self:GetProperty("IsPaused", false) then
        self.testModel:UpdateGameTime(deltaTime)
    end
    
    -- 定期恢复能量
    self.updateTimer = (self.updateTimer or 0) + deltaTime
    if self.updateTimer >= 5.0 then -- 每5秒恢复1点能量
        self.updateTimer = 0
        if not self:GetProperty("IsPlaying", false) then
            self.testModel:RestoreEnergy(1)
        end
    end
    
    -- 更新命令的CanExecute状态
    self:UpdateCommandStates()
end

-- 更新命令状态
function TDTestViewModel:UpdateCommandStates()
    local commands = {"StartGameCommand", "EndGameCommand", "TogglePauseCommand", 
                     "NextWaveCommand", "BuyGoldCommand", "RestoreEnergyCommand"}
    
    for _, commandName in ipairs(commands) do
        local command = self:GetCommand(commandName)
        if command then
            command:RaiseCanExecuteChanged()
        end
    end
end

-- 获取格式化的资源信息
function TDTestViewModel:GetFormattedResources()
    return {
        gold = string.format("%,d", self:GetProperty("Gold", 0)),
        gems = string.format("%,d", self:GetProperty("Gems", 0)),
        energy = string.format("%d/%d", self:GetProperty("Energy", 0), self:GetProperty("EnergyMax", 100))
    }
end

-- 获取游戏进度信息
function TDTestViewModel:GetGameProgress()
    local isPlaying = self:GetProperty("IsPlaying", false)
    if not isPlaying then
        return nil
    end
    
    return {
        score = self:GetProperty("GameScore", 0),
        wave = self:GetProperty("WaveNumber", 1),
        time = self:GetProperty("GameTimeText", "00:00"),
        kills = self:GetProperty("TotalKills", 0),
        isPaused = self:GetProperty("IsPaused", false)
    }
end

-- 执行购买操作
function TDTestViewModel:PurchaseGold(amount)
    return self:ExecuteCommand("BuyGoldCommand", amount)
end

-- 执行能量恢复
function TDTestViewModel:PurchaseEnergy(amount)
    return self:ExecuteCommand("RestoreEnergyCommand", amount)
end

-- 快速测试方法
function TDTestViewModel:QuickTest()
    TDUIUtils.LogInfo("=== Quick Test Started ===")
    
    -- 测试基本操作
    TDUIUtils.LogInfo("Player: %s", self:GetProperty("PlayerInfoText", "Unknown"))
    TDUIUtils.LogInfo("Resources: Gold=%d, Gems=%d, Energy=%s", 
                     self:GetProperty("Gold", 0),
                     self:GetProperty("Gems", 0),
                     self:GetProperty("EnergyInfoText", "0/0"))
    
    -- 测试命令
    if self:GetProperty("CanStartGame", false) then
        TDUIUtils.LogInfo("Starting game...")
        self:ExecuteCommand("StartGameCommand")
    else
        TDUIUtils.LogInfo("Cannot start game (not enough energy or already playing)")
    end
    
    -- 模拟一些游戏事件
    for i = 1, 5 do
        self.testModel:SimulateDataChanges()
    end
    
    TDUIUtils.LogInfo("Game Status: %s", self:GetProperty("GameStatusText", "Unknown"))
    TDUIUtils.LogInfo("Stats: %s", self:GetProperty("StatsText", "No stats"))
    
    TDUIUtils.LogInfo("=== Quick Test Completed ===")
end

-- 销毁ViewModel
function TDTestViewModel:Destroy()
    -- 停止定时器
    self.updateTimer = nil
    self.lastUpdateTime = nil
    
    -- 销毁模型
    if self.testModel then
        self.testModel:Destroy()
        self.testModel = nil
    end
    
    -- 调用父类销毁方法
    TDBaseViewModel.Destroy(self)
    
    TDUIUtils.LogInfo("TDTestViewModel destroyed")
end

return TDTestViewModel