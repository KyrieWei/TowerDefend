// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTNode.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "UObject/UnrealType.h"

// Graph editor includes for GUID map
#include "BehaviorTreeGraph.h"
#include "BehaviorTreeGraphNode.h"
#include "AIGraphNode.h"

FString FReadFileTool::GetBehaviorTreeSummary(UBehaviorTree* BehaviorTree)
{
	FString BlackboardName = TEXT("None");
	if (BehaviorTree->BlackboardAsset)
	{
		BlackboardName = BehaviorTree->BlackboardAsset->GetName();
	}

	// Count nodes
	int32 TaskCount = 0;
	int32 CompositeCount = 0;
	int32 DecoratorCount = 0;
	int32 ServiceCount = 0;

	if (BehaviorTree->RootNode)
	{
		CountBTNodes(BehaviorTree->RootNode, TaskCount, CompositeCount, DecoratorCount, ServiceCount);
	}

	FString Output = FString::Printf(TEXT("# BEHAVIOR_TREE %s blackboard=%s\n"),
		*BehaviorTree->GetName(), *BlackboardName);
	Output += FString::Printf(TEXT("composites=%d tasks=%d decorators=%d services=%d\n"),
		CompositeCount, TaskCount, DecoratorCount, ServiceCount);

	return Output;
}

void FReadFileTool::CountBTNodes(UBTCompositeNode* Node, int32& OutTasks, int32& OutComposites, int32& OutDecorators, int32& OutServices)
{
	if (!Node)
	{
		return;
	}

	OutComposites++;

	// Count services on this composite node
	OutServices += Node->Services.Num();

	// Count children
	for (int32 i = 0; i < Node->GetChildrenNum(); i++)
	{
		const FBTCompositeChild& Child = Node->Children[i];

		// Count decorators on the child link (decorators are attached to edges, not nodes)
		OutDecorators += Child.Decorators.Num();

		if (Child.ChildComposite)
		{
			// Recurse into composite child
			CountBTNodes(Child.ChildComposite, OutTasks, OutComposites, OutDecorators, OutServices);
		}
		else if (Child.ChildTask)
		{
			OutTasks++;
			// Tasks can have services too
			OutServices += Child.ChildTask->Services.Num();
		}
	}
}

void FReadFileTool::BuildBTGuidMap(UBehaviorTree* BehaviorTree, TMap<UObject*, FGuid>& OutGuidMap, TMap<UObject*, FVector2D>& OutPositionMap)
{
	if (!BehaviorTree || !BehaviorTree->BTGraph)
	{
		return;
	}

	UBehaviorTreeGraph* BTGraph = Cast<UBehaviorTreeGraph>(BehaviorTree->BTGraph);
	if (!BTGraph)
	{
		return;
	}

	for (UEdGraphNode* Node : BTGraph->Nodes)
	{
		UAIGraphNode* AINode = Cast<UAIGraphNode>(Node);
		if (!AINode)
		{
			continue;
		}

		// Map main node's runtime instance to graph GUID and position
		if (AINode->NodeInstance)
		{
			OutGuidMap.Add(AINode->NodeInstance, AINode->NodeGuid);
			OutPositionMap.Add(AINode->NodeInstance, FVector2D(AINode->NodePosX, AINode->NodePosY));
		}

		// Map sub-nodes (decorators/services) — use parent position since sub-nodes don't have independent positions
		for (UAIGraphNode* SubNode : AINode->SubNodes)
		{
			if (SubNode && SubNode->NodeInstance)
			{
				OutGuidMap.Add(SubNode->NodeInstance, SubNode->NodeGuid);
			}
		}
	}
}

FString FReadFileTool::GetBehaviorTreeNodes(UBehaviorTree* BehaviorTree)
{
	if (!BehaviorTree->RootNode)
	{
		return TEXT("# NODES 0\n(no root node)\n");
	}

	// Build reverse map: runtime node → graph GUID + position
	TMap<UObject*, FGuid> GuidMap;
	TMap<UObject*, FVector2D> PositionMap;
	BuildBTGuidMap(BehaviorTree, GuidMap, PositionMap);

	FString Output = TEXT("# NODES\n");
	Output += GetBTNodeHierarchy(BehaviorTree->RootNode, 0, GuidMap, PositionMap);

	return Output;
}

// ---------------------------------------------------------------------------
// Helper: reconstruct a logic expression from DecoratorOps (post-order)
// Returns a human-readable string like "And(Blackboard, Distance)" or "Or(And(A, B), Not(C))"
// ---------------------------------------------------------------------------
static FString ReconstructDecoratorLogic(
	const TArray<UBTDecorator*>& Decorators,
	const TArray<FBTDecoratorLogic>& DecoratorOps)
{
	if (DecoratorOps.Num() == 0)
	{
		return TEXT("");
	}

	// If only one decorator and no logic ops (or just a single Test), no expression needed
	if (Decorators.Num() <= 1 && DecoratorOps.Num() <= 1)
	{
		return TEXT("");
	}

	// Use a stack to reconstruct the expression tree from post-order ops
	TArray<FString> Stack;

	for (const FBTDecoratorLogic& Op : DecoratorOps)
	{
		switch (Op.Operation)
		{
		case EBTDecoratorLogic::Test:
		{
			int32 Idx = Op.Number;
			if (Decorators.IsValidIndex(Idx) && Decorators[Idx])
			{
				FString DecName = Decorators[Idx]->GetClass()->GetName();
				DecName.RemoveFromStart(TEXT("BTDecorator_"));
				Stack.Push(DecName);
			}
			else
			{
				Stack.Push(FString::Printf(TEXT("Test[%d]"), Idx));
			}
			break;
		}
		case EBTDecoratorLogic::And:
		case EBTDecoratorLogic::Or:
		{
			FString OpName = (Op.Operation == EBTDecoratorLogic::And) ? TEXT("And") : TEXT("Or");
			int32 ChildCount = Op.Number;
			if (ChildCount <= 0 || ChildCount > Stack.Num())
			{
				Stack.Push(FString::Printf(TEXT("%s(?)"), *OpName));
				break;
			}
			// Pop children from stack (they were pushed in order)
			TArray<FString> Children;
			for (int32 c = 0; c < ChildCount; c++)
			{
				Children.Insert(Stack.Pop(), 0); // Insert at front to preserve order
			}
			Stack.Push(FString::Printf(TEXT("%s(%s)"), *OpName, *FString::Join(Children, TEXT(", "))));
			break;
		}
		case EBTDecoratorLogic::Not:
		{
			if (Stack.Num() > 0)
			{
				FString Child = Stack.Pop();
				Stack.Push(FString::Printf(TEXT("Not(%s)"), *Child));
			}
			else
			{
				Stack.Push(TEXT("Not(?)"));
			}
			break;
		}
		default:
			break;
		}
	}

	if (Stack.Num() == 1)
	{
		return Stack[0];
	}
	else if (Stack.Num() > 1)
	{
		// Multiple items remaining — implicit AND
		return FString::Printf(TEXT("And(%s)"), *FString::Join(Stack, TEXT(", ")));
	}
	return TEXT("");
}

// Helper: get short GUID string from the reverse map (first 8 chars)
static FString GetShortGuid(const UObject* RuntimeNode, const TMap<UObject*, FGuid>& GuidMap)
{
	if (!RuntimeNode)
	{
		return TEXT("");
	}
	const FGuid* Found = GuidMap.Find(const_cast<UObject*>(RuntimeNode));
	if (Found)
	{
		return Found->ToString().Left(8);
	}
	return TEXT("");
}

FString FReadFileTool::GetBTNodeHierarchy(UBTCompositeNode* Node, int32 Depth, const TMap<UObject*, FGuid>& GuidMap, const TMap<UObject*, FVector2D>& PositionMap)
{
	if (!Node)
	{
		return TEXT("");
	}

	FString Indent;
	for (int32 i = 0; i < Depth; i++)
	{
		Indent += TEXT("  ");
	}

	// Get node class name (remove UBT prefix for readability)
	FString NodeClass = Node->GetClass()->GetName();
	NodeClass.RemoveFromStart(TEXT("BT"));
	NodeClass.RemoveFromStart(TEXT("Composite_"));

	FString Output;
	FString NodeGuid = GetShortGuid(Node, GuidMap);
	const FVector2D* NodePos = PositionMap.Find(const_cast<UBTCompositeNode*>(Node));
	FString PosStr = NodePos ? FString::Printf(TEXT("  pos=%.0f,%.0f"), NodePos->X, NodePos->Y) : TEXT("");
	if (NodeGuid.IsEmpty())
	{
		Output = FString::Printf(TEXT("%s[%s] %s%s\n"),
			*Indent, *NodeClass, *Node->GetNodeName(), *PosStr);
	}
	else
	{
		Output = FString::Printf(TEXT("%s[%s] %s  guid=%s%s\n"),
			*Indent, *NodeClass, *Node->GetNodeName(), *NodeGuid, *PosStr);
	}
	Output += GetBTNodePropertySummary(Node, Indent);

	// List services on this composite
	for (UBTService* Service : Node->Services)
	{
		if (Service)
		{
			FString SvcClass = Service->GetClass()->GetName();
			SvcClass.RemoveFromStart(TEXT("BTService_"));
			FString SvcGuid = GetShortGuid(Service, GuidMap);
			if (SvcGuid.IsEmpty())
			{
				Output += FString::Printf(TEXT("%s  $%s %s\n"),
					*Indent, *SvcClass, *Service->GetNodeName());
			}
			else
			{
				Output += FString::Printf(TEXT("%s  $%s %s  guid=%s\n"),
					*Indent, *SvcClass, *Service->GetNodeName(), *SvcGuid);
			}
			Output += GetBTNodePropertySummary(Service, Indent + TEXT("  "));
		}
	}

	// Process children
	for (int32 i = 0; i < Node->GetChildrenNum(); i++)
	{
		FBTCompositeChild& Child = Node->Children[i];

		// List decorators on the child link
		for (UBTDecorator* Decorator : Child.Decorators)
		{
			if (Decorator)
			{
				FString DecClass = Decorator->GetClass()->GetName();
				DecClass.RemoveFromStart(TEXT("BTDecorator_"));
				FString DecGuid = GetShortGuid(Decorator, GuidMap);
				if (DecGuid.IsEmpty())
				{
					Output += FString::Printf(TEXT("%s  @%s %s\n"),
						*Indent, *DecClass, *Decorator->GetNodeName());
				}
				else
				{
					Output += FString::Printf(TEXT("%s  @%s %s  guid=%s\n"),
						*Indent, *DecClass, *Decorator->GetNodeName(), *DecGuid);
				}
				Output += GetBTNodePropertySummary(Decorator, Indent + TEXT("  "));
			}
		}

		// Show decorator logic expression if multiple decorators with logic ops
		if (Child.DecoratorOps.Num() > 0 && Child.Decorators.Num() > 1)
		{
			FString LogicExpr = ReconstructDecoratorLogic(Child.Decorators, Child.DecoratorOps);
			if (!LogicExpr.IsEmpty())
			{
				Output += FString::Printf(TEXT("%s  logic: %s\n"), *Indent, *LogicExpr);
			}
		}

		if (Child.ChildComposite)
		{
			// Recurse into composite child
			Output += GetBTNodeHierarchy(Child.ChildComposite, Depth + 1, GuidMap, PositionMap);
		}
		else if (Child.ChildTask)
		{
			// Output task node
			FString TaskClass = Child.ChildTask->GetClass()->GetName();
			TaskClass.RemoveFromStart(TEXT("BTTask_"));

			FString TaskGuid = GetShortGuid(Child.ChildTask, GuidMap);
			const FVector2D* TaskPos = PositionMap.Find(Child.ChildTask);
			FString TaskPosStr = TaskPos ? FString::Printf(TEXT("  pos=%.0f,%.0f"), TaskPos->X, TaskPos->Y) : TEXT("");
			if (TaskGuid.IsEmpty())
			{
				Output += FString::Printf(TEXT("%s  <%s> %s%s\n"),
					*Indent, *TaskClass, *Child.ChildTask->GetNodeName(), *TaskPosStr);
			}
			else
			{
				Output += FString::Printf(TEXT("%s  <%s> %s  guid=%s%s\n"),
					*Indent, *TaskClass, *Child.ChildTask->GetNodeName(), *TaskGuid, *TaskPosStr);
			}
			Output += GetBTNodePropertySummary(Child.ChildTask, Indent + TEXT("  "));

			// List services on the task
			for (UBTService* Service : Child.ChildTask->Services)
			{
				if (Service)
				{
					FString SvcClass = Service->GetClass()->GetName();
					SvcClass.RemoveFromStart(TEXT("BTService_"));
					FString SvcGuid = GetShortGuid(Service, GuidMap);
					if (SvcGuid.IsEmpty())
					{
						Output += FString::Printf(TEXT("%s    $%s %s\n"),
							*Indent, *SvcClass, *Service->GetNodeName());
					}
					else
					{
						Output += FString::Printf(TEXT("%s    $%s %s  guid=%s\n"),
							*Indent, *SvcClass, *Service->GetNodeName(), *SvcGuid);
					}
					Output += GetBTNodePropertySummary(Service, Indent + TEXT("    "));
				}
			}
		}
	}

	return Output;
}

FString FReadFileTool::GetBTNodePropertySummary(UBTNode* Node, const FString& Indent)
{
	if (!Node) return TEXT("");

	FString Output;

	// Skip properties defined on the base UBTNode class (internal engine fields)
	UClass* BaseNodeClass = UBTNode::StaticClass();

	for (TFieldIterator<FProperty> PropIt(Node->GetClass()); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Only show editable properties
		if (!Property->HasAnyPropertyFlags(CPF_Edit)) continue;
		if (Property->HasAnyPropertyFlags(CPF_Deprecated)) continue;

		// Skip properties from the base UBTNode class (NodeName, TreeAsset, etc.)
		if (Property->GetOwnerClass() == BaseNodeClass) continue;

		// Skip delegate/multicast delegate properties
		if (Property->IsA<FDelegateProperty>() || Property->IsA<FMulticastDelegateProperty>()) continue;

		FString PropName = Property->GetName();

		// FBlackboardKeySelector properties - show the selected key name
		if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
		{
			if (StructProp->Struct && StructProp->Struct->GetName() == TEXT("BlackboardKeySelector"))
			{
				const FBlackboardKeySelector* KeySelector =
					StructProp->ContainerPtrToValuePtr<FBlackboardKeySelector>(Node);
				if (KeySelector)
				{
					FString KeyName = KeySelector->SelectedKeyName.IsNone()
						? TEXT("(none)") : KeySelector->SelectedKeyName.ToString();
					Output += FString::Printf(TEXT("%s    %s=%s\n"),
						*Indent, *PropName, *KeyName);
				}
				continue;
			}
		}

		// Enum properties (modern FEnumProperty)
		if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
		{
			if (UEnum* Enum = EnumProp->GetEnum())
			{
				FNumericProperty* UnderlyingProp = EnumProp->GetUnderlyingProperty();
				int64 EnumValue = UnderlyingProp->GetSignedIntPropertyValue(
					EnumProp->ContainerPtrToValuePtr<void>(Node));
				FString ValueName = Enum->GetNameStringByValue(EnumValue);
				Output += FString::Printf(TEXT("%s    %s=%s\n"),
					*Indent, *PropName, *ValueName);
			}
			continue;
		}

		// TEnumAsByte properties (common in BT nodes like FlowAbortMode)
		if (FByteProperty* ByteProp = CastField<FByteProperty>(Property))
		{
			if (UEnum* Enum = ByteProp->GetIntPropertyEnum())
			{
				uint8 ByteValue = ByteProp->GetPropertyValue_InContainer(Node);
				FString ValueName = Enum->GetNameStringByValue(ByteValue);
				Output += FString::Printf(TEXT("%s    %s=%s\n"),
					*Indent, *PropName, *ValueName);
				continue;
			}
		}

		// Array properties — show element count and contents
		if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Property))
		{
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
			if (!ValuePtr) continue;

			FScriptArrayHelper ArrayHelper(ArrayProp, ValuePtr);
			int32 Count = ArrayHelper.Num();
			if (Count == 0) continue; // Skip empty arrays

			FString ValueStr;
			Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);

			if (ValueStr.Len() > 200)
			{
				ValueStr = ValueStr.Left(197) + TEXT("...");
			}

			Output += FString::Printf(TEXT("%s    %s[%d]=%s\n"),
				*Indent, *PropName, Count, *ValueStr);
			continue;
		}

		// For all other property types, use generic export
		const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
		if (!ValuePtr) continue;

		FString ValueStr;
		Property->ExportTextItem_Direct(ValueStr, ValuePtr, nullptr, nullptr, PPF_None);

		// Truncate very long values
		if (ValueStr.Len() > 120)
		{
			ValueStr = ValueStr.Left(117) + TEXT("...");
		}

		// Skip empty/default-looking values for cleaner output
		if (ValueStr.IsEmpty() || ValueStr == TEXT("None") || ValueStr == TEXT("0") || ValueStr == TEXT("0.000000"))
		{
			continue;
		}

		Output += FString::Printf(TEXT("%s    %s=%s\n"),
			*Indent, *PropName, *ValueStr);
	}

	return Output;
}

FString FReadFileTool::GetBlackboardSummary(UBlackboardData* Blackboard)
{
	int32 KeyCount = Blackboard->Keys.Num();

	// Check for parent blackboard
	FString ParentName = TEXT("None");
	if (Blackboard->Parent)
	{
		ParentName = Blackboard->Parent->GetName();
	}

	FString Output = FString::Printf(TEXT("# BLACKBOARD %s parent=%s keys=%d\n"),
		*Blackboard->GetName(), *ParentName, KeyCount);

	return Output;
}

// Helper to extract details from a blackboard key entry
static FString GetBlackboardKeyDetails(const FBlackboardEntry& Entry)
{
	FString KeyName = Entry.EntryName.ToString();
	FString KeyType = TEXT("Unknown");
	FString Details;

	if (!Entry.KeyType)
	{
		return FString::Printf(TEXT("%s\tUnknown\n"), *KeyName);
	}

	// Get type name (remove UBlackboardKeyType_ prefix)
	KeyType = Entry.KeyType->GetClass()->GetName();
	KeyType.RemoveFromStart(TEXT("BlackboardKeyType_"));

	// Build flags
	TArray<FString> Flags;
	if (Entry.bInstanceSynced)
	{
		Flags.Add(TEXT("[Synced]"));
	}

	FString Category = Entry.EntryCategory.ToString();
	if (!Category.IsEmpty())
	{
		Flags.Add(FString::Printf(TEXT("category=%s"), *Category));
	}

	// Extract type-specific details (default values, base classes, enum types)
	if (UBlackboardKeyType_Object* ObjKey = Cast<UBlackboardKeyType_Object>(Entry.KeyType))
	{
		if (ObjKey->BaseClass)
		{
			Flags.Add(FString::Printf(TEXT("base_class=%s"), *ObjKey->BaseClass->GetName()));
		}
	}
	else if (UBlackboardKeyType_Class* ClassKey = Cast<UBlackboardKeyType_Class>(Entry.KeyType))
	{
		if (ClassKey->BaseClass)
		{
			Flags.Add(FString::Printf(TEXT("base_class=%s"), *ClassKey->BaseClass->GetName()));
		}
	}
	else if (UBlackboardKeyType_Enum* EnumKey = Cast<UBlackboardKeyType_Enum>(Entry.KeyType))
	{
		if (EnumKey->EnumType)
		{
			Flags.Add(FString::Printf(TEXT("enum=%s"), *EnumKey->EnumType->GetName()));
		}
		else if (!EnumKey->EnumName.IsEmpty())
		{
			Flags.Add(FString::Printf(TEXT("enum=%s"), *EnumKey->EnumName));
		}
		Flags.Add(FString::Printf(TEXT("default=%d"), EnumKey->DefaultValue));
	}
	else if (UBlackboardKeyType_Bool* BoolKey = Cast<UBlackboardKeyType_Bool>(Entry.KeyType))
	{
		Flags.Add(FString::Printf(TEXT("default=%s"), BoolKey->bDefaultValue ? TEXT("true") : TEXT("false")));
	}
	else if (UBlackboardKeyType_Int* IntKey = Cast<UBlackboardKeyType_Int>(Entry.KeyType))
	{
		Flags.Add(FString::Printf(TEXT("default=%d"), IntKey->DefaultValue));
	}
	else if (UBlackboardKeyType_Float* FloatKey = Cast<UBlackboardKeyType_Float>(Entry.KeyType))
	{
		Flags.Add(FString::Printf(TEXT("default=%.2f"), FloatKey->DefaultValue));
	}
	else if (UBlackboardKeyType_String* StringKey = Cast<UBlackboardKeyType_String>(Entry.KeyType))
	{
		if (!StringKey->DefaultValue.IsEmpty())
		{
			Flags.Add(FString::Printf(TEXT("default=%s"), *StringKey->DefaultValue));
		}
	}
	else if (UBlackboardKeyType_Name* NameKey = Cast<UBlackboardKeyType_Name>(Entry.KeyType))
	{
		if (!NameKey->DefaultValue.IsNone())
		{
			Flags.Add(FString::Printf(TEXT("default=%s"), *NameKey->DefaultValue.ToString()));
		}
	}

	FString FlagsStr = FString::Join(Flags, TEXT("\t"));
	if (FlagsStr.IsEmpty())
	{
		return FString::Printf(TEXT("%s\t%s\n"), *KeyName, *KeyType);
	}
	return FString::Printf(TEXT("%s\t%s\t%s\n"), *KeyName, *KeyType, *FlagsStr);
}

FString FReadFileTool::GetBlackboardKeys(UBlackboardData* Blackboard)
{
	if (Blackboard->Keys.Num() == 0)
	{
		return TEXT("# KEYS 0\n");
	}

	FString Output = FString::Printf(TEXT("# KEYS %d\n"), Blackboard->Keys.Num());

	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		Output += GetBlackboardKeyDetails(Entry);
	}

	// Also include parent keys if any
	if (Blackboard->Parent)
	{
		Output += FString::Printf(TEXT("\n# PARENT_KEYS (%s) %d\n"),
			*Blackboard->Parent->GetName(), Blackboard->Parent->Keys.Num());

		for (const FBlackboardEntry& Entry : Blackboard->Parent->Keys)
		{
			FString Line = GetBlackboardKeyDetails(Entry);
			// Trim trailing newline, add inherited marker, re-add newline
			Line.TrimEndInline();
			Output += Line + TEXT("\t(inherited)\n");
		}
	}

	return Output;
}
