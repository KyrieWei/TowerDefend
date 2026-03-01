// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditBlendSpaceTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// BlendSpace classes
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AimOffsetBlendSpace.h"
#include "Animation/AimOffsetBlendSpace1D.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendProfile.h"

// Factory classes
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Factories/AimOffsetBlendSpaceFactoryNew.h"
#include "Factories/AimOffsetBlendSpaceFactory1D.h"

// Asset utilities
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ScopedTransaction.h"

// ============================================================================
// PROTECTED MEMBER ACCESS HELPERS
// ============================================================================
// SampleData, BlendParameters, PerBoneBlendMode, ManualPerBoneOverrides,
// PerBoneBlendProfile are protected UPROPERTYs on UBlendSpace.
// We use property reflection to get mutable access — standard UE editor pattern.

static TArray<FBlendSample>& GetMutableSamples(UBlendSpace* BS)
{
	static FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("SampleData"));
	return *Prop->ContainerPtrToValuePtr<TArray<FBlendSample>>(BS);
}

static FBlendParameter* GetMutableBlendParameters(UBlendSpace* BS)
{
	static FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("BlendParameters"));
	return Prop->ContainerPtrToValuePtr<FBlendParameter>(BS);
}

static EBlendSpacePerBoneBlendMode& GetMutablePerBoneBlendMode(UBlendSpace* BS)
{
	static FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("PerBoneBlendMode"));
	return *Prop->ContainerPtrToValuePtr<EBlendSpacePerBoneBlendMode>(BS);
}

static TArray<FPerBoneInterpolation>& GetMutablePerBoneOverrides(UBlendSpace* BS)
{
	static FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("ManualPerBoneOverrides"));
	return *Prop->ContainerPtrToValuePtr<TArray<FPerBoneInterpolation>>(BS);
}

static FBlendSpaceBlendProfile& GetMutableBlendProfile(UBlendSpace* BS)
{
	static FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("PerBoneBlendProfile"));
	return *Prop->ContainerPtrToValuePtr<FBlendSpaceBlendProfile>(BS);
}

static TEnumAsByte<EBlendSpaceAxis>& GetMutableAxisToScale(UBlendSpace* BS)
{
	static FProperty* Prop = UBlendSpace::StaticClass()->FindPropertyByName(TEXT("AxisToScaleAnimation"));
	return *Prop->ContainerPtrToValuePtr<TEnumAsByte<EBlendSpaceAxis>>(BS);
}

// ============================================================================
// SCHEMA
// ============================================================================

TSharedPtr<FJsonObject> FEditBlendSpaceTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Basic parameters
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Blend Space asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (defaults to /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> TypeProp = MakeShared<FJsonObject>();
	TypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeProp->SetStringField(TEXT("description"), TEXT("Type to create: 'BlendSpace' (2D, default), 'BlendSpace1D', 'AimOffset' (2D additive), 'AimOffset1D'. Only used when creating new assets."));
	Properties->SetObjectField(TEXT("type"), TypeProp);

	TSharedPtr<FJsonObject> SkeletonProp = MakeShared<FJsonObject>();
	SkeletonProp->SetStringField(TEXT("type"), TEXT("string"));
	SkeletonProp->SetStringField(TEXT("description"), TEXT("Path to USkeleton asset. Required when creating a new Blend Space."));
	Properties->SetObjectField(TEXT("skeleton"), SkeletonProp);

	// Axis configuration
	TSharedPtr<FJsonObject> SetAxisProp = MakeShared<FJsonObject>();
	SetAxisProp->SetStringField(TEXT("type"), TEXT("array"));
	SetAxisProp->SetStringField(TEXT("description"), TEXT("Configure axes: [{axis: 0/'x' or 1/'y', display_name?: 'Speed', min?: 0, max?: 600, grid_divisions?: 4, snap_to_grid?: false, wrap_input?: false}]. 1D blend spaces only support axis 0/x."));
	Properties->SetObjectField(TEXT("set_axis"), SetAxisProp);

	// Sample management
	TSharedPtr<FJsonObject> AddSamplesProp = MakeShared<FJsonObject>();
	AddSamplesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSamplesProp->SetStringField(TEXT("description"), TEXT("Add samples: [{animation: '/Game/Anim', position: float (1D) or [x,y] (2D), rate_scale?: 1.0, use_single_frame?: false, frame_index?: 0}]. Position auto-expands axis ranges if needed."));
	Properties->SetObjectField(TEXT("add_samples"), AddSamplesProp);

	TSharedPtr<FJsonObject> RemoveSamplesProp = MakeShared<FJsonObject>();
	RemoveSamplesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSamplesProp->SetStringField(TEXT("description"), TEXT("Remove samples by index: [0, 2, 5]. Processed in reverse order to maintain index stability. WARNING: DeleteSample uses RemoveAtSwap so remaining indices may shift."));
	Properties->SetObjectField(TEXT("remove_samples"), RemoveSamplesProp);

	TSharedPtr<FJsonObject> EditSamplesProp = MakeShared<FJsonObject>();
	EditSamplesProp->SetStringField(TEXT("type"), TEXT("array"));
	EditSamplesProp->SetStringField(TEXT("description"), TEXT("Edit existing samples: [{index: 0, position?: float or [x,y], animation?: '/Game/NewAnim', rate_scale?: 1.5, use_single_frame?: false, frame_index?: 0}]. Only specified fields are modified."));
	Properties->SetObjectField(TEXT("edit_samples"), EditSamplesProp);

	TSharedPtr<FJsonObject> DuplicateSampleProp = MakeShared<FJsonObject>();
	DuplicateSampleProp->SetStringField(TEXT("type"), TEXT("object"));
	DuplicateSampleProp->SetStringField(TEXT("description"), TEXT("Duplicate a sample to a new position: {index: 0, position: float or [x,y]}. Copies animation, rate_scale, and single-frame settings."));
	Properties->SetObjectField(TEXT("duplicate_sample"), DuplicateSampleProp);

	// Per-bone blend
	TSharedPtr<FJsonObject> BlendModeProp = MakeShared<FJsonObject>();
	BlendModeProp->SetStringField(TEXT("type"), TEXT("string"));
	BlendModeProp->SetStringField(TEXT("description"), TEXT("Per-bone blend mode: 'Manual' (use set_per_bone_overrides) or 'BlendProfile' (use set_blend_profile)"));
	Properties->SetObjectField(TEXT("per_bone_blend_mode"), BlendModeProp);

	TSharedPtr<FJsonObject> SetBoneOverridesProp = MakeShared<FJsonObject>();
	SetBoneOverridesProp->SetStringField(TEXT("type"), TEXT("array"));
	SetBoneOverridesProp->SetStringField(TEXT("description"), TEXT("Set per-bone interpolation overrides: [{bone: 'spine_03', interpolation_speed: 5.0}]. Adds or updates entries by bone name."));
	Properties->SetObjectField(TEXT("set_per_bone_overrides"), SetBoneOverridesProp);

	TSharedPtr<FJsonObject> RemoveBoneOverridesProp = MakeShared<FJsonObject>();
	RemoveBoneOverridesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveBoneOverridesProp->SetStringField(TEXT("description"), TEXT("Remove per-bone overrides by bone name: ['spine_03', 'pelvis']"));
	Properties->SetObjectField(TEXT("remove_per_bone_overrides"), RemoveBoneOverridesProp);

	TSharedPtr<FJsonObject> SetBlendProfileProp = MakeShared<FJsonObject>();
	SetBlendProfileProp->SetStringField(TEXT("type"), TEXT("object"));
	SetBlendProfileProp->SetStringField(TEXT("description"), TEXT("Set blend profile for per-bone smoothing: {blend_profile?: '/Game/Path/BlendProfile', target_weight_interpolation_speed?: 5.0}"));
	Properties->SetObjectField(TEXT("set_blend_profile"), SetBlendProfileProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ============================================================================
// EXECUTE
// ============================================================================

FToolResult FEditBlendSpaceTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);

	// Load or create
	UBlendSpace* BS = GetOrLoadBlendSpace(Name, Path);
	bool bCreatedNew = false;

	if (!BS)
	{
		FString Type, SkeletonPath;
		Args->TryGetStringField(TEXT("type"), Type);
		Args->TryGetStringField(TEXT("skeleton"), SkeletonPath);

		TArray<FString> CreateResults;
		BS = CreateBlendSpaceAsset(Name, Path, Type, SkeletonPath, CreateResults);
		if (!BS)
		{
			FString Error = CreateResults.Num() > 0 ? CreateResults[0] : TEXT("Failed to create blend space");
			return FToolResult::Fail(Error);
		}
		bCreatedNew = true;
	}

	TArray<FString> Results;
	if (bCreatedNew)
	{
		FString TypeStr = BS->IsA<UAimOffsetBlendSpace>() ? TEXT("AimOffset") :
		                  BS->IsA<UAimOffsetBlendSpace1D>() ? TEXT("AimOffset1D") :
		                  Is1D(BS) ? TEXT("BlendSpace1D") : TEXT("BlendSpace");
		Results.Add(FString::Printf(TEXT("Created %s: %s"), *TypeStr, *BS->GetPathName()));
	}
	else
	{
		Results.Add(FString::Printf(TEXT("Editing BlendSpace: %s"), *BS->GetPathName()));
	}

	// Begin transaction
	const FScopedTransaction Transaction(FText::FromString(TEXT("Edit Blend Space")));
	BS->Modify();

	bool bSamplesChanged = false;

	// 1. Set axes (before samples so ranges are correct)
	const TArray<TSharedPtr<FJsonValue>>* SetAxisArray;
	if (Args->TryGetArrayField(TEXT("set_axis"), SetAxisArray))
	{
		SetAxes(BS, SetAxisArray, Results);
	}

	// 2. Add samples
	const TArray<TSharedPtr<FJsonValue>>* AddSamplesArray;
	if (Args->TryGetArrayField(TEXT("add_samples"), AddSamplesArray))
	{
		if (AddSamples(BS, AddSamplesArray, Results) > 0)
		{
			bSamplesChanged = true;
		}
	}

	// 3. Remove samples (reverse-sorted)
	const TArray<TSharedPtr<FJsonValue>>* RemoveSamplesArray;
	if (Args->TryGetArrayField(TEXT("remove_samples"), RemoveSamplesArray))
	{
		if (RemoveSamples(BS, RemoveSamplesArray, Results) > 0)
		{
			bSamplesChanged = true;
		}
	}

	// 4. Edit samples
	const TArray<TSharedPtr<FJsonValue>>* EditSamplesArray;
	if (Args->TryGetArrayField(TEXT("edit_samples"), EditSamplesArray))
	{
		if (EditSamples(BS, EditSamplesArray, Results) > 0)
		{
			bSamplesChanged = true;
		}
	}

	// 5. Duplicate sample
	const TSharedPtr<FJsonObject>* DuplicateSampleObj;
	if (Args->TryGetObjectField(TEXT("duplicate_sample"), DuplicateSampleObj))
	{
		if (DuplicateSample(BS, *DuplicateSampleObj, Results))
		{
			bSamplesChanged = true;
		}
	}

	// 6. Per-bone blend mode
	FString BlendModeStr;
	if (Args->TryGetStringField(TEXT("per_bone_blend_mode"), BlendModeStr) && !BlendModeStr.IsEmpty())
	{
		if (BlendModeStr.Equals(TEXT("Manual"), ESearchCase::IgnoreCase))
		{
			GetMutablePerBoneBlendMode(BS) = EBlendSpacePerBoneBlendMode::ManualPerBoneOverride;
			Results.Add(TEXT("= Per-bone blend mode: Manual"));
		}
		else if (BlendModeStr.Equals(TEXT("BlendProfile"), ESearchCase::IgnoreCase))
		{
			GetMutablePerBoneBlendMode(BS) = EBlendSpacePerBoneBlendMode::BlendProfile;
			Results.Add(TEXT("= Per-bone blend mode: BlendProfile"));
		}
		else
		{
			Results.Add(FString::Printf(TEXT("! Unknown per_bone_blend_mode: '%s' (use 'Manual' or 'BlendProfile')"), *BlendModeStr));
		}
	}

	// 9. Set per-bone overrides
	const TArray<TSharedPtr<FJsonValue>>* SetBoneOverridesArray;
	if (Args->TryGetArrayField(TEXT("set_per_bone_overrides"), SetBoneOverridesArray))
	{
		SetPerBoneOverrides(BS, SetBoneOverridesArray, Results);
	}

	// 10. Remove per-bone overrides
	const TArray<TSharedPtr<FJsonValue>>* RemoveBoneOverridesArray;
	if (Args->TryGetArrayField(TEXT("remove_per_bone_overrides"), RemoveBoneOverridesArray))
	{
		RemovePerBoneOverrides(BS, RemoveBoneOverridesArray, Results);
	}

	// 11. Set blend profile
	const TSharedPtr<FJsonObject>* SetBlendProfileObj;
	if (Args->TryGetObjectField(TEXT("set_blend_profile"), SetBlendProfileObj))
	{
		SetBlendProfile(BS, *SetBlendProfileObj, Results);
	}

	// Finalize: validate + resample + notify
	if (bSamplesChanged)
	{
		BS->ValidateSampleData();
	}
	BS->ResampleData();
	BS->PostEditChange();
	BS->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(BS);

	return FToolResult::Ok(FString::Join(Results, TEXT("\n")));
}

// ============================================================================
// ASSET MANAGEMENT
// ============================================================================

UBlendSpace* FEditBlendSpaceTool::GetOrLoadBlendSpace(const FString& Name, const FString& Path)
{
	FString FullPath;
	if (Name.StartsWith(TEXT("/")))
	{
		FullPath = Name;
	}
	else
	{
		FString BasePath = Path.IsEmpty() ? TEXT("/Game") : Path;
		if (!BasePath.StartsWith(TEXT("/")))
		{
			BasePath = TEXT("/Game/") + BasePath;
		}
		FullPath = BasePath / Name;
	}

	return NeoStackToolUtils::LoadAssetWithFallback<UBlendSpace>(FullPath);
}

UBlendSpace* FEditBlendSpaceTool::CreateBlendSpaceAsset(const FString& Name, const FString& Path,
                                                         const FString& Type, const FString& SkeletonPath,
                                                         TArray<FString>& OutResults)
{
	// Load skeleton
	if (SkeletonPath.IsEmpty())
	{
		OutResults.Add(TEXT("Cannot create blend space: 'skeleton' parameter is required for new assets"));
		return nullptr;
	}

	USkeleton* Skeleton = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(SkeletonPath);
	if (!Skeleton)
	{
		// Try with /Game/ prefix
		if (!SkeletonPath.StartsWith(TEXT("/")))
		{
			Skeleton = NeoStackToolUtils::LoadAssetWithFallback<USkeleton>(TEXT("/Game/") + SkeletonPath);
		}
		if (!Skeleton)
		{
			OutResults.Add(FString::Printf(TEXT("Skeleton not found: %s"), *SkeletonPath));
			return nullptr;
		}
	}

	// Create package
	FString SanitizedName;
	UPackage* Package = NeoStackToolUtils::CreateAssetPackage(Name, Path, SanitizedName);
	if (!Package)
	{
		OutResults.Add(TEXT("Failed to create asset package"));
		return nullptr;
	}

	// Determine type and create via factory
	FString TypeLower = Type.ToLower();
	UBlendSpace* NewBS = nullptr;

	if (TypeLower == TEXT("blendspace1d") || TypeLower == TEXT("1d"))
	{
		UBlendSpaceFactory1D* Factory = NewObject<UBlendSpaceFactory1D>();
		Factory->TargetSkeleton = Skeleton;
		NewBS = Cast<UBlendSpace>(Factory->FactoryCreateNew(
			UBlendSpace1D::StaticClass(), Package, FName(*SanitizedName),
			RF_Public | RF_Standalone, nullptr, GWarn));
	}
	else if (TypeLower == TEXT("aimoffset") || TypeLower == TEXT("aimoffset2d"))
	{
		UAimOffsetBlendSpaceFactoryNew* Factory = NewObject<UAimOffsetBlendSpaceFactoryNew>();
		Factory->TargetSkeleton = Skeleton;
		NewBS = Cast<UBlendSpace>(Factory->FactoryCreateNew(
			UAimOffsetBlendSpace::StaticClass(), Package, FName(*SanitizedName),
			RF_Public | RF_Standalone, nullptr, GWarn));
	}
	else if (TypeLower == TEXT("aimoffset1d"))
	{
		UAimOffsetBlendSpaceFactory1D* Factory = NewObject<UAimOffsetBlendSpaceFactory1D>();
		Factory->TargetSkeleton = Skeleton;
		NewBS = Cast<UBlendSpace>(Factory->FactoryCreateNew(
			UAimOffsetBlendSpace1D::StaticClass(), Package, FName(*SanitizedName),
			RF_Public | RF_Standalone, nullptr, GWarn));
	}
	else // Default: 2D BlendSpace
	{
		UBlendSpaceFactoryNew* Factory = NewObject<UBlendSpaceFactoryNew>();
		Factory->TargetSkeleton = Skeleton;
		NewBS = Cast<UBlendSpace>(Factory->FactoryCreateNew(
			UBlendSpace::StaticClass(), Package, FName(*SanitizedName),
			RF_Public | RF_Standalone, nullptr, GWarn));
	}

	if (NewBS)
	{
		FAssetRegistryModule::AssetCreated(NewBS);
		Package->MarkPackageDirty();
	}
	else
	{
		OutResults.Add(TEXT("Factory failed to create blend space (skeleton may be invalid)"));
	}

	return NewBS;
}

// ============================================================================
// AXIS CONFIGURATION
// ============================================================================

int32 FEditBlendSpaceTool::SetAxes(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* AxesArray,
                                    TArray<FString>& OutResults)
{
	if (!AxesArray) return 0;

	int32 Configured = 0;
	bool bIs1D = Is1D(BS);

	for (const TSharedPtr<FJsonValue>& AxisValue : *AxesArray)
	{
		const TSharedPtr<FJsonObject>* AxisObj;
		if (!AxisValue->TryGetObject(AxisObj)) continue;

		// Parse axis index
		int32 AxisIndex = -1;
		TSharedPtr<FJsonValue> AxisIdxVal = (*AxisObj)->TryGetField(TEXT("axis"));
		if (AxisIdxVal.IsValid())
		{
			AxisIndex = ParseAxisIndex(AxisIdxVal);
		}

		if (AxisIndex < 0 || AxisIndex > 1)
		{
			OutResults.Add(TEXT("! set_axis: 'axis' must be 0/'x' or 1/'y'"));
			continue;
		}

		if (bIs1D && AxisIndex > 0)
		{
			OutResults.Add(TEXT("! set_axis: 1D blend spaces only support axis 0 (X)"));
			continue;
		}

		FBlendParameter& Param = GetMutableBlendParameters(BS)[AxisIndex];

		FString DisplayName;
		if ((*AxisObj)->TryGetStringField(TEXT("display_name"), DisplayName) && !DisplayName.IsEmpty())
		{
			Param.DisplayName = DisplayName;
		}

		double MinVal;
		if ((*AxisObj)->TryGetNumberField(TEXT("min"), MinVal))
		{
			Param.Min = static_cast<float>(MinVal);
		}

		double MaxVal;
		if ((*AxisObj)->TryGetNumberField(TEXT("max"), MaxVal))
		{
			Param.Max = static_cast<float>(MaxVal);
		}

		// Validate min < max
		if (Param.Min >= Param.Max)
		{
			OutResults.Add(FString::Printf(TEXT("! set_axis[%d]: min (%.1f) must be less than max (%.1f)"), AxisIndex, Param.Min, Param.Max));
			continue;
		}

		double GridDiv;
		if ((*AxisObj)->TryGetNumberField(TEXT("grid_divisions"), GridDiv))
		{
			Param.GridNum = FMath::Max(1, static_cast<int32>(GridDiv));
		}

		bool bSnap;
		if ((*AxisObj)->TryGetBoolField(TEXT("snap_to_grid"), bSnap))
		{
			Param.bSnapToGrid = bSnap;
		}

		bool bWrap;
		if ((*AxisObj)->TryGetBoolField(TEXT("wrap_input"), bWrap))
		{
			Param.bWrapInput = bWrap;
		}

		FString AxisName = AxisIndex == 0 ? TEXT("X") : TEXT("Y");
		OutResults.Add(FString::Printf(TEXT("= Axis %s: '%s' [%.1f, %.1f] Grid=%d Snap=%s Wrap=%s"),
			*AxisName, *Param.DisplayName, Param.Min, Param.Max, Param.GridNum,
			Param.bSnapToGrid ? TEXT("true") : TEXT("false"),
			Param.bWrapInput ? TEXT("true") : TEXT("false")));
		Configured++;
	}

	return Configured;
}

// ============================================================================
// SAMPLE OPERATIONS
// ============================================================================

int32 FEditBlendSpaceTool::AddSamples(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* SamplesArray,
                                       TArray<FString>& OutResults)
{
	if (!SamplesArray) return 0;

	int32 Added = 0;
	bool bIs1D = Is1D(BS);

	for (const TSharedPtr<FJsonValue>& SampleValue : *SamplesArray)
	{
		const TSharedPtr<FJsonObject>* SampleObj;
		if (!SampleValue->TryGetObject(SampleObj)) continue;

		// Load animation
		FString AnimPath;
		if (!(*SampleObj)->TryGetStringField(TEXT("animation"), AnimPath) || AnimPath.IsEmpty())
		{
			OutResults.Add(TEXT("! add_samples: missing 'animation' field"));
			continue;
		}

		UAnimSequence* Anim = NeoStackToolUtils::LoadAssetWithFallback<UAnimSequence>(AnimPath);
		if (!Anim)
		{
			// Try with /Game/ prefix
			if (!AnimPath.StartsWith(TEXT("/")))
			{
				Anim = NeoStackToolUtils::LoadAssetWithFallback<UAnimSequence>(TEXT("/Game/") + AnimPath);
			}
			if (!Anim)
			{
				FString BuiltPath = NeoStackToolUtils::BuildAssetPath(AnimPath, TEXT(""));
				Anim = LoadObject<UAnimSequence>(nullptr, *BuiltPath);
			}
		}
		if (!Anim)
		{
			OutResults.Add(FString::Printf(TEXT("! add_samples: animation not found: %s"), *AnimPath));
			continue;
		}

		// Check compatibility
		if (!BS->IsAnimationCompatible(Anim))
		{
			OutResults.Add(FString::Printf(TEXT("! add_samples: animation '%s' is not compatible with this blend space (skeleton or additive type mismatch)"), *Anim->GetName()));
			continue;
		}

		// Parse position
		TSharedPtr<FJsonValue> PosVal = (*SampleObj)->TryGetField(TEXT("position"));
		if (!PosVal.IsValid())
		{
			OutResults.Add(TEXT("! add_samples: missing 'position' field"));
			continue;
		}

		bool bPosValid = false;
		FVector SamplePos = ParseSamplePosition(PosVal, bIs1D, bPosValid);
		if (!bPosValid)
		{
			OutResults.Add(TEXT("! add_samples: invalid 'position' — use float for 1D, [x,y] array for 2D"));
			continue;
		}

		// Add sample
		int32 NewIndex = BS->AddSample(Anim, SamplePos);
		if (NewIndex == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! add_samples: failed to add '%s' at position (%.1f, %.1f) — may be too close to existing sample"),
				*Anim->GetName(), SamplePos.X, SamplePos.Y));
			continue;
		}

		// Set optional properties on the new sample
		TArray<FBlendSample>& SampleData = GetMutableSamples(BS);
		if (NewIndex >= 0 && NewIndex < SampleData.Num())
		{
			double RateScale;
			if ((*SampleObj)->TryGetNumberField(TEXT("rate_scale"), RateScale))
			{
				SampleData[NewIndex].RateScale = FMath::Clamp(static_cast<float>(RateScale), 0.01f, 64.0f);
			}


#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
			bool bUseSingleFrame;
			if ((*SampleObj)->TryGetBoolField(TEXT("use_single_frame"), bUseSingleFrame))
			{
				SampleData[NewIndex].bUseSingleFrameForBlending = bUseSingleFrame;
			}

			double FrameIndex;
			if ((*SampleObj)->TryGetNumberField(TEXT("frame_index"), FrameIndex))
			{
				SampleData[NewIndex].FrameIndexToSample = static_cast<uint32>(FMath::Max(0.0, FrameIndex));
			}
#endif
		}

		if (bIs1D)
		{
			OutResults.Add(FString::Printf(TEXT("+ Sample[%d]: '%s' at %.2f (RateScale=%.2f)"),
				NewIndex, *Anim->GetName(), SamplePos.X, SampleData[NewIndex].RateScale));
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("+ Sample[%d]: '%s' at (%.2f, %.2f) (RateScale=%.2f)"),
				NewIndex, *Anim->GetName(), SamplePos.X, SamplePos.Y, SampleData[NewIndex].RateScale));
		}
		Added++;
	}

	return Added;
}

int32 FEditBlendSpaceTool::RemoveSamples(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* IndicesArray,
                                          TArray<FString>& OutResults)
{
	if (!IndicesArray) return 0;

	const TArray<FBlendSample>& Samples = BS->GetBlendSamples();

	// Collect valid indices
	TArray<int32> IndicesToRemove;
	for (const TSharedPtr<FJsonValue>& IdxValue : *IndicesArray)
	{
		double IdxDouble;
		if (!IdxValue->TryGetNumber(IdxDouble)) continue;

		int32 Idx = static_cast<int32>(IdxDouble);
		if (Idx < 0 || Idx >= Samples.Num())
		{
			OutResults.Add(FString::Printf(TEXT("! remove_samples: index %d out of range (0-%d)"), Idx, Samples.Num() - 1));
			continue;
		}
		IndicesToRemove.AddUnique(Idx);
	}

	// Sort descending and remove
	IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });

	int32 Removed = 0;
	for (int32 Idx : IndicesToRemove)
	{
		// Re-check bounds (samples may have shifted due to RemoveAtSwap)
		if (Idx >= 0 && Idx < GetMutableSamples(BS).Num())
		{
			FString AnimName = GetMutableSamples(BS)[Idx].Animation ? GetMutableSamples(BS)[Idx].Animation->GetName() : TEXT("(none)");
			BS->DeleteSample(Idx);
			OutResults.Add(FString::Printf(TEXT("- Sample[%d]: '%s'"), Idx, *AnimName));
			Removed++;
		}
	}

	return Removed;
}

int32 FEditBlendSpaceTool::EditSamples(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* SamplesArray,
                                        TArray<FString>& OutResults)
{
	if (!SamplesArray) return 0;

	int32 Edited = 0;
	bool bIs1D = Is1D(BS);

	for (const TSharedPtr<FJsonValue>& SampleValue : *SamplesArray)
	{
		const TSharedPtr<FJsonObject>* SampleObj;
		if (!SampleValue->TryGetObject(SampleObj)) continue;

		double IndexDouble;
		if (!(*SampleObj)->TryGetNumberField(TEXT("index"), IndexDouble))
		{
			OutResults.Add(TEXT("! edit_samples: missing 'index' field"));
			continue;
		}
		int32 Index = static_cast<int32>(IndexDouble);

		if (Index < 0 || Index >= GetMutableSamples(BS).Num())
		{
			OutResults.Add(FString::Printf(TEXT("! edit_samples: index %d out of range (0-%d)"), Index, GetMutableSamples(BS).Num() - 1));
			continue;
		}

		bool bModified = false;
		TArray<FString> Changes;

		// Edit position
		TSharedPtr<FJsonValue> PosVal = (*SampleObj)->TryGetField(TEXT("position"));
		if (PosVal.IsValid())
		{
			bool bPosValid = false;
			FVector NewPos = ParseSamplePosition(PosVal, bIs1D, bPosValid);
			if (bPosValid)
			{
				if (BS->EditSampleValue(Index, NewPos))
				{
					Changes.Add(FString::Printf(TEXT("Pos=(%.2f,%.2f)"), NewPos.X, NewPos.Y));
					bModified = true;
				}
				else
				{
					OutResults.Add(FString::Printf(TEXT("! edit_samples[%d]: position (%.2f,%.2f) rejected (out of bounds or duplicate)"),
						Index, NewPos.X, NewPos.Y));
				}
			}
		}

		// Replace animation
		FString AnimPath;
		if ((*SampleObj)->TryGetStringField(TEXT("animation"), AnimPath) && !AnimPath.IsEmpty())
		{
			UAnimSequence* NewAnim = NeoStackToolUtils::LoadAssetWithFallback<UAnimSequence>(AnimPath);
			if (!NewAnim && !AnimPath.StartsWith(TEXT("/")))
			{
				NewAnim = NeoStackToolUtils::LoadAssetWithFallback<UAnimSequence>(TEXT("/Game/") + AnimPath);
			}
			if (NewAnim)
			{
				BS->ReplaceSampleAnimation(Index, NewAnim);
				Changes.Add(FString::Printf(TEXT("Anim='%s'"), *NewAnim->GetName()));
				bModified = true;
			}
			else
			{
				OutResults.Add(FString::Printf(TEXT("! edit_samples[%d]: animation not found: %s"), Index, *AnimPath));
			}
		}

		// Rate scale
		double RateScale;
		if ((*SampleObj)->TryGetNumberField(TEXT("rate_scale"), RateScale))
		{
			GetMutableSamples(BS)[Index].RateScale = FMath::Clamp(static_cast<float>(RateScale), 0.01f, 64.0f);
			Changes.Add(FString::Printf(TEXT("RateScale=%.2f"), GetMutableSamples(BS)[Index].RateScale));
			bModified = true;
		}


#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
		// Single frame
		bool bUseSingleFrame;
		if ((*SampleObj)->TryGetBoolField(TEXT("use_single_frame"), bUseSingleFrame))
		{
			GetMutableSamples(BS)[Index].bUseSingleFrameForBlending = bUseSingleFrame;
			Changes.Add(FString::Printf(TEXT("SingleFrame=%s"), bUseSingleFrame ? TEXT("true") : TEXT("false")));
			bModified = true;
		}

		double FrameIndex;
		if ((*SampleObj)->TryGetNumberField(TEXT("frame_index"), FrameIndex))
		{
			GetMutableSamples(BS)[Index].FrameIndexToSample = static_cast<uint32>(FMath::Max(0.0, FrameIndex));
			Changes.Add(FString::Printf(TEXT("FrameIdx=%d"), GetMutableSamples(BS)[Index].FrameIndexToSample));
			bModified = true;
		}
#endif

		if (bModified)
		{
			OutResults.Add(FString::Printf(TEXT("= Sample[%d]: %s"), Index, *FString::Join(Changes, TEXT(", "))));
			Edited++;
		}
	}

	return Edited;
}

bool FEditBlendSpaceTool::DuplicateSample(UBlendSpace* BS, const TSharedPtr<FJsonObject>& DupObj,
                                           TArray<FString>& OutResults)
{
	double IndexDouble;
	if (!DupObj->TryGetNumberField(TEXT("index"), IndexDouble))
	{
		OutResults.Add(TEXT("! duplicate_sample: missing 'index' field"));
		return false;
	}
	int32 SourceIndex = static_cast<int32>(IndexDouble);

	if (SourceIndex < 0 || SourceIndex >= GetMutableSamples(BS).Num())
	{
		OutResults.Add(FString::Printf(TEXT("! duplicate_sample: index %d out of range (0-%d)"), SourceIndex, GetMutableSamples(BS).Num() - 1));
		return false;
	}

	TSharedPtr<FJsonValue> PosVal = DupObj->TryGetField(TEXT("position"));
	if (!PosVal.IsValid())
	{
		OutResults.Add(TEXT("! duplicate_sample: missing 'position' field"));
		return false;
	}

	bool bIs1D = Is1D(BS);
	bool bPosValid = false;
	FVector NewPos = ParseSamplePosition(PosVal, bIs1D, bPosValid);
	if (!bPosValid)
	{
		OutResults.Add(TEXT("! duplicate_sample: invalid 'position'"));
		return false;
	}

	// Copy source sample data BEFORE AddSample — AddSample may reallocate the array
	const FBlendSample SourceSampleCopy = GetMutableSamples(BS)[SourceIndex];
	UAnimSequence* Anim = Cast<UAnimSequence>(SourceSampleCopy.Animation);
	if (!Anim)
	{
		OutResults.Add(FString::Printf(TEXT("! duplicate_sample: source sample[%d] has no valid animation"), SourceIndex));
		return false;
	}

	int32 NewIndex = BS->AddSample(Anim, NewPos);
	if (NewIndex == INDEX_NONE)
	{
		OutResults.Add(FString::Printf(TEXT("! duplicate_sample: failed to add at position (%.2f, %.2f)"), NewPos.X, NewPos.Y));
		return false;
	}

	// Copy properties from source copy (safe — not referencing potentially-reallocated array)
	GetMutableSamples(BS)[NewIndex].RateScale = SourceSampleCopy.RateScale;
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
	GetMutableSamples(BS)[NewIndex].bUseSingleFrameForBlending = SourceSampleCopy.bUseSingleFrameForBlending;
	GetMutableSamples(BS)[NewIndex].FrameIndexToSample = SourceSampleCopy.FrameIndexToSample;
#endif

	OutResults.Add(FString::Printf(TEXT("+ Duplicated Sample[%d] -> [%d]: '%s' at (%.2f, %.2f)"),
		SourceIndex, NewIndex, *Anim->GetName(), NewPos.X, NewPos.Y));
	return true;
}

// ============================================================================
// PER-BONE BLEND OVERRIDES
// ============================================================================

int32 FEditBlendSpaceTool::SetPerBoneOverrides(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* OverridesArray,
                                                TArray<FString>& OutResults)
{
	if (!OverridesArray) return 0;

	int32 Modified = 0;

	for (const TSharedPtr<FJsonValue>& OverrideValue : *OverridesArray)
	{
		const TSharedPtr<FJsonObject>* OverrideObj;
		if (!OverrideValue->TryGetObject(OverrideObj)) continue;

		FString BoneName;
		if (!(*OverrideObj)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			OutResults.Add(TEXT("! set_per_bone_overrides: missing 'bone' field"));
			continue;
		}

		double InterpSpeed = 5.0;
		(*OverrideObj)->TryGetNumberField(TEXT("interpolation_speed"), InterpSpeed);

		// Find existing entry or add new one
		bool bFound = false;
		for (FPerBoneInterpolation& Override : GetMutablePerBoneOverrides(BS))
		{
			if (Override.BoneReference.BoneName == FName(*BoneName))
			{
				Override.InterpolationSpeedPerSec = static_cast<float>(InterpSpeed);
				OutResults.Add(FString::Printf(TEXT("= PerBoneOverride '%s': Speed=%.2f"), *BoneName, Override.InterpolationSpeedPerSec));
				bFound = true;
				Modified++;
				break;
			}
		}

		if (!bFound)
		{
			FPerBoneInterpolation NewOverride;
			NewOverride.BoneReference.BoneName = FName(*BoneName);
			NewOverride.InterpolationSpeedPerSec = static_cast<float>(InterpSpeed);
			GetMutablePerBoneOverrides(BS).Add(NewOverride);
			OutResults.Add(FString::Printf(TEXT("+ PerBoneOverride '%s': Speed=%.2f"), *BoneName, NewOverride.InterpolationSpeedPerSec));
			Modified++;
		}
	}

	return Modified;
}

int32 FEditBlendSpaceTool::RemovePerBoneOverrides(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* NamesArray,
                                                   TArray<FString>& OutResults)
{
	if (!NamesArray) return 0;

	int32 Removed = 0;

	for (const TSharedPtr<FJsonValue>& NameValue : *NamesArray)
	{
		FString BoneName;
		if (!NameValue->TryGetString(BoneName) || BoneName.IsEmpty()) continue;

		int32 FoundIndex = INDEX_NONE;
		for (int32 i = 0; i < GetMutablePerBoneOverrides(BS).Num(); ++i)
		{
			if (GetMutablePerBoneOverrides(BS)[i].BoneReference.BoneName == FName(*BoneName))
			{
				FoundIndex = i;
				break;
			}
		}

		if (FoundIndex != INDEX_NONE)
		{
			GetMutablePerBoneOverrides(BS).RemoveAt(FoundIndex);
			OutResults.Add(FString::Printf(TEXT("- PerBoneOverride: '%s'"), *BoneName));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! remove_per_bone_overrides: '%s' not found"), *BoneName));
		}
	}

	return Removed;
}

bool FEditBlendSpaceTool::SetBlendProfile(UBlendSpace* BS, const TSharedPtr<FJsonObject>& ProfileObj,
                                           TArray<FString>& OutResults)
{
	bool bAnySet = false;

	FString ProfilePath;
	if (ProfileObj->TryGetStringField(TEXT("blend_profile"), ProfilePath) && !ProfilePath.IsEmpty())
	{
		UBlendProfile* Profile = NeoStackToolUtils::LoadAssetWithFallback<UBlendProfile>(ProfilePath);
		if (!Profile && !ProfilePath.StartsWith(TEXT("/")))
		{
			Profile = NeoStackToolUtils::LoadAssetWithFallback<UBlendProfile>(TEXT("/Game/") + ProfilePath);
		}
		if (Profile)
		{
			GetMutableBlendProfile(BS).BlendProfile = Profile;
			OutResults.Add(FString::Printf(TEXT("= BlendProfile: %s"), *Profile->GetName()));
			bAnySet = true;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! Blend profile not found: %s"), *ProfilePath));
		}
	}

	double WeightSpeed;
	if (ProfileObj->TryGetNumberField(TEXT("target_weight_interpolation_speed"), WeightSpeed))
	{
		GetMutableBlendProfile(BS).TargetWeightInterpolationSpeedPerSec = FMath::Max(0.0f, static_cast<float>(WeightSpeed));
		OutResults.Add(FString::Printf(TEXT("= BlendProfile TargetWeightSpeed: %.2f"), GetMutableBlendProfile(BS).TargetWeightInterpolationSpeedPerSec));
		bAnySet = true;
	}

	return bAnySet;
}

// ============================================================================
// HELPERS
// ============================================================================

bool FEditBlendSpaceTool::Is1D(const UBlendSpace* BS) const
{
	return BS->IsA<UBlendSpace1D>();
}

FVector FEditBlendSpaceTool::ParseSamplePosition(const TSharedPtr<FJsonValue>& PosValue, bool bIs1D, bool& bOutValid) const
{
	bOutValid = false;

	if (bIs1D)
	{
		// 1D: accept a single number
		double X;
		if (PosValue->TryGetNumber(X))
		{
			bOutValid = true;
			return FVector(X, 0.0, 0.0);
		}

		// Also accept [x] or [x, y] array (use only X)
		const TArray<TSharedPtr<FJsonValue>>* PosArray;
		if (PosValue->TryGetArray(PosArray) && PosArray->Num() >= 1)
		{
			double X2;
			if ((*PosArray)[0]->TryGetNumber(X2))
			{
				bOutValid = true;
				return FVector(X2, 0.0, 0.0);
			}
		}
	}
	else
	{
		// 2D: accept [x, y] array
		const TArray<TSharedPtr<FJsonValue>>* PosArray;
		if (PosValue->TryGetArray(PosArray) && PosArray->Num() >= 2)
		{
			double X, Y;
			if ((*PosArray)[0]->TryGetNumber(X) && (*PosArray)[1]->TryGetNumber(Y))
			{
				bOutValid = true;
				return FVector(X, Y, 0.0);
			}
		}

		// Also accept a single number for X (Y defaults to 0)
		double X;
		if (PosValue->TryGetNumber(X))
		{
			bOutValid = true;
			return FVector(X, 0.0, 0.0);
		}
	}

	return FVector::ZeroVector;
}

int32 FEditBlendSpaceTool::ParseAxisIndex(const TSharedPtr<FJsonValue>& AxisValue) const
{
	// Accept number
	double AxisNum;
	if (AxisValue->TryGetNumber(AxisNum))
	{
		return static_cast<int32>(AxisNum);
	}

	// Accept string "x"/"y" or "0"/"1"
	FString AxisStr;
	if (AxisValue->TryGetString(AxisStr))
	{
		AxisStr = AxisStr.ToLower().TrimStartAndEnd();
		if (AxisStr == TEXT("x") || AxisStr == TEXT("0")) return 0;
		if (AxisStr == TEXT("y") || AxisStr == TEXT("1")) return 1;
	}

	return -1; // invalid
}
