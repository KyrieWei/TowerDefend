// Copyright TowerDefend. All Rights Reserved.

#include "HexGrid/TDHexCoord.h"

// ===================================================================
// 内部辅助：常量与工具函数
// ===================================================================

namespace TDHexCoordInternal
{
    /** 预计算 sqrt(3)，供世界坐标转换使用。 */
    static const float Sqrt3 = FMath::Sqrt(3.0f);

    /**
     * 平顶六边形的 6 个方向偏移。
     * 索引 0-5，从东方 (East) 顺时针排列。
     */
    static const FTDHexCoord Directions[6] =
    {
        FTDHexCoord(+1,  0, -1),  // 0: East
        FTDHexCoord(+1, -1,  0),  // 1: NE
        FTDHexCoord( 0, -1, +1),  // 2: NW
        FTDHexCoord(-1,  0, +1),  // 3: West
        FTDHexCoord(-1, +1,  0),  // 4: SW
        FTDHexCoord( 0, +1, -1),  // 5: SE
    };

    /**
     * 构造无效哨兵坐标，故意违反 Q+R+S==0 不变量。
     * 不使用 3-arg 构造函数以避免 checkSlow 断言。
     */
    static FTDHexCoord MakeInvalidCoord()
    {
        FTDHexCoord Coord;
        Coord.Q = INT32_MAX;
        Coord.R = INT32_MAX;
        Coord.S = INT32_MAX;
        return Coord;
    }

    /** 无效哨兵坐标实例。 */
    static const FTDHexCoord InvalidCoord = MakeInvalidCoord();

    /**
     * 将浮点立方坐标取整到最近的整数六边形坐标。
     * 对三个轴分别取整，然后将取整误差最大的轴
     * 用另外两轴重新计算，以保证 Q+R+S==0 不变量。
     */
    static FTDHexCoord CubeRound(float FracQ, float FracR, float FracS)
    {
        int32 RoundQ = FMath::RoundToInt32(FracQ);
        int32 RoundR = FMath::RoundToInt32(FracR);
        int32 RoundS = FMath::RoundToInt32(FracS);

        const float DiffQ = FMath::Abs(static_cast<float>(RoundQ) - FracQ);
        const float DiffR = FMath::Abs(static_cast<float>(RoundR) - FracR);
        const float DiffS = FMath::Abs(static_cast<float>(RoundS) - FracS);

        // 误差最大的轴由另外两轴决定
        if (DiffQ > DiffR && DiffQ > DiffS)
        {
            RoundQ = -RoundR - RoundS;
        }
        else if (DiffR > DiffS)
        {
            RoundR = -RoundQ - RoundS;
        }
        else
        {
            RoundS = -RoundQ - RoundR;
        }

        return FTDHexCoord(RoundQ, RoundR, RoundS);
    }
}

// ===================================================================
// 构造函数
// ===================================================================

FTDHexCoord::FTDHexCoord()
    : Q(0)
    , R(0)
    , S(0)
{
}

FTDHexCoord::FTDHexCoord(int32 InQ, int32 InR)
    : Q(InQ)
    , R(InR)
    , S(-InQ - InR)
{
}

FTDHexCoord::FTDHexCoord(int32 InQ, int32 InR, int32 InS)
    : Q(InQ)
    , R(InR)
    , S(InS)
{
    // 仅在 Debug/Development 构建中检查不变量
    checkSlow(InQ + InR + InS == 0);
}

// ===================================================================
// 工厂方法
// ===================================================================

FTDHexCoord FTDHexCoord::FromAxial(int32 InQ, int32 InR)
{
    return FTDHexCoord(InQ, InR);
}

const FTDHexCoord& FTDHexCoord::Invalid()
{
    return TDHexCoordInternal::InvalidCoord;
}

// ===================================================================
// 校验
// ===================================================================

bool FTDHexCoord::IsValid() const
{
    return (Q + R + S) == 0;
}

// ===================================================================
// 比较运算符
// ===================================================================

bool FTDHexCoord::operator==(const FTDHexCoord& Other) const
{
    return Q == Other.Q && R == Other.R && S == Other.S;
}

bool FTDHexCoord::operator!=(const FTDHexCoord& Other) const
{
    return !(*this == Other);
}

// ===================================================================
// 算术运算符
// ===================================================================

FTDHexCoord FTDHexCoord::operator+(const FTDHexCoord& Other) const
{
    return FTDHexCoord(Q + Other.Q, R + Other.R, S + Other.S);
}

FTDHexCoord FTDHexCoord::operator-(const FTDHexCoord& Other) const
{
    return FTDHexCoord(Q - Other.Q, R - Other.R, S - Other.S);
}

FTDHexCoord FTDHexCoord::operator*(int32 Scalar) const
{
    return FTDHexCoord(Q * Scalar, R * Scalar, S * Scalar);
}

// ===================================================================
// 距离
// ===================================================================

int32 FTDHexCoord::DistanceTo(const FTDHexCoord& Other) const
{
    return Distance(*this, Other);
}

int32 FTDHexCoord::Distance(const FTDHexCoord& A, const FTDHexCoord& B)
{
    return FMath::Max3(
        FMath::Abs(A.Q - B.Q),
        FMath::Abs(A.R - B.R),
        FMath::Abs(A.S - B.S)
    );
}

// ===================================================================
// 邻居查询
// ===================================================================

FTDHexCoord FTDHexCoord::GetNeighbor(int32 Direction) const
{
    ensureMsgf(
        Direction >= 0 && Direction <= 5,
        TEXT("FTDHexCoord::GetNeighbor: Direction %d out of range [0, 5], will be clamped."),
        Direction
    );

    const int32 ClampedDirection = FMath::Clamp(Direction, 0, 5);
    return *this + TDHexCoordInternal::Directions[ClampedDirection];
}

TArray<FTDHexCoord> FTDHexCoord::GetAllNeighbors() const
{
    TArray<FTDHexCoord> Neighbors;
    Neighbors.Reserve(6);

    for (int32 Dir = 0; Dir < 6; ++Dir)
    {
        Neighbors.Add(*this + TDHexCoordInternal::Directions[Dir]);
    }

    return Neighbors;
}

// ===================================================================
// 范围查询
// ===================================================================

TArray<FTDHexCoord> FTDHexCoord::GetCoordsInRange(int32 Range) const
{
    const int32 ClampedRange = FMath::Max(Range, 0);
    const int32 ExpectedCount = 3 * ClampedRange * ClampedRange + 3 * ClampedRange + 1;

    TArray<FTDHexCoord> Results;
    Results.Reserve(ExpectedCount);

    for (int32 DeltaQ = -ClampedRange; DeltaQ <= ClampedRange; ++DeltaQ)
    {
        const int32 MinR = FMath::Max(-ClampedRange, -DeltaQ - ClampedRange);
        const int32 MaxR = FMath::Min(ClampedRange, -DeltaQ + ClampedRange);

        for (int32 DeltaR = MinR; DeltaR <= MaxR; ++DeltaR)
        {
            const int32 DeltaS = -DeltaQ - DeltaR;
            Results.Add(FTDHexCoord(Q + DeltaQ, R + DeltaR, S + DeltaS));
        }
    }

    return Results;
}

TArray<FTDHexCoord> FTDHexCoord::GetRing(int32 RingRadius) const
{
    const int32 ClampedRadius = FMath::Max(RingRadius, 0);

    // 半径为 0 时仅返回自身
    if (ClampedRadius == 0)
    {
        return { *this };
    }

    const int32 ExpectedCount = 6 * ClampedRadius;
    TArray<FTDHexCoord> Results;
    Results.Reserve(ExpectedCount);

    // 从 SW 方向出发 ClampedRadius 步作为起点
    FTDHexCoord Current = *this + TDHexCoordInternal::Directions[4] * ClampedRadius;

    // 沿环的 6 条边各走 ClampedRadius 步
    for (int32 EdgeDir = 0; EdgeDir < 6; ++EdgeDir)
    {
        for (int32 Step = 0; Step < ClampedRadius; ++Step)
        {
            Results.Add(Current);
            Current = Current + TDHexCoordInternal::Directions[EdgeDir];
        }
    }

    return Results;
}

// ===================================================================
// 连线
// ===================================================================

TArray<FTDHexCoord> FTDHexCoord::LineTo(const FTDHexCoord& Target) const
{
    const int32 Dist = DistanceTo(Target);

    TArray<FTDHexCoord> Results;
    Results.Reserve(Dist + 1);

    // 起点与终点重合
    if (Dist == 0)
    {
        Results.Add(*this);
        return Results;
    }

    const float InvDist = 1.0f / static_cast<float>(Dist);

    // 沿浮点立方空间线性插值，每个采样点取整到最近六边形
    for (int32 I = 0; I <= Dist; ++I)
    {
        const float T = static_cast<float>(I) * InvDist;

        const float FracQ = FMath::Lerp(
            static_cast<float>(Q), static_cast<float>(Target.Q), T);
        const float FracR = FMath::Lerp(
            static_cast<float>(R), static_cast<float>(Target.R), T);
        const float FracS = FMath::Lerp(
            static_cast<float>(S), static_cast<float>(Target.S), T);

        Results.Add(TDHexCoordInternal::CubeRound(FracQ, FracR, FracS));
    }

    return Results;
}

// ===================================================================
// 世界坐标转换
// ===================================================================

FVector FTDHexCoord::ToWorldPosition(float HexSize) const
{
    // 平顶六边形: X = size * 3/2 * Q, Y = size * (sqrt3/2 * Q + sqrt3 * R)
    const float WorldX = HexSize * 1.5f * static_cast<float>(Q);
    const float WorldY = HexSize * (
        TDHexCoordInternal::Sqrt3 * 0.5f * static_cast<float>(Q)
        + TDHexCoordInternal::Sqrt3 * static_cast<float>(R)
    );

    return FVector(WorldX, WorldY, 0.0f);
}

FTDHexCoord FTDHexCoord::FromWorldPosition(
    const FVector& WorldPosition,
    float HexSize)
{
    ensureMsgf(
        !FMath::IsNearlyZero(HexSize),
        TEXT("FTDHexCoord::FromWorldPosition: HexSize must not be zero.")
    );

    if (FMath::IsNearlyZero(HexSize))
    {
        return FTDHexCoord::Invalid();
    }

    const float InvSize = 1.0f / HexSize;

    // 平顶六边形逆变换: FracQ = 2/3 * X / size
    const float FracQ = (2.0f / 3.0f) * WorldPosition.X * InvSize;

    // FracR = (-1/3 * X + sqrt3/3 * Y) / size
    const float FracR = (
        -1.0f / 3.0f * WorldPosition.X
        + TDHexCoordInternal::Sqrt3 / 3.0f * WorldPosition.Y
    ) * InvSize;

    const float FracS = -FracQ - FracR;

    return TDHexCoordInternal::CubeRound(FracQ, FracR, FracS);
}

// ===================================================================
// 调试
// ===================================================================

FString FTDHexCoord::ToString() const
{
    return FString::Printf(TEXT("(Q=%d, R=%d, S=%d)"), Q, R, S);
}

// ===================================================================
// 哈希
// ===================================================================

uint32 GetTypeHash(const FTDHexCoord& Coord)
{
    // 仅哈希 Q 和 R，因为 S = -Q - R 不含额外信息
    uint32 Hash = GetTypeHash(Coord.Q);
    Hash = HashCombine(Hash, GetTypeHash(Coord.R));
    return Hash;
}
