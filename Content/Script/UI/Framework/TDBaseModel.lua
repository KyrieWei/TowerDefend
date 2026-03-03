-- TowerDefend UI Framework - Base Model
-- 数据模型基类，提供数据封装和变更通知机制

local TDBaseModel = UnLua.Class()

-- 导入依赖
local TDUIUtils = require("UI.Common.TDUIUtils")

-- 构造函数
function TDBaseModel:Initialize()
    -- 数据存储
    self.data = {}
    
    -- 属性变更事件分发器
    self.propertyChangedDispatcher = TDUIUtils.CreateEventDispatcher()
    
    -- 验证规则
    self.validationRules = {}
    
    -- 脏标记
    self.dirtyFlags = {}
    
    TDUIUtils.LogDebug("TDBaseModel initialized")
end

-- 设置属性值
function TDBaseModel:SetProperty(propertyName, value, skipValidation)
    if not propertyName then
        TDUIUtils.LogError("SetProperty: propertyName is nil")
        return false
    end
    
    -- 验证新值
    if not skipValidation and not self:ValidateProperty(propertyName, value) then
        TDUIUtils.LogWarn("SetProperty: validation failed for property '%s'", propertyName)
        return false
    end
    
    local oldValue = self.data[propertyName]
    
    -- 检查值是否真的改变了
    if oldValue == value then
        return true
    end
    
    -- 设置新值
    self.data[propertyName] = value
    
    -- 标记为脏数据
    self.dirtyFlags[propertyName] = true
    
    -- 触发属性变更事件
    self:NotifyPropertyChanged(propertyName, value, oldValue)
    
    TDUIUtils.LogDebug("Property '%s' changed from '%s' to '%s'", propertyName, tostring(oldValue), tostring(value))
    
    return true
end

-- 获取属性值
function TDBaseModel:GetProperty(propertyName, defaultValue)
    if not propertyName then
        return defaultValue
    end
    
    local value = self.data[propertyName]
    return value ~= nil and value or defaultValue
end

-- 检查属性是否存在
function TDBaseModel:HasProperty(propertyName)
    return self.data[propertyName] ~= nil
end

-- 移除属性
function TDBaseModel:RemoveProperty(propertyName)
    if not propertyName or not self:HasProperty(propertyName) then
        return false
    end
    
    local oldValue = self.data[propertyName]
    self.data[propertyName] = nil
    self.dirtyFlags[propertyName] = nil
    
    -- 触发属性变更事件
    self:NotifyPropertyChanged(propertyName, nil, oldValue)
    
    return true
end

-- 获取所有属性名
function TDBaseModel:GetPropertyNames()
    local names = {}
    for name, _ in pairs(self.data) do
        table.insert(names, name)
    end
    return names
end

-- 获取所有数据的副本
function TDBaseModel:GetAllData()
    return TDUIUtils.DeepCopy(self.data)
end

-- 批量设置属性
function TDBaseModel:SetProperties(properties, skipValidation)
    if not properties or type(properties) ~= "table" then
        return false
    end
    
    local success = true
    for propertyName, value in pairs(properties) do
        if not self:SetProperty(propertyName, value, skipValidation) then
            success = false
        end
    end
    
    return success
end

-- 通知属性变更
function TDBaseModel:NotifyPropertyChanged(propertyName, newValue, oldValue)
    self.propertyChangedDispatcher:DispatchEvent("PropertyChanged", propertyName, newValue, oldValue)
    
    -- 也触发特定属性的事件
    local specificEvent = "PropertyChanged_" .. propertyName
    self.propertyChangedDispatcher:DispatchEvent(specificEvent, newValue, oldValue)
end

-- 监听属性变更
function TDBaseModel:OnPropertyChanged(callback, target)
    self.propertyChangedDispatcher:AddListener("PropertyChanged", callback, target)
end

-- 监听特定属性变更
function TDBaseModel:OnSpecificPropertyChanged(propertyName, callback, target)
    local specificEvent = "PropertyChanged_" .. propertyName
    self.propertyChangedDispatcher:AddListener(specificEvent, callback, target)
end

-- 移除属性变更监听
function TDBaseModel:RemovePropertyChangedListener(callback, target)
    self.propertyChangedDispatcher:RemoveListener("PropertyChanged", callback, target)
end

-- 移除特定属性变更监听
function TDBaseModel:RemoveSpecificPropertyChangedListener(propertyName, callback, target)
    local specificEvent = "PropertyChanged_" .. propertyName
    self.propertyChangedDispatcher:RemoveListener(specificEvent, callback, target)
end

-- 添加验证规则
function TDBaseModel:AddValidationRule(propertyName, validator, errorMessage)
    if not propertyName or not validator then
        return false
    end
    
    if not self.validationRules[propertyName] then
        self.validationRules[propertyName] = {}
    end
    
    table.insert(self.validationRules[propertyName], {
        validator = validator,
        errorMessage = errorMessage or "Validation failed"
    })
    
    return true
end

-- 验证属性
function TDBaseModel:ValidateProperty(propertyName, value)
    local rules = self.validationRules[propertyName]
    if not rules then
        return true
    end
    
    for _, rule in ipairs(rules) do
        local isValid = TDUIUtils.SafeCall(rule.validator, value, propertyName)
        if not isValid then
            TDUIUtils.LogWarn("Validation failed for property '%s': %s", propertyName, rule.errorMessage)
            return false
        end
    end
    
    return true
end

-- 验证所有属性
function TDBaseModel:ValidateAll()
    for propertyName, value in pairs(self.data) do
        if not self:ValidateProperty(propertyName, value) then
            return false
        end
    end
    return true
end

-- 检查是否有脏数据
function TDBaseModel:IsDirty(propertyName)
    if propertyName then
        return self.dirtyFlags[propertyName] == true
    else
        -- 检查是否有任何脏数据
        for _, isDirty in pairs(self.dirtyFlags) do
            if isDirty then
                return true
            end
        end
        return false
    end
end

-- 清除脏标记
function TDBaseModel:ClearDirty(propertyName)
    if propertyName then
        self.dirtyFlags[propertyName] = nil
    else
        self.dirtyFlags = {}
    end
end

-- 获取脏属性列表
function TDBaseModel:GetDirtyProperties()
    local dirtyProps = {}
    for propertyName, isDirty in pairs(self.dirtyFlags) do
        if isDirty then
            table.insert(dirtyProps, propertyName)
        end
    end
    return dirtyProps
end

-- 重置模型
function TDBaseModel:Reset()
    self.data = {}
    self.dirtyFlags = {}
    self.propertyChangedDispatcher:Clear()
    TDUIUtils.LogDebug("TDBaseModel reset")
end

-- 销毁模型
function TDBaseModel:Destroy()
    self:Reset()
    self.validationRules = {}
    TDUIUtils.LogDebug("TDBaseModel destroyed")
end

-- 序列化为JSON字符串
function TDBaseModel:ToJson()
    -- 这里可以使用UE的JSON库或者第三方JSON库
    -- 简单实现，实际项目中可能需要更复杂的序列化逻辑
    local jsonData = {}
    for key, value in pairs(self.data) do
        -- 只序列化基本类型
        local valueType = type(value)
        if valueType == "string" or valueType == "number" or valueType == "boolean" then
            jsonData[key] = value
        end
    end
    return jsonData
end

-- 从JSON数据反序列化
function TDBaseModel:FromJson(jsonData)
    if not jsonData or type(jsonData) ~= "table" then
        return false
    end
    
    self:Reset()
    return self:SetProperties(jsonData, true) -- 跳过验证，因为是从已保存的数据恢复
end

return TDBaseModel