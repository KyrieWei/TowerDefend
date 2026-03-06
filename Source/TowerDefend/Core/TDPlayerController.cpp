// Copyright TowerDefend. All Rights Reserved.

#include "Core/TDPlayerController.h"
#include "Core/TDCameraPawn.h"
#include "Core/TDTerrainEditorComponent.h"
#include "Core/TDMapFileManager.h"
#include "Core/TDServerValidation.h"
#include "Core/TDPlayerState.h"
#include "Core/TDGameState.h"
#include "Building/TDBuildingDataAsset.h"
#include "Unit/TDUnitDataAsset.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexTile.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "GameFramework/SpringArmComponent.h"
#include "Kismet/GameplayStatics.h"

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

    // 创建地形编辑器子组件
    TerrainEditorComponent = CreateDefaultSubobject<UTDTerrainEditorComponent>(
        TEXT("TerrainEditorComponent"));

    CheatClass = UTDCheatManager::StaticClass();
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

    // 将相机初始位置聚焦到地图中心
    if (ATDHexGridManager* GridManager = Cast<ATDHexGridManager>(
            UGameplayStatics::GetActorOfClass(GetWorld(), ATDHexGridManager::StaticClass())))
    {
        const FVector MapCenter = GridManager->GetGridCenterWorld();
        FocusOnPosition(MapCenter);

        UE_LOG(LogTDCamera, Log,
            TEXT("ATDPlayerController::BeginPlay: Camera focused on map center (%s)."),
            *MapCenter.ToString());
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

    // 左键点击 — 地形编辑器地块选中
    if (IA_LeftClick)
    {
        EnhancedInput->BindAction(
            IA_LeftClick, ETriggerEvent::Started,
            this, &ATDPlayerController::HandleLeftClick);
    }

    // 右键点击 — 地形编辑器取消选中
    if (IA_RightClick)
    {
        EnhancedInput->BindAction(
            IA_RightClick, ETriggerEvent::Started,
            this, &ATDPlayerController::HandleRightClick);
    }

    // 升高地形
    if (IA_RaiseTerrain)
    {
        EnhancedInput->BindAction(
            IA_RaiseTerrain, ETriggerEvent::Started,
            this, &ATDPlayerController::HandleRaiseTerrain);
    }

    // 降低地形
    if (IA_LowerTerrain)
    {
        EnhancedInput->BindAction(
            IA_LowerTerrain, ETriggerEvent::Started,
            this, &ATDPlayerController::HandleLowerTerrain);
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

void ATDPlayerController::HandleLeftClick(const FInputActionValue& Value)
{
    if (TerrainEditorComponent && TerrainEditorComponent->IsInEditMode())
    {
        TerrainEditorComponent->SelectTileUnderCursor();
    }
}

void ATDPlayerController::HandleRightClick(const FInputActionValue& Value)
{
    if (TerrainEditorComponent && TerrainEditorComponent->IsInEditMode())
    {
        TerrainEditorComponent->DeselectTile();
    }
}

void ATDPlayerController::HandleRaiseTerrain(const FInputActionValue& Value)
{
    if (!TerrainEditorComponent || !TerrainEditorComponent->IsInEditMode())
    {
        return;
    }

    if (!TerrainEditorComponent->HasSelectedTile())
    {
        return;
    }

    const bool bSuccess = TerrainEditorComponent->RaiseSelectedTile();

    if (GEngine)
    {
        if (bSuccess)
        {
            ATDHexTile* Tile = TerrainEditorComponent->GetSelectedTile();
            const int32 NewHeight = Tile ? Tile->GetHeightLevel() : 0;
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
                FString::Printf(TEXT("Terrain raised to height %d"), NewHeight));
        }
        else
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
                TEXT("Cannot raise terrain further"));
        }
    }
}

void ATDPlayerController::HandleLowerTerrain(const FInputActionValue& Value)
{
    if (!TerrainEditorComponent || !TerrainEditorComponent->IsInEditMode())
    {
        return;
    }

    if (!TerrainEditorComponent->HasSelectedTile())
    {
        return;
    }

    const bool bSuccess = TerrainEditorComponent->LowerSelectedTile();

    if (GEngine)
    {
        if (bSuccess)
        {
            ATDHexTile* Tile = TerrainEditorComponent->GetSelectedTile();
            const int32 NewHeight = Tile ? Tile->GetHeightLevel() : 0;
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
                FString::Printf(TEXT("Terrain lowered to height %d"), NewHeight));
        }
        else
        {
            GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red,
                TEXT("Cannot lower terrain further"));
        }
    }
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

// ===================================================================
// 控制台命令 (Exec)
// ===================================================================

void ATDPlayerController::TerrainEditMode()
{
    if (!TerrainEditorComponent)
    {
        UE_LOG(LogTDCamera, Error,
            TEXT("TerrainEditMode: TerrainEditorComponent is null."));
        return;
    }

    TerrainEditorComponent->ToggleEditMode();

    if (GEngine)
    {
        const FString StateStr = TerrainEditorComponent->IsInEditMode()
            ? TEXT("ON") : TEXT("OFF");
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
            FString::Printf(TEXT("Terrain Edit Mode: %s"), *StateStr));
    }
}

void ATDPlayerController::TerrainBrush(const FString& TypeName)
{
    if (!TerrainEditorComponent)
    {
        UE_LOG(LogTDCamera, Error,
            TEXT("TerrainBrush: TerrainEditorComponent is null."));
        return;
    }

    // 字符串 → 枚举解析
    ETDTerrainType NewType = ETDTerrainType::Plain;

    if (TypeName.Equals(TEXT("Plain"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Plain;
    }
    else if (TypeName.Equals(TEXT("Hill"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Hill;
    }
    else if (TypeName.Equals(TEXT("Mountain"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Mountain;
    }
    else if (TypeName.Equals(TEXT("Forest"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Forest;
    }
    else if (TypeName.Equals(TEXT("River"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::River;
    }
    else if (TypeName.Equals(TEXT("Swamp"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Swamp;
    }
    else if (TypeName.Equals(TEXT("DeepWater"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::DeepWater;
    }
    else
    {
        UE_LOG(LogTDCamera, Warning,
            TEXT("TerrainBrush: Unknown type '%s'. "
                 "Valid: Plain, Hill, Mountain, Forest, River, Swamp, DeepWater"),
            *TypeName);

        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                FString::Printf(TEXT("Unknown terrain type: %s"), *TypeName));
        }
        return;
    }

    TerrainEditorComponent->SetBrushTerrainType(NewType);

    if (GEngine)
    {
        GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
            FString::Printf(TEXT("Terrain Brush: %s"), *TypeName));
    }
}

void ATDPlayerController::TerrainHeight(const FString& Direction)
{
    if (!TerrainEditorComponent)
    {
        UE_LOG(LogTDCamera, Error,
            TEXT("TerrainHeight: TerrainEditorComponent is null."));
        return;
    }

    if (!TerrainEditorComponent->IsInEditMode())
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("TerrainHeight: Not in edit mode. Use TerrainEditMode first."));
        }
        return;
    }

    if (!TerrainEditorComponent->HasSelectedTile())
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("TerrainHeight: No tile selected. Click a tile first."));
        }
        return;
    }

    bool bSuccess = false;

    if (Direction.Equals(TEXT("raise"), ESearchCase::IgnoreCase)
        || Direction.Equals(TEXT("up"), ESearchCase::IgnoreCase))
    {
        bSuccess = TerrainEditorComponent->RaiseSelectedTile();
    }
    else if (Direction.Equals(TEXT("lower"), ESearchCase::IgnoreCase)
        || Direction.Equals(TEXT("down"), ESearchCase::IgnoreCase))
    {
        bSuccess = TerrainEditorComponent->LowerSelectedTile();
    }
    else
    {
        UE_LOG(LogTDCamera, Warning,
            TEXT("TerrainHeight: Unknown direction '%s'. Valid: raise/up, lower/down"),
            *Direction);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                FString::Printf(
                    TEXT("Unknown direction: %s (use raise/up or lower/down)"),
                    *Direction));
        }
        return;
    }

    if (GEngine)
    {
        if (bSuccess)
        {
            ATDHexTile* Tile = TerrainEditorComponent->GetSelectedTile();
            const int32 Height = Tile ? Tile->GetHeightLevel() : 0;
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
                FString::Printf(TEXT("Terrain height: %d"), Height));
        }
        else
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("Cannot modify terrain height (validation failed)."));
        }
    }
}

void ATDPlayerController::TerrainSet(const FString& TypeName)
{
    if (!TerrainEditorComponent)
    {
        UE_LOG(LogTDCamera, Error,
            TEXT("TerrainSet: TerrainEditorComponent is null."));
        return;
    }

    if (!TerrainEditorComponent->IsInEditMode())
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("TerrainSet: Not in edit mode. Use TerrainEditMode first."));
        }
        return;
    }

    if (!TerrainEditorComponent->HasSelectedTile())
    {
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("TerrainSet: No tile selected. Click a tile first."));
        }
        return;
    }

    // 字符串 → 枚举解析
    ETDTerrainType NewType = ETDTerrainType::Plain;

    if (TypeName.Equals(TEXT("Plain"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Plain;
    }
    else if (TypeName.Equals(TEXT("Hill"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Hill;
    }
    else if (TypeName.Equals(TEXT("Mountain"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Mountain;
    }
    else if (TypeName.Equals(TEXT("Forest"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Forest;
    }
    else if (TypeName.Equals(TEXT("River"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::River;
    }
    else if (TypeName.Equals(TEXT("Swamp"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::Swamp;
    }
    else if (TypeName.Equals(TEXT("DeepWater"), ESearchCase::IgnoreCase))
    {
        NewType = ETDTerrainType::DeepWater;
    }
    else
    {
        UE_LOG(LogTDCamera, Warning,
            TEXT("TerrainSet: Unknown type '%s'. "
                 "Valid: Plain, Hill, Mountain, Forest, River, Swamp, DeepWater"),
            *TypeName);
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                FString::Printf(TEXT("Unknown terrain type: %s"), *TypeName));
        }
        return;
    }

    const bool bSuccess =
        TerrainEditorComponent->SetSelectedTileTerrainType(NewType);

    if (GEngine)
    {
        if (bSuccess)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
                FString::Printf(TEXT("Terrain set to: %s"), *TypeName));
        }
        else
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                FString::Printf(TEXT("Failed to set terrain to: %s"), *TypeName));
        }
    }
}

void ATDPlayerController::SaveMap(const FString& MapName)
{
    if (MapName.IsEmpty())
    {
        UE_LOG(LogTDCamera, Warning, TEXT("SaveMap: MapName is empty. Usage: SaveMap <MapName>"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("SaveMap: Please provide a map name."));
        }
        return;
    }

    if (!TerrainEditorComponent || !TerrainEditorComponent->GetGridManager())
    {
        UE_LOG(LogTDCamera, Error,
            TEXT("SaveMap: No GridManager available."));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("SaveMap: No GridManager found."));
        }
        return;
    }

    const bool bSuccess = UTDMapFileManager::SaveMapToFile(
        TerrainEditorComponent->GetGridManager(), MapName);

    if (GEngine)
    {
        if (bSuccess)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
                FString::Printf(TEXT("Map saved: %s"), *MapName));
        }
        else
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                FString::Printf(TEXT("Failed to save map: %s"), *MapName));
        }
    }
}

void ATDPlayerController::LoadMap(const FString& MapName)
{
    if (MapName.IsEmpty())
    {
        UE_LOG(LogTDCamera, Warning, TEXT("LoadMap: MapName is empty. Usage: LoadMap <MapName>"));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("LoadMap: Please provide a map name."));
        }
        return;
    }

    if (!TerrainEditorComponent || !TerrainEditorComponent->GetGridManager())
    {
        UE_LOG(LogTDCamera, Error,
            TEXT("LoadMap: No GridManager available."));
        if (GEngine)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                TEXT("LoadMap: No GridManager found."));
        }
        return;
    }

    const bool bSuccess = UTDMapFileManager::LoadMapFromFile(
        TerrainEditorComponent->GetGridManager(), MapName);

    if (GEngine)
    {
        if (bSuccess)
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Green,
                FString::Printf(TEXT("Map loaded: %s"), *MapName));
        }
        else
        {
            GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red,
                FString::Printf(TEXT("Failed to load map: %s"), *MapName));
        }
    }
}

// ===================================================================
// Server RPCs
// ===================================================================

bool ATDPlayerController::ServerRequestPlaceBuilding_Validate(
    UTDBuildingDataAsset* BuildingData, FTDHexCoord Coord)
{
    return BuildingData != nullptr && Coord.IsValid();
}

void ATDPlayerController::ServerRequestPlaceBuilding_Implementation(
    UTDBuildingDataAsset* BuildingData, FTDHexCoord Coord)
{
    UE_LOG(LogTDCamera, Log,
        TEXT("ServerRequestPlaceBuilding: Player %s requests building at %s"),
        *GetName(), *Coord.ToString());
}

bool ATDPlayerController::ServerRequestTrainUnit_Validate(
    UTDUnitDataAsset* UnitData, int32 Count)
{
    return UnitData != nullptr && Count > 0 && Count <= 100;
}

void ATDPlayerController::ServerRequestTrainUnit_Implementation(
    UTDUnitDataAsset* UnitData, int32 Count)
{
    UE_LOG(LogTDCamera, Log,
        TEXT("ServerRequestTrainUnit: Player %s requests %d units"),
        *GetName(), Count);
}

bool ATDPlayerController::ServerRequestResearchTech_Validate(FName TechID)
{
    return !TechID.IsNone();
}

void ATDPlayerController::ServerRequestResearchTech_Implementation(FName TechID)
{
    UE_LOG(LogTDCamera, Log,
        TEXT("ServerRequestResearchTech: Player %s requests tech %s"),
        *GetName(), *TechID.ToString());
}

bool ATDPlayerController::ServerRequestModifyTerrain_Validate(
    FTDHexCoord Coord, int32 HeightDelta)
{
    return Coord.IsValid() && FMath::Abs(HeightDelta) <= 1;
}

void ATDPlayerController::ServerRequestModifyTerrain_Implementation(
    FTDHexCoord Coord, int32 HeightDelta)
{
    UE_LOG(LogTDCamera, Log,
        TEXT("ServerRequestModifyTerrain: Player %s requests terrain change at %s (delta=%d)"),
        *GetName(), *Coord.ToString(), HeightDelta);
}
