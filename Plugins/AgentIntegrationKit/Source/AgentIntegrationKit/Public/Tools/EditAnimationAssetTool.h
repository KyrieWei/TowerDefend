// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Unified animation asset editor.
 *
 * animation_asset_type:
 * - "montage" -> routes to montage editing
 * - "anim_sequence" -> routes to animation sequence editing
 * - "blend_space" -> routes to blend space / aim offset editing
 *
 * If omitted, routing is inferred from operation fields and asset type.
 */
class AGENTINTEGRATIONKIT_API FEditAnimationAssetTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_animation_asset"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Unified animation asset editor for montages, anim sequences, and blend spaces/aim offsets. "
			"Set animation_asset_type, or omit it to infer from parameters.");
	}

	virtual TSharedPtr<class FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;
};

