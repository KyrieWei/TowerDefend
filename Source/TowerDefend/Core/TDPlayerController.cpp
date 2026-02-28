// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDPlayerController.h"
#include "Core/TDCameraPawn.h"
#include "HexGrid/TDHexCoord.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/SpringArmComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDCamera, Log, All);

// ===================================================================
// 构造函数
// ===================================================================

ATDPlayerController::ATDPlayerController()
    : CameraMoveSpeed(1000.0f)
    , CameraRotateSpeed(2.0f)
    , CameraZoomSpeed(500.0f)
    , MinCameraHeight(500.0f)
    , MaxCameraHeight(5000.0f)
    , CameraPitchAngle(-50.0f)
    , EdgeScrollThreshold(20.0f)
    , bEnableEdgeScroll(true)
    , CameraBoundsMin(FVector2D(-10000.0f, -10000.0f))
    , CameraBoundsMax(FVector2D(10000.0f, 10000.0f))
    , FastMoveMultiplier(2.0f)
    , bIsFastMoving(false)
{
    bShowMouseCursor = true;
    bEnableClickEvents = true;
    bEnableMouseOverEvents = true;
}

// ===================================================================
// APlayerController 重写
// ===================================================================

void ATDPlayerController::BeginPlay()
{
    Super::BeginPlay();

    // 注册输入映射上下文
    if (UEnhancedInputLocalPlayerSubsystem* Subsystem =
            ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
    {
        if (IMC_Strategy)
        {
            Subsystem->AddMappingContext(IMC_Strategy, 0);
        }
        else
        {
            UE_LOG(LogTDCamera, Warning,
                TEXT("ATDPlayerController::BeginPlay: IMC_Strategy is not set. "
                     "Camera input will not work."));
        }
    }

    // 初始化弹簧臂的 Pitch 角度
    if (ATDCameraPawn* CameraPawn = GetCameraPawn())
    {
        if (USpringArmComponent* Arm = CameraPawn->GetCameraArm())
        {
            FRotator ArmRotation = Arm->GetRelativeRotation();
            ArmRotation.Pitch = CameraPitchAngle;
            Arm->SetRelativeRotation(ArmRotation);
        }
    }
}

void ATDPlayerController::PlayerTick(float DeltaTime)
{
    Super::PlayerTick(DeltaTime);

    // 边缘滚动
    if (bEnableEdgeScroll)
    {
        const FVector2D EdgeDirection = CalculateEdgeScrollDirection();
        if (!EdgeDirection.IsNearlyZero())
        {
            MoveCamera(EdgeDirection);
        }
    }
}

void ATDPlayerController::SetupInputComponent()
{
    Super::SetupInputComponent();

    UEnhancedInputComponent* EnhancedInput =
        Cast<UEnhancedInputComponent>(InputComponent);
    if (!ensureMsgf(EnhancedInput,
            TEXT("ATDPlayerController::SetupInputComponent: "
                 "InputComponent is not UEnhancedInputComponent.")))
    {
        return;
    }

    // WASD 平移 — 持续触发
    if (IA_CameraMove)
    {
        EnhancedInput->BindAction(
            IA_CameraMove, ETriggerEvent::Triggered,
            this, &ATDPlayerController::HandleCameraMove);
    }

    // 中键旋转 — 持续触发
    if (IA_CameraRotate)
    {
        EnhancedInput->BindAction(
            IA_CameraRotate, ETriggerEvent::Triggered,
            this, &ATDPlayerController::HandleCameraRotate);
    }

    // 滚轮缩放 — 触发一次
    if (IA_CameraZoom)
    {
        EnhancedInput->BindAction(
            IA_CameraZoom, ETriggerEvent::Triggered,
            this, &ATDPlayerController::HandleCameraZoom);
    }

    // Shift 加速
    if (IA_CameraFastMove)
    {
        EnhancedInput->BindAction(
            IA_CameraFastMove, ETriggerEvent::Started,
            this, &ATDPlayerController::HandleFastMoveStarted);

        EnhancedInput->BindAction(
            IA_CameraFastMove, ETriggerEvent::Completed,
            this, &ATDPlayerController::HandleFastMoveCompleted);
    }
}

// ===================================================================
// 核心接口
// ===================================================================

void ATDPlayerController::MoveCamera(const FVector2D& Direction)
{
    ATDCameraPawn* CameraPawn = GetCameraPawn();
    if (!CameraPawn)
    {
        return;
    }

    if (Direction.IsNearlyZero())
    {
        return;
    }

    const float DeltaTime = GetWorld()->GetDeltaSeconds();
    const float SpeedMultiplier = bIsFastMoving ? FastMoveMultiplier : 1.0f;
    const float EffectiveSpeed = CameraMoveSpeed * SpeedMultiplier * DeltaTime;

    // 在 Pawn 本地空间中计算移动方向（考虑当前 Yaw 旋转）
    const FRotator PawnRotation(0.0f, CameraPawn->GetActorRotation().Yaw, 0.0f);
    const FVector ForwardDir = FRotationMatrix(PawnRotation).GetUnitAxis(EAxis::X);
    const FVector RightDir = FRotationMatrix(PawnRotation).GetUnitAxis(EAxis::Y);

    const FVector MoveDelta =
        (ForwardDir * Direction.Y + RightDir * Direction.X) * EffectiveSpeed;

    CameraPawn->AddActorWorldOffset(MoveDelta, false);

    ClampCameraPosition();
}

void ATDPlayerController::RotateCamera(float DeltaYaw)
{
    ATDCameraPawn* CameraPawn = GetCameraPawn();
    if (!CameraPawn)
    {
        return;
    }

    if (FMath::IsNearlyZero(DeltaYaw))
    {
        return;
    }

    const FRotator RotationDelta(0.0f, DeltaYaw, 0.0f);
    CameraPawn->AddActorWorldRotation(RotationDelta);
}

void ATDPlayerController::ZoomCamera(float DeltaZoom)
{
    ATDCameraPawn* CameraPawn = GetCameraPawn();
    if (!CameraPawn)
    {
        return;
    }

    USpringArmComponent* Arm = CameraPawn->GetCameraArm();
    if (!Arm)
    {
        return;
    }

    const float NewArmLength = FMath::Clamp(
        Arm->TargetArmLength + DeltaZoom,
        MinCameraHeight,
        MaxCameraHeight
    );

    Arm->TargetArmLength = NewArmLength;
}

void ATDPlayerController::SetCameraBounds(
    const FVector2D& Min, const FVector2D& Max)
{
    CameraBoundsMin = Min;
    CameraBoundsMax = Max;

    // 立即钳制当前位置
    ClampCameraPosition();
}

void ATDPlayerController::FocusOnPosition(const FVector& WorldPosition)
{
    ATDCameraPawn* CameraPawn = GetCameraPawn();
    if (!CameraPawn)
    {
        return;
    }

    // 仅移动 XY，保持当前 Z
    const FVector CurrentLocation = CameraPawn->GetActorLocation();
    CameraPawn->SetActorLocation(
        FVector(WorldPosition.X, WorldPosition.Y, CurrentLocation.Z));

    ClampCameraPosition();
}

void ATDPlayerController::FocusOnHexCoord(
    const FTDHexCoord& Coord, float HexSize)
{
    if (!Coord.IsValid())
    {
        UE_LOG(LogTDCamera, Warning,
            TEXT("ATDPlayerController::FocusOnHexCoord: Invalid coordinate."));
        return;
    }

    const FVector WorldPos = Coord.ToWorldPosition(HexSize);
    FocusOnPosition(WorldPos);
}

FTDHexCoord ATDPlayerController::GetHexCoordUnderCursor(float HexSize) const
{
    if (FMath::IsNearlyZero(HexSize))
    {
        UE_LOG(LogTDCamera, Warning,
            TEXT("ATDPlayerController::GetHexCoordUnderCursor: HexSize is zero."));
        return FTDHexCoord::Invalid();
    }

    // 从鼠标位置发射射线，与 Z=0 平面求交
    FVector WorldLocation;
    FVector WorldDirection;
    if (!DeprojectMousePositionToWorld(WorldLocation, WorldDirection))
    {
        return FTDHexCoord::Invalid();
    }

    // 射线方向的 Z 分量为零时无法与水平面相交
    if (FMath::IsNearlyZero(WorldDirection.Z))
    {
        return FTDHexCoord::Invalid();
    }

    // 计算射线与 Z=0 平面的交点
    const float T = -WorldLocation.Z / WorldDirection.Z;
    if (T < 0.0f)
    {
        // 交点在相机背后
        return FTDHexCoord::Invalid();
    }

    const FVector HitPoint = WorldLocation + WorldDirection * T;
    return FTDHexCoord::FromWorldPosition(HitPoint, HexSize);
}

// ===================================================================
// 输入回调
// ===================================================================

void ATDPlayerController::HandleCameraMove(const FInputActionValue& Value)
{
    const FVector2D MoveInput = Value.Get<FVector2D>();
    MoveCamera(MoveInput);
}

void ATDPlayerController::HandleCameraRotate(const FInputActionValue& Value)
{
    const float RotateInput = Value.Get<float>();
    RotateCamera(RotateInput * CameraRotateSpeed);
}

void ATDPlayerController::HandleCameraZoom(const FInputActionValue& Value)
{
    const float ZoomInput = Value.Get<float>();
    // 滚轮向上（正值）应拉近相机（减少臂长），故取反
    ZoomCamera(-ZoomInput * CameraZoomSpeed);
}

void ATDPlayerController::HandleFastMoveStarted(const FInputActionValue& Value)
{
    bIsFastMoving = true;
}

void ATDPlayerController::HandleFastMoveCompleted(const FInputActionValue& Value)
{
    bIsFastMoving = false;
}

// ===================================================================
// 内部方法
// ===================================================================

FVector2D ATDPlayerController::CalculateEdgeScrollDirection() const
{
    float MouseX = 0.0f;
    float MouseY = 0.0f;
    if (!GetMousePosition(MouseX, MouseY))
    {
        return FVector2D::ZeroVector;
    }

    int32 ViewportSizeX = 0;
    int32 ViewportSizeY = 0;
    GetViewportSize(ViewportSizeX, ViewportSizeY);

    if (ViewportSizeX <= 0 || ViewportSizeY <= 0)
    {
        return FVector2D::ZeroVector;
    }

    FVector2D ScrollDirection = FVector2D::ZeroVector;

    // 左边缘
    if (MouseX < EdgeScrollThreshold)
    {
        ScrollDirection.X = -1.0f;
    }
    // 右边缘
    else if (MouseX > static_cast<float>(ViewportSizeX) - EdgeScrollThreshold)
    {
        ScrollDirection.X = 1.0f;
    }

    // 上边缘（屏幕 Y=0 在顶部，对应世界 "前方"）
    if (MouseY < EdgeScrollThreshold)
    {
        ScrollDirection.Y = 1.0f;
    }
    // 下边缘
    else if (MouseY > static_cast<float>(ViewportSizeY) - EdgeScrollThreshold)
    {
        ScrollDirection.Y = -1.0f;
    }

    // 对角线方向归一化
    if (!ScrollDirection.IsNearlyZero())
    {
        ScrollDirection.Normalize();
    }

    return ScrollDirection;
}

void ATDPlayerController::ClampCameraPosition()
{
    ATDCameraPawn* CameraPawn = GetCameraPawn();
    if (!CameraPawn)
    {
        return;
    }

    FVector Location = CameraPawn->GetActorLocation();

    Location.X = FMath::Clamp(
        Location.X,
        static_cast<double>(CameraBoundsMin.X),
        static_cast<double>(CameraBoundsMax.X));
    Location.Y = FMath::Clamp(
        Location.Y,
        static_cast<double>(CameraBoundsMin.Y),
        static_cast<double>(CameraBoundsMax.Y));

    CameraPawn->SetActorLocation(Location);
}

ATDCameraPawn* ATDPlayerController::GetCameraPawn() const
{
    ATDCameraPawn* CameraPawn = Cast<ATDCameraPawn>(GetPawn());
    if (!CameraPawn)
    {
        UE_LOG(LogTDCamera, Verbose,
            TEXT("ATDPlayerController::GetCameraPawn: "
                 "Possessed Pawn is not ATDCameraPawn."));
    }
    return CameraPawn;
}
