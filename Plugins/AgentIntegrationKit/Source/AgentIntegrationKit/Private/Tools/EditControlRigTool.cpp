// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditControlRigTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// Control Rig includes
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Rigs/RigHierarchyDefines.h"
#include "Rigs/RigHierarchyElements.h"
#include "Rigs/RigHierarchyMetadata.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
#include "Rigs/RigHierarchyComponents.h"
#endif
#include "Units/RigUnit.h"
#include "RigVMFunctions/Math/RigVMMathLibrary.h"

#include "RigVMTypeUtils.h"
#include "RigVMCore/RigVMRegistry.h"

// Asset tools
#include "Kismet2/BlueprintEditorUtils.h"

// ============================================================================
// Helpers (file-local)
// ============================================================================

namespace
{

ERigElementType ParseElementType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("bone"), ESearchCase::IgnoreCase)) return ERigElementType::Bone;
	if (TypeStr.Equals(TEXT("control"), ESearchCase::IgnoreCase)) return ERigElementType::Control;
	if (TypeStr.Equals(TEXT("null"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("space"), ESearchCase::IgnoreCase))
		return ERigElementType::Null;
	if (TypeStr.Equals(TEXT("curve"), ESearchCase::IgnoreCase)) return ERigElementType::Curve;
	if (TypeStr.Equals(TEXT("connector"), ESearchCase::IgnoreCase)) return ERigElementType::Connector;
	if (TypeStr.Equals(TEXT("socket"), ESearchCase::IgnoreCase)) return ERigElementType::Socket;
	return ERigElementType::None;
}

ERigControlType ParseControlType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("Float"), ESearchCase::IgnoreCase)) return ERigControlType::Float;
	if (TypeStr.Equals(TEXT("Integer"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Int"), ESearchCase::IgnoreCase))
		return ERigControlType::Integer;
	if (TypeStr.Equals(TEXT("Vector2D"), ESearchCase::IgnoreCase)) return ERigControlType::Vector2D;
	if (TypeStr.Equals(TEXT("Position"), ESearchCase::IgnoreCase)) return ERigControlType::Position;
	if (TypeStr.Equals(TEXT("Scale"), ESearchCase::IgnoreCase)) return ERigControlType::Scale;
	if (TypeStr.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase)) return ERigControlType::Rotator;
	if (TypeStr.Equals(TEXT("EulerTransform"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Transform"), ESearchCase::IgnoreCase))
		return ERigControlType::EulerTransform;
	if (TypeStr.Equals(TEXT("Bool"), ESearchCase::IgnoreCase)) return ERigControlType::Bool;
	if (TypeStr.Equals(TEXT("ScaleFloat"), ESearchCase::IgnoreCase)) return ERigControlType::ScaleFloat;
	return ERigControlType::EulerTransform; // default
}

ERigControlAnimationType ParseAnimationType(const FString& TypeStr)
{
	if (TypeStr.Equals(TEXT("AnimationControl"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Control"), ESearchCase::IgnoreCase))
		return ERigControlAnimationType::AnimationControl;
	if (TypeStr.Equals(TEXT("AnimationChannel"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Channel"), ESearchCase::IgnoreCase))
		return ERigControlAnimationType::AnimationChannel;
	if (TypeStr.Equals(TEXT("ProxyControl"), ESearchCase::IgnoreCase) || TypeStr.Equals(TEXT("Proxy"), ESearchCase::IgnoreCase))
		return ERigControlAnimationType::ProxyControl;
	if (TypeStr.Equals(TEXT("VisualCue"), ESearchCase::IgnoreCase))
		return ERigControlAnimationType::VisualCue;
	return ERigControlAnimationType::AnimationControl; // default
}

FString ElementTypeToString(ERigElementType Type)
{
	switch (Type)
	{
	case ERigElementType::Bone: return TEXT("Bone");
	case ERigElementType::Control: return TEXT("Control");
	case ERigElementType::Null: return TEXT("Null");
	case ERigElementType::Curve: return TEXT("Curve");
	case ERigElementType::Connector: return TEXT("Connector");
	case ERigElementType::Socket: return TEXT("Socket");
	case ERigElementType::Reference: return TEXT("Reference");
	default: return TEXT("Unknown");
	}
}

FString ControlTypeToString(ERigControlType Type)
{
	switch (Type)
	{
	case ERigControlType::Bool: return TEXT("Bool");
	case ERigControlType::Float: return TEXT("Float");
	case ERigControlType::Integer: return TEXT("Integer");
	case ERigControlType::Vector2D: return TEXT("Vector2D");
	case ERigControlType::Position: return TEXT("Position");
	case ERigControlType::Scale: return TEXT("Scale");
	case ERigControlType::Rotator: return TEXT("Rotator");
	case ERigControlType::EulerTransform: return TEXT("EulerTransform");
	case ERigControlType::ScaleFloat: return TEXT("ScaleFloat");
	default: return TEXT("Unknown");
	}
}

FString AnimTypeToString(ERigControlAnimationType Type)
{
	switch (Type)
	{
	case ERigControlAnimationType::AnimationControl: return TEXT("AnimationControl");
	case ERigControlAnimationType::AnimationChannel: return TEXT("AnimationChannel");
	case ERigControlAnimationType::ProxyControl: return TEXT("ProxyControl");
	case ERigControlAnimationType::VisualCue: return TEXT("VisualCue");
	default: return TEXT("Unknown");
	}
}

FLinearColor ParseColor(const FString& ColorStr)
{
	// Parse "(R=1,G=0,B=0,A=1)" format
	FLinearColor Color = FLinearColor::White;
	FString Trimmed = ColorStr;
	Trimmed.ReplaceInline(TEXT("("), TEXT(""), ESearchCase::CaseSensitive);
	Trimmed.ReplaceInline(TEXT(")"), TEXT(""), ESearchCase::CaseSensitive);
	TArray<FString> Parts;
	Trimmed.ParseIntoArray(Parts, TEXT(","));
	for (const FString& Part : Parts)
	{
		FString Key, Value;
		if (Part.Split(TEXT("="), &Key, &Value))
		{
			Key.TrimStartAndEndInline();
			float Val = FCString::Atof(*Value);
			if (Key.Equals(TEXT("R"), ESearchCase::IgnoreCase)) Color.R = Val;
			else if (Key.Equals(TEXT("G"), ESearchCase::IgnoreCase)) Color.G = Val;
			else if (Key.Equals(TEXT("B"), ESearchCase::IgnoreCase)) Color.B = Val;
			else if (Key.Equals(TEXT("A"), ESearchCase::IgnoreCase)) Color.A = Val;
		}
	}
	return Color;
}

EAxis::Type ParseAxis(const FString& AxisStr)
{
	if (AxisStr.Equals(TEXT("X"), ESearchCase::IgnoreCase)) return EAxis::X;
	if (AxisStr.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) return EAxis::Y;
	if (AxisStr.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) return EAxis::Z;
	return EAxis::X;
}

ERigControlAxis ParseControlAxis(const FString& AxisStr)
{
	if (AxisStr.Equals(TEXT("X"), ESearchCase::IgnoreCase)) return ERigControlAxis::X;
	if (AxisStr.Equals(TEXT("Y"), ESearchCase::IgnoreCase)) return ERigControlAxis::Y;
	if (AxisStr.Equals(TEXT("Z"), ESearchCase::IgnoreCase)) return ERigControlAxis::Z;
	return ERigControlAxis::X;
}

ERigControlVisibility ParseShapeVisibility(const FString& VisibilityStr)
{
	if (VisibilityStr.Equals(TEXT("UserDefined"), ESearchCase::IgnoreCase) || VisibilityStr.Equals(TEXT("User"), ESearchCase::IgnoreCase))
	{
		return ERigControlVisibility::UserDefined;
	}
	if (VisibilityStr.Equals(TEXT("BasedOnSelection"), ESearchCase::IgnoreCase) || VisibilityStr.Equals(TEXT("Selection"), ESearchCase::IgnoreCase))
	{
		return ERigControlVisibility::BasedOnSelection;
	}
	return ERigControlVisibility::UserDefined;
}

ERigControlTransformChannel ParseTransformChannel(const FString& ChannelStr, bool& bOutValid)
{
	bOutValid = true;
	if (ChannelStr.Equals(TEXT("TranslationX"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("tx"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::TranslationX;
	if (ChannelStr.Equals(TEXT("TranslationY"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("ty"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::TranslationY;
	if (ChannelStr.Equals(TEXT("TranslationZ"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("tz"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::TranslationZ;
	if (ChannelStr.Equals(TEXT("Pitch"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("rx"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::Pitch;
	if (ChannelStr.Equals(TEXT("Yaw"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("ry"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::Yaw;
	if (ChannelStr.Equals(TEXT("Roll"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("rz"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::Roll;
	if (ChannelStr.Equals(TEXT("ScaleX"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("sx"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::ScaleX;
	if (ChannelStr.Equals(TEXT("ScaleY"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("sy"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::ScaleY;
	if (ChannelStr.Equals(TEXT("ScaleZ"), ESearchCase::IgnoreCase) || ChannelStr.Equals(TEXT("sz"), ESearchCase::IgnoreCase)) return ERigControlTransformChannel::ScaleZ;
	bOutValid = false;
	return ERigControlTransformChannel::TranslationX;
}

bool ParseEulerRotationOrder(const FString& RotationOrderStr, EEulerRotationOrder& OutOrder)
{
	if (RotationOrderStr.Equals(TEXT("XYZ"), ESearchCase::IgnoreCase)) { OutOrder = EEulerRotationOrder::XYZ; return true; }
	if (RotationOrderStr.Equals(TEXT("XZY"), ESearchCase::IgnoreCase)) { OutOrder = EEulerRotationOrder::XZY; return true; }
	if (RotationOrderStr.Equals(TEXT("YXZ"), ESearchCase::IgnoreCase)) { OutOrder = EEulerRotationOrder::YXZ; return true; }
	if (RotationOrderStr.Equals(TEXT("YZX"), ESearchCase::IgnoreCase)) { OutOrder = EEulerRotationOrder::YZX; return true; }
	if (RotationOrderStr.Equals(TEXT("ZXY"), ESearchCase::IgnoreCase)) { OutOrder = EEulerRotationOrder::ZXY; return true; }
	if (RotationOrderStr.Equals(TEXT("ZYX"), ESearchCase::IgnoreCase)) { OutOrder = EEulerRotationOrder::ZYX; return true; }
	return false;
}

FRigElementKey FindElementKeyInHierarchy(URigHierarchy* Hierarchy, const FString& Name)
{
	if (!Hierarchy || Name.IsEmpty())
	{
		return FRigElementKey();
	}

	static const ERigElementType Types[] = {
		ERigElementType::Bone, ERigElementType::Control, ERigElementType::Null,
		ERigElementType::Curve, ERigElementType::Connector, ERigElementType::Socket
	};
	for (ERigElementType Type : Types)
	{
		FRigElementKey Key(FName(*Name), Type);
		if (Hierarchy->Contains(Key))
		{
			return Key;
		}
	}
	return FRigElementKey();
}

bool TryParseTransform(const TSharedPtr<FJsonObject>& Settings, const TCHAR* FieldName, FTransform& OutTransform)
{
	if (!Settings.IsValid())
	{
		return false;
	}

	FString TransformString;
	if (Settings->TryGetStringField(FieldName, TransformString) && !TransformString.IsEmpty())
	{
		FTransform Parsed = FTransform::Identity;
		if (Parsed.InitFromString(TransformString))
		{
			OutTransform = Parsed;
			return true;
		}
	}

	return false;
}

bool TryGetControlValueFromString(const FString& InValue, ERigControlType ControlType, FRigControlValue& OutValue)
{
	if (InValue.IsEmpty())
	{
		return false;
	}

	switch (ControlType)
	{
	case ERigControlType::Bool:
		OutValue = URigHierarchy::MakeControlValueFromBool(InValue.Equals(TEXT("true"), ESearchCase::IgnoreCase) || InValue == TEXT("1"));
		return true;
	case ERigControlType::Float:
	case ERigControlType::ScaleFloat:
		OutValue = URigHierarchy::MakeControlValueFromFloat(FCString::Atof(*InValue));
		return true;
	case ERigControlType::Integer:
		OutValue = URigHierarchy::MakeControlValueFromInt(FCString::Atoi(*InValue));
		return true;
	case ERigControlType::Vector2D:
	{
		FVector2D V2 = FVector2D::ZeroVector;
		if (!V2.InitFromString(InValue))
		{
			FVector V3 = FVector::ZeroVector;
			V3.InitFromString(InValue);
			V2 = FVector2D(V3.X, V3.Y);
		}
		OutValue = URigHierarchy::MakeControlValueFromVector2D(V2);
		return true;
	}
	case ERigControlType::Position:
	case ERigControlType::Scale:
	{
		FVector Value = FVector::ZeroVector;
		Value.InitFromString(InValue);
		OutValue = URigHierarchy::MakeControlValueFromVector(Value);
		return true;
	}
	case ERigControlType::Rotator:
	{
		FRotator Value = FRotator::ZeroRotator;
		Value.InitFromString(InValue);
		OutValue = URigHierarchy::MakeControlValueFromRotator(Value);
		return true;
	}
	case ERigControlType::EulerTransform:
	{
		FTransform Value = FTransform::Identity;
		Value.InitFromString(InValue);
		OutValue = URigHierarchy::MakeControlValueFromEulerTransform(FEulerTransform(Value));
		return true;
	}
	default:
	{
		FTransform Value = FTransform::Identity;
		Value.InitFromString(InValue);
		OutValue = URigHierarchy::MakeControlValueFromTransform(Value);
		return true;
	}
	}
}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
UScriptStruct* ResolveRigComponentStruct(const FString& ComponentType)
{
	if (ComponentType.IsEmpty())
	{
		return nullptr;
	}

	if (ComponentType.StartsWith(TEXT("/Script/")) || ComponentType.Contains(TEXT(".")))
	{
		UScriptStruct* StructByPath = LoadObject<UScriptStruct>(nullptr, *ComponentType);
		if (StructByPath && StructByPath->IsChildOf(FRigBaseComponent::StaticStruct()))
		{
			return StructByPath;
		}
	}

	const FString QueryLower = ComponentType.ToLower();
	const FString QueryNoPrefix = QueryLower.StartsWith(TEXT("f")) ? QueryLower.Mid(1) : QueryLower;
	const TArray<UScriptStruct*> ComponentStructs = FRigBaseComponent::GetAllComponentScriptStructs(false);
	for (UScriptStruct* Struct : ComponentStructs)
	{
		if (!Struct)
		{
			continue;
		}

		const FString StructNameLower = Struct->GetName().ToLower();
		const FString StructNameNoPrefix = StructNameLower.StartsWith(TEXT("f")) ? StructNameLower.Mid(1) : StructNameLower;
		if (StructNameLower == QueryLower || StructNameNoPrefix == QueryNoPrefix || Struct->GetPathName().Equals(ComponentType, ESearchCase::IgnoreCase))
		{
			return Struct;
		}
	}

	return nullptr;
}
#endif

FRigControlSettings ParseControlSettings(const TSharedPtr<FJsonObject>& Json, ERigControlType DefaultType)
{
	FRigControlSettings Settings;
	Settings.ControlType = DefaultType;
	Settings.AnimationType = ERigControlAnimationType::AnimationControl;
	Settings.bShapeVisible = true;
	Settings.ShapeColor = FLinearColor::Red;

	if (!Json.IsValid())
	{
		return Settings;
	}

	FString CtrlType;
	if (Json->TryGetStringField(TEXT("control_type"), CtrlType))
	{
		Settings.ControlType = ParseControlType(CtrlType);
	}

	FString AnimType;
	if (Json->TryGetStringField(TEXT("anim_type"), AnimType))
	{
		Settings.AnimationType = ParseAnimationType(AnimType);
	}

	FString DisplayName;
	if (Json->TryGetStringField(TEXT("display_name"), DisplayName))
	{
		Settings.DisplayName = FName(*DisplayName);
	}

	FString ShapeName;
	if (Json->TryGetStringField(TEXT("shape_name"), ShapeName))
	{
		Settings.ShapeName = FName(*ShapeName);
	}

	FString ShapeColor;
	if (Json->TryGetStringField(TEXT("shape_color"), ShapeColor))
	{
		Settings.ShapeColor = ParseColor(ShapeColor);
	}

	bool bShapeVisible;
	if (Json->TryGetBoolField(TEXT("shape_visible"), bShapeVisible))
	{
		Settings.bShapeVisible = bShapeVisible;
	}

	bool bIsTransient;
	if (Json->TryGetBoolField(TEXT("is_transient"), bIsTransient))
	{
		Settings.bIsTransientControl = bIsTransient;
	}

	bool bGroupWithParent;
	if (Json->TryGetBoolField(TEXT("group_with_parent"), bGroupWithParent))
	{
		Settings.bGroupWithParentControl = bGroupWithParent;
	}

	bool bDrawLimits;
	if (Json->TryGetBoolField(TEXT("draw_limits"), bDrawLimits))
	{
		Settings.bDrawLimits = bDrawLimits;
	}

	FString PrimaryAxis;
	if (Json->TryGetStringField(TEXT("primary_axis"), PrimaryAxis))
	{
		Settings.PrimaryAxis = ParseControlAxis(PrimaryAxis);
	}

	FString ShapeVisibility;
	if (Json->TryGetStringField(TEXT("shape_visibility"), ShapeVisibility))
	{
		Settings.ShapeVisibility = ParseShapeVisibility(ShapeVisibility);
	}

	bool bRestrictSpaceSwitching = false;
	if (Json->TryGetBoolField(TEXT("restrict_space_switching"), bRestrictSpaceSwitching))
	{
		Settings.bRestrictSpaceSwitching = bRestrictSpaceSwitching;
	}

	bool bUsePreferredRotationOrder = false;
	if (Json->TryGetBoolField(TEXT("use_preferred_rotation_order"), bUsePreferredRotationOrder))
	{
		Settings.bUsePreferredRotationOrder = bUsePreferredRotationOrder;
	}

	FString RotationOrder;
	if (Json->TryGetStringField(TEXT("preferred_rotation_order"), RotationOrder))
	{
		EEulerRotationOrder ParsedOrder = EEulerRotationOrder::ZYX;
		if (ParseEulerRotationOrder(RotationOrder, ParsedOrder))
		{
			Settings.PreferredRotationOrder = ParsedOrder;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* FilteredChannels = nullptr;
	if (Json->TryGetArrayField(TEXT("filtered_channels"), FilteredChannels))
	{
		Settings.FilteredChannels.Reset();
		for (const TSharedPtr<FJsonValue>& ChannelVal : *FilteredChannels)
		{
			FString ChannelName;
			if (ChannelVal.IsValid() && ChannelVal->TryGetString(ChannelName))
			{
				bool bValidChannel = false;
				ERigControlTransformChannel Channel = ParseTransformChannel(ChannelName, bValidChannel);
				if (bValidChannel)
				{
					Settings.FilteredChannels.Add(Channel);
				}
			}
		}
	}

	return Settings;
}

} // anonymous namespace

// ============================================================================
// Asset Resolution
// ============================================================================

UControlRigBlueprint* FEditControlRigTool::LoadControlRigBP(const FString& Name, const FString& Path, FString& OutError)
{
	FString AssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UControlRigBlueprint* BP = NeoStackToolUtils::LoadAssetWithFallback<UControlRigBlueprint>(AssetPath);
	if (!BP)
	{
		OutError = FString::Printf(TEXT("Control Rig not found: %s"), *AssetPath);
	}
	return BP;
}

// ============================================================================
// GetInputSchema
// ============================================================================

TSharedPtr<FJsonObject> FEditControlRigTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// --- Basic Parameters ---
	auto MakeProp = [](const FString& Type, const FString& Desc) -> TSharedPtr<FJsonObject>
	{
		TSharedPtr<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("type"), Type);
		P->SetStringField(TEXT("description"), Desc);
		return P;
	};

	Properties->SetObjectField(TEXT("name"), MakeProp(TEXT("string"), TEXT("Control Rig asset name or path")));
	Properties->SetObjectField(TEXT("path"), MakeProp(TEXT("string"), TEXT("Asset folder path (defaults to /Game)")));

	// --- Hierarchy Operations ---
	Properties->SetObjectField(TEXT("add_elements"), MakeProp(TEXT("array"),
		TEXT("Elements to add: [{type (bone/control/null/curve/connector/socket), name, parent?, settings?}]. "
			"For controls, settings: {control_type (Float/Integer/Vector2D/Position/Scale/Rotator/EulerTransform/Bool), "
			"anim_type (AnimationControl/AnimationChannel/ProxyControl/VisualCue), display_name, shape_name, "
			"shape_color \"(R=1,G=0,B=0,A=1)\", shape_visible, draw_limits, group_with_parent}")));

	Properties->SetObjectField(TEXT("remove_elements"), MakeProp(TEXT("array"), TEXT("Element names to remove")));
	Properties->SetObjectField(TEXT("rename_elements"), MakeProp(TEXT("array"), TEXT("Rename: [{name, new_name}]")));
	Properties->SetObjectField(TEXT("reparent_elements"), MakeProp(TEXT("array"), TEXT("Reparent: [{name, new_parent}]. Empty new_parent = root.")));
	Properties->SetObjectField(TEXT("add_parents"), MakeProp(TEXT("array"),
		TEXT("Add additional parents (multi-parent): [{child, parent, weight?, maintain_global?, label?}]")));
	Properties->SetObjectField(TEXT("remove_parents"), MakeProp(TEXT("array"),
		TEXT("Remove specific parents: [{child, parent, maintain_global?}]")));
	Properties->SetObjectField(TEXT("clear_parents"), MakeProp(TEXT("array"),
		TEXT("Remove all parents from each child: [{child, maintain_global?}] or [\"ChildName\"]")));
	Properties->SetObjectField(TEXT("add_components"), MakeProp(TEXT("array"),
		TEXT("Add hierarchy components: [{element, name, component_type, content?}]")));
	Properties->SetObjectField(TEXT("remove_components"), MakeProp(TEXT("array"),
		TEXT("Remove components: [{element, name}]")));
	Properties->SetObjectField(TEXT("rename_components"), MakeProp(TEXT("array"),
		TEXT("Rename components: [{element, name, new_name}]")));
	Properties->SetObjectField(TEXT("reparent_components"), MakeProp(TEXT("array"),
		TEXT("Reparent components: [{element, name, new_element}]")));
	Properties->SetObjectField(TEXT("set_element_settings"), MakeProp(TEXT("array"),
		TEXT("Configure control settings: [{name, settings: {control_type, shape_color, shape_visible, ...}}]")));
	Properties->SetObjectField(TEXT("import_hierarchy"), MakeProp(TEXT("object"),
		TEXT("Import bones: {skeleton?: \"path\", skeletal_mesh?: \"path\", include_curves?: bool}")));
	Properties->SetObjectField(TEXT("duplicate_elements"), MakeProp(TEXT("array"), TEXT("Element names to duplicate")));
	Properties->SetObjectField(TEXT("mirror_elements"), MakeProp(TEXT("object"),
		TEXT("{elements: [names], search: \"_L\", replace: \"_R\", mirror_axis?: \"X\", axis_to_flip?: \"Z\"}")));
	Properties->SetObjectField(TEXT("reorder_elements"), MakeProp(TEXT("array"), TEXT("Reorder: [{name, new_index}]")));
	Properties->SetObjectField(TEXT("set_spaces"), MakeProp(TEXT("array"),
		TEXT("Space setup: [{control_name, clear?, active_space?, spaces: [\"element_name\" | {element, op(add/remove)?, label?, index?, active?}]}]")));
	Properties->SetObjectField(TEXT("set_metadata"), MakeProp(TEXT("array"),
		TEXT("Set hierarchy metadata: [{element, name, type(bool/int32/float/name/vector/rotator/transform/color/rig_element_key), value}]")));
	Properties->SetObjectField(TEXT("remove_metadata"), MakeProp(TEXT("array"),
		TEXT("Remove hierarchy metadata: [{element, name}]")));
	Properties->SetObjectField(TEXT("select_elements"), MakeProp(TEXT("array"), TEXT("Element names to select")));
	Properties->SetObjectField(TEXT("deselect_elements"), MakeProp(TEXT("array"), TEXT("Element names to deselect")));
	Properties->SetObjectField(TEXT("set_selection"), MakeProp(TEXT("array"), TEXT("Element names to set as selection")));
	Properties->SetObjectField(TEXT("clear_selection"), MakeProp(TEXT("boolean"), TEXT("Clear hierarchy selection")));

	// --- Variable Operations ---
	Properties->SetObjectField(TEXT("add_variables"), MakeProp(TEXT("array"),
		TEXT("Add variables: [{name, type (e.g. \"float\", \"FVector\", \"FTransform\"), default_value?}]")));
	Properties->SetObjectField(TEXT("remove_variables"), MakeProp(TEXT("array"), TEXT("Variable names to remove")));
	Properties->SetObjectField(TEXT("set_variable_defaults"), MakeProp(TEXT("array"),
		TEXT("Set variable defaults: [{name, value}]")));

	// --- Discovery ---
	Properties->SetObjectField(TEXT("list_hierarchy"), MakeProp(TEXT("boolean"), TEXT("List hierarchy elements")));
	Properties->SetObjectField(TEXT("list_variables"), MakeProp(TEXT("boolean"), TEXT("List member variables")));
	Properties->SetObjectField(TEXT("list_selection"), MakeProp(TEXT("boolean"), TEXT("List selected hierarchy elements")));
	Properties->SetObjectField(TEXT("list_metadata"), MakeProp(TEXT("array"), TEXT("List metadata for specific element names")));
	Properties->SetObjectField(TEXT("list_all_metadata"), MakeProp(TEXT("boolean"), TEXT("List metadata for all hierarchy elements")));
	Properties->SetObjectField(TEXT("list_unit_types"), MakeProp(TEXT("string"),
		TEXT("Search available RigUnit types. Pass filter string (e.g. \"IK\", \"Transform\") or empty for all.")));

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ============================================================================
// Execute
// ============================================================================

FToolResult FEditControlRigTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}
	Args->TryGetStringField(TEXT("path"), Path);

	static const TCHAR* RemovedGraphFields[] = {
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
	TArray<FString> PresentRemovedFields;
	for (const TCHAR* Field : RemovedGraphFields)
	{
		if (Args->HasField(Field))
		{
			PresentRemovedFields.Add(Field);
		}
	}
	if (PresentRemovedFields.Num() > 0)
	{
		return FToolResult::Fail(
			FString::Printf(
				TEXT("Graph operations were removed from edit_control_rig. Use edit_graph for Control Rig graph operations. Unsupported fields: %s"),
				*FString::Join(PresentRemovedFields, TEXT(", "))));
	}

	// --- Load asset ---
	FString LoadError;
	UControlRigBlueprint* BP = LoadControlRigBP(Name, Path, LoadError);
	if (!BP)
	{
		return FToolResult::Fail(LoadError);
	}

	// --- Get hierarchy subsystem ---
	URigHierarchy* Hierarchy = BP->GetHierarchy();
	if (!Hierarchy)
	{
		return FToolResult::Fail(TEXT("Control Rig has no hierarchy"));
	}

	URigHierarchyController* HierCtrl = Hierarchy->GetController(true);
	if (!HierCtrl)
	{
		return FToolResult::Fail(TEXT("Failed to get hierarchy controller"));
	}

	TArray<FString> Results;
	int32 TotalChanges = 0;

	// ===================== HIERARCHY OPERATIONS =====================

	// --- Import Hierarchy ---
	const TSharedPtr<FJsonObject>* ImportConfig;
	if (Args->TryGetObjectField(TEXT("import_hierarchy"), ImportConfig))
	{
		TotalChanges += ImportHierarchy(HierCtrl, *ImportConfig, Results);
	}

	// --- Add Elements ---
	const TArray<TSharedPtr<FJsonValue>>* AddElementsArray;
	if (Args->TryGetArrayField(TEXT("add_elements"), AddElementsArray))
	{
		TArray<FElementToAdd> Elements;
		for (const auto& Val : *AddElementsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FElementToAdd Elem;
				(*Obj)->TryGetStringField(TEXT("type"), Elem.Type);
				(*Obj)->TryGetStringField(TEXT("name"), Elem.Name);
				(*Obj)->TryGetStringField(TEXT("parent"), Elem.Parent);
				const TSharedPtr<FJsonObject>* SettingsObj;
				if ((*Obj)->TryGetObjectField(TEXT("settings"), SettingsObj))
				{
					Elem.Settings = *SettingsObj;
				}
				Elements.Add(MoveTemp(Elem));
			}
		}
		TotalChanges += AddElements(HierCtrl, Hierarchy, Elements, Results);
	}

	// --- Remove Elements ---
	const TArray<TSharedPtr<FJsonValue>>* RemoveElementsArray;
	if (Args->TryGetArrayField(TEXT("remove_elements"), RemoveElementsArray))
	{
		TArray<FString> Names;
		for (const auto& Val : *RemoveElementsArray)
		{
			FString N;
			if (Val.IsValid() && Val->TryGetString(N))
			{
				Names.Add(N);
			}
		}
		TotalChanges += RemoveElements(HierCtrl, Hierarchy, Names, Results);
	}

	// --- Rename Elements ---
	const TArray<TSharedPtr<FJsonValue>>* RenameArray;
	if (Args->TryGetArrayField(TEXT("rename_elements"), RenameArray))
	{
		TArray<FElementRename> Renames;
		for (const auto& Val : *RenameArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FElementRename R;
				(*Obj)->TryGetStringField(TEXT("name"), R.Name);
				(*Obj)->TryGetStringField(TEXT("new_name"), R.NewName);
				Renames.Add(MoveTemp(R));
			}
		}
		TotalChanges += RenameElements(HierCtrl, Hierarchy, Renames, Results);
	}

	// --- Reparent Elements ---
	const TArray<TSharedPtr<FJsonValue>>* ReparentArray;
	if (Args->TryGetArrayField(TEXT("reparent_elements"), ReparentArray))
	{
		TArray<FElementReparent> Reparents;
		for (const auto& Val : *ReparentArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FElementReparent R;
				(*Obj)->TryGetStringField(TEXT("name"), R.Name);
				(*Obj)->TryGetStringField(TEXT("new_parent"), R.NewParent);
				Reparents.Add(MoveTemp(R));
			}
		}
		TotalChanges += ReparentElements(HierCtrl, Hierarchy, Reparents, Results);
	}

	// --- Add Parents (multi-parent) ---
	const TArray<TSharedPtr<FJsonValue>>* AddParentsArray;
	if (Args->TryGetArrayField(TEXT("add_parents"), AddParentsArray))
	{
		TArray<FParentAdd> Ops;
		for (const auto& Val : *AddParentsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FParentAdd Op;
				(*Obj)->TryGetStringField(TEXT("child"), Op.Child);
				(*Obj)->TryGetStringField(TEXT("parent"), Op.Parent);
				(*Obj)->TryGetStringField(TEXT("label"), Op.Label);
				double Weight = 0.0;
				if ((*Obj)->TryGetNumberField(TEXT("weight"), Weight))
				{
					Op.Weight = static_cast<float>(Weight);
				}
				(*Obj)->TryGetBoolField(TEXT("maintain_global"), Op.bMaintainGlobal);
				Ops.Add(MoveTemp(Op));
			}
		}
		TotalChanges += AddParents(HierCtrl, Hierarchy, Ops, Results);
	}

	// --- Remove Parents ---
	const TArray<TSharedPtr<FJsonValue>>* RemoveParentsArray;
	if (Args->TryGetArrayField(TEXT("remove_parents"), RemoveParentsArray))
	{
		TArray<FParentRemove> Ops;
		for (const auto& Val : *RemoveParentsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FParentRemove Op;
				(*Obj)->TryGetStringField(TEXT("child"), Op.Child);
				(*Obj)->TryGetStringField(TEXT("parent"), Op.Parent);
				(*Obj)->TryGetBoolField(TEXT("maintain_global"), Op.bMaintainGlobal);
				Ops.Add(MoveTemp(Op));
			}
		}
		TotalChanges += RemoveParents(HierCtrl, Hierarchy, Ops, Results);
	}

	// --- Clear Parents ---
	const TArray<TSharedPtr<FJsonValue>>* ClearParentsArray;
	if (Args->TryGetArrayField(TEXT("clear_parents"), ClearParentsArray))
	{
		TArray<FParentClear> Ops;
		for (const auto& Val : *ClearParentsArray)
		{
			FParentClear Op;
			if (Val.IsValid() && Val->TryGetString(Op.Child))
			{
				Ops.Add(MoveTemp(Op));
				continue;
			}

			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				(*Obj)->TryGetStringField(TEXT("child"), Op.Child);
				(*Obj)->TryGetBoolField(TEXT("maintain_global"), Op.bMaintainGlobal);
				Ops.Add(MoveTemp(Op));
			}
		}
		TotalChanges += ClearParents(HierCtrl, Hierarchy, Ops, Results);
	}

		// --- Components ---
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		const TArray<TSharedPtr<FJsonValue>>* AddComponentsArray;
		if (Args->TryGetArrayField(TEXT("add_components"), AddComponentsArray))
		{
		TArray<FComponentAdd> Ops;
		for (const auto& Val : *AddComponentsArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FComponentAdd Op;
				(*Obj)->TryGetStringField(TEXT("element"), Op.Element);
				(*Obj)->TryGetStringField(TEXT("name"), Op.Name);
				(*Obj)->TryGetStringField(TEXT("component_type"), Op.ComponentType);
				(*Obj)->TryGetStringField(TEXT("content"), Op.Content);
				Ops.Add(MoveTemp(Op));
			}
		}
		TotalChanges += AddComponents(HierCtrl, Hierarchy, Ops, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveComponentsArray;
	if (Args->TryGetArrayField(TEXT("remove_components"), RemoveComponentsArray))
	{
		TArray<FComponentRemove> Ops;
		for (const auto& Val : *RemoveComponentsArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FComponentRemove Op;
				(*Obj)->TryGetStringField(TEXT("element"), Op.Element);
				(*Obj)->TryGetStringField(TEXT("name"), Op.Name);
				Ops.Add(MoveTemp(Op));
			}
		}
		TotalChanges += RemoveComponents(HierCtrl, Hierarchy, Ops, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RenameComponentsArray;
	if (Args->TryGetArrayField(TEXT("rename_components"), RenameComponentsArray))
	{
		TArray<FComponentRename> Ops;
		for (const auto& Val : *RenameComponentsArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FComponentRename Op;
				(*Obj)->TryGetStringField(TEXT("element"), Op.Element);
				(*Obj)->TryGetStringField(TEXT("name"), Op.Name);
				(*Obj)->TryGetStringField(TEXT("new_name"), Op.NewName);
				Ops.Add(MoveTemp(Op));
			}
		}
		TotalChanges += RenameComponents(HierCtrl, Hierarchy, Ops, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* ReparentComponentsArray;
	if (Args->TryGetArrayField(TEXT("reparent_components"), ReparentComponentsArray))
	{
		TArray<FComponentReparent> Ops;
		for (const auto& Val : *ReparentComponentsArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FComponentReparent Op;
				(*Obj)->TryGetStringField(TEXT("element"), Op.Element);
				(*Obj)->TryGetStringField(TEXT("name"), Op.Name);
				(*Obj)->TryGetStringField(TEXT("new_element"), Op.NewElement);
				Ops.Add(MoveTemp(Op));
			}
		}
			TotalChanges += ReparentComponents(HierCtrl, Hierarchy, Ops, Results);
		}
#else
		if (Args->HasField(TEXT("add_components")) ||
			Args->HasField(TEXT("remove_components")) ||
			Args->HasField(TEXT("rename_components")) ||
			Args->HasField(TEXT("reparent_components")))
		{
			Results.Add(TEXT("component operations require Unreal Engine 5.6 or later"));
		}
#endif

		// --- Reorder Elements ---
	const TArray<TSharedPtr<FJsonValue>>* ReorderArray;
	if (Args->TryGetArrayField(TEXT("reorder_elements"), ReorderArray))
	{
		TArray<FElementReorder> Reorders;
		for (const auto& Val : *ReorderArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FElementReorder R;
				(*Obj)->TryGetStringField(TEXT("name"), R.Name);
				R.NewIndex = 0;
				double IdxDbl = 0;
				if ((*Obj)->TryGetNumberField(TEXT("new_index"), IdxDbl))
				{
					R.NewIndex = static_cast<int32>(IdxDbl);
				}
				Reorders.Add(MoveTemp(R));
			}
		}
		TotalChanges += ReorderElements(HierCtrl, Hierarchy, Reorders, Results);
	}

	// --- Set Element Settings ---
	const TArray<TSharedPtr<FJsonValue>>* SettingsArray;
	if (Args->TryGetArrayField(TEXT("set_element_settings"), SettingsArray))
	{
		TArray<FElementSettings> SettingsList;
		for (const auto& Val : *SettingsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FElementSettings S;
				(*Obj)->TryGetStringField(TEXT("name"), S.Name);
				const TSharedPtr<FJsonObject>* SettObj;
				if ((*Obj)->TryGetObjectField(TEXT("settings"), SettObj))
				{
					S.Settings = *SettObj;
				}
				SettingsList.Add(MoveTemp(S));
			}
		}
		TotalChanges += SetElementSettings(HierCtrl, Hierarchy, SettingsList, Results);
	}

	// --- Duplicate Elements ---
	const TArray<TSharedPtr<FJsonValue>>* DuplicateArray;
	if (Args->TryGetArrayField(TEXT("duplicate_elements"), DuplicateArray))
	{
		TArray<FString> Names;
		for (const auto& Val : *DuplicateArray)
		{
			FString N;
			if (Val.IsValid() && Val->TryGetString(N))
			{
				Names.Add(N);
			}
		}
		TotalChanges += DuplicateElements(HierCtrl, Hierarchy, Names, Results);
	}

	// --- Mirror Elements ---
	const TSharedPtr<FJsonObject>* MirrorObj;
	if (Args->TryGetObjectField(TEXT("mirror_elements"), MirrorObj))
	{
		FMirrorConfig Config;
		const TArray<TSharedPtr<FJsonValue>>* ElemsArray;
		if ((*MirrorObj)->TryGetArrayField(TEXT("elements"), ElemsArray))
		{
			for (const auto& Val : *ElemsArray)
			{
				FString N;
				if (Val.IsValid() && Val->TryGetString(N))
				{
					Config.Elements.Add(N);
				}
			}
		}
		(*MirrorObj)->TryGetStringField(TEXT("search"), Config.Search);
		(*MirrorObj)->TryGetStringField(TEXT("replace"), Config.Replace);
		(*MirrorObj)->TryGetStringField(TEXT("mirror_axis"), Config.MirrorAxis);
		(*MirrorObj)->TryGetStringField(TEXT("axis_to_flip"), Config.AxisToFlip);
		TotalChanges += MirrorElements(HierCtrl, Hierarchy, Config, Results);
	}

	// --- Set Spaces ---
	const TArray<TSharedPtr<FJsonValue>>* SpacesArray;
	if (Args->TryGetArrayField(TEXT("set_spaces"), SpacesArray))
	{
		TArray<FSpaceConfig> Configs;
		for (const auto& Val : *SpacesArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FSpaceConfig SC;
				(*Obj)->TryGetStringField(TEXT("control_name"), SC.ControlName);
				(*Obj)->TryGetStringField(TEXT("active_space"), SC.ActiveSpace);
				(*Obj)->TryGetBoolField(TEXT("clear"), SC.bClear);
				const TArray<TSharedPtr<FJsonValue>>* SpacesList;
				if ((*Obj)->TryGetArrayField(TEXT("spaces"), SpacesList))
				{
					for (const auto& SpaceVal : *SpacesList)
					{
						if (SpaceVal.IsValid() && SpaceVal->Type == EJson::String)
						{
							FString SpaceName;
							if (SpaceVal->TryGetString(SpaceName))
							{
								FSpaceEntry Entry;
								Entry.Element = SpaceName;
								Entry.Op = TEXT("add");
								SC.Spaces.Add(MoveTemp(Entry));
							}
						}
						else
						{
							const TSharedPtr<FJsonObject>* SpaceObj = nullptr;
							if (SpaceVal.IsValid() && SpaceVal->TryGetObject(SpaceObj) && SpaceObj && SpaceObj->IsValid())
							{
								FSpaceEntry Entry;
								(*SpaceObj)->TryGetStringField(TEXT("element"), Entry.Element);
								(*SpaceObj)->TryGetStringField(TEXT("op"), Entry.Op);
								(*SpaceObj)->TryGetStringField(TEXT("label"), Entry.Label);
								double SpaceIndex = 0.0;
								if ((*SpaceObj)->TryGetNumberField(TEXT("index"), SpaceIndex))
								{
									Entry.Index = static_cast<int32>(SpaceIndex);
									Entry.bHasIndex = true;
								}
								(*SpaceObj)->TryGetBoolField(TEXT("active"), Entry.bActive);
								if (Entry.Op.IsEmpty())
								{
									Entry.Op = TEXT("add");
								}
								SC.Spaces.Add(MoveTemp(Entry));
							}
						}
					}
				}
				Configs.Add(MoveTemp(SC));
			}
		}
		TotalChanges += SetSpaces(HierCtrl, Hierarchy, Configs, Results);
	}

	// --- Metadata ---
	const TArray<TSharedPtr<FJsonValue>>* SetMetadataArray;
	if (Args->TryGetArrayField(TEXT("set_metadata"), SetMetadataArray))
	{
		TArray<FMetadataSet> MetadataOps;
		for (const auto& Val : *SetMetadataArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FMetadataSet Op;
				(*Obj)->TryGetStringField(TEXT("element"), Op.Element);
				(*Obj)->TryGetStringField(TEXT("name"), Op.Name);
				(*Obj)->TryGetStringField(TEXT("type"), Op.Type);
				(*Obj)->TryGetStringField(TEXT("value"), Op.Value);
				MetadataOps.Add(MoveTemp(Op));
			}
		}
		TotalChanges += SetMetadata(Hierarchy, MetadataOps, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveMetadataArray;
	if (Args->TryGetArrayField(TEXT("remove_metadata"), RemoveMetadataArray))
	{
		TArray<FMetadataRemove> MetadataOps;
		for (const auto& Val : *RemoveMetadataArray)
		{
			const TSharedPtr<FJsonObject>* Obj = nullptr;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj && Obj->IsValid())
			{
				FMetadataRemove Op;
				(*Obj)->TryGetStringField(TEXT("element"), Op.Element);
				(*Obj)->TryGetStringField(TEXT("name"), Op.Name);
				MetadataOps.Add(MoveTemp(Op));
			}
		}
		TotalChanges += RemoveMetadata(Hierarchy, MetadataOps, Results);
	}

	// --- Selection ---
	const TArray<TSharedPtr<FJsonValue>>* SelectArray;
	if (Args->TryGetArrayField(TEXT("select_elements"), SelectArray))
	{
		TArray<FString> Names;
		for (const auto& Val : *SelectArray)
		{
			FString N;
			if (Val.IsValid() && Val->TryGetString(N))
			{
				Names.Add(N);
			}
		}
		TotalChanges += SelectElements(HierCtrl, Hierarchy, Names, true, false, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* DeselectArray;
	if (Args->TryGetArrayField(TEXT("deselect_elements"), DeselectArray))
	{
		TArray<FString> Names;
		for (const auto& Val : *DeselectArray)
		{
			FString N;
			if (Val.IsValid() && Val->TryGetString(N))
			{
				Names.Add(N);
			}
		}
		TotalChanges += SelectElements(HierCtrl, Hierarchy, Names, false, false, Results);
	}

	const TArray<TSharedPtr<FJsonValue>>* SetSelectionArray;
	if (Args->TryGetArrayField(TEXT("set_selection"), SetSelectionArray))
	{
		TArray<FString> Names;
		for (const auto& Val : *SetSelectionArray)
		{
			FString N;
			if (Val.IsValid() && Val->TryGetString(N))
			{
				Names.Add(N);
			}
		}
		TotalChanges += SetSelection(HierCtrl, Hierarchy, Names, Results);
	}

	bool bClearSelection = false;
	if (Args->TryGetBoolField(TEXT("clear_selection"), bClearSelection) && bClearSelection)
	{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		if (HierCtrl->ClearSelection(false))
#else
		if (HierCtrl->ClearSelection())
#endif
		{
			Results.Add(TEXT("Cleared hierarchy selection"));
			TotalChanges += 1;
		}
		else
		{
			Results.Add(TEXT("clear_selection: No selection change"));
		}
	}

	// ===================== VARIABLE OPERATIONS (member variables) =====================

	// --- Add Variables ---
	const TArray<TSharedPtr<FJsonValue>>* AddVarsArray;
	if (Args->TryGetArrayField(TEXT("add_variables"), AddVarsArray))
	{
		TArray<FVariableToAdd> Vars;
		for (const auto& Val : *AddVarsArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FVariableToAdd V;
				(*Obj)->TryGetStringField(TEXT("name"), V.Name);
				(*Obj)->TryGetStringField(TEXT("type"), V.Type);
				(*Obj)->TryGetStringField(TEXT("default_value"), V.DefaultValue);
				Vars.Add(MoveTemp(V));
			}
		}
		TotalChanges += AddVariables(BP, Vars, Results);
	}

	// --- Remove Variables ---
	const TArray<TSharedPtr<FJsonValue>>* RemoveVarsArray;
	if (Args->TryGetArrayField(TEXT("remove_variables"), RemoveVarsArray))
	{
		TArray<FString> Names;
		for (const auto& Val : *RemoveVarsArray)
		{
			FString N;
			if (Val.IsValid() && Val->TryGetString(N))
			{
				Names.Add(N);
			}
		}
		TotalChanges += RemoveVariables(BP, Names, Results);
	}

	// --- Set Variable Defaults ---
	const TArray<TSharedPtr<FJsonValue>>* SetVarDefArray;
	if (Args->TryGetArrayField(TEXT("set_variable_defaults"), SetVarDefArray))
	{
		TArray<FVariableDefault> Defaults;
		for (const auto& Val : *SetVarDefArray)
		{
			const TSharedPtr<FJsonObject>* Obj;
			if (Val.IsValid() && Val->TryGetObject(Obj) && Obj->IsValid())
			{
				FVariableDefault VD;
				(*Obj)->TryGetStringField(TEXT("name"), VD.Name);
				(*Obj)->TryGetStringField(TEXT("value"), VD.Value);
				Defaults.Add(MoveTemp(VD));
			}
		}
		TotalChanges += SetVariableDefaults(BP, Defaults, Results);
	}

	// ===================== DISCOVERY OPERATIONS =====================

	bool bListHierarchy = false;
	if (Args->TryGetBoolField(TEXT("list_hierarchy"), bListHierarchy) && bListHierarchy)
	{
		Results.Add(ListHierarchy(Hierarchy));
	}


	bool bListVariables = false;
	if (Args->TryGetBoolField(TEXT("list_variables"), bListVariables) && bListVariables)
	{
		Results.Add(ListVariables(BP));
	}

	bool bListSelection = false;
	if (Args->TryGetBoolField(TEXT("list_selection"), bListSelection) && bListSelection)
	{
		Results.Add(ListSelection(Hierarchy));
	}

	bool bListAllMetadata = false;
	Args->TryGetBoolField(TEXT("list_all_metadata"), bListAllMetadata);
	const TArray<TSharedPtr<FJsonValue>>* ListMetadataArray = nullptr;
	const bool bHasListMetadataArray = Args->TryGetArrayField(TEXT("list_metadata"), ListMetadataArray);
	if (bListAllMetadata || bHasListMetadataArray)
	{
		TArray<FString> Names;
		if (bHasListMetadataArray && ListMetadataArray)
		{
			for (const TSharedPtr<FJsonValue>& Val : *ListMetadataArray)
			{
				FString N;
				if (Val.IsValid() && Val->TryGetString(N))
				{
					Names.Add(N);
				}
			}
		}
		Results.Add(ListMetadata(Hierarchy, Names));
	}

	FString UnitTypeFilter;
	if (Args->TryGetStringField(TEXT("list_unit_types"), UnitTypeFilter))
	{
		Results.Add(ListUnitTypes(UnitTypeFilter));
	}

	// ===================== FINALIZE =====================

	if (TotalChanges > 0)
	{
		MarkBPModified(BP);
	}

	// Build output
	if (Results.Num() == 0)
	{
		return FToolResult::Ok(TEXT("No operations performed. Use read_asset to see hierarchy/variables, or list_unit_types to discover available RigUnit types."));
	}

	FString Output = FString::Printf(TEXT("Control Rig '%s': %d changes applied.\n\n"), *Name, TotalChanges);
	Output += FString::Join(Results, TEXT("\n"));
	return FToolResult::Ok(Output);
}

// ============================================================================
// Hierarchy Operations
// ============================================================================

int32 FEditControlRigTool::AddElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FElementToAdd>& Elements, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FElementToAdd& Elem : Elements)
	{
		if (Elem.Name.IsEmpty())
		{
			OutResults.Add(TEXT("add_element: Skipped element with empty name"));
			continue;
		}

		ERigElementType ElemType = ParseElementType(Elem.Type);
		if (ElemType == ERigElementType::None)
		{
			OutResults.Add(FString::Printf(TEXT("add_element: Unknown type '%s' for '%s'"), *Elem.Type, *Elem.Name));
			continue;
		}

		FRigElementKey ParentKey;
		if (!Elem.Parent.IsEmpty())
		{
			ParentKey = FindElementKeyInHierarchy(Hierarchy, Elem.Parent);
			if (!Hierarchy->Contains(ParentKey))
			{
				OutResults.Add(FString::Printf(TEXT("add_element: Parent '%s' not found for '%s'"), *Elem.Parent, *Elem.Name));
				continue;
			}
		}

		FTransform ElementTransform = FTransform::Identity;
		if (TryParseTransform(Elem.Settings, TEXT("transform"), ElementTransform) ||
			TryParseTransform(Elem.Settings, TEXT("initial_transform"), ElementTransform))
		{
			// Parsed from settings.
		}

		FRigElementKey NewKey;
		switch (ElemType)
		{
		case ERigElementType::Bone:
			NewKey = Ctrl->AddBone(FName(*Elem.Name), ParentKey, ElementTransform, true, ERigBoneType::User, false);
			break;

		case ERigElementType::Control:
		{
			FRigControlSettings Settings = ParseControlSettings(Elem.Settings, ERigControlType::EulerTransform);
			FRigControlValue Value = Settings.GetIdentityValue();
			FString InitialValueString;
			if (Elem.Settings.IsValid() &&
				((Elem.Settings->TryGetStringField(TEXT("default_value"), InitialValueString) && !InitialValueString.IsEmpty()) ||
				 (Elem.Settings->TryGetStringField(TEXT("value"), InitialValueString) && !InitialValueString.IsEmpty())))
			{
				TryGetControlValueFromString(InitialValueString, Settings.ControlType, Value);
			}

			FTransform OffsetTransform = FTransform::Identity;
			FTransform ShapeTransform = FTransform::Identity;
			TryParseTransform(Elem.Settings, TEXT("offset_transform"), OffsetTransform);
			TryParseTransform(Elem.Settings, TEXT("shape_transform"), ShapeTransform);
			if (OffsetTransform.Equals(FTransform::Identity))
			{
				OffsetTransform = ElementTransform;
			}

			NewKey = Ctrl->AddControl(FName(*Elem.Name), ParentKey, Settings, Value, OffsetTransform, ShapeTransform, false);
			break;
		}

		case ERigElementType::Null:
			NewKey = Ctrl->AddNull(FName(*Elem.Name), ParentKey, ElementTransform, true, false);
			break;

		case ERigElementType::Curve:
		{
			float CurveValue = 0.f;
			if (Elem.Settings.IsValid())
			{
				double CurveValueNumber = 0.0;
				if (Elem.Settings->TryGetNumberField(TEXT("value"), CurveValueNumber))
				{
					CurveValue = static_cast<float>(CurveValueNumber);
				}
				else
				{
					FString CurveValueString;
					if (Elem.Settings->TryGetStringField(TEXT("value"), CurveValueString) && !CurveValueString.IsEmpty())
					{
						CurveValue = FCString::Atof(*CurveValueString);
					}
				}
			}
			NewKey = Ctrl->AddCurve(FName(*Elem.Name), CurveValue, false);
			break;
		}

		case ERigElementType::Connector:
			NewKey = Ctrl->AddConnector(FName(*Elem.Name), FRigConnectorSettings(), false);
			break;

		case ERigElementType::Socket:
			NewKey = Ctrl->AddSocket(FName(*Elem.Name), ParentKey, ElementTransform, true, FLinearColor::White, TEXT(""), false);
			break;

		default:
			break;
		}

		if (NewKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Added %s '%s'"), *ElementTypeToString(ElemType), *NewKey.Name.ToString()));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add %s '%s'"), *ElementTypeToString(ElemType), *Elem.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::RemoveElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FString>& Names, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FString& Name : Names)
	{
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Name);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("remove_element: '%s' not found"), *Name));
			continue;
		}
		if (Ctrl->RemoveElement(Key, false))
		{
			OutResults.Add(FString::Printf(TEXT("Removed '%s'"), *Name));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to remove '%s'"), *Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::RenameElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FElementRename>& Renames, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FElementRename& R : Renames)
	{
		if (R.Name.IsEmpty() || R.NewName.IsEmpty())
		{
			OutResults.Add(TEXT("rename_element: name and new_name required"));
			continue;
		}
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, R.Name);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("rename_element: '%s' not found"), *R.Name));
			continue;
		}
		FRigElementKey NewKey = Ctrl->RenameElement(Key, FName(*R.NewName), false);
		if (NewKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Renamed '%s' -> '%s'"), *R.Name, *NewKey.Name.ToString()));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to rename '%s'"), *R.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::ReparentElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FElementReparent>& Reparents, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FElementReparent& R : Reparents)
	{
		if (R.Name.IsEmpty())
		{
			OutResults.Add(TEXT("reparent_element: name required"));
			continue;
		}
		FRigElementKey ChildKey = FindElementKeyInHierarchy(Hierarchy, R.Name);
		if (!ChildKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("reparent_element: '%s' not found"), *R.Name));
			continue;
		}

		FRigElementKey ParentKey;
		if (!R.NewParent.IsEmpty())
		{
			ParentKey = FindElementKeyInHierarchy(Hierarchy, R.NewParent);
			if (!ParentKey.IsValid())
			{
				OutResults.Add(FString::Printf(TEXT("reparent_element: Parent '%s' not found"), *R.NewParent));
				continue;
			}
		}

		if (Ctrl->SetParent(ChildKey, ParentKey, true, false))
		{
			FString ParentName = R.NewParent.IsEmpty() ? TEXT("<root>") : R.NewParent;
			OutResults.Add(FString::Printf(TEXT("Reparented '%s' -> '%s'"), *R.Name, *ParentName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to reparent '%s'"), *R.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::AddParents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FParentAdd>& Adds, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FParentAdd& Op : Adds)
	{
		if (Op.Child.IsEmpty() || Op.Parent.IsEmpty())
		{
			OutResults.Add(TEXT("add_parents: child and parent are required"));
			continue;
		}

		const FRigElementKey ChildKey = FindElementKeyInHierarchy(Hierarchy, Op.Child);
		const FRigElementKey ParentKey = FindElementKeyInHierarchy(Hierarchy, Op.Parent);
		if (!ChildKey.IsValid() || !ParentKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("add_parents: child='%s' or parent='%s' not found"), *Op.Child, *Op.Parent));
			continue;
		}

	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			const bool bAddedParent = Ctrl->AddParent(
				ChildKey,
				ParentKey,
				Op.Weight,
				Op.bMaintainGlobal,
				Op.Label.IsEmpty() ? NAME_None : FName(*Op.Label),
				false);
	#else
			const bool bAddedParent = Ctrl->AddParent(
				ChildKey,
				ParentKey,
				Op.Weight,
				Op.bMaintainGlobal,
				false);
			if (!Op.Label.IsEmpty())
			{
				OutResults.Add(TEXT("add_parents: labels are only supported in UE 5.6+"));
			}
	#endif
			if (bAddedParent)
			{
				OutResults.Add(FString::Printf(TEXT("Added parent '%s' -> '%s'"), *Op.Parent, *Op.Child));
				++Count;
			}
		else
		{
			OutResults.Add(FString::Printf(TEXT("add_parents: failed for '%s' -> '%s'"), *Op.Parent, *Op.Child));
		}
	}
	return Count;
}

int32 FEditControlRigTool::RemoveParents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FParentRemove>& Removes, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FParentRemove& Op : Removes)
	{
		if (Op.Child.IsEmpty() || Op.Parent.IsEmpty())
		{
			OutResults.Add(TEXT("remove_parents: child and parent are required"));
			continue;
		}

		const FRigElementKey ChildKey = FindElementKeyInHierarchy(Hierarchy, Op.Child);
		const FRigElementKey ParentKey = FindElementKeyInHierarchy(Hierarchy, Op.Parent);
		if (!ChildKey.IsValid() || !ParentKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("remove_parents: child='%s' or parent='%s' not found"), *Op.Child, *Op.Parent));
			continue;
		}

		if (Ctrl->RemoveParent(ChildKey, ParentKey, Op.bMaintainGlobal, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Removed parent '%s' from '%s'"), *Op.Parent, *Op.Child));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("remove_parents: failed for '%s' from '%s'"), *Op.Parent, *Op.Child));
		}
	}
	return Count;
}

int32 FEditControlRigTool::ClearParents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FParentClear>& Clears, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FParentClear& Op : Clears)
	{
		if (Op.Child.IsEmpty())
		{
			OutResults.Add(TEXT("clear_parents: child is required"));
			continue;
		}

		const FRigElementKey ChildKey = FindElementKeyInHierarchy(Hierarchy, Op.Child);
		if (!ChildKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("clear_parents: child '%s' not found"), *Op.Child));
			continue;
		}

		if (Ctrl->RemoveAllParents(ChildKey, Op.bMaintainGlobal, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Cleared all parents for '%s'"), *Op.Child));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("clear_parents: failed for '%s'"), *Op.Child));
		}
	}
	return Count;
}

int32 FEditControlRigTool::AddComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FComponentAdd>& Adds, TArray<FString>& OutResults)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	int32 Count = 0;
	for (const FComponentAdd& Op : Adds)
	{
		if (Op.Element.IsEmpty() || Op.Name.IsEmpty() || Op.ComponentType.IsEmpty())
		{
			OutResults.Add(TEXT("add_components: element, name, and component_type are required"));
			continue;
		}

		const FRigElementKey ElementKey = FindElementKeyInHierarchy(Hierarchy, Op.Element);
		if (!ElementKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("add_components: element '%s' not found"), *Op.Element));
			continue;
		}

		UScriptStruct* ComponentStruct = ResolveRigComponentStruct(Op.ComponentType);
		if (!ComponentStruct)
		{
			OutResults.Add(FString::Printf(TEXT("add_components: component_type '%s' not found"), *Op.ComponentType));
			continue;
		}

		const FRigComponentKey Added = Ctrl->AddComponent(ComponentStruct, FName(*Op.Name), ElementKey, Op.Content, false, false);
		if (Added.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Added component '%s' (%s) on '%s'"), *Added.Name.ToString(), *ComponentStruct->GetName(), *Op.Element));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("add_components: failed for '%s' on '%s'"), *Op.Name, *Op.Element));
		}
	}
	return Count;
#else
	OutResults.Add(TEXT("add_components: requires Unreal Engine 5.6 or later"));
	return 0;
#endif
}

int32 FEditControlRigTool::RemoveComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FComponentRemove>& Removes, TArray<FString>& OutResults)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	int32 Count = 0;
	for (const FComponentRemove& Op : Removes)
	{
		if (Op.Element.IsEmpty() || Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("remove_components: element and name are required"));
			continue;
		}

		const FRigElementKey ElementKey = FindElementKeyInHierarchy(Hierarchy, Op.Element);
		if (!ElementKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("remove_components: element '%s' not found"), *Op.Element));
			continue;
		}

		const FRigComponentKey ComponentKey(ElementKey, FName(*Op.Name));
		if (!Hierarchy->FindComponent(ComponentKey))
		{
			OutResults.Add(FString::Printf(TEXT("remove_components: component '%s' not found on '%s'"), *Op.Name, *Op.Element));
			continue;
		}

		if (Ctrl->RemoveComponent(ComponentKey, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Removed component '%s' from '%s'"), *Op.Name, *Op.Element));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("remove_components: failed for '%s' on '%s'"), *Op.Name, *Op.Element));
		}
	}
	return Count;
#else
	OutResults.Add(TEXT("remove_components: requires Unreal Engine 5.6 or later"));
	return 0;
#endif
}

int32 FEditControlRigTool::RenameComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FComponentRename>& Renames, TArray<FString>& OutResults)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	int32 Count = 0;
	for (const FComponentRename& Op : Renames)
	{
		if (Op.Element.IsEmpty() || Op.Name.IsEmpty() || Op.NewName.IsEmpty())
		{
			OutResults.Add(TEXT("rename_components: element, name, and new_name are required"));
			continue;
		}

		const FRigElementKey ElementKey = FindElementKeyInHierarchy(Hierarchy, Op.Element);
		if (!ElementKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("rename_components: element '%s' not found"), *Op.Element));
			continue;
		}

		const FRigComponentKey ComponentKey(ElementKey, FName(*Op.Name));
		if (!Hierarchy->FindComponent(ComponentKey))
		{
			OutResults.Add(FString::Printf(TEXT("rename_components: component '%s' not found on '%s'"), *Op.Name, *Op.Element));
			continue;
		}

		const FRigComponentKey Renamed = Ctrl->RenameComponent(ComponentKey, FName(*Op.NewName), false, false, true);
		if (Renamed.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Renamed component '%s' -> '%s' on '%s'"), *Op.Name, *Renamed.Name.ToString(), *Op.Element));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("rename_components: failed for '%s' on '%s'"), *Op.Name, *Op.Element));
		}
	}
	return Count;
#else
	OutResults.Add(TEXT("rename_components: requires Unreal Engine 5.6 or later"));
	return 0;
#endif
}

int32 FEditControlRigTool::ReparentComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FComponentReparent>& Reparents, TArray<FString>& OutResults)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	int32 Count = 0;
	for (const FComponentReparent& Op : Reparents)
	{
		if (Op.Element.IsEmpty() || Op.Name.IsEmpty() || Op.NewElement.IsEmpty())
		{
			OutResults.Add(TEXT("reparent_components: element, name, and new_element are required"));
			continue;
		}

		const FRigElementKey ElementKey = FindElementKeyInHierarchy(Hierarchy, Op.Element);
		const FRigElementKey NewElementKey = FindElementKeyInHierarchy(Hierarchy, Op.NewElement);
		if (!ElementKey.IsValid() || !NewElementKey.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("reparent_components: element '%s' or new_element '%s' not found"), *Op.Element, *Op.NewElement));
			continue;
		}

		const FRigComponentKey ComponentKey(ElementKey, FName(*Op.Name));
		if (!Hierarchy->FindComponent(ComponentKey))
		{
			OutResults.Add(FString::Printf(TEXT("reparent_components: component '%s' not found on '%s'"), *Op.Name, *Op.Element));
			continue;
		}

		const FRigComponentKey Reparented = Ctrl->ReparentComponent(ComponentKey, NewElementKey, false, false, true);
		if (Reparented.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("Reparented component '%s' from '%s' -> '%s'"), *Op.Name, *Op.Element, *Op.NewElement));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("reparent_components: failed for '%s'"), *Op.Name));
		}
	}
	return Count;
#else
	OutResults.Add(TEXT("reparent_components: requires Unreal Engine 5.6 or later"));
	return 0;
#endif
}

int32 FEditControlRigTool::SetElementSettings(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FElementSettings>& SettingsList, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FElementSettings& S : SettingsList)
	{
		if (S.Name.IsEmpty() || !S.Settings.IsValid())
		{
			OutResults.Add(TEXT("set_element_settings: name and settings required"));
			continue;
		}

		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, S.Name);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("set_element_settings: '%s' not found"), *S.Name));
			continue;
		}

		if (Key.Type != ERigElementType::Control)
		{
			OutResults.Add(FString::Printf(TEXT("set_element_settings: '%s' is not a Control (is %s)"), *S.Name, *ElementTypeToString(Key.Type)));
			continue;
		}

		// Get current settings, apply changes, set back
		FRigControlSettings Current = Ctrl->GetControlSettings(Key);
		FRigControlSettings Updated = ParseControlSettings(S.Settings, Current.ControlType);

		// Merge — only override fields that are present in JSON
		FString Tmp;
		if (S.Settings->TryGetStringField(TEXT("control_type"), Tmp)) Current.ControlType = Updated.ControlType;
		if (S.Settings->TryGetStringField(TEXT("anim_type"), Tmp)) Current.AnimationType = Updated.AnimationType;
		if (S.Settings->TryGetStringField(TEXT("display_name"), Tmp)) Current.DisplayName = Updated.DisplayName;
		if (S.Settings->TryGetStringField(TEXT("shape_name"), Tmp)) Current.ShapeName = Updated.ShapeName;
		if (S.Settings->TryGetStringField(TEXT("shape_color"), Tmp)) Current.ShapeColor = Updated.ShapeColor;
		bool bTmp;
		if (S.Settings->TryGetBoolField(TEXT("shape_visible"), bTmp)) Current.bShapeVisible = Updated.bShapeVisible;
		if (S.Settings->TryGetBoolField(TEXT("is_transient"), bTmp)) Current.bIsTransientControl = Updated.bIsTransientControl;
		if (S.Settings->TryGetBoolField(TEXT("group_with_parent"), bTmp)) Current.bGroupWithParentControl = Updated.bGroupWithParentControl;
		if (S.Settings->TryGetBoolField(TEXT("draw_limits"), bTmp)) Current.bDrawLimits = Updated.bDrawLimits;
		if (S.Settings->TryGetStringField(TEXT("primary_axis"), Tmp)) Current.PrimaryAxis = Updated.PrimaryAxis;
		if (S.Settings->TryGetStringField(TEXT("shape_visibility"), Tmp)) Current.ShapeVisibility = Updated.ShapeVisibility;
		if (S.Settings->TryGetBoolField(TEXT("restrict_space_switching"), bTmp)) Current.bRestrictSpaceSwitching = Updated.bRestrictSpaceSwitching;
		if (S.Settings->TryGetBoolField(TEXT("use_preferred_rotation_order"), bTmp)) Current.bUsePreferredRotationOrder = Updated.bUsePreferredRotationOrder;
		if (S.Settings->TryGetStringField(TEXT("preferred_rotation_order"), Tmp)) Current.PreferredRotationOrder = Updated.PreferredRotationOrder;
		const TArray<TSharedPtr<FJsonValue>>* TmpArray = nullptr;
		if (S.Settings->TryGetArrayField(TEXT("filtered_channels"), TmpArray)) Current.FilteredChannels = Updated.FilteredChannels;

		const TArray<TSharedPtr<FJsonValue>>* LimitEnabledArray = nullptr;
		if (S.Settings->TryGetArrayField(TEXT("limit_enabled"), LimitEnabledArray))
		{
			Current.LimitEnabled.Reset();
			for (const TSharedPtr<FJsonValue>& LimitVal : *LimitEnabledArray)
			{
				if (!LimitVal.IsValid())
				{
					continue;
				}
				if (LimitVal->Type == EJson::Boolean)
				{
					bool bLimit = false;
					if (LimitVal->TryGetBool(bLimit))
					{
						Current.LimitEnabled.Add(FRigControlLimitEnabled(bLimit));
					}
					continue;
				}

				const TSharedPtr<FJsonObject>* LimitObj = nullptr;
				if (LimitVal->TryGetObject(LimitObj) && LimitObj && LimitObj->IsValid())
				{
					bool bMin = false;
					bool bMax = false;
					(*LimitObj)->TryGetBoolField(TEXT("min"), bMin);
					(*LimitObj)->TryGetBoolField(TEXT("max"), bMax);
					Current.LimitEnabled.Add(FRigControlLimitEnabled(bMin, bMax));
				}
			}
		}

		FString ControlEnumPath;
		if (S.Settings->TryGetStringField(TEXT("control_enum"), ControlEnumPath))
		{
			if (ControlEnumPath.IsEmpty())
			{
				Current.ControlEnum = nullptr;
			}
			else
			{
				UEnum* EnumAsset = LoadObject<UEnum>(nullptr, *ControlEnumPath);
				if (EnumAsset)
				{
					Current.ControlEnum = EnumAsset;
				}
				else
				{
					OutResults.Add(FString::Printf(TEXT("set_element_settings: control_enum '%s' could not be loaded for '%s'"), *ControlEnumPath, *S.Name));
				}
			}
		}

		if (Ctrl->SetControlSettings(Key, Current, false))
		{
			OutResults.Add(FString::Printf(TEXT("Updated settings for '%s'"), *S.Name));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to update settings for '%s'"), *S.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::ImportHierarchy(URigHierarchyController* Ctrl,
	const TSharedPtr<FJsonObject>& Config, TArray<FString>& OutResults)
{
	int32 Count = 0;
	FString SkeletonPath, SkelMeshPath;
	bool bIncludeCurves = false;
	Config->TryGetStringField(TEXT("skeleton"), SkeletonPath);
	Config->TryGetStringField(TEXT("skeletal_mesh"), SkelMeshPath);
	Config->TryGetBoolField(TEXT("include_curves"), bIncludeCurves);

	if (!SkelMeshPath.IsEmpty())
	{
		FString FullPath = NeoStackToolUtils::BuildAssetPath(SkelMeshPath, TEXT(""));
		USkeletalMesh* SkelMesh = NeoStackToolUtils::LoadAssetWithFallback<USkeletalMesh>(FullPath);
		if (!SkelMesh)
		{
			OutResults.Add(FString::Printf(TEXT("import_hierarchy: Skeletal mesh not found: %s"), *FullPath));
			return 0;
		}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		TArray<FRigElementKey> ImportedKeys = Ctrl->ImportBonesFromSkeletalMesh(SkelMesh, NAME_None, true, true, false, false);
#else
		TArray<FRigElementKey> ImportedKeys = Ctrl->ImportBones(SkelMesh->GetRefSkeleton(), NAME_None, true, true, false, false);
#endif
		OutResults.Add(FString::Printf(TEXT("Imported %d bones from skeletal mesh '%s'"), ImportedKeys.Num(), *SkelMeshPath));
		Count += ImportedKeys.Num();

		if (bIncludeCurves)
		{
			TArray<FRigElementKey> CurveKeys = Ctrl->ImportCurvesFromSkeletalMesh(SkelMesh, NAME_None, false, false);
			OutResults.Add(FString::Printf(TEXT("Imported %d curves"), CurveKeys.Num()));
			Count += CurveKeys.Num();
		}
	}
	else if (!SkeletonPath.IsEmpty())
	{
		FString FullPath = NeoStackToolUtils::BuildAssetPath(SkeletonPath, TEXT(""));
		USkeleton* Skeleton = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(FullPath);
		if (!Skeleton)
		{
			OutResults.Add(FString::Printf(TEXT("import_hierarchy: Skeleton not found: %s"), *FullPath));
			return 0;
		}

		TArray<FRigElementKey> ImportedKeys = Ctrl->ImportBones(Skeleton, NAME_None, true, true, false, false);
		OutResults.Add(FString::Printf(TEXT("Imported %d bones from skeleton '%s'"), ImportedKeys.Num(), *SkeletonPath));
		Count += ImportedKeys.Num();

		if (bIncludeCurves)
		{
			TArray<FRigElementKey> CurveKeys = Ctrl->ImportCurves(Skeleton, NAME_None, false, false);
			OutResults.Add(FString::Printf(TEXT("Imported %d curves"), CurveKeys.Num()));
			Count += CurveKeys.Num();
		}
	}
	else
	{
		OutResults.Add(TEXT("import_hierarchy: Specify either 'skeleton' or 'skeletal_mesh' path"));
	}

	return Count;
}

int32 FEditControlRigTool::DuplicateElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FString>& Names, TArray<FString>& OutResults)
{
	TArray<FRigElementKey> Keys;
	for (const FString& Name : Names)
	{
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Name);
		if (Key.IsValid())
		{
			Keys.Add(Key);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("duplicate: '%s' not found"), *Name));
		}
	}

	if (Keys.Num() == 0)
	{
		return 0;
	}

	TArray<FRigElementKey> NewKeys = Ctrl->DuplicateElements(Keys, false, false);
	for (const FRigElementKey& K : NewKeys)
	{
		OutResults.Add(FString::Printf(TEXT("Duplicated -> '%s'"), *K.Name.ToString()));
	}
	return NewKeys.Num();
}

int32 FEditControlRigTool::MirrorElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const FMirrorConfig& Config, TArray<FString>& OutResults)
{
	TArray<FRigElementKey> Keys;
	for (const FString& Name : Config.Elements)
	{
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Name);
		if (Key.IsValid())
		{
			Keys.Add(Key);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("mirror: '%s' not found"), *Name));
		}
	}

	if (Keys.Num() == 0)
	{
		return 0;
	}

	FRigVMMirrorSettings MirrorSettings;
	MirrorSettings.SearchString = Config.Search;
	MirrorSettings.ReplaceString = Config.Replace;
	if (!Config.MirrorAxis.IsEmpty())
	{
		MirrorSettings.MirrorAxis = ParseAxis(Config.MirrorAxis);
	}
	if (!Config.AxisToFlip.IsEmpty())
	{
		MirrorSettings.AxisToFlip = ParseAxis(Config.AxisToFlip);
	}

	TArray<FRigElementKey> NewKeys = Ctrl->MirrorElements(Keys, MirrorSettings, false, false);
	for (const FRigElementKey& K : NewKeys)
	{
		OutResults.Add(FString::Printf(TEXT("Mirrored -> '%s'"), *K.Name.ToString()));
	}
	return NewKeys.Num();
}

int32 FEditControlRigTool::ReorderElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FElementReorder>& Reorders, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FElementReorder& R : Reorders)
	{
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, R.Name);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("reorder: '%s' not found"), *R.Name));
			continue;
		}
		if (Ctrl->ReorderElement(Key, R.NewIndex, false))
		{
			OutResults.Add(FString::Printf(TEXT("Reordered '%s' to index %d"), *R.Name, R.NewIndex));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to reorder '%s'"), *R.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::SetSpaces(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy,
	const TArray<FSpaceConfig>& Configs, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FSpaceConfig& SC : Configs)
	{
		FRigElementKey ControlKey = FindElementKeyInHierarchy(Hierarchy, SC.ControlName);
		if (!ControlKey.IsValid() || ControlKey.Type != ERigElementType::Control)
		{
			OutResults.Add(FString::Printf(TEXT("set_spaces: Control '%s' not found"), *SC.ControlName));
			continue;
		}

		if (SC.bClear)
		{
			FRigControlElement* ControlElement = Hierarchy->Find<FRigControlElement>(ControlKey);
			if (ControlElement)
			{
	#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
				const TArray<FRigElementKeyWithLabel>& ExistingSpaces = ControlElement->Settings.Customization.AvailableSpaces;
				for (const FRigElementKeyWithLabel& Existing : ExistingSpaces)
				{
					if (Ctrl->RemoveAvailableSpace(ControlKey, Existing.Key, false, false))
					{
						Count++;
					}
				}
	#else
				const TArray<FRigElementKey>& ExistingSpaces = ControlElement->Settings.Customization.AvailableSpaces;
				for (const FRigElementKey& Existing : ExistingSpaces)
				{
					if (Ctrl->RemoveAvailableSpace(ControlKey, Existing, false, false))
					{
						Count++;
					}
				}
	#endif
				OutResults.Add(FString::Printf(TEXT("Cleared %d spaces from '%s'"), ExistingSpaces.Num(), *SC.ControlName));
			}
		}

		for (const FSpaceEntry& Space : SC.Spaces)
		{
			if (Space.Element.IsEmpty())
			{
				OutResults.Add(FString::Printf(TEXT("set_spaces: Empty space element for '%s'"), *SC.ControlName));
				continue;
			}

			FRigElementKey SpaceKey = FindElementKeyInHierarchy(Hierarchy, Space.Element);
			if (!SpaceKey.IsValid())
			{
				OutResults.Add(FString::Printf(TEXT("set_spaces: Space element '%s' not found"), *Space.Element));
				continue;
			}

			const bool bRemove = Space.Op.Equals(TEXT("remove"), ESearchCase::IgnoreCase);
			if (bRemove)
			{
				if (Ctrl->RemoveAvailableSpace(ControlKey, SpaceKey, false, false))
				{
					OutResults.Add(FString::Printf(TEXT("Removed space '%s' from '%s'"), *Space.Element, *SC.ControlName));
					Count++;
				}
				else
				{
					OutResults.Add(FString::Printf(TEXT("set_spaces: Failed to remove space '%s' from '%s'"), *Space.Element, *SC.ControlName));
				}
				continue;
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			if (Ctrl->AddAvailableSpace(ControlKey, SpaceKey, Space.Label.IsEmpty() ? NAME_None : FName(*Space.Label), false))
#else
			if (Ctrl->AddAvailableSpace(ControlKey, SpaceKey, false, false))
#endif
			{
				OutResults.Add(FString::Printf(TEXT("Added space '%s' to '%s'"), *Space.Element, *SC.ControlName));
				Count++;
			}
			else if (!Space.Label.IsEmpty() || Space.bHasIndex)
			{
				OutResults.Add(FString::Printf(TEXT("set_spaces: Space '%s' may already exist on '%s', continuing with updates"), *Space.Element, *SC.ControlName));
			}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
			if (!Space.Label.IsEmpty())
			{
				if (Ctrl->SetAvailableSpaceLabel(ControlKey, SpaceKey, FName(*Space.Label), false, false))
				{
					Count++;
				}
			}
#else
			if (!Space.Label.IsEmpty())
			{
				OutResults.Add(TEXT("set_spaces: labels are only supported in UE 5.6+"));
			}
#endif

			if (Space.bHasIndex)
			{
				if (Ctrl->SetAvailableSpaceIndex(ControlKey, SpaceKey, Space.Index, false, false))
				{
					Count++;
				}
			}

			if (Space.bActive)
			{
				if (Hierarchy->SwitchToParent(ControlKey, SpaceKey, false, true))
				{
					OutResults.Add(FString::Printf(TEXT("Activated space '%s' on '%s'"), *Space.Element, *SC.ControlName));
					Count++;
				}
			}
		}

		if (!SC.ActiveSpace.IsEmpty())
		{
			FRigElementKey ActiveSpaceKey = FindElementKeyInHierarchy(Hierarchy, SC.ActiveSpace);
			if (!ActiveSpaceKey.IsValid())
			{
				OutResults.Add(FString::Printf(TEXT("set_spaces: active_space '%s' not found"), *SC.ActiveSpace));
			}
			else if (Hierarchy->SwitchToParent(ControlKey, ActiveSpaceKey, false, true))
			{
				OutResults.Add(FString::Printf(TEXT("Activated space '%s' on '%s'"), *SC.ActiveSpace, *SC.ControlName));
				Count++;
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("set_spaces: Failed to activate '%s' on '%s'"), *SC.ActiveSpace, *SC.ControlName));
			}
		}
	}
	return Count;
}

int32 FEditControlRigTool::SetMetadata(URigHierarchy* Hierarchy, const TArray<FMetadataSet>& MetadataOps, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FMetadataSet& Op : MetadataOps)
	{
		if (!Hierarchy || Op.Element.IsEmpty() || Op.Name.IsEmpty() || Op.Type.IsEmpty())
		{
			OutResults.Add(TEXT("set_metadata: element, name, and type are required"));
			continue;
		}

		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Op.Element);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("set_metadata: element '%s' not found"), *Op.Element));
			continue;
		}

		const FString Type = Op.Type.ToLower();
		const FName MetaName(*Op.Name);
		bool bSuccess = false;
		if (Type == TEXT("bool"))
		{
			const bool bValue = Op.Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Op.Value == TEXT("1");
			bSuccess = Hierarchy->SetBoolMetadata(Key, MetaName, bValue);
		}
		else if (Type == TEXT("int32") || Type == TEXT("int"))
		{
			bSuccess = Hierarchy->SetInt32Metadata(Key, MetaName, FCString::Atoi(*Op.Value));
		}
			else if (Type == TEXT("float"))
			{
				bSuccess = Hierarchy->SetFloatMetadata(Key, MetaName, FCString::Atof(*Op.Value));
			}
		else if (Type == TEXT("name"))
		{
			bSuccess = Hierarchy->SetNameMetadata(Key, MetaName, FName(*Op.Value));
		}
		else if (Type == TEXT("vector"))
		{
			FVector Value = FVector::ZeroVector;
			Value.InitFromString(Op.Value);
			bSuccess = Hierarchy->SetVectorMetadata(Key, MetaName, Value);
		}
			else if (Type == TEXT("rotator"))
			{
				FRotator Value = FRotator::ZeroRotator;
				Value.InitFromString(Op.Value);
				bSuccess = Hierarchy->SetRotatorMetadata(Key, MetaName, Value);
			}
		else if (Type == TEXT("transform"))
		{
			FTransform Value = FTransform::Identity;
			Value.InitFromString(Op.Value);
			bSuccess = Hierarchy->SetTransformMetadata(Key, MetaName, Value);
		}
		else if (Type == TEXT("color") || Type == TEXT("linearcolor"))
		{
			bSuccess = Hierarchy->SetLinearColorMetadata(Key, MetaName, ParseColor(Op.Value));
		}
		else if (Type == TEXT("rig_element_key"))
		{
			FString ElementName = Op.Value;
			FString ElementType;
				if (Op.Value.Split(TEXT(":"), &ElementName, &ElementType))
				{
					FRigElementKey RefKey(FName(*ElementName), ParseElementType(ElementType));
					bSuccess = Hierarchy->SetRigElementKeyMetadata(Key, MetaName, RefKey);
				}
				else
				{
					FRigElementKey RefKey = FindElementKeyInHierarchy(Hierarchy, ElementName);
					if (RefKey.IsValid())
					{
						bSuccess = Hierarchy->SetRigElementKeyMetadata(Key, MetaName, RefKey);
					}
				}
			}
		else
		{
			OutResults.Add(FString::Printf(TEXT("set_metadata: Unsupported type '%s'"), *Op.Type));
			continue;
		}

		if (bSuccess)
		{
			OutResults.Add(FString::Printf(TEXT("Set metadata %s.%s (%s)"), *Op.Element, *Op.Name, *Op.Type));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("set_metadata: Failed to set %s.%s"), *Op.Element, *Op.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::RemoveMetadata(URigHierarchy* Hierarchy, const TArray<FMetadataRemove>& MetadataOps, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FMetadataRemove& Op : MetadataOps)
	{
		if (!Hierarchy || Op.Element.IsEmpty() || Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("remove_metadata: element and name are required"));
			continue;
		}

		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Op.Element);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("remove_metadata: element '%s' not found"), *Op.Element));
			continue;
		}

		if (Hierarchy->RemoveMetadata(Key, FName(*Op.Name)))
		{
			OutResults.Add(FString::Printf(TEXT("Removed metadata %s.%s"), *Op.Element, *Op.Name));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("remove_metadata: '%s.%s' not found"), *Op.Element, *Op.Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::SelectElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FString>& Names, bool bSelect, bool bClearFirst, TArray<FString>& OutResults)
{
	if (!Ctrl || !Hierarchy)
	{
		return 0;
	}

	int32 Count = 0;
	bool bDidClear = false;
	for (const FString& Name : Names)
	{
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Name);
		if (!Key.IsValid())
		{
			OutResults.Add(FString::Printf(TEXT("%s_elements: '%s' not found"), bSelect ? TEXT("select") : TEXT("deselect"), *Name));
			continue;
		}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		const bool bResult = Ctrl->SelectElement(Key, bSelect, bClearFirst && !bDidClear, false);
#else
		const bool bResult = Ctrl->SelectElement(Key, bSelect, bClearFirst && !bDidClear);
#endif
		bDidClear = bDidClear || bClearFirst;
		if (bResult)
		{
			OutResults.Add(FString::Printf(TEXT("%s '%s'"), bSelect ? TEXT("Selected") : TEXT("Deselected"), *Name));
			Count++;
		}
	}
	return Count;
}

int32 FEditControlRigTool::SetSelection(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FString>& Names, TArray<FString>& OutResults)
{
	if (!Ctrl || !Hierarchy)
	{
		return 0;
	}

	TArray<FRigElementKey> Keys;
	for (const FString& Name : Names)
	{
		FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Name);
		if (Key.IsValid())
		{
			Keys.Add(Key);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("set_selection: '%s' not found"), *Name));
		}
	}

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	if (Ctrl->SetSelection(Keys, false, false))
#else
	if (Ctrl->SetSelection(Keys, false))
#endif
	{
		OutResults.Add(FString::Printf(TEXT("Set selection to %d element(s)"), Keys.Num()));
		return 1;
	}
	OutResults.Add(TEXT("set_selection: Failed"));
	return 0;
}

// ============================================================================
// Variable Operations (member variables on blueprint)
// ============================================================================

FString FEditControlRigTool::ResolveCPPType(const FString& TypeName)
{
	if (TypeName.IsEmpty())
	{
		return FString();
	}

	// Exact builtin type matches (case-insensitive mapping to exact engine names)
	if (TypeName.Equals(TEXT("bool"), ESearchCase::IgnoreCase)) return TEXT("bool");
	if (TypeName.Equals(TEXT("float"), ESearchCase::IgnoreCase)) return TEXT("float");
	if (TypeName.Equals(TEXT("double"), ESearchCase::IgnoreCase)) return TEXT("double");
	if (TypeName.Equals(TEXT("int"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("int32"), ESearchCase::IgnoreCase)
		|| TypeName.Equals(TEXT("integer"), ESearchCase::IgnoreCase)) return TEXT("int32");
	if (TypeName.Equals(TEXT("FString"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("string"), ESearchCase::IgnoreCase)) return TEXT("FString");
	if (TypeName.Equals(TEXT("FName"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("name"), ESearchCase::IgnoreCase)) return TEXT("FName");
	if (TypeName.Equals(TEXT("FText"), ESearchCase::IgnoreCase) || TypeName.Equals(TEXT("text"), ESearchCase::IgnoreCase)) return TEXT("FText");

	// Accept full object paths directly (e.g. "/Script/CoreUObject.Vector")
	if (TypeName.Contains(TEXT(".")))
	{
		// Use LoadObject to ensure the package is loaded (FindObject only finds already-loaded)
		UScriptStruct* Struct = LoadObject<UScriptStruct>(nullptr, *TypeName);
		if (Struct)
		{
			return TypeName;
		}
		// Also try as enum
		UEnum* Enum = LoadObject<UEnum>(nullptr, *TypeName);
		if (Enum)
		{
			return TypeName;
		}
		return FString();
	}

	// Try to resolve struct types by name
	// Strip "F" prefix if present for struct lookup (engine stores "Vector" not "FVector")
	FString StructName = TypeName;
	if (StructName.Len() > 1 && StructName[0] == TEXT('F') && FChar::IsUpper(StructName[1]))
	{
		StructName = StructName.Mid(1);
	}

	// Common struct shortcuts -> full object paths
	// These are the types most commonly used in Control Rigs
	static const TMap<FString, FString> StructPaths = {
		{ TEXT("Vector"), TEXT("/Script/CoreUObject.Vector") },
		{ TEXT("Rotator"), TEXT("/Script/CoreUObject.Rotator") },
		{ TEXT("Transform"), TEXT("/Script/CoreUObject.Transform") },
		{ TEXT("Quat"), TEXT("/Script/CoreUObject.Quat") },
		{ TEXT("Vector2D"), TEXT("/Script/CoreUObject.Vector2D") },
		{ TEXT("Vector4"), TEXT("/Script/CoreUObject.Vector4") },
		{ TEXT("LinearColor"), TEXT("/Script/CoreUObject.LinearColor") },
		{ TEXT("Color"), TEXT("/Script/CoreUObject.Color") },
		{ TEXT("EulerTransform"), TEXT("/Script/AnimationCore.EulerTransform") },
		{ TEXT("RigElementKey"), TEXT("/Script/ControlRig.RigElementKey") },
	};

	if (const FString* Found = StructPaths.Find(StructName))
	{
		return *Found;
	}

	// Fallback: search by name among all UScriptStructs
	UScriptStruct* FoundStruct = FindFirstObject<UScriptStruct>(*StructName, EFindFirstObjectOptions::NativeFirst);
	if (FoundStruct)
	{
		return FoundStruct->GetPathName();
	}

	return FString();
}

int32 FEditControlRigTool::AddVariables(UControlRigBlueprint* BP, const TArray<FVariableToAdd>& Vars, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FVariableToAdd& V : Vars)
	{
		if (V.Name.IsEmpty() || V.Type.IsEmpty())
		{
			OutResults.Add(TEXT("add_variable: name and type required"));
			continue;
		}

		FString CPPType = ResolveCPPType(V.Type);
		if (CPPType.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("add_variable: Unknown type '%s'. Use: bool, float, double, int32, FString, FName, FVector, FRotator, FTransform, FEulerTransform, FRigElementKey, or a full type path."), *V.Type));
			continue;
		}

		// Pre-validate struct/enum types before calling AddMemberVariable.
		// ExternalVariableFromCPPTypePath (called internally) has check(false) that crashes
		// if the type path can't be resolved. We validate here to return an error instead.
		UScriptStruct* ResolvedStruct = nullptr;
		UEnum* ResolvedEnum = nullptr;
		bool bIsPrimitive = (CPPType == TEXT("bool") || CPPType == TEXT("float") || CPPType == TEXT("double")
			|| CPPType == TEXT("int32") || CPPType == TEXT("FString") || CPPType == TEXT("FName") || CPPType == TEXT("FText"));

		if (!bIsPrimitive)
		{
			// Use LoadObject to ensure the package is loaded (not just FindObject)
			ResolvedStruct = LoadObject<UScriptStruct>(nullptr, *CPPType);
			if (!ResolvedStruct)
			{
				ResolvedEnum = LoadObject<UEnum>(nullptr, *CPPType);
			}

			if (!ResolvedStruct && !ResolvedEnum)
			{
				OutResults.Add(FString::Printf(TEXT("add_variable: Type '%s' could not be loaded. Ensure the type path is correct and the module is loaded."), *CPPType));
				continue;
			}
		}

		// Type is validated — safe to call AddMemberVariable (won't hit check(false))
		FName Result = BP->AddMemberVariable(FName(*V.Name), CPPType, false, false, V.DefaultValue);
		if (!Result.IsNone())
		{
			// Register the struct/enum type in FRigVMRegistry so the RigVM knows about it.
			// AddMemberVariable goes through AddHostMemberVariableFromExternal which creates
			// the K2 variable, but OnVariableAdded (which registers in the registry) is only
			// called through the editor UI's OnPostVariableChange delegate — not from code.
			if (ResolvedStruct)
			{
				FString StructCPPName = RigVMTypeUtils::GetUniqueStructTypeName(ResolvedStruct);
				FRigVMRegistry::Get().FindOrAddType(FRigVMTemplateArgumentType(*StructCPPName, ResolvedStruct));
			}
			else if (ResolvedEnum)
			{
				FString EnumCPPName = RigVMTypeUtils::CPPTypeFromEnum(ResolvedEnum);
				FRigVMRegistry::Get().FindOrAddType(FRigVMTemplateArgumentType(*EnumCPPName, ResolvedEnum));
			}

			OutResults.Add(FString::Printf(TEXT("Added variable '%s' (%s)"), *Result.ToString(), *CPPType));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add variable '%s' — name may already exist"), *V.Name));
		}
	}

	if (Count > 0)
	{
		// Ensure the RigVM picks up the new variables
		BP->RequestAutoVMRecompilation();
	}

	return Count;
}

int32 FEditControlRigTool::RemoveVariables(UControlRigBlueprint* BP, const TArray<FString>& Names, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FString& Name : Names)
	{
		if (BP->RemoveMemberVariable(FName(*Name)))
		{
			OutResults.Add(FString::Printf(TEXT("Removed variable '%s'"), *Name));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to remove variable '%s' — not found"), *Name));
		}
	}
	return Count;
}

int32 FEditControlRigTool::SetVariableDefaults(UControlRigBlueprint* BP, const TArray<FVariableDefault>& Defaults, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FVariableDefault& VD : Defaults)
	{
		if (VD.Name.IsEmpty())
		{
			OutResults.Add(TEXT("set_variable_default: name required"));
			continue;
		}

		// Find the existing variable to get its current type
		TArray<FRigVMGraphVariableDescription> MemberVars = BP->GetMemberVariables();
		const FRigVMGraphVariableDescription* Found = nullptr;
		for (const FRigVMGraphVariableDescription& Desc : MemberVars)
		{
			if (Desc.Name == FName(*VD.Name))
			{
				Found = &Desc;
				break;
			}
		}

		if (!Found)
		{
			OutResults.Add(FString::Printf(TEXT("set_variable_default: Variable '%s' not found"), *VD.Name));
			continue;
		}

		// Use ChangeMemberVariableType with the SAME type but new default value
		if (BP->ChangeMemberVariableType(FName(*VD.Name), Found->CPPType, false, false, VD.Value))
		{
			OutResults.Add(FString::Printf(TEXT("Set variable '%s' default = %s"), *VD.Name, *VD.Value.Left(100)));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to set default for variable '%s'"), *VD.Name));
		}
	}
	return Count;
}

// ============================================================================
// Discovery
// ============================================================================

FString FEditControlRigTool::ListHierarchy(URigHierarchy* Hierarchy)
{
	if (!Hierarchy)
	{
		return TEXT("Hierarchy: (null)");
	}

	TArray<FRigElementKey> Keys = Hierarchy->GetAllKeys(false, ERigElementType::All);
	Keys.Sort([](const FRigElementKey& A, const FRigElementKey& B)
	{
		const int32 TypeCmp = static_cast<int32>(A.Type) - static_cast<int32>(B.Type);
		return (TypeCmp == 0) ? (A.Name.LexicalLess(B.Name)) : (TypeCmp < 0);
	});

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Hierarchy elements (%d):"), Keys.Num()));
	for (const FRigElementKey& Key : Keys)
	{
		const TArray<FRigElementKey> Parents = Hierarchy->GetParents(Key, false);
		TArray<FString> ParentNames;
		for (const FRigElementKey& Parent : Parents)
		{
			ParentNames.Add(Parent.Name.ToString());
		}
		const FString ParentList = ParentNames.Num() > 0 ? FString::Join(ParentNames, TEXT(",")) : TEXT("None");

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
		const TArray<FRigComponentKey> ComponentKeys = Hierarchy->GetComponentKeys(Key);
		TArray<FString> ComponentNames;
		for (const FRigComponentKey& ComponentKey : ComponentKeys)
		{
			ComponentNames.Add(ComponentKey.Name.ToString());
		}
		const FString ComponentsList = ComponentNames.Num() > 0 ? FString::Join(ComponentNames, TEXT(",")) : TEXT("None");
#else
		const FString ComponentsList = TEXT("N/A (UE 5.6+)");
#endif

		Lines.Add(FString::Printf(TEXT("  %s:%s parents=%s components=%s"),
			*Key.Name.ToString(),
			*ElementTypeToString(Key.Type),
			*ParentList,
			*ComponentsList));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString FEditControlRigTool::ListVariables(UControlRigBlueprint* BP)
{
	if (!BP)
	{
		return TEXT("Variables: (null blueprint)");
	}

	const TArray<FRigVMGraphVariableDescription> Vars = BP->GetMemberVariables();
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Variables (%d):"), Vars.Num()));
	for (const FRigVMGraphVariableDescription& Var : Vars)
	{
		const FString DefaultVal = Var.DefaultValue.IsEmpty() ? TEXT("<none>") : Var.DefaultValue;
		Lines.Add(FString::Printf(TEXT("  %s : %s = %s"), *Var.Name.ToString(), *Var.CPPType, *DefaultVal));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString FEditControlRigTool::ListSelection(URigHierarchy* Hierarchy)
{
	if (!Hierarchy)
	{
		return TEXT("Selection: (null hierarchy)");
	}

	const TArray<FRigElementKey> Selected = Hierarchy->GetSelectedKeys(ERigElementType::All);
	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Selected elements (%d):"), Selected.Num()));
	for (const FRigElementKey& Key : Selected)
	{
		Lines.Add(FString::Printf(TEXT("  %s:%s"), *Key.Name.ToString(), *ElementTypeToString(Key.Type)));
	}
	return FString::Join(Lines, TEXT("\n"));
}

FString FEditControlRigTool::ListMetadata(URigHierarchy* Hierarchy, const TArray<FString>& ElementNames)
{
	if (!Hierarchy)
	{
		return TEXT("Metadata: (null hierarchy)");
	}

	TArray<FRigElementKey> Keys;
	if (ElementNames.Num() == 0)
	{
		Keys = Hierarchy->GetAllKeys(false, ERigElementType::All);
	}
	else
	{
		for (const FString& Name : ElementNames)
		{
			const FRigElementKey Key = FindElementKeyInHierarchy(Hierarchy, Name);
			if (Key.IsValid())
			{
				Keys.Add(Key);
			}
		}
	}

	auto MetadataTypeToString = [](ERigMetadataType Type) -> FString
	{
		switch (Type)
		{
		case ERigMetadataType::Bool: return TEXT("bool");
		case ERigMetadataType::Float: return TEXT("float");
		case ERigMetadataType::Int32: return TEXT("int32");
		case ERigMetadataType::Name: return TEXT("name");
		case ERigMetadataType::Vector: return TEXT("vector");
		case ERigMetadataType::Rotator: return TEXT("rotator");
		case ERigMetadataType::Quat: return TEXT("quat");
		case ERigMetadataType::Transform: return TEXT("transform");
		case ERigMetadataType::LinearColor: return TEXT("linearcolor");
		case ERigMetadataType::RigElementKey: return TEXT("rig_element_key");
		default: return TEXT("unknown");
		}
	};

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Metadata (elements=%d):"), Keys.Num()));
	for (const FRigElementKey& Key : Keys)
	{
		const TArray<FName> Names = Hierarchy->GetMetadataNames(Key);
		if (Names.Num() == 0)
		{
			Lines.Add(FString::Printf(TEXT("  %s:%s -> (none)"), *Key.Name.ToString(), *ElementTypeToString(Key.Type)));
			continue;
		}

		for (const FName& MetaName : Names)
		{
			const ERigMetadataType MetaType = Hierarchy->GetMetadataType(Key, MetaName);
			FString Value;
			switch (MetaType)
			{
			case ERigMetadataType::Bool:
				Value = Hierarchy->GetBoolMetadata(Key, MetaName, false) ? TEXT("true") : TEXT("false");
				break;
			case ERigMetadataType::Float:
				Value = FString::SanitizeFloat(Hierarchy->GetFloatMetadata(Key, MetaName, 0.0f));
				break;
			case ERigMetadataType::Int32:
				Value = FString::FromInt(Hierarchy->GetInt32Metadata(Key, MetaName, 0));
				break;
			case ERigMetadataType::Name:
				Value = Hierarchy->GetNameMetadata(Key, MetaName, NAME_None).ToString();
				break;
			case ERigMetadataType::Vector:
				Value = Hierarchy->GetVectorMetadata(Key, MetaName, FVector::ZeroVector).ToString();
				break;
			case ERigMetadataType::Rotator:
				Value = Hierarchy->GetRotatorMetadata(Key, MetaName, FRotator::ZeroRotator).ToString();
				break;
			case ERigMetadataType::Quat:
				Value = Hierarchy->GetQuatMetadata(Key, MetaName, FQuat::Identity).ToString();
				break;
			case ERigMetadataType::Transform:
				Value = Hierarchy->GetTransformMetadata(Key, MetaName, FTransform::Identity).ToString();
				break;
			case ERigMetadataType::LinearColor:
				Value = Hierarchy->GetLinearColorMetadata(Key, MetaName, FLinearColor::Transparent).ToString();
				break;
			case ERigMetadataType::RigElementKey:
			{
				const FRigElementKey RefKey = Hierarchy->GetRigElementKeyMetadata(Key, MetaName, FRigElementKey());
				Value = FString::Printf(TEXT("%s:%s"), *RefKey.Name.ToString(), *ElementTypeToString(RefKey.Type));
				break;
			}
			default:
				Value = TEXT("<unsupported>");
				break;
			}

			Lines.Add(FString::Printf(TEXT("  %s:%s.%s [%s] = %s"),
				*Key.Name.ToString(),
				*ElementTypeToString(Key.Type),
				*MetaName.ToString(),
				*MetadataTypeToString(MetaType),
				*Value));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString FEditControlRigTool::ListUnitTypes(const FString& Filter)
{
	TArray<TPair<FString, FString>> FoundTypes; // Name, Tooltip

	for (TObjectIterator<UScriptStruct> It; It; ++It)
	{
		UScriptStruct* Struct = *It;
		if (!Struct || !Struct->IsChildOf(FRigUnit::StaticStruct()))
		{
			continue;
		}

		// Skip abstract and deprecated structs
		if (Struct->HasMetaData(TEXT("Abstract")) || Struct->HasMetaData(TEXT("Deprecated")))
		{
			continue;
		}

		FString Name = Struct->GetName();

		// Apply filter
		if (!Filter.IsEmpty() && !Name.Contains(Filter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		FString Tooltip;
#if WITH_EDITOR
		Tooltip = Struct->GetMetaData(TEXT("ToolTip"));
		if (Tooltip.Len() > 120)
		{
			Tooltip = Tooltip.Left(117) + TEXT("...");
		}
#endif
		FoundTypes.Add(TPair<FString, FString>(Name, Tooltip));
	}

	// Sort alphabetically
	FoundTypes.Sort([](const TPair<FString, FString>& A, const TPair<FString, FString>& B)
	{
		return A.Key < B.Key;
	});

	TArray<FString> Lines;
	Lines.Add(FString::Printf(TEXT("Available RigUnit types (%d%s):"),
		FoundTypes.Num(),
		Filter.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(", filter: '%s'"), *Filter)));

	for (const auto& T : FoundTypes)
	{
		// Show short name (strip "RigUnit_" prefix for readability)
		FString ShortName = T.Key;
		ShortName.RemoveFromStart(TEXT("RigUnit_"));

		if (T.Value.IsEmpty())
		{
			Lines.Add(FString::Printf(TEXT("  %s (%s)"), *ShortName, *T.Key));
		}
		else
		{
			Lines.Add(FString::Printf(TEXT("  %s — %s"), *ShortName, *T.Value));
		}
	}

	return FString::Join(Lines, TEXT("\n"));
}

// ============================================================================
// Helpers
// ============================================================================

void FEditControlRigTool::MarkBPModified(UControlRigBlueprint* BP)
{
	if (BP)
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}
}
