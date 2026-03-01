// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDMapFileManager.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "HAL/PlatformFileManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDMapFile, Log, All);

// ===================================================================
// 保存 / 加载
// ===================================================================

bool UTDMapFileManager::SaveMapToFile(ATDHexGridManager* Grid, const FString& MapName)
{
    if (!Grid)
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::SaveMapToFile: Grid is null."));
        return false;
    }

    if (MapName.IsEmpty())
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::SaveMapToFile: MapName is empty."));
        return false;
    }

    // 确保目录存在
    const FString Directory = GetSavedMapsDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);

    // 导出网格数据
    const FTDHexGridSaveData SaveData = Grid->ExportSaveData();

    // 使用 UTDHexGridSaveGame 进行 JSON 序列化
    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    SaveGame->GridData = SaveData;

    const FString FilePath = GetMapFilePath(MapName);
    if (!SaveGame->ExportToJsonFile(FilePath))
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::SaveMapToFile: "
                 "Failed to export to '%s'."), *FilePath);
        return false;
    }

    UE_LOG(LogTDMapFile, Log,
        TEXT("Map saved: '%s' (%d tiles) -> %s"),
        *MapName, SaveData.GetTileCount(), *FilePath);
    return true;
}

bool UTDMapFileManager::LoadMapFromFile(ATDHexGridManager* Grid, const FString& MapName)
{
    if (!Grid)
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromFile: Grid is null."));
        return false;
    }

    if (MapName.IsEmpty())
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromFile: MapName is empty."));
        return false;
    }

    const FString FilePath = GetMapFilePath(MapName);

    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromFile: "
                 "File not found: '%s'."), *FilePath);
        return false;
    }

    // 使用 UTDHexGridSaveGame 进行 JSON 反序列化
    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    if (!SaveGame->ImportFromJsonFile(FilePath))
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromFile: "
                 "Failed to import from '%s'."), *FilePath);
        return false;
    }

    // 应用到网格
    Grid->ApplySaveData(SaveGame->GridData);

    UE_LOG(LogTDMapFile, Log,
        TEXT("Map loaded: '%s' (%d tiles) <- %s"),
        *MapName, SaveGame->GridData.GetTileCount(), *FilePath);
    return true;
}

// ===================================================================
// 路径工具
// ===================================================================

FString UTDMapFileManager::GetMapFilePath(const FString& MapName)
{
    return GetSavedMapsDirectory() / (MapName + TEXT(".json"));
}

TArray<FString> UTDMapFileManager::GetAvailableMapNames()
{
    TArray<FString> MapNames;
    const FString Directory = GetSavedMapsDirectory();

    TArray<FString> FoundFiles;
    IFileManager::Get().FindFiles(FoundFiles, *(Directory / TEXT("*.json")), true, false);

    for (const FString& FileName : FoundFiles)
    {
        // 去掉 .json 扩展名
        MapNames.Add(FPaths::GetBaseFilename(FileName));
    }

    MapNames.Sort();
    return MapNames;
}

FString UTDMapFileManager::GetSavedMapsDirectory()
{
    return FPaths::ProjectContentDir() / TEXT("SavedMaps");
}
