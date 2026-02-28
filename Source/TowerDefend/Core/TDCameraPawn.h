// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TDCameraPawn.generated.h"

class USpringArmComponent;
class UCameraComponent;

/**
 * ATDCameraPawn - 策略视角相机 Pawn。
 *
 * 承载俯视策略相机的组件层级：
 *   RootSceneComponent（控制 XY 平移和 Yaw 旋转）
 *     -> CameraArm（SpringArm，控制 Pitch 角度和臂长/缩放距离）
 *       -> CameraComponent（挂在弹簧臂末端的实际相机）
 *
 * 本类不包含碰撞、物理或移动组件，所有运动由 TDPlayerController 驱动。
 */
UCLASS()
class TOWERDEFEND_API ATDCameraPawn : public APawn
{
    GENERATED_BODY()

public:

    ATDCameraPawn();

    // ---------------------------------------------------------------
    // 组件访问
    // ---------------------------------------------------------------

    /** 获取弹簧臂组件（用于控制臂长和 Pitch）。 */
    FORCEINLINE USpringArmComponent* GetCameraArm() const { return CameraArm; }

    /** 获取相机组件。 */
    FORCEINLINE UCameraComponent* GetCameraComponent() const { return CameraComponent; }

protected:

    // ---------------------------------------------------------------
    // 组件
    // ---------------------------------------------------------------

    /** 根场景组件，控制 Pawn 的 XY 位置和 Z 轴旋转 (Yaw)。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USceneComponent> RootSceneComponent;

    /** 弹簧臂组件，控制相机距离 (ArmLength) 和俯仰角 (Pitch)。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<USpringArmComponent> CameraArm;

    /** 相机组件，挂在弹簧臂末端，提供实际的渲染视角。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
    TObjectPtr<UCameraComponent> CameraComponent;
};
