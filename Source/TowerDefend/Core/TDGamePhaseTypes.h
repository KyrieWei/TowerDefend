// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDGamePhaseTypes.generated.h"

/**
 * 游戏阶段枚举
 * 定义对局中所有可能的阶段状态，驱动整个回合生命周期流转。
 * 流程：None -> Preparation -> Matchmaking -> Battle -> Settlement -> (循环或 GameOver)
 */
UENUM(BlueprintType)
enum class ETDGamePhase : uint8
{
    /** 未开始 — 对局尚未启动 */
    None            UMETA(DisplayName = "None"),

    /** 准备阶段 — 建造、训练、研究、地形改造 */
    Preparation     UMETA(DisplayName = "Preparation"),

    /** 配对阶段 — 系统将玩家两两配对，决定攻守方 */
    Matchmaking     UMETA(DisplayName = "Matchmaking"),

    /** 战斗阶段 — 进攻方派兵，防守方自动防御 */
    Battle          UMETA(DisplayName = "Battle"),

    /** 结算阶段 — 胜负奖惩、淘汰判定、自动保存 */
    Settlement      UMETA(DisplayName = "Settlement"),

    /** 对局结束 — 已决出最终胜者或所有回合耗尽 */
    GameOver        UMETA(DisplayName = "GameOver"),
};

/**
 * 单回合战斗结果
 * 记录一次攻防对战的关键数据，用于结算和战绩统计。
 */
USTRUCT(BlueprintType)
struct FTDRoundResult
{
    GENERATED_BODY()

    /** 回合编号（从 1 开始） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 RoundNumber = 0;

    /** 进攻方玩家索引 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 AttackerPlayerIndex = INDEX_NONE;

    /** 防守方玩家索引 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 DefenderPlayerIndex = INDEX_NONE;

    /** 进攻方是否获胜 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bAttackerWon = false;

    /** 对败方扣除的血量 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    int32 DamageDealt = 0;
};

/**
 * 对局配置
 * 一局游戏的全局参数，在对局开始前设定，运行时只读。
 * 可在编辑器中直接调整以适应不同的玩法变体。
 */
USTRUCT(BlueprintType)
struct FTDMatchConfig
{
    GENERATED_BODY()

    /** 最大玩家数量 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "2", ClampMax = "16"))
    int32 MaxPlayers = 8;

    /** 最大回合数，达到后强制结算 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
    int32 MaxRounds = 20;

    /** 每位玩家的初始金币 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
    int32 StartingGold = 100;

    /** 每位玩家的初始血量 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
    int32 StartingHealth = 100;

    /** 每回合基础金币收入 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
    int32 GoldPerRound = 50;

    /** 胜利方额外金币奖励 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
    int32 WinBonusGold = 30;

    /** 败方每次扣除的基础血量 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
    int32 LoseDamage = 10;

    /** 准备阶段时长（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
    float PreparationTime = 60.0f;

    /** 战斗阶段时长（秒） */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1.0"))
    float BattleTime = 90.0f;
};
