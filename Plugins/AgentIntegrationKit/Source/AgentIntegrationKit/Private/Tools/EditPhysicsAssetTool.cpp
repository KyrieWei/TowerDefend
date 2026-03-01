// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditPhysicsAssetTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/ConstraintInstance.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ScopedTransaction.h"

// ============================================================================
// Helpers
// ============================================================================

static EPhysAssetFitGeomType ParseGeomType(const FString& Str)
{
	if (Str.Equals(TEXT("box"), ESearchCase::IgnoreCase)) return EFG_Box;
	if (Str.Equals(TEXT("sphere"), ESearchCase::IgnoreCase)) return EFG_Sphere;
	if (Str.Equals(TEXT("tapered_capsule"), ESearchCase::IgnoreCase)) return EFG_TaperedCapsule;
	if (Str.Equals(TEXT("convex"), ESearchCase::IgnoreCase)) return EFG_SingleConvexHull;
	if (Str.Equals(TEXT("multi_convex"), ESearchCase::IgnoreCase)) return EFG_MultiConvexHull;
	return EFG_Sphyl; // capsule is default
}

static EAngularConstraintMotion ParseAngularMotion(const FString& Str)
{
	if (Str.Equals(TEXT("free"), ESearchCase::IgnoreCase)) return ACM_Free;
	if (Str.Equals(TEXT("locked"), ESearchCase::IgnoreCase)) return ACM_Locked;
	return ACM_Limited; // default
}

static EPhysicsType ParsePhysicsType(const FString& Str)
{
	if (Str.Equals(TEXT("kinematic"), ESearchCase::IgnoreCase)) return PhysType_Kinematic;
	if (Str.Equals(TEXT("simulated"), ESearchCase::IgnoreCase)) return PhysType_Simulated;
	return PhysType_Default;
}

static FVector ParseVec3(const TArray<TSharedPtr<FJsonValue>>& Arr, const FVector& Default = FVector::ZeroVector)
{
	if (Arr.Num() < 3) return Default;
	return FVector(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
}

static FRotator ParseRot3(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	if (Arr.Num() < 3) return FRotator::ZeroRotator;
	return FRotator(Arr[0]->AsNumber(), Arr[1]->AsNumber(), Arr[2]->AsNumber());
}

USkeletalBodySetup* FEditPhysicsAssetTool::FindBodyByBone(UPhysicsAsset* PhysAsset, const FString& BoneName)
{
	int32 Idx = PhysAsset->FindBodyIndex(FName(*BoneName));
	if (Idx == INDEX_NONE || !PhysAsset->SkeletalBodySetups.IsValidIndex(Idx))
	{
		return nullptr;
	}
	return PhysAsset->SkeletalBodySetups[Idx];
}

// ============================================================================
// Schema
// ============================================================================

TSharedPtr<FJsonObject> FEditPhysicsAssetTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();

	// Required: asset name
	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("Physics Asset name or full path (required)"));
	Props->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path (defaults to /Game)"));
	Props->SetObjectField(TEXT("path"), PathProp);

	// Generate
	TSharedPtr<FJsonObject> GenProp = MakeShared<FJsonObject>();
	GenProp->SetStringField(TEXT("type"), TEXT("object"));
	GenProp->SetStringField(TEXT("description"),
		TEXT("Auto-generate bodies and constraints from a skeletal mesh. DESTRUCTIVE: replaces all existing data. "
			"{skeletal_mesh (name or path, required), geom_type?: 'capsule'|'box'|'sphere'|'tapered_capsule'|'convex' (default capsule), "
			"min_bone_size?: number (default 20), create_constraints?: bool (default true), "
			"angular_constraint_mode?: 'free'|'limited'|'locked' (default limited), "
			"body_for_all?: bool (default false), disable_collisions?: bool (default true)}"));
	Props->SetObjectField(TEXT("generate"), GenProp);

	// Body operations
	TSharedPtr<FJsonObject> AddBodiesProp = MakeShared<FJsonObject>();
	AddBodiesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddBodiesProp->SetStringField(TEXT("description"),
		TEXT("Add bodies on bones. Each: {bone (bone name, required), "
			"geom_type?: 'capsule'|'box'|'sphere' (default capsule, used if auto-fitting from mesh)}"));
	Props->SetObjectField(TEXT("add_bodies"), AddBodiesProp);

	TSharedPtr<FJsonObject> RemBodiesProp = MakeShared<FJsonObject>();
	RemBodiesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemBodiesProp->SetStringField(TEXT("description"),
		TEXT("Remove bodies by bone name: [\"bone1\", \"bone2\"]"));
	Props->SetObjectField(TEXT("remove_bodies"), RemBodiesProp);

	TSharedPtr<FJsonObject> SetTypeProp = MakeShared<FJsonObject>();
	SetTypeProp->SetStringField(TEXT("type"), TEXT("array"));
	SetTypeProp->SetStringField(TEXT("description"),
		TEXT("Set physics type per body. Each: {bone, type: 'default'|'kinematic'|'simulated'}"));
	Props->SetObjectField(TEXT("set_physics_type"), SetTypeProp);

	// Shape operations
	TSharedPtr<FJsonObject> AddShapesProp = MakeShared<FJsonObject>();
	AddShapesProp->SetStringField(TEXT("type"), TEXT("array"));
	AddShapesProp->SetStringField(TEXT("description"),
		TEXT("Add collision shapes to existing bodies. Each: {bone, type: 'sphere'|'box'|'capsule'|'tapered_capsule', "
			"center?: [x,y,z], rotation?: [pitch,yaw,roll], "
			"sphere: radius | box: x, y, z | capsule: radius, length | tapered_capsule: radius0, radius1, length}"));
	Props->SetObjectField(TEXT("add_shapes"), AddShapesProp);

	TSharedPtr<FJsonObject> RemShapesProp = MakeShared<FJsonObject>();
	RemShapesProp->SetStringField(TEXT("type"), TEXT("array"));
	RemShapesProp->SetStringField(TEXT("description"),
		TEXT("Remove shapes from bodies. Each: {bone, type: 'sphere'|'box'|'capsule'|'tapered_capsule', "
			"type_index: int (0-based index within that shape type on the body)}"));
	Props->SetObjectField(TEXT("remove_shapes"), RemShapesProp);

	TSharedPtr<FJsonObject> EditShapesProp = MakeShared<FJsonObject>();
	EditShapesProp->SetStringField(TEXT("type"), TEXT("array"));
	EditShapesProp->SetStringField(TEXT("description"),
		TEXT("Edit existing shapes. Each: {bone, type, type_index, ...new values for center, rotation, radius, etc.}"));
	Props->SetObjectField(TEXT("edit_shapes"), EditShapesProp);

	// Constraint operations
	TSharedPtr<FJsonObject> AddConProp = MakeShared<FJsonObject>();
	AddConProp->SetStringField(TEXT("type"), TEXT("array"));
	AddConProp->SetStringField(TEXT("description"),
		TEXT("Add constraints between body pairs. Each: {child_bone, parent_bone, "
			"swing1?: {motion: 'free'|'limited'|'locked', limit?: degrees (default 45)}, "
			"swing2?: {motion, limit?}, twist?: {motion, limit?}}. "
			"Auto-disables collision between constrained bodies."));
	Props->SetObjectField(TEXT("add_constraints"), AddConProp);

	TSharedPtr<FJsonObject> RemConProp = MakeShared<FJsonObject>();
	RemConProp->SetStringField(TEXT("type"), TEXT("array"));
	RemConProp->SetStringField(TEXT("description"),
		TEXT("Remove constraints by bone pair. Each: {child_bone, parent_bone}"));
	Props->SetObjectField(TEXT("remove_constraints"), RemConProp);

	TSharedPtr<FJsonObject> EditConProp = MakeShared<FJsonObject>();
	EditConProp->SetStringField(TEXT("type"), TEXT("array"));
	EditConProp->SetStringField(TEXT("description"),
		TEXT("Edit constraint limits. Each: {child_bone, parent_bone, "
			"swing1?: {motion, limit?}, swing2?: {motion, limit?}, twist?: {motion, limit?}, "
			"disable_collision?: bool}"));
	Props->SetObjectField(TEXT("edit_constraints"), EditConProp);

	// Collision
	TSharedPtr<FJsonObject> DisCollProp = MakeShared<FJsonObject>();
	DisCollProp->SetStringField(TEXT("type"), TEXT("array"));
	DisCollProp->SetStringField(TEXT("description"),
		TEXT("Disable collision between body pairs. Each: {bone_a, bone_b}"));
	Props->SetObjectField(TEXT("disable_collision"), DisCollProp);

	TSharedPtr<FJsonObject> EnCollProp = MakeShared<FJsonObject>();
	EnCollProp->SetStringField(TEXT("type"), TEXT("array"));
	EnCollProp->SetStringField(TEXT("description"),
		TEXT("Enable collision between body pairs. Each: {bone_a, bone_b}"));
	Props->SetObjectField(TEXT("enable_collision"), EnCollProp);

	// Weld
	TSharedPtr<FJsonObject> WeldProp = MakeShared<FJsonObject>();
	WeldProp->SetStringField(TEXT("type"), TEXT("array"));
	WeldProp->SetStringField(TEXT("description"),
		TEXT("Weld (merge) bodies together. Each: {base_bone, add_bone}. "
			"Moves all shapes from add_bone's body into base_bone's body, "
			"reconnects constraints, and removes add_bone's body."));
	Props->SetObjectField(TEXT("weld_bodies"), WeldProp);

	Schema->SetObjectField(TEXT("properties"), Props);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ============================================================================
// Execute
// ============================================================================

FToolResult FEditPhysicsAssetTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// 1. Extract name, build path, load asset
	FString Name;
	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	FString Path;
	Args->TryGetStringField(TEXT("path"), Path);

	FString AssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UPhysicsAsset* PhysAsset = LoadObject<UPhysicsAsset>(nullptr, *AssetPath);
	if (!PhysAsset)
	{
		PhysAsset = NeoStackToolUtils::LoadAssetWithFallback<UPhysicsAsset>(AssetPath);
	}
	if (!PhysAsset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Physics Asset not found: %s"), *AssetPath));
	}

	// 2. Transaction
	const FScopedTransaction Transaction(FText::FromString(TEXT("AI Edit Physics Asset")));
	PhysAsset->Modify();

	TArray<FString> Results;
	Results.Add(FString::Printf(TEXT("Editing Physics Asset: %s"), *PhysAsset->GetPathName()));
	int32 TotalOps = 0;

	// 3. Dispatch operations (generate first — it's destructive)
	const TSharedPtr<FJsonObject>* GenObj;
	if (Args->TryGetObjectField(TEXT("generate"), GenObj))
	{
		if (GenerateFromMesh(PhysAsset, *GenObj, Results)) TotalOps++;
	}

	const TArray<TSharedPtr<FJsonValue>>* Arr;

	if (Args->TryGetArrayField(TEXT("add_bodies"), Arr))
		TotalOps += AddBodies(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_bodies"), Arr))
		TotalOps += RemoveBodies(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("set_physics_type"), Arr))
		TotalOps += SetPhysicsTypes(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("add_shapes"), Arr))
		TotalOps += AddShapes(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_shapes"), Arr))
		TotalOps += RemoveShapes(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("edit_shapes"), Arr))
		TotalOps += EditShapes(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("add_constraints"), Arr))
		TotalOps += AddConstraints(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("remove_constraints"), Arr))
		TotalOps += RemoveConstraints(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("edit_constraints"), Arr))
		TotalOps += EditConstraints(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("disable_collision"), Arr))
		TotalOps += DisableCollisionPairs(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("enable_collision"), Arr))
		TotalOps += EnableCollisionPairs(PhysAsset, Arr, Results);

	if (Args->TryGetArrayField(TEXT("weld_bodies"), Arr))
		TotalOps += WeldBodiesOp(PhysAsset, Arr, Results);

	// 4. Finalize
	if (TotalOps == 0)
	{
		return FToolResult::Fail(TEXT("No valid operations specified or all operations failed"));
	}

	PhysAsset->UpdateBodySetupIndexMap();
	PhysAsset->UpdateBoundsBodiesArray();
	PhysAsset->InvalidateAllPhysicsMeshes();
	PhysAsset->PostEditChange();
	PhysAsset->MarkPackageDirty();

	return FToolResult::Ok(FString::Join(Results, TEXT("\n")));
}

// ============================================================================
// Auto-Generation
// ============================================================================

bool FEditPhysicsAssetTool::GenerateFromMesh(UPhysicsAsset* PhysAsset,
                                              const TSharedPtr<FJsonObject>& GenObj,
                                              TArray<FString>& OutResults)
{
	if (!GenObj.IsValid())
	{
		OutResults.Add(TEXT("! generate: invalid object"));
		return false;
	}

	FString MeshName;
	if (!GenObj->TryGetStringField(TEXT("skeletal_mesh"), MeshName) || MeshName.IsEmpty())
	{
		OutResults.Add(TEXT("! generate: missing required 'skeletal_mesh'"));
		return false;
	}

	// Load skeletal mesh (try direct, then BuildAssetPath, then fallback)
	USkeletalMesh* SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshName);
	if (!SkelMesh)
	{
		FString MeshPath = NeoStackToolUtils::BuildAssetPath(MeshName, TEXT(""));
		SkelMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
	}
	if (!SkelMesh)
	{
		SkelMesh = NeoStackToolUtils::LoadAssetWithFallback<USkeletalMesh>(MeshName);
	}
	if (!SkelMesh)
	{
		OutResults.Add(FString::Printf(TEXT("! generate: skeletal mesh not found: '%s'"), *MeshName));
		return false;
	}

	// Build params from JSON
	FPhysAssetCreateParams Params;

	FString GeomStr;
	if (GenObj->TryGetStringField(TEXT("geom_type"), GeomStr))
	{
		Params.GeomType = ParseGeomType(GeomStr);
	}

	double MinBone;
	if (GenObj->TryGetNumberField(TEXT("min_bone_size"), MinBone))
	{
		Params.MinBoneSize = FMath::Max(1.0f, static_cast<float>(MinBone));
	}

	bool bCreateConstraints;
	if (GenObj->TryGetBoolField(TEXT("create_constraints"), bCreateConstraints))
	{
		Params.bCreateConstraints = bCreateConstraints;
	}

	FString AngularStr;
	if (GenObj->TryGetStringField(TEXT("angular_constraint_mode"), AngularStr))
	{
		Params.AngularConstraintMode = ParseAngularMotion(AngularStr);
	}

	bool bBodyForAll;
	if (GenObj->TryGetBoolField(TEXT("body_for_all"), bBodyForAll))
	{
		Params.bBodyForAll = bBodyForAll;
	}

	bool bDisableColl;
	if (GenObj->TryGetBoolField(TEXT("disable_collisions"), bDisableColl))
	{
		Params.bDisableCollisionsByDefault = bDisableColl;
	}

	FText ErrMsg;
	bool bSuccess = FPhysicsAssetUtils::CreateFromSkeletalMesh(
		PhysAsset, SkelMesh, Params, ErrMsg,
		/*bSetToMesh=*/ false,
		/*bShowProgress=*/ false);

	if (bSuccess)
	{
		OutResults.Add(FString::Printf(TEXT("+ Generated %d bodies, %d constraints from '%s'"),
			PhysAsset->SkeletalBodySetups.Num(),
			PhysAsset->ConstraintSetup.Num(),
			*SkelMesh->GetName()));
		return true;
	}
	else
	{
		OutResults.Add(FString::Printf(TEXT("! generate failed: %s"), *ErrMsg.ToString()));
		return false;
	}
}

// ============================================================================
// Body Operations
// ============================================================================

int32 FEditPhysicsAssetTool::AddBodies(UPhysicsAsset* PhysAsset,
                                         const TArray<TSharedPtr<FJsonValue>>* Arr,
                                         TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;

		FString BoneName;
		if (!(*ObjPtr)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			OutResults.Add(TEXT("! add_bodies: missing 'bone'"));
			continue;
		}

		if (PhysAsset->FindBodyIndex(FName(*BoneName)) != INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! add_bodies: body for '%s' already exists"), *BoneName));
			continue;
		}

		FPhysAssetCreateParams Params;
		FString GeomStr;
		if ((*ObjPtr)->TryGetStringField(TEXT("geom_type"), GeomStr))
		{
			Params.GeomType = ParseGeomType(GeomStr);
		}

		int32 NewIdx = FPhysicsAssetUtils::CreateNewBody(PhysAsset, FName(*BoneName), Params);
		if (NewIdx != INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("+ Body: '%s' (index %d)"), *BoneName, NewIdx));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! add_bodies: failed for '%s'"), *BoneName));
		}
	}

	return Count;
}

int32 FEditPhysicsAssetTool::RemoveBodies(UPhysicsAsset* PhysAsset,
                                            const TArray<TSharedPtr<FJsonValue>>* Arr,
                                            TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	// Collect indices to remove
	TArray<TPair<int32, FString>> ToRemove;
	for (const auto& Val : *Arr)
	{
		FString BoneName;
		if (!Val->TryGetString(BoneName) || BoneName.IsEmpty())
		{
			OutResults.Add(TEXT("! remove_bodies: invalid bone name"));
			continue;
		}

		int32 BodyIdx = PhysAsset->FindBodyIndex(FName(*BoneName));
		if (BodyIdx == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! remove_bodies: no body for '%s'"), *BoneName));
			continue;
		}
		ToRemove.Add(TPair<int32, FString>(BodyIdx, BoneName));
	}

	// Sort descending to avoid index invalidation
	ToRemove.Sort([](const auto& A, const auto& B) { return A.Key > B.Key; });

	for (const auto& Pair : ToRemove)
	{
		FPhysicsAssetUtils::DestroyBody(PhysAsset, Pair.Key);
		OutResults.Add(FString::Printf(TEXT("- Body: '%s'"), *Pair.Value));
	}

	return ToRemove.Num();
}

int32 FEditPhysicsAssetTool::SetPhysicsTypes(UPhysicsAsset* PhysAsset,
                                               const TArray<TSharedPtr<FJsonValue>>* Arr,
                                               TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;

		FString BoneName;
		if (!(*ObjPtr)->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			OutResults.Add(TEXT("! set_physics_type: missing 'bone'"));
			continue;
		}

		FString TypeStr;
		if (!(*ObjPtr)->TryGetStringField(TEXT("type"), TypeStr) || TypeStr.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("! set_physics_type: missing 'type' for '%s'"), *BoneName));
			continue;
		}

		USkeletalBodySetup* Body = FindBodyByBone(PhysAsset, BoneName);
		if (!Body)
		{
			OutResults.Add(FString::Printf(TEXT("! set_physics_type: no body for '%s'"), *BoneName));
			continue;
		}

		Body->Modify();
		Body->PhysicsType = ParsePhysicsType(TypeStr);
		OutResults.Add(FString::Printf(TEXT("= '%s' physics type → %s"), *BoneName, *TypeStr));
		Count++;
	}

	return Count;
}

// ============================================================================
// Shape Operations
// ============================================================================

int32 FEditPhysicsAssetTool::AddShapes(UPhysicsAsset* PhysAsset,
                                         const TArray<TSharedPtr<FJsonValue>>* Arr,
                                         TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString BoneName;
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty())
		{
			OutResults.Add(TEXT("! add_shapes: missing 'bone'"));
			continue;
		}

		FString ShapeType;
		if (!Obj->TryGetStringField(TEXT("type"), ShapeType) || ShapeType.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("! add_shapes: missing 'type' for '%s'"), *BoneName));
			continue;
		}

		USkeletalBodySetup* Body = FindBodyByBone(PhysAsset, BoneName);
		if (!Body)
		{
			OutResults.Add(FString::Printf(TEXT("! add_shapes: no body for '%s'"), *BoneName));
			continue;
		}

		Body->Modify();

		// Parse optional center and rotation
		FVector Center = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		const TArray<TSharedPtr<FJsonValue>>* CenterArr;
		if (Obj->TryGetArrayField(TEXT("center"), CenterArr))
		{
			Center = ParseVec3(*CenterArr);
		}
		const TArray<TSharedPtr<FJsonValue>>* RotArr;
		if (Obj->TryGetArrayField(TEXT("rotation"), RotArr))
		{
			Rotation = ParseRot3(*RotArr);
		}

		if (ShapeType.Equals(TEXT("sphere"), ESearchCase::IgnoreCase))
		{
			double Radius = 10.0;
			Obj->TryGetNumberField(TEXT("radius"), Radius);

			FKSphereElem Elem;
			Elem.Center = Center;
			Elem.Radius = FMath::Max(0.1f, static_cast<float>(Radius));
			Body->AggGeom.SphereElems.Add(Elem);
			OutResults.Add(FString::Printf(TEXT("+ Shape: sphere (R=%.1f) on '%s'"), Elem.Radius, *BoneName));
			Count++;
		}
		else if (ShapeType.Equals(TEXT("box"), ESearchCase::IgnoreCase))
		{
			double X = 10.0, Y = 10.0, Z = 10.0;
			Obj->TryGetNumberField(TEXT("x"), X);
			Obj->TryGetNumberField(TEXT("y"), Y);
			Obj->TryGetNumberField(TEXT("z"), Z);

			FKBoxElem Elem;
			Elem.Center = Center;
			Elem.Rotation = Rotation;
			Elem.X = FMath::Max(0.1f, static_cast<float>(X));
			Elem.Y = FMath::Max(0.1f, static_cast<float>(Y));
			Elem.Z = FMath::Max(0.1f, static_cast<float>(Z));
			Body->AggGeom.BoxElems.Add(Elem);
			OutResults.Add(FString::Printf(TEXT("+ Shape: box (%.1f x %.1f x %.1f) on '%s'"),
				Elem.X, Elem.Y, Elem.Z, *BoneName));
			Count++;
		}
		else if (ShapeType.Equals(TEXT("capsule"), ESearchCase::IgnoreCase))
		{
			double Radius = 5.0, Length = 20.0;
			Obj->TryGetNumberField(TEXT("radius"), Radius);
			Obj->TryGetNumberField(TEXT("length"), Length);

			FKSphylElem Elem;
			Elem.Center = Center;
			Elem.Rotation = Rotation;
			Elem.Radius = FMath::Max(0.1f, static_cast<float>(Radius));
			Elem.Length = FMath::Max(0.0f, static_cast<float>(Length));
			Body->AggGeom.SphylElems.Add(Elem);
			OutResults.Add(FString::Printf(TEXT("+ Shape: capsule (R=%.1f, L=%.1f) on '%s'"),
				Elem.Radius, Elem.Length, *BoneName));
			Count++;
		}
		else if (ShapeType.Equals(TEXT("tapered_capsule"), ESearchCase::IgnoreCase))
		{
			double R0 = 5.0, R1 = 3.0, Length = 20.0;
			Obj->TryGetNumberField(TEXT("radius0"), R0);
			Obj->TryGetNumberField(TEXT("radius1"), R1);
			Obj->TryGetNumberField(TEXT("length"), Length);

			FKTaperedCapsuleElem Elem;
			Elem.Center = Center;
			Elem.Rotation = Rotation;
			Elem.Radius0 = FMath::Max(0.1f, static_cast<float>(R0));
			Elem.Radius1 = FMath::Max(0.1f, static_cast<float>(R1));
			Elem.Length = FMath::Max(0.0f, static_cast<float>(Length));
			Body->AggGeom.TaperedCapsuleElems.Add(Elem);
			OutResults.Add(FString::Printf(TEXT("+ Shape: tapered_capsule (R0=%.1f, R1=%.1f, L=%.1f) on '%s'"),
				Elem.Radius0, Elem.Radius1, Elem.Length, *BoneName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! add_shapes: unknown type '%s'"), *ShapeType));
		}
	}

	return Count;
}

int32 FEditPhysicsAssetTool::RemoveShapes(UPhysicsAsset* PhysAsset,
                                            const TArray<TSharedPtr<FJsonValue>>* Arr,
                                            TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString BoneName, ShapeType;
		double TypeIndexVal = 0.0;
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty() ||
			!Obj->TryGetStringField(TEXT("type"), ShapeType) || ShapeType.IsEmpty() ||
			!Obj->TryGetNumberField(TEXT("type_index"), TypeIndexVal))
		{
			OutResults.Add(TEXT("! remove_shapes: requires bone, type, and type_index"));
			continue;
		}
		int32 TypeIndex = static_cast<int32>(TypeIndexVal);

		USkeletalBodySetup* Body = FindBodyByBone(PhysAsset, BoneName);
		if (!Body)
		{
			OutResults.Add(FString::Printf(TEXT("! remove_shapes: no body for '%s'"), *BoneName));
			continue;
		}

		Body->Modify();

		bool bRemoved = false;
		if (ShapeType.Equals(TEXT("sphere"), ESearchCase::IgnoreCase))
		{
			if (Body->AggGeom.SphereElems.IsValidIndex(TypeIndex))
			{
				Body->AggGeom.SphereElems.RemoveAt(TypeIndex);
				bRemoved = true;
			}
		}
		else if (ShapeType.Equals(TEXT("box"), ESearchCase::IgnoreCase))
		{
			if (Body->AggGeom.BoxElems.IsValidIndex(TypeIndex))
			{
				Body->AggGeom.BoxElems.RemoveAt(TypeIndex);
				bRemoved = true;
			}
		}
		else if (ShapeType.Equals(TEXT("capsule"), ESearchCase::IgnoreCase))
		{
			if (Body->AggGeom.SphylElems.IsValidIndex(TypeIndex))
			{
				Body->AggGeom.SphylElems.RemoveAt(TypeIndex);
				bRemoved = true;
			}
		}
		else if (ShapeType.Equals(TEXT("tapered_capsule"), ESearchCase::IgnoreCase))
		{
			if (Body->AggGeom.TaperedCapsuleElems.IsValidIndex(TypeIndex))
			{
				Body->AggGeom.TaperedCapsuleElems.RemoveAt(TypeIndex);
				bRemoved = true;
			}
		}

		if (bRemoved)
		{
			OutResults.Add(FString::Printf(TEXT("- Shape: %s[%d] from '%s'"), *ShapeType, TypeIndex, *BoneName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! remove_shapes: %s[%d] not found on '%s'"),
				*ShapeType, TypeIndex, *BoneName));
		}
	}

	return Count;
}

int32 FEditPhysicsAssetTool::EditShapes(UPhysicsAsset* PhysAsset,
                                          const TArray<TSharedPtr<FJsonValue>>* Arr,
                                          TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString BoneName, ShapeType;
		double TypeIndexVal = 0.0;
		if (!Obj->TryGetStringField(TEXT("bone"), BoneName) || BoneName.IsEmpty() ||
			!Obj->TryGetStringField(TEXT("type"), ShapeType) || ShapeType.IsEmpty() ||
			!Obj->TryGetNumberField(TEXT("type_index"), TypeIndexVal))
		{
			OutResults.Add(TEXT("! edit_shapes: requires bone, type, and type_index"));
			continue;
		}
		int32 TypeIndex = static_cast<int32>(TypeIndexVal);

		USkeletalBodySetup* Body = FindBodyByBone(PhysAsset, BoneName);
		if (!Body)
		{
			OutResults.Add(FString::Printf(TEXT("! edit_shapes: no body for '%s'"), *BoneName));
			continue;
		}

		Body->Modify();

		// Parse optional center/rotation overrides
		const TArray<TSharedPtr<FJsonValue>>* CenterArr;
		const TArray<TSharedPtr<FJsonValue>>* RotArr;
		bool bHasCenter = Obj->TryGetArrayField(TEXT("center"), CenterArr);
		bool bHasRotation = Obj->TryGetArrayField(TEXT("rotation"), RotArr);

		bool bEdited = false;

		if (ShapeType.Equals(TEXT("sphere"), ESearchCase::IgnoreCase))
		{
			if (!Body->AggGeom.SphereElems.IsValidIndex(TypeIndex))
			{
				OutResults.Add(FString::Printf(TEXT("! edit_shapes: sphere[%d] not found on '%s'"), TypeIndex, *BoneName));
				continue;
			}
			FKSphereElem& Elem = Body->AggGeom.SphereElems[TypeIndex];
			if (bHasCenter) Elem.Center = ParseVec3(*CenterArr);
			double Radius;
			if (Obj->TryGetNumberField(TEXT("radius"), Radius))
			{
				Elem.Radius = FMath::Max(0.1f, static_cast<float>(Radius));
			}
			bEdited = true;
		}
		else if (ShapeType.Equals(TEXT("box"), ESearchCase::IgnoreCase))
		{
			if (!Body->AggGeom.BoxElems.IsValidIndex(TypeIndex))
			{
				OutResults.Add(FString::Printf(TEXT("! edit_shapes: box[%d] not found on '%s'"), TypeIndex, *BoneName));
				continue;
			}
			FKBoxElem& Elem = Body->AggGeom.BoxElems[TypeIndex];
			if (bHasCenter) Elem.Center = ParseVec3(*CenterArr);
			if (bHasRotation) Elem.Rotation = ParseRot3(*RotArr);
			double X, Y, Z;
			if (Obj->TryGetNumberField(TEXT("x"), X)) Elem.X = FMath::Max(0.1f, static_cast<float>(X));
			if (Obj->TryGetNumberField(TEXT("y"), Y)) Elem.Y = FMath::Max(0.1f, static_cast<float>(Y));
			if (Obj->TryGetNumberField(TEXT("z"), Z)) Elem.Z = FMath::Max(0.1f, static_cast<float>(Z));
			bEdited = true;
		}
		else if (ShapeType.Equals(TEXT("capsule"), ESearchCase::IgnoreCase))
		{
			if (!Body->AggGeom.SphylElems.IsValidIndex(TypeIndex))
			{
				OutResults.Add(FString::Printf(TEXT("! edit_shapes: capsule[%d] not found on '%s'"), TypeIndex, *BoneName));
				continue;
			}
			FKSphylElem& Elem = Body->AggGeom.SphylElems[TypeIndex];
			if (bHasCenter) Elem.Center = ParseVec3(*CenterArr);
			if (bHasRotation) Elem.Rotation = ParseRot3(*RotArr);
			double Radius, Length;
			if (Obj->TryGetNumberField(TEXT("radius"), Radius)) Elem.Radius = FMath::Max(0.1f, static_cast<float>(Radius));
			if (Obj->TryGetNumberField(TEXT("length"), Length)) Elem.Length = FMath::Max(0.0f, static_cast<float>(Length));
			bEdited = true;
		}
		else if (ShapeType.Equals(TEXT("tapered_capsule"), ESearchCase::IgnoreCase))
		{
			if (!Body->AggGeom.TaperedCapsuleElems.IsValidIndex(TypeIndex))
			{
				OutResults.Add(FString::Printf(TEXT("! edit_shapes: tapered_capsule[%d] not found on '%s'"), TypeIndex, *BoneName));
				continue;
			}
			FKTaperedCapsuleElem& Elem = Body->AggGeom.TaperedCapsuleElems[TypeIndex];
			if (bHasCenter) Elem.Center = ParseVec3(*CenterArr);
			if (bHasRotation) Elem.Rotation = ParseRot3(*RotArr);
			double R0, R1, Length;
			if (Obj->TryGetNumberField(TEXT("radius0"), R0)) Elem.Radius0 = FMath::Max(0.1f, static_cast<float>(R0));
			if (Obj->TryGetNumberField(TEXT("radius1"), R1)) Elem.Radius1 = FMath::Max(0.1f, static_cast<float>(R1));
			if (Obj->TryGetNumberField(TEXT("length"), Length)) Elem.Length = FMath::Max(0.0f, static_cast<float>(Length));
			bEdited = true;
		}

		if (bEdited)
		{
			OutResults.Add(FString::Printf(TEXT("= Shape: %s[%d] on '%s'"), *ShapeType, TypeIndex, *BoneName));
			Count++;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("! edit_shapes: unknown type '%s'"), *ShapeType));
		}
	}

	return Count;
}

// ============================================================================
// Constraint Operations
// ============================================================================

void FEditPhysicsAssetTool::ApplyAngularLimits(UPhysicsConstraintTemplate* CS,
                                                 const TSharedPtr<FJsonObject>& Obj)
{
	if (!CS || !Obj.IsValid()) return;

	// SetAngularSwing1Limit(EAngularConstraintMotion, float) sets both motion type and angle
	auto ProcessLimit = [&](const FString& FieldName, auto SetLimitFunc)
	{
		const TSharedPtr<FJsonObject>* LimitObj;
		if (Obj->TryGetObjectField(FieldName, LimitObj))
		{
			EAngularConstraintMotion Motion = ACM_Limited;
			FString MotionStr;
			if ((*LimitObj)->TryGetStringField(TEXT("motion"), MotionStr))
			{
				Motion = ParseAngularMotion(MotionStr);
			}

			double Limit = 45.0;
			(*LimitObj)->TryGetNumberField(TEXT("limit"), Limit);

			(CS->DefaultInstance.*SetLimitFunc)(Motion,
				static_cast<float>(FMath::Clamp(Limit, 0.0, 180.0)));
		}
	};

	ProcessLimit(TEXT("swing1"), &FConstraintInstance::SetAngularSwing1Limit);
	ProcessLimit(TEXT("swing2"), &FConstraintInstance::SetAngularSwing2Limit);
	ProcessLimit(TEXT("twist"), &FConstraintInstance::SetAngularTwistLimit);
}

int32 FEditPhysicsAssetTool::AddConstraints(UPhysicsAsset* PhysAsset,
                                              const TArray<TSharedPtr<FJsonValue>>* Arr,
                                              TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString ChildBone, ParentBone;
		if (!Obj->TryGetStringField(TEXT("child_bone"), ChildBone) || ChildBone.IsEmpty())
		{
			OutResults.Add(TEXT("! add_constraints: missing 'child_bone'"));
			continue;
		}
		if (!Obj->TryGetStringField(TEXT("parent_bone"), ParentBone) || ParentBone.IsEmpty())
		{
			OutResults.Add(TEXT("! add_constraints: missing 'parent_bone'"));
			continue;
		}

		// Verify both bodies exist
		int32 ChildIdx = PhysAsset->FindBodyIndex(FName(*ChildBone));
		int32 ParentIdx = PhysAsset->FindBodyIndex(FName(*ParentBone));
		if (ChildIdx == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! add_constraints: no body for child '%s'"), *ChildBone));
			continue;
		}
		if (ParentIdx == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! add_constraints: no body for parent '%s'"), *ParentBone));
			continue;
		}

		// Check if constraint already exists
		if (PhysAsset->FindConstraintIndex(FName(*ChildBone), FName(*ParentBone)) != INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! add_constraints: constraint '%s' -> '%s' already exists"),
				*ChildBone, *ParentBone));
			continue;
		}

		int32 NewIdx = FPhysicsAssetUtils::CreateNewConstraint(PhysAsset, FName(*ChildBone));
		if (NewIdx == INDEX_NONE || !PhysAsset->ConstraintSetup.IsValidIndex(NewIdx))
		{
			OutResults.Add(FString::Printf(TEXT("! add_constraints: failed to create for '%s' -> '%s'"),
				*ChildBone, *ParentBone));
			continue;
		}

		UPhysicsConstraintTemplate* CS = PhysAsset->ConstraintSetup[NewIdx];
		if (!CS)
		{
			OutResults.Add(FString::Printf(TEXT("! add_constraints: null template for '%s' -> '%s'"),
				*ChildBone, *ParentBone));
			continue;
		}

		CS->DefaultInstance.ConstraintBone1 = FName(*ChildBone);
		CS->DefaultInstance.ConstraintBone2 = FName(*ParentBone);

		// Apply angular limits from JSON
		ApplyAngularLimits(CS, Obj);

		// Position constraint at bone
		CS->DefaultInstance.SnapTransformsToDefault(
			EConstraintTransformComponentFlags::All, PhysAsset);
		CS->SetDefaultProfile(CS->DefaultInstance);

		// Disable collision between constrained bodies (matching engine behavior)
		PhysAsset->DisableCollision(ChildIdx, ParentIdx);

		OutResults.Add(FString::Printf(TEXT("+ Constraint: '%s' -> '%s'"), *ChildBone, *ParentBone));
		Count++;
	}

	return Count;
}

int32 FEditPhysicsAssetTool::RemoveConstraints(UPhysicsAsset* PhysAsset,
                                                 const TArray<TSharedPtr<FJsonValue>>* Arr,
                                                 TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	// Collect indices to remove
	TArray<TPair<int32, FString>> ToRemove;

	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;

		FString ChildBone, ParentBone;
		if (!(*ObjPtr)->TryGetStringField(TEXT("child_bone"), ChildBone) || ChildBone.IsEmpty() ||
			!(*ObjPtr)->TryGetStringField(TEXT("parent_bone"), ParentBone) || ParentBone.IsEmpty())
		{
			OutResults.Add(TEXT("! remove_constraints: requires child_bone and parent_bone"));
			continue;
		}

		int32 ConIdx = PhysAsset->FindConstraintIndex(FName(*ChildBone), FName(*ParentBone));
		if (ConIdx == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! remove_constraints: no constraint '%s' -> '%s'"),
				*ChildBone, *ParentBone));
			continue;
		}

		ToRemove.Add(TPair<int32, FString>(ConIdx,
			FString::Printf(TEXT("%s -> %s"), *ChildBone, *ParentBone)));
	}

	// Sort descending
	ToRemove.Sort([](const auto& A, const auto& B) { return A.Key > B.Key; });

	for (const auto& Pair : ToRemove)
	{
		FPhysicsAssetUtils::DestroyConstraint(PhysAsset, Pair.Key);
		OutResults.Add(FString::Printf(TEXT("- Constraint: %s"), *Pair.Value));
	}

	return ToRemove.Num();
}

int32 FEditPhysicsAssetTool::EditConstraints(UPhysicsAsset* PhysAsset,
                                               const TArray<TSharedPtr<FJsonValue>>* Arr,
                                               TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;
		const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

		FString ChildBone, ParentBone;
		if (!Obj->TryGetStringField(TEXT("child_bone"), ChildBone) || ChildBone.IsEmpty() ||
			!Obj->TryGetStringField(TEXT("parent_bone"), ParentBone) || ParentBone.IsEmpty())
		{
			OutResults.Add(TEXT("! edit_constraints: requires child_bone and parent_bone"));
			continue;
		}

		int32 ConIdx = PhysAsset->FindConstraintIndex(FName(*ChildBone), FName(*ParentBone));
		if (ConIdx == INDEX_NONE || !PhysAsset->ConstraintSetup.IsValidIndex(ConIdx))
		{
			OutResults.Add(FString::Printf(TEXT("! edit_constraints: no constraint '%s' -> '%s'"),
				*ChildBone, *ParentBone));
			continue;
		}

		UPhysicsConstraintTemplate* CS = PhysAsset->ConstraintSetup[ConIdx];
		if (!CS)
		{
			OutResults.Add(FString::Printf(TEXT("! edit_constraints: null template for '%s' -> '%s'"),
				*ChildBone, *ParentBone));
			continue;
		}

		CS->Modify();

		// Apply angular limit changes
		ApplyAngularLimits(CS, Obj);

		// Optionally toggle collision
		bool bDisableColl;
		if (Obj->TryGetBoolField(TEXT("disable_collision"), bDisableColl))
		{
			int32 ChildIdx = PhysAsset->FindBodyIndex(FName(*ChildBone));
			int32 ParentIdx = PhysAsset->FindBodyIndex(FName(*ParentBone));
			if (ChildIdx != INDEX_NONE && ParentIdx != INDEX_NONE)
			{
				if (bDisableColl)
				{
					PhysAsset->DisableCollision(ChildIdx, ParentIdx);
				}
				else
				{
					PhysAsset->EnableCollision(ChildIdx, ParentIdx);
				}
			}
		}

		CS->SetDefaultProfile(CS->DefaultInstance);

		OutResults.Add(FString::Printf(TEXT("= Constraint: '%s' -> '%s'"), *ChildBone, *ParentBone));
		Count++;
	}

	return Count;
}

// ============================================================================
// Collision Pairs
// ============================================================================

int32 FEditPhysicsAssetTool::DisableCollisionPairs(UPhysicsAsset* PhysAsset,
                                                     const TArray<TSharedPtr<FJsonValue>>* Arr,
                                                     TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;

		FString BoneA, BoneB;
		if (!(*ObjPtr)->TryGetStringField(TEXT("bone_a"), BoneA) || BoneA.IsEmpty() ||
			!(*ObjPtr)->TryGetStringField(TEXT("bone_b"), BoneB) || BoneB.IsEmpty())
		{
			OutResults.Add(TEXT("! disable_collision: requires bone_a and bone_b"));
			continue;
		}

		int32 IdxA = PhysAsset->FindBodyIndex(FName(*BoneA));
		int32 IdxB = PhysAsset->FindBodyIndex(FName(*BoneB));
		if (IdxA == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! disable_collision: no body for '%s'"), *BoneA));
			continue;
		}
		if (IdxB == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! disable_collision: no body for '%s'"), *BoneB));
			continue;
		}

		PhysAsset->DisableCollision(IdxA, IdxB);
		OutResults.Add(FString::Printf(TEXT("= Collision disabled: '%s' <-> '%s'"), *BoneA, *BoneB));
		Count++;
	}

	return Count;
}

int32 FEditPhysicsAssetTool::EnableCollisionPairs(UPhysicsAsset* PhysAsset,
                                                    const TArray<TSharedPtr<FJsonValue>>* Arr,
                                                    TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;

		FString BoneA, BoneB;
		if (!(*ObjPtr)->TryGetStringField(TEXT("bone_a"), BoneA) || BoneA.IsEmpty() ||
			!(*ObjPtr)->TryGetStringField(TEXT("bone_b"), BoneB) || BoneB.IsEmpty())
		{
			OutResults.Add(TEXT("! enable_collision: requires bone_a and bone_b"));
			continue;
		}

		int32 IdxA = PhysAsset->FindBodyIndex(FName(*BoneA));
		int32 IdxB = PhysAsset->FindBodyIndex(FName(*BoneB));
		if (IdxA == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! enable_collision: no body for '%s'"), *BoneA));
			continue;
		}
		if (IdxB == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! enable_collision: no body for '%s'"), *BoneB));
			continue;
		}

		PhysAsset->EnableCollision(IdxA, IdxB);
		OutResults.Add(FString::Printf(TEXT("= Collision enabled: '%s' <-> '%s'"), *BoneA, *BoneB));
		Count++;
	}

	return Count;
}

// ============================================================================
// Weld Bodies
// ============================================================================

int32 FEditPhysicsAssetTool::WeldBodiesOp(UPhysicsAsset* PhysAsset,
                                            const TArray<TSharedPtr<FJsonValue>>* Arr,
                                            TArray<FString>& OutResults)
{
	if (!Arr) return 0;

	// Get preview skeletal mesh for the weld API
	USkeletalMesh* SkelMesh = PhysAsset->GetPreviewMesh();
	if (!SkelMesh)
	{
		OutResults.Add(TEXT("! weld_bodies: no preview skeletal mesh set on physics asset"));
		return 0;
	}

	// Create temporary component for the weld API
	USkeletalMeshComponent* TempComp = NewObject<USkeletalMeshComponent>(GetTransientPackage());
	TempComp->SetSkeletalMesh(SkelMesh);
	TempComp->SetPhysicsAsset(PhysAsset);

	int32 Count = 0;
	for (const auto& Val : *Arr)
	{
		const TSharedPtr<FJsonObject>* ObjPtr;
		if (!Val->TryGetObject(ObjPtr)) continue;

		FString BaseBone, AddBone;
		if (!(*ObjPtr)->TryGetStringField(TEXT("base_bone"), BaseBone) || BaseBone.IsEmpty() ||
			!(*ObjPtr)->TryGetStringField(TEXT("add_bone"), AddBone) || AddBone.IsEmpty())
		{
			OutResults.Add(TEXT("! weld_bodies: requires base_bone and add_bone"));
			continue;
		}

		int32 BaseIdx = PhysAsset->FindBodyIndex(FName(*BaseBone));
		int32 AddIdx = PhysAsset->FindBodyIndex(FName(*AddBone));
		if (BaseIdx == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! weld_bodies: no body for base '%s'"), *BaseBone));
			continue;
		}
		if (AddIdx == INDEX_NONE)
		{
			OutResults.Add(FString::Printf(TEXT("! weld_bodies: no body for add '%s'"), *AddBone));
			continue;
		}
		if (BaseIdx == AddIdx)
		{
			OutResults.Add(FString::Printf(TEXT("! weld_bodies: cannot weld body to itself '%s'"), *BaseBone));
			continue;
		}

		FPhysicsAssetUtils::WeldBodies(PhysAsset, BaseIdx, AddIdx, TempComp);
		OutResults.Add(FString::Printf(TEXT("= Welded '%s' into '%s'"), *AddBone, *BaseBone));
		Count++;

		// Rebuild index map after each weld since indices change
		PhysAsset->UpdateBodySetupIndexMap();
	}

	return Count;
}
