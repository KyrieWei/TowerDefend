-- TDLocalPlayerAccessor - 本地玩家数据访问工具
-- 封装 UTDBlueprintLibrary 中本地玩家数据相关接口，
-- 提供金币、血量、科研、存活状态等便捷访问。

local TDLocalPlayerAccessor = {}

local UTDBlueprintLibrary = UE.UTDBlueprintLibrary

-- 获取本地玩家当前金币
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetGold(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerGold(WorldContextObject)
end

-- 获取本地玩家当前血量
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetHealth(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerHealth(WorldContextObject)
end

-- 获取本地玩家最大血量
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetMaxHealth(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerMaxHealth(WorldContextObject)
end

-- 获取本地玩家科研点数
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetResearchPoints(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerResearchPoints(WorldContextObject)
end

-- 获取本地玩家当前科技时代
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetTechEra(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerTechEra(WorldContextObject)
end

-- 本地玩家是否存活
---@param WorldContextObject UObject 世界上下文对象
---@return boolean
function TDLocalPlayerAccessor.IsAlive(WorldContextObject)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.IsLocalPlayerAlive(WorldContextObject)
end

-- 获取本地玩家胜利次数
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetWinCount(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerWinCount(WorldContextObject)
end

-- 获取本地玩家失败次数
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDLocalPlayerAccessor.GetLossCount(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetLocalPlayerLossCount(WorldContextObject)
end

-- 本地玩家是否能支付指定费用
---@param WorldContextObject UObject 世界上下文对象
---@param Cost integer 费用
---@return boolean
function TDLocalPlayerAccessor.CanAfford(WorldContextObject, Cost)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.CanLocalPlayerAfford(WorldContextObject, Cost)
end

return TDLocalPlayerAccessor
