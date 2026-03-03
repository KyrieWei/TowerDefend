-- TowerDefend UI Framework - Base View
-- 视图基类，封装UMG Widget操作和ViewModel绑定

local TDBaseView = UnLua.Class()

-- 导入依赖
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDUIDefines = require("UI.Common.TDUIDefines")

-- 构造函数
function TDBaseView:Initialize()
    -- Widget实例
    self.widget = nil
    
    -- 关联的ViewModel
    self.viewModel = nil
    
    -- UI状态
    self.uiState = TDUIDefines.UIState.None
    
    -- 事件分发器
    self.eventDispatcher = TDUIUtils.CreateEventDispatcher()
    
    -- Widget绑定信息
    self.widgetBindings = {}
    
    -- 命令绑定信息
    self.commandBindings = {}
    
    -- 动画信息
    self.animations = {}
    
    -- 是否已初始化
    self.isInitialized = false
    
    TDUIUtils.LogDebug("TDBaseView initialized")
end

-- 创建Widget
function TDBaseView:Create(widgetClass, ownerPlayer)
    if self.uiState ~= TDUIDefines.UIState.None then
        TDUIUtils.LogWarn("View already created, current state: %d", self.uiState)
        return false
    end
    
    if not widgetClass then
        TDUIUtils.LogError("Create: widgetClass is nil")
        return false
    end
    
    -- 获取PlayerController
    local playerController = ownerPlayer
    if not playerController then
        playerController = UE.UGameplayStatics.GetPlayerController(self, 0)
    end
    
    if not playerController then
        TDUIUtils.LogError("Create: Cannot get PlayerController")
        return false
    end
    
    -- 创建Widget实例
    self.widget = UE.UWidgetBlueprintLibrary.Create(playerController, widgetClass)
    
    if not TDUIUtils.IsValidWidget(self.widget) then
        TDUIUtils.LogError("Create: Failed to create widget")
        return false
    end
    
    -- 设置状态
    self.uiState = TDUIDefines.UIState.Created
    
    -- 执行初始化
    self:DoInitialize()
    
    -- 触发创建事件
    self:DispatchEvent(TDUIDefines.UIEventType.OnCreate)
    
    TDUIUtils.LogDebug("View created successfully")
    return true
end

-- 显示UI
function TDBaseView:Show(layer, animation)
    if not TDUIUtils.IsValidWidget(self.widget) then
        TDUIUtils.LogError("Show: Widget is not valid")
        return false
    end
    
    if self.uiState == TDUIDefines.UIState.Shown or self.uiState == TDUIDefines.UIState.Showing then
        TDUIUtils.LogWarn("View is already shown or showing")
        return true
    end
    
    -- 设置状态
    self.uiState = TDUIDefines.UIState.Showing
    
    -- 添加到视口
    layer = layer or TDUIDefines.UILayer.Normal
    self.widget:AddToViewport(layer)
    
    -- 执行显示动画
    if animation and animation ~= TDUIDefines.AnimationType.None then
        self:PlayShowAnimation(animation, function()
            self.uiState = TDUIDefines.UIState.Shown
            self:DispatchEvent(TDUIDefines.UIEventType.OnShow)
        end)
    else
        self.uiState = TDUIDefines.UIState.Shown
        self:DispatchEvent(TDUIDefines.UIEventType.OnShow)
    end
    
    TDUIUtils.LogDebug("View shown with layer %d", layer)
    return true
end

-- 隐藏UI
function TDBaseView:Hide(animation)
    if not TDUIUtils.IsValidWidget(self.widget) then
        TDUIUtils.LogError("Hide: Widget is not valid")
        return false
    end
    
    if self.uiState == TDUIDefines.UIState.Hidden or self.uiState == TDUIDefines.UIState.Hiding then
        TDUIUtils.LogWarn("View is already hidden or hiding")
        return true
    end
    
    -- 设置状态
    self.uiState = TDUIDefines.UIState.Hiding
    
    -- 执行隐藏动画
    if animation and animation ~= TDUIDefines.AnimationType.None then
        self:PlayHideAnimation(animation, function()
            self.widget:RemoveFromParent()
            self.uiState = TDUIDefines.UIState.Hidden
            self:DispatchEvent(TDUIDefines.UIEventType.OnHide)
        end)
    else
        self.widget:RemoveFromParent()
        self.uiState = TDUIDefines.UIState.Hidden
        self:DispatchEvent(TDUIDefines.UIEventType.OnHide)
    end
    
    TDUIUtils.LogDebug("View hidden")
    return true
end

-- 销毁UI
function TDBaseView:Destroy()
    if self.uiState == TDUIDefines.UIState.Destroyed then
        return
    end
    
    -- 先隐藏
    if self.uiState == TDUIDefines.UIState.Shown or self.uiState == TDUIDefines.UIState.Showing then
        self:Hide()
    end
    
    -- 解除ViewModel绑定
    self:UnbindViewModel()
    
    -- 清理Widget绑定
    self:ClearAllBindings()
    
    -- 销毁Widget
    if TDUIUtils.IsValidWidget(self.widget) then
        self.widget:RemoveFromParent()
        -- 注意：UE的Widget会自动进行垃圾回收
        self.widget = nil
    end
    
    -- 清理事件分发器
    self.eventDispatcher:Clear()
    
    -- 设置状态
    self.uiState = TDUIDefines.UIState.Destroyed
    
    -- 触发销毁事件
    self:DispatchEvent(TDUIDefines.UIEventType.OnDestroy)
    
    TDUIUtils.LogDebug("View destroyed")
end

-- 设置ViewModel
function TDBaseView:SetViewModel(viewModel)
    -- 解除旧的绑定
    self:UnbindViewModel()
    
    self.viewModel = viewModel
    
    -- 建立新的绑定
    if self.viewModel then
        self:BindViewModel()
    end
    
    TDUIUtils.LogDebug("ViewModel set to View")
end

-- 获取ViewModel
function TDBaseView:GetViewModel()
    return self.viewModel
end

-- 绑定ViewModel
function TDBaseView:BindViewModel()
    if not self.viewModel then
        return
    end
    
    -- 监听ViewModel属性变更
    self.viewModel:OnPropertyChanged(self.OnViewModelPropertyChanged, self)
    
    -- 执行子类的绑定逻辑
    self:OnBindViewModel()
    
    -- 刷新所有绑定
    self:RefreshAllBindings()
end

-- 解除ViewModel绑定
function TDBaseView:UnbindViewModel()
    if self.viewModel then
        self.viewModel:RemovePropertyChangedListener(self.OnViewModelPropertyChanged, self)
        self:OnUnbindViewModel()
    end
end

-- ViewModel属性变更回调
function TDBaseView:OnViewModelPropertyChanged(propertyName, newValue, oldValue)
    -- 更新相关的Widget绑定
    for _, binding in ipairs(self.widgetBindings) do
        if binding.property == propertyName then
            self:UpdateWidgetBinding(binding, newValue)
        end
    end
    
    -- 更新相关的命令绑定
    for _, binding in ipairs(self.commandBindings) do
        if binding.canExecuteProperty == propertyName then
            self:UpdateCommandBinding(binding)
        end
    end
end

-- 绑定Widget属性
function TDBaseView:BindWidgetProperty(widgetName, widgetProperty, vmProperty, converter, mode)
    if not widgetName or not widgetProperty or not vmProperty then
        TDUIUtils.LogError("BindWidgetProperty: invalid parameters")
        return false
    end
    
    local widget = self:FindWidget(widgetName)
    if not TDUIUtils.IsValidWidget(widget) then
        TDUIUtils.LogError("BindWidgetProperty: widget '%s' not found", widgetName)
        return false
    end
    
    mode = mode or TDUIDefines.BindingMode.OneWay
    
    local binding = {
        widget = widget,
        widgetName = widgetName,
        widgetProperty = widgetProperty,
        property = vmProperty,
        converter = converter,
        mode = mode
    }
    
    table.insert(self.widgetBindings, binding)
    
    -- 立即更新一次
    if self.viewModel then
        local value = self.viewModel:GetProperty(vmProperty)
        self:UpdateWidgetBinding(binding, value)
    end
    
    -- 如果是双向绑定，监听Widget事件
    if mode == TDUIDefines.BindingMode.TwoWay then
        self:SetupTwoWayBinding(binding)
    end
    
    TDUIUtils.LogDebug("Widget property bound: %s.%s -> %s", widgetName, widgetProperty, vmProperty)
    return true
end

-- 更新Widget绑定
function TDBaseView:UpdateWidgetBinding(binding, value)
    if not TDUIUtils.IsValidWidget(binding.widget) then
        return
    end
    
    -- 转换值
    local convertedValue = value
    if binding.converter then
        convertedValue = TDUIUtils.SafeCall(binding.converter, value)
    end
    
    -- 根据Widget类型和属性设置值
    self:SetWidgetProperty(binding.widget, binding.widgetProperty, convertedValue)
end

-- 设置Widget属性值
function TDBaseView:SetWidgetProperty(widget, propertyName, value)
    if not TDUIUtils.IsValidWidget(widget) then
        return false
    end
    
    -- 根据Widget类型和属性名设置值
    if propertyName == "Text" then
        if widget:IsA(UE.UTextBlock) then
            widget:SetText(TDUIUtils.ToFText(value))
        elseif widget:IsA(UE.UEditableText) then
            widget:SetText(TDUIUtils.ToFText(value))
        elseif widget:IsA(UE.UButton) then
            -- 按钮可能需要设置子TextBlock的文本
            local textBlock = TDUIUtils.FindChildWidget(widget, "Text")
            if TDUIUtils.IsValidWidget(textBlock) then
                textBlock:SetText(TDUIUtils.ToFText(value))
            end
        end
    elseif propertyName == "Percent" then
        if widget:IsA(UE.UProgressBar) then
            widget:SetPercent(TDUIUtils.ToNumber(value))
        end
    elseif propertyName == "Visibility" then
        local visibility = UE.ESlateVisibility.Visible
        if not TDUIUtils.ToBoolean(value) then
            visibility = UE.ESlateVisibility.Collapsed
        end
        widget:SetVisibility(visibility)
    elseif propertyName == "IsEnabled" then
        widget:SetIsEnabled(TDUIUtils.ToBoolean(value))
    elseif propertyName == "Texture" then
        if widget:IsA(UE.UImage) and value then
            widget:SetBrushFromTexture(value)
        end
    end
    
    return true
end

-- 绑定命令
function TDBaseView:BindCommand(widgetName, eventName, commandName, parameter, canExecuteProperty)
    if not widgetName or not eventName or not commandName then
        TDUIUtils.LogError("BindCommand: invalid parameters")
        return false
    end
    
    local widget = self:FindWidget(widgetName)
    if not TDUIUtils.IsValidWidget(widget) then
        TDUIUtils.LogError("BindCommand: widget '%s' not found", widgetName)
        return false
    end
    
    local binding = {
        widget = widget,
        widgetName = widgetName,
        eventName = eventName,
        commandName = commandName,
        parameter = parameter,
        canExecuteProperty = canExecuteProperty
    }
    
    -- 绑定事件
    self:BindWidgetEvent(widget, eventName, function()
        if self.viewModel then
            self.viewModel:ExecuteCommand(commandName, parameter)
        end
    end)
    
    table.insert(self.commandBindings, binding)
    
    -- 更新初始状态
    self:UpdateCommandBinding(binding)
    
    TDUIUtils.LogDebug("Command bound: %s.%s -> %s", widgetName, eventName, commandName)
    return true
end

-- 更新命令绑定
function TDBaseView:UpdateCommandBinding(binding)
    if not TDUIUtils.IsValidWidget(binding.widget) or not self.viewModel then
        return
    end
    
    local command = self.viewModel:GetCommand(binding.commandName)
    if not command then
        return
    end
    
    -- 更新CanExecute状态
    local canExecute = true
    if binding.canExecuteProperty then
        canExecute = TDUIUtils.ToBoolean(self.viewModel:GetProperty(binding.canExecuteProperty, true))
    else
        canExecute = command:CanExecute(binding.parameter)
    end
    
    -- 设置Widget的启用状态
    binding.widget:SetIsEnabled(canExecute)
end

-- 绑定Widget事件
function TDBaseView:BindWidgetEvent(widget, eventName, callback)
    if not TDUIUtils.IsValidWidget(widget) or not eventName or not callback then
        return false
    end
    
    -- 根据Widget类型和事件名绑定
    if eventName == "OnClicked" and widget:IsA(UE.UButton) then
        widget.OnClicked:Add(self, callback)
    elseif eventName == "OnTextChanged" and widget:IsA(UE.UEditableText) then
        widget.OnTextChanged:Add(self, function(text)
            callback(text:ToString())
        end)
    elseif eventName == "OnValueChanged" and widget:IsA(UE.USlider) then
        widget.OnValueChanged:Add(self, callback)
    elseif eventName == "OnCheckStateChanged" and widget:IsA(UE.UCheckBox) then
        widget.OnCheckStateChanged:Add(self, callback)
    end
    
    return true
end

-- 查找Widget
function TDBaseView:FindWidget(widgetName)
    if not TDUIUtils.IsValidWidget(self.widget) then
        return nil
    end
    
    -- 首先尝试直接访问
    local widget = self.widget[widgetName]
    if TDUIUtils.IsValidWidget(widget) then
        return widget
    end
    
    -- 递归查找
    return TDUIUtils.FindChildWidget(self.widget, widgetName)
end

-- 播放显示动画
function TDBaseView:PlayShowAnimation(animationType, onComplete)
    -- 子类可以重写实现具体的动画逻辑
    if onComplete then
        onComplete()
    end
end

-- 播放隐藏动画
function TDBaseView:PlayHideAnimation(animationType, onComplete)
    -- 子类可以重写实现具体的动画逻辑
    if onComplete then
        onComplete()
    end
end

-- 设置焦点
function TDBaseView:SetFocus()
    if TDUIUtils.IsValidWidget(self.widget) then
        self.widget:SetFocus()
        self:DispatchEvent(TDUIDefines.UIEventType.OnFocus)
    end
end

-- 刷新所有绑定
function TDBaseView:RefreshAllBindings()
    if not self.viewModel then
        return
    end
    
    -- 刷新Widget绑定
    for _, binding in ipairs(self.widgetBindings) do
        local value = self.viewModel:GetProperty(binding.property)
        self:UpdateWidgetBinding(binding, value)
    end
    
    -- 刷新命令绑定
    for _, binding in ipairs(self.commandBindings) do
        self:UpdateCommandBinding(binding)
    end
end

-- 清理所有绑定
function TDBaseView:ClearAllBindings()
    self.widgetBindings = {}
    self.commandBindings = {}
end

-- 分发事件
function TDBaseView:DispatchEvent(eventType, ...)
    self.eventDispatcher:DispatchEvent(eventType, ...)
end

-- 监听事件
function TDBaseView:OnEvent(eventType, callback, target)
    self.eventDispatcher:AddListener(eventType, callback, target)
end

-- 移除事件监听
function TDBaseView:RemoveEventListener(eventType, callback, target)
    self.eventDispatcher:RemoveListener(eventType, callback, target)
end

-- 获取UI状态
function TDBaseView:GetUIState()
    return self.uiState
end

-- 检查是否可见
function TDBaseView:IsVisible()
    return self.uiState == TDUIDefines.UIState.Shown or self.uiState == TDUIDefines.UIState.Showing
end

-- 子类重写的初始化方法
function TDBaseView:OnInitialize()
    -- 子类实现具体的初始化逻辑
end

-- 子类重写的ViewModel绑定方法
function TDBaseView:OnBindViewModel()
    -- 子类实现具体的绑定逻辑
end

-- 子类重写的ViewModel解绑方法
function TDBaseView:OnUnbindViewModel()
    -- 子类实现具体的解绑逻辑
end

-- 执行初始化
function TDBaseView:DoInitialize()
    if self.isInitialized then
        return
    end
    
    self:OnInitialize()
    self.isInitialized = true
    
    TDUIUtils.LogDebug("View initialized")
end

return TDBaseView