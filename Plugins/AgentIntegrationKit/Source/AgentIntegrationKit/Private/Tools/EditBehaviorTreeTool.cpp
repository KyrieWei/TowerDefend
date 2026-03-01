// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/EditBehaviorTreeTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Behavior Tree
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BTCompositeNode.h"

// Blackboard
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Name.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Rotator.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Class.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Enum.h"

// Asset utilities
#include "AssetRegistry/AssetRegistryModule.h"

// Transaction support for undo/redo
#include "ScopedTransaction.h"

TSharedPtr<FJsonObject> FEditBehaviorTreeTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("BehaviorTree or Blackboard asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> ReorderChildrenProp = MakeShared<FJsonObject>();
	ReorderChildrenProp->SetStringField(TEXT("type"), TEXT("array"));
	ReorderChildrenProp->SetStringField(TEXT("description"), TEXT("Reorder children of a composite: [{parent (composite name), order ([child names in desired order])}]"));
	Properties->SetObjectField(TEXT("reorder_children"), ReorderChildrenProp);

	TSharedPtr<FJsonObject> AddKeyProp = MakeShared<FJsonObject>();
	AddKeyProp->SetStringField(TEXT("type"), TEXT("array"));
	AddKeyProp->SetStringField(TEXT("description"), TEXT("Blackboard keys to add: [{name, type (Bool/Int/Float/String/Vector/Object/etc), base_class, category, description, instance_synced}]"));
	Properties->SetObjectField(TEXT("add_key"), AddKeyProp);

	TSharedPtr<FJsonObject> RemoveKeyProp = MakeShared<FJsonObject>();
	RemoveKeyProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveKeyProp->SetStringField(TEXT("description"), TEXT("Blackboard key names to remove"));
	TSharedPtr<FJsonObject> RemoveKeyItems = MakeShared<FJsonObject>();
	RemoveKeyItems->SetStringField(TEXT("type"), TEXT("string"));
	RemoveKeyProp->SetObjectField(TEXT("items"), RemoveKeyItems);
	Properties->SetObjectField(TEXT("remove_key"), RemoveKeyProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FEditBehaviorTreeTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Name, Path;

	if (!Args->TryGetStringField(TEXT("name"), Name) || Name.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: name"));
	}

	Args->TryGetStringField(TEXT("path"), Path);

	// Build asset path
	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);

	// Try to load as BehaviorTree first, then as Blackboard
	UBehaviorTree* BehaviorTree = LoadObject<UBehaviorTree>(nullptr, *FullAssetPath);
	UBlackboardData* Blackboard = nullptr;

	if (!BehaviorTree)
	{
		Blackboard = LoadObject<UBlackboardData>(nullptr, *FullAssetPath);
		if (!Blackboard)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Asset not found (expected BehaviorTree or Blackboard): %s"), *FullAssetPath));
		}
	}

	// Create transaction for undo/redo support
	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditBehaviorTree", "AI Edit BehaviorTree: {0}"),
		FText::FromString(Name)));

	TArray<FString> Results;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;

	// ========== Behavior Tree Operations ==========
	if (BehaviorTree)
	{
		BehaviorTree->Modify();

		// Process reorder_children
		const TArray<TSharedPtr<FJsonValue>>* ReorderArray;
		if (Args->TryGetArrayField(TEXT("reorder_children"), ReorderArray))
		{
			for (const TSharedPtr<FJsonValue>& Value : *ReorderArray)
			{
				const TSharedPtr<FJsonObject>* ReorderObj;
				if (Value->TryGetObject(ReorderObj))
				{
					FString ParentName;
					(*ReorderObj)->TryGetStringField(TEXT("parent"), ParentName);

					TArray<FString> Order;
					const TArray<TSharedPtr<FJsonValue>>* OrderArray;
					if ((*ReorderObj)->TryGetArrayField(TEXT("order"), OrderArray))
					{
						for (const TSharedPtr<FJsonValue>& OrderVal : *OrderArray)
						{
							FString ChildName;
							if (OrderVal->TryGetString(ChildName))
							{
								Order.Add(ChildName);
							}
						}
					}

					FString Result = ReorderChildren(BehaviorTree, ParentName, Order);
					Results.Add(Result);
				}
			}
		}

		BehaviorTree->MarkPackageDirty();
	}

	// ========== Blackboard Operations (works for both standalone and BT's blackboard) ==========
	UBlackboardData* TargetBlackboard = Blackboard ? Blackboard : (BehaviorTree ? BehaviorTree->BlackboardAsset.Get() : nullptr);

	if (TargetBlackboard)
	{
		TargetBlackboard->SetFlags(RF_Transactional);
		TargetBlackboard->Modify();

		// Process add_key
		const TArray<TSharedPtr<FJsonValue>>* AddKeys;
		if (Args->TryGetArrayField(TEXT("add_key"), AddKeys))
		{
			for (const TSharedPtr<FJsonValue>& Value : *AddKeys)
			{
				const TSharedPtr<FJsonObject>* KeyObj;
				if (Value->TryGetObject(KeyObj))
				{
					FBlackboardKeyDefinition KeyDef;
					(*KeyObj)->TryGetStringField(TEXT("name"), KeyDef.Name);
					(*KeyObj)->TryGetStringField(TEXT("type"), KeyDef.Type);
					(*KeyObj)->TryGetStringField(TEXT("base_class"), KeyDef.BaseClass);
					(*KeyObj)->TryGetStringField(TEXT("category"), KeyDef.Category);
					(*KeyObj)->TryGetStringField(TEXT("description"), KeyDef.Description);
					(*KeyObj)->TryGetBoolField(TEXT("instance_synced"), KeyDef.bInstanceSynced);

					FString Result = AddBlackboardKey(TargetBlackboard, KeyDef);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("+"))) AddedCount++;
				}
			}
		}

		// Process remove_key
		const TArray<TSharedPtr<FJsonValue>>* RemoveKeys;
		if (Args->TryGetArrayField(TEXT("remove_key"), RemoveKeys))
		{
			for (const TSharedPtr<FJsonValue>& Value : *RemoveKeys)
			{
				FString KeyName;
				if (Value->TryGetString(KeyName))
				{
					FString Result = RemoveBlackboardKey(TargetBlackboard, KeyName);
					Results.Add(Result);
					if (Result.StartsWith(TEXT("-"))) RemovedCount++;
				}
			}
		}

		TargetBlackboard->MarkPackageDirty();
	}

	// Build output
	FString AssetType = BehaviorTree ? TEXT("BehaviorTree") : TEXT("Blackboard");
	FString Output = FString::Printf(TEXT("# EDIT %s %s\n"), *AssetType, *Name);
	for (const FString& R : Results)
	{
		Output += R + TEXT("\n");
	}
	Output += FString::Printf(TEXT("= %d added, %d removed\n"), AddedCount, RemovedCount);

	return FToolResult::Ok(Output);
}

// ========== Find Helpers ==========

UBTCompositeNode* FEditBehaviorTreeTool::FindCompositeByName(UBTCompositeNode* Root, const FString& Name)
{
	if (!Root)
	{
		return nullptr;
	}

	if (Root->GetNodeName().Equals(Name, ESearchCase::IgnoreCase))
	{
		return Root;
	}

	for (int32 i = 0; i < Root->GetChildrenNum(); i++)
	{
		FBTCompositeChild& Child = Root->Children[i];
		if (Child.ChildComposite)
		{
			UBTCompositeNode* Found = FindCompositeByName(Child.ChildComposite, Name);
			if (Found)
			{
				return Found;
			}
		}
	}

	return nullptr;
}

// ========== Behavior Tree Operations ==========

FString FEditBehaviorTreeTool::ReorderChildren(UBehaviorTree* BehaviorTree, const FString& ParentName, const TArray<FString>& Order)
{
	if (ParentName.IsEmpty())
	{
		return TEXT("! Reorder: Missing parent name");
	}
	if (Order.Num() == 0)
	{
		return TEXT("! Reorder: Empty order array");
	}
	if (!BehaviorTree->RootNode)
	{
		return TEXT("! Reorder: Tree is empty");
	}

	UBTCompositeNode* ParentNode = FindCompositeByName(BehaviorTree->RootNode, ParentName);
	if (!ParentNode)
	{
		return FString::Printf(TEXT("! Reorder: Parent '%s' not found"), *ParentName);
	}

	if (ParentNode->Children.Num() == 0)
	{
		return FString::Printf(TEXT("! Reorder: Parent '%s' has no children"), *ParentName);
	}

	ParentNode->Modify();

	TArray<FBTCompositeChild> OldChildren = ParentNode->Children;
	TArray<FBTCompositeChild> NewChildren;
	TArray<bool> Used;
	Used.SetNumZeroed(OldChildren.Num());

	// Place children in the requested order
	for (const FString& ChildName : Order)
	{
		for (int32 i = 0; i < OldChildren.Num(); i++)
		{
			if (Used[i])
			{
				continue;
			}

			UBTNode* ChildNode = OldChildren[i].ChildComposite
				? static_cast<UBTNode*>(OldChildren[i].ChildComposite)
				: static_cast<UBTNode*>(OldChildren[i].ChildTask);

			if (ChildNode && ChildNode->GetNodeName().Equals(ChildName, ESearchCase::IgnoreCase))
			{
				NewChildren.Add(OldChildren[i]);
				Used[i] = true;
				break;
			}
		}
	}

	// Append any children not in the order list (preserve them at end)
	for (int32 i = 0; i < OldChildren.Num(); i++)
	{
		if (!Used[i])
		{
			NewChildren.Add(OldChildren[i]);
		}
	}

	ParentNode->Children = NewChildren;

	return FString::Printf(TEXT("= Reorder: %s children reordered"), *ParentName);
}

// ========== Blackboard Key Operations ==========

UClass* FEditBehaviorTreeTool::FindBlackboardKeyTypeClass(const FString& TypeName)
{
	if (TypeName.Equals(TEXT("Bool"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Boolean"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Bool::StaticClass();
	}
	if (TypeName.Equals(TEXT("Int"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Integer"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Int::StaticClass();
	}
	if (TypeName.Equals(TEXT("Float"), ESearchCase::IgnoreCase) ||
		TypeName.Equals(TEXT("Double"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Float::StaticClass();
	}
	if (TypeName.Equals(TEXT("String"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_String::StaticClass();
	}
	if (TypeName.Equals(TEXT("Name"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Name::StaticClass();
	}
	if (TypeName.Equals(TEXT("Vector"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Vector::StaticClass();
	}
	if (TypeName.Equals(TEXT("Rotator"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Rotator::StaticClass();
	}
	if (TypeName.Equals(TEXT("Object"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Object::StaticClass();
	}
	if (TypeName.Equals(TEXT("Class"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Class::StaticClass();
	}
	if (TypeName.Equals(TEXT("Enum"), ESearchCase::IgnoreCase))
	{
		return UBlackboardKeyType_Enum::StaticClass();
	}

	return nullptr;
}

FString FEditBehaviorTreeTool::AddBlackboardKey(UBlackboardData* Blackboard, const FBlackboardKeyDefinition& KeyDef)
{
	if (KeyDef.Name.IsEmpty())
	{
		return TEXT("! Key: Missing name");
	}
	if (KeyDef.Type.IsEmpty())
	{
		return TEXT("! Key: Missing type");
	}

	// Check if key already exists
	for (const FBlackboardEntry& Entry : Blackboard->Keys)
	{
		if (Entry.EntryName.ToString().Equals(KeyDef.Name, ESearchCase::IgnoreCase))
		{
			return FString::Printf(TEXT("! Key: '%s' already exists"), *KeyDef.Name);
		}
	}

	// Find key type class
	UClass* KeyTypeClass = FindBlackboardKeyTypeClass(KeyDef.Type);
	if (!KeyTypeClass)
	{
		return FString::Printf(TEXT("! Key: Unknown type '%s'"), *KeyDef.Type);
	}

	// Create the key type instance
	UBlackboardKeyType* KeyType = NewObject<UBlackboardKeyType>(Blackboard, KeyTypeClass);
	if (!KeyType)
	{
		return FString::Printf(TEXT("! Key: Failed to create type '%s'"), *KeyDef.Type);
	}

	// Create entry
	FBlackboardEntry NewEntry;
	NewEntry.EntryName = FName(*KeyDef.Name);
	NewEntry.KeyType = KeyType;
	NewEntry.bInstanceSynced = KeyDef.bInstanceSynced;

	if (!KeyDef.Category.IsEmpty())
	{
		NewEntry.EntryCategory = FName(*KeyDef.Category);
	}

	if (!KeyDef.Description.IsEmpty())
	{
		NewEntry.EntryDescription = KeyDef.Description;
	}

	Blackboard->Keys.Add(NewEntry);

	FString Flags = KeyDef.bInstanceSynced ? TEXT(" [Synced]") : TEXT("");
	return FString::Printf(TEXT("+ Key: %s (%s)%s"), *KeyDef.Name, *KeyDef.Type, *Flags);
}

FString FEditBehaviorTreeTool::RemoveBlackboardKey(UBlackboardData* Blackboard, const FString& KeyName)
{
	for (int32 i = Blackboard->Keys.Num() - 1; i >= 0; i--)
	{
		if (Blackboard->Keys[i].EntryName.ToString().Equals(KeyName, ESearchCase::IgnoreCase))
		{
			Blackboard->Keys.RemoveAt(i);
			return FString::Printf(TEXT("- Key: %s"), *KeyName);
		}
	}

	return FString::Printf(TEXT("! Key: '%s' not found"), *KeyName);
}

