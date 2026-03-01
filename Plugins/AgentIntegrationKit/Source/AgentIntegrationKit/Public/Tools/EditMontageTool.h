// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UAnimMontage;

/**
 * Tool for editing Animation Montages:
 * - Add/remove slot animation tracks with animation segments
 * - Add/remove animation segments in slot tracks
 * - Add/remove/link composite sections
 * - Add/remove anim notifies
 * Blend settings (BlendIn.BlendTime, BlendOut.BlendTime, etc.) are set via configure_asset.
 */
class AGENTINTEGRATIONKIT_API FEditMontageTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_montage"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Animation Montages: add/remove slot tracks with animations, add/remove animation segments, add/remove/link sections, add/remove notifies, set blend settings");
	}

	virtual TSharedPtr<class FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;
};
