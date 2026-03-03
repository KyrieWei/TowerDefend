-- TowerDefend UI Framework - Test View
-- 测试视图，展示MVVM框架中View层的使用和数据绑定

local TDTestView = UnLua.Class()

-- 导入依赖
local TDBaseView = require("UI.Framework.TDBaseView")
local TDTestViewModel = require("UI.Examples.TDTestViewModel")
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDUIDefines = require("UI.Common.TDUIDefines")
local TDDataBinding = require("UI.Framework.TDDataBinding")

-- 继承基类
setmetatable(TDTestView, {__index = TDBaseView})

-- 构造函数
function TDTestView:Initialize()
    -- 调用父类构造函数
    TDBaseView.Initialize(self)
    
    -- 创建ViewModel
    self.testViewModel = TDTestViewModel()
    
    TDUIUtils.LogInfo("TDTestView initialized")
end

-- 重写初始化方法
function TDTestView:OnInitialize()
    -- 设置ViewModel
    self:SetViewModel(self.testViewModel)
    
    -- 初始化ViewModel
    self.testViewModel:OnInitialize()
    
    TDUIUtils.LogInfo("TDTestView OnInitialize completed")
end

-- 重写ViewModel绑定方法
function TDTestView:OnBindViewModel()
    if not self.viewModel then
        TDUIUtils.LogError("OnBindViewModel: ViewModel is nil")
        return
    end
    
    -- 设置Widget绑定
    self:SetupWidgetBindings()
    
    -- 设置命令绑定
    self:SetupCommandBindings()
    
    -- 设置事件监听
    self:SetupEventListeners()
    
    TDUIUtils.LogInfo("ViewModel bound to TDTestView")
end

-- 设置Widget绑定
function TDTestView:SetupWidgetBindings()
    -- 玩家信息绑定
    self:BindWidgetProperty("PlayerInfoText", "Text", "PlayerInfoText")
    self:BindWidgetProperty("PlayerLevelText", "Text", "PlayerLevel", function(value)
        return string.format("Level %d", value or 1)
    end)
    
    -- 经验条绑定
    self:BindWidgetProperty("ExpProgressBar", "Percent", "ExpPercent")
    self:BindWidgetProperty("ExpInfoText", "Text", "ExpInfoText")
    
    -- 资源信息绑定
    self:BindWidgetProperty("GoldText", "Text", "Gold", function(value)
        return string.format("Gold: %,d", value or 0)
    end)
    self:BindWidgetProperty("GemsText", "Text", "Gems", function(value)
        return string.format("Gems: %,d", value or 0)
    end)
    
    -- 能量条绑定
    self:BindWidgetProperty("EnergyProgressBar", "Percent", "EnergyPercent")
    self:BindWidgetProperty("EnergyInfoText", "Text", "EnergyInfoText")
    
    -- 游戏状态绑定
    self:BindWidgetProperty("GameStatusText", "Text", "GameStatusText")
    self:BindWidgetProperty("GameScoreText", "Text", "GameScore", function(value)
        return string.format("Score: %,d", value or 0)
    end)
    self:BindWidgetProperty("GameTimeText", "Text", "GameTimeText")
    self:BindWidgetProperty("WaveNumberText", "Text", "WaveNumber", function(value)
        return string.format("Wave %d", value or 1)
    end)
    
    -- 统计信息绑定
    self:BindWidgetProperty("StatsText", "Text", "StatsText")
    self:BindWidgetProperty("BestScoreText", "Text", "BestScore", function(value)
        return string.format("Best: %,d", value or 0)
    end)
    
    -- 按钮文本绑定
    self:BindWidgetProperty("StartButton", "Text", "StartButtonText")
    self:BindWidgetProperty("PauseButton", "Text", "PauseButtonText")
    self:BindWidgetProperty("DebugButton", "Text", "DebugButtonText")
    self:BindWidgetProperty("MenuButton", "Text", "MenuButtonText")
    
    -- 按钮启用状态绑定
    self:BindWidgetProperty("StartButton", "IsEnabled", "CanStartGame")
    self:BindWidgetProperty("PauseButton", "IsEnabled", "IsPlaying")
    self:BindWidgetProperty("NextWaveButton", "IsEnabled", "IsPlaying")
    self:BindWidgetProperty("BuyGoldButton", "IsEnabled", "CanBuyGold")
    self:BindWidgetProperty("RestoreEnergyButton", "IsEnabled", "CanRestoreEnergy")
    
    -- 可见性绑定
    self:BindWidgetProperty("GamePanel", "Visibility", "IsPlaying", 
        TDDataBinding.Converters.BooleanToVisibility)
    self:BindWidgetProperty("MenuPanel", "Visibility", "IsMenuOpen", 
        TDDataBinding.Converters.BooleanToVisibility)
    self:BindWidgetProperty("DebugPanel", "Visibility", "ShowDebugInfo", 
        TDDataBinding.Converters.BooleanToVisibility)
    self:BindWidgetProperty("LowEnergyWarning", "Visibility", "LowEnergyWarning", 
        TDDataBinding.Converters.BooleanToVisibility)
    
    -- 暂停遮罩
    self:BindWidgetProperty("PauseOverlay", "Visibility", "IsPaused", 
        TDDataBinding.Converters.BooleanToVisibility)
    
    TDUIUtils.LogDebug("Widget bindings setup completed")
end

-- 设置命令绑定
function TDTestView:SetupCommandBindings()
    -- 主要游戏控制按钮
    self:BindCommand("StartButton", "OnClicked", "StartGameCommand")
    self:BindCommand("PauseButton", "OnClicked", "TogglePauseCommand")
    self:BindCommand("NextWaveButton", "OnClicked", "NextWaveCommand")
    
    -- UI控制按钮
    self:BindCommand("DebugButton", "OnClicked", "ToggleDebugCommand")
    self:BindCommand("MenuButton", "OnClicked", "ToggleMenuCommand")
    
    -- 购买按钮
    self:BindCommand("BuyGoldButton", "OnClicked", "BuyGoldCommand", 100)
    self:BindCommand("RestoreEnergyButton", "OnClicked", "RestoreEnergyCommand", 50)
    
    -- 测试和重置按钮
    self:BindCommand("SimulateButton", "OnClicked", "SimulateCommand")
    self:BindCommand("ResetButton", "OnClicked", "ResetDataCommand")
    
    -- 快捷键绑定（如果支持）
    -- 这里可以添加键盘快捷键的绑定
    
    TDUIUtils.LogDebug("Command bindings setup completed")
end

-- 设置事件监听
function TDTestView:SetupEventListeners()
    -- 监听ViewModel的特定事件
    if self.viewModel then
        -- 监听游戏状态变化
        self.viewModel:OnSpecificPropertyChanged("IsPlaying", function(newValue, oldValue)
            self:OnGameStateChanged(newValue, oldValue)
        end, self)
        
        -- 监听暂停状态变化
        self.viewModel:OnSpecificPropertyChanged("IsPaused", function(newValue, oldValue)
            self:OnPauseStateChanged(newValue, oldValue)
        end, self)
        
        -- 监听等级变化
        self.viewModel:OnSpecificPropertyChanged("PlayerLevel", function(newValue, oldValue)
            self:OnPlayerLevelChanged(newValue, oldValue)
        end, self)
        
        -- 监听分数变化
        self.viewModel:OnSpecificPropertyChanged("GameScore", function(newValue, oldValue)
            self:OnScoreChanged(newValue, oldValue)
        end, self)
        
        -- 监听能量变化
        self.viewModel:OnSpecificPropertyChanged("Energy", function(newValue, oldValue)
            self:OnEnergyChanged(newValue, oldValue)
        end, self)
    end
    
    TDUIUtils.LogDebug("Event listeners setup completed")
end

-- 游戏状态变化处理
function TDTestView:OnGameStateChanged(isPlaying, wasPlaying)
    TDUIUtils.LogInfo("Game state changed: %s -> %s", 
                     tostring(wasPlaying), tostring(isPlaying))
    
    if isPlaying and not wasPlaying then
        -- 游戏开始
        self:PlayGameStartAnimation()
        self:ShowGameStartMessage()
    elseif not isPlaying and wasPlaying then
        -- 游戏结束
        self:PlayGameEndAnimation()
        self:ShowGameEndMessage()
    end
end

-- 暂停状态变化处理
function TDTestView:OnPauseStateChanged(isPaused, wasPaused)
    TDUIUtils.LogInfo("Pause state changed: %s -> %s", 
                     tostring(wasPaused), tostring(isPaused))
    
    if isPaused then
        self:ShowPauseEffect()
    else
        self:HidePauseEffect()
    end
end

-- 玩家等级变化处理
function TDTestView:OnPlayerLevelChanged(newLevel, oldLevel)
    if newLevel and oldLevel and newLevel > oldLevel then
        TDUIUtils.LogInfo("Player leveled up: %d -> %d", oldLevel, newLevel)
        self:ShowLevelUpEffect(newLevel)
    end
end

-- 分数变化处理
function TDTestView:OnScoreChanged(newScore, oldScore)
    if newScore and oldScore and newScore > oldScore then
        local scoreIncrease = newScore - oldScore
        if scoreIncrease >= 100 then -- 大幅分数增加时显示特效
            self:ShowScoreIncreaseEffect(scoreIncrease)
        end
    end
end

-- 能量变化处理
function TDTestView:OnEnergyChanged(newEnergy, oldEnergy)
    local maxEnergy = self.viewModel:GetProperty("EnergyMax", 100)
    local lowEnergyThreshold = maxEnergy * 0.2
    
    if newEnergy and newEnergy <= lowEnergyThreshold and 
       (not oldEnergy or oldEnergy > lowEnergyThreshold) then
        self:ShowLowEnergyWarning()
    end
end

-- 动画和特效方法

-- 播放游戏开始动画
function TDTestView:PlayGameStartAnimation()
    -- 这里可以实现具体的动画效果
    TDUIUtils.LogDebug("Playing game start animation")
    
    -- 示例：淡入游戏面板
    local gamePanel = self:FindWidget("GamePanel")
    if TDUIUtils.IsValidWidget(gamePanel) then
        -- 可以使用UE的动画系统
        -- gamePanel:PlayAnimation(self.GameStartAnimation)
    end
end

-- 播放游戏结束动画
function TDTestView:PlayGameEndAnimation()
    TDUIUtils.LogDebug("Playing game end animation")
    
    -- 示例：淡出游戏面板
    local gamePanel = self:FindWidget("GamePanel")
    if TDUIUtils.IsValidWidget(gamePanel) then
        -- gamePanel:PlayAnimation(self.GameEndAnimation)
    end
end

-- 显示暂停效果
function TDTestView:ShowPauseEffect()
    TDUIUtils.LogDebug("Showing pause effect")
    
    -- 可以添加模糊效果、暗化等
    local pauseOverlay = self:FindWidget("PauseOverlay")
    if TDUIUtils.IsValidWidget(pauseOverlay) then
        pauseOverlay:SetVisibility(UE.ESlateVisibility.Visible)
    end
end

-- 隐藏暂停效果
function TDTestView:HidePauseEffect()
    TDUIUtils.LogDebug("Hiding pause effect")
    
    local pauseOverlay = self:FindWidget("PauseOverlay")
    if TDUIUtils.IsValidWidget(pauseOverlay) then
        pauseOverlay:SetVisibility(UE.ESlateVisibility.Collapsed)
    end
end

-- 显示升级特效
function TDTestView:ShowLevelUpEffect(newLevel)
    TDUIUtils.LogInfo("Showing level up effect for level %d", newLevel)
    
    -- 可以播放升级动画、音效等
    -- 显示升级提示文本
    self:ShowTemporaryMessage(string.format("Level Up! Now Level %d", newLevel), 3.0)
end

-- 显示分数增加特效
function TDTestView:ShowScoreIncreaseEffect(scoreIncrease)
    TDUIUtils.LogDebug("Showing score increase effect: +%d", scoreIncrease)
    
    -- 可以显示飞出的分数文本动画
    self:ShowTemporaryMessage(string.format("+%d Score!", scoreIncrease), 2.0)
end

-- 显示低能量警告
function TDTestView:ShowLowEnergyWarning()
    TDUIUtils.LogWarn("Showing low energy warning")
    
    -- 可以播放警告动画、音效等
    local warningWidget = self:FindWidget("LowEnergyWarning")
    if TDUIUtils.IsValidWidget(warningWidget) then
        -- 可以添加闪烁效果
        warningWidget:SetVisibility(UE.ESlateVisibility.Visible)
    end
end

-- 显示游戏开始消息
function TDTestView:ShowGameStartMessage()
    self:ShowTemporaryMessage("Game Started! Good Luck!", 2.0)
end

-- 显示游戏结束消息
function TDTestView:ShowGameEndMessage()
    if self.viewModel then
        local score = self.viewModel:GetProperty("GameScore", 0)
        local bestScore = self.viewModel:GetProperty("BestScore", 0)
        
        local message = string.format("Game Over! Score: %d", score)
        if score >= bestScore then
            message = message .. " (New Best!)"
        end
        
        self:ShowTemporaryMessage(message, 3.0)
    end
end

-- 显示临时消息
function TDTestView:ShowTemporaryMessage(message, duration)
    TDUIUtils.LogInfo("Message: %s", message)
    
    -- 这里可以实现消息显示逻辑
    -- 例如更新MessageText widget并设置定时器隐藏
    local messageWidget = self:FindWidget("MessageText")
    if TDUIUtils.IsValidWidget(messageWidget) then
        messageWidget:SetText(TDUIUtils.ToFText(message))
        messageWidget:SetVisibility(UE.ESlateVisibility.Visible)
        
        -- 设置定时器隐藏消息
        -- 这里需要使用UE的定时器系统
        -- UE.UKismetSystemLibrary.SetTimer(self, "HideMessage", duration or 2.0, false)
    end
end

-- 隐藏消息
function TDTestView:HideMessage()
    local messageWidget = self:FindWidget("MessageText")
    if TDUIUtils.IsValidWidget(messageWidget) then
        messageWidget:SetVisibility(UE.ESlateVisibility.Collapsed)
    end
end

-- 更新游戏逻辑（从外部调用）
function TDTestView:UpdateGame(deltaTime)
    if self.testViewModel then
        self.testViewModel:UpdateGame(deltaTime)
    end
end

-- 处理输入事件
function TDTestView:HandleInput(inputKey, inputEvent)
    if inputEvent ~= UE.EInputEvent.IE_Pressed then
        return false
    end
    
    -- 快捷键处理
    if inputKey == UE.EKeys.SpaceBar then
        -- 空格键：开始/暂停游戏
        if self.viewModel:GetProperty("IsPlaying", false) then
            self.viewModel:ExecuteCommand("TogglePauseCommand")
        else
            self.viewModel:ExecuteCommand("StartGameCommand")
        end
        return true
    elseif inputKey == UE.EKeys.Escape then
        -- ESC键：切换菜单
        self.viewModel:ExecuteCommand("ToggleMenuCommand")
        return true
    elseif inputKey == UE.EKeys.F1 then
        -- F1键：切换调试信息
        self.viewModel:ExecuteCommand("ToggleDebugCommand")
        return true
    elseif inputKey == UE.EKeys.F5 then
        -- F5键：重置数据
        self.viewModel:ExecuteCommand("ResetDataCommand")
        return true
    elseif inputKey == UE.EKeys.F9 then
        -- F9键：模拟数据变化
        self.viewModel:ExecuteCommand("SimulateCommand")
        return true
    end
    
    return false
end

-- 重写解绑ViewModel方法
function TDTestView:OnUnbindViewModel()
    -- 移除事件监听
    if self.viewModel then
        self.viewModel:RemovePropertyChangedListener(self.OnGameStateChanged, self)
        self.viewModel:RemovePropertyChangedListener(self.OnPauseStateChanged, self)
        self.viewModel:RemovePropertyChangedListener(self.OnPlayerLevelChanged, self)
        self.viewModel:RemovePropertyChangedListener(self.OnScoreChanged, self)
        self.viewModel:RemovePropertyChangedListener(self.OnEnergyChanged, self)
    end
    
    TDUIUtils.LogDebug("ViewModel unbound from TDTestView")
end

-- 获取调试信息
function TDTestView:GetDebugInfo()
    if not self.viewModel then
        return "No ViewModel"
    end
    
    local info = {
        "=== TDTestView Debug Info ===",
        string.format("UI State: %d", self:GetUIState()),
        string.format("Widget Valid: %s", tostring(TDUIUtils.IsValidWidget(self.widget))),
        string.format("ViewModel: %s", tostring(self.viewModel ~= nil)),
        "",
        "=== Game State ===",
        string.format("Playing: %s", tostring(self.viewModel:GetProperty("IsPlaying", false))),
        string.format("Paused: %s", tostring(self.viewModel:GetProperty("IsPaused", false))),
        string.format("Score: %d", self.viewModel:GetProperty("GameScore", 0)),
        string.format("Wave: %d", self.viewModel:GetProperty("WaveNumber", 1)),
        "",
        "=== Resources ===",
        string.format("Gold: %d", self.viewModel:GetProperty("Gold", 0)),
        string.format("Gems: %d", self.viewModel:GetProperty("Gems", 0)),
        string.format("Energy: %s", self.viewModel:GetProperty("EnergyInfoText", "0/0")),
        "",
        "=== Bindings ===",
        string.format("Widget Bindings: %d", #self.widgetBindings),
        string.format("Command Bindings: %d", #self.commandBindings)
    }
    
    return table.concat(info, "\n")
end

-- 快速测试方法
function TDTestView:QuickTest()
    TDUIUtils.LogInfo("=== TDTestView Quick Test ===")
    
    if self.testViewModel then
        self.testViewModel:QuickTest()
    end
    
    -- 测试UI绑定
    TDUIUtils.LogInfo("Testing UI bindings...")
    self:RefreshAllBindings()
    
    -- 输出调试信息
    TDUIUtils.LogInfo("Debug Info:\n%s", self:GetDebugInfo())
    
    TDUIUtils.LogInfo("=== Quick Test Completed ===")
end

-- 销毁View
function TDTestView:Destroy()
    -- 销毁ViewModel
    if self.testViewModel then
        self.testViewModel:Destroy()
        self.testViewModel = nil
    end
    
    -- 调用父类销毁方法
    TDBaseView.Destroy(self)
    
    TDUIUtils.LogInfo("TDTestView destroyed")
end

return TDTestView