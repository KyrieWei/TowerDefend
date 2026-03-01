// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UPhysicsAsset;
class USkeletalBodySetup;
class UPhysicsConstraintTemplate;

/**
 * Tool for editing Physics Asset (ragdoll/collision) configurations.
 *
 * Parameters:
 *   - name: Physics Asset name or path (required)
 *   - path: Asset folder path (optional, defaults to /Game)
 *
 * Auto-Generation:
 *   - generate: {skeletal_mesh, geom_type?, min_bone_size?, create_constraints?,
 *                angular_constraint_mode?, body_for_all?, disable_collisions?}
 *     Replaces all existing bodies/constraints with auto-generated ones from the mesh.
 *
 * Body Operations:
 *   - add_bodies: [{bone, geom_type?}] — Add empty bodies on bones
 *   - remove_bodies: ["bone1", ...] — Remove bodies by bone name
 *   - set_physics_type: [{bone, type: "default"|"kinematic"|"simulated"}]
 *
 * Shape Operations:
 *   - add_shapes: [{bone, type: "sphere"|"box"|"capsule"|"tapered_capsule",
 *                   ...shape params, center?: [x,y,z], rotation?: [pitch,yaw,roll]}]
 *     sphere: radius | box: x, y, z | capsule: radius, length | tapered_capsule: radius0, radius1, length
 *   - remove_shapes: [{bone, type, type_index}] — Per-type indexing (0-based within shape type)
 *   - edit_shapes: [{bone, type, type_index, ...new params}]
 *
 * Constraint Operations:
 *   - add_constraints: [{child_bone, parent_bone,
 *                        swing1?: {motion, limit?}, swing2?: {motion, limit?}, twist?: {motion, limit?}}]
 *     motion: "free"|"limited"|"locked", limit: degrees (default 45)
 *   - remove_constraints: [{child_bone, parent_bone}]
 *   - edit_constraints: [{child_bone, parent_bone, swing1?, swing2?, twist?, disable_collision?}]
 *
 * Collision:
 *   - disable_collision: [{bone_a, bone_b}]
 *   - enable_collision: [{bone_a, bone_b}]
 *
 * Weld:
 *   - weld_bodies: [{base_bone, add_bone}] — Merge add_bone's shapes into base_bone
 */
class AGENTINTEGRATIONKIT_API FEditPhysicsAssetTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_physics_asset"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Physics Assets (ragdolls): auto-generate from skeletal mesh, "
			"add/remove bodies with collision shapes (capsule, box, sphere, tapered capsule), "
			"add/remove/configure constraints with angular limits (swing1, swing2, twist), "
			"set physics types (default, kinematic, simulated), "
			"enable/disable collision between body pairs, and weld bodies together.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// Auto-generation
	bool GenerateFromMesh(UPhysicsAsset* PhysAsset, const TSharedPtr<FJsonObject>& GenObj,
	                      TArray<FString>& OutResults);

	// Body operations
	int32 AddBodies(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                TArray<FString>& OutResults);
	int32 RemoveBodies(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                   TArray<FString>& OutResults);
	int32 SetPhysicsTypes(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                      TArray<FString>& OutResults);

	// Shape operations
	int32 AddShapes(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                TArray<FString>& OutResults);
	int32 RemoveShapes(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                   TArray<FString>& OutResults);
	int32 EditShapes(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                 TArray<FString>& OutResults);

	// Constraint operations
	int32 AddConstraints(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                     TArray<FString>& OutResults);
	int32 RemoveConstraints(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                        TArray<FString>& OutResults);
	int32 EditConstraints(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                      TArray<FString>& OutResults);

	// Collision pairs
	int32 DisableCollisionPairs(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                            TArray<FString>& OutResults);
	int32 EnableCollisionPairs(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                           TArray<FString>& OutResults);

	// Weld
	int32 WeldBodiesOp(UPhysicsAsset* PhysAsset, const TArray<TSharedPtr<FJsonValue>>* Arr,
	                   TArray<FString>& OutResults);

	// Helpers
	static void ApplyAngularLimits(UPhysicsConstraintTemplate* CS, const TSharedPtr<FJsonObject>& Obj);
	static USkeletalBodySetup* FindBodyByBone(UPhysicsAsset* PhysAsset, const FString& BoneName);
};
