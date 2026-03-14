// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TDMapFileManager.generated.h"

class ATDHexGridManager;
class UTDBuildingManager;
class UTDUnitSquad;
class UTDBuildingDataAsset;
class UTDUnitDataAsset;

/**
 * UTDMapFileManager - 地图文件管理器。
 *
 * 纯静态工具类，将地图 JSON 文件统一保存到 Content/SavedMaps/ 目录。
 * 复用 UTDHexGridSaveGame 的 JSON 导入导出能力，提供面向文件名的简洁接口。
 *
 * 保存路径: {ProjectContentDir}/SavedMaps/{MapName}.json
 * 文件格式: UTF-8 JSON，Git 友好。
 */
UCLASS()
class TOWERDEFEND_API UTDMapFileManager : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 将当前网格数据保存为 JSON 文件。
     *
     * @param Grid     网格管理器。
     * @param MapName  地图名称（不含扩展名）。
     * @return         保存是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "MapFile")
    static bool SaveMapToFile(ATDHexGridManager* Grid, const FString& MapName);

    /**
     * 将网格、建筑和单位数据一并保存为 JSON 文件。
     * 存档版本号自动设为 2。
     *
     * @param Grid              网格管理器。
     * @param MapName           地图名称（不含扩展名）。
     * @param BuildingManager   建筑管理器（可选，为空则不保存建筑）。
     * @param UnitSquad         单位编队管理器（可选，为空则不保存单位）。
     * @return                  保存是否成功。
     */
    static bool SaveMapToFileWithEntities(
        ATDHexGridManager* Grid,
        const FString& MapName,
        UTDBuildingManager* BuildingManager,
        UTDUnitSquad* UnitSquad);

    /**
     * 从 JSON 文件加载地图数据并应用到网格。
     *
     * @param Grid     网格管理器。
     * @param MapName  地图名称（不含扩展名）。
     * @return         加载是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "MapFile")
    static bool LoadMapFromFile(ATDHexGridManager* Grid, const FString& MapName);

    /**
     * 从 JSON 文件加载地图、建筑和单位数据。
     * 如果文件版本 >= 2 且提供了管理器，自动恢复建筑和单位。
     *
     * @param Grid              网格管理器。
     * @param MapName           地图名称（不含扩展名）。
     * @param BuildingManager   建筑管理器（可选，为空则跳过建筑恢复）。
     * @param UnitSquad         单位编队管理器（可选，为空则跳过单位恢复）。
     * @param BuildingDataAssets 可用的建筑数据资产列表（用于按 ID 查找）。
     * @param UnitDataAssets    可用的单位数据资产列表（用于按 ID 查找）。
     * @return                  加载是否成功。
     */
    static bool LoadMapFromFileWithEntities(
        ATDHexGridManager* Grid,
        const FString& MapName,
        UTDBuildingManager* BuildingManager,
        UTDUnitSquad* UnitSquad,
        const TArray<UTDBuildingDataAsset*>& BuildingDataAssets,
        const TArray<UTDUnitDataAsset*>& UnitDataAssets);

    /**
     * 获取指定地图名称对应的完整文件路径。
     *
     * @param MapName  地图名称（不含扩展名）。
     * @return         完整文件路径。
     */
    UFUNCTION(BlueprintPure, Category = "MapFile")
    static FString GetMapFilePath(const FString& MapName);

    /**
     * 列出 SavedMaps 目录下所有可用的地图名称。
     *
     * @return  地图名称数组（不含扩展名）。
     */
    UFUNCTION(BlueprintCallable, Category = "MapFile")
    static TArray<FString> GetAvailableMapNames();

    /**
     * 获取 SavedMaps 目录的完整路径。
     *
     * @return  Content/SavedMaps/ 的绝对路径。
     */
    UFUNCTION(BlueprintPure, Category = "MapFile")
    static FString GetSavedMapsDirectory();

    // ---------------------------------------------------------------
    //  默认序列化路径 & 文件轮转
    // ---------------------------------------------------------------

    /** 默认序列化文件名（不含扩展名） */
    static const FString DefaultSerializationMapName;

    /**
     * 获取默认序列化地图目录。
     * 路径: Content/TowerDefend/SerializationMaps/
     */
    UFUNCTION(BlueprintPure, Category = "MapFile")
    static FString GetDefaultSerializationDirectory();

    /**
     * 获取默认序列化地图文件路径。
     * 路径: Content/TowerDefend/SerializationMaps/SerializationMaps.json
     */
    UFUNCTION(BlueprintPure, Category = "MapFile")
    static FString GetDefaultSerializationFilePath();

    /**
     * 保存到默认序列化路径，并自动轮转历史文件。
     * 保留最近 MaxHistory 个备份 (SerializationMaps_01 ~ SerializationMaps_09)。
     *
     * @param Grid        网格管理器。
     * @param MaxHistory  最大保留历史数量（含当前文件共 MaxHistory 个）。默认 10。
     * @return            是否保存成功。
     */
    static bool SaveMapToDefaultPath(ATDHexGridManager* Grid, int32 MaxHistory = 10);

    /**
     * 从默认序列化路径加载最新的地图文件。
     *
     * @param Grid  网格管理器。
     * @return      是否加载成功。
     */
    static bool LoadMapFromDefaultPath(ATDHexGridManager* Grid);

private:
    /**
     * 轮转历史文件。将当前文件依次重命名为 _01 ~ _N，
     * 超出 MaxHistory 的旧文件将被删除。
     */
    static void RotateHistoryFiles(const FString& Directory, const FString& BaseName, int32 MaxHistory);
};
