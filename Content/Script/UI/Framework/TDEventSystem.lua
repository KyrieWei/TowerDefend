-- TowerDefend UI Framework - Event System
-- 事件系统，提供统一的事件处理和分发机制

local TDEventSystem = {}

-- 导入依赖
local TDUIUtils = require("UI.Common.TDUIUtils")

-- 全局事件总线
local GlobalEventBus = nil

-- 事件总线类
local EventBus = {}

function EventBus:New()
    local bus = {
        listeners = {},
        eventQueue = {},
        isProcessing = false,
        maxQueueSize = 1000,
        enableLogging = false
    }
    setmetatable(bus, {__index = self})
    return bus
end

-- 订阅事件
function EventBus:Subscribe(eventType, callback, target, priority)
    if not eventType or not callback then
        TDUIUtils.LogError("Subscribe: invalid parameters")
        return false
    end
    
    priority = priority or 0
    
    if not self.listeners[eventType] then
        self.listeners[eventType] = {}
    end
    
    local listener = {
        callback = callback,
        target = target,
        priority = priority,
        id = #self.listeners[eventType] + 1
    }
    
    table.insert(self.listeners[eventType], listener)
    
    -- 按优先级排序（高优先级先执行）
    table.sort(self.listeners[eventType], function(a, b)
        return a.priority > b.priority
    end)
    
    if self.enableLogging then
        TDUIUtils.LogDebug("Event subscribed: %s, listeners: %d", eventType, #self.listeners[eventType])
    end
    
    return listener.id
end

-- 取消订阅事件
function EventBus:Unsubscribe(eventType, callbackOrId, target)
    if not eventType or not self.listeners[eventType] then
        return false
    end
    
    local listeners = self.listeners[eventType]
    local removed = false
    
    for i = #listeners, 1, -1 do
        local listener = listeners[i]
        local shouldRemove = false
        
        if type(callbackOrId) == "number" then
            -- 按ID移除
            shouldRemove = listener.id == callbackOrId
        else
            -- 按回调函数和目标移除
            shouldRemove = listener.callback == callbackOrId and listener.target == target
        end
        
        if shouldRemove then
            table.remove(listeners, i)
            removed = true
        end
    end
    
    if self.enableLogging and removed then
        TDUIUtils.LogDebug("Event unsubscribed: %s, remaining listeners: %d", eventType, #listeners)
    end
    
    return removed
end

-- 发布事件（立即执行）
function EventBus:Publish(eventType, ...)
    if not eventType then
        return false
    end
    
    local listeners = self.listeners[eventType]
    if not listeners or #listeners == 0 then
        return true
    end
    
    if self.enableLogging then
        TDUIUtils.LogDebug("Event published: %s, listeners: %d", eventType, #listeners)
    end
    
    local args = {...}
    local success = true
    
    -- 执行所有监听器
    for _, listener in ipairs(listeners) do
        local result = TDUIUtils.SafeCall(listener.callback, listener.target, eventType, table.unpack(args))
        if result == false then
            -- 如果监听器返回false，停止传播
            break
        end
    end
    
    return success
end

-- 发布事件（异步队列）
function EventBus:PublishAsync(eventType, ...)
    if not eventType then
        return false
    end
    
    -- 检查队列大小
    if #self.eventQueue >= self.maxQueueSize then
        TDUIUtils.LogWarn("Event queue full, dropping event: %s", eventType)
        return false
    end
    
    local event = {
        type = eventType,
        args = {...},
        timestamp = os.clock()
    }
    
    table.insert(self.eventQueue, event)
    
    if self.enableLogging then
        TDUIUtils.LogDebug("Event queued: %s, queue size: %d", eventType, #self.eventQueue)
    end
    
    return true
end

-- 处理事件队列
function EventBus:ProcessQueue(maxEvents)
    if self.isProcessing then
        return 0
    end
    
    self.isProcessing = true
    maxEvents = maxEvents or math.huge
    local processedCount = 0
    
    while #self.eventQueue > 0 and processedCount < maxEvents do
        local event = table.remove(self.eventQueue, 1)
        self:Publish(event.type, table.unpack(event.args))
        processedCount = processedCount + 1
    end
    
    self.isProcessing = false
    
    if self.enableLogging and processedCount > 0 then
        TDUIUtils.LogDebug("Processed %d events, remaining: %d", processedCount, #self.eventQueue)
    end
    
    return processedCount
end

-- 清空事件队列
function EventBus:ClearQueue()
    local count = #self.eventQueue
    self.eventQueue = {}
    
    if self.enableLogging and count > 0 then
        TDUIUtils.LogDebug("Event queue cleared, %d events dropped", count)
    end
    
    return count
end

-- 获取监听器数量
function EventBus:GetListenerCount(eventType)
    if eventType then
        local listeners = self.listeners[eventType]
        return listeners and #listeners or 0
    else
        local total = 0
        for _, listeners in pairs(self.listeners) do
            total = total + #listeners
        end
        return total
    end
end

-- 获取事件类型列表
function EventBus:GetEventTypes()
    local types = {}
    for eventType, _ in pairs(self.listeners) do
        table.insert(types, eventType)
    end
    return types
end

-- 清理所有监听器
function EventBus:Clear()
    self.listeners = {}
    self:ClearQueue()
    
    if self.enableLogging then
        TDUIUtils.LogDebug("EventBus cleared")
    end
end

-- 启用/禁用日志
function EventBus:SetLoggingEnabled(enabled)
    self.enableLogging = enabled
end

-- 设置最大队列大小
function EventBus:SetMaxQueueSize(size)
    self.maxQueueSize = math.max(1, size)
end

TDEventSystem.EventBus = EventBus

-- 获取全局事件总线
function TDEventSystem.GetGlobalEventBus()
    if not GlobalEventBus then
        GlobalEventBus = EventBus:New()
        GlobalEventBus:SetLoggingEnabled(true)
    end
    return GlobalEventBus
end

-- 事件聚合器
local EventAggregator = {}

function EventAggregator:New()
    local aggregator = {
        eventBuses = {},
        defaultBus = EventBus:New()
    }
    setmetatable(aggregator, {__index = self})
    return aggregator
end

-- 创建或获取事件总线
function EventAggregator:GetEventBus(name)
    name = name or "default"
    
    if name == "default" then
        return self.defaultBus
    end
    
    if not self.eventBuses[name] then
        self.eventBuses[name] = EventBus:New()
    end
    
    return self.eventBuses[name]
end

-- 订阅事件（指定总线）
function EventAggregator:Subscribe(eventType, callback, target, priority, busName)
    local bus = self:GetEventBus(busName)
    return bus:Subscribe(eventType, callback, target, priority)
end

-- 发布事件（指定总线）
function EventAggregator:Publish(eventType, busName, ...)
    local bus = self:GetEventBus(busName)
    return bus:Publish(eventType, ...)
end

-- 广播事件（所有总线）
function EventAggregator:Broadcast(eventType, ...)
    local success = true
    
    -- 发布到默认总线
    success = success and self.defaultBus:Publish(eventType, ...)
    
    -- 发布到所有命名总线
    for _, bus in pairs(self.eventBuses) do
        success = success and bus:Publish(eventType, ...)
    end
    
    return success
end

-- 处理所有总线的队列
function EventAggregator:ProcessAllQueues(maxEventsPerBus)
    local totalProcessed = 0
    
    totalProcessed = totalProcessed + self.defaultBus:ProcessQueue(maxEventsPerBus)
    
    for _, bus in pairs(self.eventBuses) do
        totalProcessed = totalProcessed + bus:ProcessQueue(maxEventsPerBus)
    end
    
    return totalProcessed
end

-- 清理所有总线
function EventAggregator:Clear()
    self.defaultBus:Clear()
    for _, bus in pairs(self.eventBuses) do
        bus:Clear()
    end
    self.eventBuses = {}
end

TDEventSystem.EventAggregator = EventAggregator

-- 事件过滤器
local EventFilter = {}

function EventFilter:New(filterFunc)
    local filter = {
        filterFunc = filterFunc,
        isEnabled = true
    }
    setmetatable(filter, {__index = self})
    return filter
end

function EventFilter:ShouldProcess(eventType, ...)
    if not self.isEnabled or not self.filterFunc then
        return true
    end
    
    return TDUIUtils.SafeCall(self.filterFunc, eventType, ...) == true
end

function EventFilter:SetEnabled(enabled)
    self.isEnabled = enabled
end

TDEventSystem.EventFilter = EventFilter

-- 事件拦截器
local EventInterceptor = {}

function EventInterceptor:New(interceptFunc)
    local interceptor = {
        interceptFunc = interceptFunc,
        isEnabled = true
    }
    setmetatable(interceptor, {__index = self})
    return interceptor
end

function EventInterceptor:Intercept(eventType, ...)
    if not self.isEnabled or not self.interceptFunc then
        return true, ...
    end
    
    local shouldContinue, newArgs = TDUIUtils.SafeCall(self.interceptFunc, eventType, ...)
    if shouldContinue == false then
        return false
    end
    
    if newArgs then
        return true, newArgs
    else
        return true, ...
    end
end

function EventInterceptor:SetEnabled(enabled)
    self.isEnabled = enabled
end

TDEventSystem.EventInterceptor = EventInterceptor

-- 高级事件总线（支持过滤器和拦截器）
local AdvancedEventBus = {}
setmetatable(AdvancedEventBus, {__index = EventBus})

function AdvancedEventBus:New()
    local bus = EventBus:New()
    bus.filters = {}
    bus.interceptors = {}
    setmetatable(bus, {__index = self})
    return bus
end

-- 添加过滤器
function AdvancedEventBus:AddFilter(filter)
    table.insert(self.filters, filter)
end

-- 移除过滤器
function AdvancedEventBus:RemoveFilter(filter)
    for i, f in ipairs(self.filters) do
        if f == filter then
            table.remove(self.filters, i)
            return true
        end
    end
    return false
end

-- 添加拦截器
function AdvancedEventBus:AddInterceptor(interceptor)
    table.insert(self.interceptors, interceptor)
end

-- 移除拦截器
function AdvancedEventBus:RemoveInterceptor(interceptor)
    for i, inter in ipairs(self.interceptors) do
        if inter == interceptor then
            table.remove(self.interceptors, i)
            return true
        end
    end
    return false
end

-- 重写发布方法以支持过滤和拦截
function AdvancedEventBus:Publish(eventType, ...)
    if not eventType then
        return false
    end
    
    local args = {...}
    
    -- 应用过滤器
    for _, filter in ipairs(self.filters) do
        if not filter:ShouldProcess(eventType, table.unpack(args)) then
            if self.enableLogging then
                TDUIUtils.LogDebug("Event filtered: %s", eventType)
            end
            return false
        end
    end
    
    -- 应用拦截器
    local shouldContinue = true
    for _, interceptor in ipairs(self.interceptors) do
        shouldContinue, args = interceptor:Intercept(eventType, table.unpack(args))
        if not shouldContinue then
            if self.enableLogging then
                TDUIUtils.LogDebug("Event intercepted: %s", eventType)
            end
            return false
        end
    end
    
    -- 调用父类的发布方法
    return EventBus.Publish(self, eventType, table.unpack(args))
end

TDEventSystem.AdvancedEventBus = AdvancedEventBus

-- 便捷函数
TDEventSystem.Subscribe = function(eventType, callback, target, priority)
    return TDEventSystem.GetGlobalEventBus():Subscribe(eventType, callback, target, priority)
end

TDEventSystem.Unsubscribe = function(eventType, callbackOrId, target)
    return TDEventSystem.GetGlobalEventBus():Unsubscribe(eventType, callbackOrId, target)
end

TDEventSystem.Publish = function(eventType, ...)
    return TDEventSystem.GetGlobalEventBus():Publish(eventType, ...)
end

TDEventSystem.PublishAsync = function(eventType, ...)
    return TDEventSystem.GetGlobalEventBus():PublishAsync(eventType, ...)
end

TDEventSystem.ProcessQueue = function(maxEvents)
    return TDEventSystem.GetGlobalEventBus():ProcessQueue(maxEvents)
end

-- 常用事件类型定义
TDEventSystem.Events = {
    -- UI事件
    UI_SHOWN = "UI_SHOWN",
    UI_HIDDEN = "UI_HIDDEN",
    UI_CREATED = "UI_CREATED",
    UI_DESTROYED = "UI_DESTROYED",
    UI_FOCUS_CHANGED = "UI_FOCUS_CHANGED",
    
    -- 数据事件
    DATA_CHANGED = "DATA_CHANGED",
    MODEL_UPDATED = "MODEL_UPDATED",
    PROPERTY_CHANGED = "PROPERTY_CHANGED",
    
    -- 游戏事件
    GAME_STATE_CHANGED = "GAME_STATE_CHANGED",
    PLAYER_STATE_CHANGED = "PLAYER_STATE_CHANGED",
    LEVEL_LOADED = "LEVEL_LOADED",
    
    -- 系统事件
    SYSTEM_ERROR = "SYSTEM_ERROR",
    SYSTEM_WARNING = "SYSTEM_WARNING",
    SYSTEM_INFO = "SYSTEM_INFO"
}

return TDEventSystem