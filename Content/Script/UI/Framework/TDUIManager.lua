-- TowerDefend UI Framework - UI Manager
-- UI管理器，负责UI的注册、创建、显示、隐藏和层级管理

local TDUIManager = UnLua.Class()

-- 导入依赖
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDUIDefines = require("UI.Common.TDUIDefines")

-- 单例实例
local instance = nil

-- 构造函数
function TDUIManager:Initialize()
    -- UI注册表 {uiName -> {viewClass, widgetClass, layer, ...}}
    self.uiRegistry = {}
    
    -- 活跃的UI实例 {uiName -> viewInstance}
    self.activeUIs = {}
    
    -- UI堆栈（按层级排序）
    self.uiStack = {}
    
    -- 事件分发器
    self.eventDispatcher = TDUIUtils.CreateEventDispatcher()
    
    -- 默认的PlayerController
    self.defaultPlayerController = nil
    
    -- UI缓存池
    self.uiCache = {}
    
    -- 是否启用缓存
    self.enableCache = true
    
    -- 最大缓存数量
    self.maxCacheCount = 10
    
    TDUIUtils.LogInfo("TDUIManager initialized")
end

-- 获取单例实例
function TDUIManager.GetInstance()
    if not instance then
        instance = TDUIManager()
    end
    return instance
end

-- 注册UI
function TDUIManager:RegisterUI(uiName, viewClass, widgetClass, config)
    if not uiName or not viewClass or not widgetClass then
        TDUIUtils.LogError("RegisterUI: invalid parameters")
        return false
    end
    
    if self.uiRegistry[uiName] then
        TDUIUtils.LogWarn("RegisterUI: UI '%s' already registered, overwriting", uiName)
    end
    
    config = config or {}
    
    self.uiRegistry[uiName] = {
        viewClass = viewClass,
        widgetClass = widgetClass,
        layer = config.layer or TDUIDefines.UILayer.Normal,
        singleton = config.singleton ~= false, -- 默认为单例
        cacheable = config.cacheable ~= false, -- 默认可缓存
        showAnimation = config.showAnimation or TDUIDefines.AnimationType.None,
        hideAnimation = config.hideAnimation or TDUIDefines.AnimationType.None,
        autoFocus = config.autoFocus == true,
        modal = config.modal == true, -- 是否为模态窗口
        closeOnEscape = config.closeOnEscape == true
    }
    
    TDUIUtils.LogInfo("UI '%s' registered successfully", uiName)
    return true
end

-- 注销UI
function TDUIManager:UnregisterUI(uiName)
    if not self.uiRegistry[uiName] then
        TDUIUtils.LogWarn("UnregisterUI: UI '%s' not registered", uiName)
        return false
    end
    
    -- 如果UI正在显示，先关闭它
    if self.activeUIs[uiName] then
        self:CloseUI(uiName)
    end
    
    -- 清理缓存
    if self.uiCache[uiName] then
        for _, cachedView in ipairs(self.uiCache[uiName]) do
            cachedView:Destroy()
        end
        self.uiCache[uiName] = nil
    end
    
    self.uiRegistry[uiName] = nil
    
    TDUIUtils.LogInfo("UI '%s' unregistered", uiName)
    return true
end

-- 显示UI
function TDUIManager:ShowUI(uiName, params, playerController)
    local uiConfig = self.uiRegistry[uiName]
    if not uiConfig then
        TDUIUtils.LogError("ShowUI: UI '%s' not registered", uiName)
        return nil
    end
    
    -- 如果是单例且已经显示，直接返回
    if uiConfig.singleton and self.activeUIs[uiName] then
        local existingView = self.activeUIs[uiName]
        if existingView:IsVisible() then
            TDUIUtils.LogInfo("ShowUI: UI '%s' already visible", uiName)
            return existingView
        end
    end
    
    -- 获取或创建View实例
    local view = self:GetOrCreateView(uiName, playerController)
    if not view then
        TDUIUtils.LogError("ShowUI: Failed to create view for UI '%s'", uiName)
        return nil
    end
    
    -- 设置参数（如果View支持）
    if params and view.SetParameters then
        view:SetParameters(params)
    end
    
    -- 显示UI
    local success = view:Show(uiConfig.layer, uiConfig.showAnimation)
    if not success then
        TDUIUtils.LogError("ShowUI: Failed to show UI '%s'", uiName)
        return nil
    end
    
    -- 添加到活跃列表
    self.activeUIs[uiName] = view
    
    -- 更新UI堆栈
    self:UpdateUIStack(uiName, view, true)
    
    -- 设置焦点
    if uiConfig.autoFocus then
        view:SetFocus()
    end
    
    -- 触发事件
    self.eventDispatcher:DispatchEvent("UIShown", uiName, view)
    
    TDUIUtils.LogInfo("UI '%s' shown successfully", uiName)
    return view
end

-- 隐藏UI
function TDUIManager:HideUI(uiName, destroy)
    local view = self.activeUIs[uiName]
    if not view then
        TDUIUtils.LogWarn("HideUI: UI '%s' not active", uiName)
        return false
    end
    
    local uiConfig = self.uiRegistry[uiName]
    
    -- 隐藏UI
    local success = view:Hide(uiConfig and uiConfig.hideAnimation or TDUIDefines.AnimationType.None)
    if not success then
        TDUIUtils.LogError("HideUI: Failed to hide UI '%s'", uiName)
        return false
    end
    
    -- 从活跃列表移除
    self.activeUIs[uiName] = nil
    
    -- 更新UI堆栈
    self:UpdateUIStack(uiName, view, false)
    
    -- 处理销毁或缓存
    if destroy or not (uiConfig and uiConfig.cacheable) or not self.enableCache then
        view:Destroy()
    else
        self:CacheView(uiName, view)
    end
    
    -- 触发事件
    self.eventDispatcher:DispatchEvent("UIHidden", uiName, view)
    
    TDUIUtils.LogInfo("UI '%s' hidden", uiName)
    return true
end

-- 关闭UI（隐藏并销毁）
function TDUIManager:CloseUI(uiName)
    return self:HideUI(uiName, true)
end

-- 获取UI实例
function TDUIManager:GetUI(uiName)
    return self.activeUIs[uiName]
end

-- 检查UI是否显示
function TDUIManager:IsUIVisible(uiName)
    local view = self.activeUIs[uiName]
    return view and view:IsVisible()
end

-- 获取或创建View实例
function TDUIManager:GetOrCreateView(uiName, playerController)
    local uiConfig = self.uiRegistry[uiName]
    if not uiConfig then
        return nil
    end
    
    local view = nil
    
    -- 尝试从缓存获取
    if self.enableCache and uiConfig.cacheable then
        view = self:GetCachedView(uiName)
    end
    
    -- 如果缓存中没有，创建新的
    if not view then
        view = self:CreateView(uiName, playerController)
    end
    
    return view
end

-- 创建View实例
function TDUIManager:CreateView(uiName, playerController)
    local uiConfig = self.uiRegistry[uiName]
    if not uiConfig then
        return nil
    end
    
    -- 获取PlayerController
    playerController = playerController or self.defaultPlayerController
    if not playerController then
        playerController = UE.UGameplayStatics.GetPlayerController(self, 0)
    end
    
    if not playerController then
        TDUIUtils.LogError("CreateView: Cannot get PlayerController")
        return nil
    end
    
    -- 创建View实例
    local view = uiConfig.viewClass()
    if not view then
        TDUIUtils.LogError("CreateView: Failed to create view instance for UI '%s'", uiName)
        return nil
    end
    
    -- 创建Widget
    local success = view:Create(uiConfig.widgetClass, playerController)
    if not success then
        TDUIUtils.LogError("CreateView: Failed to create widget for UI '%s'", uiName)
        return nil
    end
    
    -- 设置UI名称（用于调试）
    view.uiName = uiName
    
    return view
end

-- 缓存View
function TDUIManager:CacheView(uiName, view)
    if not self.enableCache then
        view:Destroy()
        return
    end
    
    if not self.uiCache[uiName] then
        self.uiCache[uiName] = {}
    end
    
    local cache = self.uiCache[uiName]
    
    -- 检查缓存数量限制
    if #cache >= self.maxCacheCount then
        -- 移除最旧的缓存
        local oldestView = table.remove(cache, 1)
        oldestView:Destroy()
    end
    
    -- 添加到缓存
    table.insert(cache, view)
    
    TDUIUtils.LogDebug("View cached for UI '%s', cache count: %d", uiName, #cache)
end

-- 从缓存获取View
function TDUIManager:GetCachedView(uiName)
    local cache = self.uiCache[uiName]
    if not cache or #cache == 0 then
        return nil
    end
    
    -- 获取最新的缓存
    local view = table.remove(cache)
    
    TDUIUtils.LogDebug("View retrieved from cache for UI '%s', remaining cache count: %d", uiName, #cache)
    
    return view
end

-- 更新UI堆栈
function TDUIManager:UpdateUIStack(uiName, view, isShowing)
    if isShowing then
        -- 添加到堆栈
        table.insert(self.uiStack, {
            name = uiName,
            view = view,
            layer = self.uiRegistry[uiName].layer
        })
        
        -- 按层级排序
        table.sort(self.uiStack, function(a, b)
            return a.layer < b.layer
        end)
    else
        -- 从堆栈移除
        for i = #self.uiStack, 1, -1 do
            if self.uiStack[i].name == uiName then
                table.remove(self.uiStack, i)
                break
            end
        end
    end
    
    -- 更新ZOrder
    self:UpdateZOrder()
end

-- 更新ZOrder
function TDUIManager:UpdateZOrder()
    for i, stackItem in ipairs(self.uiStack) do
        local widget = stackItem.view.widget
        if TDUIUtils.IsValidWidget(widget) then
            TDUIUtils.SetWidgetZOrder(widget, stackItem.layer + i)
        end
    end
end

-- 获取顶层UI
function TDUIManager:GetTopUI()
    if #self.uiStack > 0 then
        return self.uiStack[#self.uiStack]
    end
    return nil
end

-- 关闭所有UI
function TDUIManager:CloseAllUI(excludeList)
    excludeList = excludeList or {}
    local excludeSet = {}
    for _, uiName in ipairs(excludeList) do
        excludeSet[uiName] = true
    end
    
    local uiNamesToClose = {}
    for uiName, _ in pairs(self.activeUIs) do
        if not excludeSet[uiName] then
            table.insert(uiNamesToClose, uiName)
        end
    end
    
    for _, uiName in ipairs(uiNamesToClose) do
        self:CloseUI(uiName)
    end
    
    TDUIUtils.LogInfo("Closed %d UIs", #uiNamesToClose)
end

-- 设置UI层级
function TDUIManager:SetUILayer(uiName, layer)
    local uiConfig = self.uiRegistry[uiName]
    if not uiConfig then
        TDUIUtils.LogError("SetUILayer: UI '%s' not registered", uiName)
        return false
    end
    
    uiConfig.layer = layer
    
    -- 如果UI正在显示，更新ZOrder
    local view = self.activeUIs[uiName]
    if view then
        self:UpdateUIStack(uiName, view, true)
    end
    
    return true
end

-- 设置默认PlayerController
function TDUIManager:SetDefaultPlayerController(playerController)
    self.defaultPlayerController = playerController
end

-- 启用/禁用缓存
function TDUIManager:SetCacheEnabled(enabled)
    self.enableCache = enabled
    
    -- 如果禁用缓存，清理所有缓存
    if not enabled then
        self:ClearAllCache()
    end
end

-- 设置最大缓存数量
function TDUIManager:SetMaxCacheCount(count)
    self.maxCacheCount = math.max(1, count)
end

-- 清理所有缓存
function TDUIManager:ClearAllCache()
    for uiName, cache in pairs(self.uiCache) do
        for _, view in ipairs(cache) do
            view:Destroy()
        end
    end
    self.uiCache = {}
    
    TDUIUtils.LogInfo("All UI cache cleared")
end

-- 清理指定UI的缓存
function TDUIManager:ClearUICache(uiName)
    local cache = self.uiCache[uiName]
    if cache then
        for _, view in ipairs(cache) do
            view:Destroy()
        end
        self.uiCache[uiName] = nil
        
        TDUIUtils.LogInfo("Cache cleared for UI '%s'", uiName)
    end
end

-- 获取统计信息
function TDUIManager:GetStatistics()
    local stats = {
        registeredUICount = 0,
        activeUICount = 0,
        cachedUICount = 0,
        totalCachedViews = 0
    }
    
    for _ in pairs(self.uiRegistry) do
        stats.registeredUICount = stats.registeredUICount + 1
    end
    
    for _ in pairs(self.activeUIs) do
        stats.activeUICount = stats.activeUICount + 1
    end
    
    for uiName, cache in pairs(self.uiCache) do
        if #cache > 0 then
            stats.cachedUICount = stats.cachedUICount + 1
            stats.totalCachedViews = stats.totalCachedViews + #cache
        end
    end
    
    return stats
end

-- 监听UI事件
function TDUIManager:OnUIEvent(eventType, callback, target)
    self.eventDispatcher:AddListener(eventType, callback, target)
end

-- 移除UI事件监听
function TDUIManager:RemoveUIEventListener(eventType, callback, target)
    self.eventDispatcher:RemoveListener(eventType, callback, target)
end

-- 销毁管理器
function TDUIManager:Destroy()
    -- 关闭所有UI
    self:CloseAllUI()
    
    -- 清理缓存
    self:ClearAllCache()
    
    -- 清理数据
    self.uiRegistry = {}
    self.activeUIs = {}
    self.uiStack = {}
    self.eventDispatcher:Clear()
    
    -- 清理单例
    if instance == self then
        instance = nil
    end
    
    TDUIUtils.LogInfo("TDUIManager destroyed")
end

return TDUIManager