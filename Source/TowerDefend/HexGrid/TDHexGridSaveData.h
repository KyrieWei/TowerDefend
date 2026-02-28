// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "HexGrid/TDHexCoord.h"
#include "TDHexGridSaveData.generated.h"

// ===================================================================
// ETDTerrainType - 地形类型枚举
// ===================================================================

/**
 * 六边形格子的地形类型。
 * 地形类型决定了格子的视觉外观、移动消耗、防御加成和建造限制。
 * 地形类型与高度等级共同构成格子的完整地形描述。
 */
UENUM(BlueprintType)
enum class ETDTerrainType : uint8
{
    /** 平原 - 基准地形，无额外效果。 */
    Plain       UMETA(DisplayName = "Plain"),

    /** 丘陵 - 略微凸起，防御+10%、远程射程+1、移动消耗+0.5。 */
    Hill        UMETA(DisplayName = "Hill"),

    /** 山地 - 大幅凸起，不可通行（除特殊单位），不可建造（除特殊建筑）。 */
    Mountain    UMETA(DisplayName = "Mountain"),

    /** 森林 - 提供掩护，影响视线。 */
    Forest      UMETA(DisplayName = "Forest"),

    /** 河流 - 水域地形，减速通过单位。 */
    River       UMETA(DisplayName = "River"),

    /** 沼泽 - 大幅减速，不可建造大型建筑。 */
    Swamp       UMETA(DisplayName = "Swamp"),

    /** 深水 - 不可通行，不可建造。 */
    DeepWater   UMETA(DisplayName = "DeepWater"),

    /** 枚举计数哨兵，勿用于逻辑。 */
    MAX         UMETA(Hidden)
};

// ===================================================================
// FTDHexTileSaveData - 单个格子的序列化数据
// ===================================================================

/**
 * 单个六边形格子的保存数据。
 * 纯数据结构体，不包含任何运行时引用。
 * 用于地图序列化 / 反序列化、网络同步和程序化生成输出。
 */
USTRUCT(BlueprintType)
struct FTDHexTileSaveData
{
    GENERATED_BODY()

    /** 格子在六边形网格中的立方坐标 (Q, R, S)。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TileSaveData")
    FTDHexCoord Coord;

    /** 格子的地形类型。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TileSaveData")
    ETDTerrainType TerrainType = ETDTerrainType::Plain;

    /** 格子的高度等级，有效范围 [-2, 3]。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TileSaveData",
        meta = (ClampMin = "-2", ClampMax = "3"))
    int32 HeightLevel = 0;

    /** 所属玩家索引，-1 表示中立。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TileSaveData")
    int32 OwnerPlayerIndex = -1;

    FTDHexTileSaveData() = default;

    FTDHexTileSaveData(const FTDHexCoord& InCoord, ETDTerrainType InType,
        int32 InHeight, int32 InOwner = -1)
        : Coord(InCoord)
        , TerrainType(InType)
        , HeightLevel(InHeight)
        , OwnerPlayerIndex(InOwner)
    {
    }
};

// ===================================================================
// FTDHexGridSaveData - 完整地图的序列化数据
// ===================================================================

/**
 * 完整六边形网格地图的保存数据。
 * 包含地图元信息和所有格子数据，可序列化为 USaveGame 或 JSON。
 * 用作地形生成器的输出格式和存档的核心数据载体。
 */
USTRUCT(BlueprintType)
struct FTDHexGridSaveData
{
    GENERATED_BODY()

    /** 地图半径（六边形格子数）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridSaveData")
    int32 MapRadius = 0;

    /** 生成此地图所用的随机种子。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridSaveData")
    int32 Seed = 0;

    /** 数据版本号，用于存档兼容性检查。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridSaveData")
    int32 Version = 1;

    /** 所有格子的保存数据列表。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GridSaveData")
    TArray<FTDHexTileSaveData> TileDataList;

    /** 清空所有数据并重置为默认值。 */
    void Reset();

    /** 返回 TileDataList 中格子的数量。 */
    int32 GetTileCount() const;
};

// ===================================================================
// UTDHexGridSaveGame - USaveGame 子类，用于本地存档
// ===================================================================

/**
 * 六边形网格地图的本地存档类。
 * 封装 FTDHexGridSaveData，提供便捷的 SaveToSlot / LoadFromSlot 接口。
 * 同时提供 JSON 导入导出接口，用于策划编辑和地图分享。
 */
UCLASS()
class UTDHexGridSaveGame : public USaveGame
{
    GENERATED_BODY()

public:
    /** 存储的网格地图数据。 */
    UPROPERTY(VisibleAnywhere, Category = "SaveGame")
    FTDHexGridSaveData GridData;

    /**
     * 将当前 GridData 保存到指定槽位。
     *
     * @param SlotName  存档槽位名称。
     * @param UserIndex 用户索引，默认为 0。
     * @return          保存是否成功。
     */
    bool SaveToSlot(const FString& SlotName, int32 UserIndex = 0);

    /**
     * 从指定槽位加载数据到 GridData。
     *
     * @param SlotName  存档槽位名称。
     * @param UserIndex 用户索引，默认为 0。
     * @return          加载是否成功。
     */
    bool LoadFromSlot(const FString& SlotName, int32 UserIndex = 0);

    /**
     * 将 GridData 导出为 JSON 字符串。
     *
     * @param OutJsonString  输出的 JSON 字符串。
     * @return               导出是否成功。
     */
    bool ExportToJsonString(FString& OutJsonString) const;

    /**
     * 从 JSON 字符串导入数据到 GridData。
     *
     * @param JsonString  JSON 字符串。
     * @return            导入是否成功。
     */
    bool ImportFromJsonString(const FString& JsonString);

    /**
     * 将 GridData 导出到 JSON 文件。
     *
     * @param FilePath  文件路径。
     * @return          导出是否成功。
     */
    bool ExportToJsonFile(const FString& FilePath) const;

    /**
     * 从 JSON 文件导入数据到 GridData。
     *
     * @param FilePath  文件路径。
     * @return          导入是否成功。
     */
    bool ImportFromJsonFile(const FString& FilePath);
};
