// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CheatManager.h"
#include "TDCheatManager.generated.h"

/**
 * UTDCheatManager - 开发 / 调试用控制台指令管理器。
 *
 * 通过 UE 控制台（~ 键）输入 TD.xxx 系列命令来操作游戏系统。
 * 仅在非 Shipping 构建中可用。
 *
 * 命令列表：
 *   地图：TD.Map.Save / Load / ExportJson / ImportJson / Generate
 *   资源：TD.SetGold / AddGold / SetResearch / AddResearch
 *   阶段：TD.Phase.Next / Phase.Set / StartMatch / EndMatch
 *   生命：TD.SetHealth / God
 */
UCLASS()
class TOWERDEFEND_API UTDCheatManager : public UCheatManager
{
    GENERATED_BODY()

public:

    // ─── 地图操作 ─────────────────────────────────────

    /** 将当前地图保存到指定存档槽 */
    UFUNCTION(Exec)
    void CheatSaveMap(FString SlotName);

    /** 从存档槽加载地图 */
    UFUNCTION(Exec)
    void CheatLoadMap(FString SlotName);

    /** 导出地图为 JSON 文件 */
    UFUNCTION(Exec)
    void CheatExportMapJson(FString FilePath);

    /** 从 JSON 文件导入地图 */
    UFUNCTION(Exec)
    void CheatImportMapJson(FString FilePath);

    /** 以指定半径和种子重新生成地图 */
    UFUNCTION(Exec)
    void CheatGenerateMap(int32 Radius, int32 Seed);

    // ─── 资源操作 ─────────────────────────────────────

    /** 设置当前玩家金币（先清零再加） */
    UFUNCTION(Exec)
    void CheatSetGold(int32 Amount);

    /** 增加金币 */
    UFUNCTION(Exec)
    void CheatAddGold(int32 Amount);

    /** 设置科研点 */
    UFUNCTION(Exec)
    void CheatSetResearch(int32 Amount);

    /** 增加科研点 */
    UFUNCTION(Exec)
    void CheatAddResearch(int32 Amount);

    // ─── 阶段控制 ─────────────────────────────────────

    /** 推进到下一阶段 */
    UFUNCTION(Exec)
    void CheatNextPhase();

    /** 跳转到指定阶段（按名称） */
    UFUNCTION(Exec)
    void CheatSetPhase(FString PhaseName);

    /** 开始对局 */
    UFUNCTION(Exec)
    void CheatStartMatch();

    /** 结束对局 */
    UFUNCTION(Exec)
    void CheatEndMatch();

    // ─── 生命值操作 ───────────────────────────────────

    /** 设置当前玩家生命值 */
    UFUNCTION(Exec)
    void CheatSetHealth(int32 Amount);

    /** 切换无敌模式（恢复满血） */
    UFUNCTION(Exec)
    void CheatGodMode();

private:

    /** 无敌模式标记 */
    bool bGodMode = false;
};
