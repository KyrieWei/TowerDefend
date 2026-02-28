// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TDProjectileBase.generated.h"

class UProjectileMovementComponent;
class UStaticMeshComponent;
class ATDHexGridManager;
class UTDTerrainModifier;

/**
 * ATDProjectileBase - 投射物基类。
 *
 * 代表从防御塔或远程单位发出的投射物 Actor。
 * 投射物沿直线从发射点飞向目标位置，到达后施加伤害。
 * 使用 UE5 的 UProjectileMovementComponent 驱动飞行。
 *
 * 重型投射物（攻城器械）命中后有概率降低目标格子的地形高度，
 * 通过 UTDTerrainModifier::LowerTerrain() 实现地形破坏。
 */
UCLASS(Blueprintable, BlueprintType)
class ATDProjectileBase : public AActor
{
    GENERATED_BODY()

public:
    ATDProjectileBase();

    // ---------------------------------------------------------------
    // 初始化
    // ---------------------------------------------------------------

    /**
     * 初始化投射物的飞行参数和伤害数据。
     * 应在 Spawn 后立即调用。
     *
     * @param InTargetLocation      目标世界坐标。
     * @param InTargetActor         目标 Actor（可为 null，此时飞向坐标位置）。
     * @param InDamage              携带的伤害值。
     * @param InOwnerPlayerIndex    发射者所属玩家索引。
     * @param bInCanDestroyTerrain  是否可以破坏地形。
     */
    UFUNCTION(BlueprintCallable, Category = "Projectile")
    void InitializeProjectile(
        const FVector& InTargetLocation,
        AActor* InTargetActor,
        int32 InDamage,
        int32 InOwnerPlayerIndex,
        bool bInCanDestroyTerrain = false
    );

    // ---------------------------------------------------------------
    // 属性查询
    // ---------------------------------------------------------------

    /** 获取投射物携带的伤害值。 */
    UFUNCTION(BlueprintPure, Category = "Projectile")
    int32 GetDamage() const { return Damage; }

    /** 获取发射者所属玩家索引。 */
    UFUNCTION(BlueprintPure, Category = "Projectile")
    int32 GetOwnerPlayerIndex() const { return OwnerPlayerIndex; }

protected:
    // ---------------------------------------------------------------
    // AActor 重写
    // ---------------------------------------------------------------

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;

    // ---------------------------------------------------------------
    // 碰撞回调
    // ---------------------------------------------------------------

    /**
     * 投射物命中其他 Actor 时的回调。
     * 由 ProjectileMovement 组件触发。
     */
    UFUNCTION()
    void OnProjectileHit(
        UPrimitiveComponent* HitComponent,
        AActor* OtherActor,
        UPrimitiveComponent* OtherComp,
        FVector NormalImpulse,
        const FHitResult& Hit
    );

    /**
     * 到达目标位置时调用。
     * 执行伤害施加和可能的地形破坏。
     */
    void OnReachTarget();

    /**
     * 对目标施加伤害。
     * 分别处理 ATDUnitBase 和 ATDBuildingBase 类型。
     */
    void ApplyDamageToTarget();

    /**
     * 尝试破坏目标所在格子的地形。
     * 仅当 bCanDestroyTerrain 为 true 时生效，
     * 以 TerrainDestroyChance 的概率降低一级高度。
     */
    void TryDestroyTerrain();

    // ---------------------------------------------------------------
    // 投射物参数
    // ---------------------------------------------------------------

    /** 飞行速度（cm/s）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile",
        meta = (ClampMin = "100.0"))
    float Speed = 2000.0f;

    /** 携带的伤害值。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    int32 Damage = 0;

    /** 目标世界坐标。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    FVector TargetLocation = FVector::ZeroVector;

    /** 目标 Actor 弱引用（飞行途中目标可能被销毁，不可作为 UPROPERTY 反射）。 */
    TWeakObjectPtr<AActor> TargetActor;

    /** 发射者所属玩家索引。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile")
    int32 OwnerPlayerIndex = -1;

    /** 是否可以破坏地形（重型投射物特性）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Terrain")
    bool bCanDestroyTerrain = false;

    /** 破坏地形概率（0.0 ~ 1.0）。 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Projectile|Terrain",
        meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "bCanDestroyTerrain"))
    float TerrainDestroyChance = 0.1f;

    // ---------------------------------------------------------------
    // 组件
    // ---------------------------------------------------------------

    /** 投射物 Mesh 组件。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Visual")
    UStaticMeshComponent* ProjectileMesh = nullptr;

    /** UE5 投射物移动组件，负责驱动飞行。 */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Projectile|Movement")
    UProjectileMovementComponent* ProjectileMovement = nullptr;

private:
    /** 投射物是否已命中目标（防止重复触发）。 */
    bool bHasReachedTarget = false;

    /** 判定到达目标的距离阈值（cm）。 */
    static constexpr float ArrivalDistanceThreshold = 50.0f;
};
