// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDBlueprintLibrary.h"

#include "Core/TDGameState.h"
#include "Core/TDGameMode.h"
#include "Core/TDPlayerState.h"
#include "Core/TDPlayerController.h"
#include "Core/TDMapFileManager.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "Building/TDBuildingManager.h"
#include "Building/TDBuildingDataAsset.h"
#include "Unit/TDUnitSquad.h"
#include "Unit/TDUnitDataAsset.h"

#include "Engine/World.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

// ═══════════════════════════════════════════════════════
//  核心对象获取
// ═══════════════════════════════════════════════════════

ATDGameState* UTDBlueprintLibrary::GetTDGameState(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;
	return Cast<ATDGameState>(World->GetGameState());
}

ATDGameMode* UTDBlueprintLibrary::GetTDGameMode(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;
	return Cast<ATDGameMode>(World->GetAuthGameMode());
}

ATDPlayerState* UTDBlueprintLibrary::GetLocalTDPlayerState(const UObject* WorldContextObject)
{
	const ATDPlayerController* PC = GetLocalTDPlayerController(WorldContextObject);
	return PC ? Cast<ATDPlayerState>(PC->PlayerState) : nullptr;
}

ATDPlayerController* UTDBlueprintLibrary::GetLocalTDPlayerController(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;
	return Cast<ATDPlayerController>(World->GetFirstPlayerController());
}

ATDPlayerState* UTDBlueprintLibrary::GetTDPlayerStateByIndex(const UObject* WorldContextObject, int32 PlayerIndex)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	if (!GS) return nullptr;

	const TArray<APlayerState*>& PlayerArray = GS->PlayerArray;
	if (!PlayerArray.IsValidIndex(PlayerIndex)) return nullptr;

	return Cast<ATDPlayerState>(PlayerArray[PlayerIndex]);
}

// ═══════════════════════════════════════════════════════
//  游戏阶段 & 回合
// ═══════════════════════════════════════════════════════

ETDGamePhase UTDBlueprintLibrary::GetCurrentGamePhase(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->GetCurrentPhase() : ETDGamePhase::None;
}

int32 UTDBlueprintLibrary::GetCurrentRound(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->GetCurrentRound() : 0;
}

int32 UTDBlueprintLibrary::GetMaxRounds(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->GetMaxRounds() : 0;
}

float UTDBlueprintLibrary::GetPhaseRemainingTime(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->GetPhaseRemainingTime() : 0.0f;
}

bool UTDBlueprintLibrary::IsInPreparationPhase(const UObject* WorldContextObject)
{
	return GetCurrentGamePhase(WorldContextObject) == ETDGamePhase::Preparation;
}

bool UTDBlueprintLibrary::IsInBattlePhase(const UObject* WorldContextObject)
{
	return GetCurrentGamePhase(WorldContextObject) == ETDGamePhase::Battle;
}

bool UTDBlueprintLibrary::IsGameOver(const UObject* WorldContextObject)
{
	return GetCurrentGamePhase(WorldContextObject) == ETDGamePhase::GameOver;
}

FText UTDBlueprintLibrary::GetGamePhaseDisplayName(ETDGamePhase Phase)
{
	switch (Phase)
	{
	case ETDGamePhase::None:         return NSLOCTEXT("TD", "Phase_None", "未开始");
	case ETDGamePhase::Preparation:  return NSLOCTEXT("TD", "Phase_Preparation", "准备阶段");
	case ETDGamePhase::Matchmaking:  return NSLOCTEXT("TD", "Phase_Matchmaking", "配对阶段");
	case ETDGamePhase::Battle:       return NSLOCTEXT("TD", "Phase_Battle", "战斗阶段");
	case ETDGamePhase::Settlement:   return NSLOCTEXT("TD", "Phase_Settlement", "结算阶段");
	case ETDGamePhase::GameOver:     return NSLOCTEXT("TD", "Phase_GameOver", "游戏结束");
	default:                         return NSLOCTEXT("TD", "Phase_Unknown", "未知");
	}
}

// ═══════════════════════════════════════════════════════
//  本地玩家数据快捷访问
// ═══════════════════════════════════════════════════════

int32 UTDBlueprintLibrary::GetLocalPlayerGold(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetGold() : 0;
}

int32 UTDBlueprintLibrary::GetLocalPlayerHealth(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetHealth() : 0;
}

int32 UTDBlueprintLibrary::GetLocalPlayerMaxHealth(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetMaxHealth() : 0;
}

int32 UTDBlueprintLibrary::GetLocalPlayerResearchPoints(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetResearchPoints() : 0;
}

int32 UTDBlueprintLibrary::GetLocalPlayerTechEra(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetCurrentTechEra() : 0;
}

bool UTDBlueprintLibrary::IsLocalPlayerAlive(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->IsAlive() : false;
}

int32 UTDBlueprintLibrary::GetLocalPlayerWinCount(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetWinCount() : 0;
}

int32 UTDBlueprintLibrary::GetLocalPlayerLossCount(const UObject* WorldContextObject)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->GetLossCount() : 0;
}

bool UTDBlueprintLibrary::CanLocalPlayerAfford(const UObject* WorldContextObject, int32 Cost)
{
	const ATDPlayerState* PS = GetLocalTDPlayerState(WorldContextObject);
	return PS ? PS->CanAfford(Cost) : false;
}

// ═══════════════════════════════════════════════════════
//  对局信息
// ═══════════════════════════════════════════════════════

FTDMatchConfig UTDBlueprintLibrary::GetMatchConfig(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->GetMatchConfig() : FTDMatchConfig();
}

int32 UTDBlueprintLibrary::GetAlivePlayerCount(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->GetAlivePlayerCount() : 0;
}

TArray<ATDPlayerState*> UTDBlueprintLibrary::GetAllPlayerStates(const UObject* WorldContextObject)
{
	TArray<ATDPlayerState*> Result;
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	if (!GS) return Result;

	for (APlayerState* PS : GS->PlayerArray)
	{
		if (ATDPlayerState* TDPS = Cast<ATDPlayerState>(PS))
		{
			Result.Add(TDPS);
		}
	}
	return Result;
}

TArray<ATDPlayerState*> UTDBlueprintLibrary::GetAlivePlayerStates(const UObject* WorldContextObject)
{
	TArray<ATDPlayerState*> Result;
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	if (!GS) return Result;

	for (APlayerState* PS : GS->PlayerArray)
	{
		ATDPlayerState* TDPS = Cast<ATDPlayerState>(PS);
		if (TDPS && TDPS->IsAlive())
		{
			Result.Add(TDPS);
		}
	}
	return Result;
}

int32 UTDBlueprintLibrary::GetTotalPlayerCount(const UObject* WorldContextObject)
{
	const ATDGameState* GS = GetTDGameState(WorldContextObject);
	return GS ? GS->PlayerArray.Num() : 0;
}

// ═══════════════════════════════════════════════════════
//  地图管理
// ═══════════════════════════════════════════════════════

ATDHexGridManager* UTDBlueprintLibrary::GetHexGridManager(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World) return nullptr;
	return Cast<ATDHexGridManager>(
		UGameplayStatics::GetActorOfClass(World, ATDHexGridManager::StaticClass()));
}

bool UTDBlueprintLibrary::SaveMapToFile(const UObject* WorldContextObject, const FString& MapName)
{
	ATDHexGridManager* Grid = GetHexGridManager(WorldContextObject);
	if (!Grid)
	{
		return false;
	}

	// MapName 为空时使用默认序列化路径，并自动轮转保留最近 10 个历史文件
	if (MapName.IsEmpty())
	{
		return UTDMapFileManager::SaveMapToDefaultPath(Grid, 10);
	}

	return UTDMapFileManager::SaveMapToFile(Grid, MapName);
}

bool UTDBlueprintLibrary::LoadMapFromFile(const UObject* WorldContextObject, const FString& MapName)
{
	ATDHexGridManager* Grid = GetHexGridManager(WorldContextObject);
	if (!Grid)
	{
		return false;
	}

	// 确定文件路径
	FString FilePath;
	if (MapName.IsEmpty())
	{
		FilePath = UTDMapFileManager::GetDefaultSerializationFilePath();
	}
	else
	{
		FilePath = UTDMapFileManager::GetMapFilePath(MapName);
	}

	return LoadMapFromFileWithEntities(WorldContextObject, Grid, FilePath);
}

TArray<FString> UTDBlueprintLibrary::GetAvailableMapNames()
{
	return UTDMapFileManager::GetAvailableMapNames();
}

bool UTDBlueprintLibrary::SaveMapToSlot(const UObject* WorldContextObject, const FString& SlotName)
{
	ATDHexGridManager* Grid = GetHexGridManager(WorldContextObject);
	if (!Grid)
	{
		return false;
	}

	UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
	SaveGame->GridData = Grid->ExportSaveData();
	return SaveGame->SaveToSlot(SlotName);
}

bool UTDBlueprintLibrary::LoadMapFromSlot(const UObject* WorldContextObject, const FString& SlotName)
{
	ATDHexGridManager* Grid = GetHexGridManager(WorldContextObject);
	if (!Grid)
	{
		return false;
	}

	UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
	if (!SaveGame->LoadFromSlot(SlotName))
	{
		return false;
	}

	Grid->ApplySaveData(SaveGame->GridData);
	return true;
}

void UTDBlueprintLibrary::RegenerateMap(const UObject* WorldContextObject, int32 Radius)
{
	ATDHexGridManager* Grid = GetHexGridManager(WorldContextObject);
	if (Grid)
	{
		Grid->GenerateGrid(Radius);
	}
}

// ═══════════════════════════════════════════════════════
//  内部辅助
// ═══════════════════════════════════════════════════════

TArray<UTDBuildingDataAsset*> UTDBlueprintLibrary::CollectBuildingDataAssets()
{
	TArray<UTDBuildingDataAsset*> Result;

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
		.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(
		UTDBuildingDataAsset::StaticClass()->GetClassPathName(),
		AssetDataList, true);

	for (const FAssetData& AssetData : AssetDataList)
	{
		UTDBuildingDataAsset* Asset =
			Cast<UTDBuildingDataAsset>(AssetData.GetAsset());
		if (Asset)
		{
			Result.Add(Asset);
		}
	}

	return Result;
}

TArray<UTDUnitDataAsset*> UTDBlueprintLibrary::CollectUnitDataAssets()
{
	TArray<UTDUnitDataAsset*> Result;

	IAssetRegistry& AssetRegistry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry")
		.Get();

	TArray<FAssetData> AssetDataList;
	AssetRegistry.GetAssetsByClass(
		UTDUnitDataAsset::StaticClass()->GetClassPathName(),
		AssetDataList, true);

	for (const FAssetData& AssetData : AssetDataList)
	{
		UTDUnitDataAsset* Asset =
			Cast<UTDUnitDataAsset>(AssetData.GetAsset());
		if (Asset)
		{
			Result.Add(Asset);
		}
	}

	return Result;
}

bool UTDBlueprintLibrary::LoadMapFromFileWithEntities(
	const UObject* WorldContextObject,
	ATDHexGridManager* Grid,
	const FString& FilePath)
{
	if (!FPaths::FileExists(FilePath))
	{
		UE_LOG(LogTemp, Error,
			TEXT("UTDBlueprintLibrary::LoadMapFromFileWithEntities: "
				 "File not found: '%s'."), *FilePath);
		return false;
	}

	// 解析 JSON
	UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
	if (!SaveGame->ImportFromJsonFile(FilePath))
	{
		UE_LOG(LogTemp, Error,
			TEXT("UTDBlueprintLibrary::LoadMapFromFileWithEntities: "
				 "Failed to parse JSON: '%s'."), *FilePath);
		return false;
	}

	// 恢复地形
	Grid->ApplySaveData(SaveGame->GridData);

	UWorld* World = Grid->GetWorld();

	// 恢复建筑（V2 数据存在时）
	int32 BuildingCount = 0;
	if (SaveGame->GridData.BuildingDataList.Num() > 0 && World)
	{
		TArray<UTDBuildingDataAsset*> BuildingAssets = CollectBuildingDataAssets();

		UTDBuildingManager* TempBuildingMgr =
			NewObject<UTDBuildingManager>();
		TempBuildingMgr->ClearAllBuildings();
		BuildingCount = TempBuildingMgr->ImportBuildingData(
			World, Grid,
			SaveGame->GridData.BuildingDataList,
			BuildingAssets);
	}

	// 恢复单位（V2 数据存在时）
	int32 UnitCount = 0;
	if (SaveGame->GridData.UnitDataList.Num() > 0 && World)
	{
		TArray<UTDUnitDataAsset*> UnitAssets = CollectUnitDataAssets();

		UTDUnitSquad* TempUnitSquad = NewObject<UTDUnitSquad>();
		TempUnitSquad->ClearAllUnits();
		UnitCount = TempUnitSquad->ImportUnitData(
			World,
			SaveGame->GridData.UnitDataList,
			UnitAssets);
	}

	UE_LOG(LogTemp, Log,
		TEXT("Map loaded: %d tiles, %d buildings, %d units <- %s"),
		SaveGame->GridData.GetTileCount(),
		BuildingCount, UnitCount, *FilePath);

	return true;
}
