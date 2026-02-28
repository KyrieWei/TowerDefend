// Copyright TowerDefend. All Rights Reserved.

#include "Unit/TDUnitSquad.h"
#include "Unit/TDUnitBase.h"

// ===================================================================
// 添加 / 移除
// ===================================================================

void UTDUnitSquad::AddUnit(ATDUnitBase* Unit)
{
    if (!ensure(Unit != nullptr))
    {
        UE_LOG(LogTemp, Error, TEXT("UTDUnitSquad::AddUnit - Unit is null!"));
        return;
    }

    FTDHexCoord Coord = Unit->GetCurrentCoord();

    // 如果该坐标已有单位，输出警告
    if (ATDUnitBase* ExistingUnit = UnitMap.FindRef(Coord))
    {
        if (ExistingUnit != Unit)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("UTDUnitSquad::AddUnit - Overwriting existing unit at %s"),
                *Coord.ToString());
        }
    }

    UnitMap.Add(Coord, Unit);
}

void UTDUnitSquad::RemoveUnit(ATDUnitBase* Unit)
{
    if (!Unit)
    {
        return;
    }

    FTDHexCoord Coord = Unit->GetCurrentCoord();

    // 确保映射中的单位和待移除的单位是同一个
    ATDUnitBase* MappedUnit = UnitMap.FindRef(Coord);
    if (MappedUnit == Unit)
    {
        UnitMap.Remove(Coord);
    }
    else
    {
        // 单位可能已经移动过坐标，遍历查找
        for (auto It = UnitMap.CreateIterator(); It; ++It)
        {
            if (It->Value == Unit)
            {
                It.RemoveCurrent();
                break;
            }
        }
    }
}

// ===================================================================
// 查询
// ===================================================================

TArray<ATDUnitBase*> UTDUnitSquad::GetAllUnits() const
{
    TArray<ATDUnitBase*> Result;
    UnitMap.GenerateValueArray(Result);
    return Result;
}

TArray<ATDUnitBase*> UTDUnitSquad::GetUnitsByOwner(int32 OwnerPlayerIndex) const
{
    TArray<ATDUnitBase*> Result;
    for (const auto& Pair : UnitMap)
    {
        if (IsValid(Pair.Value) && Pair.Value->GetOwnerPlayerIndex() == OwnerPlayerIndex)
        {
            Result.Add(Pair.Value);
        }
    }
    return Result;
}

TArray<ATDUnitBase*> UTDUnitSquad::GetUnitsInRange(const FTDHexCoord& Center, int32 Range) const
{
    TArray<ATDUnitBase*> Result;
    for (const auto& Pair : UnitMap)
    {
        if (!IsValid(Pair.Value))
        {
            continue;
        }

        int32 Distance = Center.DistanceTo(Pair.Key);
        if (Distance <= Range)
        {
            Result.Add(Pair.Value);
        }
    }
    return Result;
}

ATDUnitBase* UTDUnitSquad::GetUnitAt(const FTDHexCoord& Coord) const
{
    ATDUnitBase* const* Found = UnitMap.Find(Coord);
    if (Found && IsValid(*Found))
    {
        return *Found;
    }
    return nullptr;
}

int32 UTDUnitSquad::GetUnitCount() const
{
    return UnitMap.Num();
}

int32 UTDUnitSquad::GetUnitCountByOwner(int32 OwnerPlayerIndex) const
{
    int32 Count = 0;
    for (const auto& Pair : UnitMap)
    {
        if (IsValid(Pair.Value) && Pair.Value->GetOwnerPlayerIndex() == OwnerPlayerIndex)
        {
            ++Count;
        }
    }
    return Count;
}

// ===================================================================
// 批量操作
// ===================================================================

void UTDUnitSquad::ResetAllMovePoints()
{
    for (auto& Pair : UnitMap)
    {
        if (IsValid(Pair.Value) && !Pair.Value->IsDead())
        {
            Pair.Value->ResetMovePoints();
        }
    }
}

void UTDUnitSquad::RemoveDeadUnits()
{
    TArray<FTDHexCoord> DeadCoords;

    for (const auto& Pair : UnitMap)
    {
        if (!IsValid(Pair.Value) || Pair.Value->IsDead())
        {
            DeadCoords.Add(Pair.Key);
        }
    }

    for (const FTDHexCoord& Coord : DeadCoords)
    {
        UnitMap.Remove(Coord);
    }
}

void UTDUnitSquad::ClearAllUnits()
{
    UnitMap.Empty();
}

void UTDUnitSquad::UpdateUnitPosition(ATDUnitBase* Unit, const FTDHexCoord& OldCoord)
{
    if (!ensure(Unit != nullptr))
    {
        return;
    }

    // 移除旧坐标的映射
    ATDUnitBase* MappedUnit = UnitMap.FindRef(OldCoord);
    if (MappedUnit == Unit)
    {
        UnitMap.Remove(OldCoord);
    }

    // 在新坐标上注册
    FTDHexCoord NewCoord = Unit->GetCurrentCoord();
    UnitMap.Add(NewCoord, Unit);
}
