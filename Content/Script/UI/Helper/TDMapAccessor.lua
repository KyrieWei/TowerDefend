-- TDMapAccessor - 地图管理访问工具
-- 封装 UTDBlueprintLibrary 中地图管理相关接口，
-- 提供地图存取、地图列表、地图重生成等便捷访问。

local TDMapAccessor = {}

local UTDBlueprintLibrary = UE.UTDBlueprintLibrary

-- 获取 HexGridManager
---@param WorldContextObject UObject 世界上下文对象
---@return ATDHexGridManager|nil
function TDMapAccessor.GetHexGridManager(WorldContextObject)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetHexGridManager(WorldContextObject)
end

-- 保存当前地图到 JSON 文件
---@param WorldContextObject UObject 世界上下文对象
---@param MapName string 地图文件名（不含扩展名），为空则使用默认路径
---@return boolean
function TDMapAccessor.SaveMapToFile(WorldContextObject, MapName)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.SaveMapToFile(WorldContextObject, MapName or "")
end

-- 从 JSON 文件加载地图
---@param WorldContextObject UObject 世界上下文对象
---@param MapName string 地图文件名（不含扩展名），为空则使用默认路径
---@return boolean
function TDMapAccessor.LoadMapFromFile(WorldContextObject, MapName)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.LoadMapFromFile(WorldContextObject, MapName or "")
end

-- 获取所有可用的地图名称列表
---@return string[]
function TDMapAccessor.GetAvailableMapNames()
    return UTDBlueprintLibrary.GetAvailableMapNames()
end

-- 保存当前地图到 UE 存档槽位
---@param WorldContextObject UObject 世界上下文对象
---@param SlotName string 存档槽位名称
---@return boolean
function TDMapAccessor.SaveMapToSlot(WorldContextObject, SlotName)
    if not WorldContextObject or not SlotName then
        return false
    end
    return UTDBlueprintLibrary.SaveMapToSlot(WorldContextObject, SlotName)
end

-- 从 UE 存档槽位加载地图
---@param WorldContextObject UObject 世界上下文对象
---@param SlotName string 存档槽位名称
---@return boolean
function TDMapAccessor.LoadMapFromSlot(WorldContextObject, SlotName)
    if not WorldContextObject or not SlotName then
        return false
    end
    return UTDBlueprintLibrary.LoadMapFromSlot(WorldContextObject, SlotName)
end

-- 重新生成随机地图
---@param WorldContextObject UObject 世界上下文对象
---@param Radius integer 地图半径（0 使用默认值）
function TDMapAccessor.RegenerateMap(WorldContextObject, Radius)
    if not WorldContextObject then
        return
    end
    UTDBlueprintLibrary.RegenerateMap(WorldContextObject, Radius or 0)
end

return TDMapAccessor
