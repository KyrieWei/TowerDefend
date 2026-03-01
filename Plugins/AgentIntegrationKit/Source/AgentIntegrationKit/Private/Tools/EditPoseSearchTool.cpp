// Copyright 2026 Betide Studio. All Rights Reserved.

#if WITH_POSE_SEARCH

#include "Tools/EditPoseSearchTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 6

#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchRole.h"
#include "PoseSearch/PoseSearchDerivedData.h"

#include "PoseSearch/PoseSearchFeatureChannel.h"
#include "PoseSearch/PoseSearchFeatureChannel_Pose.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchFeatureChannel_Velocity.h"
#include "PoseSearch/PoseSearchFeatureChannel_Position.h"
#include "PoseSearch/PoseSearchFeatureChannel_Heading.h"
#include "PoseSearch/PoseSearchFeatureChannel_Phase.h"
#include "PoseSearch/PoseSearchFeatureChannel_Curve.h"

#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimComposite.h"
#include "Animation/AnimMontage.h"
#include "Animation/Skeleton.h"
#include "Animation/MirrorDataTable.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ScopedTransaction.h"

#if ENGINE_MINOR_VERSION < 7
#include "InstancedStruct.h"
#endif

TSharedPtr<FJsonObject> FEditPoseSearchTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Asset type
	TSharedPtr<FJsonObject> AssetTypeProp = MakeShared<FJsonObject>();
	AssetTypeProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetTypeProp->SetStringField(TEXT("description"), TEXT("Type of Pose Search asset to create/edit: \"schema\" (feature channels definition), \"database\" (indexed animation collection), or \"normalization_set\" (database grouping)"));
	TArray<TSharedPtr<FJsonValue>> AssetTypeEnum;
	AssetTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("schema")));
	AssetTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("database")));
	AssetTypeEnum.Add(MakeShared<FJsonValueString>(TEXT("normalization_set")));
	AssetTypeProp->SetArrayField(TEXT("enum"), AssetTypeEnum);
	Properties->SetObjectField(TEXT("asset_type"), AssetTypeProp);

	// Name
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	// Path
	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (defaults to /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// Schema params
	TSharedPtr<FJsonObject> SkeletonProp = MakeShared<FJsonObject>();
	SkeletonProp->SetStringField(TEXT("type"), TEXT("string"));
	SkeletonProp->SetStringField(TEXT("description"), TEXT("[schema] USkeleton or USkeletalMesh asset path (required for new schemas). If a SkeletalMesh is given, its Skeleton is used."));
	Properties->SetObjectField(TEXT("skeleton"), SkeletonProp);

	TSharedPtr<FJsonObject> MirrorProp = MakeShared<FJsonObject>();
	MirrorProp->SetStringField(TEXT("type"), TEXT("string"));
	MirrorProp->SetStringField(TEXT("description"), TEXT("[schema] UMirrorDataTable asset path for mirroring support"));
	Properties->SetObjectField(TEXT("mirror_data_table"), MirrorProp);

	TSharedPtr<FJsonObject> ChannelsProp = MakeShared<FJsonObject>();
	ChannelsProp->SetStringField(TEXT("type"), TEXT("array"));
	ChannelsProp->SetStringField(TEXT("description"),
		TEXT("[schema] Feature channels to add. Each: {type, weight, ...}. "
			"Types: Pose (bones:[{bone,flags:[Position,Velocity,Rotation,Phase],weight}]), "
			"Trajectory (samples:[{offset,flags:[Position,Velocity,VelocityDirection,FacingDirection,PositionXY,VelocityXY,VelocityDirectionXY,FacingDirectionXY],weight}]), "
			"Velocity (bone), Position (bone), Heading (bone, heading_axis:X/Y/Z), "
			"Phase (bone), Curve (curve_name)"));
	Properties->SetObjectField(TEXT("add_channels"), ChannelsProp);

	// Database params
	TSharedPtr<FJsonObject> AddAnimsProp = MakeShared<FJsonObject>();
	AddAnimsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddAnimsProp->SetStringField(TEXT("description"),
		TEXT("[database] Animations to add: [{asset (path to AnimSequence/BlendSpace/AnimComposite/AnimMontage), "
			"enabled (default true), mirror_option (UnmirroredOnly/MirroredOnly/UnmirroredAndMirrored), "
			"sampling_range: [min, max] (seconds, [0,0]=full range)}]"));
	Properties->SetObjectField(TEXT("add_animations"), AddAnimsProp);

	TSharedPtr<FJsonObject> RemoveAnimsProp = MakeShared<FJsonObject>();
	RemoveAnimsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveAnimsProp->SetStringField(TEXT("description"), TEXT("[database] Animation indices to remove (0-based, processed in descending order)"));
	Properties->SetObjectField(TEXT("remove_animations"), RemoveAnimsProp);

	// Normalization Set params
	TSharedPtr<FJsonObject> AddDBsProp = MakeShared<FJsonObject>();
	AddDBsProp->SetStringField(TEXT("type"), TEXT("array"));
	AddDBsProp->SetStringField(TEXT("description"), TEXT("[normalization_set] Database asset paths to add"));
	Properties->SetObjectField(TEXT("add_databases"), AddDBsProp);

	TSharedPtr<FJsonObject> RemoveDBsProp = MakeShared<FJsonObject>();
	RemoveDBsProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveDBsProp->SetStringField(TEXT("description"), TEXT("[normalization_set] Database indices to remove (0-based)"));
	Properties->SetObjectField(TEXT("remove_databases"), RemoveDBsProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset_type")));
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditPoseSearchTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString AssetType;
	if (!Args->TryGetStringField(TEXT("asset_type"), AssetType) || AssetType.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: asset_type (\"schema\", \"database\", or \"normalization_set\")"));
	}

	if (AssetType.Equals(TEXT("schema"), ESearchCase::IgnoreCase))
	{
		return ExecuteSchema(Args);
	}
	else if (AssetType.Equals(TEXT("database"), ESearchCase::IgnoreCase))
	{
		return ExecuteDatabase(Args);
	}
	else if (AssetType.Equals(TEXT("normalization_set"), ESearchCase::IgnoreCase))
	{
		return ExecuteNormalizationSet(Args);
	}

	return FToolResult::Fail(FString::Printf(TEXT("Unknown asset_type: %s. Use \"schema\", \"database\", or \"normalization_set\"."), *AssetType));
}

// ============================================================================
// SCHEMA
// ============================================================================

FToolResult FEditPoseSearchTool::ExecuteSchema(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}
	Args->TryGetStringField(TEXT("path"), Path);

	bool bCreated = false;
	UPoseSearchSchema* Schema = GetOrCreateSchema(Name, Path, bCreated);
	if (!Schema)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create or load schema: %s"), *Name));
	}

	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditPoseSearchSchema", "AI Edit Pose Search Schema: {0}"),
		FText::FromString(Name)));

	// CRITICAL: Enable undo/redo tracking (engine pattern)
	Schema->Modify();

	TArray<FString> Results;
	int32 TotalChanges = 0;

	if (bCreated)
	{
		Results.Add(TEXT("Created new schema"));
		TotalChanges++;
	}

	// Skeleton
	FString SkeletonPath;
	if (Args->TryGetStringField(TEXT("skeleton"), SkeletonPath) && !SkeletonPath.IsEmpty())
	{
		FString FullPath = NeoStackToolUtils::BuildAssetPath(SkeletonPath, TEXT(""));

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *FullPath);
		if (!Skeleton)
		{
			USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *FullPath);
			if (SkelMesh)
			{
				Skeleton = SkelMesh->GetSkeleton();
			}
		}

		if (!Skeleton)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Skeleton not found: %s"), *FullPath));
		}

		UMirrorDataTable* MirrorTable = nullptr;
		FString MirrorPath;
		if (Args->TryGetStringField(TEXT("mirror_data_table"), MirrorPath) && !MirrorPath.IsEmpty())
		{
			FString FullMirrorPath = NeoStackToolUtils::BuildAssetPath(MirrorPath, TEXT(""));
			MirrorTable = LoadObject<UMirrorDataTable>(nullptr, *FullMirrorPath);
			if (!MirrorTable)
			{
				Results.Add(FString::Printf(TEXT("Warning: MirrorDataTable not found: %s"), *FullMirrorPath));
			}
		}

		if (Schema->GetRoledSkeletons().Num() > 0)
		{
			// Skeletons is private in 5.6+; clear via property reflection
			FProperty* SkeletonsProp = UPoseSearchSchema::StaticClass()->FindPropertyByName(TEXT("Skeletons"));
			if (SkeletonsProp)
			{
				void* ValuePtr = SkeletonsProp->ContainerPtrToValuePtr<void>(Schema);
				SkeletonsProp->ClearValue(ValuePtr);
			}
		}
		Schema->AddSkeleton(Skeleton, MirrorTable);
		Results.Add(FString::Printf(TEXT("Set skeleton: %s"), *Skeleton->GetName()));
		TotalChanges++;
	}

	// Channels
	const TArray<TSharedPtr<FJsonValue>>* ChannelsArray;
	if (Args->TryGetArrayField(TEXT("add_channels"), ChannelsArray))
	{
		TotalChanges += AddChannels(Schema, ChannelsArray, Results);
	}

	if (bCreated && Schema->GetChannels().Num() == 0)
	{
		const TArray<TSharedPtr<FJsonValue>>* ChannelsArrayCheck;
		if (!Args->TryGetArrayField(TEXT("add_channels"), ChannelsArrayCheck))
		{
			Schema->AddDefaultChannels();
			Results.Add(TEXT("Added default channels (Pose + Trajectory) - no channels were specified"));
			TotalChanges++;
		}
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified for schema. Use skeleton, add_channels, or configure_asset for other properties."));
	}

	Schema->PostEditChange();
	Schema->GetPackage()->MarkPackageDirty();

	FString Summary;
	Summary += FString::Printf(TEXT("Schema: %s\n"), *Schema->GetPathName());
	Summary += FString::Printf(TEXT("Sample Rate: %d\n"), Schema->SampleRate);
	Summary += FString::Printf(TEXT("Skeletons: %d\n"), Schema->GetRoledSkeletons().Num());
	Summary += FString::Printf(TEXT("\nChanges (%d):\n"), TotalChanges);
	for (const FString& Result : Results)
	{
		Summary += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Summary);
}

// ============================================================================
// DATABASE
// ============================================================================

FToolResult FEditPoseSearchTool::ExecuteDatabase(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}
	Args->TryGetStringField(TEXT("path"), Path);

	bool bCreated = false;
	UPoseSearchDatabase* Database = GetOrCreateDatabase(Name, Path, bCreated);
	if (!Database)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create or load database: %s"), *Name));
	}

	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditPoseSearchDB", "AI Edit Pose Search Database: {0}"),
		FText::FromString(Name)));

	// CRITICAL: Enable undo/redo tracking (engine pattern)
	Database->Modify();

	TArray<FString> Results;
	int32 TotalChanges = 0;

	if (bCreated)
	{
		Results.Add(TEXT("Created new database"));
		TotalChanges++;
	}

	// Add animations
	const TArray<TSharedPtr<FJsonValue>>* AddAnimsArray;
	if (Args->TryGetArrayField(TEXT("add_animations"), AddAnimsArray))
	{
		TotalChanges += AddAnimations(Database, AddAnimsArray, Results);
	}

	// Remove animations
	const TArray<TSharedPtr<FJsonValue>>* RemoveAnimsArray;
	if (Args->TryGetArrayField(TEXT("remove_animations"), RemoveAnimsArray))
	{
		TotalChanges += RemoveAnimations(Database, RemoveAnimsArray, Results);
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified for database. Use add_animations, remove_animations, or configure_asset for properties like schema, search_mode, cost biases."));
	}

	if (!Database->Schema)
	{
		Results.Add(TEXT("Warning: Database has no schema assigned. Set 'schema' parameter to enable pose searching."));
	}

	Database->PostEditChange();
	Database->GetPackage()->MarkPackageDirty();

	UE::PoseSearch::FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database, UE::PoseSearch::ERequestAsyncBuildFlag::NewRequest);

	FString Summary;
	Summary += FString::Printf(TEXT("Database: %s\n"), *Database->GetPathName());
	Summary += FString::Printf(TEXT("Schema: %s\n"), Database->Schema ? *Database->Schema->GetName() : TEXT("None"));
	Summary += FString::Printf(TEXT("Animations: %d\n"), Database->GetNumAnimationAssets());
	Summary += FString::Printf(TEXT("\nChanges (%d):\n"), TotalChanges);
	for (const FString& Result : Results)
	{
		Summary += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Summary);
}

// ============================================================================
// NORMALIZATION SET
// ============================================================================

FToolResult FEditPoseSearchTool::ExecuteNormalizationSet(const TSharedPtr<FJsonObject>& Args)
{
	FString Name, Path;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}
	Args->TryGetStringField(TEXT("path"), Path);

	bool bCreated = false;
	UPoseSearchNormalizationSet* NormSet = GetOrCreateNormalizationSet(Name, Path, bCreated);
	if (!NormSet)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Failed to create or load normalization set: %s"), *Name));
	}

	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditPoseSearchNormSet", "AI Edit Pose Search Normalization Set: {0}"),
		FText::FromString(Name)));

	// CRITICAL: Enable undo/redo tracking (engine pattern)
	NormSet->Modify();

	TArray<FString> Results;
	int32 TotalChanges = 0;

	if (bCreated)
	{
		Results.Add(TEXT("Created new normalization set"));
		TotalChanges++;
	}

	// Add databases
	const TArray<TSharedPtr<FJsonValue>>* AddDBsArray;
	if (Args->TryGetArrayField(TEXT("add_databases"), AddDBsArray))
	{
		for (const TSharedPtr<FJsonValue>& DBValue : *AddDBsArray)
		{
			FString DBPath;
			if (!DBValue->TryGetString(DBPath) || DBPath.IsEmpty())
			{
				continue;
			}

			FString FullDBPath = NeoStackToolUtils::BuildAssetPath(DBPath, TEXT(""));
			UPoseSearchDatabase* DB = LoadObject<UPoseSearchDatabase>(nullptr, *FullDBPath);
			if (DB)
			{
				NormSet->Databases.AddUnique(DB);
				Results.Add(FString::Printf(TEXT("Added database: %s"), *DB->GetName()));
				TotalChanges++;
			}
			else
			{
				Results.Add(FString::Printf(TEXT("Database not found: %s"), *FullDBPath));
			}
		}
	}

	// Remove databases
	const TArray<TSharedPtr<FJsonValue>>* RemoveDBsArray;
	if (Args->TryGetArrayField(TEXT("remove_databases"), RemoveDBsArray))
	{
		TArray<int32> Indices;
		for (const TSharedPtr<FJsonValue>& IdxValue : *RemoveDBsArray)
		{
			int32 Idx = 0;
			if (IdxValue->TryGetNumber(Idx))
			{
				Indices.AddUnique(Idx);
			}
		}
		Indices.Sort([](int32 A, int32 B) { return A > B; });

		for (int32 Idx : Indices)
		{
			if (Idx >= 0 && Idx < NormSet->Databases.Num())
			{
				Results.Add(FString::Printf(TEXT("Removed database at index %d"), Idx));
				NormSet->Databases.RemoveAt(Idx);
				TotalChanges++;
			}
			else
			{
				Results.Add(FString::Printf(TEXT("Invalid database index: %d"), Idx));
			}
		}
	}

	if (TotalChanges == 0)
	{
		return FToolResult::Fail(TEXT("No operations specified for normalization set. Use add_databases or remove_databases."));
	}

	// CRITICAL: Notify of property changes and mark dirty (engine pattern)
	NormSet->PostEditChange();
	NormSet->GetPackage()->MarkPackageDirty();

	FString Summary;
	Summary += FString::Printf(TEXT("Normalization Set: %s\n"), *NormSet->GetPathName());
	Summary += FString::Printf(TEXT("Databases: %d\n"), NormSet->Databases.Num());
	Summary += FString::Printf(TEXT("\nChanges (%d):\n"), TotalChanges);
	for (const FString& Result : Results)
	{
		Summary += FString::Printf(TEXT("  %s\n"), *Result);
	}

	return FToolResult::Ok(Summary);
}

// ============================================================================
// CHANNEL OPERATIONS
// ============================================================================

int32 FEditPoseSearchTool::AddChannels(UPoseSearchSchema* Schema, const TArray<TSharedPtr<FJsonValue>>* ChannelsArray, TArray<FString>& OutResults)
{
	if (!ChannelsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& ChannelValue : *ChannelsArray)
	{
		const TSharedPtr<FJsonObject>* ChannelObj;
		if (!ChannelValue->TryGetObject(ChannelObj))
		{
			continue;
		}

		FString Type;
		if (!(*ChannelObj)->TryGetStringField(TEXT("type"), Type) || Type.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped channel with no type"));
			continue;
		}

		double Weight = 1.0;
		(*ChannelObj)->TryGetNumberField(TEXT("weight"), Weight);

		UPoseSearchFeatureChannel* Channel = nullptr;

		if (Type.Equals(TEXT("Pose"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Pose* PoseChannel = NewObject<UPoseSearchFeatureChannel_Pose>(Schema, NAME_None, RF_Transactional);
			PoseChannel->Weight = static_cast<float>(Weight);

			const TArray<TSharedPtr<FJsonValue>>* BonesArray;
			if ((*ChannelObj)->TryGetArrayField(TEXT("bones"), BonesArray))
			{
				for (const TSharedPtr<FJsonValue>& BoneValue : *BonesArray)
				{
					const TSharedPtr<FJsonObject>* BoneObj;
					if (!BoneValue->TryGetObject(BoneObj))
					{
						continue;
					}

					FPoseSearchBone Bone;

					FString BoneName;
					if ((*BoneObj)->TryGetStringField(TEXT("bone"), BoneName))
					{
						Bone.Reference.BoneName = FName(*BoneName);
					}

					const TArray<TSharedPtr<FJsonValue>>* FlagsArray;
					if ((*BoneObj)->TryGetArrayField(TEXT("flags"), FlagsArray))
					{
						Bone.Flags = ParseBoneFlags(FlagsArray);
					}

					double BoneWeight = 1.0;
					if ((*BoneObj)->TryGetNumberField(TEXT("weight"), BoneWeight))
					{
						Bone.Weight = static_cast<float>(BoneWeight);
					}

					PoseChannel->SampledBones.Add(Bone);
				}
			}

			Channel = PoseChannel;
			OutResults.Add(FString::Printf(TEXT("Added Pose channel (weight=%.2f, bones=%d)"),
				PoseChannel->Weight, PoseChannel->SampledBones.Num()));
		}
		else if (Type.Equals(TEXT("Trajectory"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Trajectory* TrajChannel = NewObject<UPoseSearchFeatureChannel_Trajectory>(Schema, NAME_None, RF_Transactional);
			TrajChannel->Weight = static_cast<float>(Weight);

			const TArray<TSharedPtr<FJsonValue>>* SamplesArray;
			if ((*ChannelObj)->TryGetArrayField(TEXT("samples"), SamplesArray))
			{
				for (const TSharedPtr<FJsonValue>& SampleValue : *SamplesArray)
				{
					const TSharedPtr<FJsonObject>* SampleObj;
					if (!SampleValue->TryGetObject(SampleObj))
					{
						continue;
					}

					FPoseSearchTrajectorySample Sample;

					double Offset = 0.0;
					(*SampleObj)->TryGetNumberField(TEXT("offset"), Offset);
					Sample.Offset = static_cast<float>(Offset);

					const TArray<TSharedPtr<FJsonValue>>* FlagsArray;
					if ((*SampleObj)->TryGetArrayField(TEXT("flags"), FlagsArray))
					{
						Sample.Flags = ParseTrajectoryFlags(FlagsArray);
					}

					double SampleWeight = 1.0;
					if ((*SampleObj)->TryGetNumberField(TEXT("weight"), SampleWeight))
					{
						Sample.Weight = static_cast<float>(SampleWeight);
					}

					TrajChannel->Samples.Add(Sample);
				}
			}

			Channel = TrajChannel;
			OutResults.Add(FString::Printf(TEXT("Added Trajectory channel (weight=%.2f, samples=%d)"),
				TrajChannel->Weight, TrajChannel->Samples.Num()));
		}
		else if (Type.Equals(TEXT("Velocity"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Velocity* VelChannel = NewObject<UPoseSearchFeatureChannel_Velocity>(Schema, NAME_None, RF_Transactional);
			VelChannel->Weight = static_cast<float>(Weight);

			FString BoneName;
			if ((*ChannelObj)->TryGetStringField(TEXT("bone"), BoneName))
			{
				VelChannel->Bone.BoneName = FName(*BoneName);
			}

			Channel = VelChannel;
			OutResults.Add(FString::Printf(TEXT("Added Velocity channel (weight=%.2f, bone=%s)"),
				VelChannel->Weight, *BoneName));
		}
		else if (Type.Equals(TEXT("Position"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Position* PosChannel = NewObject<UPoseSearchFeatureChannel_Position>(Schema, NAME_None, RF_Transactional);
			PosChannel->Weight = static_cast<float>(Weight);

			FString BoneName;
			if ((*ChannelObj)->TryGetStringField(TEXT("bone"), BoneName))
			{
				PosChannel->Bone.BoneName = FName(*BoneName);
			}

			Channel = PosChannel;
			OutResults.Add(FString::Printf(TEXT("Added Position channel (weight=%.2f, bone=%s)"),
				PosChannel->Weight, *BoneName));
		}
		else if (Type.Equals(TEXT("Heading"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Heading* HeadChannel = NewObject<UPoseSearchFeatureChannel_Heading>(Schema, NAME_None, RF_Transactional);
			HeadChannel->Weight = static_cast<float>(Weight);

			FString BoneName;
			if ((*ChannelObj)->TryGetStringField(TEXT("bone"), BoneName))
			{
				HeadChannel->Bone.BoneName = FName(*BoneName);
			}

			FString AxisStr;
			if ((*ChannelObj)->TryGetStringField(TEXT("heading_axis"), AxisStr))
			{
				if (AxisStr.Equals(TEXT("Y"), ESearchCase::IgnoreCase))
				{
					HeadChannel->HeadingAxis = EHeadingAxis::Y;
				}
				else if (AxisStr.Equals(TEXT("Z"), ESearchCase::IgnoreCase))
				{
					HeadChannel->HeadingAxis = EHeadingAxis::Z;
				}
			}

			Channel = HeadChannel;
			OutResults.Add(FString::Printf(TEXT("Added Heading channel (weight=%.2f, bone=%s)"),
				HeadChannel->Weight, *BoneName));
		}
		else if (Type.Equals(TEXT("Phase"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Phase* PhaseChannel = NewObject<UPoseSearchFeatureChannel_Phase>(Schema, NAME_None, RF_Transactional);
			PhaseChannel->Weight = static_cast<float>(Weight);

			FString BoneName;
			if ((*ChannelObj)->TryGetStringField(TEXT("bone"), BoneName))
			{
				PhaseChannel->Bone.BoneName = FName(*BoneName);
			}

			Channel = PhaseChannel;
			OutResults.Add(FString::Printf(TEXT("Added Phase channel (weight=%.2f)"), PhaseChannel->Weight));
		}
		else if (Type.Equals(TEXT("Curve"), ESearchCase::IgnoreCase))
		{
			UPoseSearchFeatureChannel_Curve* CurveChannel = NewObject<UPoseSearchFeatureChannel_Curve>(Schema, NAME_None, RF_Transactional);
			CurveChannel->Weight = static_cast<float>(Weight);

			FString CurveName;
			if ((*ChannelObj)->TryGetStringField(TEXT("curve_name"), CurveName))
			{
				CurveChannel->CurveName = FName(*CurveName);
			}

			Channel = CurveChannel;
			OutResults.Add(FString::Printf(TEXT("Added Curve channel (weight=%.2f, curve=%s)"),
				CurveChannel->Weight, *CurveName));
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Unknown channel type: %s"), *Type));
			continue;
		}

		if (Channel)
		{
			Schema->AddChannel(Channel);
			Added++;
		}
	}

	return Added;
}

int32 FEditPoseSearchTool::ParseBoneFlags(const TArray<TSharedPtr<FJsonValue>>* FlagsArray)
{
	if (!FlagsArray) return int32(EPoseSearchBoneFlags::Position);

	int32 Flags = 0;
	for (const TSharedPtr<FJsonValue>& FlagValue : *FlagsArray)
	{
		FString Flag;
		if (!FlagValue->TryGetString(Flag))
		{
			continue;
		}

		if (Flag.Equals(TEXT("Position"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchBoneFlags::Position;
		else if (Flag.Equals(TEXT("Velocity"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchBoneFlags::Velocity;
		else if (Flag.Equals(TEXT("Rotation"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchBoneFlags::Rotation;
		else if (Flag.Equals(TEXT("Phase"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchBoneFlags::Phase;
	}

	return Flags != 0 ? Flags : int32(EPoseSearchBoneFlags::Position);
}

int32 FEditPoseSearchTool::ParseTrajectoryFlags(const TArray<TSharedPtr<FJsonValue>>* FlagsArray)
{
	if (!FlagsArray) return int32(EPoseSearchTrajectoryFlags::Position);

	int32 Flags = 0;
	for (const TSharedPtr<FJsonValue>& FlagValue : *FlagsArray)
	{
		FString Flag;
		if (!FlagValue->TryGetString(Flag))
		{
			continue;
		}

		if (Flag.Equals(TEXT("Position"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::Position;
		else if (Flag.Equals(TEXT("Velocity"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::Velocity;
		else if (Flag.Equals(TEXT("VelocityDirection"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::VelocityDirection;
		else if (Flag.Equals(TEXT("FacingDirection"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::FacingDirection;
		else if (Flag.Equals(TEXT("VelocityXY"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::VelocityXY;
		else if (Flag.Equals(TEXT("PositionXY"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::PositionXY;
		else if (Flag.Equals(TEXT("VelocityDirectionXY"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::VelocityDirectionXY;
		else if (Flag.Equals(TEXT("FacingDirectionXY"), ESearchCase::IgnoreCase))
			Flags |= EPoseSearchTrajectoryFlags::FacingDirectionXY;
	}

	return Flags != 0 ? Flags : int32(EPoseSearchTrajectoryFlags::Position);
}

// ============================================================================
// ANIMATION OPERATIONS
// ============================================================================

static EPoseSearchMirrorOption ParseMirrorOption(const FString& Str)
{
	if (Str.Equals(TEXT("MirroredOnly"), ESearchCase::IgnoreCase))
		return EPoseSearchMirrorOption::MirroredOnly;
	if (Str.Equals(TEXT("UnmirroredAndMirrored"), ESearchCase::IgnoreCase))
		return EPoseSearchMirrorOption::UnmirroredAndMirrored;
	return EPoseSearchMirrorOption::UnmirroredOnly;
}

int32 FEditPoseSearchTool::AddAnimations(UPoseSearchDatabase* Database, const TArray<TSharedPtr<FJsonValue>>* AnimsArray, TArray<FString>& OutResults)
{
	if (!AnimsArray) return 0;

	int32 Added = 0;
	for (const TSharedPtr<FJsonValue>& AnimValue : *AnimsArray)
	{
		const TSharedPtr<FJsonObject>* AnimObj;
		if (!AnimValue->TryGetObject(AnimObj))
		{
			continue;
		}

		FString AssetPath;
		if (!(*AnimObj)->TryGetStringField(TEXT("asset"), AssetPath) || AssetPath.IsEmpty())
		{
			OutResults.Add(TEXT("Skipped animation with no asset path"));
			continue;
		}

		FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(AssetPath, TEXT(""));
		UObject* AnimAsset = LoadObject<UObject>(nullptr, *FullAssetPath);
		if (!AnimAsset)
		{
			OutResults.Add(FString::Printf(TEXT("Animation not found: %s"), *FullAssetPath));
			continue;
		}

		if (Database->Schema && Database->Schema->GetRoledSkeletons().Num() > 0)
		{
			UAnimSequenceBase* AnimSeqBase = Cast<UAnimSequenceBase>(AnimAsset);
			if (AnimSeqBase)
			{
				USkeleton* AnimSkeleton = AnimSeqBase->GetSkeleton();
				USkeleton* SchemaSkeleton = Database->Schema->GetRoledSkeletons()[0].Skeleton.Get();
				if (AnimSkeleton && SchemaSkeleton && AnimSkeleton != SchemaSkeleton)
				{
					if (!SchemaSkeleton->IsCompatibleForEditor(AnimSkeleton))
					{
						OutResults.Add(FString::Printf(TEXT("Warning: Animation '%s' skeleton (%s) may be incompatible with schema skeleton (%s)"),
							*AnimAsset->GetName(), *AnimSkeleton->GetName(), *SchemaSkeleton->GetName()));
					}
				}
			}
		}

		bool bEnabled = true;
		(*AnimObj)->TryGetBoolField(TEXT("enabled"), bEnabled);

		FString MirrorStr;
		EPoseSearchMirrorOption MirrorOption = EPoseSearchMirrorOption::UnmirroredOnly;
		if ((*AnimObj)->TryGetStringField(TEXT("mirror_option"), MirrorStr))
		{
			MirrorOption = ParseMirrorOption(MirrorStr);
		}

		FFloatInterval SamplingRange(0.f, 0.f);
		const TArray<TSharedPtr<FJsonValue>>* RangeArray;
		if ((*AnimObj)->TryGetArrayField(TEXT("sampling_range"), RangeArray) && RangeArray->Num() >= 2)
		{
			double MinVal = 0, MaxVal = 0;
			(*RangeArray)[0]->TryGetNumber(MinVal);
			(*RangeArray)[1]->TryGetNumber(MaxVal);
			SamplingRange = FFloatInterval(static_cast<float>(MinVal), static_cast<float>(MaxVal));
		}

#if ENGINE_MINOR_VERSION >= 7
		FPoseSearchDatabaseAnimationAsset Entry;
		Entry.AnimAsset = AnimAsset;
		Entry.bEnabled = bEnabled;
		Entry.MirrorOption = MirrorOption;
		Entry.SamplingRange = SamplingRange;
		Database->AddAnimationAsset(Entry);
		OutResults.Add(FString::Printf(TEXT("Added animation: %s"), *AnimAsset->GetName()));
		Added++;
#else
		bool bAdded = false;

		if (UAnimSequence* Seq = Cast<UAnimSequence>(AnimAsset))
		{
			FPoseSearchDatabaseSequence Entry;
			Entry.Sequence = Seq;
			Entry.bEnabled = bEnabled;
			Entry.MirrorOption = MirrorOption;
			Entry.SamplingRange = SamplingRange;
			Database->AddAnimationAsset(FInstancedStruct::Make(Entry));
			bAdded = true;
		}
		else if (UBlendSpace* BS = Cast<UBlendSpace>(AnimAsset))
		{
			FPoseSearchDatabaseBlendSpace Entry;
			Entry.BlendSpace = BS;
			Entry.bEnabled = bEnabled;
			Entry.MirrorOption = MirrorOption;
			Database->AddAnimationAsset(FInstancedStruct::Make(Entry));
			bAdded = true;
		}
		else if (UAnimComposite* AC = Cast<UAnimComposite>(AnimAsset))
		{
			FPoseSearchDatabaseAnimComposite Entry;
			Entry.AnimComposite = AC;
			Entry.bEnabled = bEnabled;
			Entry.MirrorOption = MirrorOption;
			Entry.SamplingRange = SamplingRange;
			Database->AddAnimationAsset(FInstancedStruct::Make(Entry));
			bAdded = true;
		}
		else if (UAnimMontage* AM = Cast<UAnimMontage>(AnimAsset))
		{
			FPoseSearchDatabaseAnimMontage Entry;
			Entry.AnimMontage = AM;
			Entry.bEnabled = bEnabled;
			Entry.MirrorOption = MirrorOption;
			Entry.SamplingRange = SamplingRange;
			Database->AddAnimationAsset(FInstancedStruct::Make(Entry));
			bAdded = true;
		}

		if (bAdded)
		{
			OutResults.Add(FString::Printf(TEXT("Added animation: %s"), *AnimAsset->GetName()));
			Added++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Unsupported animation type: %s (%s)"),
				*AnimAsset->GetName(), *AnimAsset->GetClass()->GetName()));
		}
#endif
	}

	return Added;
}

int32 FEditPoseSearchTool::RemoveAnimations(UPoseSearchDatabase* Database, const TArray<TSharedPtr<FJsonValue>>* IndicesArray, TArray<FString>& OutResults)
{
	if (!IndicesArray) return 0;

	TArray<int32> Indices;
	for (const TSharedPtr<FJsonValue>& IdxValue : *IndicesArray)
	{
		int32 Idx = 0;
		if (IdxValue->TryGetNumber(Idx))
		{
			Indices.AddUnique(Idx);
		}
	}
	Indices.Sort([](int32 A, int32 B) { return A > B; });

	int32 Removed = 0;
	for (int32 Idx : Indices)
	{
		if (Idx >= 0 && Idx < Database->GetNumAnimationAssets())
		{
			Database->RemoveAnimationAssetAt(Idx);
			OutResults.Add(FString::Printf(TEXT("Removed animation at index %d"), Idx));
			Removed++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Invalid animation index: %d"), Idx));
		}
	}

	return Removed;
}

// ============================================================================
// ASSET CREATION HELPERS
// ============================================================================

UPoseSearchSchema* FEditPoseSearchTool::GetOrCreateSchema(const FString& Name, const FString& Path, bool& bCreated)
{
	bCreated = false;
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UPoseSearchSchema* Schema = LoadObject<UPoseSearchSchema>(nullptr, *FullAssetPath);
	if (Schema) return Schema;

	FString SanitizedName;
	UPackage* Package = NeoStackToolUtils::CreateAssetPackage(Name, Path, SanitizedName);
	if (!Package) return nullptr;

	Schema = NewObject<UPoseSearchSchema>(Package, FName(*SanitizedName), RF_Public | RF_Standalone);
	if (Schema)
	{
		FAssetRegistryModule::AssetCreated(Schema);
		Package->MarkPackageDirty();
		bCreated = true;
	}
	return Schema;
}

UPoseSearchDatabase* FEditPoseSearchTool::GetOrCreateDatabase(const FString& Name, const FString& Path, bool& bCreated)
{
	bCreated = false;
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UPoseSearchDatabase* Database = LoadObject<UPoseSearchDatabase>(nullptr, *FullAssetPath);
	if (Database) return Database;

	FString SanitizedName;
	UPackage* Package = NeoStackToolUtils::CreateAssetPackage(Name, Path, SanitizedName);
	if (!Package) return nullptr;

	Database = NewObject<UPoseSearchDatabase>(Package, FName(*SanitizedName), RF_Public | RF_Standalone);
	if (Database)
	{
		FAssetRegistryModule::AssetCreated(Database);
		Package->MarkPackageDirty();
		bCreated = true;
	}
	return Database;
}

UPoseSearchNormalizationSet* FEditPoseSearchTool::GetOrCreateNormalizationSet(const FString& Name, const FString& Path, bool& bCreated)
{
	bCreated = false;
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UPoseSearchNormalizationSet* NormSet = LoadObject<UPoseSearchNormalizationSet>(nullptr, *FullAssetPath);
	if (NormSet) return NormSet;

	FString SanitizedName;
	UPackage* Package = NeoStackToolUtils::CreateAssetPackage(Name, Path, SanitizedName);
	if (!Package) return nullptr;

	NormSet = NewObject<UPoseSearchNormalizationSet>(Package, FName(*SanitizedName), RF_Public | RF_Standalone);
	if (NormSet)
	{
		FAssetRegistryModule::AssetCreated(NormSet);
		Package->MarkPackageDirty();
		bCreated = true;
	}
	return NormSet;
}

#else // UE 5.5 - Pose Search channel headers not publicly available

TSharedPtr<FJsonObject> FEditPoseSearchTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Asset name"));
	Properties->SetObjectField(TEXT("name"), NameProp);
	Schema->SetObjectField(TEXT("properties"), Properties);
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);
	return Schema;
}

FToolResult FEditPoseSearchTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	return FToolResult::Fail(TEXT("edit_pose_search tool requires Unreal Engine 5.6 or later. Pose Search feature channel headers are not publicly available in UE 5.5."));
}

#endif // ENGINE_MINOR_VERSION >= 6

#endif // WITH_POSE_SEARCH
