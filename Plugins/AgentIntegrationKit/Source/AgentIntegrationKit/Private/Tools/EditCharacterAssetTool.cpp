// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditCharacterAssetTool.h"
#include "Tools/EditSkeletonTool.h"
#include "Tools/EditPhysicsAssetTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"
#include <initializer_list>

namespace
{
	static const TArray<FString>& GetSkeletonInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("add_sockets"),
			TEXT("remove_sockets"),
			TEXT("edit_sockets"),
			TEXT("add_virtual_bones"),
			TEXT("remove_virtual_bones"),
			TEXT("rename_virtual_bone"),
			TEXT("set_retargeting_mode"),
			TEXT("add_slots"),
			TEXT("add_slot_groups"),
			TEXT("set_slot_group"),
			TEXT("remove_slots"),
			TEXT("remove_slot_groups"),
			TEXT("rename_slot"),
			TEXT("add_curves"),
			TEXT("remove_curves"),
			TEXT("rename_curve"),
			TEXT("set_curve_flags"),
			TEXT("add_blend_profiles"),
			TEXT("remove_blend_profiles"),
			TEXT("rename_blend_profile"),
			TEXT("set_blend_profile_bones"),
			TEXT("remove_blend_profile_bones"),
			TEXT("add_notify_names"),
			TEXT("remove_notify_names"),
			TEXT("rename_notify_name"),
			TEXT("add_marker_names"),
			TEXT("remove_marker_names"),
			TEXT("rename_marker_name"),
			TEXT("add_compatible_skeletons"),
			TEXT("remove_compatible_skeletons")
		};
		return Fields;
	}

	static const TArray<FString>& GetPhysicsAssetInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("generate"),
			TEXT("add_bodies"),
			TEXT("remove_bodies"),
			TEXT("set_physics_type"),
			TEXT("add_shapes"),
			TEXT("remove_shapes"),
			TEXT("edit_shapes"),
			TEXT("add_constraints"),
			TEXT("remove_constraints"),
			TEXT("edit_constraints"),
			TEXT("disable_collision"),
			TEXT("enable_collision"),
			TEXT("weld_bodies")
		};
		return Fields;
	}

	static bool CharacterAssetToolHasAnyField(const TSharedPtr<FJsonObject>& Args, const TArray<FString>& Fields)
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

	static void CharacterAssetToolMergeSchemaProperties(TSharedPtr<FJsonObject> TargetSchema, const TSharedPtr<FJsonObject>& SourceSchema)
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

	static bool CharacterAssetToolIsClassOrSuperNamed(const UObject* Asset, std::initializer_list<const TCHAR*> Names)
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

TSharedPtr<FJsonObject> FEditCharacterAssetTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> TypeProp = MakeShared<FJsonObject>();
	TypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeProp->SetStringField(TEXT("description"), TEXT("Character asset mode: 'skeleton' or 'physics_asset'."));
	TArray<TSharedPtr<FJsonValue>> TypeEnum;
	TypeEnum.Add(MakeShared<FJsonValueString>(TEXT("skeleton")));
	TypeEnum.Add(MakeShared<FJsonValueString>(TEXT("physics_asset")));
	TypeProp->SetArrayField(TEXT("enum"), TypeEnum);
	Properties->SetObjectField(TEXT("character_asset_type"), TypeProp);

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Character asset name or path."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path."));
	Properties->SetObjectField(TEXT("path"), PathProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	FEditSkeletonTool SkeletonTool;
	FEditPhysicsAssetTool PhysicsTool;
	CharacterAssetToolMergeSchemaProperties(Schema, SkeletonTool.GetInputSchema());
	CharacterAssetToolMergeSchemaProperties(Schema, PhysicsTool.GetInputSchema());

	Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
	return Schema;
}

FToolResult FEditCharacterAssetTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString AssetType;
	Args->TryGetStringField(TEXT("character_asset_type"), AssetType);
	AssetType = AssetType.ToLower();

	const bool bHasSkeletonFields = CharacterAssetToolHasAnyField(Args, GetSkeletonInferenceFields());
	const bool bHasPhysicsFields = CharacterAssetToolHasAnyField(Args, GetPhysicsAssetInferenceFields());

	if (AssetType.IsEmpty() && bHasSkeletonFields && bHasPhysicsFields)
	{
		return FToolResult::Fail(TEXT("Ambiguous request: contains both skeleton and physics-asset operations. Set character_asset_type explicitly."));
	}

	if (AssetType.IsEmpty())
	{
		if (bHasSkeletonFields)
		{
			AssetType = TEXT("skeleton");
		}
		else if (bHasPhysicsFields)
		{
			AssetType = TEXT("physics_asset");
		}
	}

	if (AssetType.IsEmpty())
	{
		FString Name;
		if (Args->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
		{
			FString Path;
			Args->TryGetStringField(TEXT("path"), Path);
			const FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
			if (UObject* Asset = LoadObject<UObject>(nullptr, *FullAssetPath))
			{
				if (CharacterAssetToolIsClassOrSuperNamed(Asset, { TEXT("Skeleton") }))
				{
					AssetType = TEXT("skeleton");
				}
				else if (CharacterAssetToolIsClassOrSuperNamed(Asset, { TEXT("PhysicsAsset") }))
				{
					AssetType = TEXT("physics_asset");
				}
			}
		}
	}

	if (AssetType == TEXT("skeleton"))
	{
		FEditSkeletonTool Tool;
		return Tool.Execute(Args);
	}

	if (AssetType == TEXT("physics_asset") || AssetType == TEXT("physicsasset"))
	{
		FEditPhysicsAssetTool Tool;
		return Tool.Execute(Args);
	}

	if (AssetType.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Unable to infer character_asset_type. Set character_asset_type to 'skeleton' or 'physics_asset'."));
	}

	return FToolResult::Fail(TEXT("Invalid character_asset_type. Use 'skeleton' or 'physics_asset'."));
}
