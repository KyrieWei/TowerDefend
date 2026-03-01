// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Unified rigging editor.
 *
 * rigging_type:
 * - "motion" -> routes to IK Rig / IK Retargeter / Pose Search operations
 * - "control_rig" -> routes to Control Rig hierarchy and RigVM operations
 *
 * If omitted, routing is inferred from operation fields and asset type.
 */
class AGENTINTEGRATIONKIT_API FEditRiggingTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_rigging"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Unified rigging editor for motion assets (IK Rig, IK Retargeter, Pose Search) and Control Rig. "
			"Set rigging_type='motion' or rigging_type='control_rig', or omit to infer from parameters.");
	}

	virtual TSharedPtr<class FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;
};

