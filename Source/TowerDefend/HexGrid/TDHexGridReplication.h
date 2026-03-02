// Copyright TowerDefend. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "Core/TDNetworkTypes.h"
#include "TDHexGridReplication.generated.h"

class ATDHexGridManager;

/** 网格增量变化委托。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FTDOnGridDeltaApplied, const FTDGridDelta&, Delta);

/**
 * UTDHexGridReplication - 网格状态增量复制管理器。
 *
 * 追踪网格状态的变化，生成增量数据包，
 * 并支持在客户端应用增量更新。
 *
 * 设计：
 * - 服务端：修改网格后调用 RecordDelta()，然后通过 RPC 发送增量
 * - 客户端：接收增量后调用 ApplyDelta() 更新本地网格
 */
UCLASS(BlueprintType)
class TOWERDEFEND_API UTDHexGridReplication : public UObject
{
    GENERATED_BODY()

public:
    /**
     * 设置关联的网格管理器。
     *
     * @param InGrid 六边形网格管理器。
     */
    void SetGridManager(ATDHexGridManager* InGrid);

    /**
     * 记录一个网格变化增量。
     * 在服务端修改网格后调用。
     *
     * @param Delta 增量数据。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Network|Grid")
    void RecordDelta(const FTDGridDelta& Delta);

    /**
     * 将增量应用到本地网格。
     * 在客户端接收到增量后调用。
     *
     * @param Delta 增量数据。
     * @return      是否成功应用。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Network|Grid")
    bool ApplyDelta(const FTDGridDelta& Delta);

    /** 获取待发送的增量队列。 */
    UFUNCTION(BlueprintPure, Category = "TD|Network|Grid")
    const TArray<FTDGridDelta>& GetPendingDeltas() const { return PendingDeltas; }

    /** 清空待发送队列（发送后调用）。 */
    UFUNCTION(BlueprintCallable, Category = "TD|Network|Grid")
    void ClearPendingDeltas();

    /**
     * 生成完整网格快照。
     * 用于新客户端加入时的全量同步。
     *
     * @return 所有格子的增量数组。
     */
    UFUNCTION(BlueprintCallable, Category = "TD|Network|Grid")
    TArray<FTDGridDelta> GenerateFullSnapshot() const;

    /** 网格增量被应用时广播。 */
    UPROPERTY(BlueprintAssignable, Category = "TD|Network|Grid|Events")
    FTDOnGridDeltaApplied OnGridDeltaApplied;

private:
    /** 关联的网格管理器（弱引用）。 */
    TWeakObjectPtr<ATDHexGridManager> GridManager;

    /** 待发送的增量队列。 */
    TArray<FTDGridDelta> PendingDeltas;
};
