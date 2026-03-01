// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UControlRigBlueprint;
class URigHierarchy;
class URigHierarchyController;

/**
 * Tool for editing Control Rig assets — both hierarchy (bones, controls, nulls, curves,
 * connectors, sockets) and member variables.
 *
 * Parameters:
 *   name (string, required) — Asset name or path
 *   path (string, optional) — Content folder path
 *
 * Hierarchy operations:
 *   add_elements — Array of [{type, name, parent?, settings?}]
 *   remove_elements — Array of element names to remove
 *   rename_elements — Array of [{name, new_name}]
 *   reparent_elements — Array of [{name, new_parent}]
 *   set_element_settings — Array of [{name, settings}]
 *   import_hierarchy — {skeleton?, skeletal_mesh?, include_curves?}
 *   duplicate_elements — Array of element names to duplicate
 *   mirror_elements — {elements, search, replace, mirror_axis?, axis_to_flip?}
 *   reorder_elements — Array of [{name, new_index}]
 *   set_spaces — Array of [{control_name, clear?, active_space?, spaces: [string|{element, op?, label?, index?, active?}]}]
 *   set_metadata — Array of [{element, name, type, value}]
 *   remove_metadata — Array of [{element, name}]
 *   select_elements — Array of element names to select
 *   deselect_elements — Array of element names to deselect
 *   set_selection — Array of element names to become the full selection
 *   clear_selection — bool, clear current hierarchy selection
 *
 * Variable operations:
 *   add_variables — Array of [{name, type, default_value?}]
 *   remove_variables — Array of variable names
 *   set_variable_defaults — Array of [{name, value}]
 *
 * Discovery operations:
 *   list_hierarchy (bool) — List all hierarchy elements
 *   list_variables (bool) — List all variables
 *   list_selection (bool) — List currently selected hierarchy elements
 *   list_metadata (bool/array) — List metadata (all elements or specific element names)
 *   list_unit_types (string) — Search available RigUnit struct names
 */
class AGENTINTEGRATIONKIT_API FEditControlRigTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_control_rig"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Edit Control Rig assets: hierarchy (bones, controls, nulls, curves, connectors, sockets) "
			"and member variables. "
			"Use add_elements to add bones/controls/nulls/curves. "
			"Use import_hierarchy to import bones from a skeleton or skeletal mesh. "
			"Use list_hierarchy/list_variables/list_unit_types for discovery. "
			"Use edit_graph for Control Rig graph node operations.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	// --- Parse Structs ---
	struct FElementToAdd { FString Type; FString Name; FString Parent; TSharedPtr<FJsonObject> Settings; };
	struct FElementRename { FString Name; FString NewName; };
	struct FElementReparent { FString Name; FString NewParent; };
	struct FParentAdd { FString Child; FString Parent; float Weight = 0.f; bool bMaintainGlobal = true; FString Label; };
	struct FParentRemove { FString Child; FString Parent; bool bMaintainGlobal = true; };
	struct FParentClear { FString Child; bool bMaintainGlobal = true; };
	struct FComponentAdd { FString Element; FString Name; FString ComponentType; FString Content; };
	struct FComponentRemove { FString Element; FString Name; };
	struct FComponentRename { FString Element; FString Name; FString NewName; };
	struct FComponentReparent { FString Element; FString Name; FString NewElement; };
	struct FElementSettings { FString Name; TSharedPtr<FJsonObject> Settings; };
	struct FVariableToAdd { FString Name; FString Type; FString DefaultValue; };
	struct FVariableDefault { FString Name; FString Value; };
	struct FElementReorder { FString Name; int32 NewIndex; };
	struct FSpaceEntry
	{
		FString Element;
		FString Op;
		FString Label;
		int32 Index = INDEX_NONE;
		bool bHasIndex = false;
		bool bActive = false;
	};
	struct FSpaceConfig
	{
		FString ControlName;
		TArray<FSpaceEntry> Spaces;
		FString ActiveSpace;
		bool bClear = false;
	};
	struct FMirrorConfig { TArray<FString> Elements; FString Search; FString Replace; FString MirrorAxis; FString AxisToFlip; };
	struct FMetadataSet
	{
		FString Element;
		FString Name;
		FString Type;
		FString Value;
	};
	struct FMetadataRemove
	{
		FString Element;
		FString Name;
	};

	// --- Asset Resolution ---
	UControlRigBlueprint* LoadControlRigBP(const FString& Name, const FString& Path, FString& OutError);

	// --- Hierarchy Operations ---
	int32 AddElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FElementToAdd>& Elements, TArray<FString>& OutResults);
	int32 RemoveElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FString>& Names, TArray<FString>& OutResults);
	int32 RenameElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FElementRename>& Renames, TArray<FString>& OutResults);
	int32 ReparentElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FElementReparent>& Reparents, TArray<FString>& OutResults);
	int32 AddParents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FParentAdd>& Adds, TArray<FString>& OutResults);
	int32 RemoveParents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FParentRemove>& Removes, TArray<FString>& OutResults);
	int32 ClearParents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FParentClear>& Clears, TArray<FString>& OutResults);
	int32 AddComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FComponentAdd>& Adds, TArray<FString>& OutResults);
	int32 RemoveComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FComponentRemove>& Removes, TArray<FString>& OutResults);
	int32 RenameComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FComponentRename>& Renames, TArray<FString>& OutResults);
	int32 ReparentComponents(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FComponentReparent>& Reparents, TArray<FString>& OutResults);
	int32 SetElementSettings(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FElementSettings>& Settings, TArray<FString>& OutResults);
	int32 ImportHierarchy(URigHierarchyController* Ctrl, const TSharedPtr<FJsonObject>& Config, TArray<FString>& OutResults);
	int32 DuplicateElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FString>& Names, TArray<FString>& OutResults);
	int32 MirrorElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const FMirrorConfig& Config, TArray<FString>& OutResults);
	int32 ReorderElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FElementReorder>& Reorders, TArray<FString>& OutResults);
	int32 SetSpaces(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FSpaceConfig>& Configs, TArray<FString>& OutResults);
	int32 SetMetadata(URigHierarchy* Hierarchy, const TArray<FMetadataSet>& MetadataOps, TArray<FString>& OutResults);
	int32 RemoveMetadata(URigHierarchy* Hierarchy, const TArray<FMetadataRemove>& MetadataOps, TArray<FString>& OutResults);
	int32 SelectElements(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FString>& Names, bool bSelect, bool bClearFirst, TArray<FString>& OutResults);
	int32 SetSelection(URigHierarchyController* Ctrl, URigHierarchy* Hierarchy, const TArray<FString>& Names, TArray<FString>& OutResults);

	// --- Variable Operations (member variables on the blueprint) ---
	int32 AddVariables(UControlRigBlueprint* BP, const TArray<FVariableToAdd>& Vars, TArray<FString>& OutResults);
	int32 RemoveVariables(UControlRigBlueprint* BP, const TArray<FString>& Names, TArray<FString>& OutResults);
	int32 SetVariableDefaults(UControlRigBlueprint* BP, const TArray<FVariableDefault>& Defaults, TArray<FString>& OutResults);

	// --- Discovery ---
	FString ListHierarchy(URigHierarchy* Hierarchy);
	FString ListVariables(UControlRigBlueprint* BP);
	FString ListSelection(URigHierarchy* Hierarchy);
	FString ListMetadata(URigHierarchy* Hierarchy, const TArray<FString>& ElementNames);
	FString ListUnitTypes(const FString& Filter);

	// --- Helpers (defined in .cpp with full engine types) ---
	FString ResolveCPPType(const FString& TypeName);
	void MarkBPModified(UControlRigBlueprint* BP);
};
