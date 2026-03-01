// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditIKRigTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// IK Rig struct-based API is only available in UE 5.6+
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6

// IK Rig includes
#include "Rig/IKRigDefinition.h"
#include "RigEditor/IKRigController.h"
#include "Rig/Solvers/IKRigSolverBase.h"
#include "Rig/Solvers/IKRigFullBodyIK.h"
#include "Rig/Solvers/IKRigLimbSolver.h"
#include "Rig/Solvers/IKRigPoleSolver.h"
#include "Rig/Solvers/IKRigBodyMoverSolver.h"
#include "Rig/Solvers/IKRigSetTransform.h"
#include "RigEditor/IKRigDefinitionFactory.h"

// Asset creation
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"

// Transaction support
#include "ScopedTransaction.h"

TSharedPtr<FJsonObject> FEditIKRigTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Basic parameters
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("IK Rig asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (defaults to /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> MeshProp = MakeShared<FJsonObject>();
	MeshProp->SetStringField(TEXT("type"), TEXT("string"));
	MeshProp->SetStringField(TEXT("description"), TEXT("Skeletal mesh path to use for the IK Rig (required for new rigs)"));
	Properties->SetObjectField(TEXT("skeletal_mesh"), MeshProp);

	// Solver operations
	TSharedPtr<FJsonObject> AddSolversProp = MakeShared<FJsonObject>();
	AddSolversProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSolversProp->SetStringField(TEXT("description"), TEXT("Solvers to add: [{type (FBIK/LimbSolver/PoleSolver/BodyMover/SetTransform), root_bone, end_bone, enabled, settings:{...}}]. FBIK=full body, LimbSolver=2-3 bone chains (arms/legs)"));
	Properties->SetObjectField(TEXT("add_solvers"), AddSolversProp);

	TSharedPtr<FJsonObject> RemoveSolversProp = MakeShared<FJsonObject>();
	RemoveSolversProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSolversProp->SetStringField(TEXT("description"), TEXT("Solver indices to remove (0-based)"));
	Properties->SetObjectField(TEXT("remove_solvers"), RemoveSolversProp);

	TSharedPtr<FJsonObject> ConfigureSolverProp = MakeShared<FJsonObject>();
	ConfigureSolverProp->SetStringField(TEXT("type"), TEXT("object"));
	ConfigureSolverProp->SetStringField(TEXT("description"), TEXT("Configure solver: {index, enabled, root_bone, end_bone, settings:{iterations, mass_multiplier, ...}}"));
	Properties->SetObjectField(TEXT("configure_solver"), ConfigureSolverProp);

	// Goal operations
	TSharedPtr<FJsonObject> AddGoalsProp = MakeShared<FJsonObject>();
	AddGoalsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddGoalsProp->SetStringField(TEXT("description"), TEXT("Goals (IK targets) to add: [{name, bone}]. Goals are positions that solvers pull bones toward."));
	Properties->SetObjectField(TEXT("add_goals"), AddGoalsProp);

	TSharedPtr<FJsonObject> RemoveGoalsProp = MakeShared<FJsonObject>();
	RemoveGoalsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveGoalsProp->SetStringField(TEXT("description"), TEXT("Goal names to remove"));
	Properties->SetObjectField(TEXT("remove_goals"), RemoveGoalsProp);

	TSharedPtr<FJsonObject> ConnectGoalsProp = MakeShared<FJsonObject>();
	ConnectGoalsProp->SetStringField(TEXT("type"), TEXT("array"));
	ConnectGoalsProp->SetStringField(TEXT("description"), TEXT("Connect goals to solvers: [{goal, solver_index}]. This creates effectors linking goals to solvers."));
	Properties->SetObjectField(TEXT("connect_goals"), ConnectGoalsProp);

	TSharedPtr<FJsonObject> DisconnectGoalsProp = MakeShared<FJsonObject>();
	DisconnectGoalsProp->SetStringField(TEXT("type"), TEXT("array"));
	DisconnectGoalsProp->SetStringField(TEXT("description"), TEXT("Disconnect goals from solvers: [{goal, solver_index}]"));
	Properties->SetObjectField(TEXT("disconnect_goals"), DisconnectGoalsProp);

	// Bone operations
	TSharedPtr<FJsonObject> ExcludeBonesProp = MakeShared<FJsonObject>();
	ExcludeBonesProp->SetStringField(TEXT("type"), TEXT("array"));
	ExcludeBonesProp->SetStringField(TEXT("description"), TEXT("Bone names to exclude from all solvers"));
	Properties->SetObjectField(TEXT("exclude_bones"), ExcludeBonesProp);

	TSharedPtr<FJsonObject> IncludeBonesProp = MakeShared<FJsonObject>();
	IncludeBonesProp->SetStringField(TEXT("type"), TEXT("array"));
	IncludeBonesProp->SetStringField(TEXT("description"), TEXT("Bone names to include (un-exclude) in solvers"));
	Properties->SetObjectField(TEXT("include_bones"), IncludeBonesProp);

	TSharedPtr<FJsonObject> AddBoneSettingsProp = MakeShared<FJsonObject>();
	AddBoneSettingsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddBoneSettingsProp->SetStringField(TEXT("description"), TEXT("Add solver-specific bone settings: [{bone, solver_index}]"));
	Properties->SetObjectField(TEXT("add_bone_settings"), AddBoneSettingsProp);

	// Retarget chain operations
	TSharedPtr<FJsonObject> AddChainsProp = MakeShared<FJsonObject>();
	AddChainsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddChainsProp->SetStringField(TEXT("description"), TEXT("Retarget chains to add: [{name, start_bone, end_bone, goal}]. Used for animation retargeting between skeletons."));
	Properties->SetObjectField(TEXT("add_retarget_chains"), AddChainsProp);

	TSharedPtr<FJsonObject> RemoveChainsProp = MakeShared<FJsonObject>();
	RemoveChainsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveChainsProp->SetStringField(TEXT("description"), TEXT("Retarget chain names to remove"));
	Properties->SetObjectField(TEXT("remove_retarget_chains"), RemoveChainsProp);

	TSharedPtr<FJsonObject> RetargetRootProp = MakeShared<FJsonObject>();
	RetargetRootProp->SetStringField(TEXT("type"), TEXT("string"));
	RetargetRootProp->SetStringField(TEXT("description"), TEXT("Bone name to set as retarget root (usually pelvis/hips)"));
	Properties->SetObjectField(TEXT("set_retarget_root"), RetargetRootProp);

	// Auto-generation
	TSharedPtr<FJsonObject> AutoRetargetProp = MakeShared<FJsonObject>();
	AutoRetargetProp->SetStringField(TEXT("type"), TEXT("boolean"));
	AutoRetargetProp->SetStringField(TEXT("description"), TEXT("Auto-generate retarget chains for known skeleton templates (Mannequin, MetaHuman, etc)"));
	Properties->SetObjectField(TEXT("auto_retarget"), AutoRetargetProp);

	TSharedPtr<FJsonObject> AutoFBIKProp = MakeShared<FJsonObject>();
	AutoFBIKProp->SetStringField(TEXT("type"), TEXT("boolean"));
	AutoFBIKProp->SetStringField(TEXT("description"), TEXT("Auto-generate full body IK setup for known skeleton templates"));
	Properties->SetObjectField(TEXT("auto_fbik"), AutoFBIKProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditIKRigTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path, SkeletalMeshPath;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("skeletal_mesh"), SkeletalMeshPath);

	// Try to load existing IK Rig
	UIKRigDefinition* IKRig = GetOrLoadIKRig(Name, Path);

	// If no existing rig, create a new one
	if (!IKRig)
	{
		if (SkeletalMeshPath.IsEmpty())
		{
			return FToolResult::Fail(TEXT("IK Rig not found and no skeletal_mesh specified for creation"));
		}

		IKRig = CreateIKRigAsset(Name, Path);
		if (!IKRig)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Failed to create IK Rig asset: %s"), *Name));
		}
	}

	// Get the controller for this IK Rig
	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		return FToolResult::Fail(TEXT("Failed to get IK Rig controller"));
	}

	// Set skeletal mesh if provided
	if (!SkeletalMeshPath.IsEmpty())
	{
		FString FullMeshPath = NeoStackToolUtils::BuildAssetPath(SkeletalMeshPath, TEXT(""));
		USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *FullMeshPath);
		if (!SkeletalMesh)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Skeletal mesh not found: %s"), *FullMeshPath));
		}

		if (!Controller->SetSkeletalMesh(SkeletalMesh))
		{
			return FToolResult::Fail(FString::Printf(TEXT("Failed to set skeletal mesh: %s"), *FullMeshPath));
		}
	}

	// Verify we have a skeletal mesh
	if (!Controller->GetSkeletalMesh())
	{
		return FToolResult::Fail(TEXT("IK Rig has no skeletal mesh. Specify skeletal_mesh parameter."));
	}

	TArray<FString> Results;
	int32 TotalChanges = 0;

	// Process auto-generation first (before manual operations)
	bool bAutoRetarget = false;
	bool bAutoFBIK = false;
	Args->TryGetBoolField(TEXT("auto_retarget"), bAutoRetarget);
	Args->TryGetBoolField(TEXT("auto_fbik"), bAutoFBIK);

	if (bAutoRetarget)
	{
		if (Controller->ApplyAutoGeneratedRetargetDefinition())
		{
			Results.Add(TEXT("Auto-generated retarget chains"));
			TotalChanges++;
		}
		else
		{
			Results.Add(TEXT("Auto-retarget: No matching skeleton template found"));
		}
	}

	if (bAutoFBIK)
	{
		if (Controller->ApplyAutoFBIK())
		{
			Results.Add(TEXT("Auto-generated full body IK setup"));
			TotalChanges++;
		}
		else
		{
			Results.Add(TEXT("Auto-FBIK: No matching skeleton template found"));
		}
	}

	// Process solver operations
	const TArray<TSharedPtr<FJsonValue>>* AddSolversArray;
	if (Args->TryGetArrayField(TEXT("add_solvers"), AddSolversArray))
	{
		TotalChanges += AddSolvers(Controller, AddSolversArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveSolversArray;
	if (Args->TryGetArrayField(TEXT("remove_solvers"), RemoveSolversArray))
	{
		TotalChanges += RemoveSolvers(Controller, RemoveSolversArray, Results);
	}

	const TSharedPtr<FJsonObject>* ConfigureSolverObj;
	if (Args->TryGetObjectField(TEXT("configure_solver"), ConfigureSolverObj))
	{
		if (ConfigureSolver(Controller, *ConfigureSolverObj, Results))
		{
			TotalChanges++;
		}
	}

	// Process goal operations
	const TArray<TSharedPtr<FJsonValue>>* AddGoalsArray;
	if (Args->TryGetArrayField(TEXT("add_goals"), AddGoalsArray))
	{
		TotalChanges += AddGoals(Controller, AddGoalsArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveGoalsArray;
	if (Args->TryGetArrayField(TEXT("remove_goals"), RemoveGoalsArray))
	{
		TotalChanges += RemoveGoals(Controller, RemoveGoalsArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* ConnectGoalsArray;
	if (Args->TryGetArrayField(TEXT("connect_goals"), ConnectGoalsArray))
	{
		TotalChanges += ConnectGoals(Controller, ConnectGoalsArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* DisconnectGoalsArray;
	if (Args->TryGetArrayField(TEXT("disconnect_goals"), DisconnectGoalsArray))
	{
		TotalChanges += DisconnectGoals(Controller, DisconnectGoalsArray, Results);
	}

	// Process bone operations
	const TArray<TSharedPtr<FJsonValue>>* ExcludeBonesArray;
	if (Args->TryGetArrayField(TEXT("exclude_bones"), ExcludeBonesArray))
	{
		TotalChanges += ExcludeBones(Controller, ExcludeBonesArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* IncludeBonesArray;
	if (Args->TryGetArrayField(TEXT("include_bones"), IncludeBonesArray))
	{
		TotalChanges += IncludeBones(Controller, IncludeBonesArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* AddBoneSettingsArray;
	if (Args->TryGetArrayField(TEXT("add_bone_settings"), AddBoneSettingsArray))
	{
		TotalChanges += AddBoneSettings(Controller, AddBoneSettingsArray, Results);
	}

	// Process retarget chain operations
	const TArray<TSharedPtr<FJsonValue>>* AddChainsArray;
	if (Args->TryGetArrayField(TEXT("add_retarget_chains"), AddChainsArray))
	{
		TotalChanges += AddRetargetChains(Controller, AddChainsArray, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveChainsArray;
	if (Args->TryGetArrayField(TEXT("remove_retarget_chains"), RemoveChainsArray))
	{
		TotalChanges += RemoveRetargetChains(Controller, RemoveChainsArray, Results);
	}

	FString RetargetRootBone;
	if (Args->TryGetStringField(TEXT("set_retarget_root"), RetargetRootBone) && !RetargetRootBone.IsEmpty())
	{
		if (SetRetargetRoot(Controller, RetargetRootBone, Results))
		{
			TotalChanges++;
		}
	}

	if (TotalChanges == 0 && !bAutoRetarget && !bAutoFBIK)
	{
		return FToolResult::Fail(TEXT("No operations specified. Use add_solvers, add_goals, add_retarget_chains, auto_fbik, etc."));
	}

	// Mark dirty and save
	IKRig->GetPackage()->MarkPackageDirty();

	// Build summary
	FString Summary;
	Summary += FString::Printf(TEXT("IK Rig: %s\n"), *IKRig->GetPathName());
	Summary += FString::Printf(TEXT("Skeletal Mesh: %s\n"), Controller->GetSkeletalMesh() ? *Controller->GetSkeletalMesh()->GetName() : TEXT("None"));
	Summary += FString::Printf(TEXT("Solvers: %d\n"), Controller->GetNumSolvers());
	Summary += FString::Printf(TEXT("Goals: %d\n"), Controller->GetAllGoals().Num());
	Summary += FString::Printf(TEXT("Retarget Chains: %d\n"), Controller->GetRetargetChains().Num());
	Summary += FString::Printf(TEXT("Retarget Root: %s\n"), *Controller->GetRetargetRoot().ToString());
	Summary += FString::Printf(TEXT("\nChanges (%d):\n"), TotalChanges);

	for (const FString& Result : Results)
	{
		Summary += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Summary);
}

// ============================================================================
// SOLVER OPERATIONS
// ============================================================================

int32 FEditIKRigTool::AddSolvers(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* SolversArray, TArray<FString>& OutResults)
{
	if (!SolversArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& SolverValue : *SolversArray)
	{
		const TSharedPtr<FJsonObject>* SolverObj;
		if (!SolverValue->TryGetObject(SolverObj))
		{
			continue;
		}

		FSolverOp Op = ParseSolverOp(*SolverObj);
		if (Op.Type.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped solver with no type"));
			continue;
		}

		FString SolverTypePath = GetSolverTypePath(Op.Type);
		if (SolverTypePath.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("Unknown solver type: %s"), *Op.Type));
			continue;
		}

		int32 SolverIndex = Controller->AddSolver(SolverTypePath);
		if (SolverIndex >= 0)
		{
			// Set start bone if provided
			if (!Op.RootBone.IsEmpty())
			{
				Controller->SetStartBone(FName(*Op.RootBone), SolverIndex);
			}

			// Set end bone if provided
			if (!Op.EndBone.IsEmpty())
			{
				if (!Controller->SetEndBone(FName(*Op.EndBone), SolverIndex))
				{
					OutResults.Add(FString::Printf(TEXT("Warning: SetEndBone('%s') failed for solver %d. For LimbSolvers, use a goal connected to the solver instead."), *Op.EndBone, SolverIndex));
				}
			}

			// Set enabled state
			Controller->SetSolverEnabled(SolverIndex, Op.bEnabled);

			// Apply solver-specific settings
			if (Op.Settings.IsValid())
			{
				FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(SolverIndex);
				if (Solver)
				{
					ApplySolverSettings(Solver, Op.Settings, Controller);
				}
			}

			OutResults.Add(FString::Printf(TEXT("Added solver: %s (index %d)"), *Op.Type, SolverIndex));
			Added++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add solver: %s"), *Op.Type));
		}
	}

	return Added;
}

int32 FEditIKRigTool::RemoveSolvers(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* SolversArray, TArray<FString>& OutResults)
{
	if (!SolversArray) return 0;

	// Collect indices and sort descending to remove from end first
	TArray<int32> Indices;
	for (const TSharedPtr<FJsonValue>& IndexValue : *SolversArray)
	{
		int32 Index = 0;
		if (IndexValue->TryGetNumber(Index))
		{
			Indices.AddUnique(Index);
		}
	}
	Indices.Sort([](int32 A, int32 B) { return A > B; });

	int32 Removed = 0;
	for (int32 Index : Indices)
	{
		if (Index >= 0 && Index < Controller->GetNumSolvers())
		{
			if (Controller->RemoveSolver(Index))
			{
				OutResults.Add(FString::Printf(TEXT("Removed solver at index %d"), Index));
				Removed++;
			}
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Invalid solver index: %d"), Index));
		}
	}

	return Removed;
}

bool FEditIKRigTool::ConfigureSolver(UIKRigController* Controller, const TSharedPtr<FJsonObject>& ConfigObj, TArray<FString>& OutResults)
{
	int32 Index = -1;
	if (!ConfigObj->TryGetNumberField(TEXT("index"), Index) || Index < 0 || Index >= Controller->GetNumSolvers())
	{
		OutResults.Add(TEXT("configure_solver: Invalid or missing index"));
		return false;
	}

	TArray<FString> Changes;

	// Set enabled state
	bool bEnabled;
	if (ConfigObj->TryGetBoolField(TEXT("enabled"), bEnabled))
	{
		Controller->SetSolverEnabled(Index, bEnabled);
		Changes.Add(FString::Printf(TEXT("enabled=%s"), bEnabled ? TEXT("true") : TEXT("false")));
	}

	// Set start bone
	FString RootBone;
	if (ConfigObj->TryGetStringField(TEXT("root_bone"), RootBone) && !RootBone.IsEmpty())
	{
		if (Controller->SetStartBone(FName(*RootBone), Index))
		{
			Changes.Add(FString::Printf(TEXT("start=%s"), *RootBone));
		}
	}

	// Set end bone
	FString EndBone;
	if (ConfigObj->TryGetStringField(TEXT("end_bone"), EndBone) && !EndBone.IsEmpty())
	{
		if (Controller->SetEndBone(FName(*EndBone), Index))
		{
			Changes.Add(FString::Printf(TEXT("end=%s"), *EndBone));
		}
	}

	// Apply solver-specific settings
	const TSharedPtr<FJsonObject>* SettingsObj;
	if (ConfigObj->TryGetObjectField(TEXT("settings"), SettingsObj))
	{
		FIKRigSolverBase* Solver = Controller->GetSolverAtIndex(Index);
		if (Solver)
		{
			ApplySolverSettings(Solver, *SettingsObj, Controller);
			Changes.Add(TEXT("settings updated"));
		}
	}

	if (Changes.Num() > 0)
	{
		OutResults.Add(FString::Printf(TEXT("Configured solver %d: %s"), Index, *FString::Join(Changes, TEXT(", "))));
		return true;
	}

	return false;
}

// ============================================================================
// GOAL OPERATIONS
// ============================================================================

int32 FEditIKRigTool::AddGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* GoalsArray, TArray<FString>& OutResults)
{
	if (!GoalsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& GoalValue : *GoalsArray)
	{
		const TSharedPtr<FJsonObject>* GoalObj;
		if (!GoalValue->TryGetObject(GoalObj) || !GoalObj)
		{
			continue;
		}

		FGoalOp Op = ParseGoalOp(*GoalObj);
		if (Op.Name.IsEmpty() || Op.Bone.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped goal with missing name or bone"));
			continue;
		}

		FName ResultName = Controller->AddNewGoal(FName(*Op.Name), FName(*Op.Bone));
		if (!ResultName.IsNone())
		{
			OutResults.Add(FString::Printf(TEXT("Added goal: %s -> bone %s"), *ResultName.ToString(), *Op.Bone));
			Added++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add goal: %s (bone may not exist)"), *Op.Name));
		}
	}

	return Added;
}

int32 FEditIKRigTool::RemoveGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* GoalsArray, TArray<FString>& OutResults)
{
	if (!GoalsArray) return 0;

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& GoalValue : *GoalsArray)
	{
		FString GoalName;
		if (!GoalValue->TryGetString(GoalName))
		{
			continue;
		}

		if (Controller->RemoveGoal(FName(*GoalName)))
		{
			OutResults.Add(FString::Printf(TEXT("Removed goal: %s"), *GoalName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Goal not found: %s"), *GoalName));
		}
	}

	return Removed;
}

int32 FEditIKRigTool::ConnectGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray, TArray<FString>& OutResults)
{
	if (!ConnectionsArray) return 0;

	int32 Connected = 0;
	const int32 NumSolvers = Controller->GetNumSolvers();

	for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
	{
		const TSharedPtr<FJsonObject>* ConnObj;
		if (!ConnValue->TryGetObject(ConnObj) || !ConnObj)
		{
			continue;
		}

		FGoalConnectionOp Op = ParseGoalConnectionOp(*ConnObj);
		if (Op.Goal.IsEmpty() || Op.SolverIndex < 0)
		{
			OutResults.Add(TEXT("Skipped connection with missing goal or invalid solver_index"));
			continue;
		}

		// CRITICAL: Validate solver index is within bounds (engine pattern)
		if (Op.SolverIndex >= NumSolvers)
		{
			OutResults.Add(FString::Printf(TEXT("Solver index %d out of range (only %d solvers)"), Op.SolverIndex, NumSolvers));
			continue;
		}

		if (Controller->ConnectGoalToSolver(FName(*Op.Goal), Op.SolverIndex))
		{
			OutResults.Add(FString::Printf(TEXT("Connected goal %s to solver %d"), *Op.Goal, Op.SolverIndex));
			Connected++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to connect goal %s to solver %d"), *Op.Goal, Op.SolverIndex));
		}
	}

	return Connected;
}

int32 FEditIKRigTool::DisconnectGoals(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray, TArray<FString>& OutResults)
{
	if (!ConnectionsArray) return 0;

	int32 Disconnected = 0;
	const int32 NumSolvers = Controller->GetNumSolvers();

	for (const TSharedPtr<FJsonValue>& ConnValue : *ConnectionsArray)
	{
		const TSharedPtr<FJsonObject>* ConnObj;
		if (!ConnValue->TryGetObject(ConnObj) || !ConnObj)
		{
			continue;
		}

		FGoalConnectionOp Op = ParseGoalConnectionOp(*ConnObj);
		if (Op.Goal.IsEmpty() || Op.SolverIndex < 0)
		{
			continue;
		}

		// CRITICAL: Validate solver index is within bounds (engine pattern)
		if (Op.SolverIndex >= NumSolvers)
		{
			OutResults.Add(FString::Printf(TEXT("Solver index %d out of range (only %d solvers)"), Op.SolverIndex, NumSolvers));
			continue;
		}

		if (Controller->DisconnectGoalFromSolver(FName(*Op.Goal), Op.SolverIndex))
		{
			OutResults.Add(FString::Printf(TEXT("Disconnected goal %s from solver %d"), *Op.Goal, Op.SolverIndex));
			Disconnected++;
		}
	}

	return Disconnected;
}

// ============================================================================
// BONE OPERATIONS
// ============================================================================

int32 FEditIKRigTool::ExcludeBones(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* BonesArray, TArray<FString>& OutResults)
{
	if (!BonesArray) return 0;

	int32 Excluded = 0;
	for (const TSharedPtr<FJsonValue>& BoneValue : *BonesArray)
	{
		FString BoneName;
		if (!BoneValue->TryGetString(BoneName))
		{
			continue;
		}

		if (Controller->SetBoneExcluded(FName(*BoneName), true))
		{
			OutResults.Add(FString::Printf(TEXT("Excluded bone: %s"), *BoneName));
			Excluded++;
		}
	}

	return Excluded;
}

int32 FEditIKRigTool::IncludeBones(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* BonesArray, TArray<FString>& OutResults)
{
	if (!BonesArray) return 0;

	int32 Included = 0;
	for (const TSharedPtr<FJsonValue>& BoneValue : *BonesArray)
	{
		FString BoneName;
		if (!BoneValue->TryGetString(BoneName))
		{
			continue;
		}

		if (Controller->SetBoneExcluded(FName(*BoneName), false))
		{
			OutResults.Add(FString::Printf(TEXT("Included bone: %s"), *BoneName));
			Included++;
		}
	}

	return Included;
}

int32 FEditIKRigTool::AddBoneSettings(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* SettingsArray, TArray<FString>& OutResults)
{
	if (!SettingsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& SettingValue : *SettingsArray)
	{
		const TSharedPtr<FJsonObject>* SettingObj;
		if (!SettingValue->TryGetObject(SettingObj))
		{
			continue;
		}

		FBoneSettingOp Op = ParseBoneSettingOp(*SettingObj);
		if (Op.Bone.IsEmpty() || Op.SolverIndex < 0)
		{
			continue;
		}

		if (Op.SolverIndex >= Controller->GetNumSolvers())
		{
			OutResults.Add(FString::Printf(TEXT("Invalid solver index %d for bone setting (max: %d)"), Op.SolverIndex, Controller->GetNumSolvers() - 1));
			continue;
		}

		if (Controller->AddBoneSetting(FName(*Op.Bone), Op.SolverIndex))
		{
			OutResults.Add(FString::Printf(TEXT("Added bone setting: %s for solver %d"), *Op.Bone, Op.SolverIndex));
			Added++;
		}
	}

	return Added;
}

// ============================================================================
// RETARGET CHAIN OPERATIONS
// ============================================================================

int32 FEditIKRigTool::AddRetargetChains(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults)
{
	if (!ChainsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& ChainValue : *ChainsArray)
	{
		const TSharedPtr<FJsonObject>* ChainObj;
		if (!ChainValue->TryGetObject(ChainObj))
		{
			continue;
		}

		FRetargetChainOp Op = ParseRetargetChainOp(*ChainObj);
		if (Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped chain with no name"));
			continue;
		}

		FName ResultName = Controller->AddRetargetChain(
			FName(*Op.Name),
			FName(*Op.StartBone),
			FName(*Op.EndBone),
			FName(*Op.Goal)
		);

		if (!ResultName.IsNone())
		{
			OutResults.Add(FString::Printf(TEXT("Added retarget chain: %s (%s -> %s)"),
				*ResultName.ToString(), *Op.StartBone, *Op.EndBone));
			Added++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add chain: %s"), *Op.Name));
		}
	}

	return Added;
}

int32 FEditIKRigTool::RemoveRetargetChains(UIKRigController* Controller, const TArray<TSharedPtr<FJsonValue>>* ChainsArray, TArray<FString>& OutResults)
{
	if (!ChainsArray) return 0;

	int32 Removed = 0;
	for (const TSharedPtr<FJsonValue>& ChainValue : *ChainsArray)
	{
		FString ChainName;
		if (!ChainValue->TryGetString(ChainName))
		{
			continue;
		}

		if (Controller->RemoveRetargetChain(FName(*ChainName)))
		{
			OutResults.Add(FString::Printf(TEXT("Removed chain: %s"), *ChainName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Chain not found: %s"), *ChainName));
		}
	}

	return Removed;
}

bool FEditIKRigTool::SetRetargetRoot(UIKRigController* Controller, const FString& RootBoneName, TArray<FString>& OutResults)
{
	Controller->SetRetargetRoot(FName(*RootBoneName));

	FName ActualRoot = Controller->GetRetargetRoot();
	if (ActualRoot == FName(*RootBoneName))
	{
		OutResults.Add(FString::Printf(TEXT("Set retarget root: %s"), *RootBoneName));
		return true;
	}

	OutResults.Add(FString::Printf(TEXT("Failed to set retarget root '%s' - bone not found in skeleton (result: %s)"), *RootBoneName, *ActualRoot.ToString()));
	return false;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

FEditIKRigTool::FSolverOp FEditIKRigTool::ParseSolverOp(const TSharedPtr<FJsonObject>& SolverObj)
{
	FSolverOp Op;
	SolverObj->TryGetStringField(TEXT("type"), Op.Type);
	SolverObj->TryGetStringField(TEXT("root_bone"), Op.RootBone);
	SolverObj->TryGetStringField(TEXT("end_bone"), Op.EndBone);
	SolverObj->TryGetBoolField(TEXT("enabled"), Op.bEnabled);

	const TSharedPtr<FJsonObject>* SettingsObj;
	if (SolverObj->TryGetObjectField(TEXT("settings"), SettingsObj))
	{
		Op.Settings = *SettingsObj;
	}

	return Op;
}

FEditIKRigTool::FGoalOp FEditIKRigTool::ParseGoalOp(const TSharedPtr<FJsonObject>& GoalObj)
{
	FGoalOp Op;
	GoalObj->TryGetStringField(TEXT("name"), Op.Name);
	GoalObj->TryGetStringField(TEXT("bone"), Op.Bone);
	return Op;
}

FEditIKRigTool::FGoalConnectionOp FEditIKRigTool::ParseGoalConnectionOp(const TSharedPtr<FJsonObject>& ConnectionObj)
{
	FGoalConnectionOp Op;
	ConnectionObj->TryGetStringField(TEXT("goal"), Op.Goal);
	ConnectionObj->TryGetNumberField(TEXT("solver_index"), Op.SolverIndex);
	return Op;
}

FEditIKRigTool::FBoneSettingOp FEditIKRigTool::ParseBoneSettingOp(const TSharedPtr<FJsonObject>& SettingObj)
{
	FBoneSettingOp Op;
	SettingObj->TryGetStringField(TEXT("bone"), Op.Bone);
	SettingObj->TryGetNumberField(TEXT("solver_index"), Op.SolverIndex);
	return Op;
}

FEditIKRigTool::FRetargetChainOp FEditIKRigTool::ParseRetargetChainOp(const TSharedPtr<FJsonObject>& ChainObj)
{
	FRetargetChainOp Op;
	ChainObj->TryGetStringField(TEXT("name"), Op.Name);
	ChainObj->TryGetStringField(TEXT("start_bone"), Op.StartBone);
	ChainObj->TryGetStringField(TEXT("end_bone"), Op.EndBone);
	ChainObj->TryGetStringField(TEXT("goal"), Op.Goal);
	return Op;
}

FString FEditIKRigTool::GetSolverTypePath(const FString& TypeName)
{
	if (TypeName.Equals(TEXT("FBIK"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("FullBodyIK"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("FBIKSolver"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRigFullBodyIKSolver");
	}
	else if (TypeName.Equals(TEXT("LimbSolver"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Limb"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRigLimbSolver");
	}
	else if (TypeName.Equals(TEXT("PoleSolver"), ESearchCase::IgnoreCase) ||
			 TypeName.Equals(TEXT("Pole"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRigPoleSolver");
	}
	else if (TypeName.Equals(TEXT("BodyMover"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRigBodyMoverSolver");
	}
	else if (TypeName.Equals(TEXT("SetTransform"), ESearchCase::IgnoreCase))
	{
		return TEXT("/Script/IKRig.IKRigSetTransform");
	}

	return FString();
}

void FEditIKRigTool::ApplySolverSettings(FIKRigSolverBase* Solver, const TSharedPtr<FJsonObject>& Settings, UIKRigController* Controller)
{
	if (!Solver || !Settings.IsValid())
	{
		return;
	}

	// Get the solver settings struct
	FIKRigSolverSettingsBase* SolverSettings = Solver->GetSolverSettings();
	if (!SolverSettings)
	{
		return;
	}

	const UScriptStruct* SettingsType = Solver->GetSolverSettingsType();
	if (!SettingsType)
	{
		return;
	}

	// Use reflection to set properties on the settings struct
	for (const auto& Pair : Settings->Values)
	{
		FName PropertyName(*Pair.Key);
		FProperty* Property = SettingsType->FindPropertyByName(PropertyName);
		if (!Property)
		{
			continue;
		}

		// Skip read-only properties
		if (Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly | CPF_EditConst))
		{
			continue;
		}

		void* PropertyValue = Property->ContainerPtrToValuePtr<void>(SolverSettings);

		// Handle different value types
		if (Pair.Value->Type == EJson::Number)
		{
			double NumValue = Pair.Value->AsNumber();
			if (FFloatProperty* FloatProp = CastField<FFloatProperty>(Property))
			{
				FloatProp->SetPropertyValue(PropertyValue, static_cast<float>(NumValue));
			}
			else if (FDoubleProperty* DoubleProp = CastField<FDoubleProperty>(Property))
			{
				DoubleProp->SetPropertyValue(PropertyValue, NumValue);
			}
			else if (FIntProperty* IntProp = CastField<FIntProperty>(Property))
			{
				IntProp->SetPropertyValue(PropertyValue, static_cast<int32>(NumValue));
			}
		}
		else if (Pair.Value->Type == EJson::Boolean)
		{
			if (FBoolProperty* BoolProp = CastField<FBoolProperty>(Property))
			{
				BoolProp->SetPropertyValue(PropertyValue, Pair.Value->AsBool());
			}
		}
		else if (Pair.Value->Type == EJson::String)
		{
			FString StrValue = Pair.Value->AsString();
			Property->ImportText_Direct(*StrValue, PropertyValue, nullptr, PPF_None);
		}
	}

	// Reinitialize solver after settings change so it picks up new values
	if (Controller)
	{
		Controller->BroadcastNeedsReinitialized();
	}
}

UIKRigDefinition* FEditIKRigTool::CreateIKRigAsset(const FString& AssetName, const FString& AssetPath)
{
	FString SanitizedName;
	UPackage* Package = NeoStackToolUtils::CreateAssetPackage(AssetName, AssetPath, SanitizedName);
	if (!Package)
	{
		return nullptr;
	}

	UIKRigDefinitionFactory* Factory = NewObject<UIKRigDefinitionFactory>();
	UIKRigDefinition* NewRig = Cast<UIKRigDefinition>(Factory->FactoryCreateNew(
		UIKRigDefinition::StaticClass(),
		Package,
		FName(*SanitizedName),
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (NewRig)
	{
		FAssetRegistryModule::AssetCreated(NewRig);
		Package->MarkPackageDirty();
	}

	return NewRig;
}

UIKRigDefinition* FEditIKRigTool::GetOrLoadIKRig(const FString& Name, const FString& Path)
{
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	return LoadObject<UIKRigDefinition>(nullptr, *FullAssetPath);
}

#else // UE 5.5 - IK Rig struct-based API not available

TSharedPtr<FJsonObject> FEditIKRigTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("IK Rig asset name"));
	Properties->SetObjectField(TEXT("name"), NameProp);
	Schema->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);
	return Schema;
}

FToolResult FEditIKRigTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	return FToolResult::Fail(TEXT("edit_ikrig tool requires Unreal Engine 5.6 or later. The IK Rig struct-based API is not available in UE 5.5."));
}

#endif // ENGINE_MINOR_VERSION >= 6
