// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Unified AI tree editor.
 *
 * tree_type:
 * - "behavior_tree" -> routes to Behavior Tree / Blackboard editing
 * - "state_tree" -> routes to StateTree editing
 *
 * If omitted, routing is inferred from operation fields and asset type.
 */
class AGENTINTEGRATIONKIT_API FEditAITreeTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_ai_tree"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit AI trees in one tool. Use tree_type='behavior_tree' for BehaviorTree/Blackboard edits "
			"or tree_type='state_tree' for StateTree edits. If omitted, routing is inferred.");
	}

	virtual TSharedPtr<class FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<class FJsonObject>& Args) override;
};

