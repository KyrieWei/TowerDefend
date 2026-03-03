-- TowerDefend UI Framework - Data Binding System
-- 数据绑定系统，提供高级的数据绑定功能和转换器

local TDDataBinding = {}

-- 导入依赖
local TDUIUtils = require("UI.Common.TDUIUtils")
local TDUIDefines = require("UI.Common.TDUIDefines")

-- 绑定表达式解析器
local BindingExpressionParser = {}

-- 解析绑定表达式 (例如: "Player.Name", "Stats.Health + Stats.MaxHealth")
function BindingExpressionParser.Parse(expression)
    if not expression or type(expression) ~= "string" then
        return nil
    end
    
    -- 简单的属性路径解析 (支持 "object.property" 格式)
    local parts = {}
    for part in string.gmatch(expression, "[^%.]+") do
        table.insert(parts, part)
    end
    
    if #parts == 0 then
        return nil
    end
    
    return {
        expression = expression,
        parts = parts,
        isSimplePath = #parts <= 2 and not string.find(expression, "[%+%-%*%/%(%)]")
    }
end

-- 计算绑定表达式的值
function BindingExpressionParser.Evaluate(parsedExpression, context)
    if not parsedExpression or not context then
        return nil
    end
    
    if parsedExpression.isSimplePath then
        -- 简单路径直接访问
        local value = context
        for _, part in ipairs(parsedExpression.parts) do
            if type(value) == "table" and value[part] ~= nil then
                value = value[part]
            elseif type(value) == "userdata" and value.GetProperty then
                value = value:GetProperty(part)
            else
                return nil
            end
        end
        return value
    else
        -- 复杂表达式需要更高级的解析（这里简化处理）
        TDUIUtils.LogWarn("Complex binding expressions not fully supported: %s", parsedExpression.expression)
        return nil
    end
end

-- 内置转换器
TDDataBinding.Converters = {}

-- 布尔转可见性转换器
TDDataBinding.Converters.BooleanToVisibility = function(value)
    return TDUIUtils.ToBoolean(value) and UE.ESlateVisibility.Visible or UE.ESlateVisibility.Collapsed
end

-- 反向布尔转可见性转换器
TDDataBinding.Converters.InverseBooleanToVisibility = function(value)
    return TDUIUtils.ToBoolean(value) and UE.ESlateVisibility.Collapsed or UE.ESlateVisibility.Visible
end

-- 数字转百分比转换器
TDDataBinding.Converters.NumberToPercent = function(value, max)
    max = max or 100
    local num = TDUIUtils.ToNumber(value, 0)
    return math.max(0, math.min(1, num / max))
end

-- 数字转文本转换器
TDDataBinding.Converters.NumberToText = function(value, format)
    format = format or "%.0f"
    local num = TDUIUtils.ToNumber(value, 0)
    return string.format(format, num)
end

-- 布尔转文本转换器
TDDataBinding.Converters.BooleanToText = function(value, trueText, falseText)
    trueText = trueText or "True"
    falseText = falseText or "False"
    return TDUIUtils.ToBoolean(value) and trueText or falseText
end

-- 空值转默认值转换器
TDDataBinding.Converters.NullToDefault = function(value, defaultValue)
    return value ~= nil and value or defaultValue
end

-- 字符串格式化转换器
TDDataBinding.Converters.StringFormat = function(format, ...)
    local args = {...}
    return string.format(format, table.unpack(args))
end

-- 多值转换器基类
local MultiValueConverter = {}

function MultiValueConverter:New(convertFunc)
    local converter = {
        convertFunc = convertFunc,
        sourceProperties = {}
    }
    setmetatable(converter, {__index = self})
    return converter
end

function MultiValueConverter:AddSource(property)
    table.insert(self.sourceProperties, property)
end

function MultiValueConverter:Convert(values)
    if self.convertFunc then
        return TDUIUtils.SafeCall(self.convertFunc, table.unpack(values))
    end
    return nil
end

TDDataBinding.MultiValueConverter = MultiValueConverter

-- 绑定上下文
local BindingContext = {}

function BindingContext:New(source)
    local context = {
        source = source,
        bindings = {},
        eventDispatcher = TDUIUtils.CreateEventDispatcher()
    }
    setmetatable(context, {__index = self})
    return context
end

-- 创建属性绑定
function BindingContext:CreateBinding(targetObject, targetProperty, sourceExpression, converter, mode)
    if not targetObject or not targetProperty or not sourceExpression then
        TDUIUtils.LogError("CreateBinding: invalid parameters")
        return nil
    end
    
    mode = mode or TDUIDefines.BindingMode.OneWay
    
    -- 解析源表达式
    local parsedExpression = BindingExpressionParser.Parse(sourceExpression)
    if not parsedExpression then
        TDUIUtils.LogError("CreateBinding: failed to parse expression '%s'", sourceExpression)
        return nil
    end
    
    local binding = {
        id = #self.bindings + 1,
        targetObject = targetObject,
        targetProperty = targetProperty,
        sourceExpression = sourceExpression,
        parsedExpression = parsedExpression,
        converter = converter,
        mode = mode,
        isActive = false
    }
    
    table.insert(self.bindings, binding)
    
    -- 激活绑定
    self:ActivateBinding(binding)
    
    TDUIUtils.LogDebug("Binding created: %s -> %s.%s", sourceExpression, tostring(targetObject), targetProperty)
    
    return binding
end

-- 激活绑定
function BindingContext:ActivateBinding(binding)
    if binding.isActive then
        return
    end
    
    -- 监听源属性变更
    if self.source and self.source.OnPropertyChanged then
        self.source:OnPropertyChanged(function(propertyName, newValue, oldValue)
            self:OnSourcePropertyChanged(binding, propertyName, newValue, oldValue)
        end, self)
    end
    
    -- 初始化目标值
    self:UpdateTarget(binding)
    
    binding.isActive = true
end

-- 停用绑定
function BindingContext:DeactivateBinding(binding)
    if not binding.isActive then
        return
    end
    
    -- 移除事件监听（这里简化处理，实际应该精确移除）
    binding.isActive = false
end

-- 源属性变更处理
function BindingContext:OnSourcePropertyChanged(binding, propertyName, newValue, oldValue)
    if not binding.isActive then
        return
    end
    
    -- 检查是否影响此绑定
    if self:IsBindingAffected(binding, propertyName) then
        self:UpdateTarget(binding)
    end
end

-- 检查绑定是否受属性变更影响
function BindingContext:IsBindingAffected(binding, propertyName)
    -- 简单检查：如果属性名在表达式中出现
    return string.find(binding.sourceExpression, propertyName) ~= nil
end

-- 更新目标值
function BindingContext:UpdateTarget(binding)
    if not binding.isActive then
        return
    end
    
    -- 计算源值
    local sourceValue = BindingExpressionParser.Evaluate(binding.parsedExpression, self.source)
    
    -- 应用转换器
    local targetValue = sourceValue
    if binding.converter then
        targetValue = TDUIUtils.SafeCall(binding.converter, sourceValue)
    end
    
    -- 设置目标值
    self:SetTargetValue(binding.targetObject, binding.targetProperty, targetValue)
end

-- 设置目标对象的属性值
function BindingContext:SetTargetValue(targetObject, targetProperty, value)
    if not targetObject then
        return false
    end
    
    -- 如果目标对象有SetProperty方法
    if targetObject.SetProperty then
        return targetObject:SetProperty(targetProperty, value)
    end
    
    -- 如果是UE Widget，使用特殊处理
    if type(targetObject) == "userdata" and TDUIUtils.IsValidWidget(targetObject) then
        return self:SetWidgetProperty(targetObject, targetProperty, value)
    end
    
    -- 直接设置属性
    targetObject[targetProperty] = value
    return true
end

-- 设置Widget属性（复用TDBaseView中的逻辑）
function BindingContext:SetWidgetProperty(widget, propertyName, value)
    if not TDUIUtils.IsValidWidget(widget) then
        return false
    end
    
    -- 这里复用TDBaseView中的SetWidgetProperty逻辑
    if propertyName == "Text" then
        if widget:IsA(UE.UTextBlock) then
            widget:SetText(TDUIUtils.ToFText(value))
        elseif widget:IsA(UE.UEditableText) then
            widget:SetText(TDUIUtils.ToFText(value))
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
    end
    
    return true
end

-- 移除绑定
function BindingContext:RemoveBinding(binding)
    if not binding then
        return false
    end
    
    self:DeactivateBinding(binding)
    
    -- 从列表中移除
    for i, b in ipairs(self.bindings) do
        if b.id == binding.id then
            table.remove(self.bindings, i)
            break
        end
    end
    
    return true
end

-- 清理所有绑定
function BindingContext:ClearAllBindings()
    for _, binding in ipairs(self.bindings) do
        self:DeactivateBinding(binding)
    end
    self.bindings = {}
end

-- 销毁绑定上下文
function BindingContext:Destroy()
    self:ClearAllBindings()
    self.eventDispatcher:Clear()
    self.source = nil
end

TDDataBinding.BindingContext = BindingContext

-- 绑定工厂
TDDataBinding.BindingFactory = {}

-- 创建简单属性绑定
function TDDataBinding.BindingFactory.CreatePropertyBinding(source, target, sourceProperty, targetProperty, converter, mode)
    local context = BindingContext:New(source)
    return context:CreateBinding(target, targetProperty, sourceProperty, converter, mode)
end

-- 创建多值绑定
function TDDataBinding.BindingFactory.CreateMultiBinding(source, target, targetProperty, sourceProperties, converter)
    local context = BindingContext:New(source)
    
    -- 创建多值转换器
    local multiConverter = MultiValueConverter:New(converter)
    for _, prop in ipairs(sourceProperties) do
        multiConverter:AddSource(prop)
    end
    
    -- 创建复合表达式
    local expression = table.concat(sourceProperties, ",")
    
    return context:CreateBinding(target, targetProperty, expression, function()
        local values = {}
        for _, prop in ipairs(sourceProperties) do
            local value = BindingExpressionParser.Evaluate(
                BindingExpressionParser.Parse(prop), 
                source
            )
            table.insert(values, value)
        end
        return multiConverter:Convert(values)
    end)
end

-- 创建命令绑定
function TDDataBinding.BindingFactory.CreateCommandBinding(source, target, commandName, eventName, parameter)
    if not source or not target or not commandName or not eventName then
        return nil
    end
    
    -- 获取命令
    local command = source:GetCommand and source:GetCommand(commandName)
    if not command then
        TDUIUtils.LogError("CreateCommandBinding: command '%s' not found", commandName)
        return nil
    end
    
    -- 绑定事件
    local success = false
    if eventName == "OnClicked" and target:IsA(UE.UButton) then
        target.OnClicked:Add(source, function()
            command:Execute(parameter)
        end)
        success = true
    elseif eventName == "OnTextChanged" and target:IsA(UE.UEditableText) then
        target.OnTextChanged:Add(source, function(text)
            command:Execute(text:ToString())
        end)
        success = true
    end
    
    if success then
        TDUIUtils.LogDebug("Command binding created: %s.%s -> %s", tostring(target), eventName, commandName)
    end
    
    return success
end

-- 绑定助手函数
TDDataBinding.BindingHelper = {}

-- 快速创建文本绑定
function TDDataBinding.BindingHelper.BindText(source, textWidget, property, format)
    local converter = format and function(value)
        return string.format(format, value)
    end or nil
    
    return TDDataBinding.BindingFactory.CreatePropertyBinding(
        source, textWidget, property, "Text", converter
    )
end

-- 快速创建进度条绑定
function TDDataBinding.BindingHelper.BindProgress(source, progressWidget, valueProperty, maxProperty)
    if maxProperty then
        return TDDataBinding.BindingFactory.CreateMultiBinding(
            source, progressWidget, "Percent", 
            {valueProperty, maxProperty},
            function(value, max)
                return TDDataBinding.Converters.NumberToPercent(value, max)
            end
        )
    else
        return TDDataBinding.BindingFactory.CreatePropertyBinding(
            source, progressWidget, valueProperty, "Percent"
        )
    end
end

-- 快速创建可见性绑定
function TDDataBinding.BindingHelper.BindVisibility(source, widget, property, inverse)
    local converter = inverse and 
        TDDataBinding.Converters.InverseBooleanToVisibility or 
        TDDataBinding.Converters.BooleanToVisibility
    
    return TDDataBinding.BindingFactory.CreatePropertyBinding(
        source, widget, property, "Visibility", converter
    )
end

-- 快速创建启用状态绑定
function TDDataBinding.BindingHelper.BindEnabled(source, widget, property, inverse)
    local converter = inverse and function(value)
        return not TDUIUtils.ToBoolean(value)
    end or nil
    
    return TDDataBinding.BindingFactory.CreatePropertyBinding(
        source, widget, property, "IsEnabled", converter
    )
end

return TDDataBinding