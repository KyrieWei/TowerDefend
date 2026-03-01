// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/InheritableComponentHandler.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_Composite.h"
#include "UObject/UnrealType.h"
#include "GameFramework/Actor.h"
#include "Components/ActorComponent.h"

FString FReadFileTool::GetBlueprintSummary(UBlueprint* Blueprint)
{
	FString ParentName = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");

	int32 ComponentCount = 0;
	if (Blueprint->SimpleConstructionScript)
	{
		ComponentCount = Blueprint->SimpleConstructionScript->GetAllNodes().Num();
	}
	int32 VarCount = Blueprint->NewVariables.Num();

	// Collect composite graphs recursively
	struct FCompositeGraphInfo
	{
		UEdGraph* Graph;
		FString Selector; // e.g. "composite:EventGraph/MyComposite"
	};
	TArray<FCompositeGraphInfo> CompositeGraphs;
	{
		TSet<UEdGraph*> Visited;
		auto CollectComposites = [&](UEdGraph* GraphToSearch, const FString& ParentName, auto&& Self) -> void
		{
			if (!GraphToSearch || Visited.Contains(GraphToSearch)) return;
			Visited.Add(GraphToSearch);

			for (UEdGraphNode* Node : GraphToSearch->Nodes)
			{
				if (!Node) continue;
				if (UK2Node_Composite* Composite = Cast<UK2Node_Composite>(Node))
				{
					if (Composite->BoundGraph)
					{
						FString CompName = Composite->GetNodeTitle(ENodeTitleType::ListView).ToString();
						if (CompName.IsEmpty())
						{
							CompName = Composite->BoundGraph->GetName();
						}
						CompositeGraphs.Add({Composite->BoundGraph,
							FString::Printf(TEXT("composite:%s/%s"), *ParentName, *CompName)});
						Self(Composite->BoundGraph, ParentName, Self);
					}
				}
			}
		};

		for (UEdGraph* Graph : Blueprint->UbergraphPages)
		{
			if (Graph) CollectComposites(Graph, Graph->GetName(), CollectComposites);
		}
		for (UEdGraph* Graph : Blueprint->FunctionGraphs)
		{
			if (Graph) CollectComposites(Graph, Graph->GetName(), CollectComposites);
		}
		for (UEdGraph* Graph : Blueprint->MacroGraphs)
		{
			if (Graph) CollectComposites(Graph, Graph->GetName(), CollectComposites);
		}
	}

	int32 GraphCount = Blueprint->UbergraphPages.Num() + Blueprint->FunctionGraphs.Num() + Blueprint->MacroGraphs.Num() + CompositeGraphs.Num();
	int32 InterfaceCount = Blueprint->ImplementedInterfaces.Num();

	// Build class flags string
	TArray<FString> ClassFlags;
	if (Blueprint->bGenerateAbstractClass) ClassFlags.Add(TEXT("Abstract"));
	if (Blueprint->bGenerateConstClass) ClassFlags.Add(TEXT("Const"));
	if (Blueprint->bDeprecate) ClassFlags.Add(TEXT("Deprecated"));

	FString Output = FString::Printf(TEXT("# BLUEPRINT %s parent=%s\ncomponents=%d variables=%d graphs=%d interfaces=%d\n"),
		*Blueprint->GetName(), *ParentName, ComponentCount, VarCount, GraphCount, InterfaceCount);

	if (ClassFlags.Num() > 0)
	{
		Output += TEXT("flags=") + FString::Join(ClassFlags, TEXT(",")) + TEXT("\n");
	}

	// Add graph list
	Output += FString::Printf(TEXT("\n# GRAPHS %d\n"), GraphCount);

	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		Output += FString::Printf(TEXT("%s\tubergraph\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		Output += FString::Printf(TEXT("%s\tfunction\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		Output += FString::Printf(TEXT("%s\tmacro\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
	}
	for (const FCompositeGraphInfo& Info : CompositeGraphs)
	{
		Output += FString::Printf(TEXT("%s\t%s\t%d\n"), *Info.Graph->GetName(), *Info.Selector, Info.Graph->Nodes.Num());
	}

	return Output;
}

FString FReadFileTool::GetBlueprintVariables(UBlueprint* Blueprint, int32 Offset, int32 Limit)
{
	const TArray<FBPVariableDescription>& Vars = Blueprint->NewVariables;
	int32 Total = Vars.Num();

	if (Total == 0)
	{
		return TEXT("# VARIABLES 0\n");
	}

	int32 StartIdx = Offset - 1;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# VARIABLES %d\n"), Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const FBPVariableDescription& Var = Vars[i];

		// Get type name
		FString TypeName = Var.VarType.PinCategory.ToString();
		if (Var.VarType.PinSubCategoryObject.IsValid())
		{
			TypeName = Var.VarType.PinSubCategoryObject->GetName();
		}

		// Container type prefix
		if (Var.VarType.IsArray())
		{
			TypeName = FString::Printf(TEXT("Array<%s>"), *TypeName);
		}
		else if (Var.VarType.IsSet())
		{
			TypeName = FString::Printf(TEXT("Set<%s>"), *TypeName);
		}
		else if (Var.VarType.IsMap())
		{
			FString ValueType = Var.VarType.PinValueType.TerminalCategory.ToString();
			if (Var.VarType.PinValueType.TerminalSubCategoryObject.IsValid())
			{
				ValueType = Var.VarType.PinValueType.TerminalSubCategoryObject->GetName();
			}
			TypeName = FString::Printf(TEXT("Map<%s,%s>"), *TypeName, *ValueType);
		}

		// Get default value
		FString DefaultValue = Var.DefaultValue.IsEmpty() ? TEXT("None") : Var.DefaultValue;

		// Build flags string from PropertyFlags
		TArray<FString> Flags;
		if (Var.PropertyFlags & CPF_Net)
		{
			Flags.Add(TEXT("Replicated"));
		}
		if (Var.PropertyFlags & CPF_RepNotify)
		{
			Flags.Add(FString::Printf(TEXT("RepNotify(%s)"), *Var.RepNotifyFunc.ToString()));
		}
		if (Var.PropertyFlags & CPF_ExposeOnSpawn)
		{
			Flags.Add(TEXT("ExposeOnSpawn"));
		}
		if (Var.PropertyFlags & CPF_DisableEditOnInstance)
		{
			Flags.Add(TEXT("Private"));
		}
		if (Var.PropertyFlags & CPF_Transient)
		{
			Flags.Add(TEXT("Transient"));
		}
		if (Var.PropertyFlags & CPF_SaveGame)
		{
			Flags.Add(TEXT("SaveGame"));
		}

		FString FlagsStr = Flags.Num() > 0 ? FString::Printf(TEXT("\t[%s]"), *FString::Join(Flags, TEXT(","))) : TEXT("");

		Output += FString::Printf(TEXT("%s\t%s\t%s%s\n"), *Var.VarName.ToString(), *TypeName, *DefaultValue, *FlagsStr);
	}

	return Output;
}

FString FReadFileTool::GetBlueprintComponents(UBlueprint* Blueprint, int32 Offset, int32 Limit)
{
	// Collect all components: Name, Class, Parent, Source (this/inherited/native)
	struct FComponentInfo
	{
		FString Name;
		FString ClassName;
		FString ParentName;
		FString Source; // "this", "inherited", "native"
	};
	TArray<FComponentInfo> AllComponents;

	// 1. Get components from this Blueprint's SCS
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate)
			{
				FComponentInfo Info;
				Info.Name = Node->GetVariableName().ToString();
				Info.ClassName = Node->ComponentTemplate->GetClass()->GetName();
				Info.ParentName = (Node->ParentComponentOrVariableName != NAME_None)
					? Node->ParentComponentOrVariableName.ToString()
					: TEXT("ROOT");
				Info.Source = TEXT("this");
				AllComponents.Add(Info);
			}
		}
	}

	// 2. Walk up the Blueprint hierarchy to get inherited components
	UClass* ParentClass = Blueprint->ParentClass;
	while (ParentClass)
	{
		// Check if parent is a Blueprint class
		if (UBlueprintGeneratedClass* ParentBPGC = Cast<UBlueprintGeneratedClass>(ParentClass))
		{
			// Get the parent Blueprint
			UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentBPGC->ClassGeneratedBy);
			if (ParentBlueprint && ParentBlueprint->SimpleConstructionScript)
			{
				for (USCS_Node* Node : ParentBlueprint->SimpleConstructionScript->GetAllNodes())
				{
					if (Node && Node->ComponentTemplate)
					{
						// Check if already added (overridden in child)
						FString NodeName = Node->GetVariableName().ToString();
						bool bAlreadyExists = AllComponents.ContainsByPredicate([&NodeName](const FComponentInfo& Info) {
							return Info.Name.Equals(NodeName, ESearchCase::IgnoreCase);
						});

						if (!bAlreadyExists)
						{
							FComponentInfo Info;
							Info.Name = NodeName;
							Info.ClassName = Node->ComponentTemplate->GetClass()->GetName();
							Info.ParentName = (Node->ParentComponentOrVariableName != NAME_None)
								? Node->ParentComponentOrVariableName.ToString()
								: TEXT("ROOT");
							Info.Source = FString::Printf(TEXT("inherited:%s"), *ParentBlueprint->GetName());
							AllComponents.Add(Info);
						}
					}
				}
			}
		}
		else
		{
			// Native C++ class - get components from CDO
			if (ParentClass->IsChildOf(AActor::StaticClass()))
			{
				AActor* CDO = Cast<AActor>(ParentClass->GetDefaultObject());
				if (CDO)
				{
					TArray<UActorComponent*> NativeComponents;
					CDO->GetComponents<UActorComponent>(NativeComponents);

					for (UActorComponent* Comp : NativeComponents)
					{
						if (Comp)
						{
							FString CompName = Comp->GetName();
							bool bAlreadyExists = AllComponents.ContainsByPredicate([&CompName](const FComponentInfo& Info) {
								return Info.Name.Equals(CompName, ESearchCase::IgnoreCase);
							});

							if (!bAlreadyExists)
							{
								FComponentInfo Info;
								Info.Name = CompName;
								Info.ClassName = Comp->GetClass()->GetName();
								Info.ParentName = TEXT("ROOT");
								Info.Source = FString::Printf(TEXT("native:%s"), *ParentClass->GetName());
								AllComponents.Add(Info);
							}
						}
					}
				}
			}
			break; // Stop at native class
		}

		ParentClass = ParentClass->GetSuperClass();
	}

	int32 Total = AllComponents.Num();

	if (Total == 0)
	{
		return TEXT("# COMPONENTS 0\n");
	}

	int32 StartIdx = FMath::Max(0, Offset - 1);
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# COMPONENTS %d (showing %d-%d)\n"), Total, StartIdx + 1, EndIdx);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const FComponentInfo& Info = AllComponents[i];
		// Format: Name<tab>Class<tab>Parent<tab>Source
		Output += FString::Printf(TEXT("%s\t%s\t%s\t%s\n"),
			*Info.Name, *Info.ClassName, *Info.ParentName, *Info.Source);
	}

	return Output;
}

FString FReadFileTool::GetBlueprintComponentProperties(UBlueprint* Blueprint, const FString& ComponentName, int32 Offset, int32 Limit)
{
	UObject* Component = nullptr;
	FString SourceInfo;

	// 1. Search in this Blueprint's SCS (directly added components)
	if (Blueprint->SimpleConstructionScript)
	{
		for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
		{
			if (Node && Node->ComponentTemplate &&
				Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
			{
				Component = Node->ComponentTemplate;
				SourceInfo = TEXT("this");
				break;
			}
		}
	}

	// 2. Check InheritableComponentHandler for overrides of inherited components
	// This is where modified inherited component values are stored!
	if (!Component)
	{
		UInheritableComponentHandler* ICH = Blueprint->GetInheritableComponentHandler(false);
		if (ICH)
		{
			// Search parent hierarchy for the original component to build the key
			UClass* ParentClass = Blueprint->ParentClass;
			while (ParentClass && !Component)
			{
				if (UBlueprintGeneratedClass* ParentBPGC = Cast<UBlueprintGeneratedClass>(ParentClass))
				{
					UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentBPGC->ClassGeneratedBy);
					if (ParentBlueprint && ParentBlueprint->SimpleConstructionScript)
					{
						for (USCS_Node* Node : ParentBlueprint->SimpleConstructionScript->GetAllNodes())
						{
							if (Node && Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
							{
								// Found the inherited component - check if we have an override
								FComponentKey Key(Node);
								UActorComponent* Override = ICH->GetOverridenComponentTemplate(Key);
								if (Override)
								{
									Component = Override;
									SourceInfo = FString::Printf(TEXT("override:%s"), *ParentBlueprint->GetName());
								}
								else if (Node->ComponentTemplate)
								{
									// No override, use parent's original template
									Component = Node->ComponentTemplate;
									SourceInfo = FString::Printf(TEXT("inherited:%s"), *ParentBlueprint->GetName());
								}
								break;
							}
						}
					}
					if (Component) break;
					ParentClass = ParentClass->GetSuperClass();
				}
				else
				{
					break; // Stop at native class
				}
			}
		}
	}

	// 3. If still not found, search parent hierarchy without ICH (fallback)
	if (!Component)
	{
		UClass* ParentClass = Blueprint->ParentClass;
		while (ParentClass && !Component)
		{
			if (UBlueprintGeneratedClass* ParentBPGC = Cast<UBlueprintGeneratedClass>(ParentClass))
			{
				UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentBPGC->ClassGeneratedBy);
				if (ParentBlueprint && ParentBlueprint->SimpleConstructionScript)
				{
					for (USCS_Node* Node : ParentBlueprint->SimpleConstructionScript->GetAllNodes())
					{
						if (Node && Node->ComponentTemplate &&
							Node->GetVariableName().ToString().Equals(ComponentName, ESearchCase::IgnoreCase))
						{
							Component = Node->ComponentTemplate;
							SourceInfo = FString::Printf(TEXT("inherited:%s"), *ParentBlueprint->GetName());
							break;
						}
					}
				}
				ParentClass = ParentClass->GetSuperClass();
			}
			else
			{
				// Native C++ class - check CDO
				if (ParentClass->IsChildOf(AActor::StaticClass()))
				{
					AActor* CDO = Cast<AActor>(ParentClass->GetDefaultObject());
					if (CDO)
					{
						TArray<UActorComponent*> NativeComponents;
						CDO->GetComponents<UActorComponent>(NativeComponents);

						for (UActorComponent* Comp : NativeComponents)
						{
							if (Comp && Comp->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
							{
								Component = Comp;
								SourceInfo = FString::Printf(TEXT("native:%s"), *ParentClass->GetName());
								break;
							}
						}
					}
				}
				break; // Stop at native class
			}
		}
	}

	if (!Component)
	{
		return FString::Printf(TEXT("! Component '%s' not found\n"), *ComponentName);
	}

	UClass* ComponentClass = Component->GetClass();

	// Collect editable properties
	TArray<TPair<FString, TPair<FString, FString>>> Properties; // Name -> (Type, Value)

	for (TFieldIterator<FProperty> PropIt(ComponentClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Only show editable properties
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Skip deprecated properties
		if (Property->HasAnyPropertyFlags(CPF_Deprecated))
		{
			continue;
		}

		FString PropName = Property->GetName();
		FString PropType = GetPropertyTypeString(Property);
		FString PropValue = GetPropertyValueString(Component, Property);

		Properties.Add(TPair<FString, TPair<FString, FString>>(PropName, TPair<FString, FString>(PropType, PropValue)));
	}

	int32 Total = Properties.Num();

	if (Total == 0)
	{
		return FString::Printf(TEXT("# COMPONENT_PROPERTIES %s (%s) [%s] 0\n"),
			*ComponentName, *ComponentClass->GetName(), *SourceInfo);
	}

	// Apply pagination
	int32 StartIdx = FMath::Max(0, Offset - 1);
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# COMPONENT_PROPERTIES %s (%s) [%s] %d (showing %d-%d)\n"),
		*ComponentName, *ComponentClass->GetName(), *SourceInfo, Total, StartIdx + 1, EndIdx);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const auto& Prop = Properties[i];
		// Format: PropertyName<tab>Type<tab>Value
		Output += FString::Printf(TEXT("%s\t%s\t%s\n"), *Prop.Key, *Prop.Value.Key, *Prop.Value.Value);
	}

	return Output;
}

FString FReadFileTool::GetPropertyValueString(UObject* Object, FProperty* Property)
{
	if (!Object || !Property) return TEXT("");
	return NeoStackToolUtils::GetPropertyValueAsString(Object, Property, Object, 200);
}

FString FReadFileTool::GetBlueprintGraphs(UBlueprint* Blueprint, int32 Offset, int32 Limit)
{
	FString Output;

	// Collect all graphs with nodes and connections
	for (UEdGraph* Graph : Blueprint->UbergraphPages)
	{
		Output += GetGraphWithNodes(Graph, TEXT("ubergraph"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(Graph);
		Output += TEXT("\n");
	}

	for (UEdGraph* Graph : Blueprint->FunctionGraphs)
	{
		Output += GetGraphWithNodes(Graph, TEXT("function"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(Graph);
		Output += TEXT("\n");
	}

	for (UEdGraph* Graph : Blueprint->MacroGraphs)
	{
		Output += GetGraphWithNodes(Graph, TEXT("macro"), TEXT(""), Offset, Limit);
		Output += TEXT("\n") + GetGraphConnections(Graph);
		Output += TEXT("\n");
	}

	return Output;
}

FString FReadFileTool::GetBlueprintInterfaces(UBlueprint* Blueprint)
{
	int32 Total = Blueprint->ImplementedInterfaces.Num();

	if (Total == 0)
	{
		return TEXT("# INTERFACES 0\n");
	}

	FString Output = FString::Printf(TEXT("# INTERFACES %d\n"), Total);

	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		if (Interface.Interface)
		{
			Output += FString::Printf(TEXT("%s"), *Interface.Interface->GetName());
			if (Interface.Graphs.Num() > 0)
			{
				TArray<FString> FuncNames;
				for (UEdGraph* Graph : Interface.Graphs)
				{
					if (Graph) FuncNames.Add(Graph->GetName());
				}
				Output += TEXT(" [") + FString::Join(FuncNames, TEXT(", ")) + TEXT("]");
			}
			Output += TEXT("\n");
		}
	}

	return Output;
}

FString FReadFileTool::GetGraphType(UEdGraph* Graph, UBlueprint* Blueprint)
{
	return NeoStackToolUtils::GetGraphType(Graph, Blueprint);
}
