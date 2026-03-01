// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ReadFileTool.h"
#include "WidgetBlueprint.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Components/Widget.h"
#include "Components/PanelWidget.h"
#include "Components/PanelSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Widgets/Layout/Anchors.h"
#include "WidgetBlueprintEditor.h"
#include "UObject/UnrealType.h"

FString FReadFileTool::GetWidgetBlueprintSummary(UWidgetBlueprint* WidgetBlueprint)
{
	FString ParentName = WidgetBlueprint->ParentClass ? WidgetBlueprint->ParentClass->GetName() : TEXT("UserWidget");

	int32 WidgetCount = 0;
	if (WidgetBlueprint->WidgetTree)
	{
		TArray<UWidget*> AllWidgets;
		WidgetBlueprint->WidgetTree->GetAllWidgets(AllWidgets);
		WidgetCount = AllWidgets.Num();
	}

	int32 VarCount = WidgetBlueprint->NewVariables.Num();
	int32 GraphCount = WidgetBlueprint->UbergraphPages.Num() + WidgetBlueprint->FunctionGraphs.Num();
	int32 AnimCount = WidgetBlueprint->Animations.Num();

	FString Output = FString::Printf(TEXT("# WIDGET_BLUEPRINT %s parent=%s\nwidgets=%d variables=%d graphs=%d animations=%d\n"),
		*WidgetBlueprint->GetName(), *ParentName, WidgetCount, VarCount, GraphCount, AnimCount);

	// Add graph list
	if (GraphCount > 0)
	{
		Output += FString::Printf(TEXT("\n# GRAPHS %d\n"), GraphCount);

		for (UEdGraph* Graph : WidgetBlueprint->UbergraphPages)
		{
			Output += FString::Printf(TEXT("%s\tubergraph\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
		}
		for (UEdGraph* Graph : WidgetBlueprint->FunctionGraphs)
		{
			Output += FString::Printf(TEXT("%s\tfunction\t%d\n"), *Graph->GetName(), Graph->Nodes.Num());
		}
	}

	return Output;
}

FString FReadFileTool::GetWidgetTree(UWidgetBlueprint* WidgetBlueprint, bool bShowSchema)
{
	if (!WidgetBlueprint->WidgetTree)
	{
		return TEXT("# WIDGET_TREE 0\n(no widget tree)\n");
	}

	TArray<UWidget*> AllWidgets;
	WidgetBlueprint->WidgetTree->GetAllWidgets(AllWidgets);

	FString Output = FString::Printf(TEXT("# WIDGET_TREE %d\n"), AllWidgets.Num());

	if (bShowSchema)
	{
		Output += TEXT("# Schema mode: showing all editable properties with types and format hints\n");
		Output += TEXT("# Use these property names in configure_widgets\n\n");
	}

	// Get root widget
	UWidget* RootWidget = WidgetBlueprint->WidgetTree->RootWidget;
	if (RootWidget)
	{
		Output += GetWidgetHierarchy(RootWidget, 0, bShowSchema);
	}
	else
	{
		Output += TEXT("(no root widget)\n");
	}

	return Output;
}

FString FReadFileTool::GetWidgetHierarchy(UWidget* Widget, int32 Depth, bool bShowSchema)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Indent;
	for (int32 i = 0; i < Depth; i++)
	{
		Indent += TEXT("  ");
	}

	// Get widget info
	FString WidgetName = Widget->GetName();
	FString WidgetClass = Widget->GetClass()->GetName();
	FString WidgetVisibility = Widget->IsVisible() ? TEXT("visible") : TEXT("hidden");

	// Check if widget is marked as a variable (accessible in Blueprint)
	FString IsVariable = Widget->bIsVariable ? TEXT("[var]") : TEXT("");

	// Get slot type info
	FString SlotType = TEXT("");
	if (Widget->Slot)
	{
		SlotType = FString::Printf(TEXT(" slot=%s"), *Widget->Slot->GetClass()->GetName());
	}

	FString Output = FString::Printf(TEXT("%s%s (%s) %s %s%s\n"),
		*Indent, *WidgetName, *WidgetClass, *WidgetVisibility, *IsVariable, *SlotType);

	if (bShowSchema)
	{
		// Show editable properties with types and current values
		Output += GetWidgetPropertySchema(Widget, Indent + TEXT("  "));
		if (Widget->Slot)
		{
			Output += GetSlotPropertySchema(Widget->Slot, Indent + TEXT("  "));
		}
	}
	else
	{
		// Get slot info dynamically based on slot type
		FString SlotInfo = GetWidgetSlotInfo(Widget);
		if (!SlotInfo.IsEmpty())
		{
			Output += FString::Printf(TEXT("%s  %s\n"), *Indent, *SlotInfo);
		}

		// Get widget-specific properties using reflection
		FString PropInfo = GetWidgetProperties(Widget, Indent + TEXT("  "));
		if (!PropInfo.IsEmpty())
		{
			Output += PropInfo;
		}
	}

	// If this is a panel widget, recursively add children
	if (UPanelWidget* PanelWidget = Cast<UPanelWidget>(Widget))
	{
		int32 ChildCount = PanelWidget->GetChildrenCount();
		for (int32 i = 0; i < ChildCount; i++)
		{
			UWidget* ChildWidget = PanelWidget->GetChildAt(i);
			if (ChildWidget)
			{
				Output += GetWidgetHierarchy(ChildWidget, Depth + 1, bShowSchema);
			}
		}
	}

	return Output;
}

FString FReadFileTool::GetWidgetPropertySchema(UWidget* Widget, const FString& Indent)
{
	if (!Widget)
	{
		return TEXT("");
	}

	FString Output = FString::Printf(TEXT("%s# PROPERTIES (configure_widgets.properties)\n"), *Indent);

	UClass* WidgetClass = Widget->GetClass();
	UClass* BaseClass = UWidget::StaticClass();

	// Iterate through class hierarchy
	for (UClass* Class = WidgetClass; Class && Class != BaseClass; Class = Class->GetSuperClass())
	{
		for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;

			// Only show editable properties
			if (!Property->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}

			// Skip delegates
			if (Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FDelegateProperty>())
			{
				continue;
			}

			FString PropName = Property->GetName();
			FString PropType = GetPropertyTypeString(Property);

			// Get current value
			FString CurrentValue;
			const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Widget);
			Property->ExportTextItem_Direct(CurrentValue, PropertyValue, nullptr, nullptr, PPF_None);

			// Truncate long values
			if (CurrentValue.Len() > 60)
			{
				CurrentValue = CurrentValue.Left(57) + TEXT("...");
			}

			Output += FString::Printf(TEXT("%s  %s: %s = %s\n"), *Indent, *PropName, *PropType, *CurrentValue);
		}
	}

	return Output;
}

FString FReadFileTool::GetSlotPropertySchema(UPanelSlot* Slot, const FString& Indent)
{
	if (!Slot)
	{
		return TEXT("");
	}

	FString Output = FString::Printf(TEXT("%s# SLOT PROPERTIES (configure_widgets.slot) - %s\n"),
		*Indent, *Slot->GetClass()->GetName());

	UClass* SlotClass = Slot->GetClass();

	for (TFieldIterator<FProperty> PropIt(SlotClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;

		// Only show editable properties
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		FString PropName = Property->GetName();
		FString PropType = GetPropertyTypeString(Property);

		// Get current value
		FString CurrentValue;
		const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Slot);
		Property->ExportTextItem_Direct(CurrentValue, PropertyValue, nullptr, nullptr, PPF_None);

		Output += FString::Printf(TEXT("%s  %s: %s = %s\n"), *Indent, *PropName, *PropType, *CurrentValue);
	}

	return Output;
}

FString FReadFileTool::GetPropertyTypeString(FProperty* Property)
{
	if (!Property)
	{
		return TEXT("Unknown");
	}

	if (Property->IsA<FStrProperty>())
	{
		return TEXT("String");
	}
	else if (Property->IsA<FTextProperty>())
	{
		return TEXT("Text (use plain string)");
	}
	else if (Property->IsA<FNameProperty>())
	{
		return TEXT("Name");
	}
	else if (Property->IsA<FBoolProperty>())
	{
		return TEXT("Bool (true/false)");
	}
	else if (Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>())
	{
		return TEXT("Float");
	}
	else if (Property->IsA<FIntProperty>())
	{
		return TEXT("Int");
	}
	else if (Property->IsA<FByteProperty>())
	{
		FByteProperty* ByteProp = CastField<FByteProperty>(Property);
		if (ByteProp->Enum)
		{
			return FString::Printf(TEXT("Enum:%s"), *ByteProp->Enum->GetName());
		}
		return TEXT("Byte");
	}
	else if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Property))
	{
		return FString::Printf(TEXT("Enum:%s"), *EnumProp->GetEnum()->GetName());
	}
	else if (FStructProperty* StructProp = CastField<FStructProperty>(Property))
	{
		FString StructName = StructProp->Struct->GetName();
		// Provide format hints for common structs
		if (StructName == TEXT("LinearColor") || StructName == TEXT("Color"))
		{
			return TEXT("Color \"(R=1.0,G=0.0,B=0.0,A=1.0)\"");
		}
		else if (StructName == TEXT("Vector2D"))
		{
			return TEXT("Vector2D \"(X=0.0,Y=0.0)\"");
		}
		else if (StructName == TEXT("Vector"))
		{
			return TEXT("Vector \"(X=0.0,Y=0.0,Z=0.0)\"");
		}
		else if (StructName == TEXT("Margin"))
		{
			return TEXT("Margin \"(Left=0,Top=0,Right=0,Bottom=0)\"");
		}
		else if (StructName == TEXT("Anchors"))
		{
			return TEXT("Anchors \"(Minimum=(X=0,Y=0),Maximum=(X=1,Y=1))\"");
		}
		else if (StructName == TEXT("AnchorData"))
		{
			return TEXT("AnchorData \"(Offsets=(Left=0,Top=0,Right=100,Bottom=50),Anchors=(Minimum=(X=0,Y=0),Maximum=(X=0,Y=0)),Alignment=(X=0,Y=0))\"");
		}
		else if (StructName == TEXT("SlateBrush"))
		{
			return TEXT("SlateBrush (complex struct)");
		}
		else if (StructName == TEXT("SlateColor"))
		{
			return TEXT("SlateColor \"(SpecifiedColor=(R=1.0,G=1.0,B=1.0,A=1.0))\"");
		}
		return FString::Printf(TEXT("Struct:%s"), *StructName);
	}
	else if (Property->IsA<FObjectProperty>())
	{
		FObjectProperty* ObjProp = CastField<FObjectProperty>(Property);
		return FString::Printf(TEXT("Object:%s"), *ObjProp->PropertyClass->GetName());
	}

	return Property->GetCPPType();
}

FString FReadFileTool::GetWidgetSlotInfo(UWidget* Widget)
{
	if (!Widget || !Widget->Slot)
	{
		return TEXT("");
	}

	UPanelSlot* Slot = Widget->Slot;
	FString SlotInfo;

	// Use reflection to get slot properties dynamically
	UClass* SlotClass = Slot->GetClass();

	// Common slot properties we want to show
	static const TSet<FString> ImportantSlotProps = {
		TEXT("LayoutData"),       // Canvas
		TEXT("Offsets"),          // Canvas
		TEXT("Anchors"),          // Canvas
		TEXT("Alignment"),        // Canvas
		TEXT("bAutoSize"),        // Canvas
		TEXT("ZOrder"),           // Canvas
		TEXT("Padding"),          // Box slots
		TEXT("Size"),             // Box slots
		TEXT("HorizontalAlignment"), // Multiple
		TEXT("VerticalAlignment"),   // Multiple
	};

	TArray<FString> SlotProps;
	for (TFieldIterator<FProperty> PropIt(SlotClass); PropIt; ++PropIt)
	{
		FProperty* Property = *PropIt;
		FString PropName = Property->GetName();

		// Skip internal/unimportant properties
		if (PropName.StartsWith(TEXT("b")) && PropName.Len() > 1 && !PropName.Equals(TEXT("bAutoSize")))
		{
			continue; // Skip most bool flags
		}

		// Only show properties that are editor-visible
		if (!Property->HasAnyPropertyFlags(CPF_Edit))
		{
			continue;
		}

		// Export property value
		FString ValueStr;
		const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Slot);
		Property->ExportTextItem_Direct(ValueStr, PropertyValue, nullptr, nullptr, PPF_None);

		// Skip empty or default values
		if (ValueStr.IsEmpty() || ValueStr.Equals(TEXT("None")) || ValueStr.Equals(TEXT("0")) || ValueStr.Equals(TEXT("0.000000")))
		{
			continue;
		}

		// Truncate long values
		if (ValueStr.Len() > 60)
		{
			ValueStr = ValueStr.Left(57) + TEXT("...");
		}

		SlotProps.Add(FString::Printf(TEXT("%s=%s"), *PropName, *ValueStr));
	}

	if (SlotProps.Num() > 0)
	{
		SlotInfo = TEXT(" [") + FString::Join(SlotProps, TEXT(", ")) + TEXT("]");
	}

	return SlotInfo;
}

FString FReadFileTool::GetWidgetProperties(UWidget* Widget, const FString& Indent)
{
	if (!Widget)
	{
		return TEXT("");
	}

	UClass* WidgetClass = Widget->GetClass();
	UClass* BaseWidgetClass = UWidget::StaticClass();

	// Properties we want to show for UI design (text, colors, brushes, etc.)
	// These are the properties that matter for visual design
	static const TSet<FString> ImportantProps = {
		// Text properties
		TEXT("Text"),
		TEXT("ColorAndOpacity"),
		TEXT("Font"),
		TEXT("Justification"),
		TEXT("AutoWrapText"),
		// Image/Brush properties
		TEXT("Brush"),
		TEXT("BrushColor"),
		TEXT("BackgroundColor"),
		TEXT("ContentColor"),
		// Layout
		TEXT("Padding"),
		TEXT("MinDesiredWidth"),
		TEXT("MinDesiredHeight"),
		TEXT("MaxDesiredWidth"),
		TEXT("MaxDesiredHeight"),
		TEXT("WidthOverride"),
		TEXT("HeightOverride"),
		// State
		TEXT("IsChecked"),
		TEXT("Percent"),
		TEXT("Value"),
		// Style
		TEXT("Style"),
		TEXT("WidgetStyle"),
		TEXT("ToolTipText"),
		TEXT("Cursor"),
	};

	TArray<FString> Props;

	// Iterate through class hierarchy from widget class down to UWidget
	for (UClass* Class = WidgetClass; Class && Class != BaseWidgetClass; Class = Class->GetSuperClass())
	{
		// Only iterate properties defined in this class (not inherited)
		for (TFieldIterator<FProperty> PropIt(Class, EFieldIteratorFlags::ExcludeSuper); PropIt; ++PropIt)
		{
			FProperty* Property = *PropIt;
			FString PropName = Property->GetName();

			// Check if it's an important property OR if it's BlueprintReadWrite/EditAnywhere
			bool bIsImportant = ImportantProps.Contains(PropName);
			bool bIsEditable = Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible);

			// Skip properties that aren't relevant for design
			if (!bIsImportant && !bIsEditable)
			{
				continue;
			}

			// Skip delegate/event properties
			if (Property->IsA<FMulticastDelegateProperty>() || Property->IsA<FDelegateProperty>())
			{
				continue;
			}

			// Skip object references (too noisy)
			if (Property->IsA<FObjectProperty>() && !PropName.Contains(TEXT("Brush")) && !PropName.Contains(TEXT("Style")))
			{
				continue;
			}

			// Export property value
			FString ValueStr;
			const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Widget);
			Property->ExportTextItem_Direct(ValueStr, PropertyValue, nullptr, nullptr, PPF_None);

			// Skip empty or default values
			if (ValueStr.IsEmpty() || ValueStr.Equals(TEXT("None")))
			{
				continue;
			}

			// Skip false booleans
			if (ValueStr.Equals(TEXT("False"), ESearchCase::IgnoreCase))
			{
				continue;
			}

			// Truncate long values (like brush structs)
			if (ValueStr.Len() > 80)
			{
				ValueStr = ValueStr.Left(77) + TEXT("...");
			}

			Props.Add(FString::Printf(TEXT("%s: %s"), *PropName, *ValueStr));
		}
	}

	// Format output
	FString Output;
	for (const FString& Prop : Props)
	{
		Output += FString::Printf(TEXT("%s| %s\n"), *Indent, *Prop);
	}

	return Output;
}
