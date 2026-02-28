// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexGridSaveData.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// ===================================================================
// FTDHexGridSaveData
// ===================================================================

void FTDHexGridSaveData::Reset()
{
    MapRadius = 0;
    Seed = 0;
    Version = 1;
    TileDataList.Empty();
}

int32 FTDHexGridSaveData::GetTileCount() const
{
    return TileDataList.Num();
}

// ===================================================================
// JSON 序列化辅助：地形类型 ←→ 字符串
// ===================================================================

namespace TDSaveDataInternal
{
    static const TCHAR* TerrainTypeToString(ETDTerrainType Type)
    {
        switch (Type)
        {
        case ETDTerrainType::Plain:     return TEXT("Plain");
        case ETDTerrainType::Hill:      return TEXT("Hill");
        case ETDTerrainType::Mountain:  return TEXT("Mountain");
        case ETDTerrainType::Forest:    return TEXT("Forest");
        case ETDTerrainType::River:     return TEXT("River");
        case ETDTerrainType::Swamp:     return TEXT("Swamp");
        case ETDTerrainType::DeepWater: return TEXT("DeepWater");
        default:                        return TEXT("Plain");
        }
    }

    static ETDTerrainType StringToTerrainType(const FString& Str)
    {
        if (Str == TEXT("Plain"))     return ETDTerrainType::Plain;
        if (Str == TEXT("Hill"))      return ETDTerrainType::Hill;
        if (Str == TEXT("Mountain"))  return ETDTerrainType::Mountain;
        if (Str == TEXT("Forest"))    return ETDTerrainType::Forest;
        if (Str == TEXT("River"))     return ETDTerrainType::River;
        if (Str == TEXT("Swamp"))     return ETDTerrainType::Swamp;
        if (Str == TEXT("DeepWater")) return ETDTerrainType::DeepWater;

        UE_LOG(LogTemp, Warning, TEXT("TDSaveData: Unknown terrain type '%s', defaulting to Plain."), *Str);
        return ETDTerrainType::Plain;
    }

    /**
     * 将单个格子数据序列化为 JSON 对象。
     */
    static TSharedRef<FJsonObject> TileSaveDataToJson(const FTDHexTileSaveData& TileData)
    {
        TSharedRef<FJsonObject> JsonObj = MakeShared<FJsonObject>();
        JsonObj->SetNumberField(TEXT("Q"), TileData.Coord.Q);
        JsonObj->SetNumberField(TEXT("R"), TileData.Coord.R);
        JsonObj->SetStringField(TEXT("TerrainType"), TerrainTypeToString(TileData.TerrainType));
        JsonObj->SetNumberField(TEXT("HeightLevel"), TileData.HeightLevel);
        JsonObj->SetNumberField(TEXT("OwnerPlayerIndex"), TileData.OwnerPlayerIndex);
        return JsonObj;
    }

    /**
     * 从 JSON 对象反序列化单个格子数据。
     * 缺失字段使用安全默认值。
     */
    static FTDHexTileSaveData JsonToTileSaveData(const TSharedPtr<FJsonObject>& JsonObj)
    {
        FTDHexTileSaveData TileData;

        if (!JsonObj.IsValid())
        {
            return TileData;
        }

        const int32 TileQ = static_cast<int32>(JsonObj->GetNumberField(TEXT("Q")));
        const int32 TileR = static_cast<int32>(JsonObj->GetNumberField(TEXT("R")));
        TileData.Coord = FTDHexCoord(TileQ, TileR);

        if (JsonObj->HasField(TEXT("TerrainType")))
        {
            const FString TerrainStr = JsonObj->GetStringField(TEXT("TerrainType"));
            TileData.TerrainType = StringToTerrainType(TerrainStr);
        }

        TileData.HeightLevel = static_cast<int32>(JsonObj->GetNumberField(TEXT("HeightLevel")));
        TileData.OwnerPlayerIndex = static_cast<int32>(JsonObj->GetNumberField(TEXT("OwnerPlayerIndex")));

        return TileData;
    }

    /**
     * 将完整地图数据序列化为 JSON 对象。
     */
    static TSharedRef<FJsonObject> GridSaveDataToJson(const FTDHexGridSaveData& GridData)
    {
        TSharedRef<FJsonObject> RootObj = MakeShared<FJsonObject>();
        RootObj->SetNumberField(TEXT("MapRadius"), GridData.MapRadius);
        RootObj->SetNumberField(TEXT("Seed"), GridData.Seed);
        RootObj->SetNumberField(TEXT("Version"), GridData.Version);

        TArray<TSharedPtr<FJsonValue>> TileArray;
        TileArray.Reserve(GridData.TileDataList.Num());

        for (const FTDHexTileSaveData& TileData : GridData.TileDataList)
        {
            TileArray.Add(MakeShared<FJsonValueObject>(TileSaveDataToJson(TileData)));
        }

        RootObj->SetArrayField(TEXT("Tiles"), TileArray);

        return RootObj;
    }

    /**
     * 从 JSON 对象反序列化完整地图数据。
     */
    static bool JsonToGridSaveData(const TSharedPtr<FJsonObject>& RootObj, FTDHexGridSaveData& OutData)
    {
        if (!RootObj.IsValid())
        {
            UE_LOG(LogTemp, Error, TEXT("TDSaveData: JSON root object is invalid."));
            return false;
        }

        OutData.Reset();

        OutData.MapRadius = static_cast<int32>(RootObj->GetNumberField(TEXT("MapRadius")));
        OutData.Seed = static_cast<int32>(RootObj->GetNumberField(TEXT("Seed")));
        OutData.Version = static_cast<int32>(RootObj->GetNumberField(TEXT("Version")));

        if (!RootObj->HasField(TEXT("Tiles")))
        {
            UE_LOG(LogTemp, Error, TEXT("TDSaveData: JSON missing 'Tiles' array."));
            return false;
        }

        const TArray<TSharedPtr<FJsonValue>>& TileArray = RootObj->GetArrayField(TEXT("Tiles"));

        OutData.TileDataList.Reserve(TileArray.Num());

        for (const TSharedPtr<FJsonValue>& TileValue : TileArray)
        {
            const TSharedPtr<FJsonObject>& TileObj = TileValue->AsObject();
            OutData.TileDataList.Add(JsonToTileSaveData(TileObj));
        }

        return true;
    }
}

// ===================================================================
// UTDHexGridSaveGame
// ===================================================================

bool UTDHexGridSaveGame::SaveToSlot(const FString& SlotName, int32 UserIndex)
{
    return UGameplayStatics::SaveGameToSlot(this, SlotName, UserIndex);
}

bool UTDHexGridSaveGame::LoadFromSlot(const FString& SlotName, int32 UserIndex)
{
    USaveGame* LoadedGame = UGameplayStatics::LoadGameFromSlot(SlotName, UserIndex);

    if (!LoadedGame)
    {
        UE_LOG(LogTemp, Warning, TEXT("UTDHexGridSaveGame::LoadFromSlot: Failed to load from slot '%s'."), *SlotName);
        return false;
    }

    UTDHexGridSaveGame* LoadedGridSave = Cast<UTDHexGridSaveGame>(LoadedGame);

    if (!LoadedGridSave)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("UTDHexGridSaveGame::LoadFromSlot: Slot '%s' does not contain UTDHexGridSaveGame."), *SlotName);
        return false;
    }

    GridData = LoadedGridSave->GridData;
    return true;
}

bool UTDHexGridSaveGame::ExportToJsonString(FString& OutJsonString) const
{
    TSharedRef<FJsonObject> RootObj = TDSaveDataInternal::GridSaveDataToJson(GridData);

    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonString);
    const bool bSuccess = FJsonSerializer::Serialize(RootObj, Writer);
    Writer->Close();

    if (!bSuccess)
    {
        UE_LOG(LogTemp, Error, TEXT("UTDHexGridSaveGame::ExportToJsonString: JSON serialization failed."));
    }

    return bSuccess;
}

bool UTDHexGridSaveGame::ImportFromJsonString(const FString& JsonString)
{
    TSharedPtr<FJsonObject> RootObj;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("UTDHexGridSaveGame::ImportFromJsonString: JSON parse failed."));
        return false;
    }

    return TDSaveDataInternal::JsonToGridSaveData(RootObj, GridData);
}

bool UTDHexGridSaveGame::ExportToJsonFile(const FString& FilePath) const
{
    FString JsonString;

    if (!ExportToJsonString(JsonString))
    {
        return false;
    }

    if (!FFileHelper::SaveStringToFile(JsonString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        UE_LOG(LogTemp, Error,
            TEXT("UTDHexGridSaveGame::ExportToJsonFile: Failed to write file '%s'."), *FilePath);
        return false;
    }

    return true;
}

bool UTDHexGridSaveGame::ImportFromJsonFile(const FString& FilePath)
{
    FString JsonString;

    if (!FFileHelper::LoadFileToString(JsonString, *FilePath))
    {
        UE_LOG(LogTemp, Error,
            TEXT("UTDHexGridSaveGame::ImportFromJsonFile: Failed to read file '%s'."), *FilePath);
        return false;
    }

    return ImportFromJsonString(JsonString);
}
