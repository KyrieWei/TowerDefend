// Copyright TowerDefend. All Rights Reserved.

#include "Combat/TDProjectileBase.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "HexGrid/TDHexGridManager.h"
#include "HexGrid/TDHexCoord.h"
#include "HexGrid/TDTerrainModifier.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDProjectile, Log, All);

// ===================================================================
// 构造函数
// ===================================================================

ATDProjectileBase::ATDProjectileBase()
{
    PrimaryActorTick.bCanEverTick = true;

    // Mesh 组件作为根
    ProjectileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ProjectileMesh"));
    SetRootComponent(ProjectileMesh);
    ProjectileMesh->SetCollisionProfileName(TEXT("Projectile"));
    ProjectileMesh->SetGenerateOverlapEvents(false);

    // 投射物移动组件
    ProjectileMovement = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("ProjectileMovement"));
    ProjectileMovement->UpdatedComponent = ProjectileMesh;
    ProjectileMovement->bRotationFollowsVelocity = true;
    ProjectileMovement->bShouldBounce = false;
    ProjectileMovement->ProjectileGravityScale = 0.0f;
    ProjectileMovement->InitialSpeed = Speed;
    ProjectileMovement->MaxSpeed = Speed;
}

// ===================================================================
// 初始化
// ===================================================================

void ATDProjectileBase::InitializeProjectile(
    const FVector& InTargetLocation,
    AActor* InTargetActor,
    int32 InDamage,
    int32 InOwnerPlayerIndex,
    bool bInCanDestroyTerrain)
{
    TargetLocation = InTargetLocation;
    TargetActor = InTargetActor;
    Damage = FMath::Max(0, InDamage);
    OwnerPlayerIndex = InOwnerPlayerIndex;
    bCanDestroyTerrain = bInCanDestroyTerrain;

    // 计算飞行方向并设置移动组件速度
    const FVector Direction = (TargetLocation - GetActorLocation()).GetSafeNormal();

    if (ProjectileMovement)
    {
        ProjectileMovement->InitialSpeed = Speed;
        ProjectileMovement->MaxSpeed = Speed;
        ProjectileMovement->Velocity = Direction * Speed;
    }
}

// ===================================================================
// AActor 重写
// ===================================================================

void ATDProjectileBase::BeginPlay()
{
    Super::BeginPlay();

    // 绑定碰撞回调
    if (ProjectileMesh)
    {
        ProjectileMesh->OnComponentHit.AddDynamic(this, &ATDProjectileBase::OnProjectileHit);
    }
}

void ATDProjectileBase::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (bHasReachedTarget)
    {
        return;
    }

    // 如果目标 Actor 仍然有效，更新目标位置（追踪目标）
    if (TargetActor.IsValid())
    {
        TargetLocation = TargetActor->GetActorLocation();
    }

    // 检查是否到达目标位置
    const float DistanceToTarget = FVector::Dist(GetActorLocation(), TargetLocation);

    if (DistanceToTarget <= ArrivalDistanceThreshold)
    {
        OnReachTarget();
    }
}

// ===================================================================
// 碰撞回调
// ===================================================================

void ATDProjectileBase::OnProjectileHit(
    UPrimitiveComponent* HitComponent,
    AActor* OtherActor,
    UPrimitiveComponent* OtherComp,
    FVector NormalImpulse,
    const FHitResult& Hit)
{
    if (bHasReachedTarget)
    {
        return;
    }

    // 忽略碰到自己或所有者
    if (!OtherActor || OtherActor == this || OtherActor == GetOwner())
    {
        return;
    }

    OnReachTarget();
}

// ===================================================================
// 命中处理
// ===================================================================

void ATDProjectileBase::OnReachTarget()
{
    if (bHasReachedTarget)
    {
        return;
    }

    bHasReachedTarget = true;

    // 停止移动
    if (ProjectileMovement)
    {
        ProjectileMovement->StopMovementImmediately();
    }

    ApplyDamageToTarget();
    TryDestroyTerrain();

    // 销毁投射物
    Destroy();
}

void ATDProjectileBase::ApplyDamageToTarget()
{
    if (Damage <= 0)
    {
        return;
    }

    AActor* Target = TargetActor.Get();

    if (!Target)
    {
        UE_LOG(LogTDProjectile, Verbose,
            TEXT("ATDProjectileBase::ApplyDamageToTarget: "
                 "Target actor no longer valid, damage=%d wasted."),
            Damage);
        return;
    }

    // 通过 UE 通用伤害系统施加伤害
    UGameplayStatics::ApplyDamage(
        Target,
        static_cast<float>(Damage),
        nullptr,
        this,
        nullptr
    );

    UE_LOG(LogTDProjectile, Log,
        TEXT("ATDProjectileBase: Applied %d damage to %s."),
        Damage, *Target->GetName());
}

void ATDProjectileBase::TryDestroyTerrain()
{
    if (!bCanDestroyTerrain)
    {
        return;
    }

    // 根据概率判定是否破坏地形
    if (FMath::FRand() > TerrainDestroyChance)
    {
        return;
    }

    // 查找场景中的 HexGridManager
    ATDHexGridManager* GridManager = nullptr;
    TArray<AActor*> FoundActors;
    UGameplayStatics::GetAllActorsOfClass(
        GetWorld(), ATDHexGridManager::StaticClass(), FoundActors);

    if (FoundActors.Num() > 0)
    {
        GridManager = Cast<ATDHexGridManager>(FoundActors[0]);
    }

    if (!GridManager)
    {
        UE_LOG(LogTDProjectile, Warning,
            TEXT("ATDProjectileBase::TryDestroyTerrain: "
                 "No ATDHexGridManager found in world."));
        return;
    }

    // 将命中位置转换为 hex 坐标
    const FTDHexCoord HitCoord = FTDHexCoord::FromWorldPosition(
        TargetLocation, GridManager->GetHexSize());

    // 使用 TerrainModifier 降低地形（Outer 使用 GridManager 而非即将销毁的 this）
    UTDTerrainModifier* TerrainModifier = NewObject<UTDTerrainModifier>(GridManager);

    if (TerrainModifier && TerrainModifier->LowerTerrain(GridManager, HitCoord))
    {
        UE_LOG(LogTDProjectile, Log,
            TEXT("ATDProjectileBase: Terrain destroyed at %s."),
            *HitCoord.ToString());
    }
}
