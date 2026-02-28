// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TDPlayerController.generated.h"

class UInputMappingContext;
class UInputAction;
struct FInputActionValue;
struct FTDHexCoord;
class ATDCameraPawn;

/**
 * ATDPlayerController - 策略视角相机控制器。
 *
 * 管理俯视/斜45度策略视角的相机操作，参考文明6风格。
 * 通过 Enhanced Input 系统处理以下输入：
 *   - WASD / 边缘平移
 *   - 滚轮缩放
 *   - 中键旋转
 *   - Shift 加速
 *
 * 所有相机运动最终作用于所 Possess 的 ATDCameraPawn。
 * 与六边形坐标系的交互通过 FTDHexCoord 的静态方法完成，不直接依赖网格管理器。
 */
UCLASS()
class TOWERDEFEND_API ATDPlayerController : public APlayerController
{
    GENERATED_BODY()

public:

    ATDPlayerController();

    // ---------------------------------------------------------------
    // 核心接口
    // ---------------------------------------------------------------

    /**
     * 沿指定方向平移相机。
     * Direction 为归一化的 XY 平面方向，在 Pawn 本地空间中生效。
     *
     * @param Direction  归一化的 2D 方向向量。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void MoveCamera(const FVector2D& Direction);

    /**
     * 绕 Z 轴旋转相机。
     *
     * @param DeltaYaw  旋转增量（度）。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void RotateCamera(float DeltaYaw);

    /**
     * 缩放相机（调整弹簧臂长度）。
     *
     * @param DeltaZoom  缩放增量，正值拉远，负值拉近。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void ZoomCamera(float DeltaZoom);

    /**
     * 设置相机的移动边界范围。
     *
     * @param Min  XY 平面最小边界。
     * @param Max  XY 平面最大边界。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void SetCameraBounds(const FVector2D& Min, const FVector2D& Max);

    /**
     * 将相机聚焦到指定世界坐标位置。
     *
     * @param WorldPosition  目标世界坐标。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void FocusOnPosition(const FVector& WorldPosition);

    /**
     * 将相机聚焦到指定六边形格子。
     * 内部通过 FTDHexCoord::ToWorldPosition() 转换坐标。
     *
     * @param Coord    目标六边形坐标。
     * @param HexSize  六边形外接圆半径。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    void FocusOnHexCoord(const FTDHexCoord& Coord, float HexSize);

    /**
     * 获取当前鼠标光标下方的六边形坐标。
     * 通过射线检测获取地面交点，再转换为六边形坐标。
     * 纯坐标计算，不依赖地图是否已生成。
     *
     * @param HexSize  六边形外接圆半径。
     * @return         光标下方的六边形坐标，射线未命中时返回 Invalid。
     */
    UFUNCTION(BlueprintCallable, Category = "Camera")
    FTDHexCoord GetHexCoordUnderCursor(float HexSize) const;

protected:

    // ---------------------------------------------------------------
    // APlayerController 重写
    // ---------------------------------------------------------------

    virtual void BeginPlay() override;
    virtual void PlayerTick(float DeltaTime) override;
    virtual void SetupInputComponent() override;

    // ---------------------------------------------------------------
    // 配置参数
    // ---------------------------------------------------------------

    /** 相机平移速度（单位/秒）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
    float CameraMoveSpeed;

    /** 相机旋转速度（度/像素）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
    float CameraRotateSpeed;

    /** 相机缩放速度（单位/滚轮刻度）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement")
    float CameraZoomSpeed;

    /** 相机最小高度（最大缩放时的臂长）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (ClampMin = "100.0"))
    float MinCameraHeight;

    /** 相机最大高度（最小缩放时的臂长）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (ClampMin = "100.0"))
    float MaxCameraHeight;

    /** 相机俯仰角度（弹簧臂 Pitch），负值为俯视。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Zoom", meta = (ClampMin = "-89.0", ClampMax = "-10.0"))
    float CameraPitchAngle;

    /** 屏幕边缘触发平移的距离阈值（像素）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|EdgeScroll", meta = (ClampMin = "1.0"))
    float EdgeScrollThreshold;

    /** 是否启用鼠标边缘滚动。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|EdgeScroll")
    bool bEnableEdgeScroll;

    /** 相机移动范围最小值 (XY)。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Bounds")
    FVector2D CameraBoundsMin;

    /** 相机移动范围最大值 (XY)。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Bounds")
    FVector2D CameraBoundsMax;

    /** Shift 加速倍率。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Movement", meta = (ClampMin = "1.0"))
    float FastMoveMultiplier;

    // ---------------------------------------------------------------
    // 输入资产引用
    // ---------------------------------------------------------------

    /** 策略相机专用的输入映射上下文。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Input")
    TObjectPtr<UInputMappingContext> IMC_Strategy;

    /** 相机平移输入动作 (WASD, Vector2D)。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Input")
    TObjectPtr<UInputAction> IA_CameraMove;

    /** 相机旋转输入动作 (中键拖拽 X 轴, Axis1D)。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Input")
    TObjectPtr<UInputAction> IA_CameraRotate;

    /** 相机缩放输入动作 (滚轮, Axis1D)。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Input")
    TObjectPtr<UInputAction> IA_CameraZoom;

    /** 快速移动输入动作 (Shift, Bool)。 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Camera|Input")
    TObjectPtr<UInputAction> IA_CameraFastMove;

private:

    // ---------------------------------------------------------------
    // 输入回调
    // ---------------------------------------------------------------

    /** WASD 平移输入回调。 */
    void HandleCameraMove(const FInputActionValue& Value);

    /** 中键旋转输入回调。 */
    void HandleCameraRotate(const FInputActionValue& Value);

    /** 滚轮缩放输入回调。 */
    void HandleCameraZoom(const FInputActionValue& Value);

    /** Shift 加速启动回调。 */
    void HandleFastMoveStarted(const FInputActionValue& Value);

    /** Shift 加速结束回调。 */
    void HandleFastMoveCompleted(const FInputActionValue& Value);

    // ---------------------------------------------------------------
    // 内部方法
    // ---------------------------------------------------------------

    /** 检测鼠标是否在屏幕边缘并返回平移方向。 */
    FVector2D CalculateEdgeScrollDirection() const;

    /** 将 Pawn 位置钳制在相机边界内。 */
    void ClampCameraPosition();

    /** 获取当前 Possess 的 TDCameraPawn（带有效性检查）。 */
    ATDCameraPawn* GetCameraPawn() const;

    // ---------------------------------------------------------------
    // 运行时状态
    // ---------------------------------------------------------------

    /** 当前是否按住快速移动键。 */
    bool bIsFastMoving;
};
