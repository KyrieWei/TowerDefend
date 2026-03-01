// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class USkeletalMesh;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
class UIKRigDefinition;
class UIKRigController;
struct FIKRigSolverBase;
#endif

/**
 * Tool for creating and editing IK Rig assets
 *
 * IK Rigs define inverse kinematics configurations for skeletal meshes, enabling:
 * - Foot IK (ground adaptation)
 * - Hand IK (reaching/grabbing)
 * - Head/look-at tracking
 * - Full body IK for procedural animation
 * - Animation retargeting between different skeletons
 *
 * Parameters:
 *   - name: IK Rig asset name (required)
 *   - path: Asset folder path (optional, defaults to /Game)
 *   - skeletal_mesh: Path to skeletal mesh to use (required for new rigs)
 *
 * Solver operations:
 *   - add_solvers: Array of solver definitions [{type, root_bone, end_bone, enabled, settings}]
 *       Types: "FBIK", "LimbSolver", "PoleSolver", "BodyMover", "SetTransform"
 *   - remove_solvers: Array of solver indices to remove
 *   - configure_solver: Configure a solver by index {index, enabled, root_bone, end_bone, settings}
 *
 * Goal operations:
 *   - add_goals: Array of goal definitions [{name, bone}]
 *   - remove_goals: Array of goal names to remove
 *   - connect_goals: Array of goal-solver connections [{goal, solver_index}]
 *   - disconnect_goals: Array of goal-solver disconnections [{goal, solver_index}]
 *
 * Bone operations:
 *   - exclude_bones: Array of bone names to exclude from IK
 *   - include_bones: Array of bone names to include in IK
 *   - add_bone_settings: Array of bone settings [{bone, solver_index}]
 *
 * Retarget chain operations:
 *   - add_retarget_chains: Array of chain definitions [{name, start_bone, end_bone, goal}]
 *   - remove_retarget_chains: Array of chain names to remove
 *   - set_retarget_root: Bone name to use as retarget root
 *
 * Auto-generation:
 *   - auto_retarget: Boolean - auto-generate retarget chains for known skeleton templates
 *   - auto_fbik: Boolean - auto-generate full body IK setup for known templates
 */
class AGENTINTEGRATIONKIT_API FEditIKRigTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_ikrig"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create and edit IK Rig assets for inverse kinematics, foot IK, hand reaching, and animation retargeting. "
			"Use add_solvers to add IK solving algorithms (FBIK for full body, LimbSolver for arms/legs). "
			"Use add_goals to create IK targets that solvers pull bones toward. "
			"Use add_retarget_chains to define bone chains for animation retargeting between skeletons. "
			"Use auto_fbik=true to auto-generate a full body IK setup for standard humanoid skeletons.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Solver definition from JSON */
	struct FSolverOp
	{
		FString Type;
		FString RootBone;
		FString EndBone;
		bool bEnabled = true;
		TSharedPtr<FJsonObject> Settings;
	};

	/** Goal definition from JSON */
	struct FGoalOp
	{
		FString Name;
		FString Bone;
	};

	/** Goal-solver connection from JSON */
	struct FGoalConnectionOp
	{
		FString Goal;
		int32 SolverIndex = -1;
	};

	/** Bone setting from JSON */
	struct FBoneSettingOp
	{
		FString Bone;
		int32 SolverIndex = -1;
		bool bExclude = false;
	};

	/** Retarget chain definition from JSON */
	struct FRetargetChainOp
	{
		FString Name;
		FString StartBone;
		FString EndBone;
		FString Goal;
	};

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	// Solver operations
	int32 AddSolvers(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* SolversArray, TArray<FString>& OutResults);
	int32 RemoveSolvers(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* SolversArray, TArray<FString>& OutResults);
	bool ConfigureSolver(UIKRigController* Controller, const TSharedPtr<FJsonObject>& ConfigObj, TArray<FString>& OutResults);

	// Goal operations
	int32 AddGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* GoalsArray, TArray<FString>& OutResults);
	int32 RemoveGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* GoalsArray, TArray<FString>& OutResults);
	int32 ConnectGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray, TArray<FString>& OutResults);
	int32 DisconnectGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray, TArray<FString>& OutResults);

	// Bone operations
	int32 ExcludeBones(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* BonesArray, TArray<FString>& OutResults);
	int32 IncludeBones(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* BonesArray, TArray<FString>& OutResults);
	int32 AddBoneSettings(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* SettingsArray, TArray<FString>& OutResults);

	// Retarget chain operations
	int32 AddRetargetChains(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults);
	int32 RemoveRetargetChains(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults);
	bool SetRetargetRoot(UIKRigController* Controller, const FString& RootBoneName, TArray<FString>& OutResults);

	// Parsing helpers
	FSolverOp ParseSolverOp(const TSharedPtr<FJsonObject>& SolverObj);
	FGoalOp ParseGoalOp(const TSharedPtr<FJsonObject>& GoalObj);
	FGoalConnectionOp ParseGoalConnectionOp(const TSharedPtr<FJsonObject>& ConnectionObj);
	FBoneSettingOp ParseBoneSettingOp(const TSharedPtr<FJsonObject>& SettingObj);
	FRetargetChainOp ParseRetargetChainOp(const TSharedPtr<FJsonObject>& ChainObj);

	/** Get solver type path from type name */
	FString GetSolverTypePath(const FString& TypeName);

	/** Apply solver-specific settings from JSON */
	void ApplySolverSettings(FIKRigSolverBase* Solver, const TSharedPtr<FJsonObject>& Settings, UIKRigController* Controller);

	/** Create a new IK Rig asset */
	UIKRigDefinition* CreateIKRigAsset(const FString& AssetName, const FString& AssetPath);

	/** Get or load an IK Rig asset */
	UIKRigDefinition* GetOrLoadIKRig(const FString& Name, const FString& Path);
#endif
};
