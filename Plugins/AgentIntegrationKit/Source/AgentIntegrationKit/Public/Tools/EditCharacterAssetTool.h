// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Unified character asset editor.
 *
 * character_asset_type:
 * - "skeleton" -> routes to skeleton editing
 * - "physics_asset" -> routes to physics asset editing
 *
 * If omitted, routing is inferred from operation fields and asset type.
 */
class AGENTINTEGRATIONKIT_API FEditCharacterAssetTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_character_asset"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Unified character asset editor for Skeleton and PhysicsAsset workflows. "
			"Set character_asset_type='skeleton' or 'physics_asset', or omit to infer from parameters.");
	}

	virtual TSharedPtr<class FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;
};

