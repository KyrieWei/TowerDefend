// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HexGrid/TDHexCoord.h"
#include "TDNetworkTypes.generated.h"

/**
 * ETDNetworkAction - 网络操作类型枚举。
 * 标识客户端请求的操作类别，用于统一验证和日志。
 */
UENUM(BlueprintType)
enum class ETDNetworkAction : uint8
{
    /** 放置建筑。 */
    PlaceBuilding   UMETA(DisplayName = "Place Building"),

    /** 训练单位。 */
    TrainUnit       UMETA(DisplayName = "Train Unit"),

    /** 研究科技。 */
    ResearchTech    UMETA(DisplayName = "Research Tech"),

    /** 修改地形。 */
    ModifyTerrain   UMETA(DisplayName = "Modify Terrain"),
};

/**
 * FTDNetworkActionResult - 网络操作结果。
 * 由服务端验证后返回，告知操作是否成功及失败原因。
 */
USTRUCT(BlueprintType)
struct FTDNetworkActionResult
{
    GENERATED_BODY()

    /** 操作是否成功。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    bool bSuccess = false;

    /** 失败原因描述。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    FString FailReason;

    /** 操作类型。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    ETDNetworkAction ActionType = ETDNetworkAction::PlaceBuilding;
};

/**
 * FTDGridDelta - 网格状态增量数据。
 * 描述单个格子的状态变化，用于增量复制。
 */
USTRUCT(BlueprintType)
struct FTDGridDelta
{
    GENERATED_BODY()

    /** 变化的格子坐标。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    FTDHexCoord Coord;

    /** 新地形类型（-1 表示未变化）。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    int32 NewTerrainType = -1;

    /** 新高度等级（INT_MIN 表示未变化）。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    int32 NewHeightLevel = INT_MIN;

    /** 新所有者玩家索引（-2 表示未变化）。 */
    UPROPERTY(BlueprintReadOnly, Category = "Network")
    int32 NewOwnerPlayerIndex = -2;
};
