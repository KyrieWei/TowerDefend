-- TowerDefend UI Framework - Base ViewModel
-- 视图模型基类，作为View和Model之间的桥梁

local TDBaseViewModel = UnLua.Class()

-- 导入依赖
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDUIDefines = require("UI.Common.TDUIDefines")

-- 构造函数
function TDBaseViewModel:Initialize()
    -- 关联的数据模型
    self.model = nil
    
    -- 属性变更事件分发器
    self.propertyChangedDispatcher = TDUIUtils.CreateEventDispatcher()
    
    -- 命令集合
    self.commands = {}
    
    -- 属性绑定信息
    self.propertyBindings = {}
    
    -- 计算属性缓存
    self.computedProperties = {}
    self.computedPropertyCache = {}
    
    -- 初始化标记
    self.isInitialized = false
    
    TDUIUtils.LogDebug("TDBaseViewModel initialized")
end

-- 设置数据模型
function TDBaseViewModel:SetModel(model)
    -- 移除旧模型的监听
    if self.model then
        self.model:RemovePropertyChangedListener(self.OnModelPropertyChanged, self)
    end
    
    self.model = model
    
    -- 监听新模型的属性变更
    if self.model then
        self.model:OnPropertyChanged(self.OnModelPropertyChanged, self)
    end
    
    -- 刷新所有绑定
    self:RefreshAllBindings()
    
    TDUIUtils.LogDebug("Model set to ViewModel")
end

-- 获取数据模型
function TDBaseViewModel:GetModel()
    return self.model
end

-- 模型属性变更回调
function TDBaseViewModel:OnModelPropertyChanged(propertyName, newValue, oldValue)
    -- 检查是否有绑定到此模型属性的ViewModel属性
    for vmProperty, binding in pairs(self.propertyBindings) do
        if binding.modelProperty == propertyName then
            -- 转换值（如果有转换器）
            local convertedValue = newValue
            if binding.converter then
                convertedValue = TDUIUtils.SafeCall(binding.converter, newValue)
            end
            
            -- 更新ViewModel属性并通知
            self:NotifyPropertyChanged(vmProperty, convertedValue, nil)
        end
    end
    
    -- 清除相关的计算属性缓存
    self:InvalidateComputedProperties(propertyName)
end

-- 绑定属性到模型
function TDBaseViewModel:BindProperty(vmProperty, modelProperty, converter, mode)
    if not vmProperty or not modelProperty then
        TDUIUtils.LogError("BindProperty: invalid parameters")
        return false
    end
    
    mode = mode or TDUIDefines.BindingMode.OneWay
    
    self.propertyBindings[vmProperty] = {
        modelProperty = modelProperty,
        converter = converter,
        mode = mode
    }
    
    -- 如果模型已设置，立即同步初始值
    if self.model and self.model:HasProperty(modelProperty) then
        local modelValue = self.model:GetProperty(modelProperty)
        local convertedValue = modelValue
        if converter then
            convertedValue = TDUIUtils.SafeCall(converter, modelValue)
        end
        self:NotifyPropertyChanged(vmProperty, convertedValue, nil)
    end
    
    TDUIUtils.LogDebug("Property '%s' bound to model property '%s'", vmProperty, modelProperty)
    return true
end

-- 解除属性绑定
function TDBaseViewModel:UnbindProperty(vmProperty)
    if self.propertyBindings[vmProperty] then
        self.propertyBindings[vmProperty] = nil
        TDUIUtils.LogDebug("Property '%s' unbound", vmProperty)
        return true
    end
    return false
end

-- 获取属性值（支持绑定和计算属性）
function TDBaseViewModel:GetProperty(propertyName, defaultValue)
    -- 检查是否是计算属性
    if self.computedProperties[propertyName] then
        return self:GetComputedProperty(propertyName, defaultValue)
    end
    
    -- 检查是否有绑定
    local binding = self.propertyBindings[propertyName]
    if binding and self.model then
        local modelValue = self.model:GetProperty(binding.modelProperty)
        if binding.converter then
            return TDUIUtils.SafeCall(binding.converter, modelValue) or defaultValue
        end
        return modelValue ~= nil and modelValue or defaultValue
    end
    
    return defaultValue
end

-- 设置属性值（支持双向绑定）
function TDBaseViewModel:SetProperty(propertyName, value)
    local binding = self.propertyBindings[propertyName]
    
    if binding and self.model then
        -- 如果是双向绑定，更新模型
        if binding.mode == TDUIDefines.BindingMode.TwoWay then
            -- 反向转换值（如果有反向转换器）
            local modelValue = value
            if binding.reverseConverter then
                modelValue = TDUIUtils.SafeCall(binding.reverseConverter, value)
            end
            
            return self.model:SetProperty(binding.modelProperty, modelValue)
        end
    end
    
    -- 直接通知属性变更（用于非绑定属性）
    self:NotifyPropertyChanged(propertyName, value, nil)
    return true
end

-- 通知属性变更
function TDBaseViewModel:NotifyPropertyChanged(propertyName, newValue, oldValue)
    self.propertyChangedDispatcher:DispatchEvent("PropertyChanged", propertyName, newValue, oldValue)
    
    -- 触发特定属性事件
    local specificEvent = "PropertyChanged_" .. propertyName
    self.propertyChangedDispatcher:DispatchEvent(specificEvent, newValue, oldValue)
    
    -- 清除相关的计算属性缓存
    self:InvalidateComputedProperties(propertyName)
end

-- 监听属性变更
function TDBaseViewModel:OnPropertyChanged(callback, target)
    self.propertyChangedDispatcher:AddListener("PropertyChanged", callback, target)
end

-- 监听特定属性变更
function TDBaseViewModel:OnSpecificPropertyChanged(propertyName, callback, target)
    local specificEvent = "PropertyChanged_" .. propertyName
    self.propertyChangedDispatcher:AddListener(specificEvent, callback, target)
end

-- 移除属性变更监听
function TDBaseViewModel:RemovePropertyChangedListener(callback, target)
    self.propertyChangedDispatcher:RemoveListener("PropertyChanged", callback, target)
end

-- 定义计算属性
function TDBaseViewModel:DefineComputedProperty(propertyName, computeFunc, dependencies)
    if not propertyName or not computeFunc then
        return false
    end
    
    self.computedProperties[propertyName] = {
        computeFunc = computeFunc,
        dependencies = dependencies or {}
    }
    
    TDUIUtils.LogDebug("Computed property '%s' defined", propertyName)
    return true
end

-- 获取计算属性值
function TDBaseViewModel:GetComputedProperty(propertyName, defaultValue)
    local computedProp = self.computedProperties[propertyName]
    if not computedProp then
        return defaultValue
    end
    
    -- 检查缓存
    if self.computedPropertyCache[propertyName] ~= nil then
        return self.computedPropertyCache[propertyName]
    end
    
    -- 计算值
    local value = TDUIUtils.SafeCall(computedProp.computeFunc, self)
    if value == nil then
        value = defaultValue
    end
    
    -- 缓存结果
    self.computedPropertyCache[propertyName] = value
    
    return value
end

-- 使计算属性缓存失效
function TDBaseViewModel:InvalidateComputedProperties(changedProperty)
    for propName, computedProp in pairs(self.computedProperties) do
        -- 检查依赖关系
        if not changedProperty or not computedProp.dependencies or #computedProp.dependencies == 0 then
            -- 如果没有指定依赖或没有依赖列表，清除所有缓存
            self.computedPropertyCache[propName] = nil
        else
            -- 检查是否依赖于变更的属性
            for _, dep in ipairs(computedProp.dependencies) do
                if dep == changedProperty then
                    self.computedPropertyCache[propName] = nil
                    break
                end
            end
        end
    end
end

-- 创建命令
function TDBaseViewModel:CreateCommand(commandName, executeFunc, canExecuteFunc)
    if not commandName or not executeFunc then
        return nil
    end
    
    local command = {
        name = commandName,
        executeFunc = executeFunc,
        canExecuteFunc = canExecuteFunc or function() return true end,
        canExecuteChangedDispatcher = TDUIUtils.CreateEventDispatcher()
    }
    
    -- 执行命令
    function command:Execute(parameter)
        if self:CanExecute(parameter) then
            return TDUIUtils.SafeCall(self.executeFunc, parameter)
        end
        return false
    end
    
    -- 检查是否可执行
    function command:CanExecute(parameter)
        return TDUIUtils.SafeCall(self.canExecuteFunc, parameter) == true
    end
    
    -- 触发CanExecute变更事件
    function command:RaiseCanExecuteChanged()
        self.canExecuteChangedDispatcher:DispatchEvent("CanExecuteChanged")
    end
    
    -- 监听CanExecute变更
    function command:OnCanExecuteChanged(callback, target)
        self.canExecuteChangedDispatcher:AddListener("CanExecuteChanged", callback, target)
    end
    
    self.commands[commandName] = command
    TDUIUtils.LogDebug("Command '%s' created", commandName)
    
    return command
end

-- 获取命令
function TDBaseViewModel:GetCommand(commandName)
    return self.commands[commandName]
end

-- 执行命令
function TDBaseViewModel:ExecuteCommand(commandName, parameter)
    local command = self.commands[commandName]
    if command then
        return command:Execute(parameter)
    end
    
    TDUIUtils.LogWarn("Command '%s' not found", commandName)
    return false
end

-- 刷新所有绑定
function TDBaseViewModel:RefreshAllBindings()
    if not self.model then
        return
    end
    
    for vmProperty, binding in pairs(self.propertyBindings) do
        if self.model:HasProperty(binding.modelProperty) then
            local modelValue = self.model:GetProperty(binding.modelProperty)
            local convertedValue = modelValue
            if binding.converter then
                convertedValue = TDUIUtils.SafeCall(binding.converter, modelValue)
            end
            self:NotifyPropertyChanged(vmProperty, convertedValue, nil)
        end
    end
    
    -- 清除所有计算属性缓存
    self.computedPropertyCache = {}
end

-- 初始化ViewModel（子类可重写）
function TDBaseViewModel:OnInitialize()
    -- 子类实现具体的初始化逻辑
end

-- 执行初始化
function TDBaseViewModel:DoInitialize()
    if self.isInitialized then
        return
    end
    
    self:OnInitialize()
    self.isInitialized = true
    
    TDUIUtils.LogDebug("ViewModel initialized")
end

-- 销毁ViewModel
function TDBaseViewModel:Destroy()
    -- 移除模型监听
    if self.model then
        self.model:RemovePropertyChangedListener(self.OnModelPropertyChanged, self)
    end
    
    -- 清理事件分发器
    self.propertyChangedDispatcher:Clear()
    
    -- 清理命令
    for _, command in pairs(self.commands) do
        command.canExecuteChangedDispatcher:Clear()
    end
    
    -- 清理数据
    self.model = nil
    self.commands = {}
    self.propertyBindings = {}
    self.computedProperties = {}
    self.computedPropertyCache = {}
    self.isInitialized = false
    
    TDUIUtils.LogDebug("TDBaseViewModel destroyed")
end

return TDBaseViewModel