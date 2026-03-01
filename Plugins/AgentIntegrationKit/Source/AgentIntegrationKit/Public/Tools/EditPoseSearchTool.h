// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

#if WITH_POSE_SEARCH

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
class UPoseSearchSchema;
class UPoseSearchDatabase;
class UPoseSearchNormalizationSet;
#endif

class AGENTINTEGRATIONKIT_API FEditPoseSearchTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_pose_search"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Create and edit Pose Search (Motion Matching) assets. "
			"Use asset_type=\"schema\" to create schemas with feature channels (Pose, Trajectory, Velocity, Position, Heading, Phase, Curve). "
			"Use asset_type=\"database\" to create databases and add/remove animations for pose searching. "
			"Use asset_type=\"normalization_set\" to group databases for combined normalization. "
			"Use configure_asset for properties like sample_rate, data_preprocessor, search_mode, cost biases, schema, and normalization_set.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6
	FToolResult ExecuteSchema(const TSharedPtr<FJsonObject>& Args);
	FToolResult ExecuteDatabase(const TSharedPtr<FJsonObject>& Args);
	FToolResult ExecuteNormalizationSet(const TSharedPtr<FJsonObject>& Args);

	UPoseSearchSchema* GetOrCreateSchema(const FString& Name, const FString& Path, bool& bCreated);
	UPoseSearchDatabase* GetOrCreateDatabase(const FString& Name, const FString& Path, bool& bCreated);
	UPoseSearchNormalizationSet* GetOrCreateNormalizationSet(const FString& Name, const FString& Path, bool& bCreated);

	int32 AddChannels(UPoseSearchSchema* Schema, const TArray<TSharedPtr<FJsonValue>>* ChannelsArray, TArray<FString>& OutResults);
	int32 AddAnimations(UPoseSearchDatabase* Database, const TArray<TSharedPtr<FJsonValue>>* AnimsArray, TArray<FString>& OutResults);
	int32 RemoveAnimations(UPoseSearchDatabase* Database, const TArray<TSharedPtr<FJsonValue>>* IndicesArray, TArray<FString>& OutResults);

	int32 ParseBoneFlags(const TArray<TSharedPtr<FJsonValue>>* FlagsArray);
	int32 ParseTrajectoryFlags(const TArray<TSharedPtr<FJsonValue>>* FlagsArray);
#endif
};

#endif // WITH_POSE_SEARCH
