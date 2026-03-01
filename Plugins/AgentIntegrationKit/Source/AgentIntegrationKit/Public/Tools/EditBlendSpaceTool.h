// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBlendSpace;
class UAnimSequence;

/**
 * Tool for creating and editing Blend Space assets (1D, 2D, Aim Offsets)
 *
 * Blend Spaces blend between animations based on input parameters (speed, direction, etc.).
 * Supports all four types: BlendSpace (2D), BlendSpace1D, AimOffset (2D), AimOffset1D.
 *
 * Parameters:
 *   - name: Blend Space asset name (required)
 *   - path: Asset folder path (optional, defaults to /Game)
 *   - type: "BlendSpace" (default), "BlendSpace1D", "AimOffset", "AimOffset1D"
 *   - skeleton: Path to USkeleton asset (required for creation)
 *
 * Axis Configuration:
 *   - set_axis: [{axis: 0/"x" or 1/"y", display_name, min, max, grid_divisions, snap_to_grid, wrap_input}]
 *
 * Sample Management:
 *   - add_samples: [{animation, position (float for 1D, [x,y] for 2D), rate_scale, use_single_frame, frame_index}]
 *   - remove_samples: [indices] — processed in reverse order
 *   - edit_samples: [{index, position?, animation?, rate_scale?, use_single_frame?, frame_index?}]
 *   - duplicate_sample: {index, position} — copy sample to new position
 *
 * Properties & Interpolation:
 *   Use configure_asset tool to set BlendSpace properties and per-axis interpolation
 *   via reflection (e.g. InterpolationParam[0].InterpolationTime).
 *
 * Per-Bone Blending:
 *   - per_bone_blend_mode: "Manual" or "BlendProfile"
 *   - set_per_bone_overrides: [{bone, interpolation_speed}]
 *   - remove_per_bone_overrides: ["bone1", "bone2"]
 *   - set_blend_profile: {blend_profile: "/Game/Path", target_weight_interpolation_speed}
 */
class AGENTINTEGRATIONKIT_API FEditBlendSpaceTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_blend_space"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create and edit Blend Spaces (1D/2D) and Aim Offsets for animation blending. "
			"Add/remove/edit animation samples at blend positions, configure axis parameters "
			"(range, grid, snap, wrap), and set interpolation smoothing. "
			"Use set_axis to configure X/Y ranges before adding samples. "
			"Supports all four types: BlendSpace, BlendSpace1D, AimOffset, AimOffset1D. "
			"For per-bone smoothing, use per_bone_blend_mode + set_per_bone_overrides or set_blend_profile.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// Asset management
	UBlendSpace* GetOrLoadBlendSpace(const FString& Name, const FString& Path);
	UBlendSpace* CreateBlendSpaceAsset(const FString& Name, const FString& Path,
	                                    const FString& Type, const FString& SkeletonPath,
	                                    TArray<FString>& OutResults);

	// Sample operations
	int32 AddSamples(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* SamplesArray,
	                 TArray<FString>& OutResults);
	int32 RemoveSamples(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* IndicesArray,
	                    TArray<FString>& OutResults);
	int32 EditSamples(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* SamplesArray,
	                  TArray<FString>& OutResults);
	bool DuplicateSample(UBlendSpace* BS, const TSharedPtr<FJsonObject>& DupObj,
	                     TArray<FString>& OutResults);

	// Configuration
	int32 SetAxes(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* AxesArray,
	              TArray<FString>& OutResults);

	// Per-bone blend
	int32 SetPerBoneOverrides(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* OverridesArray,
	                          TArray<FString>& OutResults);
	int32 RemovePerBoneOverrides(UBlendSpace* BS, const TArray<TSharedPtr<FJsonValue>>* NamesArray,
	                             TArray<FString>& OutResults);
	bool SetBlendProfile(UBlendSpace* BS, const TSharedPtr<FJsonObject>& ProfileObj,
	                     TArray<FString>& OutResults);

	// Helpers
	bool Is1D(const UBlendSpace* BS) const;
	FVector ParseSamplePosition(const TSharedPtr<FJsonValue>& PosValue, bool bIs1D, bool& bOutValid) const;
	int32 ParseAxisIndex(const TSharedPtr<FJsonValue>& AxisValue) const;
};
