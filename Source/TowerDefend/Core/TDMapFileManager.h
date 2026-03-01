// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "TDMapFileManager.generated.h"

class ATDHexGridManager;

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
     * 从 JSON 文件加载地图数据并应用到网格。
     *
     * @param Grid     网格管理器。
     * @param MapName  地图名称（不含扩展名）。
     * @return         加载是否成功。
     */
    UFUNCTION(BlueprintCallable, Category = "MapFile")
    static bool LoadMapFromFile(ATDHexGridManager* Grid, const FString& MapName);

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
};
