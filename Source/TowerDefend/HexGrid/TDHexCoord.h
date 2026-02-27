// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDHexCoord.generated.h"

/**
 * FTDHexCoord - 六边形网格的立方坐标 (Cube Coordinate)。
 *
 * 使用立方坐标系 (Q, R, S)，满足不变量 Q + R + S == 0。
 * 参考 Red Blob Games 的六边形网格指南设计。
 * 支持轴向 (Q, R) 和完整立方 (Q, R, S) 两种构造方式。
 *
 * 纯数据结构体，值语义：轻量、可自由拷贝，
 * 可作为 TMap / TSet 的键使用。
 * 采用平顶 (flat-top) 六边形朝向（与文明6一致）。
 *
 * 世界坐标转换需要外部传入 HexSize 参数（六边形外接圆半径），
 * 坐标结构体本身不存储任何视觉尺寸信息。
 */
USTRUCT(BlueprintType)
struct FTDHexCoord
{
    GENERATED_BODY()

    // ---------------------------------------------------------------
    // 成员变量
    // ---------------------------------------------------------------

    /** 立方坐标 Q 轴（列方向），与 R、S 共同满足 Q + R + S == 0。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexCoord")
    int32 Q;

    /** 立方坐标 R 轴（行方向），与 Q、S 共同满足 Q + R + S == 0。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexCoord")
    int32 R;

    /** 立方坐标 S 轴，由 Q 和 R 派生：S = -Q - R。显式存储以便校验数据完整性。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HexCoord")
    int32 S;

    // ---------------------------------------------------------------
    // 构造函数
    // ---------------------------------------------------------------

    /** 默认构造函数，初始化为原点 (0, 0, 0)。 */
    FTDHexCoord();

    /**
     * 从轴向坐标构造，S 自动派生为 -Q - R。
     * 这是游戏逻辑代码中最常用的构造方式。
     *
     * @param InQ  Q 轴坐标（列）。
     * @param InR  R 轴坐标（行）。
     */
    FTDHexCoord(int32 InQ, int32 InR);

    /**
     * 从完整立方坐标构造。
     * Debug 构建中会断言 InQ + InR + InS == 0。
     *
     * @param InQ  Q 轴值。
     * @param InR  R 轴值。
     * @param InS  S 轴值，必须满足 Q + R + S == 0。
     */
    FTDHexCoord(int32 InQ, int32 InR, int32 InS);

    // ---------------------------------------------------------------
    // 工厂方法
    // ---------------------------------------------------------------

    /**
     * 从轴向坐标 (Q, R) 创建，语义上等同于 FTDHexCoord(InQ, InR)，
     * 但在调用处可读性更强。
     */
    static FTDHexCoord FromAxial(int32 InQ, int32 InR);

    /** 返回无效哨兵坐标，其 IsValid() 返回 false。 */
    static const FTDHexCoord& Invalid();

    // ---------------------------------------------------------------
    // 校验
    // ---------------------------------------------------------------

    /**
     * 检查此坐标是否满足立方不变量 Q + R + S == 0。
     * 对于 Invalid() 哨兵和任何损坏数据返回 false。
     */
    bool IsValid() const;

    // ---------------------------------------------------------------
    // 比较运算符
    // ---------------------------------------------------------------

    /** 相等比较。三个轴全部相同时返回 true。 */
    bool operator==(const FTDHexCoord& Other) const;

    /** 不等比较。 */
    bool operator!=(const FTDHexCoord& Other) const;

    // ---------------------------------------------------------------
    // 算术运算符（不可变风格，返回新值）
    // ---------------------------------------------------------------

    /** 逐分量相加。 */
    FTDHexCoord operator+(const FTDHexCoord& Other) const;

    /** 逐分量相减。 */
    FTDHexCoord operator-(const FTDHexCoord& Other) const;

    /** 各分量乘以标量。 */
    FTDHexCoord operator*(int32 Scalar) const;

    // ---------------------------------------------------------------
    // 距离
    // ---------------------------------------------------------------

    /**
     * 计算到另一个坐标的六边形格子距离。
     * 使用立方距离公式：max(|dQ|, |dR|, |dS|)。
     *
     * @param Other  目标坐标。
     * @return       整数步数距离。
     */
    int32 DistanceTo(const FTDHexCoord& Other) const;

    /**
     * 静态版距离计算。
     *
     * @param A  坐标 A。
     * @param B  坐标 B。
     * @return   整数步数距离。
     */
    static int32 Distance(const FTDHexCoord& A, const FTDHexCoord& B);

    // ---------------------------------------------------------------
    // 邻居查询
    // ---------------------------------------------------------------

    /**
     * 获取指定方向的相邻坐标。
     * 方向为 0-5 索引（平顶六边形，从东方顺时针：E, NE, NW, W, SW, SE）。
     *
     * @param Direction  方向索引 [0, 5]。超出范围时钳制。
     * @return           相邻格子坐标。
     */
    FTDHexCoord GetNeighbor(int32 Direction) const;

    /**
     * 获取全部 6 个相邻坐标。
     * 按方向顺序 0-5 返回（E, NE, NW, W, SW, SE）。
     *
     * @return  包含 6 个邻居坐标的数组。
     */
    TArray<FTDHexCoord> GetAllNeighbors() const;

    // ---------------------------------------------------------------
    // 范围查询
    // ---------------------------------------------------------------

    /**
     * 获取距离不超过 Range 的所有坐标（含自身）。
     * 结果数量为 3*Range^2 + 3*Range + 1。
     * Range 为 0 时仅返回自身。
     *
     * @param Range  最大距离，必须 >= 0。
     * @return       范围内所有坐标。
     */
    TArray<FTDHexCoord> GetCoordsInRange(int32 Range) const;

    /**
     * 获取恰好在指定距离上的所有坐标（环形查询）。
     * 结果数量为 6 * RingRadius（RingRadius 为 0 时返回 1 个）。
     *
     * @param RingRadius  精确距离，必须 >= 0。
     * @return            环上所有坐标。
     */
    TArray<FTDHexCoord> GetRing(int32 RingRadius) const;

    // ---------------------------------------------------------------
    // 连线
    // ---------------------------------------------------------------

    /**
     * 计算从此坐标到目标坐标的直线路径（含两端点）。
     * 使用浮点立方插值 + CubeRound 算法。
     * 适用于视线检测。
     *
     * @param Target  目标坐标。
     * @return        从自身到目标的有序坐标数组。
     */
    TArray<FTDHexCoord> LineTo(const FTDHexCoord& Target) const;

    // ---------------------------------------------------------------
    // 世界坐标转换
    // ---------------------------------------------------------------

    /**
     * 将六边形坐标转换为世界空间位置 (FVector)。
     * 采用平顶六边形朝向，Z 设为 0。
     * 调用者负责添加高度偏移。
     *
     * @param HexSize  六边形外接圆半径（中心到顶点距离）。
     * @return         此格子中心的世界坐标。
     */
    FVector ToWorldPosition(float HexSize) const;

    /**
     * 将世界空间位置转换为最近的六边形坐标。
     * 采用平顶六边形朝向，忽略 Z 分量。
     * HexSize 为零时返回 Invalid()。
     *
     * @param WorldPosition  世界空间位置。
     * @param HexSize        六边形外接圆半径。
     * @return               最近的六边形坐标。
     */
    static FTDHexCoord FromWorldPosition(const FVector& WorldPosition, float HexSize);

    // ---------------------------------------------------------------
    // 调试
    // ---------------------------------------------------------------

    /** 返回可读字符串，格式如 "(Q=1, R=-2, S=1)"。 */
    FString ToString() const;

    // ---------------------------------------------------------------
    // 哈希支持（TMap / TSet 键）
    // ---------------------------------------------------------------

    /** 哈希函数，仅哈希 Q 和 R（S 由不变量决定，不含额外信息）。 */
    friend uint32 GetTypeHash(const FTDHexCoord& Coord);
};
