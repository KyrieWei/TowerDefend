// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDCheatManager.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGameMode.h"
#include "Core/TDGamePhaseTypes.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexGridSaveData.h"
#include "HexGrid/TDTerrainGenerator.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDCheat, Log, All);

// ─── 辅助宏 ──────────────────────────────────────────

/** 获取本地 PlayerController */
#define GET_PC() \
    APlayerController* PC = GetOuterAPlayerController(); \
    if (!PC) { UE_LOG(LogTDCheat, Warning, TEXT("%s: No PlayerController"), ANSI_TO_TCHAR(__FUNCTION__)); return; }

/** 获取 PlayerState */
#define GET_PS() \
    GET_PC(); \
    ATDPlayerState* PS = Cast<ATDPlayerState>(PC->PlayerState); \
    if (!PS) { UE_LOG(LogTDCheat, Warning, TEXT("%s: No TDPlayerState"), ANSI_TO_TCHAR(__FUNCTION__)); return; }

/** 获取 GameMode */
#define GET_GM() \
    ATDGameMode* GM = GetWorld() ? Cast<ATDGameMode>(GetWorld()->GetAuthGameMode()) : nullptr; \
    if (!GM) { UE_LOG(LogTDCheat, Warning, TEXT("%s: No TDGameMode"), ANSI_TO_TCHAR(__FUNCTION__)); return; }

/** 获取 HexGridManager（场景中唯一） */
#define GET_GRID() \
    ATDHexGridManager* Grid = Cast<ATDHexGridManager>( \
        UGameplayStatics::GetActorOfClass(GetWorld(), ATDHexGridManager::StaticClass())); \
    if (!Grid) { UE_LOG(LogTDCheat, Warning, TEXT("%s: No HexGridManager in level"), ANSI_TO_TCHAR(__FUNCTION__)); return; }

// ===================================================================
// 地图操作
// ===================================================================

void UTDCheatManager::CheatSaveMap(FString SlotName)
{
    GET_GRID();

    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    SaveGame->GridData = Grid->ExportSaveData();

    if (SaveGame->SaveToSlot(SlotName))
    {
        UE_LOG(LogTDCheat, Log, TEXT("CheatSaveMap: Saved to slot '%s' (%d tiles)"),
            *SlotName, SaveGame->GridData.GetTileCount());
    }
    else
    {
        UE_LOG(LogTDCheat, Error, TEXT("CheatSaveMap: Failed to save to slot '%s'"), *SlotName);
    }
}

void UTDCheatManager::CheatLoadMap(FString SlotName)
{
    GET_GRID();

    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    if (SaveGame->LoadFromSlot(SlotName))
    {
        Grid->ApplySaveData(SaveGame->GridData);
        UE_LOG(LogTDCheat, Log, TEXT("CheatLoadMap: Loaded from slot '%s' (%d tiles)"),
            *SlotName, SaveGame->GridData.GetTileCount());
    }
    else
    {
        UE_LOG(LogTDCheat, Error, TEXT("CheatLoadMap: Failed to load from slot '%s'"), *SlotName);
    }
}

void UTDCheatManager::CheatExportMapJson(FString FilePath)
{
    GET_GRID();

    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    SaveGame->GridData = Grid->ExportSaveData();

    if (SaveGame->ExportToJsonFile(FilePath))
    {
        UE_LOG(LogTDCheat, Log, TEXT("CheatExportMapJson: Exported to '%s' (%d tiles)"),
            *FilePath, SaveGame->GridData.GetTileCount());
    }
    else
    {
        UE_LOG(LogTDCheat, Error, TEXT("CheatExportMapJson: Failed to export to '%s'"), *FilePath);
    }
}

void UTDCheatManager::CheatImportMapJson(FString FilePath)
{
    GET_GRID();

    UTDHexGridSaveGame* SaveGame = NewObject<UTDHexGridSaveGame>();
    if (SaveGame->ImportFromJsonFile(FilePath))
    {
        Grid->ApplySaveData(SaveGame->GridData);
        UE_LOG(LogTDCheat, Log, TEXT("CheatImportMapJson: Imported from '%s' (%d tiles)"),
            *FilePath, SaveGame->GridData.GetTileCount());
    }
    else
    {
        UE_LOG(LogTDCheat, Error, TEXT("CheatImportMapJson: Failed to import from '%s'"), *FilePath);
    }
}

void UTDCheatManager::CheatGenerateMap(int32 Radius, int32 Seed)
{
    GET_GRID();

    // 设置种子
    if (Grid->TerrainGenerator)
    {
        Grid->TerrainGenerator->Seed = Seed;
    }

    Grid->GenerateGrid(Radius);

    UE_LOG(LogTDCheat, Log, TEXT("CheatGenerateMap: Generated map with Radius=%d, Seed=%d"),
        Radius, Seed);
}

// ===================================================================
// 资源操作
// ===================================================================

void UTDCheatManager::CheatSetGold(int32 Amount)
{
    GET_PS();

    const int32 CurrentGold = PS->GetGold();
    if (CurrentGold > 0)
    {
        PS->SpendGold(CurrentGold);
    }
    if (Amount > 0)
    {
        PS->AddGold(Amount);
    }

    UE_LOG(LogTDCheat, Log, TEXT("CheatSetGold: Gold set to %d (was %d)"), Amount, CurrentGold);
}

void UTDCheatManager::CheatAddGold(int32 Amount)
{
    GET_PS();

    PS->AddGold(Amount);

    UE_LOG(LogTDCheat, Log, TEXT("CheatAddGold: Added %d gold (now %d)"), Amount, PS->GetGold());
}

void UTDCheatManager::CheatSetResearch(int32 Amount)
{
    GET_PS();

    const int32 CurrentRP = PS->GetResearchPoints();
    if (CurrentRP > 0)
    {
        PS->SpendResearchPoints(CurrentRP);
    }
    if (Amount > 0)
    {
        PS->AddResearchPoints(Amount);
    }

    UE_LOG(LogTDCheat, Log, TEXT("CheatSetResearch: Research set to %d (was %d)"), Amount, CurrentRP);
}

void UTDCheatManager::CheatAddResearch(int32 Amount)
{
    GET_PS();

    PS->AddResearchPoints(Amount);

    UE_LOG(LogTDCheat, Log, TEXT("CheatAddResearch: Added %d RP (now %d)"), Amount, PS->GetResearchPoints());
}

// ===================================================================
// 阶段控制
// ===================================================================

void UTDCheatManager::CheatNextPhase()
{
    GET_GM();

    const ETDGamePhase OldPhase = GM->GetCurrentPhase();
    GM->AdvanceToNextPhase();

    UE_LOG(LogTDCheat, Log, TEXT("CheatNextPhase: Advanced from phase %d to %d"),
        static_cast<uint8>(OldPhase), static_cast<uint8>(GM->GetCurrentPhase()));
}

void UTDCheatManager::CheatSetPhase(FString PhaseName)
{
#if !UE_BUILD_SHIPPING
    GET_GM();

    // 将字符串映射到枚举
    ETDGamePhase TargetPhase = ETDGamePhase::None;
    bool bFound = false;

    if (PhaseName.Equals(TEXT("None"), ESearchCase::IgnoreCase))
    {
        TargetPhase = ETDGamePhase::None; bFound = true;
    }
    else if (PhaseName.Equals(TEXT("Preparation"), ESearchCase::IgnoreCase))
    {
        TargetPhase = ETDGamePhase::Preparation; bFound = true;
    }
    else if (PhaseName.Equals(TEXT("Matchmaking"), ESearchCase::IgnoreCase))
    {
        TargetPhase = ETDGamePhase::Matchmaking; bFound = true;
    }
    else if (PhaseName.Equals(TEXT("Battle"), ESearchCase::IgnoreCase))
    {
        TargetPhase = ETDGamePhase::Battle; bFound = true;
    }
    else if (PhaseName.Equals(TEXT("Settlement"), ESearchCase::IgnoreCase))
    {
        TargetPhase = ETDGamePhase::Settlement; bFound = true;
    }
    else if (PhaseName.Equals(TEXT("GameOver"), ESearchCase::IgnoreCase))
    {
        TargetPhase = ETDGamePhase::GameOver; bFound = true;
    }

    if (!bFound)
    {
        UE_LOG(LogTDCheat, Warning,
            TEXT("CheatSetPhase: Unknown phase '%s'. Valid: None, Preparation, Matchmaking, Battle, Settlement, GameOver"),
            *PhaseName);
        return;
    }

    GM->CheatSetPhase(TargetPhase);

    UE_LOG(LogTDCheat, Log, TEXT("CheatSetPhase: Phase set to '%s' (%d)"),
        *PhaseName, static_cast<uint8>(TargetPhase));
#else
    UE_LOG(LogTDCheat, Warning, TEXT("CheatSetPhase: Not available in Shipping builds"));
#endif
}

void UTDCheatManager::CheatStartMatch()
{
    GET_GM();

    GM->StartMatch();

    UE_LOG(LogTDCheat, Log, TEXT("CheatStartMatch: Match started"));
}

void UTDCheatManager::CheatEndMatch()
{
    GET_GM();

    GM->EndMatch();

    UE_LOG(LogTDCheat, Log, TEXT("CheatEndMatch: Match ended"));
}

// ===================================================================
// 生命值操作
// ===================================================================

void UTDCheatManager::CheatSetHealth(int32 Amount)
{
    GET_PS();

    const int32 MaxHP = PS->GetMaxHealth();
    const int32 ClampedAmount = FMath::Clamp(Amount, 0, MaxHP);

    // 先回满血，再扣到目标值
    PS->HealHealth(MaxHP);
    if (ClampedAmount < MaxHP)
    {
        PS->ApplyDamage(MaxHP - ClampedAmount);
    }

    UE_LOG(LogTDCheat, Log, TEXT("CheatSetHealth: Health set to %d/%d"), ClampedAmount, MaxHP);
}

void UTDCheatManager::CheatGodMode()
{
    GET_PS();

    bGodMode = !bGodMode;

    if (bGodMode)
    {
        // 恢复满血
        PS->HealHealth(PS->GetMaxHealth());
    }

    UE_LOG(LogTDCheat, Log, TEXT("CheatGodMode: God mode %s (Health: %d/%d)"),
        bGodMode ? TEXT("ON") : TEXT("OFF"), PS->GetHealth(), PS->GetMaxHealth());
}
