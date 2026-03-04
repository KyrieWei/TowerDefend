-- TDPhaseAccessor - 游戏阶段与回合访问工具
-- 封装 UTDBlueprintLibrary 中游戏阶段和回合相关接口，
-- 提供阶段查询、回合数、倒计时等便捷访问。

local TDPhaseAccessor = {}

local UTDBlueprintLibrary = UE.UTDBlueprintLibrary

-- 获取当前游戏阶段枚举值
---@param WorldContextObject UObject 世界上下文对象
---@return ETDGamePhase
function TDPhaseAccessor.GetCurrentGamePhase(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetCurrentGamePhase(WorldContextObject)
end

-- 获取当前回合数
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDPhaseAccessor.GetCurrentRound(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetCurrentRound(WorldContextObject)
end

-- 获取最大回合数
---@param WorldContextObject UObject 世界上下文对象
---@return integer
function TDPhaseAccessor.GetMaxRounds(WorldContextObject)
    if not WorldContextObject then
        return 0
    end
    return UTDBlueprintLibrary.GetMaxRounds(WorldContextObject)
end

-- 获取当前阶段剩余时间（秒）
---@param WorldContextObject UObject 世界上下文对象
---@return number
function TDPhaseAccessor.GetPhaseRemainingTime(WorldContextObject)
    if not WorldContextObject then
        return 0.0
    end
    return UTDBlueprintLibrary.GetPhaseRemainingTime(WorldContextObject)
end

-- 当前是否处于准备阶段
---@param WorldContextObject UObject 世界上下文对象
---@return boolean
function TDPhaseAccessor.IsInPreparationPhase(WorldContextObject)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.IsInPreparationPhase(WorldContextObject)
end

-- 当前是否处于战斗阶段
---@param WorldContextObject UObject 世界上下文对象
---@return boolean
function TDPhaseAccessor.IsInBattlePhase(WorldContextObject)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.IsInBattlePhase(WorldContextObject)
end

-- 游戏是否已结束
---@param WorldContextObject UObject 世界上下文对象
---@return boolean
function TDPhaseAccessor.IsGameOver(WorldContextObject)
    if not WorldContextObject then
        return false
    end
    return UTDBlueprintLibrary.IsGameOver(WorldContextObject)
end

-- 获取游戏阶段的本地化显示名称
---@param Phase ETDGamePhase 游戏阶段枚举值
---@return string
function TDPhaseAccessor.GetGamePhaseDisplayName(Phase)
    return UTDBlueprintLibrary.GetGamePhaseDisplayName(Phase)
end

return TDPhaseAccessor
