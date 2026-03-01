// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Tool for reading and configuring asset properties using UE5 reflection system.
 * Supports ANY editable property on Materials, Blueprints, AnimBlueprints, Widgets, Components, etc.
 *
 * Three modes of operation:
 * 1. GET - Read specific property values
 * 2. LIST - Discover all editable properties on an asset
 * 3. SET - Change property values
 *
 * Uses ExportText/ImportText for dynamic property access:
 * - Enums work directly: "BLEND_Translucent", "BLEND_Masked"
 * - Booleans: "True", "False"
 * - Numbers: "0.5", "100"
 * - Structs: "(X=1,Y=2,Z=3)" for vectors, etc.
 * - No hardcoding needed - new properties automatically work
 *
 * Subobject support (widgets in Widget Blueprints, components in Blueprints, nodes in BehaviorTrees):
 * Use the "subobject" parameter to target a specific widget, component, or BT node.
 *
 * Example - Configure widget property:
 * {
 *   "name": "WBP_MainMenu",
 *   "subobject": "StartButton",
 *   "changes": [{"property": "ColorAndOpacity", "value": "(R=1,G=0,B=0,A=1)"}]
 * }
 *
 * Example - Configure component property:
 * {
 *   "name": "BP_Enemy",
 *   "subobject": "MeshComponent",
 *   "changes": [{"property": "RelativeScale3D", "value": "(X=2,Y=2,Z=2)"}]
 * }
 *
 * Example - List widget properties:
 * {
 *   "name": "WBP_MainMenu",
 *   "subobject": "TitleText",
 *   "list_properties": true
 * }
 *
 * Example - Configure BehaviorTree decorator:
 * {
 *   "name": "BT_EnemyAI",
 *   "subobject": "Check Target",
 *   "changes": [
 *     {"property": "BlackboardKey", "value": "TargetActor"},
 *     {"property": "FlowAbortMode", "value": "Both"}
 *   ]
 * }
 *
 * Example - Set material properties:
 * {
 *   "name": "M_BaseMaterial",
 *   "changes": [
 *     {"property": "BlendMode", "value": "BLEND_Translucent"},
 *     {"property": "TwoSided", "value": "true"}
 *   ]
 * }
 *
 * Graph node targeting - configure properties on nodes inside Blueprint graphs:
 * Use "graph" + "node" to target a specific node in an AnimBP, Blueprint, etc.
 *
 * Example - Configure SequencePlayer node properties (dot-notation for embedded structs):
 * {
 *   "name": "ABP_Character",
 *   "graph": "state:AnimGraph/Idle",
 *   "node": "<GUID from read_asset>",
 *   "changes": [
 *     {"property": "Node.bLoopAnimation", "value": "True"},
 *     {"property": "Node.PlayRate", "value": "1.5"}
 *   ]
 * }
 *
 * Example - Configure transition properties:
 * {
 *   "name": "ABP_Character",
 *   "graph": "transition:AnimGraph/Idle->Walk",
 *   "node": "<GUID>",
 *   "changes": [{"property": "CrossfadeDuration", "value": "0.25"}]
 * }
 *
 * For graph+node targets, "get"/"changes" also support pin defaults directly:
 * - Read pin defaults: "get": ["BlendTime"] or "get": ["pin.BlendTime"]
 * - Set pin defaults: "changes": [{"property":"BlendTime","value":"0.25"}]
 * - list_properties includes input pins as "pin.<PinName>" rows.
 */
class AGENTINTEGRATIONKIT_API FConfigureAssetTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("configure_asset"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Read and configure properties on Materials, Blueprints, AnimBlueprints, and graph nodes using reflection. "
			"Supports dot-notation for nested struct properties (e.g. Node.bLoopAnimation). "
			"For graph+node targets, unresolved get/changes paths fall back to input pin defaults by pin name (or pin.<PinName>). "
			"Supports semantic MaterialInstance parameter edits via material_instance_parameters (scalar/vector/texture/static_switch by name). "
			"Use 'graph' + 'node' params to target specific nodes inside Blueprint graphs.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Resolved property that may be inside a nested struct (for dot-notation paths) */
	struct FResolvedProperty
	{
		FProperty* Property = nullptr;
		void* ContainerPtr = nullptr;      // Container holding the property (may be struct interior, not the UObject)
		UObject* OwnerObject = nullptr;    // Top-level UObject (for ImportText context)
	};

	/** Property change request from JSON */
	struct FPropertyChange
	{
		FString PropertyName;
		FString Value;
	};

	struct FMIParameterEdit
	{
		FString Type; // scalar, vector, texture, static_switch
		FName Name;
		TSharedPtr<FJsonValue> Value;
	};

	/** Result of applying a single property change */
	struct FChangeResult
	{
		FString PropertyName;
		FString OldValue;
		FString NewValue;
		bool bSuccess;
		FString Error;
	};

	/** Property info for listing */
	struct FPropertyInfo
	{
		FString Name;
		FString Type;
		FString CurrentValue;
		FString Category;
	};

	/** Parse property changes from JSON array */
	bool ParseChanges(const TArray<TSharedPtr<FJsonValue>>& ChangesArray,
	                  TArray<FPropertyChange>& OutChanges, FString& OutError);
	bool ParseMaterialInstanceParameterEdits(const TArray<TSharedPtr<FJsonValue>>& EditArray,
		TArray<FMIParameterEdit>& OutEdits, FString& OutError);
	TArray<FChangeResult> ApplyMaterialInstanceParameterEdits(class UMaterialInstanceConstant* MaterialInstance,
		const TArray<FMIParameterEdit>& Edits);

	/** Get values of specific properties */
	TArray<TPair<FString, FString>> GetPropertyValues(UObject* Asset, const TArray<FString>& PropertyNames,
	                                                   TArray<FString>& OutErrors);

	/** List all editable properties on an asset. If bIncludeAll, includes non-EditAnywhere properties too. */
	TArray<FPropertyInfo> ListEditableProperties(UObject* Asset, bool bIncludeAll = false);

	/** Apply changes to an asset using reflection (WorkingAsset may be preview copy when editor is open) */
	TArray<FChangeResult> ApplyChanges(UObject* WorkingAsset, UObject* OriginalAsset, const TArray<FPropertyChange>& Changes);

	/** Find a property on the asset by name (case-insensitive) */
	FProperty* FindProperty(UObject* Asset, const FString& PropertyName);

	/** Get the current value of a property as string */
	FString GetPropertyValue(UObject* Asset, FProperty* Property);

	/** Set a property value from string */
	bool SetPropertyValue(UObject* Asset, FProperty* Property, const FString& Value, FString& OutError);

	/** Get property type as readable string */
	FString GetPropertyTypeName(FProperty* Property) const;

	/** Get asset type display name */
	FString GetAssetTypeName(UObject* Asset) const;

	/** Format results to output string */
	FString FormatResults(const FString& AssetName, const FString& AssetType,
	                      const TArray<TPair<FString, FString>>& GetResults,
	                      const TArray<FString>& GetErrors,
	                      const TArray<FPropertyInfo>& ListedProperties,
	                      const TArray<FChangeResult>& ChangeResults) const;

	/** Find a subobject (widget in Widget Blueprint, component in Blueprint, node in BehaviorTree) by name */
	UObject* FindSubobject(UObject* Asset, const FString& SubobjectName);

	/** Find a BT node (composite, task, decorator, service) by NodeName in a BehaviorTree */
	UObject* FindBTNodeByName(class UBehaviorTree* BehaviorTree, const FString& NodeName);

	/** Refresh the Blueprint editor when a subobject was modified */
	void RefreshBlueprintEditor(UObject* Asset);

	/** Configure slot properties for a widget (position, size, anchors, etc.) */
	FString ConfigureSlot(class UWidget* Widget, const TSharedPtr<FJsonObject>& SlotConfig, UObject* OriginalAsset);

	/** Resolve a dot-notation property path (e.g. "Node.bLoopAnimation") to the final property and container */
	FResolvedProperty ResolvePropertyPath(UObject* Object, const FString& PropertyPath);

	/** Find a graph node by GUID within a resolved graph */
	UEdGraphNode* FindGraphNode(UBlueprint* Blueprint, const FString& GraphSelector,
		const FString& NodeGuidStr, FString& OutError);

	/** Resolve a graph by typed selector string (animgraph:, statemachine:, state:, transition:, etc.) */
	UEdGraph* ResolveSubgraph(UBlueprint* Blueprint, const FString& GraphSelector);

	/** Handle post-edit for graph nodes: reconstruct pins and mark BP modified */
	void HandleGraphNodePostEdit(UEdGraphNode* Node, UBlueprint* OwningBlueprint);
};
