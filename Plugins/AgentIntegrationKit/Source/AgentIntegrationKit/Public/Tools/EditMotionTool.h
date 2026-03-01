// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Unified motion stack editor that routes to IK Rig, IK Retargeter, or Pose Search.
 *
 * motion_type:
 * - "ikrig"
 * - "ikretargeter"
 * - "pose_search"
 *
 * If omitted, routing is inferred from operation fields and asset type.
 */
class AGENTINTEGRATIONKIT_API FEditMotionTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_motion"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit motion systems in one tool: IK Rig, IK Retargeter, and Pose Search. "
			"Use motion_type to select target subsystem, or omit it to infer automatically.");
	}

	virtual TSharedPtr<class FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;
};

