// Copyright 2025 Betide Studio. All Rights Reserved.

#include "ACPContextSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_MacroInstance.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Kismet2/BlueprintEditorUtils.h"

FString FACPContextSerializer::SerializeNode(const UEdGraphNode* Node, bool bIncludeConnections)
{
	if (!Node)
	{
		return TEXT("(null node)");
	}

	FString Output;

	// Node identification
	Output += FString::Printf(TEXT("## Node: %s\n"), *Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	Output += FString::Printf(TEXT("Type: %s\n"), *Node->GetClass()->GetName());
	Output += FString::Printf(TEXT("GUID: %s\n"), *Node->NodeGuid.ToString());
	Output += FString::Printf(TEXT("Position: (%d, %d)\n"), Node->NodePosX, Node->NodePosY);

	// Node comment if present
	if (!Node->NodeComment.IsEmpty())
	{
		Output += FString::Printf(TEXT("Comment: %s\n"), *Node->NodeComment);
	}

	Output += TEXT("\n");

	// Input pins
	Output += TEXT("### Input Pins\n");
	bool bHasInputs = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Input && !Pin->bHidden)
		{
			bHasInputs = true;
			Output += SerializePinForContext(Pin);

			if (bIncludeConnections && Pin->LinkedTo.Num() > 0)
			{
				Output += TEXT("    Connected from: ");
				Output += GetConnectedNodeSummary(Pin);
				Output += TEXT("\n");
			}
		}
	}
	if (!bHasInputs)
	{
		Output += TEXT("  (none)\n");
	}

	// Output pins
	Output += TEXT("\n### Output Pins\n");
	bool bHasOutputs = false;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output && !Pin->bHidden)
		{
			bHasOutputs = true;
			Output += SerializePinForContext(Pin);

			if (bIncludeConnections && Pin->LinkedTo.Num() > 0)
			{
				Output += TEXT("    Connected to: ");
				Output += GetConnectedNodeSummary(Pin);
				Output += TEXT("\n");
			}
		}
	}
	if (!bHasOutputs)
	{
		Output += TEXT("  (none)\n");
	}

	// Node-specific details
	if (const UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		if (UFunction* Func = CallNode->GetTargetFunction())
		{
			Output += TEXT("\n### Function Details\n");
			Output += FString::Printf(TEXT("Function: %s\n"), *Func->GetName());

			if (Func->GetOwnerClass())
			{
				Output += FString::Printf(TEXT("Class: %s\n"), *Func->GetOwnerClass()->GetName());
			}

			TArray<FString> Flags;
			if (Func->HasAnyFunctionFlags(FUNC_Const)) Flags.Add(TEXT("Const"));
			if (Func->HasAnyFunctionFlags(FUNC_Static)) Flags.Add(TEXT("Static"));
			if (Func->HasMetaData(TEXT("Latent"))) Flags.Add(TEXT("Latent"));
			if (Func->HasMetaData(TEXT("DeprecatedFunction"))) Flags.Add(TEXT("Deprecated"));

			if (Flags.Num() > 0)
			{
				Output += FString::Printf(TEXT("Flags: %s\n"), *FString::Join(Flags, TEXT(", ")));
			}
		}
	}
	else if (const UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(Node))
	{
		Output += TEXT("\n### Variable Details\n");
		Output += FString::Printf(TEXT("Variable: %s\n"), *VarNode->GetVarName().ToString());
	}
	else if (const UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		Output += TEXT("\n### Event Details\n");
		Output += FString::Printf(TEXT("Event: %s\n"), *EventNode->GetNodeTitle(ENodeTitleType::MenuTitle).ToString());
	}
	else if (const UK2Node_CustomEvent* CustomEventNode = Cast<UK2Node_CustomEvent>(Node))
	{
		Output += TEXT("\n### Custom Event Details\n");
		Output += FString::Printf(TEXT("Event Name: %s\n"), *CustomEventNode->CustomFunctionName.ToString());
	}
	else if (const UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
	{
		Output += TEXT("\n### Macro Details\n");
		if (MacroNode->GetMacroGraph())
		{
			Output += FString::Printf(TEXT("Macro: %s\n"), *MacroNode->GetMacroGraph()->GetName());
		}
	}

	return Output;
}

FString FACPContextSerializer::SerializeBlueprintOverview(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return TEXT("(null blueprint)");
	}

	FString Output;

	// Blueprint identification
	Output += FString::Printf(TEXT("# Blueprint: %s\n"), *Blueprint->GetName());
	Output += FString::Printf(TEXT("Path: %s\n"), *Blueprint->GetPathName());

	if (Blueprint->ParentClass)
	{
		Output += FString::Printf(TEXT("Parent Class: %s\n"), *Blueprint->ParentClass->GetName());
	}

	Output += TEXT("\n");

	// Variables
	Output += TEXT("## Variables\n");
	if (Blueprint->NewVariables.Num() > 0)
	{
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			FString TypeStr = UEdGraphSchema_K2::TypeToText(Var.VarType).ToString();
			Output += FString::Printf(TEXT("- %s: %s"), *Var.VarName.ToString(), *TypeStr);

			if (!Var.DefaultValue.IsEmpty())
			{
				FString DefaultVal = Var.DefaultValue;
				if (DefaultVal.Len() > 50)
				{
					DefaultVal = DefaultVal.Left(47) + TEXT("...");
				}
				Output += FString::Printf(TEXT(" = %s"), *DefaultVal);
			}

			Output += TEXT("\n");
		}
	}
	else
	{
		Output += TEXT("(none)\n");
	}

	Output += TEXT("\n");

	// Components
	Output += TEXT("## Components\n");
	bool bHasComponents = false;
	if (Blueprint->SimpleConstructionScript)
	{
		TArray<USCS_Node*> AllNodes = Blueprint->SimpleConstructionScript->GetAllNodes();
		for (USCS_Node* SCSNode : AllNodes)
		{
			if (SCSNode && SCSNode->ComponentClass)
			{
				bHasComponents = true;
				FString ParentInfo;
				if (SCSNode->ParentComponentOrVariableName != NAME_None)
				{
					ParentInfo = FString::Printf(TEXT(" (parent: %s)"), *SCSNode->ParentComponentOrVariableName.ToString());
				}
				Output += FString::Printf(TEXT("- %s: %s%s\n"),
					*SCSNode->GetVariableName().ToString(),
					*SCSNode->ComponentClass->GetName(),
					*ParentInfo);
			}
		}
	}
	if (!bHasComponents)
	{
		Output += TEXT("(none)\n");
	}

	Output += TEXT("\n");

	// Functions
	Output += TEXT("## Functions\n");
	if (Blueprint->FunctionGraphs.Num() > 0)
	{
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph)
			{
				Output += FString::Printf(TEXT("- %s (%d nodes)\n"), *Graph->GetName(), Graph->Nodes.Num());
			}
		}
	}
	else
	{
		Output += TEXT("(none)\n");
	}

	Output += TEXT("\n");

	// Event Graphs
	Output += TEXT("## Event Graphs\n");
	if (Blueprint->UbergraphPages.Num() > 0)
	{
		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph)
			{
				Output += FString::Printf(TEXT("- %s (%d nodes)\n"), *Graph->GetName(), Graph->Nodes.Num());
			}
		}
	}
	else
	{
		Output += TEXT("(none)\n");
	}

	// Macros
	if (Blueprint->MacroGraphs.Num() > 0)
	{
		Output += TEXT("\n## Macros\n");
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph)
			{
				Output += FString::Printf(TEXT("- %s (%d nodes)\n"), *Graph->GetName(), Graph->Nodes.Num());
			}
		}
	}

	// Interfaces
	if (Blueprint->ImplementedInterfaces.Num() > 0)
	{
		Output += TEXT("\n## Implemented Interfaces\n");
		for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
		{
			if (Interface.Interface)
			{
				Output += FString::Printf(TEXT("- %s\n"), *Interface.Interface->GetName());
			}
		}
	}

	return Output;
}

FString FACPContextSerializer::GetNodeDisplayName(const UEdGraphNode* Node)
{
	if (!Node)
	{
		return TEXT("Unknown Node");
	}

	return Node->GetNodeTitle(ENodeTitleType::MenuTitle).ToString();
}

FString FACPContextSerializer::GetBlueprintDisplayName(const UBlueprint* Blueprint)
{
	if (!Blueprint)
	{
		return TEXT("Unknown Blueprint");
	}

	return Blueprint->GetName();
}

FString FACPContextSerializer::SerializePinForContext(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("");
	}

	FString TypeStr = PinTypeToString(Pin);
	FString Line = FString::Printf(TEXT("  - %s (%s)"), *Pin->PinName.ToString(), *TypeStr);

	// Include default value if set (skip for exec pins)
	if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
	{
		FString DefaultStr;
		bool bHasDefault = false;

		if (!Pin->DefaultValue.IsEmpty())
		{
			bHasDefault = true;
			DefaultStr = Pin->DefaultValue;
		}
		else if (Pin->DefaultObject)
		{
			bHasDefault = true;
			DefaultStr = Pin->DefaultObject->GetName();
		}
		else if (!Pin->AutogeneratedDefaultValue.IsEmpty())
		{
			bHasDefault = true;
			DefaultStr = Pin->AutogeneratedDefaultValue;
		}

		if (bHasDefault)
		{
			// Truncate long values
			if (DefaultStr.Len() > 50)
			{
				DefaultStr = DefaultStr.Left(47) + TEXT("...");
			}
			Line += FString::Printf(TEXT(" = %s"), *DefaultStr);
		}
	}

	Line += TEXT("\n");
	return Line;
}

FString FACPContextSerializer::GetConnectedNodeSummary(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("");
	}

	TArray<FString> Summaries;
	for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
	{
		if (LinkedPin && LinkedPin->GetOwningNode())
		{
			Summaries.Add(FString::Printf(TEXT("%s.%s"),
				*LinkedPin->GetOwningNode()->GetNodeTitle(ENodeTitleType::MenuTitle).ToString(),
				*LinkedPin->PinName.ToString()));
		}
	}

	return FString::Join(Summaries, TEXT(", "));
}

FString FACPContextSerializer::PinTypeToString(const UEdGraphPin* Pin)
{
	if (!Pin)
	{
		return TEXT("unknown");
	}

	const FEdGraphPinType& PinType = Pin->PinType;

	// Handle exec pins
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
	{
		return TEXT("exec");
	}

	// Get base type name
	FString TypeName = PinType.PinCategory.ToString();

	// For object/struct types, include the subtype
	if (PinType.PinSubCategoryObject.IsValid())
	{
		TypeName = PinType.PinSubCategoryObject->GetName();
	}
	else if (!PinType.PinSubCategory.IsNone())
	{
		TypeName = PinType.PinSubCategory.ToString();
	}

	// Handle containers
	if (PinType.ContainerType == EPinContainerType::Array)
	{
		TypeName = FString::Printf(TEXT("Array<%s>"), *TypeName);
	}
	else if (PinType.ContainerType == EPinContainerType::Set)
	{
		TypeName = FString::Printf(TEXT("Set<%s>"), *TypeName);
	}
	else if (PinType.ContainerType == EPinContainerType::Map)
	{
		TypeName = FString::Printf(TEXT("Map<%s>"), *TypeName);
	}

	// Handle reference
	if (PinType.bIsReference)
	{
		TypeName += TEXT("&");
	}

	return TypeName;
}
