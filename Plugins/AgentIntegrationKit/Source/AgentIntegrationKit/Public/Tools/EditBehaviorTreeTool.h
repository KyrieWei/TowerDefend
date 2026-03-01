// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBehaviorTree;
class UBlackboardData;
class UBTCompositeNode;

/**
 * Tool for editing Behavior Tree blackboard keys and child ordering.
 * For adding/removing nodes, decorators, services, and setting properties,
 * use edit_graph discovery (operation='find_nodes') + edit_graph instead.
 * Use configure_asset for BlackboardAsset, Parent, and other property assignments.
 *
 * Operations:
 * - add_key / remove_key: Add/remove blackboard keys
 * - reorder_children: Reorder children of a composite node
 */
class AGENTINTEGRATIONKIT_API FEditBehaviorTreeTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_behavior_tree"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Behavior Tree blackboard keys and child ordering. "
			"Use configure_asset for setting BlackboardAsset and Parent properties. "
			"For adding/removing nodes, decorators, services, and setting properties, use edit_graph discovery + edit_graph instead.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Blackboard key definition */
	struct FBlackboardKeyDefinition
	{
		FString Name;       // Key name
		FString Type;       // Key type (Bool, Int, Float, String, Name, Vector, Rotator, Object, Class, Enum)
		FString BaseClass;  // For Object/Class types, the base class name
		FString Category;   // Optional category
		FString Description; // Optional key description
		bool bInstanceSynced = false;
	};

	// ========== Behavior Tree Operations ==========

	/** Reorder children of a composite node */
	FString ReorderChildren(UBehaviorTree* BehaviorTree, const FString& ParentName, const TArray<FString>& Order);

	/** Find a BT composite node by name (needed by reorder) */
	UBTCompositeNode* FindCompositeByName(UBTCompositeNode* Root, const FString& Name);

	// ========== Blackboard Operations ==========

	/** Find a blackboard key type class by name */
	UClass* FindBlackboardKeyTypeClass(const FString& TypeName);

	/** Add a key to the blackboard */
	FString AddBlackboardKey(UBlackboardData* Blackboard, const FBlackboardKeyDefinition& KeyDef);

	/** Remove a key from the blackboard */
	FString RemoveBlackboardKey(UBlackboardData* Blackboard, const FString& KeyName);
};
