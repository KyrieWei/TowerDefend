// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/NeoStackToolUtils.h"
#include "Tools/NeoStackToolBase.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "ControlRigBlueprintLegacy.h"
#else
#include "ControlRigBlueprint.h"
#endif
#include "Units/RigUnit.h"
#include "RigVMModel/RigVMGraph.h"
#include "RigVMModel/RigVMController.h"
#include "RigVMModel/RigVMNode.h"
#include "RigVMModel/RigVMPin.h"
#include "RigVMModel/RigVMFunctionLibrary.h"
#include "RigVMModel/Nodes/RigVMCommentNode.h"
#include "RigVMCore/RigVMFunction.h"
#include "RigVMCore/RigVMRegistry.h"

namespace
{

static constexpr TCHAR ControlRigUnitPrefix[] = TEXT("CR_UNIT:");
static constexpr TCHAR ControlRigActionPrefix[] = TEXT("CR_ACTION:");

struct FControlRigNodeMatch
{
	FString DisplayName;
	FString UnitPath;
	FString Tooltip;
	FString Keywords;
	FString SpawnerId;
	FString MatchedQuery;
	int32 Score = 0;
};

static TMap<FString, FString> ControlRigActionCache; // CR_ACTION:* -> /Script/...
static TArray<FControlRigNodeMatch> ControlRigCachedNodeInfos;

struct FNodeToAdd
{
	FString NodeKind;
	FString StructName;
	FString TemplateNotation;
	FString FunctionName;
	FString FunctionHostPath;
	FString VariableName;
	FString CPPType;
	FString CPPTypeObjectPath;
	FString DefaultValue;
	FString PinPath;
	FString EntryName;
	FVector2D Position = FVector2D::ZeroVector;
	bool bIsGetter = true;
	bool bAsInput = false;
	TSharedPtr<FJsonObject> PinDefaults;
};

struct FLinkSpec
{
	FString SourcePin;
	FString TargetPin;
};

struct FPinDefault
{
	FString PinPath;
	FString Value;
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
	FString Axis = TEXT("x");
	FString Mode = TEXT("center");
};

struct FLayoutNodesOp
{
	TArray<FString> NodeRefs;
	int32 StartX = 0;
	int32 StartY = 0;
	int32 SpacingX = 350;
	int32 SpacingY = 220;
	int32 Columns = 4;
};

struct FCommentOp
{
	FString Text;
	FVector2D Position = FVector2D::ZeroVector;
	FVector2D Size = FVector2D(400.f, 300.f);
	FLinearColor Color = FLinearColor(0.05f, 0.05f, 0.05f, 1.0f);
	bool bHasColor = false;
};

struct FArrayPinAddOp
{
	FString PinPath;
	FString DefaultValue;
	bool bInsert = false;
	int32 InsertIndex = INDEX_NONE;
};

struct FArrayPinRemoveOp
{
	FString PinPath;
};

struct FPinVariableBindOp
{
	FString PinPath;
	FString VariablePath;
};

struct FPromotePinOp
{
	FString PinPath;
	bool bCreateVariableNode = true;
	FVector2D NodePosition = FVector2D::ZeroVector;
};

struct FPinExpansionOp
{
	FString PinPath;
	bool bExpanded = true;
};

struct FExposedPinAddOp
{
	FString Name;
	ERigVMPinDirection Direction = ERigVMPinDirection::Input;
	FString CPPType;
	FString CPPTypeObjectPath;
	FString DefaultValue;
};

struct FExposedPinRemoveOp
{
	FString Name;
};

struct FExposedPinRenameOp
{
	FString Name;
	FString NewName;
};

struct FExposedPinTypeChangeOp
{
	FString Name;
	FString CPPType;
	FString CPPTypeObjectPath;
};

struct FExposedPinReorderOp
{
	FString Name;
	int32 Index = INDEX_NONE;
};

struct FNodeCategoryOp
{
	FString NodeRef;
	FString Category;
};

struct FNodeKeywordsOp
{
	FString NodeRef;
	FString Keywords;
};

struct FNodeDescriptionOp
{
	FString NodeRef;
	FString Description;
};

struct FPinCategoryOp
{
	FString PinPath;
	FString Category;
	bool bClear = false;
};

static bool IsFindMode(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return false;
	}

	FString Operation;
	Args->TryGetStringField(TEXT("operation"), Operation);
	Operation = Operation.ToLower();

	const bool bHasQuery = Args->HasField(TEXT("query"));
	const bool bHasGraphOps =
		Args->HasField(TEXT("add_nodes")) ||
		Args->HasField(TEXT("delete_nodes")) ||
		Args->HasField(TEXT("set_pins")) ||
		Args->HasField(TEXT("move_nodes")) ||
		Args->HasField(TEXT("align_nodes")) ||
		Args->HasField(TEXT("layout_nodes")) ||
		Args->HasField(TEXT("add_comments")) ||
		Args->HasField(TEXT("connections")) ||
		Args->HasField(TEXT("disconnect")) ||
		Args->HasField(TEXT("add_array_pins")) ||
		Args->HasField(TEXT("insert_array_pins")) ||
		Args->HasField(TEXT("remove_array_pins")) ||
		Args->HasField(TEXT("bind_pin_variables")) ||
		Args->HasField(TEXT("promote_pins")) ||
		Args->HasField(TEXT("set_pin_expansion")) ||
		Args->HasField(TEXT("add_exposed_pins")) ||
		Args->HasField(TEXT("remove_exposed_pins")) ||
		Args->HasField(TEXT("rename_exposed_pins")) ||
		Args->HasField(TEXT("change_exposed_pin_types")) ||
		Args->HasField(TEXT("reorder_exposed_pins")) ||
		Args->HasField(TEXT("set_node_categories")) ||
		Args->HasField(TEXT("set_node_keywords")) ||
		Args->HasField(TEXT("set_node_descriptions")) ||
		Args->HasField(TEXT("set_pin_categories"));

	return (Operation == TEXT("find_nodes") || Operation == TEXT("search_nodes") || (bHasQuery && !bHasGraphOps));
}

static bool ParsePinDirection(const FString& InDirection, ERigVMPinDirection& OutDirection)
{
	const FString Direction = InDirection.ToLower();
	if (Direction.IsEmpty() || Direction == TEXT("input") || Direction == TEXT("in"))
	{
		OutDirection = ERigVMPinDirection::Input;
		return true;
	}
	if (Direction == TEXT("output") || Direction == TEXT("out"))
	{
		OutDirection = ERigVMPinDirection::Output;
		return true;
	}
	if (Direction == TEXT("io") || Direction == TEXT("inout"))
	{
		OutDirection = ERigVMPinDirection::IO;
		return true;
	}
	if (Direction == TEXT("visible"))
	{
		OutDirection = ERigVMPinDirection::Visible;
		return true;
	}
	if (Direction == TEXT("hidden"))
	{
		OutDirection = ERigVMPinDirection::Hidden;
		return true;
	}
	return false;
}

static bool JsonValueToString(const TSharedPtr<FJsonValue>& Value, FString& OutString)
{
	if (!Value.IsValid())
	{
		return false;
	}

	switch (Value->Type)
	{
	case EJson::String:
		OutString = Value->AsString();
		return true;
	case EJson::Number:
		OutString = FString::SanitizeFloat(Value->AsNumber());
		return true;
	case EJson::Boolean:
		OutString = Value->AsBool() ? TEXT("True") : TEXT("False");
		return true;
	case EJson::Object:
	{
		const TSharedPtr<FJsonObject> Obj = Value->AsObject();
		if (!Obj.IsValid())
		{
			return false;
		}
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
		return FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
	}
	case EJson::Array:
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Value->TryGetArray(Arr) || !Arr)
		{
			return false;
		}
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutString);
		return FJsonSerializer::Serialize(*Arr, Writer);
	}
	case EJson::Null:
		OutString = TEXT("null");
		return true;
	default:
		return false;
	}
}

static bool MatchesQuery(const FString& DisplayName, const FString& UnitPath, const FString& Tooltip, const FString& Keywords, const TArray<FString>& Queries, FString& OutMatchedQuery, int32& OutScore)
{
	OutScore = 0;
	const FString NameLower = DisplayName.ToLower();
	const FString PathLower = UnitPath.ToLower();
	const FString TooltipLower = Tooltip.ToLower();
	const FString KeywordsLower = Keywords.ToLower();

	for (const FString& Query : Queries)
	{
		int32 Score = 0;
		if (NameLower.Equals(Query))
		{
			Score = 100;
		}
		else if (NameLower.StartsWith(Query))
		{
			Score = 85;
		}
		else if (NameLower.Contains(Query))
		{
			Score = 65;
		}
		else if (PathLower.Contains(Query))
		{
			Score = 45;
		}
		else if (TooltipLower.Contains(Query))
		{
			Score = 35;
		}
		else if (KeywordsLower.Contains(Query))
		{
			Score = 25;
		}

		if (Score > OutScore)
		{
			OutScore = Score;
			OutMatchedQuery = Query;
		}
	}

	return OutScore > 0;
}

static FString StripRigUnitPrefix(const FString& StructName)
{
	FString Name = StructName;
	Name.RemoveFromStart(TEXT("FRigUnit_"));
	Name.RemoveFromStart(TEXT("RigUnit_"));
	return Name;
}

static FString NormalizeRigUnitName(const FString& InName)
{
	FString Name = InName;
	Name.TrimStartAndEndInline();
	Name = Name.ToLower();
	Name.RemoveFromStart(TEXT("frigunit_"));
	Name.RemoveFromStart(TEXT("rigunit_"));
	return Name;
}

static bool EnsureControlRigActionCachePopulated(bool bForceRebuild = false)
{
	if (!bForceRebuild && ControlRigActionCache.Num() > 0 && ControlRigCachedNodeInfos.Num() > 0)
	{
		return true;
	}

	ControlRigActionCache.Empty();
	ControlRigCachedNodeInfos.Empty();

	FRigVMRegistry::Get().RefreshEngineTypesIfRequired();
	const TChunkedArray<FRigVMFunction>& Functions = FRigVMRegistry::Get().GetFunctions();

	TSet<FString> SeenStructPaths;
	int32 ActionIndex = 0;

	for (const FRigVMFunction& Function : Functions)
	{
		UScriptStruct* Struct = Function.Struct;
		if (!Struct || !Struct->IsChildOf(FRigUnit::StaticStruct()))
		{
			continue;
		}
		if (Struct->HasMetaData(TEXT("Abstract")) || Struct->HasMetaData(TEXT("Deprecated")) || Struct->HasMetaData(TEXT("Hidden")))
		{
			continue;
		}

		const FString UnitPath = Struct->GetPathName();
		if (UnitPath.IsEmpty() || SeenStructPaths.Contains(UnitPath))
		{
			continue;
		}
		SeenStructPaths.Add(UnitPath);

		FString DisplayName;
#if WITH_EDITOR
		DisplayName = Struct->GetDisplayNameText().ToString();
#endif
		if (DisplayName.IsEmpty())
		{
			DisplayName = StripRigUnitPrefix(Struct->GetName());
		}

		FString Tooltip;
#if WITH_EDITOR
		Tooltip = Struct->GetToolTipText().ToString();
#endif
		if (Tooltip.IsEmpty())
		{
			Tooltip = Struct->GetMetaData(TEXT("ToolTip"));
		}

		const FString Keywords = Struct->GetMetaData(TEXT("Keywords"));
		const FString SpawnerId = FString::Printf(TEXT("CR_ACTION:%d:%s"), ActionIndex, *DisplayName.Replace(TEXT(" "), TEXT("_")));
		++ActionIndex;

		ControlRigActionCache.Add(SpawnerId, UnitPath);

		FControlRigNodeMatch& Cached = ControlRigCachedNodeInfos.AddDefaulted_GetRef();
		Cached.DisplayName = DisplayName;
		Cached.UnitPath = UnitPath;
		Cached.Tooltip = Tooltip;
		Cached.Keywords = Keywords;
		Cached.SpawnerId = SpawnerId;
	}

	return ControlRigActionCache.Num() > 0;
}

static FString ResolveUnitPathFromSpawnerId(const FString& SpawnerId)
{
	if (SpawnerId.StartsWith(ControlRigUnitPrefix))
	{
		return SpawnerId.Mid(FCString::Strlen(ControlRigUnitPrefix));
	}

	if (SpawnerId.StartsWith(ControlRigActionPrefix))
	{
		if (!EnsureControlRigActionCachePopulated(false))
		{
			return FString();
		}
		if (const FString* CachedPath = ControlRigActionCache.Find(SpawnerId))
		{
			return *CachedPath;
		}
	}

	return FString();
}

static bool ResolveGraph(UControlRigBlueprint* BP, const FString& GraphNameOrPath, URigVMGraph*& OutGraph, FString& OutError)
{
	OutGraph = nullptr;
	if (!BP)
	{
		OutError = TEXT("Invalid Control Rig blueprint");
		return false;
	}

	if (GraphNameOrPath.IsEmpty())
	{
		OutGraph = BP->GetDefaultModel();
		if (!OutGraph)
		{
			OutError = TEXT("Control Rig has no default graph model");
			return false;
		}
		return true;
	}

	OutGraph = BP->GetModel(GraphNameOrPath);
	if (OutGraph)
	{
		return true;
	}

	TArray<URigVMGraph*> AllModels = BP->GetAllModels();
	for (URigVMGraph* Candidate : AllModels)
	{
		if (!Candidate)
		{
			continue;
		}
		if (Candidate->GetName().Equals(GraphNameOrPath, ESearchCase::IgnoreCase) ||
			Candidate->GetNodePath().Equals(GraphNameOrPath, ESearchCase::IgnoreCase))
		{
			OutGraph = Candidate;
			return true;
		}
	}

	OutError = FString::Printf(TEXT("Control Rig graph '%s' not found"), *GraphNameOrPath);
	return false;
}

static FString ResolveUnitStruct(const FString& StructName)
{
	if (StructName.IsEmpty())
	{
		return FString();
	}

	if (!EnsureControlRigActionCachePopulated(true))
	{
		return FString();
	}

	if (StructName.StartsWith(TEXT("/Script/")))
	{
		const FString SearchPath = StructName.ToLower();
		for (const FControlRigNodeMatch& Cached : ControlRigCachedNodeInfos)
		{
			if (Cached.UnitPath.ToLower().Equals(SearchPath))
			{
				return Cached.UnitPath;
			}
		}
		return FString();
	}

	const FString NormalizedInput = NormalizeRigUnitName(StructName);
	FString ExactMatch;
	int32 ExactMatchCount = 0;

	for (const FControlRigNodeMatch& Cached : ControlRigCachedNodeInfos)
	{
		const FString NormalizedStruct = NormalizeRigUnitName(Cached.UnitPath.RightChop(Cached.UnitPath.Find(TEXT("."), ESearchCase::CaseSensitive, ESearchDir::FromEnd) + 1));
		const FString NormalizedDisplay = NormalizeRigUnitName(Cached.DisplayName);
		if (NormalizedStruct == NormalizedInput || NormalizedDisplay == NormalizedInput)
		{
			ExactMatch = Cached.UnitPath;
			++ExactMatchCount;
		}
	}

	return ExactMatchCount == 1 ? ExactMatch : FString();
}

static int32 AddNodes(URigVMController* Ctrl, UControlRigBlueprint* BP, const TArray<FNodeToAdd>& Nodes, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FNodeToAdd& N : Nodes)
	{
		FString Kind = N.NodeKind.ToLower();
		if (Kind.IsEmpty())
		{
			Kind = TEXT("unit");
		}

		URigVMNode* NewNode = nullptr;
		if (Kind == TEXT("unit"))
		{
			const FString StructPath = ResolveUnitStruct(N.StructName);
			if (StructPath.IsEmpty())
			{
				OutResults.Add(FString::Printf(TEXT("add_node: Could not resolve struct '%s'"), *N.StructName));
				continue;
			}
			NewNode = Ctrl->AddUnitNodeFromStructPath(StructPath, TEXT("Execute"), N.Position, FString(), false);
		}
		else if (Kind == TEXT("template"))
		{
			if (N.TemplateNotation.IsEmpty())
			{
				OutResults.Add(TEXT("add_node: template_notation required for node_kind=template"));
				continue;
			}
			NewNode = Ctrl->AddTemplateNode(FName(*N.TemplateNotation), N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("function_ref"))
		{
			URigVMFunctionLibrary* FunctionLibrary = BP ? BP->GetLocalFunctionLibrary() : nullptr;
			URigVMLibraryNode* FunctionDef = FunctionLibrary ? FunctionLibrary->FindFunction(FName(*N.FunctionName)) : nullptr;
			if (!FunctionDef)
			{
				OutResults.Add(FString::Printf(TEXT("add_node: function '%s' not found in local function library"), *N.FunctionName));
				continue;
			}
			NewNode = Ctrl->AddFunctionReferenceNode(FunctionDef, N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("external_function_ref"))
		{
			if (N.FunctionHostPath.IsEmpty() || N.FunctionName.IsEmpty())
			{
				OutResults.Add(TEXT("add_node: function_host_path and function_name required for node_kind=external_function_ref"));
				continue;
			}
			NewNode = Ctrl->AddExternalFunctionReferenceNode(N.FunctionHostPath, FName(*N.FunctionName), N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("variable"))
		{
			if (N.VariableName.IsEmpty() || N.CPPType.IsEmpty())
			{
				OutResults.Add(TEXT("add_node: variable_name and cpp_type required for node_kind=variable"));
				continue;
			}
			UObject* CPPTypeObject = nullptr;
			if (!N.CPPTypeObjectPath.IsEmpty())
			{
				CPPTypeObject = LoadObject<UObject>(nullptr, *N.CPPTypeObjectPath);
			}
			NewNode = Ctrl->AddVariableNode(FName(*N.VariableName), N.CPPType, CPPTypeObject, N.bIsGetter, N.DefaultValue, N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("branch"))
		{
			NewNode = Ctrl->AddBranchNode(N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("if"))
		{
			if (N.CPPType.IsEmpty())
			{
				OutResults.Add(TEXT("add_node: cpp_type required for node_kind=if"));
				continue;
			}
			NewNode = Ctrl->AddIfNode(N.CPPType, FName(*N.CPPTypeObjectPath), N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("select"))
		{
			if (N.CPPType.IsEmpty())
			{
				OutResults.Add(TEXT("add_node: cpp_type required for node_kind=select"));
				continue;
			}
			NewNode = Ctrl->AddSelectNode(N.CPPType, FName(*N.CPPTypeObjectPath), N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("reroute"))
		{
			if (N.PinPath.IsEmpty())
			{
				OutResults.Add(TEXT("add_node: pin_path required for node_kind=reroute"));
				continue;
			}
			NewNode = Ctrl->AddRerouteNodeOnPin(N.PinPath, N.bAsInput, N.Position, FString(), false, false);
		}
		else if (Kind == TEXT("invoke_entry"))
		{
			const FString EntryName = N.EntryName.IsEmpty() ? TEXT("Execute") : N.EntryName;
			NewNode = Ctrl->AddInvokeEntryNode(FName(*EntryName), N.Position, FString(), false, false);
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("add_node: Unsupported node_kind '%s'"), *N.NodeKind));
			continue;
		}

		if (!NewNode)
		{
			OutResults.Add(FString::Printf(TEXT("add_node: Failed to add node_kind '%s'"), *Kind));
			continue;
		}

		if (N.PinDefaults.IsValid())
		{
			const FString NodeName = NewNode->GetName();
			for (const auto& KV : N.PinDefaults->Values)
			{
				FString PinValue;
				if (KV.Value.IsValid() && KV.Value->TryGetString(PinValue))
				{
					Ctrl->SetPinDefaultValue(NodeName + TEXT(".") + KV.Key, PinValue, true, false, false);
				}
			}
		}

		OutResults.Add(FString::Printf(TEXT("Added node '%s'"), *NewNode->GetName()));
		++Count;
	}

	return Count;
}

static int32 RemoveNodes(URigVMController* Ctrl, const TArray<FString>& NodeNames, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FString& Name : NodeNames)
	{
		if (Ctrl->RemoveNodeByName(FName(*Name), false))
		{
			OutResults.Add(FString::Printf(TEXT("Removed node '%s'"), *Name));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to remove node '%s'"), *Name));
		}
	}
	return Count;
}

static int32 AddLinks(URigVMController* Ctrl, const TArray<FLinkSpec>& Links, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FLinkSpec& Link : Links)
	{
		if (Link.SourcePin.IsEmpty() || Link.TargetPin.IsEmpty())
		{
			OutResults.Add(TEXT("add_link: source_pin and target_pin required"));
			continue;
		}
		if (Ctrl->AddLink(Link.SourcePin, Link.TargetPin, false))
		{
			OutResults.Add(FString::Printf(TEXT("Linked %s -> %s"), *Link.SourcePin, *Link.TargetPin));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to link %s -> %s"), *Link.SourcePin, *Link.TargetPin));
		}
	}
	return Count;
}

static int32 BreakLinks(URigVMController* Ctrl, const TArray<FLinkSpec>& Links, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FLinkSpec& Link : Links)
	{
		if (Link.SourcePin.IsEmpty() || Link.TargetPin.IsEmpty())
		{
			OutResults.Add(TEXT("break_link: source_pin and target_pin required"));
			continue;
		}
		if (Ctrl->BreakLink(Link.SourcePin, Link.TargetPin, false))
		{
			OutResults.Add(FString::Printf(TEXT("Broke link %s -> %s"), *Link.SourcePin, *Link.TargetPin));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to break link %s -> %s"), *Link.SourcePin, *Link.TargetPin));
		}
	}
	return Count;
}

static int32 SetPinDefaults(URigVMController* Ctrl, const TArray<FPinDefault>& Defaults, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FPinDefault& Default : Defaults)
	{
		if (Default.PinPath.IsEmpty())
		{
			OutResults.Add(TEXT("set_pin_default: pin_path required"));
			continue;
		}
		if (Ctrl->SetPinDefaultValue(Default.PinPath, Default.Value, true, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Set %s"), *Default.PinPath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to set pin '%s'"), *Default.PinPath));
		}
	}
	return Count;
}

static URigVMNode* ResolveRigVMNodeRef(URigVMGraph* Graph, const FString& NodeRef)
{
	if (!Graph || NodeRef.IsEmpty())
	{
		return nullptr;
	}

	if (URigVMNode* ByName = Graph->FindNodeByName(FName(*NodeRef)))
	{
		return ByName;
	}
	if (URigVMNode* ByPath = Graph->FindNode(NodeRef))
	{
		return ByPath;
	}

	for (URigVMNode* Node : Graph->GetNodes())
	{
		if (!Node)
		{
			continue;
		}
		if (Node->GetName().Equals(NodeRef, ESearchCase::IgnoreCase) ||
			Node->GetNodePath().Equals(NodeRef, ESearchCase::IgnoreCase))
		{
			return Node;
		}
	}

	return nullptr;
}

static bool TryParseHexColor(const FString& InHex, FLinearColor& OutColor)
{
	if (!InHex.StartsWith(TEXT("#")))
	{
		return false;
	}

	const FColor SRGB = FColor::FromHex(InHex);
	OutColor = SRGB.ReinterpretAsLinear();
	return true;
}

static int32 MoveNodes(URigVMController* Ctrl, const TArray<FMoveNodeOp>& Ops, TArray<FString>& OutResults)
{
	URigVMGraph* Graph = Ctrl ? Ctrl->GetGraph() : nullptr;
	if (!Graph)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FMoveNodeOp& Op : Ops)
	{
		URigVMNode* Node = ResolveRigVMNodeRef(Graph, Op.NodeRef);
		if (!Node)
		{
			OutResults.Add(FString::Printf(TEXT("move_nodes: node '%s' not found"), *Op.NodeRef));
			continue;
		}

		const FVector2D OldPos = Node->GetPosition();
		const FVector2D NewPos = Op.bHasAbsolute
			? FVector2D(Op.X, Op.Y)
			: FVector2D(OldPos.X + Op.DeltaX, OldPos.Y + Op.DeltaY);
		if (Ctrl->SetNodePosition(Node, NewPos, false, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Moved '%s' (%d,%d)->(%d,%d)"),
				*Op.NodeRef,
				FMath::RoundToInt(OldPos.X), FMath::RoundToInt(OldPos.Y),
				FMath::RoundToInt(NewPos.X), FMath::RoundToInt(NewPos.Y)));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("move_nodes: failed to move '%s'"), *Op.NodeRef));
		}
	}

	return Count;
}

static int32 AlignNodes(URigVMController* Ctrl, const TArray<FAlignNodesOp>& Ops, TArray<FString>& OutResults)
{
	URigVMGraph* Graph = Ctrl ? Ctrl->GetGraph() : nullptr;
	if (!Graph)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FAlignNodesOp& Op : Ops)
	{
		TArray<URigVMNode*> Nodes;
		for (const FString& Ref : Op.NodeRefs)
		{
			if (URigVMNode* Node = ResolveRigVMNodeRef(Graph, Ref))
			{
				Nodes.Add(Node);
			}
		}
		if (Nodes.Num() < 2)
		{
			OutResults.Add(TEXT("align_nodes: fewer than 2 nodes resolved"));
			continue;
		}

		const bool bAxisX = Op.Axis.Equals(TEXT("x"), ESearchCase::IgnoreCase);
		int32 TargetCoord = 0;
		if (Op.Mode.Equals(TEXT("min"), ESearchCase::IgnoreCase))
		{
			TargetCoord = TNumericLimits<int32>::Max();
			for (URigVMNode* Node : Nodes)
			{
				TargetCoord = FMath::Min(TargetCoord, FMath::RoundToInt(bAxisX ? Node->GetPosition().X : Node->GetPosition().Y));
			}
		}
		else if (Op.Mode.Equals(TEXT("max"), ESearchCase::IgnoreCase))
		{
			TargetCoord = TNumericLimits<int32>::Lowest();
			for (URigVMNode* Node : Nodes)
			{
				TargetCoord = FMath::Max(TargetCoord, FMath::RoundToInt(bAxisX ? Node->GetPosition().X : Node->GetPosition().Y));
			}
		}
		else
		{
			int64 Sum = 0;
			for (URigVMNode* Node : Nodes)
			{
				Sum += FMath::RoundToInt(bAxisX ? Node->GetPosition().X : Node->GetPosition().Y);
			}
			TargetCoord = FMath::RoundToInt(static_cast<double>(Sum) / static_cast<double>(Nodes.Num()));
		}

		for (URigVMNode* Node : Nodes)
		{
			const FVector2D Pos = Node->GetPosition();
			const FVector2D NewPos = bAxisX ? FVector2D(TargetCoord, Pos.Y) : FVector2D(Pos.X, TargetCoord);
			Ctrl->SetNodePosition(Node, NewPos, false, false, false);
		}

		OutResults.Add(FString::Printf(TEXT("Aligned %d node(s) axis=%s mode=%s"),
			Nodes.Num(), bAxisX ? TEXT("x") : TEXT("y"), *Op.Mode));
		Count += Nodes.Num();
	}

	return Count;
}

static int32 LayoutNodes(URigVMController* Ctrl, const TArray<FLayoutNodesOp>& Ops, TArray<FString>& OutResults)
{
	URigVMGraph* Graph = Ctrl ? Ctrl->GetGraph() : nullptr;
	if (!Graph)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FLayoutNodesOp& Op : Ops)
	{
		TArray<URigVMNode*> Nodes;
		for (const FString& Ref : Op.NodeRefs)
		{
			if (URigVMNode* Node = ResolveRigVMNodeRef(Graph, Ref))
			{
				Nodes.Add(Node);
			}
		}
		if (Nodes.Num() == 0)
		{
			OutResults.Add(TEXT("layout_nodes: no nodes resolved"));
			continue;
		}

		const int32 Columns = FMath::Max(1, Op.Columns);
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			const int32 Row = Index / Columns;
			const int32 Col = Index % Columns;
			const FVector2D NewPos(
				Op.StartX + Col * Op.SpacingX,
				Op.StartY + Row * Op.SpacingY);
			Ctrl->SetNodePosition(Nodes[Index], NewPos, false, false, false);
			++Count;
		}

		OutResults.Add(FString::Printf(TEXT("Laid out %d node(s)"), Nodes.Num()));
	}
	return Count;
}

static int32 AddComments(URigVMController* Ctrl, const TArray<FCommentOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FCommentOp& Op : Ops)
	{
		if (Op.Text.IsEmpty())
		{
			OutResults.Add(TEXT("add_comments: missing text"));
			continue;
		}

		URigVMCommentNode* CommentNode = Ctrl->AddCommentNode(
			Op.Text,
			Op.Position,
			Op.Size,
			Op.bHasColor ? Op.Color : FLinearColor::Black,
			FString(),
			false,
			false);

		if (!CommentNode)
		{
			OutResults.Add(FString::Printf(TEXT("add_comments: failed to add '%s'"), *Op.Text.Left(64)));
			continue;
		}

		OutResults.Add(FString::Printf(TEXT("Added comment '%s'"), *CommentNode->GetName()));
		++Count;
	}
	return Count;
}

static int32 AddArrayPins(URigVMController* Ctrl, const TArray<FArrayPinAddOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FArrayPinAddOp& Op : Ops)
	{
		if (Op.PinPath.IsEmpty())
		{
			OutResults.Add(TEXT("array pin op missing pin_path"));
			continue;
		}

		const FString NewPinPath = Op.bInsert
			? Ctrl->InsertArrayPin(Op.PinPath, Op.InsertIndex, Op.DefaultValue, false, false)
			: Ctrl->AddArrayPin(Op.PinPath, Op.DefaultValue, false, false);
		if (!NewPinPath.IsEmpty())
		{
			OutResults.Add(FString::Printf(TEXT("%s -> %s"),
				Op.bInsert ? TEXT("Inserted array pin") : TEXT("Added array pin"),
				*NewPinPath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed array pin op on '%s'"), *Op.PinPath));
		}
	}
	return Count;
}

static int32 RemoveArrayPins(URigVMController* Ctrl, const TArray<FArrayPinRemoveOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FArrayPinRemoveOp& Op : Ops)
	{
		if (Op.PinPath.IsEmpty())
		{
			OutResults.Add(TEXT("remove_array_pins entry missing pin_path"));
			continue;
		}
		if (Ctrl->RemoveArrayPin(Op.PinPath, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Removed array pin '%s'"), *Op.PinPath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to remove array pin '%s'"), *Op.PinPath));
		}
	}
	return Count;
}

static int32 BindPinVariables(URigVMController* Ctrl, const TArray<FPinVariableBindOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FPinVariableBindOp& Op : Ops)
	{
		if (Op.PinPath.IsEmpty() || Op.VariablePath.IsEmpty())
		{
			OutResults.Add(TEXT("bind_pin_variables requires pin_path and variable"));
			continue;
		}
		if (Ctrl->BindPinToVariable(Op.PinPath, Op.VariablePath, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Bound %s -> %s"), *Op.PinPath, *Op.VariablePath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to bind %s -> %s"), *Op.PinPath, *Op.VariablePath));
		}
	}
	return Count;
}

static int32 PromotePins(URigVMController* Ctrl, const TArray<FPromotePinOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FPromotePinOp& Op : Ops)
	{
		if (Op.PinPath.IsEmpty())
		{
			OutResults.Add(TEXT("promote_pins entry missing pin_path"));
			continue;
		}
		if (Ctrl->PromotePinToVariable(Op.PinPath, Op.bCreateVariableNode, Op.NodePosition, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Promoted pin '%s'"), *Op.PinPath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to promote pin '%s'"), *Op.PinPath));
		}
	}
	return Count;
}

static int32 SetPinExpansion(URigVMController* Ctrl, const TArray<FPinExpansionOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FPinExpansionOp& Op : Ops)
	{
		if (Op.PinPath.IsEmpty())
		{
			OutResults.Add(TEXT("set_pin_expansion entry missing pin_path"));
			continue;
		}
		if (Ctrl->SetPinExpansion(Op.PinPath, Op.bExpanded, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("%s pin '%s'"), Op.bExpanded ? TEXT("Expanded") : TEXT("Collapsed"), *Op.PinPath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to set expansion for '%s'"), *Op.PinPath));
		}
	}
	return Count;
}

static int32 AddExposedPins(URigVMController* Ctrl, const TArray<FExposedPinAddOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FExposedPinAddOp& Op : Ops)
	{
		if (Op.Name.IsEmpty() || Op.CPPType.IsEmpty())
		{
			OutResults.Add(TEXT("add_exposed_pins requires name and cpp_type"));
			continue;
		}

		const FName NewPinName = Ctrl->AddExposedPin(
			FName(*Op.Name),
			Op.Direction,
			Op.CPPType,
			Op.CPPTypeObjectPath.IsEmpty() ? NAME_None : FName(*Op.CPPTypeObjectPath),
			Op.DefaultValue,
			false,
			false);
		if (NewPinName != NAME_None)
		{
			OutResults.Add(FString::Printf(TEXT("Added exposed pin '%s'"), *NewPinName.ToString()));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to add exposed pin '%s'"), *Op.Name));
		}
	}
	return Count;
}

static int32 RemoveExposedPins(URigVMController* Ctrl, const TArray<FExposedPinRemoveOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FExposedPinRemoveOp& Op : Ops)
	{
		if (Op.Name.IsEmpty())
		{
			OutResults.Add(TEXT("remove_exposed_pins entry missing name"));
			continue;
		}

		if (Ctrl->RemoveExposedPin(FName(*Op.Name), false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Removed exposed pin '%s'"), *Op.Name));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to remove exposed pin '%s'"), *Op.Name));
		}
	}
	return Count;
}

static int32 RenameExposedPins(URigVMController* Ctrl, const TArray<FExposedPinRenameOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FExposedPinRenameOp& Op : Ops)
	{
		if (Op.Name.IsEmpty() || Op.NewName.IsEmpty())
		{
			OutResults.Add(TEXT("rename_exposed_pins entry requires name and new_name"));
			continue;
		}

		if (Ctrl->RenameExposedPin(FName(*Op.Name), FName(*Op.NewName), false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Renamed exposed pin '%s' -> '%s'"), *Op.Name, *Op.NewName));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to rename exposed pin '%s'"), *Op.Name));
		}
	}
	return Count;
}

static int32 ChangeExposedPinTypes(URigVMController* Ctrl, const TArray<FExposedPinTypeChangeOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FExposedPinTypeChangeOp& Op : Ops)
	{
		if (Op.Name.IsEmpty() || Op.CPPType.IsEmpty())
		{
			OutResults.Add(TEXT("change_exposed_pin_types entry requires name and cpp_type"));
			continue;
		}

		bool bSetupUndoRedo = false;
		const bool bChanged = Ctrl->ChangeExposedPinType(
			FName(*Op.Name),
			Op.CPPType,
			Op.CPPTypeObjectPath.IsEmpty() ? NAME_None : FName(*Op.CPPTypeObjectPath),
			bSetupUndoRedo,
			true,
			false);
		if (bChanged)
		{
			OutResults.Add(FString::Printf(TEXT("Changed exposed pin '%s' type to %s"), *Op.Name, *Op.CPPType));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to change exposed pin '%s' type"), *Op.Name));
		}
	}
	return Count;
}

static int32 ReorderExposedPins(URigVMController* Ctrl, const TArray<FExposedPinReorderOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FExposedPinReorderOp& Op : Ops)
	{
		if (Op.Name.IsEmpty() || Op.Index < 0)
		{
			OutResults.Add(TEXT("reorder_exposed_pins entry requires name and non-negative index"));
			continue;
		}

		if (Ctrl->SetExposedPinIndex(FName(*Op.Name), Op.Index, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Moved exposed pin '%s' to index %d"), *Op.Name, Op.Index));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to reorder exposed pin '%s'"), *Op.Name));
		}
	}
	return Count;
}

static int32 SetNodeCategories(URigVMController* Ctrl, const TArray<FNodeCategoryOp>& Ops, TArray<FString>& OutResults)
{
	URigVMGraph* Graph = Ctrl ? Ctrl->GetGraph() : nullptr;
	if (!Graph)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FNodeCategoryOp& Op : Ops)
	{
		if (Op.NodeRef.IsEmpty())
		{
			OutResults.Add(TEXT("set_node_categories entry missing node"));
			continue;
		}
		URigVMNode* Node = ResolveRigVMNodeRef(Graph, Op.NodeRef);
		if (!Node)
		{
			OutResults.Add(FString::Printf(TEXT("set_node_categories: node '%s' not found"), *Op.NodeRef));
			continue;
		}
		if (Ctrl->SetNodeCategoryByName(Node->GetFName(), Op.Category, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Set category for '%s'"), *Op.NodeRef));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to set category for '%s'"), *Op.NodeRef));
		}
	}
	return Count;
}

static int32 SetNodeKeywords(URigVMController* Ctrl, const TArray<FNodeKeywordsOp>& Ops, TArray<FString>& OutResults)
{
	URigVMGraph* Graph = Ctrl ? Ctrl->GetGraph() : nullptr;
	if (!Graph)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FNodeKeywordsOp& Op : Ops)
	{
		if (Op.NodeRef.IsEmpty())
		{
			OutResults.Add(TEXT("set_node_keywords entry missing node"));
			continue;
		}
		URigVMNode* Node = ResolveRigVMNodeRef(Graph, Op.NodeRef);
		if (!Node)
		{
			OutResults.Add(FString::Printf(TEXT("set_node_keywords: node '%s' not found"), *Op.NodeRef));
			continue;
		}
		if (Ctrl->SetNodeKeywordsByName(Node->GetFName(), Op.Keywords, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Set keywords for '%s'"), *Op.NodeRef));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to set keywords for '%s'"), *Op.NodeRef));
		}
	}
	return Count;
}

static int32 SetNodeDescriptions(URigVMController* Ctrl, const TArray<FNodeDescriptionOp>& Ops, TArray<FString>& OutResults)
{
	URigVMGraph* Graph = Ctrl ? Ctrl->GetGraph() : nullptr;
	if (!Graph)
	{
		return 0;
	}

	int32 Count = 0;
	for (const FNodeDescriptionOp& Op : Ops)
	{
		if (Op.NodeRef.IsEmpty())
		{
			OutResults.Add(TEXT("set_node_descriptions entry missing node"));
			continue;
		}
		URigVMNode* Node = ResolveRigVMNodeRef(Graph, Op.NodeRef);
		if (!Node)
		{
			OutResults.Add(FString::Printf(TEXT("set_node_descriptions: node '%s' not found"), *Op.NodeRef));
			continue;
		}
		if (Ctrl->SetNodeDescriptionByName(Node->GetFName(), Op.Description, false, false))
		{
			OutResults.Add(FString::Printf(TEXT("Set description for '%s'"), *Op.NodeRef));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to set description for '%s'"), *Op.NodeRef));
		}
	}
	return Count;
}

static int32 SetPinCategories(URigVMController* Ctrl, const TArray<FPinCategoryOp>& Ops, TArray<FString>& OutResults)
{
	int32 Count = 0;
	for (const FPinCategoryOp& Op : Ops)
	{
		if (Op.PinPath.IsEmpty())
		{
			OutResults.Add(TEXT("set_pin_categories entry missing pin_path"));
			continue;
		}

		const bool bChanged = Op.bClear
			? Ctrl->ClearPinCategory(Op.PinPath, false, false)
			: Ctrl->SetPinCategory(Op.PinPath, Op.Category, false, false);
		if (bChanged)
		{
			OutResults.Add(FString::Printf(TEXT("%s pin category '%s'"), Op.bClear ? TEXT("Cleared") : TEXT("Set"), *Op.PinPath));
			++Count;
		}
		else
		{
			OutResults.Add(FString::Printf(TEXT("Failed to change pin category '%s'"), *Op.PinPath));
		}
	}
	return Count;
}

static FToolResult FindControlRigNodes(const TSharedPtr<FJsonObject>& Args, const FString& AssetName, const FString& GraphName)
{
	const TArray<TSharedPtr<FJsonValue>>* QueryArray = nullptr;
	if (!Args->TryGetArrayField(TEXT("query"), QueryArray) || !QueryArray)
	{
		return FToolResult::Fail(TEXT("Missing required parameter: query (array of search terms)"));
	}

	TArray<FString> Queries;
	for (const TSharedPtr<FJsonValue>& Value : *QueryArray)
	{
		FString Query;
		if (Value.IsValid() && Value->TryGetString(Query) && !Query.IsEmpty())
		{
			Queries.Add(Query.ToLower());
		}
	}

	if (Queries.Num() == 0)
	{
		return FToolResult::Fail(TEXT("query must contain at least one non-empty string"));
	}

	int32 Limit = 15;
	if (Args->HasField(TEXT("limit")))
	{
		Limit = FMath::Max(1, static_cast<int32>(Args->GetNumberField(TEXT("limit"))));
	}

	if (!EnsureControlRigActionCachePopulated(true))
	{
		return FToolResult::Fail(TEXT("Failed to load Control Rig unit registry"));
	}

	TArray<FControlRigNodeMatch> Matches;
	for (const FControlRigNodeMatch& Cached : ControlRigCachedNodeInfos)
	{
		FString MatchedQuery;
		int32 Score = 0;
		if (!MatchesQuery(Cached.DisplayName, Cached.UnitPath, Cached.Tooltip, Cached.Keywords, Queries, MatchedQuery, Score))
		{
			continue;
		}

		FControlRigNodeMatch& Match = Matches.AddDefaulted_GetRef();
		Match.DisplayName = Cached.DisplayName;
		Match.UnitPath = Cached.UnitPath;
		Match.Tooltip = Cached.Tooltip;
		Match.Keywords = Cached.Keywords;
		Match.SpawnerId = Cached.SpawnerId;
		Match.MatchedQuery = MatchedQuery;
		Match.Score = Score;
	}

	Matches.Sort([](const FControlRigNodeMatch& A, const FControlRigNodeMatch& B)
	{
		if (A.Score != B.Score)
		{
			return A.Score > B.Score;
		}
		return A.DisplayName < B.DisplayName;
	});

	TMap<FString, int32> PerQueryCount;
	FString Output;
	Output += FString::Printf(TEXT("# FIND NODES in %s (ControlRig)\n"), *AssetName);
	if (!GraphName.IsEmpty())
	{
		Output += FString::Printf(TEXT("Graph: %s\n"), *GraphName);
	}
	Output += FString::Printf(TEXT("Query: %s\n\n"), *FString::Join(Queries, TEXT(", ")));

	if (Matches.Num() == 0)
	{
		Output += TEXT("No matching Control Rig unit nodes found.");
		return FToolResult::Ok(Output);
	}

	Output += FString::Printf(TEXT("## Results (%d found, top %d per query)\n\n"), Matches.Num(), Limit);
	for (const FControlRigNodeMatch& Match : Matches)
	{
		int32& Count = PerQueryCount.FindOrAdd(Match.MatchedQuery);
		if (Count >= Limit)
		{
			continue;
		}
		++Count;

		Output += FString::Printf(TEXT("+ %s\n"), *Match.DisplayName);
		Output += FString::Printf(TEXT("  ID: %s\n"), *Match.SpawnerId);
		Output += FString::Printf(TEXT("  Path: %s%s\n"), ControlRigUnitPrefix, *Match.UnitPath);
		if (!Match.Tooltip.IsEmpty())
		{
			FString Tooltip = Match.Tooltip.Replace(TEXT("\n"), TEXT(" "));
			if (Tooltip.Len() > 140)
			{
				Tooltip = Tooltip.Left(137) + TEXT("...");
			}
			Output += FString::Printf(TEXT("  Desc: %s\n"), *Tooltip);
		}
		Output += TEXT("\n");
	}

	return FToolResult::Ok(Output);
}

static bool ConvertConnectionStringToPinPair(const FString& Connection, FString& OutSourcePin, FString& OutTargetPin)
{
	FString Left, Right;
	if (!Connection.Split(TEXT("->"), &Left, &Right))
	{
		return false;
	}

	auto ConvertEndpoint = [](const FString& Endpoint, FString& OutPinPath) -> bool
	{
		FString NodeRef;
		FString PinName;
		if (!Endpoint.Split(TEXT(":"), &NodeRef, &PinName))
		{
			return false;
		}
		NodeRef.TrimStartAndEndInline();
		PinName.TrimStartAndEndInline();
		if (NodeRef.IsEmpty() || PinName.IsEmpty())
		{
			return false;
		}

		OutPinPath = NodeRef + TEXT(".") + PinName;
		return true;
	};

	return ConvertEndpoint(Left, OutSourcePin) && ConvertEndpoint(Right, OutTargetPin);
}

} // namespace

bool EditGraphTool_IsControlRig(UObject* Asset)
{
	return Cast<UControlRigBlueprint>(Asset) != nullptr;
}

FToolResult EditGraphTool_HandleControlRigGraph(const TSharedPtr<FJsonObject>& Args, const FString& AssetName, const FString& Path)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString GraphName;
	Args->TryGetStringField(TEXT("graph_name"), GraphName);

	if (IsFindMode(Args))
	{
		return FindControlRigNodes(Args, AssetName, GraphName);
	}

	const FString AssetPath = NeoStackToolUtils::BuildAssetPath(AssetName, Path);
	UControlRigBlueprint* BP = NeoStackToolUtils::LoadAssetWithFallback<UControlRigBlueprint>(AssetPath);
	if (!BP)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Control Rig not found: %s"), *AssetPath));
	}

	URigVMGraph* Graph = nullptr;
	FString ResolveError;
	if (!ResolveGraph(BP, GraphName, Graph, ResolveError))
	{
		return FToolResult::Fail(ResolveError);
	}

	URigVMController* Controller = BP->GetOrCreateController(Graph);
	if (!Controller)
	{
		return FToolResult::Fail(TEXT("Failed to get Control Rig graph controller"));
	}

	TArray<FString> ConversionErrors;
	TArray<FNodeToAdd> NodesToAdd;
	TArray<FString> NodesToRemove;
	TArray<FLinkSpec> LinksToAdd;
	TArray<FLinkSpec> LinksToBreak;
	TArray<FPinDefault> DefaultsToSet;
	TArray<FMoveNodeOp> MoveNodeOps;
	TArray<FAlignNodesOp> AlignNodeOps;
	TArray<FLayoutNodesOp> LayoutNodeOps;
	TArray<FCommentOp> CommentOps;
	TArray<FArrayPinAddOp> AddArrayPinOps;
	TArray<FArrayPinAddOp> InsertArrayPinOps;
	TArray<FArrayPinRemoveOp> RemoveArrayPinOps;
	TArray<FPinVariableBindOp> BindVariableOps;
	TArray<FPromotePinOp> PromotePinOps;
	TArray<FPinExpansionOp> PinExpansionOps;
	TArray<FExposedPinAddOp> AddExposedPinOps;
	TArray<FExposedPinRemoveOp> RemoveExposedPinOps;
	TArray<FExposedPinRenameOp> RenameExposedPinOps;
	TArray<FExposedPinTypeChangeOp> ChangeExposedPinTypeOps;
	TArray<FExposedPinReorderOp> ReorderExposedPinOps;
	TArray<FNodeCategoryOp> SetNodeCategoryOps;
	TArray<FNodeKeywordsOp> SetNodeKeywordsOps;
	TArray<FNodeDescriptionOp> SetNodeDescriptionOps;
	TArray<FPinCategoryOp> SetPinCategoryOps;

	const TArray<TSharedPtr<FJsonValue>>* AddNodesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("add_nodes"), AddNodesArray) && AddNodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddNodesArray)
		{
			const TSharedPtr<FJsonObject>* NodeObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(NodeObj) || !NodeObj || !NodeObj->IsValid())
			{
				ConversionErrors.Add(TEXT("add_nodes entry is not an object"));
				continue;
			}

			FNodeToAdd Node;

			FString SpawnerId;
			if ((*NodeObj)->TryGetStringField(TEXT("id"), SpawnerId) && !SpawnerId.IsEmpty())
			{
				const FString ResolvedUnitPath = ResolveUnitPathFromSpawnerId(SpawnerId);
				if (!ResolvedUnitPath.IsEmpty())
				{
					Node.NodeKind = TEXT("unit");
					Node.StructName = ResolvedUnitPath;
				}
				else
				{
					ConversionErrors.Add(FString::Printf(TEXT("Unsupported ControlRig node id '%s' (expected CR_ACTION:* from a prior find_nodes call in this session, or CR_UNIT:<StructPath>)"), *SpawnerId));
					continue;
				}
			}
			else
			{
				FString NodeKind;
				FString StructName;
				(*NodeObj)->TryGetStringField(TEXT("node_kind"), NodeKind);
				(*NodeObj)->TryGetStringField(TEXT("struct_name"), StructName);
				if (NodeKind.IsEmpty() && StructName.IsEmpty())
				{
					ConversionErrors.Add(TEXT("ControlRig add_nodes requires id (CR_ACTION:* or CR_UNIT:...) or node_kind/struct_name"));
					continue;
				}
				Node.NodeKind = NodeKind;
				Node.StructName = StructName;
			}

			const TSharedPtr<FJsonObject>* PosObj = nullptr;
			if ((*NodeObj)->TryGetObjectField(TEXT("position"), PosObj) && PosObj && PosObj->IsValid())
			{
				double X = 0.0;
				double Y = 0.0;
				(*PosObj)->TryGetNumberField(TEXT("x"), X);
				(*PosObj)->TryGetNumberField(TEXT("y"), Y);
				Node.Position = FVector2D(X, Y);
			}

			const TSharedPtr<FJsonObject>* PinsObj = nullptr;
			if ((*NodeObj)->TryGetObjectField(TEXT("pins"), PinsObj) && PinsObj && PinsObj->IsValid())
			{
				Node.PinDefaults = *PinsObj;
			}

			(*NodeObj)->TryGetStringField(TEXT("template_notation"), Node.TemplateNotation);
			(*NodeObj)->TryGetStringField(TEXT("function_name"), Node.FunctionName);
			(*NodeObj)->TryGetStringField(TEXT("function_host_path"), Node.FunctionHostPath);
			(*NodeObj)->TryGetStringField(TEXT("variable_name"), Node.VariableName);
			(*NodeObj)->TryGetStringField(TEXT("cpp_type"), Node.CPPType);
			(*NodeObj)->TryGetStringField(TEXT("cpp_type_object_path"), Node.CPPTypeObjectPath);
			(*NodeObj)->TryGetStringField(TEXT("default_value"), Node.DefaultValue);
			(*NodeObj)->TryGetStringField(TEXT("pin_path"), Node.PinPath);
			(*NodeObj)->TryGetStringField(TEXT("entry_name"), Node.EntryName);
			(*NodeObj)->TryGetBoolField(TEXT("is_getter"), Node.bIsGetter);
			(*NodeObj)->TryGetBoolField(TEXT("as_input"), Node.bAsInput);

			NodesToAdd.Add(MoveTemp(Node));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* DeleteNodesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("delete_nodes"), DeleteNodesArray) && DeleteNodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *DeleteNodesArray)
		{
			FString NodeName;
			if (!Value.IsValid() || !Value->TryGetString(NodeName) || NodeName.IsEmpty())
			{
				ConversionErrors.Add(TEXT("delete_nodes entry is not a non-empty string"));
				continue;
			}
			NodesToRemove.Add(NodeName);
		}
	}

	auto ConvertConnectionsField = [&](const FString& SourceField, TArray<FLinkSpec>& OutLinks)
	{
		const TArray<TSharedPtr<FJsonValue>>* ConnectionsArray = nullptr;
		if (!Args->TryGetArrayField(SourceField, ConnectionsArray) || !ConnectionsArray)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *ConnectionsArray)
		{
			FString ConnectionString;
			if (!Value.IsValid() || !Value->TryGetString(ConnectionString))
			{
				ConversionErrors.Add(FString::Printf(TEXT("%s entry is not a string"), *SourceField));
				continue;
			}

			FString SourcePin;
			FString TargetPin;
			if (!ConvertConnectionStringToPinPair(ConnectionString, SourcePin, TargetPin))
			{
				ConversionErrors.Add(FString::Printf(TEXT("Invalid connection format '%s' (expected Node:Pin->Node:Pin)"), *ConnectionString));
				continue;
			}

			FLinkSpec Spec;
			Spec.SourcePin = SourcePin;
			Spec.TargetPin = TargetPin;
			OutLinks.Add(MoveTemp(Spec));
		}
	};

	ConvertConnectionsField(TEXT("connections"), LinksToAdd);
	ConvertConnectionsField(TEXT("disconnect"), LinksToBreak);

	const TArray<TSharedPtr<FJsonValue>>* SetPinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("set_pins"), SetPinsArray) && SetPinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetPinsArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("set_pins entry is not an object"));
				continue;
			}

			FString NodeRef;
			if (!(*OpObj)->TryGetStringField(TEXT("node"), NodeRef) || NodeRef.IsEmpty())
			{
				ConversionErrors.Add(TEXT("set_pins entry missing 'node'"));
				continue;
			}

			const TSharedPtr<FJsonObject>* ValuesObj = nullptr;
			if (!(*OpObj)->TryGetObjectField(TEXT("values"), ValuesObj) || !ValuesObj || !ValuesObj->IsValid())
			{
				ConversionErrors.Add(FString::Printf(TEXT("set_pins for node '%s' missing 'values' object"), *NodeRef));
				continue;
			}

			for (const auto& Pair : (*ValuesObj)->Values)
			{
				FString ValueString;
				if (!JsonValueToString(Pair.Value, ValueString))
				{
					ConversionErrors.Add(FString::Printf(TEXT("set_pins value conversion failed for %s.%s"), *NodeRef, *Pair.Key));
					continue;
				}

				FPinDefault PinDefault;
				PinDefault.PinPath = NodeRef + TEXT(".") + Pair.Key;
				PinDefault.Value = ValueString;
				DefaultsToSet.Add(MoveTemp(PinDefault));
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* MoveNodesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("move_nodes"), MoveNodesArray) && MoveNodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *MoveNodesArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("move_nodes entry is not an object"));
				continue;
			}

			FMoveNodeOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("node"), Op.NodeRef) || Op.NodeRef.IsEmpty())
			{
				ConversionErrors.Add(TEXT("move_nodes entry missing 'node'"));
				continue;
			}

			const TSharedPtr<FJsonObject>* PositionObj = nullptr;
			if ((*OpObj)->TryGetObjectField(TEXT("position"), PositionObj) && PositionObj && PositionObj->IsValid())
			{
				double X = 0.0;
				double Y = 0.0;
				if ((*PositionObj)->TryGetNumberField(TEXT("x"), X) && (*PositionObj)->TryGetNumberField(TEXT("y"), Y))
				{
					Op.bHasAbsolute = true;
					Op.X = FMath::RoundToInt(X);
					Op.Y = FMath::RoundToInt(Y);
				}
			}
			else
			{
				double X = 0.0;
				double Y = 0.0;
				if ((*OpObj)->TryGetNumberField(TEXT("x"), X) && (*OpObj)->TryGetNumberField(TEXT("y"), Y))
				{
					Op.bHasAbsolute = true;
					Op.X = FMath::RoundToInt(X);
					Op.Y = FMath::RoundToInt(Y);
				}
			}

			double DeltaX = 0.0;
			double DeltaY = 0.0;
			if ((*OpObj)->TryGetNumberField(TEXT("dx"), DeltaX) || (*OpObj)->TryGetNumberField(TEXT("delta_x"), DeltaX))
			{
				Op.DeltaX = FMath::RoundToInt(DeltaX);
			}
			if ((*OpObj)->TryGetNumberField(TEXT("dy"), DeltaY) || (*OpObj)->TryGetNumberField(TEXT("delta_y"), DeltaY))
			{
				Op.DeltaY = FMath::RoundToInt(DeltaY);
			}

			MoveNodeOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* AlignNodesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("align_nodes"), AlignNodesArray) && AlignNodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AlignNodesArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("align_nodes entry is not an object"));
				continue;
			}

			FAlignNodesOp Op;
			const TArray<TSharedPtr<FJsonValue>>* NodeRefs = nullptr;
			if (!(*OpObj)->TryGetArrayField(TEXT("nodes"), NodeRefs) || !NodeRefs || NodeRefs->Num() == 0)
			{
				ConversionErrors.Add(TEXT("align_nodes entry requires non-empty 'nodes'"));
				continue;
			}

			for (const TSharedPtr<FJsonValue>& NodeValue : *NodeRefs)
			{
				FString NodeRef;
				if (NodeValue.IsValid() && NodeValue->TryGetString(NodeRef) && !NodeRef.IsEmpty())
				{
					Op.NodeRefs.Add(NodeRef);
				}
			}
			if (Op.NodeRefs.Num() == 0)
			{
				ConversionErrors.Add(TEXT("align_nodes entry has no valid node refs"));
				continue;
			}

			(*OpObj)->TryGetStringField(TEXT("axis"), Op.Axis);
			(*OpObj)->TryGetStringField(TEXT("mode"), Op.Mode);
			AlignNodeOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* LayoutNodesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("layout_nodes"), LayoutNodesArray) && LayoutNodesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *LayoutNodesArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("layout_nodes entry is not an object"));
				continue;
			}

			FLayoutNodesOp Op;
			const TArray<TSharedPtr<FJsonValue>>* NodeRefs = nullptr;
			if ((*OpObj)->TryGetArrayField(TEXT("nodes"), NodeRefs) && NodeRefs)
			{
				for (const TSharedPtr<FJsonValue>& NodeValue : *NodeRefs)
				{
					FString NodeRef;
					if (NodeValue.IsValid() && NodeValue->TryGetString(NodeRef) && !NodeRef.IsEmpty())
					{
						Op.NodeRefs.Add(NodeRef);
					}
				}
			}
			if (Op.NodeRefs.Num() == 0)
			{
				ConversionErrors.Add(TEXT("layout_nodes entry requires non-empty 'nodes'"));
				continue;
			}

			double Number = 0.0;
			if ((*OpObj)->TryGetNumberField(TEXT("start_x"), Number))
			{
				Op.StartX = FMath::RoundToInt(Number);
			}
			if ((*OpObj)->TryGetNumberField(TEXT("start_y"), Number))
			{
				Op.StartY = FMath::RoundToInt(Number);
			}
			if ((*OpObj)->TryGetNumberField(TEXT("spacing_x"), Number))
			{
				Op.SpacingX = FMath::RoundToInt(Number);
			}
			if ((*OpObj)->TryGetNumberField(TEXT("spacing_y"), Number))
			{
				Op.SpacingY = FMath::RoundToInt(Number);
			}
			if ((*OpObj)->TryGetNumberField(TEXT("columns"), Number))
			{
				Op.Columns = FMath::RoundToInt(Number);
			}
			LayoutNodeOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* AddCommentsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("add_comments"), AddCommentsArray) && AddCommentsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddCommentsArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("add_comments entry is not an object"));
				continue;
			}

			FCommentOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("text"), Op.Text) || Op.Text.IsEmpty())
			{
				ConversionErrors.Add(TEXT("add_comments entry missing 'text'"));
				continue;
			}

			const TSharedPtr<FJsonObject>* PositionObj = nullptr;
			if ((*OpObj)->TryGetObjectField(TEXT("position"), PositionObj) && PositionObj && PositionObj->IsValid())
			{
				double X = 0.0;
				double Y = 0.0;
				(*PositionObj)->TryGetNumberField(TEXT("x"), X);
				(*PositionObj)->TryGetNumberField(TEXT("y"), Y);
				Op.Position = FVector2D(X, Y);
			}

			const TSharedPtr<FJsonObject>* SizeObj = nullptr;
			if ((*OpObj)->TryGetObjectField(TEXT("size"), SizeObj) && SizeObj && SizeObj->IsValid())
			{
				double W = Op.Size.X;
				double H = Op.Size.Y;
				(*SizeObj)->TryGetNumberField(TEXT("w"), W);
				(*SizeObj)->TryGetNumberField(TEXT("h"), H);
				(*SizeObj)->TryGetNumberField(TEXT("x"), W);
				(*SizeObj)->TryGetNumberField(TEXT("y"), H);
				Op.Size = FVector2D(W, H);
			}

			FString ColorHex;
			if ((*OpObj)->TryGetStringField(TEXT("color"), ColorHex))
			{
				Op.bHasColor = TryParseHexColor(ColorHex, Op.Color);
				if (!Op.bHasColor)
				{
					ConversionErrors.Add(FString::Printf(TEXT("add_comments invalid color '%s' (expected #RRGGBB[AA])"), *ColorHex));
					continue;
				}
			}
			else
			{
				const TSharedPtr<FJsonObject>* ColorObj = nullptr;
				if ((*OpObj)->TryGetObjectField(TEXT("color"), ColorObj) && ColorObj && ColorObj->IsValid())
				{
					double R = Op.Color.R;
					double G = Op.Color.G;
					double B = Op.Color.B;
					double A = Op.Color.A;
					(*ColorObj)->TryGetNumberField(TEXT("r"), R);
					(*ColorObj)->TryGetNumberField(TEXT("g"), G);
					(*ColorObj)->TryGetNumberField(TEXT("b"), B);
					(*ColorObj)->TryGetNumberField(TEXT("a"), A);
					Op.Color = FLinearColor(R, G, B, A);
					Op.bHasColor = true;
				}
			}

			CommentOps.Add(MoveTemp(Op));
		}
	}

	auto ParseArrayPinAddField = [&](const FString& FieldName, bool bInsert, TArray<FArrayPinAddOp>& OutOps)
	{
		const TArray<TSharedPtr<FJsonValue>>* ArrayField = nullptr;
		if (!Args->TryGetArrayField(FieldName, ArrayField) || !ArrayField)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *ArrayField)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(FString::Printf(TEXT("%s entry is not an object"), *FieldName));
				continue;
			}

			FArrayPinAddOp Op;
			Op.bInsert = bInsert;
			if (!(*OpObj)->TryGetStringField(TEXT("pin_path"), Op.PinPath) || Op.PinPath.IsEmpty())
			{
				ConversionErrors.Add(FString::Printf(TEXT("%s entry missing pin_path"), *FieldName));
				continue;
			}
			(*OpObj)->TryGetStringField(TEXT("default_value"), Op.DefaultValue);

			if (bInsert)
			{
				double Index = INDEX_NONE;
				if (!(*OpObj)->TryGetNumberField(TEXT("index"), Index))
				{
					ConversionErrors.Add(TEXT("insert_array_pins entry missing index"));
					continue;
				}
				Op.InsertIndex = FMath::RoundToInt(Index);
			}

			OutOps.Add(MoveTemp(Op));
		}
	};

	ParseArrayPinAddField(TEXT("add_array_pins"), false, AddArrayPinOps);
	ParseArrayPinAddField(TEXT("insert_array_pins"), true, InsertArrayPinOps);

	const TArray<TSharedPtr<FJsonValue>>* RemoveArrayPinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("remove_array_pins"), RemoveArrayPinsArray) && RemoveArrayPinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveArrayPinsArray)
		{
			FArrayPinRemoveOp Op;
			if (Value.IsValid() && Value->TryGetString(Op.PinPath) && !Op.PinPath.IsEmpty())
			{
				RemoveArrayPinOps.Add(MoveTemp(Op));
				continue;
			}

			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("remove_array_pins entry is not a string/object"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("pin_path"), Op.PinPath) || Op.PinPath.IsEmpty())
			{
				ConversionErrors.Add(TEXT("remove_array_pins entry missing pin_path"));
				continue;
			}
			RemoveArrayPinOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* BindPinVariablesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("bind_pin_variables"), BindPinVariablesArray) && BindPinVariablesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *BindPinVariablesArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("bind_pin_variables entry is not an object"));
				continue;
			}

			FPinVariableBindOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("pin_path"), Op.PinPath) || Op.PinPath.IsEmpty())
			{
				ConversionErrors.Add(TEXT("bind_pin_variables entry missing pin_path"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("variable"), Op.VariablePath))
			{
				(*OpObj)->TryGetStringField(TEXT("variable_path"), Op.VariablePath);
			}
			if (Op.VariablePath.IsEmpty())
			{
				ConversionErrors.Add(TEXT("bind_pin_variables entry missing variable/variable_path"));
				continue;
			}
			BindVariableOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* PromotePinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("promote_pins"), PromotePinsArray) && PromotePinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *PromotePinsArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("promote_pins entry is not an object"));
				continue;
			}

			FPromotePinOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("pin_path"), Op.PinPath) || Op.PinPath.IsEmpty())
			{
				ConversionErrors.Add(TEXT("promote_pins entry missing pin_path"));
				continue;
			}
			(*OpObj)->TryGetBoolField(TEXT("create_variable_node"), Op.bCreateVariableNode);
			const TSharedPtr<FJsonObject>* PositionObj = nullptr;
			if ((*OpObj)->TryGetObjectField(TEXT("position"), PositionObj) && PositionObj && PositionObj->IsValid())
			{
				double X = 0.0;
				double Y = 0.0;
				(*PositionObj)->TryGetNumberField(TEXT("x"), X);
				(*PositionObj)->TryGetNumberField(TEXT("y"), Y);
				Op.NodePosition = FVector2D(X, Y);
			}
			PromotePinOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* SetPinExpansionArray = nullptr;
	if (Args->TryGetArrayField(TEXT("set_pin_expansion"), SetPinExpansionArray) && SetPinExpansionArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetPinExpansionArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("set_pin_expansion entry is not an object"));
				continue;
			}

			FPinExpansionOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("pin_path"), Op.PinPath) || Op.PinPath.IsEmpty())
			{
				ConversionErrors.Add(TEXT("set_pin_expansion entry missing pin_path"));
				continue;
			}
			(*OpObj)->TryGetBoolField(TEXT("expanded"), Op.bExpanded);
			PinExpansionOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* AddExposedPinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("add_exposed_pins"), AddExposedPinsArray) && AddExposedPinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddExposedPinsArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("add_exposed_pins entry is not an object"));
				continue;
			}

			FExposedPinAddOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("name"), Op.Name) || Op.Name.IsEmpty())
			{
				ConversionErrors.Add(TEXT("add_exposed_pins entry missing name"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("cpp_type"), Op.CPPType) || Op.CPPType.IsEmpty())
			{
				ConversionErrors.Add(TEXT("add_exposed_pins entry missing cpp_type"));
				continue;
			}

			FString Direction;
			(*OpObj)->TryGetStringField(TEXT("direction"), Direction);
			if (!ParsePinDirection(Direction, Op.Direction))
			{
				ConversionErrors.Add(FString::Printf(TEXT("add_exposed_pins invalid direction '%s'"), *Direction));
				continue;
			}

			(*OpObj)->TryGetStringField(TEXT("cpp_type_object_path"), Op.CPPTypeObjectPath);
			(*OpObj)->TryGetStringField(TEXT("default_value"), Op.DefaultValue);
			AddExposedPinOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* RemoveExposedPinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("remove_exposed_pins"), RemoveExposedPinsArray) && RemoveExposedPinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveExposedPinsArray)
		{
			FExposedPinRemoveOp Op;
			if (Value.IsValid() && Value->TryGetString(Op.Name) && !Op.Name.IsEmpty())
			{
				RemoveExposedPinOps.Add(MoveTemp(Op));
				continue;
			}

			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("remove_exposed_pins entry is not a string/object"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("name"), Op.Name) || Op.Name.IsEmpty())
			{
				ConversionErrors.Add(TEXT("remove_exposed_pins entry missing name"));
				continue;
			}
			RemoveExposedPinOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* RenameExposedPinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("rename_exposed_pins"), RenameExposedPinsArray) && RenameExposedPinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *RenameExposedPinsArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("rename_exposed_pins entry is not an object"));
				continue;
			}

			FExposedPinRenameOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("name"), Op.Name) || Op.Name.IsEmpty())
			{
				ConversionErrors.Add(TEXT("rename_exposed_pins entry missing name"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("new_name"), Op.NewName) || Op.NewName.IsEmpty())
			{
				ConversionErrors.Add(TEXT("rename_exposed_pins entry missing new_name"));
				continue;
			}
			RenameExposedPinOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ChangeExposedPinTypesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("change_exposed_pin_types"), ChangeExposedPinTypesArray) && ChangeExposedPinTypesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ChangeExposedPinTypesArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("change_exposed_pin_types entry is not an object"));
				continue;
			}

			FExposedPinTypeChangeOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("name"), Op.Name) || Op.Name.IsEmpty())
			{
				ConversionErrors.Add(TEXT("change_exposed_pin_types entry missing name"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("cpp_type"), Op.CPPType) || Op.CPPType.IsEmpty())
			{
				ConversionErrors.Add(TEXT("change_exposed_pin_types entry missing cpp_type"));
				continue;
			}
			(*OpObj)->TryGetStringField(TEXT("cpp_type_object_path"), Op.CPPTypeObjectPath);
			ChangeExposedPinTypeOps.Add(MoveTemp(Op));
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* ReorderExposedPinsArray = nullptr;
	if (Args->TryGetArrayField(TEXT("reorder_exposed_pins"), ReorderExposedPinsArray) && ReorderExposedPinsArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *ReorderExposedPinsArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("reorder_exposed_pins entry is not an object"));
				continue;
			}

			FExposedPinReorderOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("name"), Op.Name) || Op.Name.IsEmpty())
			{
				ConversionErrors.Add(TEXT("reorder_exposed_pins entry missing name"));
				continue;
			}
			double Index = -1.0;
			if (!(*OpObj)->TryGetNumberField(TEXT("index"), Index))
			{
				ConversionErrors.Add(TEXT("reorder_exposed_pins entry missing index"));
				continue;
			}
			Op.Index = FMath::RoundToInt(Index);
			ReorderExposedPinOps.Add(MoveTemp(Op));
		}
	}

	auto ParseNodeTextOps = [&](const FString& FieldName, auto&& AddOp)
	{
		const TArray<TSharedPtr<FJsonValue>>* OpArray = nullptr;
		if (!Args->TryGetArrayField(FieldName, OpArray) || !OpArray)
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *OpArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(FString::Printf(TEXT("%s entry is not an object"), *FieldName));
				continue;
			}
			AddOp(*OpObj);
		}
	};

	ParseNodeTextOps(TEXT("set_node_categories"), [&](const TSharedPtr<FJsonObject>& Obj)
	{
		FNodeCategoryOp Op;
		if (!Obj->TryGetStringField(TEXT("node"), Op.NodeRef) || Op.NodeRef.IsEmpty())
		{
			ConversionErrors.Add(TEXT("set_node_categories entry missing node"));
			return;
		}
		Obj->TryGetStringField(TEXT("category"), Op.Category);
		SetNodeCategoryOps.Add(MoveTemp(Op));
	});

	ParseNodeTextOps(TEXT("set_node_keywords"), [&](const TSharedPtr<FJsonObject>& Obj)
	{
		FNodeKeywordsOp Op;
		if (!Obj->TryGetStringField(TEXT("node"), Op.NodeRef) || Op.NodeRef.IsEmpty())
		{
			ConversionErrors.Add(TEXT("set_node_keywords entry missing node"));
			return;
		}
		Obj->TryGetStringField(TEXT("keywords"), Op.Keywords);
		SetNodeKeywordsOps.Add(MoveTemp(Op));
	});

	ParseNodeTextOps(TEXT("set_node_descriptions"), [&](const TSharedPtr<FJsonObject>& Obj)
	{
		FNodeDescriptionOp Op;
		if (!Obj->TryGetStringField(TEXT("node"), Op.NodeRef) || Op.NodeRef.IsEmpty())
		{
			ConversionErrors.Add(TEXT("set_node_descriptions entry missing node"));
			return;
		}
		Obj->TryGetStringField(TEXT("description"), Op.Description);
		SetNodeDescriptionOps.Add(MoveTemp(Op));
	});

	const TArray<TSharedPtr<FJsonValue>>* SetPinCategoriesArray = nullptr;
	if (Args->TryGetArrayField(TEXT("set_pin_categories"), SetPinCategoriesArray) && SetPinCategoriesArray)
	{
		for (const TSharedPtr<FJsonValue>& Value : *SetPinCategoriesArray)
		{
			const TSharedPtr<FJsonObject>* OpObj = nullptr;
			if (!Value.IsValid() || !Value->TryGetObject(OpObj) || !OpObj || !OpObj->IsValid())
			{
				ConversionErrors.Add(TEXT("set_pin_categories entry is not an object"));
				continue;
			}

			FPinCategoryOp Op;
			if (!(*OpObj)->TryGetStringField(TEXT("pin_path"), Op.PinPath) || Op.PinPath.IsEmpty())
			{
				ConversionErrors.Add(TEXT("set_pin_categories entry missing pin_path"));
				continue;
			}
			if (!(*OpObj)->TryGetStringField(TEXT("category"), Op.Category))
			{
				Op.bClear = true;
			}
			else
			{
				bool bClearExplicit = false;
				if ((*OpObj)->TryGetBoolField(TEXT("clear"), bClearExplicit))
				{
					Op.bClear = bClearExplicit;
				}
				else
				{
					Op.bClear = Op.Category.IsEmpty();
				}
			}
			SetPinCategoryOps.Add(MoveTemp(Op));
		}
	}

	if (ConversionErrors.Num() > 0)
	{
		return FToolResult::Fail(FString::Join(ConversionErrors, TEXT("\n")));
	}

	const bool bHasOps =
		NodesToAdd.Num() > 0 ||
		NodesToRemove.Num() > 0 ||
		LinksToAdd.Num() > 0 ||
		LinksToBreak.Num() > 0 ||
		DefaultsToSet.Num() > 0 ||
		MoveNodeOps.Num() > 0 ||
		AlignNodeOps.Num() > 0 ||
		LayoutNodeOps.Num() > 0 ||
		CommentOps.Num() > 0 ||
		AddArrayPinOps.Num() > 0 ||
		InsertArrayPinOps.Num() > 0 ||
		RemoveArrayPinOps.Num() > 0 ||
		BindVariableOps.Num() > 0 ||
		PromotePinOps.Num() > 0 ||
		PinExpansionOps.Num() > 0 ||
		AddExposedPinOps.Num() > 0 ||
		RemoveExposedPinOps.Num() > 0 ||
		RenameExposedPinOps.Num() > 0 ||
		ChangeExposedPinTypeOps.Num() > 0 ||
		ReorderExposedPinOps.Num() > 0 ||
		SetNodeCategoryOps.Num() > 0 ||
		SetNodeKeywordsOps.Num() > 0 ||
		SetNodeDescriptionOps.Num() > 0 ||
		SetPinCategoryOps.Num() > 0;

	if (!bHasOps)
	{
		return FToolResult::Fail(TEXT("No supported ControlRig graph operations found. Supported: find_nodes/query, add_nodes, delete_nodes, connections, disconnect, set_pins, move_nodes, align_nodes, layout_nodes, add_comments, add_array_pins, insert_array_pins, remove_array_pins, bind_pin_variables, promote_pins, set_pin_expansion, add_exposed_pins, remove_exposed_pins, rename_exposed_pins, change_exposed_pin_types, reorder_exposed_pins, set_node_categories, set_node_keywords, set_node_descriptions, set_pin_categories."));
	}

	TArray<FString> Results;
	int32 TotalChanges = 0;
	TotalChanges += AddNodes(Controller, BP, NodesToAdd, Results);
	TotalChanges += RemoveNodes(Controller, NodesToRemove, Results);
	TotalChanges += AddLinks(Controller, LinksToAdd, Results);
	TotalChanges += BreakLinks(Controller, LinksToBreak, Results);
	TotalChanges += SetPinDefaults(Controller, DefaultsToSet, Results);
	TotalChanges += MoveNodes(Controller, MoveNodeOps, Results);
	TotalChanges += AlignNodes(Controller, AlignNodeOps, Results);
	TotalChanges += LayoutNodes(Controller, LayoutNodeOps, Results);
	TotalChanges += AddComments(Controller, CommentOps, Results);
	TotalChanges += AddArrayPins(Controller, AddArrayPinOps, Results);
	TotalChanges += AddArrayPins(Controller, InsertArrayPinOps, Results);
	TotalChanges += RemoveArrayPins(Controller, RemoveArrayPinOps, Results);
	TotalChanges += BindPinVariables(Controller, BindVariableOps, Results);
	TotalChanges += PromotePins(Controller, PromotePinOps, Results);
	TotalChanges += SetPinExpansion(Controller, PinExpansionOps, Results);
	TotalChanges += AddExposedPins(Controller, AddExposedPinOps, Results);
	TotalChanges += RemoveExposedPins(Controller, RemoveExposedPinOps, Results);
	TotalChanges += RenameExposedPins(Controller, RenameExposedPinOps, Results);
	TotalChanges += ChangeExposedPinTypes(Controller, ChangeExposedPinTypeOps, Results);
	TotalChanges += ReorderExposedPins(Controller, ReorderExposedPinOps, Results);
	TotalChanges += SetNodeCategories(Controller, SetNodeCategoryOps, Results);
	TotalChanges += SetNodeKeywords(Controller, SetNodeKeywordsOps, Results);
	TotalChanges += SetNodeDescriptions(Controller, SetNodeDescriptionOps, Results);
	TotalChanges += SetPinCategories(Controller, SetPinCategoryOps, Results);

	if (TotalChanges > 0)
	{
		BP->RequestAutoVMRecompilation();
		FBlueprintEditorUtils::MarkBlueprintAsModified(BP);
	}

	FString Output = FString::Printf(TEXT("Control Rig '%s': %d graph changes applied.\n\n"), *AssetName, TotalChanges);
	Output += FString::Join(Results, TEXT("\n"));
	return FToolResult::Ok(Output);
}
