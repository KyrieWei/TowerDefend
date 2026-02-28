// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "TDSharedTypes.generated.h"

/**
 * 科技时代枚举
 *
 * 定义游戏中的六个科技时代，从远古到现代依次递进。
 * 各模块（科技树、单位、建筑等）共享此枚举来标识时代阶段。
 */
UENUM(BlueprintType)
enum class ETDTechEra : uint8
{
    /** 远古时代 */
    Ancient = 0     UMETA(DisplayName = "Ancient"),

    /** 古典时代 */
    Classical = 1   UMETA(DisplayName = "Classical"),

    /** 中世纪 */
    Medieval = 2    UMETA(DisplayName = "Medieval"),

    /** 文艺复兴 */
    Renaissance = 3 UMETA(DisplayName = "Renaissance"),

    /** 工业时代 */
    Industrial = 4  UMETA(DisplayName = "Industrial"),

    /** 现代 */
    Modern = 5      UMETA(DisplayName = "Modern"),
};
