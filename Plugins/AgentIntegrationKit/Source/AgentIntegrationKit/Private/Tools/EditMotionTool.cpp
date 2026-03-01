// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditMotionTool.h"
#include "Tools/EditIKRigTool.h"
#include "Tools/EditIKRetargeterTool.h"
#include "Tools/NeoStackToolUtils.h"
#if WITH_POSE_SEARCH
#include "Tools/EditPoseSearchTool.h"
#endif
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	static const TArray<FString>& GetIKRetargeterInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("source_ikrig"),
			TEXT("target_ikrig"),
			TEXT("add_default_ops"),
			TEXT("add_ops"),
			TEXT("remove_ops"),
			TEXT("move_op"),
			TEXT("enable_ops"),
			TEXT("disable_ops"),
			TEXT("auto_map_chains"),
			TEXT("map_chains"),
			TEXT("unmap_chains"),
			TEXT("create_pose"),
			TEXT("delete_pose"),
			TEXT("set_current_pose"),
			TEXT("auto_align"),
			TEXT("edit_pose_bones"),
			TEXT("set_root_offset"),
			TEXT("retarget_animations"),
			TEXT("configure_fk_chains"),
			TEXT("configure_ik_chains"),
			TEXT("configure_pelvis"),
			TEXT("configure_root_motion"),
			TEXT("set_preview_mesh"),
			TEXT("duplicate_pose"),
			TEXT("rename_pose"),
			TEXT("reset_pose_bones"),
			TEXT("snap_to_ground")
		};
		return Fields;
	}

	static const TArray<FString>& GetPoseSearchInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("asset_type"),
			TEXT("skeleton"),
			TEXT("mirror_data_table"),
			TEXT("add_channels"),
			TEXT("add_animations"),
			TEXT("remove_animations"),
			TEXT("add_databases"),
			TEXT("remove_databases")
		};
		return Fields;
	}

	static const TArray<FString>& GetIKRigInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("skeletal_mesh"),
			TEXT("add_solvers"),
			TEXT("remove_solvers"),
			TEXT("configure_solver"),
			TEXT("add_goals"),
			TEXT("remove_goals"),
			TEXT("connect_goals"),
			TEXT("disconnect_goals"),
			TEXT("exclude_bones"),
			TEXT("include_bones"),
			TEXT("add_bone_settings"),
			TEXT("add_retarget_chains"),
			TEXT("remove_retarget_chains"),
			TEXT("set_retarget_root"),
			TEXT("auto_retarget"),
			TEXT("auto_fbik")
		};
		return Fields;
	}

	static bool HasAnyField(const TSharedPtr<FJsonObject>& Args, const TArray<FString>& Fields)
	{
		for (const FString& Field : Fields)
		{
			if (Args->HasField(Field))
			{
				return true;
			}
		}
		return false;
	}

	static void MergeSchemaProperties(TSharedPtr<FJsonObject> TargetSchema, const TSharedPtr<FJsonObject>& SourceSchema)
	{
		if (!TargetSchema.IsValid() || !SourceSchema.IsValid())
		{
			return;
		}

		TSharedPtr<FJsonObject> TargetProps;
		const TSharedPtr<FJsonObject>* TargetPropsPtr = nullptr;
		if (!TargetSchema->TryGetObjectField(TEXT("properties"), TargetPropsPtr) || !TargetPropsPtr || !(*TargetPropsPtr).IsValid())
		{
			TargetProps = MakeShared<FJsonObject>();
			TargetSchema->SetObjectField(TEXT("properties"), TargetProps);
		}
		else
		{
			TargetProps = *TargetPropsPtr;
		}

		const TSharedPtr<FJsonObject>* SourceProps = nullptr;
		if (!SourceSchema->TryGetObjectField(TEXT("properties"), SourceProps) || !SourceProps || !(*SourceProps).IsValid())
		{
			return;
		}

		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*SourceProps)->Values)
		{
			if (!TargetProps->HasField(Pair.Key))
			{
				TargetProps->SetField(Pair.Key, Pair.Value);
			}
		}
	}
}

TSharedPtr<FJsonObject> FEditMotionTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> MotionTypeProp = MakeShared<FJsonObject>();
	MotionTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	MotionTypeProp->SetStringField(TEXT("description"), TEXT("Motion target: 'ikrig', 'ikretargeter', or 'pose_search'."));
	TArray<TSharedPtr<FJsonValue>> MotionTypeEnum;
	MotionTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("ikrig")));
	MotionTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("ikretargeter")));
	MotionTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("pose_search")));
	MotionTypeProp->SetArrayField(TEXT("enum"), MotionTypeEnum);
	Properties->SetObjectField(TEXT("motion_type"), MotionTypeProp);

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Motion asset name or path."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path."));
	Properties->SetObjectField(TEXT("path"), PathProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	// Merge full parameter surfaces from delegated tools for parity/discoverability.
	FEditIKRigTool IKRigTool;
	FEditIKRetargeterTool IKRetargeterTool;
	MergeSchemaProperties(Schema, IKRigTool.GetInputSchema());
	MergeSchemaProperties(Schema, IKRetargeterTool.GetInputSchema());
#if WITH_POSE_SEARCH
	FEditPoseSearchTool PoseSearchTool;
	MergeSchemaProperties(Schema, PoseSearchTool.GetInputSchema());
#endif

	// Unified tool: requirements vary by route.
	Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});

	return Schema;
}

FToolResult FEditMotionTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString MotionType;
	Args->TryGetStringField(TEXT("motion_type"), MotionType);
	MotionType = MotionType.ToLower();

	if (MotionType.IsEmpty())
	{
		const bool bHasIKRetargeterFields = HasAnyField(Args, GetIKRetargeterInferenceFields());
		const bool bHasPoseSearchFields = HasAnyField(Args, GetPoseSearchInferenceFields());
		const bool bHasIKRigFields = HasAnyField(Args, GetIKRigInferenceFields());

		int32 ModeSignals = 0;
		ModeSignals += bHasIKRetargeterFields ? 1 : 0;
		ModeSignals += bHasPoseSearchFields ? 1 : 0;
		ModeSignals += bHasIKRigFields ? 1 : 0;
		if (ModeSignals > 1)
		{
			return FToolResult::Fail(TEXT("Ambiguous request: contains operations for multiple motion tool modes. Set motion_type explicitly."));
		}

		if (bHasIKRetargeterFields)
		{
			MotionType = TEXT("ikretargeter");
		}
		else if (bHasPoseSearchFields)
		{
			FString AssetType;
			Args->TryGetStringField(TEXT("asset_type"), AssetType);
			AssetType = AssetType.ToLower();
			if (AssetType == TEXT("schema") || AssetType == TEXT("database") || AssetType == TEXT("normalization_set") || AssetType.IsEmpty())
			{
				MotionType = TEXT("pose_search");
			}
		}
		else if (bHasIKRigFields)
		{
			MotionType = TEXT("ikrig");
		}
	}

	if (MotionType.IsEmpty())
	{
		// Fallback to asset class detection when only target asset is provided.
		FString Name;
		if (Args->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			FString Path;
			Args->TryGetStringField(TEXT("path"), Path);
			const FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);

			if (UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath))
			{
				const FString ClassName = Asset->GetClass() ? Asset->GetClass()->GetName() : FString();
				if (ClassName.Equals(TEXT("IKRigDefinition"), ESearchCase::IgnoreCase))
				{
					MotionType = TEXT("ikrig");
				}
				else if (ClassName.Equals(TEXT("IKRetargeter"), ESearchCase::IgnoreCase))
				{
					MotionType = TEXT("ikretargeter");
				}
				else if (ClassName.Equals(TEXT("PoseSearchSchema"), ESearchCase::IgnoreCase) ||
						 ClassName.Equals(TEXT("PoseSearchDatabase"), ESearchCase::IgnoreCase) ||
						 ClassName.Equals(TEXT("PoseSearchNormalizationSet"), ESearchCase::IgnoreCase))
				{
					MotionType = TEXT("pose_search");
				}
			}
		}
	}

	if (MotionType == TEXT("ikrig") || MotionType == TEXT("ik_rig"))
	{
		FEditIKRigTool Tool;
		return Tool.Execute(Args);
	}

	if (MotionType == TEXT("ikretargeter") || MotionType == TEXT("ik_retargeter"))
	{
		FEditIKRetargeterTool Tool;
		return Tool.Execute(Args);
	}

	if (MotionType == TEXT("pose_search") || MotionType == TEXT("pose"))
	{
#if WITH_POSE_SEARCH
		FEditPoseSearchTool Tool;
		return Tool.Execute(Args);
#else
		return FToolResult::Fail(TEXT("Pose Search support is disabled in this build."));
#endif
	}

	if (MotionType.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Unable to infer motion_type. Set motion_type to 'ikrig', 'ikretargeter', or 'pose_search'."));
	}

	return FToolResult::Fail(TEXT("Invalid motion_type. Use 'ikrig', 'ikretargeter', or 'pose_search'."));
}
