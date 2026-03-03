-- TowerDefend UI Framework - Test Model
-- 测试数据模型，展示MVVM框架中Model层的使用

local TDTestModel = UnLua.Class()

-- 导入依赖
local TDBaseModel = require("UI.Framework.TDBaseModel")
local TDUIUtils = require("UI.Common.TDUIUtils")

-- 继承基类
setmetatable(TDTestModel, {__index = TDBaseModel})

-- 构造函数
function TDTestModel:Initialize()
    -- 调用父类构造函数
    TDBaseModel.Initialize(self)
    
    -- 初始化测试数据
    self:InitializeTestData()
    
    -- 添加验证规则
    self:SetupValidationRules()
    
    TDUIUtils.LogInfo("TDTestModel initialized")
end

-- 初始化测试数据
function TDTestModel:InitializeTestData()
    -- 玩家基本信息
    self:SetProperty("PlayerName", "TestPlayer", true)
    self:SetProperty("PlayerLevel", 1, true)
    self:SetProperty("PlayerExp", 0, true)
    self:SetProperty("PlayerExpMax", 100, true)
    
    -- 资源信息
    self:SetProperty("Gold", 1000, true)
    self:SetProperty("Gems", 50, true)
    self:SetProperty("Energy", 100, true)
    self:SetProperty("EnergyMax", 100, true)
    
    -- 游戏状态
    self:SetProperty("IsPlaying", false, true)
    self:SetProperty("GameScore", 0, true)
    self:SetProperty("GameTime", 0, true)
    self:SetProperty("WaveNumber", 1, true)
    
    -- UI状态
    self:SetProperty("ShowDebugInfo", false, true)
    self:SetProperty("IsPaused", false, true)
    self:SetProperty("IsMenuOpen", false, true)
    
    -- 统计信息
    self:SetProperty("TotalKills", 0, true)
    self:SetProperty("TotalDamage", 0, true)
    self:SetProperty("BestScore", 0, true)
end

-- 设置验证规则
function TDTestModel:SetupValidationRules()
    -- 玩家等级验证
    self:AddValidationRule("PlayerLevel", function(value)
        return type(value) == "number" and value >= 1 and value <= 100
    end, "Player level must be between 1 and 100")
    
    -- 经验值验证
    self:AddValidationRule("PlayerExp", function(value)
        return type(value) == "number" and value >= 0
    end, "Player experience must be non-negative")
    
    -- 资源验证
    self:AddValidationRule("Gold", function(value)
        return type(value) == "number" and value >= 0
    end, "Gold must be non-negative")
    
    self:AddValidationRule("Gems", function(value)
        return type(value) == "number" and value >= 0
    end, "Gems must be non-negative")
    
    -- 能量验证
    self:AddValidationRule("Energy", function(value)
        local maxEnergy = self:GetProperty("EnergyMax", 100)
        return type(value) == "number" and value >= 0 and value <= maxEnergy
    end, "Energy must be between 0 and max energy")
end

-- 业务逻辑方法

-- 增加经验值
function TDTestModel:AddExperience(amount)
    if not amount or amount <= 0 then
        return false
    end
    
    local currentExp = self:GetProperty("PlayerExp", 0)
    local maxExp = self:GetProperty("PlayerExpMax", 100)
    local currentLevel = self:GetProperty("PlayerLevel", 1)
    
    local newExp = currentExp + amount
    
    -- 检查是否升级
    while newExp >= maxExp and currentLevel < 100 do
        newExp = newExp - maxExp
        currentLevel = currentLevel + 1
        maxExp = maxExp + 50 -- 每级增加50经验需求
        
        TDUIUtils.LogInfo("Player leveled up to %d!", currentLevel)
        
        -- 升级奖励
        self:SetProperty("Gold", self:GetProperty("Gold", 0) + currentLevel * 10)
    end
    
    -- 更新属性
    self:SetProperty("PlayerLevel", currentLevel)
    self:SetProperty("PlayerExp", newExp)
    self:SetProperty("PlayerExpMax", maxExp)
    
    return true
end

-- 消费金币
function TDTestModel:SpendGold(amount)
    if not amount or amount <= 0 then
        return false
    end
    
    local currentGold = self:GetProperty("Gold", 0)
    if currentGold < amount then
        TDUIUtils.LogWarn("Not enough gold! Need %d, have %d", amount, currentGold)
        return false
    end
    
    self:SetProperty("Gold", currentGold - amount)
    return true
end

-- 消费宝石
function TDTestModel:SpendGems(amount)
    if not amount or amount <= 0 then
        return false
    end
    
    local currentGems = self:GetProperty("Gems", 0)
    if currentGems < amount then
        TDUIUtils.LogWarn("Not enough gems! Need %d, have %d", amount, currentGems)
        return false
    end
    
    self:SetProperty("Gems", currentGems - amount)
    return true
end

-- 消费能量
function TDTestModel:SpendEnergy(amount)
    if not amount or amount <= 0 then
        return false
    end
    
    local currentEnergy = self:GetProperty("Energy", 0)
    if currentEnergy < amount then
        TDUIUtils.LogWarn("Not enough energy! Need %d, have %d", amount, currentEnergy)
        return false
    end
    
    self:SetProperty("Energy", currentEnergy - amount)
    return true
end

-- 恢复能量
function TDTestModel:RestoreEnergy(amount)
    if not amount or amount <= 0 then
        return false
    end
    
    local currentEnergy = self:GetProperty("Energy", 0)
    local maxEnergy = self:GetProperty("EnergyMax", 100)
    local newEnergy = math.min(maxEnergy, currentEnergy + amount)
    
    self:SetProperty("Energy", newEnergy)
    return true
end

-- 开始游戏
function TDTestModel:StartGame()
    if self:GetProperty("IsPlaying", false) then
        TDUIUtils.LogWarn("Game is already running")
        return false
    end
    
    -- 消费能量
    if not self:SpendEnergy(10) then
        return false
    end
    
    -- 重置游戏状态
    self:SetProperty("IsPlaying", true)
    self:SetProperty("GameScore", 0)
    self:SetProperty("GameTime", 0)
    self:SetProperty("WaveNumber", 1)
    self:SetProperty("TotalKills", 0)
    self:SetProperty("TotalDamage", 0)
    self:SetProperty("IsPaused", false)
    
    TDUIUtils.LogInfo("Game started!")
    return true
end

-- 结束游戏
function TDTestModel:EndGame(score)
    if not self:GetProperty("IsPlaying", false) then
        return false
    end
    
    score = score or self:GetProperty("GameScore", 0)
    
    -- 更新最佳分数
    local bestScore = self:GetProperty("BestScore", 0)
    if score > bestScore then
        self:SetProperty("BestScore", score)
        TDUIUtils.LogInfo("New best score: %d!", score)
    end
    
    -- 计算奖励
    local expReward = math.floor(score / 10)
    local goldReward = math.floor(score / 5)
    
    -- 给予奖励
    self:AddExperience(expReward)
    self:SetProperty("Gold", self:GetProperty("Gold", 0) + goldReward)
    
    -- 重置游戏状态
    self:SetProperty("IsPlaying", false)
    self:SetProperty("IsPaused", false)
    
    TDUIUtils.LogInfo("Game ended! Score: %d, Exp: +%d, Gold: +%d", score, expReward, goldReward)
    return true
end

-- 暂停/恢复游戏
function TDTestModel:TogglePause()
    if not self:GetProperty("IsPlaying", false) then
        return false
    end
    
    local isPaused = self:GetProperty("IsPaused", false)
    self:SetProperty("IsPaused", not isPaused)
    
    TDUIUtils.LogInfo("Game %s", isPaused and "resumed" or "paused")
    return true
end

-- 增加分数
function TDTestModel:AddScore(points)
    if not self:GetProperty("IsPlaying", false) then
        return false
    end
    
    if not points or points <= 0 then
        return false
    end
    
    local currentScore = self:GetProperty("GameScore", 0)
    self:SetProperty("GameScore", currentScore + points)
    
    return true
end

-- 增加击杀数
function TDTestModel:AddKill(damage)
    if not self:GetProperty("IsPlaying", false) then
        return false
    end
    
    local currentKills = self:GetProperty("TotalKills", 0)
    local currentDamage = self:GetProperty("TotalDamage", 0)
    
    self:SetProperty("TotalKills", currentKills + 1)
    
    if damage and damage > 0 then
        self:SetProperty("TotalDamage", currentDamage + damage)
    end
    
    -- 增加分数
    self:AddScore(10)
    
    return true
end

-- 下一波
function TDTestModel:NextWave()
    if not self:GetProperty("IsPlaying", false) then
        return false
    end
    
    local currentWave = self:GetProperty("WaveNumber", 1)
    self:SetProperty("WaveNumber", currentWave + 1)
    
    -- 波数奖励
    self:AddScore(currentWave * 50)
    
    TDUIUtils.LogInfo("Wave %d completed! Next wave: %d", currentWave, currentWave + 1)
    return true
end

-- 更新游戏时间
function TDTestModel:UpdateGameTime(deltaTime)
    if not self:GetProperty("IsPlaying", false) or self:GetProperty("IsPaused", false) then
        return false
    end
    
    local currentTime = self:GetProperty("GameTime", 0)
    self:SetProperty("GameTime", currentTime + deltaTime)
    
    return true
end

-- 切换调试信息显示
function TDTestModel:ToggleDebugInfo()
    local showDebug = self:GetProperty("ShowDebugInfo", false)
    self:SetProperty("ShowDebugInfo", not showDebug)
    
    TDUIUtils.LogInfo("Debug info %s", showDebug and "hidden" or "shown")
    return true
end

-- 切换菜单显示
function TDTestModel:ToggleMenu()
    local isMenuOpen = self:GetProperty("IsMenuOpen", false)
    self:SetProperty("IsMenuOpen", not isMenuOpen)
    
    return true
end

-- 获取玩家信息摘要
function TDTestModel:GetPlayerSummary()
    return {
        name = self:GetProperty("PlayerName", "Unknown"),
        level = self:GetProperty("PlayerLevel", 1),
        exp = self:GetProperty("PlayerExp", 0),
        expMax = self:GetProperty("PlayerExpMax", 100),
        gold = self:GetProperty("Gold", 0),
        gems = self:GetProperty("Gems", 0),
        energy = self:GetProperty("Energy", 0),
        energyMax = self:GetProperty("EnergyMax", 100),
        bestScore = self:GetProperty("BestScore", 0)
    }
end

-- 获取游戏状态摘要
function TDTestModel:GetGameSummary()
    return {
        isPlaying = self:GetProperty("IsPlaying", false),
        isPaused = self:GetProperty("IsPaused", false),
        score = self:GetProperty("GameScore", 0),
        time = self:GetProperty("GameTime", 0),
        wave = self:GetProperty("WaveNumber", 1),
        kills = self:GetProperty("TotalKills", 0),
        damage = self:GetProperty("TotalDamage", 0)
    }
end

-- 重置所有数据
function TDTestModel:ResetAllData()
    self:Reset()
    self:InitializeTestData()
    self:SetupValidationRules()
    
    TDUIUtils.LogInfo("All data reset to default values")
end

-- 模拟数据变化（用于测试）
function TDTestModel:SimulateDataChanges()
    -- 模拟游戏进行
    if not self:GetProperty("IsPlaying", false) then
        self:StartGame()
    end
    
    -- 随机事件
    local events = {
        function() self:AddKill(math.random(50, 200)) end,
        function() self:AddScore(math.random(10, 100)) end,
        function() self:UpdateGameTime(1.0) end,
        function() 
            if math.random() > 0.8 then
                self:NextWave()
            end
        end,
        function()
            if math.random() > 0.9 then
                self:AddExperience(math.random(5, 20))
            end
        end
    }
    
    -- 执行随机事件
    local event = events[math.random(#events)]
    event()
end

return TDTestModel