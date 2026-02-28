// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDCameraPawn.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

ATDCameraPawn::ATDCameraPawn()
{
    PrimaryActorTick.bCanEverTick = false;

    // 禁用默认的 Pawn 输入绑定（由 PlayerController 处理）
    AutoPossessPlayer = EAutoReceiveInput::Player0;

    // 根场景组件：控制 XY 平移和 Yaw 旋转
    RootSceneComponent = CreateDefaultSubobject<USceneComponent>(TEXT("RootSceneComponent"));
    SetRootComponent(RootSceneComponent);

    // 弹簧臂：控制 Pitch 角度和臂长（映射为缩放距离）
    CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
    CameraArm->SetupAttachment(RootSceneComponent);
    CameraArm->TargetArmLength = 2000.0f;
    CameraArm->SetRelativeRotation(FRotator(-50.0f, 0.0f, 0.0f));
    CameraArm->bDoCollisionTest = false;
    CameraArm->bEnableCameraLag = true;
    CameraArm->CameraLagSpeed = 10.0f;
    CameraArm->bEnableCameraRotationLag = true;
    CameraArm->CameraRotationLagSpeed = 10.0f;

    // 相机组件：挂在弹簧臂末端
    CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComponent"));
    CameraComponent->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
}
