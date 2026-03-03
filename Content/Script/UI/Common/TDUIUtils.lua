-- TowerDefend UI Framework - UI Utils
-- 提供UI框架中使用的工具函数

local TDUIUtils = {}

-- 导入依赖
local TDUIDefines = require("UI.Common.TDUIDefines")

-- 安全调用函数，避免nil调用错误
function TDUIUtils.SafeCall(func, ...)
    if func and type(func) == "function" then
        local success, result = pcall(func, ...)
        if not success then
            print("[TDUIUtils] SafeCall error: " .. tostring(result))
            return nil
        end
        return result
    end
    return nil
end

-- 深拷贝表
function TDUIUtils.DeepCopy(original)
    local copy
    if type(original) == 'table' then
        copy = {}
        for key, value in next, original, nil do
            copy[TDUIUtils.DeepCopy(key)] = TDUIUtils.DeepCopy(value)
        end
        setmetatable(copy, TDUIUtils.DeepCopy(getmetatable(original)))
    else
        copy = original
    end
    return copy
end

-- 合并表
function TDUIUtils.MergeTable(target, source)
    if not target or not source then
        return target
    end
    
    for key, value in pairs(source) do
        if type(value) == "table" and type(target[key]) == "table" then
            TDUIUtils.MergeTable(target[key], value)
        else
            target[key] = value
        end
    end
    return target
end

-- 检查Widget是否有效
function TDUIUtils.IsValidWidget(widget)
    return widget and widget:IsValid() and not widget:IsPendingKill()
end

-- 设置Widget的ZOrder
function TDUIUtils.SetWidgetZOrder(widget, zOrder)
    if TDUIUtils.IsValidWidget(widget) then
        local slot = widget.Slot
        if slot and slot:IsA(UE.UCanvasPanelSlot) then
            slot:SetZOrder(zOrder)
            return true
        end
    end
    return false
end

-- 获取Widget的ZOrder
function TDUIUtils.GetWidgetZOrder(widget)
    if TDUIUtils.IsValidWidget(widget) then
        local slot = widget.Slot
        if slot and slot:IsA(UE.UCanvasPanelSlot) then
            return slot:GetZOrder()
        end
    end
    return 0
end

-- 设置Widget可见性
function TDUIUtils.SetWidgetVisibility(widget, visibility)
    if TDUIUtils.IsValidWidget(widget) then
        widget:SetVisibility(visibility)
        return true
    end
    return false
end

-- 查找子Widget
function TDUIUtils.FindChildWidget(parent, widgetName)
    if not TDUIUtils.IsValidWidget(parent) then
        return nil
    end
    
    -- 尝试直接通过名称获取
    local child = parent[widgetName]
    if TDUIUtils.IsValidWidget(child) then
        return child
    end
    
    -- 递归查找
    local function FindRecursive(widget, name)
        if not TDUIUtils.IsValidWidget(widget) then
            return nil
        end
        
        -- 检查当前Widget的名称
        if widget:GetName() == name then
            return widget
        end
        
        -- 遍历子Widget
        local childrenCount = widget:GetChildrenCount()
        for i = 0, childrenCount - 1 do
            local childWidget = widget:GetChildAt(i)
            local found = FindRecursive(childWidget, name)
            if found then
                return found
            end
        end
        
        return nil
    end
    
    return FindRecursive(parent, widgetName)
end

-- 创建简单的事件系统
function TDUIUtils.CreateEventDispatcher()
    local dispatcher = {
        listeners = {}
    }
    
    function dispatcher:AddListener(eventType, callback, target)
        if not self.listeners[eventType] then
            self.listeners[eventType] = {}
        end
        
        table.insert(self.listeners[eventType], {
            callback = callback,
            target = target
        })
    end
    
    function dispatcher:RemoveListener(eventType, callback, target)
        if not self.listeners[eventType] then
            return
        end
        
        for i = #self.listeners[eventType], 1, -1 do
            local listener = self.listeners[eventType][i]
            if listener.callback == callback and listener.target == target then
                table.remove(self.listeners[eventType], i)
            end
        end
    end
    
    function dispatcher:DispatchEvent(eventType, ...)
        if not self.listeners[eventType] then
            return
        end
        
        for _, listener in ipairs(self.listeners[eventType]) do
            TDUIUtils.SafeCall(listener.callback, listener.target, ...)
        end
    end
    
    function dispatcher:Clear()
        self.listeners = {}
    end
    
    return dispatcher
end

-- 类型转换工具
function TDUIUtils.ToFText(value)
    if not value then
        return UE.FText.GetEmpty()
    end
    return UE.FText.FromString(tostring(value))
end

function TDUIUtils.ToFString(value)
    if not value then
        return ""
    end
    return tostring(value)
end

function TDUIUtils.ToNumber(value, defaultValue)
    local num = tonumber(value)
    return num or (defaultValue or 0)
end

function TDUIUtils.ToBoolean(value)
    if type(value) == "boolean" then
        return value
    elseif type(value) == "number" then
        return value ~= 0
    elseif type(value) == "string" then
        local lower = string.lower(value)
        return lower == "true" or lower == "1" or lower == "yes"
    end
    return false
end

-- 日志工具
function TDUIUtils.Log(level, message, ...)
    local levels = {
        DEBUG = "[DEBUG]",
        INFO = "[INFO]",
        WARN = "[WARN]", 
        ERROR = "[ERROR]"
    }
    
    local prefix = levels[level] or "[LOG]"
    local formattedMessage = string.format(message, ...)
    print(string.format("%s [TDUIFramework] %s", prefix, formattedMessage))
end

function TDUIUtils.LogDebug(message, ...)
    TDUIUtils.Log("DEBUG", message, ...)
end

function TDUIUtils.LogInfo(message, ...)
    TDUIUtils.Log("INFO", message, ...)
end

function TDUIUtils.LogWarn(message, ...)
    TDUIUtils.Log("WARN", message, ...)
end

function TDUIUtils.LogError(message, ...)
    TDUIUtils.Log("ERROR", message, ...)
end

return TDUIUtils