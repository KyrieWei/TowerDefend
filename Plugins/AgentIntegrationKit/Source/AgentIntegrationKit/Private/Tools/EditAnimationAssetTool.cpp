// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditAnimationAssetTool.h"
#include "Tools/EditMontageTool.h"
#include "Tools/EditAnimSequenceTool.h"
#include "Tools/EditBlendSpaceTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "UObject/UObjectGlobals.h"
#include <initializer_list>

namespace
{
	static const TArray<FString>& GetMontageInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("add_slot_track"),
			TEXT("remove_slot_track"),
			TEXT("add_anim_segment"),
			TEXT("remove_anim_segment"),
			TEXT("add_section"),
			TEXT("remove_section"),
			TEXT("link_sections"),
			TEXT("add_notify"),
			TEXT("remove_notify")
		};
		return Fields;
	}

	static const TArray<FString>& GetAnimSequenceInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("create"),
			TEXT("add_notifies"),
			TEXT("remove_notifies"),
			TEXT("edit_notifies"),
			TEXT("add_sync_markers"),
			TEXT("remove_sync_markers"),
			TEXT("rename_sync_marker"),
			TEXT("add_curves"),
			TEXT("remove_curves"),
			TEXT("set_curve_keys"),
			TEXT("set_transform_curve_keys"),
			TEXT("rename_curve"),
			TEXT("scale_curve"),
			TEXT("set_curve_color"),
			TEXT("set_frame_rate"),
			TEXT("set_number_of_frames"),
			TEXT("resize"),
			TEXT("add_bone_tracks"),
			TEXT("remove_bone_tracks"),
			TEXT("set_bone_track_keys"),
			TEXT("update_bone_track_keys")
		};
		return Fields;
	}

	static const TArray<FString>& GetBlendSpaceInferenceFields()
	{
		static const TArray<FString> Fields = {
			TEXT("set_axis"),
			TEXT("add_samples"),
			TEXT("remove_samples"),
			TEXT("edit_samples"),
			TEXT("duplicate_sample"),
			TEXT("per_bone_blend_mode"),
			TEXT("set_per_bone_overrides"),
			TEXT("remove_per_bone_overrides"),
			TEXT("set_blend_profile")
		};
		return Fields;
	}

	static bool AnimationAssetToolHasAnyField(const TSharedPtr<FJsonObject>& Args, const TArray<FString>& Fields)
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

	static void AnimationAssetToolMergeSchemaProperties(TSharedPtr<FJsonObject> TargetSchema, const TSharedPtr<FJsonObject>& SourceSchema)
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

	static bool AnimationAssetToolIsClassOrSuperNamed(const UObject* Asset, std::initializer_list<const TCHAR*> Names)
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

TSharedPtr<FJsonObject> FEditAnimationAssetTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> TypeProp = MakeShared<FJsonObject>();
	TypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeProp->SetStringField(TEXT("description"), TEXT("Animation asset mode: 'montage', 'anim_sequence', or 'blend_space'."));
	TArray<TSharedPtr<FJsonValue>> TypeEnum;
	TypeEnum.Add(MakeShared<FJsonValueString>(TEXT("montage")));
	TypeEnum.Add(MakeShared<FJsonValueString>(TEXT("anim_sequence")));
	TypeEnum.Add(MakeShared<FJsonValueString>(TEXT("blend_space")));
	TypeProp->SetArrayField(TEXT("enum"), TypeEnum);
	Properties->SetObjectField(TEXT("animation_asset_type"), TypeProp);

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Animation asset name or path."));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path."));
	Properties->SetObjectField(TEXT("path"), PathProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	FEditMontageTool MontageTool;
	FEditAnimSequenceTool AnimSequenceTool;
	FEditBlendSpaceTool BlendSpaceTool;
	AnimationAssetToolMergeSchemaProperties(Schema, MontageTool.GetInputSchema());
	AnimationAssetToolMergeSchemaProperties(Schema, AnimSequenceTool.GetInputSchema());
	AnimationAssetToolMergeSchemaProperties(Schema, BlendSpaceTool.GetInputSchema());

	Schema->SetArrayField(TEXT("required"), TArray<TSharedPtr<FJsonValue>>{});
	return Schema;
}

FToolResult FEditAnimationAssetTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString AssetType;
	Args->TryGetStringField(TEXT("animation_asset_type"), AssetType);
	AssetType = AssetType.ToLower();

	const bool bHasMontageFields = AnimationAssetToolHasAnyField(Args, GetMontageInferenceFields());
	const bool bHasAnimSequenceFields = AnimationAssetToolHasAnyField(Args, GetAnimSequenceInferenceFields());
	const bool bHasBlendSpaceFields = AnimationAssetToolHasAnyField(Args, GetBlendSpaceInferenceFields());

	if (AssetType.IsEmpty())
	{
		int32 ModeSignals = 0;
		ModeSignals += bHasMontageFields ? 1 : 0;
		ModeSignals += bHasAnimSequenceFields ? 1 : 0;
		ModeSignals += bHasBlendSpaceFields ? 1 : 0;
		if (ModeSignals > 1)
		{
			return FToolResult::Fail(TEXT("Ambiguous request: contains operations for multiple animation asset modes. Set animation_asset_type explicitly."));
		}

		if (bHasMontageFields)
		{
			AssetType = TEXT("montage");
		}
		else if (bHasAnimSequenceFields)
		{
			AssetType = TEXT("anim_sequence");
		}
		else if (bHasBlendSpaceFields)
		{
			AssetType = TEXT("blend_space");
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
				if (AnimationAssetToolIsClassOrSuperNamed(Asset, { TEXT("AnimMontage") }))
				{
					AssetType = TEXT("montage");
				}
				else if (AnimationAssetToolIsClassOrSuperNamed(Asset, {
					TEXT("BlendSpace"),
					TEXT("BlendSpace1D"),
					TEXT("AimOffsetBlendSpace"),
					TEXT("AimOffsetBlendSpace1D")
				}))
				{
					AssetType = TEXT("blend_space");
				}
				else if (AnimationAssetToolIsClassOrSuperNamed(Asset, { TEXT("AnimSequence") }))
				{
					AssetType = TEXT("anim_sequence");
				}
			}
		}
	}

	if (AssetType == TEXT("montage") || AssetType == TEXT("anim_montage"))
	{
		FEditMontageTool Tool;
		return Tool.Execute(Args);
	}

	if (AssetType == TEXT("anim_sequence") || AssetType == TEXT("sequence") || AssetType == TEXT("animsequence"))
	{
		FEditAnimSequenceTool Tool;
		return Tool.Execute(Args);
	}

	if (AssetType == TEXT("blend_space") || AssetType == TEXT("blendspace") || AssetType == TEXT("aim_offset") || AssetType == TEXT("aimoffset"))
	{
		FEditBlendSpaceTool Tool;
		return Tool.Execute(Args);
	}

	if (AssetType.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Unable to infer animation_asset_type. Set animation_asset_type to 'montage', 'anim_sequence', or 'blend_space'."));
	}

	return FToolResult::Fail(TEXT("Invalid animation_asset_type. Use 'montage', 'anim_sequence', or 'blend_space'."));
}
