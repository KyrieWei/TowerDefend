-- TowerDefend UI Framework - Initialization
-- MVVM框架初始化文件，提供统一的入口和初始化

local TDUIInit = {}

-- 导入核心模块
local TDUIManager = require("UI.Framework.TDUIManager")
local TDBaseView = require("UI.Framework.TDBaseView")
local TDBaseViewModel = require("UI.Framework.TDBaseViewModel")
local TDBaseModel = require("UI.Framework.TDBaseModel")
local TDDataBinding = require("UI.Framework.TDDataBinding")
local TDEventSystem = require("UI.Framework.TDEventSystem")

-- 导入工具模块
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDUIDefines = require("UI.Common.TDUIDefines")

-- 导入示例模块
local TDTestView = require("UI.Examples.TDTestView")
local TDTestViewModel = require("UI.Examples.TDTestViewModel")
local TDTestModel = require("UI.Examples.TDTestModel")

-- 框架版本信息
TDUIInit.Version = {
    Major = 1,
    Minor = 0,
    Patch = 0,
    Build = "Alpha"
}

-- 框架配置
TDUIInit.Config = {
    EnableLogging = true,
    EnableCache = true,
    MaxCacheCount = 10,
    EnableDebugMode = true,
    AutoRegisterExamples = true
}

-- 初始化状态
local isInitialized = false
local uiManager = nil

-- 获取版本字符串
function TDUIInit.GetVersionString()
    local v = TDUIInit.Version
    return string.format("%d.%d.%d-%s", v.Major, v.Minor, v.Patch, v.Build)
end

-- 初始化框架
function TDUIInit.Initialize(config)
    if isInitialized then
        TDUIUtils.LogWarn("TDUIFramework already initialized")
        return true
    end
    
    TDUIUtils.LogInfo("Initializing TowerDefend UI Framework v%s", TDUIInit.GetVersionString())
    
    -- 合并配置
    if config then
        TDUIInit.Config = TDUIUtils.MergeTable(TDUIInit.Config, config)
    end
    
    -- 初始化日志系统
    TDUIInit.InitializeLogging()
    
    -- 初始化事件系统
    TDUIInit.InitializeEventSystem()
    
    -- 初始化UI管理器
    TDUIInit.InitializeUIManager()
    
    -- 注册示例UI（如果启用）
    if TDUIInit.Config.AutoRegisterExamples then
        TDUIInit.RegisterExampleUIs()
    end
    
    -- 设置全局访问
    TDUIInit.SetupGlobalAccess()
    
    isInitialized = true
    
    TDUIUtils.LogInfo("TowerDefend UI Framework initialized successfully")
    
    -- 触发初始化完成事件
    TDEventSystem.Publish(TDEventSystem.Events.SYSTEM_INFO, "TDUIFramework", "Initialized")
    
    return true
end

-- 初始化日志系统
function TDUIInit.InitializeLogging()
    -- 这里可以配置日志级别、输出目标等
    TDUIUtils.LogInfo("Logging system initialized")
end

-- 初始化事件系统
function TDUIInit.InitializeEventSystem()
    local globalBus = TDEventSystem.GetGlobalEventBus()
    globalBus:SetLoggingEnabled(TDUIInit.Config.EnableLogging)
    globalBus:SetMaxQueueSize(1000)
    
    TDUIUtils.LogInfo("Event system initialized")
end

-- 初始化UI管理器
function TDUIInit.InitializeUIManager()
    uiManager = TDUIManager.GetInstance()
    
    -- 配置UI管理器
    uiManager:SetCacheEnabled(TDUIInit.Config.EnableCache)
    uiManager:SetMaxCacheCount(TDUIInit.Config.MaxCacheCount)
    
    -- 设置默认PlayerController
    local playerController = UE.UGameplayStatics.GetPlayerController(uiManager, 0)
    if playerController then
        uiManager:SetDefaultPlayerController(playerController)
    end
    
    TDUIUtils.LogInfo("UI Manager initialized")
end

-- 注册示例UI
function TDUIInit.RegisterExampleUIs()
    if not uiManager then
        TDUIUtils.LogError("RegisterExampleUIs: UI Manager not initialized")
        return false
    end
    
    -- 注册测试UI
    -- 注意：这里需要实际的Widget Blueprint类
    -- 在实际项目中，应该使用真实的Widget Blueprint路径
    local testWidgetClass = "/Game/TowerDefend/UI/Test/WBP_TDTestUI.WBP_TDTestUI_C"
    
    local success = uiManager:RegisterUI("TestUI", TDTestView, testWidgetClass, {
        layer = TDUIDefines.UILayer.Normal,
        singleton = true,
        cacheable = true,
        showAnimation = TDUIDefines.AnimationType.Fade,
        hideAnimation = TDUIDefines.AnimationType.Fade,
        autoFocus = true,
        modal = false,
        closeOnEscape = true
    })
    
    if success then
        TDUIUtils.LogInfo("Example UIs registered successfully")
    else
        TDUIUtils.LogWarn("Failed to register example UIs (Widget Blueprint may not exist)")
    end
    
    return success
end

-- 设置全局访问
function TDUIInit.SetupGlobalAccess()
    -- 将框架模块设置为全局可访问
    _G.TDUIFramework = {
        UIManager = uiManager,
        EventSystem = TDEventSystem,
        DataBinding = TDDataBinding,
        Utils = TDUIUtils,
        Defines = TDUIDefines,
        
        -- 基类
        BaseView = TDBaseView,
        BaseViewModel = TDBaseViewModel,
        BaseModel = TDBaseModel,
        
        -- 示例类
        TestView = TDTestView,
        TestViewModel = TDTestViewModel,
        TestModel = TDTestModel,
        
        -- 版本信息
        Version = TDUIInit.GetVersionString(),
        Config = TDUIInit.Config
    }
    
    TDUIUtils.LogInfo("Global access setup completed")
end

-- 关闭框架
function TDUIInit.Shutdown()
    if not isInitialized then
        return
    end
    
    TDUIUtils.LogInfo("Shutting down TowerDefend UI Framework")
    
    -- 关闭UI管理器
    if uiManager then
        uiManager:Destroy()
        uiManager = nil
    end
    
    -- 清理全局访问
    _G.TDUIFramework = nil
    
    -- 清理事件系统
    TDEventSystem.GetGlobalEventBus():Clear()
    
    isInitialized = false
    
    TDUIUtils.LogInfo("TowerDefend UI Framework shutdown completed")
end

-- 获取UI管理器
function TDUIInit.GetUIManager()
    return uiManager
end

-- 检查是否已初始化
function TDUIInit.IsInitialized()
    return isInitialized
end

-- 快速启动测试UI
function TDUIInit.QuickStartTest()
    if not isInitialized then
        TDUIUtils.LogError("QuickStartTest: Framework not initialized")
        return false
    end
    
    if not uiManager then
        TDUIUtils.LogError("QuickStartTest: UI Manager not available")
        return false
    end
    
    TDUIUtils.LogInfo("Starting quick test...")
    
    -- 尝试显示测试UI
    local testView = uiManager:ShowUI("TestUI")
    if testView then
        TDUIUtils.LogInfo("Test UI shown successfully")
        
        -- 执行快速测试
        if testView.QuickTest then
            testView:QuickTest()
        end
        
        return true
    else
        TDUIUtils.LogWarn("Failed to show test UI (Widget Blueprint may not exist)")
        
        -- 创建纯代码测试
        TDUIInit.RunCodeOnlyTest()
        return false
    end
end

-- 运行纯代码测试（不需要Widget Blueprint）
function TDUIInit.RunCodeOnlyTest()
    TDUIUtils.LogInfo("Running code-only test...")
    
    -- 创建测试模型
    local testModel = TDTestModel()
    TDUIUtils.LogInfo("Test model created")
    
    -- 创建测试ViewModel
    local testViewModel = TDTestViewModel()
    testViewModel:SetModel(testModel)
    testViewModel:OnInitialize()
    TDUIUtils.LogInfo("Test ViewModel created and initialized")
    
    -- 执行一些测试操作
    testViewModel:QuickTest()
    
    -- 模拟一些数据变化
    TDUIUtils.LogInfo("Simulating data changes...")
    for i = 1, 10 do
        testModel:SimulateDataChanges()
    end
    
    -- 输出最终状态
    local playerSummary = testModel:GetPlayerSummary()
    local gameSummary = testModel:GetGameSummary()
    
    TDUIUtils.LogInfo("=== Final State ===")
    TDUIUtils.LogInfo("Player: %s (Lv.%d) - Gold: %d, Gems: %d, Energy: %d/%d", 
                     playerSummary.name, playerSummary.level, 
                     playerSummary.gold, playerSummary.gems, 
                     playerSummary.energy, playerSummary.energyMax)
    TDUIUtils.LogInfo("Game: Playing=%s, Score=%d, Wave=%d, Kills=%d", 
                     tostring(gameSummary.isPlaying), gameSummary.score, 
                     gameSummary.wave, gameSummary.kills)
    
    -- 清理
    testViewModel:Destroy()
    testModel:Destroy()
    
    TDUIUtils.LogInfo("Code-only test completed")
end

-- 获取框架统计信息
function TDUIInit.GetStatistics()
    local stats = {
        version = TDUIInit.GetVersionString(),
        initialized = isInitialized,
        config = TDUIInit.Config
    }
    
    if uiManager then
        stats.uiManager = uiManager:GetStatistics()
    end
    
    local globalBus = TDEventSystem.GetGlobalEventBus()
    stats.eventSystem = {
        totalListeners = globalBus:GetListenerCount(),
        queueSize = #globalBus.eventQueue,
        eventTypes = globalBus:GetEventTypes()
    }
    
    return stats
end

-- 打印框架信息
function TDUIInit.PrintInfo()
    TDUIUtils.LogInfo("=== TowerDefend UI Framework Info ===")
    TDUIUtils.LogInfo("Version: %s", TDUIInit.GetVersionString())
    TDUIUtils.LogInfo("Initialized: %s", tostring(isInitialized))
    
    if isInitialized then
        local stats = TDUIInit.GetStatistics()
        
        if stats.uiManager then
            TDUIUtils.LogInfo("UI Manager - Registered: %d, Active: %d, Cached: %d", 
                             stats.uiManager.registeredUICount,
                             stats.uiManager.activeUICount,
                             stats.uiManager.cachedUICount)
        end
        
        if stats.eventSystem then
            TDUIUtils.LogInfo("Event System - Listeners: %d, Queue: %d, Types: %d", 
                             stats.eventSystem.totalListeners,
                             stats.eventSystem.queueSize,
                             #stats.eventSystem.eventTypes)
        end
    end
    
    TDUIUtils.LogInfo("Config: EnableLogging=%s, EnableCache=%s, EnableDebug=%s", 
                     tostring(TDUIInit.Config.EnableLogging),
                     tostring(TDUIInit.Config.EnableCache),
                     tostring(TDUIInit.Config.EnableDebugMode))
end

-- 导出主要接口
TDUIInit.UIManager = TDUIManager
TDUIInit.EventSystem = TDEventSystem
TDUIInit.DataBinding = TDDataBinding
TDUIInit.Utils = TDUIUtils
TDUIInit.Defines = TDUIDefines

-- 导出基类
TDUIInit.BaseView = TDBaseView
TDUIInit.BaseViewModel = TDBaseViewModel
TDUIInit.BaseModel = TDBaseModel

-- 导出示例类
TDUIInit.TestView = TDTestView
TDUIInit.TestViewModel = TDTestViewModel
TDUIInit.TestModel = TDTestModel

return TDUIInit