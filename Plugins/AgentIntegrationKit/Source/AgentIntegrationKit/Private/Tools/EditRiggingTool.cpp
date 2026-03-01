// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditRiggingTool.h"
#include "Tools/EditMotionTool.h"
#include "Tools/EditControlRigTool.h"
#include "Tools/EditGraphTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"
#include <initializer_list>

namespace
{
	static const TArray<FString>& GetMotionInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("motion_type"),
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
			TEXT("snap_to_ground"),
			TEXT("asset_type"),
			TEXT("skeleton"),
			TEXT("mirror_data_table"),
			TEXT("add_channels"),
			TEXT("add_animations"),
			TEXT("remove_animations"),
			TEXT("add_databases"),
			TEXT("remove_databases"),
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

	static const TArray<FString>& GetControlRigInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("add_elements"),
			TEXT("remove_elements"),
				TEXT("rename_elements"),
				TEXT("reparent_elements"),
				TEXT("add_parents"),
				TEXT("remove_parents"),
				TEXT("clear_parents"),
				TEXT("add_components"),
				TEXT("remove_components"),
				TEXT("rename_components"),
				TEXT("reparent_components"),
				TEXT("set_element_settings"),
			TEXT("import_hierarchy"),
			TEXT("duplicate_elements"),
			TEXT("mirror_elements"),
			TEXT("reorder_elements"),
			TEXT("set_spaces"),
			TEXT("set_metadata"),
			TEXT("remove_metadata"),
			TEXT("select_elements"),
			TEXT("deselect_elements"),
			TEXT("set_selection"),
			TEXT("clear_selection"),
			TEXT("add_variables"),
			TEXT("remove_variables"),
			TEXT("set_variable_defaults"),
			TEXT("list_hierarchy"),
			TEXT("list_selection"),
			TEXT("list_metadata"),
			TEXT("list_all_metadata"),
			TEXT("list_variables"),
			TEXT("list_unit_types")
		};
		return Fields;
	}

	static const TArray<FString>& GetControlRigGraphFields()
	{
		static const TArray<FString> Fields = {
			TEXT("graph"),
			TEXT("graph_name"),
			TEXT("operation"),
			TEXT("query"),
			TEXT("add_nodes"),
			TEXT("remove_nodes"),
			TEXT("delete_nodes"),
			TEXT("add_links"),
			TEXT("break_links"),
			TEXT("set_pin_defaults"),
			TEXT("connections"),
			TEXT("disconnect"),
			TEXT("set_pins"),
			TEXT("move_nodes"),
			TEXT("align_nodes"),
			TEXT("layout_nodes"),
			TEXT("add_comments"),
			TEXT("add_array_pins"),
			TEXT("insert_array_pins"),
			TEXT("remove_array_pins"),
			TEXT("bind_pin_variables"),
			TEXT("promote_pins"),
			TEXT("set_pin_expansion"),
			TEXT("add_exposed_pins"),
			TEXT("remove_exposed_pins"),
			TEXT("rename_exposed_pins"),
			TEXT("change_exposed_pin_types"),
			TEXT("reorder_exposed_pins"),
			TEXT("set_node_categories"),
			TEXT("set_node_keywords"),
			TEXT("set_node_descriptions"),
			TEXT("set_pin_categories"),
			TEXT("list_nodes"),
			TEXT("list_graphs")
		};
		return Fields;
	}

	static bool RiggingToolHasAnyField(const TSharedPtr<FJsonObject>& Args, const TArray<FString>& Fields)
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

	static void RiggingToolMergeSchemaProperties(TSharedPtr<FJsonObject> TargetSchema, const TSharedPtr<FJsonObject>& SourceSchema)
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

	static bool RiggingToolIsClassOrSuperNamed(const UObject* Asset, std::initializer_list<const TCHAR*> Names)
	{
		if (!Asset || !Asset->GetClass())
		{
			return false;
		}

		for (const UClass* Class = Asset->GetClass(); Class; Class = Class->GetSuperClass())
		{
			const FString ClassName = Class->GetName();
			for (const TCHAR* Name : Names)
			{
				if (ClassName.Equals(Name, ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		return false;
	}
}

TSharedPtr<FJsonObject> FEditRiggingTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> RiggingTypeProp = MakeShared<FJsonObject>();
	RiggingTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	RiggingTypeProp->SetStringField(TEXT("description"), TEXT("Rigging mode: 'motion' or 'control_rig'."));
	TArray<TSharedPtr<FJsonValue>> RiggingTypeEnum;
	RiggingTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("motion")));
	RiggingTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("control_rig")));
	RiggingTypeProp->SetArrayField(TEXT("enum"), RiggingTypeEnum);
	Properties->SetObjectField(TEXT("rigging_type"), RiggingTypeProp);

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Rigging asset name or path."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path."));
	Properties->SetObjectField(TEXT("path"), PathProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	FEditMotionTool MotionTool;
	FEditControlRigTool ControlRigTool;
	RiggingToolMergeSchemaProperties(Schema, MotionTool.GetInputSchema());
	RiggingToolMergeSchemaProperties(Schema, ControlRigTool.GetInputSchema());

	Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
	return Schema;
}

FToolResult FEditRiggingTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString RiggingType;
	Args->TryGetStringField(TEXT("rigging_type"), RiggingType);
	RiggingType = RiggingType.ToLower();

	const bool bHasMotionFields = RiggingToolHasAnyField(Args, GetMotionInferenceFields());
	const bool bHasControlRigFields = RiggingToolHasAnyField(Args, GetControlRigInferenceFields());

	if (RiggingType.IsEmpty() && bHasMotionFields && bHasControlRigFields)
	{
		return FToolResult::Fail(TEXT("Ambiguous request: contains both motion and control-rig operations. Set rigging_type explicitly."));
	}

	if (RiggingType.IsEmpty())
	{
		if (bHasMotionFields)
		{
			RiggingType = TEXT("motion");
		}
		else if (bHasControlRigFields)
		{
			RiggingType = TEXT("control_rig");
		}
	}

	if (RiggingType.IsEmpty())
	{
		FString Name;
		if (Args->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			FString Path;
			Args->TryGetStringField(TEXT("path"), Path);
			const FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
			if (UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath))
			{
				if (RiggingToolIsClassOrSuperNamed(Asset, { TEXT("ControlRigBlueprint") }))
				{
					RiggingType = TEXT("control_rig");
				}
				else if (RiggingToolIsClassOrSuperNamed(Asset, {
					TEXT("IKRigDefinition"),
					TEXT("IKRetargeter"),
					TEXT("PoseSearchSchema"),
					TEXT("PoseSearchDatabase"),
					TEXT("PoseSearchNormalizationSet")
				}))
				{
					RiggingType = TEXT("motion");
				}
			}
		}
	}

	if (RiggingType == TEXT("motion") || RiggingType == TEXT("motion_stack"))
	{
		FEditMotionTool Tool;
		return Tool.Execute(Args);
	}

	if (RiggingType == TEXT("control_rig") || RiggingType == TEXT("controlrig") || RiggingType == TEXT("rig"))
	{
		TArray<FString> GraphFieldsPresent;
		for (const FString& Field : GetControlRigGraphFields())
		{
			if (Args->HasField(Field))
			{
				GraphFieldsPresent.Add(Field);
			}
		}
		if (GraphFieldsPresent.Num() > 0)
		{
			TSharedPtr<FJsonObject> GraphArgs = MakeShared<FJsonObject>();
			GraphArgs->Values = Args->Values;

			// edit_graph expects "asset"; edit_rigging typically receives "name".
			if (!GraphArgs->HasField(TEXT("asset")))
			{
				FString Name;
				if (GraphArgs->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
				{
					GraphArgs->SetStringField(TEXT("asset"), Name);
				}
			}

			FString AssetName;
			if (!GraphArgs->TryGetStringField(TEXT("asset"), AssetName) || AssetName.IsEmpty())
			{
				return FToolResult::Fail(TEXT("Control Rig graph operations require name (or asset) to resolve the target Control Rig."));
			}

			FEditGraphTool Tool;
			return Tool.Execute(GraphArgs);
		}

		FEditControlRigTool Tool;
		return Tool.Execute(Args);
	}

	if (RiggingType.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Unable to infer rigging_type. Set rigging_type to 'motion' or 'control_rig'."));
	}

	return FToolResult::Fail(TEXT("Invalid rigging_type. Use 'motion' or 'control_rig'."));
}
