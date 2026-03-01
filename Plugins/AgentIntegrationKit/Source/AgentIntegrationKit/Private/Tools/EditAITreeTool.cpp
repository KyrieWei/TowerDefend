// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditAITreeTool.h"
#include "Tools/EditBehaviorTreeTool.h"
#include "Tools/EditStateTreeTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "StateTree.h"

namespace
{
	static const TArray<FString>& GetBehaviorTreeInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("add_key"),
			TEXT("remove_key"),
			TEXT("reorder_children")
		};
		return Fields;
	}

	static const TArray<FString>& GetStateTreeInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("create"),
			TEXT("schema"),
			TEXT("list_types"),
			TEXT("add_state"),
			TEXT("remove_state"),
			TEXT("add_task"),
			TEXT("remove_task"),
			TEXT("add_evaluator"),
			TEXT("remove_evaluator"),
			TEXT("add_global_task"),
			TEXT("remove_global_task"),
			TEXT("add_transition"),
			TEXT("remove_transition"),
			TEXT("add_enter_condition"),
			TEXT("remove_enter_condition"),
			TEXT("add_transition_condition"),
			TEXT("remove_transition_condition"),
			TEXT("add_consideration"),
			TEXT("remove_consideration"),
			TEXT("add_property_binding"),
			TEXT("remove_property_binding"),
			TEXT("add_parameter"),
			TEXT("remove_parameter"),
			TEXT("set_properties"),
			TEXT("set_schema")
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

	static FString InferTreeTypeFromFields(const TSharedPtr<FJsonObject>& Args)
	{
		if (HasAnyField(Args, GetBehaviorTreeInferenceFields()))
		{
			return TEXT("behavior_tree");
		}
		if (HasAnyField(Args, GetStateTreeInferenceFields()))
		{
			return TEXT("state_tree");
		}
		return TEXT("");
	}
}

TSharedPtr<FJsonObject> FEditAITreeTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> TreeTypeProp = MakeShared<FJsonObject>();
	TreeTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TreeTypeProp->SetStringField(TEXT("description"), TEXT("AI tree type to edit: 'behavior_tree' or 'state_tree'."));
	TArray<TSharedPtr<FJsonValue>> TreeTypeEnum;
	TreeTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("behavior_tree")));
	TreeTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("state_tree")));
	TreeTypeProp->SetArrayField(TEXT("enum"), TreeTypeEnum);
	Properties->SetObjectField(TEXT("tree_type"), TreeTypeProp);

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("BehaviorTree/Blackboard/StateTree asset name or path."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path."));
	Properties->SetObjectField(TEXT("path"), PathProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	// Merge full parameter surfaces from both delegated tools for parity/discoverability.
	FEditBehaviorTreeTool BehaviorTool;
	FEditStateTreeTool StateTool;
	MergeSchemaProperties(Schema, BehaviorTool.GetInputSchema());
	MergeSchemaProperties(Schema, StateTool.GetInputSchema());

	// Unified tool: requirements vary by operation. Avoid hard-required fields globally.
	Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});

	return Schema;
}

FToolResult FEditAITreeTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString TreeType;
	Args->TryGetStringField(TEXT("tree_type"), TreeType);
	TreeType = TreeType.ToLower();

	const bool bHasBehaviorFields = HasAnyField(Args, GetBehaviorTreeInferenceFields());
	const bool bHasStateFields = HasAnyField(Args, GetStateTreeInferenceFields());

	if (TreeType.IsEmpty() && bHasBehaviorFields && bHasStateFields)
	{
		return FToolResult::Fail(TEXT("Ambiguous request: contains both BehaviorTree and StateTree operations. Set tree_type explicitly."));
	}

	if (TreeType.IsEmpty())
	{
		TreeType = InferTreeTypeFromFields(Args);
	}

	if (TreeType.IsEmpty())
	{
		// Fallback to asset type detection when an asset is provided.
		FString Name;
		if (Args->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			FString Path;
			Args->TryGetStringField(TEXT("path"), Path);
			if (Path.IsEmpty())
			{
				Path = TEXT("/Game");
			}

			const FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
			UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath);
			if (Cast<UBehaviorTree>(Asset) || Cast<UBlackboardData>(Asset))
			{
				TreeType = TEXT("behavior_tree");
			}
			else if (Cast<UStateTree>(Asset))
			{
				TreeType = TEXT("state_tree");
			}
		}
	}

	if (TreeType == TEXT("behavior_tree") || TreeType == TEXT("behavior") || TreeType == TEXT("bt"))
	{
		FEditBehaviorTreeTool Tool;
		return Tool.Execute(Args);
	}

	if (TreeType == TEXT("state_tree") || TreeType == TEXT("state") || TreeType == TEXT("st"))
	{
		FEditStateTreeTool Tool;
		return Tool.Execute(Args);
	}

	return FToolResult::Fail(TEXT("Unable to infer tree_type. Set tree_type to 'behavior_tree' or 'state_tree'."));
}
