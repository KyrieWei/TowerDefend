// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "TDGamePhaseTypes.h"
#include "TDSharedTypes.h"
#include "TDBlueprintLibrary.generated.h"

class ATDGameState;
class ATDGameMode;
class ATDPlayerState;
class ATDPlayerController;

/**
 * TD 蓝图工具库
 *
 * 提供全局静态蓝图接口，方便 UI 和蓝图逻辑快速访问
 * 游戏核心数据，无需手动获取和类型转换各个子系统。
 *
 * 所有函数均为静态函数，通过 WorldContextObject 自动获取上下文。
 */
UCLASS()
class TOWERDEFEND_API UTDBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// ═══════════════════════════════════════════════════════
	//  核心对象获取
	// ═══════════════════════════════════════════════════════

	/** 获取 TDGameState（已转型） */
	UFUNCTION(BlueprintPure, Category = "TD|Core",
		meta = (WorldContext = "WorldContextObject"))
	static ATDGameState* GetTDGameState(const UObject* WorldContextObject);

	/** 获取 TDGameMode（仅服务端有效） */
	UFUNCTION(BlueprintPure, Category = "TD|Core",
		meta = (WorldContext = "WorldContextObject"))
	static ATDGameMode* GetTDGameMode(const UObject* WorldContextObject);

	/** 获取本地玩家的 TDPlayerState */
	UFUNCTION(BlueprintPure, Category = "TD|Core",
		meta = (WorldContext = "WorldContextObject"))
	static ATDPlayerState* GetLocalTDPlayerState(const UObject* WorldContextObject);

	/** 获取本地玩家的 TDPlayerController */
	UFUNCTION(BlueprintPure, Category = "TD|Core",
		meta = (WorldContext = "WorldContextObject"))
	static ATDPlayerController* GetLocalTDPlayerController(const UObject* WorldContextObject);

	/** 根据玩家索引获取 TDPlayerState */
	UFUNCTION(BlueprintPure, Category = "TD|Core",
		meta = (WorldContext = "WorldContextObject"))
	static ATDPlayerState* GetTDPlayerStateByIndex(const UObject* WorldContextObject, int32 PlayerIndex);

	// ═══════════════════════════════════════════════════════
	//  游戏阶段 & 回合
	// ═══════════════════════════════════════════════════════

	/** 获取当前游戏阶段 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static ETDGamePhase GetCurrentGamePhase(const UObject* WorldContextObject);

	/** 获取当前回合数 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetCurrentRound(const UObject* WorldContextObject);

	/** 获取最大回合数 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetMaxRounds(const UObject* WorldContextObject);

	/** 获取当前阶段剩余时间（秒） */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static float GetPhaseRemainingTime(const UObject* WorldContextObject);

	/** 当前是否处于准备阶段 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static bool IsInPreparationPhase(const UObject* WorldContextObject);

	/** 当前是否处于战斗阶段 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static bool IsInBattlePhase(const UObject* WorldContextObject);

	/** 游戏是否已结束 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase",
		meta = (WorldContext = "WorldContextObject"))
	static bool IsGameOver(const UObject* WorldContextObject);

	/** 将游戏阶段枚举转换为本地化显示文本 */
	UFUNCTION(BlueprintPure, Category = "TD|Phase")
	static FText GetGamePhaseDisplayName(ETDGamePhase Phase);

	// ═══════════════════════════════════════════════════════
	//  本地玩家数据快捷访问
	// ═══════════════════════════════════════════════════════

	/** 获取本地玩家当前金币 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerGold(const UObject* WorldContextObject);

	/** 获取本地玩家当前血量 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerHealth(const UObject* WorldContextObject);

	/** 获取本地玩家最大血量 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerMaxHealth(const UObject* WorldContextObject);

	/** 获取本地玩家科研点数 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerResearchPoints(const UObject* WorldContextObject);

	/** 获取本地玩家当前科技时代 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerTechEra(const UObject* WorldContextObject);

	/** 本地玩家是否存活 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static bool IsLocalPlayerAlive(const UObject* WorldContextObject);

	/** 获取本地玩家胜利次数 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerWinCount(const UObject* WorldContextObject);

	/** 获取本地玩家失败次数 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetLocalPlayerLossCount(const UObject* WorldContextObject);

	/** 本地玩家是否能支付指定费用 */
	UFUNCTION(BlueprintPure, Category = "TD|LocalPlayer",
		meta = (WorldContext = "WorldContextObject"))
	static bool CanLocalPlayerAfford(const UObject* WorldContextObject, int32 Cost);

	// ═══════════════════════════════════════════════════════
	//  对局信息
	// ═══════════════════════════════════════════════════════

	/** 获取当前对局配置 */
	UFUNCTION(BlueprintPure, Category = "TD|Match",
		meta = (WorldContext = "WorldContextObject"))
	static FTDMatchConfig GetMatchConfig(const UObject* WorldContextObject);

	/** 获取当前存活玩家数量 */
	UFUNCTION(BlueprintPure, Category = "TD|Match",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetAlivePlayerCount(const UObject* WorldContextObject);

	/** 获取所有 TDPlayerState 列表 */
	UFUNCTION(BlueprintPure, Category = "TD|Match",
		meta = (WorldContext = "WorldContextObject"))
	static TArray<ATDPlayerState*> GetAllPlayerStates(const UObject* WorldContextObject);

	/** 获取所有存活的 TDPlayerState 列表 */
	UFUNCTION(BlueprintPure, Category = "TD|Match",
		meta = (WorldContext = "WorldContextObject"))
	static TArray<ATDPlayerState*> GetAlivePlayerStates(const UObject* WorldContextObject);

	/** 获取总玩家数量 */
	UFUNCTION(BlueprintPure, Category = "TD|Match",
		meta = (WorldContext = "WorldContextObject"))
	static int32 GetTotalPlayerCount(const UObject* WorldContextObject);
};
