local M = UnLua.Class()

-- 导入MVVM框架
local TDUIInit = require("TDUIInit")
local TDUIFrameworkTest = require("TDUIFrameworkTest")

function M:ReceiveBeginPlay()
    print("=== TowerDefend MVVM UI Framework Demo ===")
    
    -- 初始化框架
    print("Initializing MVVM UI Framework...")
    local success = TDUIInit.Initialize({
        EnableLogging = true,
        EnableCache = true,
        EnableDebugMode = true,
        AutoRegisterExamples = false
    })
    
    if success then
        print("Framework initialized successfully!")
        
        -- 输出框架信息
        TDUIInit.PrintInfo()
        
        -- 运行快速测试
        print("\nRunning quick test...")
        TDUIInit.QuickStartTest()
        
        -- 运行完整测试套件
        print("\nRunning comprehensive test suite...")
        local testSuccess = TDUIFrameworkTest.RunAllTests()
        
        if testSuccess then
            print("\n✓ All tests passed! Framework is working correctly.")
            
            -- 运行性能测试
            print("\nRunning performance test...")
            local perfSuccess = TDUIFrameworkTest.RunPerformanceTest()
            
            if perfSuccess then
                print("✓ Performance test passed!")
            else
                print("⚠ Performance test completed with warnings")
            end
        else
            print("\n✗ Some tests failed. Please check the logs above.")
        end
        
        print("\n=== Framework Demo Completed ===")
        print("The MVVM UI Framework is ready for use!")
        print("Access the framework via: _G.TDUIFramework")
        
    else
        print("✗ Failed to initialize framework!")
    end
end

function M:ReceiveEndPlay()
    print("Shutting down MVVM UI Framework...")
    
    if TDUIInit.IsInitialized() then
        TDUIInit.Shutdown()
        print("Framework shutdown completed.")
    end
end

return M
