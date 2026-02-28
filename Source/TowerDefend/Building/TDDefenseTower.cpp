// Copyright TowerDefend. All Rights Reserved.

#include "Building/TDDefenseTower.h"
#include "Building/TDBuildingDataAsset.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogTDDefenseTower, Log, All);

// ===================================================================
// 构造函数
// ===================================================================

ATDDefenseTower::ATDDefenseTower()
{
    PrimaryActorTick.bCanEverTick = false;
}

// ===================================================================
// Override
// ===================================================================

bool ATDDefenseTower::CanAttack() const
{
    // 防御塔始终具备攻击能力（前提是数据有效）
    if (!BuildingData)
    {
        return false;
    }
    return BuildingData->AttackDamage > 0.0f;
}

float ATDDefenseTower::GetAttackRange() const
{
    if (!BuildingData)
    {
        return 0.0f;
    }
    return BuildingData->GetEffectiveAttackRange(CachedHeightLevel);
}

// ===================================================================
// 自动攻击控制
// ===================================================================

void ATDDefenseTower::StartAutoAttack()
{
    if (bIsAutoAttacking)
    {
        return;
    }

    if (!CanAttack())
    {
        UE_LOG(LogTDDefenseTower, Warning,
            TEXT("ATDDefenseTower::StartAutoAttack: Cannot attack "
                 "(no attack data)."));
        return;
    }

    if (IsDestroyed())
    {
        UE_LOG(LogTDDefenseTower, Warning,
            TEXT("ATDDefenseTower::StartAutoAttack: Building is destroyed."));
        return;
    }

    bIsAutoAttacking = true;

    const float Interval = GetAttackInterval();

    GetWorldTimerManager().SetTimer(
        AttackTimerHandle,
        this,
        &ATDDefenseTower::OnAttackTimerFired,
        Interval,
        true,      // 循环
        Interval   // 首次延迟等于攻击间隔，避免放置后立即开火
    );

    UE_LOG(LogTDDefenseTower, Log,
        TEXT("Defense tower at %s started auto-attack "
             "(interval=%.2fs, range=%.1f)."),
        *GetCoord().ToString(), Interval, GetAttackRange());
}

void ATDDefenseTower::StopAutoAttack()
{
    if (!bIsAutoAttacking)
    {
        return;
    }

    bIsAutoAttacking = false;

    GetWorldTimerManager().ClearTimer(AttackTimerHandle);

    UE_LOG(LogTDDefenseTower, Log,
        TEXT("Defense tower at %s stopped auto-attack."),
        *GetCoord().ToString());
}

void ATDDefenseTower::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    StopAutoAttack();
    Super::EndPlay(EndPlayReason);
}

// ===================================================================
// 攻击逻辑
// ===================================================================

void ATDDefenseTower::OnAttackTimerFired()
{
    // 已被摧毁则停止攻击
    if (IsDestroyed())
    {
        StopAutoAttack();
        return;
    }

    AActor* Target = FindNearestTarget();
    if (Target)
    {
        AttackTarget(Target);
    }
}

AActor* ATDDefenseTower::FindNearestTarget() const
{
    // TODO: Unit 系统完成后接入实际目标搜索逻辑
    // 搜索步骤：
    //   1. 获取 Coord 为中心、GetAttackRange() 为半径的格子范围
    //   2. 遍历范围内格子上的敌方单位
    //   3. 按距离排序，返回最近的
    return nullptr;
}

void ATDDefenseTower::AttackTarget(AActor* Target)
{
    if (!Target)
    {
        return;
    }

    // TODO: Combat 系统完成后执行实际伤害
    // 当前仅输出日志
    UE_LOG(LogTDDefenseTower, Log,
        TEXT("Defense tower at %s attacking target '%s' "
             "for %.1f damage."),
        *GetCoord().ToString(),
        *Target->GetName(),
        GetAttackDamage());
}

// ===================================================================
// 高度缓存
// ===================================================================

void ATDDefenseTower::SetCachedHeightLevel(int32 InHeightLevel)
{
    CachedHeightLevel = InHeightLevel;
}
