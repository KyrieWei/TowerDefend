-- TDMatchAccessor - 对局信息访问工具
-- 封装 UTDBlueprintLibrary 中对局相关接口，
-- 提供对局配置、玩家列表、存活人数等便捷访问。

local TDMatchAccessor = {}

local UTDBlueprintLibrary = UE.UTDBlueprintLibrary

-- 获取当前对局配置
---@param WorldContextObject UObject 世界上下文对象
---@return FTDMatchConfig|nil
function TDMatchAccessor.GetMatchConfig(WorldContextObject)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetMatchConfig(WorldContextObject)
end

-- 获取当前存活玩家数量
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDMatchAccessor.GetAlivePlayerCount(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetAlivePlayerCount(WorldContextObject)
end

-- 获取所有 TDPlayerState 列表
---@param WorldContextObject UObject 世界上下文对象
---@return ATDPlayerState[]
function TDMatchAccessor.GetAllPlayerStates(WorldContextObject)
    if not WorldContextObject then
        return {}
    end
    return UTDBlueprintLibrary.GetAllPlayerStates(WorldContextObject)
end

-- 获取所有存活的 TDPlayerState 列表
---@param WorldContextObject UObject 世界上下文对象
---@return ATDPlayerState[]
function TDMatchAccessor.GetAlivePlayerStates(WorldContextObject)
    if not WorldContextObject then
        return {}
    end
    return UTDBlueprintLibrary.GetAlivePlayerStates(WorldContextObject)
end

-- 获取总玩家数量
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDMatchAccessor.GetTotalPlayerCount(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetTotalPlayerCount(WorldContextObject)
end

return TDMatchAccessor
