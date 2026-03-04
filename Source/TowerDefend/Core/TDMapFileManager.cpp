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

// ===================================================================
// 默认序列化路径 & 文件轮转
// ===================================================================

const FString UTDMapFileManager::DefaultSerializationMapName = TEXT("SerializationMaps");

FString UTDMapFileManager::GetDefaultSerializationDirectory()
{
    return FPaths::ProjectContentDir() / TEXT("TowerDefend/SerializationMaps");
}

FString UTDMapFileManager::GetDefaultSerializationFilePath()
{
    return GetDefaultSerializationDirectory() / (DefaultSerializationMapName + TEXT(".json"));
}

bool UTDMapFileManager::SaveMapToDefaultPath(ATDHexGridManager* Grid, int32 MaxHistory)
{
    if (!Grid)
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::SaveMapToDefaultPath: Grid is null."));
        return false;
    }

    const FString Directory = GetDefaultSerializationDirectory();
    IFileManager::Get().MakeDirectory(*Directory, true);

    // 轮转历史文件（先轮转再保存，这样新文件始终是 SerializationMaps.json）
    RotateHistoryFiles(Directory, DefaultSerializationMapName, MaxHistory);

    // 导出网格数据
    const FTDHexGridSaveData SaveData = Grid->ExportSaveData();

    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    SaveGame->GridData = SaveData;

    const FString FilePath = GetDefaultSerializationFilePath();
    if (!SaveGame->ExportToJsonFile(FilePath))
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::SaveMapToDefaultPath: "
                 "Failed to export to '%s'."), *FilePath);
        return false;
    }

    UE_LOG(LogTDMapFile, Log,
        TEXT("Map saved to default path: (%d tiles) -> %s"),
        SaveData.GetTileCount(), *FilePath);
    return true;
}

bool UTDMapFileManager::LoadMapFromDefaultPath(ATDHexGridManager* Grid)
{
    if (!Grid)
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromDefaultPath: Grid is null."));
        return false;
    }

    const FString FilePath = GetDefaultSerializationFilePath();

    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromDefaultPath: "
                 "File not found: '%s'."), *FilePath);
        return false;
    }

    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    if (!SaveGame->ImportFromJsonFile(FilePath))
    {
        UE_LOG(LogTDMapFile, Error,
            TEXT("UTDMapFileManager::LoadMapFromDefaultPath: "
                 "Failed to import from '%s'."), *FilePath);
        return false;
    }

    Grid->ApplySaveData(SaveGame->GridData);

    UE_LOG(LogTDMapFile, Log,
        TEXT("Map loaded from default path: (%d tiles) <- %s"),
        SaveGame->GridData.GetTileCount(), *FilePath);
    return true;
}

void UTDMapFileManager::RotateHistoryFiles(const FString& Directory, const FString& BaseName, int32 MaxHistory)
{
    // 历史文件命名: BaseName_01.json, BaseName_02.json, ..., BaseName_09.json
    // BaseName.json 是最新的（编号 0）
    // _01 是次新，_09 是最旧
    // MaxHistory = 10 表示总共保留 10 个文件（当前 + 9 个历史）

    const int32 MaxBackups = MaxHistory - 1; // 除去当前文件的备份数量

    // 删除最旧的备份（如果超出上限）
    const FString OldestPath = Directory / FString::Printf(TEXT("%s_%02d.json"), *BaseName, MaxBackups);
    if (FPaths::FileExists(OldestPath))
    {
        IFileManager::Get().Delete(*OldestPath);
    }

    // 依次将 _N-1 重命名为 _N（从后往前）
    for (int32 i = MaxBackups - 1; i >= 1; --i)
    {
        const FString SrcPath = Directory / FString::Printf(TEXT("%s_%02d.json"), *BaseName, i);
        const FString DstPath = Directory / FString::Printf(TEXT("%s_%02d.json"), *BaseName, i + 1);

        if (FPaths::FileExists(SrcPath))
        {
            IFileManager::Get().Move(*DstPath, *SrcPath, true, true);
        }
    }

    // 将当前文件重命名为 _01
    const FString CurrentPath = Directory / (BaseName + TEXT(".json"));
    if (FPaths::FileExists(CurrentPath))
    {
        const FString BackupPath = Directory / FString::Printf(TEXT("%s_%02d.json"), *BaseName, 1);
        IFileManager::Get().Move(*BackupPath, *CurrentPath, true, true);
    }
}
