// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BlueprintNodeBinder.h"
#include "Tools/NeoStackToolBase.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UBlueprintNodeSpawner;

/**
 * Tool for editing graph logic in Blueprint, Material, PCG, MetaSound, BehaviorTree, and EQS assets:
 * - Add nodes by spawner ID (from integrated edit_graph node discovery)
 * - Set pin default values (Blueprints) or expression/node properties (Materials, BT)
 * - Create connections between nodes
 * - References work by name (session-persistent) or GUID
 *
 * Supports: Blueprints, AnimBlueprints, Materials, MaterialFunctions, PCGGraphs, MetaSounds, BehaviorTrees, EnvironmentQueries
 * ControlRig graph operations are routed to a dedicated backend: find_nodes (RigUnit discovery),
 * add_nodes/delete_nodes/connections/disconnect/set_pins.
 *
 * Connection format: "NodeRef:PinName->NodeRef:PinName"
 * NodeRef can be: friendly name (registered) or raw GUID
 *
 * set_pins: For Blueprints sets pin default values, for Materials sets expression
 * properties dynamically using reflection, for BT nodes sets runtime properties.
 *
 * BehaviorTree specifics:
 * - BT_ACTION: spawner IDs for composites/tasks (main graph nodes)
 * - BT_SUBNODE: spawner IDs for decorators/services (require "parent" field)
 * - BT_DECGRAPH: spawner IDs for decorator sub-graph logic nodes
 * - graph_name: "decorator:<Name>" targets a Composite Decorator's sub-graph
 * - "Root" is auto-registered as a node reference for the BT root node
 */
class AGENTINTEGRATIONKIT_API FEditGraphTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("edit_graph"); }
	virtual FString GetDescription() const override
	{
		return TEXT(
			"Supports integrated node discovery: set operation='find_nodes' or provide query=[...] to search available nodes (legacy find_node behavior). "
			"Add nodes, set values, and wire connections in Blueprint/AnimBP/Material/PCG/MetaSound/BehaviorTree/ControlRig graphs. "
			"IMPORTANT: add_nodes requires a 'position' field ({x, y}) for each node. Use read_asset first to see existing node positions and sizes, then choose coordinates for new nodes. "
			"For Materials: Use read_asset first to get existing node GUIDs and positions (especially the Material output node). "
			"For PCG/MetaSound/BehaviorTree: Use edit_graph with operation='find_nodes' first to discover available nodes and get spawner IDs. "
			"For ControlRig graph nodes, use operation='find_nodes' with query to discover dynamic action IDs (CR_ACTION:...) and unit paths (CR_UNIT:/Script/...). "
			"Connection format: \"NodeRef:PinName->NodeRef:PinName\" where NodeRef is either a friendly name (from add_nodes) or a GUID (for existing nodes). "
			"add_nodes supports optional bind_to for component/member calls (engine call-on-member path). "
			"add_nodes supports optional parent for BT sub-nodes (decorators/services) and EQS tests — required for BT_SUBNODE: and EQS_TEST: spawner IDs. "
			"set_pins on BT nodes sets properties via reflection (same as set_properties did in edit_behavior_tree). "
			"move_nodes/align_nodes/layout_nodes support explicit repositioning/layout of existing nodes. "
			"add_comments: Add comment boxes (auto-wrap nodes or explicit position/size). "
			"split_pins/recombine_pins: Split struct pins (Vector->X/Y/Z) or recombine them back. "
			"add_exec_pins/remove_exec_pins: Add/remove dynamic pins on Sequence, MakeArray, promotable operators (Add, Multiply), etc. "
			"convert_pin_type: Change pin types on promotable operator nodes — [{\"node\": \"ref\", \"pin\": \"A\", \"type\": \"Integer\"}]. Omit 'type' to list available conversions. "
			"For AnimBPs, graph_name can target subgraphs: animgraph:<Graph>, statemachine:<AnimGraph>/<SM>, "
			"state:<AnimGraph>/<State>, transition:<AnimGraph>/<From->To>, custom_transition:<AnimGraph>/<From->To>, "
			"conduit:<AnimGraph>/<Conduit>, composite:<AnimGraph>/<Composite>. "
			"For BehaviorTrees, graph_name can target: decorator:<CompositeDecoratorName> for logic sub-graphs. "
			"Create Event (K2Node_CreateDelegate): set pins/add_nodes pins with {\"SelectedFunctionName\": \"MyFunction\"} to bind the delegate to a function."
		);
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Node definition from JSON */
	struct FNodeDefinition
	{
		FString SpawnerId;          // From integrated node discovery
		FString Name;               // Friendly name for referencing
		FString BindTo;             // Optional component/object property to bind function calls to
		FString Parent;             // Parent node ref for BT sub-node attachment (decorators/services)
		TSharedPtr<FJsonObject> Pins;  // Pin name -> default value
		FVector2D Position = FVector2D::ZeroVector;  // Required: agent-specified placement coordinates
	};

	/** Parsed connection */
	struct FConnectionDef
	{
		FString FromNodeRef;
		FString FromPinName;
		FString ToNodeRef;
		FString ToPinName;
	};

	/** Set pins/properties operation */
	struct FSetPinsOp
	{
		FString NodeRef;                    // Node name or GUID
		TSharedPtr<FJsonObject> Values;     // Pin/property name -> value
	};

	struct FMoveNodeOp
	{
		FString NodeRef;
		bool bHasAbsolute = false;
		int32 X = 0;
		int32 Y = 0;
		int32 DeltaX = 0;
		int32 DeltaY = 0;
	};

	struct FAlignNodesOp
	{
		TArray<FString> NodeRefs;
		FString Axis; // "x" or "y"
		FString Mode; // "min", "max", "center"
	};

	struct FLayoutNodesOp
	{
		TArray<FString> NodeRefs;
		int32 StartX = 0;
		int32 StartY = 0;
		int32 SpacingX = 400;
		int32 SpacingY = 220;
		int32 Columns = 4;
	};

	/** Result tracking */
	struct FAddedNode
	{
		FString Name;
		FString NodeType;
		FGuid Guid;
		FVector2D Position;
		TArray<FString> PinValues;  // "PinName = Value" strings
		TArray<FString> InputPins;  // Available input pin names
		TArray<FString> OutputPins; // Available output pin names
	};

	/** Connection result type - tracks how connection was made */
	enum class EConnectionResultType
	{
		Direct,         // Direct pin-to-pin connection
		Promoted,       // Type promotion was applied (e.g., float to double)
		Converted,      // Conversion node was auto-inserted
		Failed          // Connection could not be made
	};

	/** Connection result with details */
	struct FConnectionResult
	{
		bool bSuccess = false;
		EConnectionResultType Type = EConnectionResultType::Failed;
		FString Error;
		FString Details;  // e.g., "promoted float to int" or "inserted ToText node"
	};

	/** Parse a node definition from JSON */
	bool ParseNodeDefinition(const TSharedPtr<FJsonObject>& NodeObj, FNodeDefinition& OutDef, FString& OutError);

	/** Build engine-compatible binding set for member call nodes */
	bool BuildBindingsForNode(const FNodeDefinition& NodeDef, UBlueprint* Blueprint,
	                          IBlueprintNodeBinder::FBindingSet& OutBindings, FString& OutError) const;

	/** Parse connection string "NodeRef:Pin->NodeRef:Pin" */
	bool ParseConnection(const FString& ConnectionStr, FConnectionDef& OutDef, FString& OutError);

	/** Parse set_pins operation from JSON */
	bool ParseSetPinsOp(const TSharedPtr<FJsonObject>& OpObj, FSetPinsOp& OutOp, FString& OutError);
	bool ParseMoveNodeOp(const TSharedPtr<FJsonObject>& OpObj, FMoveNodeOp& OutOp, FString& OutError);
	bool ParseAlignNodesOp(const TSharedPtr<FJsonObject>& OpObj, FAlignNodesOp& OutOp, FString& OutError);
	bool ParseLayoutNodesOp(const TSharedPtr<FJsonObject>& OpObj, FLayoutNodesOp& OutOp, FString& OutError);

	/** Find spawner by signature ID */
	UBlueprintNodeSpawner* FindSpawnerById(const FString& SpawnerId, UEdGraph* Graph);

	/** Spawn a node using the spawner */
	UEdGraphNode* SpawnNode(UBlueprintNodeSpawner* Spawner, UEdGraph* Graph, const FVector2D& Position);

	/** Set default values on node pins (Blueprint) or expression properties (Material) */
	TArray<FString> SetPinValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& PinValues);

	/** Set values on existing node - dispatches to Blueprint pins or Material expression properties */
	TArray<FString> SetNodeValues(UEdGraphNode* Node, const TSharedPtr<FJsonObject>& Values, UEdGraph* Graph);

	/** Resolve a node reference (name or GUID) to actual node */
	UEdGraphNode* ResolveNodeRef(const FString& NodeRef, UEdGraph* Graph, const FString& AssetPath,
	                             const TMap<FString, UEdGraphNode*>& NewNodes);

	/** Find a pin on a node by name */
	UEdGraphPin* FindPinByName(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction);

	/** List available pins on a node for error messages */
	FString ListAvailablePins(UEdGraphNode* Node, EEdGraphPinDirection Direction);

	/** Create a connection between two pins with three-tier fallback strategy:
	 * 1. Direct connection if types match
	 * 2. Type promotion if schema supports it (e.g., float to double)
	 * 3. Auto-insert conversion node if needed (e.g., int to string)
	 */
	FConnectionResult CreateConnectionWithFallback(UEdGraphPin* FromPin, UEdGraphPin* ToPin);

	/** Legacy simple connection (for compatibility) */
	bool CreateConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError);

	/** Validate connection prerequisites */
	bool ValidateConnectionPrerequisites(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError);

	/** Break a connection between two pins */
	bool BreakConnection(UEdGraphPin* FromPin, UEdGraphPin* ToPin, FString& OutError);

	/** Break all connections on a pin */
	bool BreakAllConnections(UEdGraphPin* Pin, FString& OutError);

	/** Get the target graph from a Blueprint */
	UEdGraph* GetGraphByName(UBlueprint* Blueprint, const FString& GraphName) const;

	/** Get node type display name */
	FString GetNodeTypeName(UEdGraphNode* Node) const;

	/** Format results to output string */
	FString FormatResults(const FString& AssetName, const FString& GraphName,
	                      const TArray<FAddedNode>& AddedNodes,
	                      const TArray<FString>& DeletedNodes,
	                      const TArray<FString>& Connections,
	                      const TArray<FString>& Disconnections,
	                      const TArray<FString>& MoveResults,
	                      const TArray<FString>& AlignResults,
	                      const TArray<FString>& LayoutResults,
	                      const TArray<FString>& SetPinsResults,
	                      const TArray<FString>& Errors) const;
};
