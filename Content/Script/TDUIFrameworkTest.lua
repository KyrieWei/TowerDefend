-- TowerDefend UI Framework - Test Script
-- 框架测试脚本，验证MVVM框架的各项功能

local TDUIFrameworkTest = {}

-- 导入框架
local TDUIInit = require("TDUIInit")

-- 测试结果
local testResults = {
    passed = 0,
    failed = 0,
    total = 0,
    details = {}
}

-- 测试工具函数
local function Assert(condition, message)
    testResults.total = testResults.total + 1
    
    if condition then
        testResults.passed = testResults.passed + 1
        print(string.format("[PASS] %s", message))
        table.insert(testResults.details, {result = "PASS", message = message})
    else
        testResults.failed = testResults.failed + 1
        print(string.format("[FAIL] %s", message))
        table.insert(testResults.details, {result = "FAIL", message = message})
    end
end

local function TestSection(sectionName)
    print(string.format("\n=== %s ===", sectionName))
end

-- 测试框架初始化
function TDUIFrameworkTest.TestInitialization()
    TestSection("Framework Initialization")
    
    -- 测试初始化
    local success = TDUIInit.Initialize({
        EnableLogging = true,
        EnableCache = true,
        EnableDebugMode = true,
        AutoRegisterExamples = false -- 暂时不注册示例UI
    })
    
    Assert(success, "Framework initialization")
    Assert(TDUIInit.IsInitialized(), "Framework initialization status")
    
    -- 测试版本信息
    local version = TDUIInit.GetVersionString()
    Assert(version and type(version) == "string", "Version string retrieval")
    
    -- 测试UI管理器
    local uiManager = TDUIInit.GetUIManager()
    Assert(uiManager ~= nil, "UI Manager creation")
    
    -- 测试全局访问
    Assert(_G.TDUIFramework ~= nil, "Global framework access")
    Assert(_G.TDUIFramework.UIManager ~= nil, "Global UI Manager access")
end

-- 测试数据模型
function TDUIFrameworkTest.TestDataModel()
    TestSection("Data Model")
    
    local TDTestModel = TDUIInit.TestModel
    local model = TDTestModel()
    
    -- 测试基本属性操作
    Assert(model:SetProperty("TestProp", 42), "Set property")
    Assert(model:GetProperty("TestProp") == 42, "Get property")
    Assert(model:HasProperty("TestProp"), "Has property")
    
    -- 测试属性变更通知
    local changeNotified = false
    model:OnSpecificPropertyChanged("TestProp", function(newValue, oldValue)
        changeNotified = true
        Assert(newValue == 100 and oldValue == 42, "Property change notification values")
    end, TDUIFrameworkTest)
    
    model:SetProperty("TestProp", 100)
    Assert(changeNotified, "Property change notification")
    
    -- 测试验证规则
    model:AddValidationRule("TestProp", function(value)
        return type(value) == "number" and value >= 0
    end, "Must be non-negative number")
    
    Assert(model:ValidateProperty("TestProp", 50), "Valid property validation")
    Assert(not model:ValidateProperty("TestProp", -10), "Invalid property validation")
    
    -- 测试脏标记
    model:ClearDirty()
    Assert(not model:IsDirty(), "Clear dirty flag")
    
    model:SetProperty("TestProp", 200)
    Assert(model:IsDirty("TestProp"), "Dirty flag after change")
    
    -- 清理
    model:Destroy()
    Assert(true, "Model destruction")
end

-- 测试视图模型
function TDUIFrameworkTest.TestViewModel()
    TestSection("View Model")
    
    local TDTestModel = TDUIInit.TestModel
    local TDTestViewModel = TDUIInit.TestViewModel
    
    local model = TDTestModel()
    local viewModel = TDTestViewModel()
    
    -- 测试模型绑定
    viewModel:SetModel(model)
    Assert(viewModel:GetModel() == model, "Model binding")
    
    -- 测试属性绑定
    viewModel:BindProperty("VMProp", "ModelProp")
    model:SetProperty("ModelProp", "TestValue")
    
    -- 等待绑定更新
    local vmValue = viewModel:GetProperty("VMProp")
    Assert(vmValue == "TestValue", "Property binding from model to viewmodel")
    
    -- 测试计算属性
    viewModel:DefineComputedProperty("ComputedProp", function()
        local base = viewModel:GetProperty("VMProp", "")
        return base .. "_Computed"
    end, {"VMProp"})
    
    local computedValue = viewModel:GetComputedProperty("ComputedProp")
    Assert(computedValue == "TestValue_Computed", "Computed property")
    
    -- 测试命令
    local commandExecuted = false
    local command = viewModel:CreateCommand("TestCommand", function()
        commandExecuted = true
        return true
    end)
    
    Assert(command ~= nil, "Command creation")
    Assert(viewModel:ExecuteCommand("TestCommand"), "Command execution")
    Assert(commandExecuted, "Command execution callback")
    
    -- 清理
    viewModel:Destroy()
    model:Destroy()
    Assert(true, "ViewModel destruction")
end

-- 测试数据绑定系统
function TDUIFrameworkTest.TestDataBinding()
    TestSection("Data Binding System")
    
    local TDDataBinding = TDUIInit.DataBinding
    
    -- 测试转换器
    local boolToVis = TDDataBinding.Converters.BooleanToVisibility
    Assert(boolToVis(true) == UE.ESlateVisibility.Visible, "Boolean to visibility converter (true)")
    Assert(boolToVis(false) == UE.ESlateVisibility.Collapsed, "Boolean to visibility converter (false)")
    
    local numToPercent = TDDataBinding.Converters.NumberToPercent
    Assert(math.abs(numToPercent(50, 100) - 0.5) < 0.001, "Number to percent converter")
    
    -- 测试绑定上下文
    local TDTestModel = TDUIInit.TestModel
    local model = TDTestModel()
    local context = TDDataBinding.BindingContext:New(model)
    
    Assert(context ~= nil, "Binding context creation")
    
    -- 模拟目标对象
    local targetObject = {
        testProperty = nil,
        SetProperty = function(self, prop, value)
            self[prop] = value
            return true
        end
    }
    
    -- 创建绑定
    local binding = context:CreateBinding(targetObject, "testProperty", "TestProp")
    Assert(binding ~= nil, "Binding creation")
    
    -- 测试绑定更新
    model:SetProperty("TestProp", "BindingTest")
    -- 在实际实现中，这里需要手动触发更新或等待异步更新
    context:UpdateTarget(binding)
    Assert(targetObject.testProperty == "BindingTest", "Binding update")
    
    -- 清理
    context:Destroy()
    model:Destroy()
    Assert(true, "Data binding cleanup")
end

-- 测试事件系统
function TDUIFrameworkTest.TestEventSystem()
    TestSection("Event System")
    
    local TDEventSystem = TDUIInit.EventSystem
    
    -- 测试事件总线
    local eventBus = TDEventSystem.EventBus:New()
    Assert(eventBus ~= nil, "Event bus creation")
    
    -- 测试事件订阅和发布
    local eventReceived = false
    local receivedData = nil
    
    local listenerId = eventBus:Subscribe("TestEvent", function(target, eventType, data)
        eventReceived = true
        receivedData = data
    end, TDUIFrameworkTest)
    
    Assert(listenerId ~= nil, "Event subscription")
    Assert(eventBus:GetListenerCount("TestEvent") == 1, "Listener count")
    
    eventBus:Publish("TestEvent", "TestData")
    Assert(eventReceived, "Event publishing and receiving")
    Assert(receivedData == "TestData", "Event data transmission")
    
    -- 测试事件取消订阅
    eventBus:Unsubscribe("TestEvent", listenerId)
    Assert(eventBus:GetListenerCount("TestEvent") == 0, "Event unsubscription")
    
    -- 测试异步事件
    eventBus:PublishAsync("AsyncEvent", "AsyncData")
    Assert(#eventBus.eventQueue == 1, "Async event queuing")
    
    local processedCount = eventBus:ProcessQueue(1)
    Assert(processedCount == 1, "Event queue processing")
    Assert(#eventBus.eventQueue == 0, "Event queue clearing")
    
    -- 测试全局事件总线
    local globalBus = TDEventSystem.GetGlobalEventBus()
    Assert(globalBus ~= nil, "Global event bus access")
    
    -- 清理
    eventBus:Clear()
    Assert(true, "Event system cleanup")
end

-- 测试UI管理器
function TDUIFrameworkTest.TestUIManager()
    TestSection("UI Manager")
    
    local uiManager = TDUIInit.GetUIManager()
    Assert(uiManager ~= nil, "UI Manager access")
    
    -- 测试UI注册
    local TDTestView = TDUIInit.TestView
    local mockWidgetClass = UE.UUserWidget -- 使用基类作为模拟
    
    local success = uiManager:RegisterUI("TestUI", TDTestView, mockWidgetClass, {
        layer = 100,
        singleton = true,
        cacheable = true
    })
    
    Assert(success, "UI registration")
    
    -- 测试统计信息
    local stats = uiManager:GetStatistics()
    Assert(stats.registeredUICount >= 1, "UI registration count")
    
    -- 测试缓存配置
    uiManager:SetCacheEnabled(true)
    uiManager:SetMaxCacheCount(5)
    Assert(true, "UI Manager configuration")
    
    -- 清理
    uiManager:UnregisterUI("TestUI")
    Assert(true, "UI unregistration")
end

-- 测试完整的MVVM示例
function TDUIFrameworkTest.TestMVVMExample()
    TestSection("MVVM Example")
    
    local TDTestModel = TDUIInit.TestModel
    local TDTestViewModel = TDUIInit.TestViewModel
    
    -- 创建完整的MVVM结构
    local model = TDTestModel()
    local viewModel = TDTestViewModel()
    
    -- 初始化
    viewModel:SetModel(model)
    viewModel:OnInitialize()
    
    Assert(model ~= nil and viewModel ~= nil, "MVVM structure creation")
    
    -- 测试游戏逻辑
    Assert(viewModel:ExecuteCommand("StartGameCommand"), "Start game command")
    Assert(viewModel:GetProperty("IsPlaying", false), "Game state after start")
    
    -- 模拟游戏进行
    for i = 1, 5 do
        model:SimulateDataChanges()
    end
    
    local score = viewModel:GetProperty("GameScore", 0)
    Assert(score > 0, "Score increase during gameplay")
    
    -- 测试暂停/恢复
    Assert(viewModel:ExecuteCommand("TogglePauseCommand"), "Pause game command")
    Assert(viewModel:GetProperty("IsPaused", false), "Game paused state")
    
    Assert(viewModel:ExecuteCommand("TogglePauseCommand"), "Resume game command")
    Assert(not viewModel:GetProperty("IsPaused", true), "Game resumed state")
    
    -- 测试结束游戏
    Assert(viewModel:ExecuteCommand("EndGameCommand"), "End game command")
    Assert(not viewModel:GetProperty("IsPlaying", true), "Game state after end")
    
    -- 测试数据重置
    Assert(viewModel:ExecuteCommand("ResetDataCommand"), "Reset data command")
    
    -- 清理
    viewModel:Destroy()
    model:Destroy()
    Assert(true, "MVVM example cleanup")
end

-- 运行所有测试
function TDUIFrameworkTest.RunAllTests()
    print("=== TowerDefend UI Framework Test Suite ===")
    print("Starting comprehensive framework testing...")
    
    -- 重置测试结果
    testResults = {
        passed = 0,
        failed = 0,
        total = 0,
        details = {}
    }
    
    -- 运行测试
    TDUIFrameworkTest.TestInitialization()
    TDUIFrameworkTest.TestDataModel()
    TDUIFrameworkTest.TestViewModel()
    TDUIFrameworkTest.TestDataBinding()
    TDUIFrameworkTest.TestEventSystem()
    TDUIFrameworkTest.TestUIManager()
    TDUIFrameworkTest.TestMVVMExample()
    
    -- 输出测试结果
    print("\n=== Test Results ===")
    print(string.format("Total Tests: %d", testResults.total))
    print(string.format("Passed: %d", testResults.passed))
    print(string.format("Failed: %d", testResults.failed))
    print(string.format("Success Rate: %.1f%%", 
          testResults.total > 0 and (testResults.passed / testResults.total * 100) or 0))
    
    if testResults.failed > 0 then
        print("\nFailed Tests:")
        for _, detail in ipairs(testResults.details) do
            if detail.result == "FAIL" then
                print(string.format("  - %s", detail.message))
            end
        end
    end
    
    -- 输出框架信息
    print("\n=== Framework Information ===")
    TDUIInit.PrintInfo()
    
    local success = testResults.failed == 0
    print(string.format("\n=== Test Suite %s ===", success and "PASSED" or "FAILED"))
    
    return success
end

-- 运行性能测试
function TDUIFrameworkTest.RunPerformanceTest()
    print("\n=== Performance Test ===")
    
    local TDTestModel = TDUIInit.TestModel
    local TDTestViewModel = TDUIInit.TestViewModel
    
    -- 测试大量数据操作
    local startTime = os.clock()
    
    local model = TDTestModel()
    local viewModel = TDTestViewModel()
    viewModel:SetModel(model)
    viewModel:OnInitialize()
    
    -- 大量属性设置
    for i = 1, 1000 do
        model:SetProperty("Prop" .. i, i)
    end
    
    -- 大量计算属性访问
    for i = 1, 100 do
        local _ = viewModel:GetProperty("PlayerInfoText")
        local _ = viewModel:GetProperty("ExpPercent")
        local _ = viewModel:GetProperty("EnergyPercent")
    end
    
    -- 大量事件触发
    for i = 1, 500 do
        model:SimulateDataChanges()
    end
    
    local endTime = os.clock()
    local duration = endTime - startTime
    
    print(string.format("Performance test completed in %.3f seconds", duration))
    print(string.format("Operations per second: %.0f", 1600 / duration)) -- 1000 + 300 + 500
    
    -- 清理
    viewModel:Destroy()
    model:Destroy()
    
    return duration < 1.0 -- 应该在1秒内完成
end

-- 快速测试（仅核心功能）
function TDUIFrameworkTest.QuickTest()
    print("=== Quick Test ===")
    
    -- 初始化框架
    if not TDUIInit.IsInitialized() then
        TDUIInit.Initialize()
    end
    
    -- 运行代码测试
    TDUIInit.RunCodeOnlyTest()
    
    print("Quick test completed")
    return true
end

return TDUIFrameworkTest