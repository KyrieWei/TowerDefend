-- TDCoreAccessor - 核心对象访问工具
-- 封装 UTDBlueprintLibrary 中核心对象获取接口，
-- 提供 GameState、GameMode、PlayerState、PlayerController 的便捷访问。

local TDCoreAccessor = {}

local UTDBlueprintLibrary = UE.UTDBlueprintLibrary

-- 获取 TDGameState（已转型）
---@param WorldContextObject UObject 世界上下文对象（通常传 self）
---@return ATDGameState|nil
function TDCoreAccessor.GetGameState(WorldContextObject)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetTDGameState(WorldContextObject)
end

-- 获取 TDGameMode（仅服务端有效）
---@param WorldContextObject UObject 世界上下文对象
---@return ATDGameMode|nil
function TDCoreAccessor.GetGameMode(WorldContextObject)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetTDGameMode(WorldContextObject)
end

-- 获取本地玩家的 TDPlayerState
---@param WorldContextObject UObject 世界上下文对象
---@return ATDPlayerState|nil
function TDCoreAccessor.GetLocalPlayerState(WorldContextObject)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetLocalTDPlayerState(WorldContextObject)
end

-- 获取本地玩家的 TDPlayerController
---@param WorldContextObject UObject 世界上下文对象
---@return ATDPlayerController|nil
function TDCoreAccessor.GetLocalPlayerController(WorldContextObject)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetLocalTDPlayerController(WorldContextObject)
end

-- 根据玩家索引获取 TDPlayerState
---@param WorldContextObject UObject 世界上下文对象
---@param PlayerIndex integer 玩家索引
---@return ATDPlayerState|nil
function TDCoreAccessor.GetPlayerStateByIndex(WorldContextObject, PlayerIndex)
    if not WorldContextObject then
        return nil
    end
    return UTDBlueprintLibrary.GetTDPlayerStateByIndex(WorldContextObject, PlayerIndex)
end

return TDCoreAccessor
