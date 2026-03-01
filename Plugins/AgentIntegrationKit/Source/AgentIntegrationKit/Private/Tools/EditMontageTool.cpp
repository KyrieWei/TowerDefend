// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/EditMontageTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimCompositeBase.h"
#include "Animation/AnimTypes.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Animation/AnimSequenceBase.h"
#include "UObject/UObjectIterator.h"
#include "ScopedTransaction.h"

// ========== Schema ==========

TSharedPtr<FJsonObject> FEditMontageTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> NameProp = MakeShared<FJsonObject>();
	NameProp->SetStringField(TEXT("type"), TEXT("string"));
	NameProp->SetStringField(TEXT("description"), TEXT("AnimMontage asset name or path"));
	Properties->SetObjectField(TEXT("name"), NameProp);

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Asset folder path"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// --- Slot Track Operations ---

	TSharedPtr<FJsonObject> AddSlotTrackProp = MakeShared<FJsonObject>();
	AddSlotTrackProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSlotTrackProp->SetStringField(TEXT("description"),
		TEXT("Add slot animation tracks: [{slot_name (e.g. 'DefaultSlot'), "
		     "animation (optional: anim asset name or path for initial segment), "
		     "start_pos (optional: position in montage timeline, default 0), "
		     "play_rate (optional: default 1.0), loop_count (optional: default 1)}]"));
	Properties->SetObjectField(TEXT("add_slot_track"), AddSlotTrackProp);

	TSharedPtr<FJsonObject> RemoveSlotTrackProp = MakeShared<FJsonObject>();
	RemoveSlotTrackProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSlotTrackProp->SetStringField(TEXT("description"),
		TEXT("Slot track names to remove (array of strings, e.g. ['DefaultSlot'])"));
	Properties->SetObjectField(TEXT("remove_slot_track"), RemoveSlotTrackProp);

	TSharedPtr<FJsonObject> AddSegmentProp = MakeShared<FJsonObject>();
	AddSegmentProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSegmentProp->SetStringField(TEXT("description"),
		TEXT("Add animation segments to existing slot tracks: [{slot_name, animation (asset name or path), "
		     "start_pos (optional: position in montage timeline, default appends after last segment), "
		     "anim_start_time (optional: time within anim to start, default 0), "
		     "anim_end_time (optional: time within anim to end, default full length), "
		     "play_rate (optional: default 1.0), loop_count (optional: default 1)}]"));
	Properties->SetObjectField(TEXT("add_anim_segment"), AddSegmentProp);

	TSharedPtr<FJsonObject> RemoveSegmentProp = MakeShared<FJsonObject>();
	RemoveSegmentProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSegmentProp->SetStringField(TEXT("description"),
		TEXT("Remove animation segments: [{slot_name, segment_index}]"));
	Properties->SetObjectField(TEXT("remove_anim_segment"), RemoveSegmentProp);

	// --- Section Operations ---

	TSharedPtr<FJsonObject> AddSectionProp = MakeShared<FJsonObject>();
	AddSectionProp->SetStringField(TEXT("type"), TEXT("array"));
	AddSectionProp->SetStringField(TEXT("description"),
		TEXT("Sections to add: [{name (section name), time (start time in seconds)}]"));
	Properties->SetObjectField(TEXT("add_section"), AddSectionProp);

	TSharedPtr<FJsonObject> RemoveSectionProp = MakeShared<FJsonObject>();
	RemoveSectionProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveSectionProp->SetStringField(TEXT("description"),
		TEXT("Section names to remove (array of strings)"));
	Properties->SetObjectField(TEXT("remove_section"), RemoveSectionProp);

	TSharedPtr<FJsonObject> LinkSectionsProp = MakeShared<FJsonObject>();
	LinkSectionsProp->SetStringField(TEXT("type"), TEXT("array"));
	LinkSectionsProp->SetStringField(TEXT("description"),
		TEXT("Link sections: [{section (name), next_section (name, or empty to unlink)}]. "
		     "Controls montage flow: when 'section' finishes, 'next_section' plays next."));
	Properties->SetObjectField(TEXT("link_sections"), LinkSectionsProp);

	TSharedPtr<FJsonObject> AddNotifyProp = MakeShared<FJsonObject>();
	AddNotifyProp->SetStringField(TEXT("type"), TEXT("array"));
	AddNotifyProp->SetStringField(TEXT("description"),
		TEXT("Notifies to add: [{name (notify name), time (trigger time in seconds), "
		     "type (optional: notify class like 'AnimNotify_PlaySound'), "
		     "duration (optional: for state notifies, in seconds), "
		     "branching_point (optional: true for montage branching points)}]"));
	Properties->SetObjectField(TEXT("add_notify"), AddNotifyProp);

	TSharedPtr<FJsonObject> RemoveNotifyProp = MakeShared<FJsonObject>();
	RemoveNotifyProp->SetStringField(TEXT("type"), TEXT("array"));
	RemoveNotifyProp->SetStringField(TEXT("description"),
		TEXT("Notifies to remove by name or index: ['NotifyName'] or [0, 1, 2]"));
	Properties->SetObjectField(TEXT("remove_notify"), RemoveNotifyProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("name")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ========== Helpers ==========

static UClass* FindAnimNotifyClass(const FString& TypeName)
{
	if (TypeName.IsEmpty()) return nullptr;

	// Try exact match first
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (Class->IsChildOf(UAnimNotify::StaticClass()) || Class->IsChildOf(UAnimNotifyState::StaticClass()))
		{
			if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated)) continue;

			FString ClassName = Class->GetName();
			if (ClassName.Equals(TypeName, ESearchCase::IgnoreCase) ||
				ClassName.Equals(TEXT("AnimNotify_") + TypeName, ESearchCase::IgnoreCase) ||
				ClassName.Equals(TEXT("AnimNotifyState_") + TypeName, ESearchCase::IgnoreCase))
			{
				return Class;
			}
		}
	}

	return nullptr;
}

// ========== Execute ==========

FToolResult FEditMontageTool::Execute(const TSharedPtr<FJsonObject>& Args)
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

	FString FullAssetPath = NeoStackToolUtils::BuildAssetPath(Name, Path);
	UAnimMontage* Montage = LoadObject<UAnimMontage>(nullptr, *FullAssetPath);
	if (!Montage)
	{
		return FToolResult::Fail(FString::Printf(TEXT("AnimMontage not found: %s"), *FullAssetPath));
	}

	const FScopedTransaction Transaction(FText::Format(
		NSLOCTEXT("AgentIntegrationKit", "EditMontage", "AI Edit Montage: {0}"),
		FText::FromString(Name)));

	Montage->Modify();

	TArray<FString> Results;
	int32 AddedCount = 0;
	int32 RemovedCount = 0;
	int32 ModifiedCount = 0;

	// ========== Add Slot Tracks ==========
	const TArray<TSharedPtr<FJsonValue>>* AddSlotTracks;
	if (Args->TryGetArrayField(TEXT("add_slot_track"), AddSlotTracks))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddSlotTracks)
		{
			const TSharedPtr<FJsonObject>* SlotObj;
			if (!Value->TryGetObject(SlotObj)) continue;

			FString SlotName;
			(*SlotObj)->TryGetStringField(TEXT("slot_name"), SlotName);

			if (SlotName.IsEmpty())
			{
				Results.Add(TEXT("! add_slot_track: missing 'slot_name'"));
				continue;
			}

			// Check if slot already exists
			if (Montage->IsValidSlot(FName(*SlotName)))
			{
				Results.Add(FString::Printf(TEXT("! add_slot_track: slot '%s' already exists"), *SlotName));
				continue;
			}

			FSlotAnimationTrack& NewSlot = Montage->AddSlot(FName(*SlotName));

			// Optionally add initial animation segment
			FString AnimPath;
			if ((*SlotObj)->TryGetStringField(TEXT("animation"), AnimPath) && !AnimPath.IsEmpty())
			{
				UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
				if (!AnimAsset)
				{
					FString BuiltPath = NeoStackToolUtils::BuildAssetPath(AnimPath, TEXT(""));
					AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *BuiltPath);
				}

				if (AnimAsset)
				{
					FAnimSegment NewSegment;
					NewSegment.SetAnimReference(AnimAsset, true);
					NewSegment.AnimStartTime = 0.0f;
					NewSegment.AnimEndTime = AnimAsset->GetPlayLength();
					NewSegment.AnimPlayRate = 1.0f;
					NewSegment.LoopingCount = 1;

					double StartPos = 0.0, PlayRate = 1.0, LoopCount = 1.0;
					if ((*SlotObj)->TryGetNumberField(TEXT("start_pos"), StartPos))
						NewSegment.StartPos = static_cast<float>(FMath::Max(StartPos, 0.0));
					else
						NewSegment.StartPos = 0.0f;

					if ((*SlotObj)->TryGetNumberField(TEXT("play_rate"), PlayRate))
						NewSegment.AnimPlayRate = static_cast<float>(PlayRate);

					if ((*SlotObj)->TryGetNumberField(TEXT("loop_count"), LoopCount))
						NewSegment.LoopingCount = FMath::Max(1, static_cast<int32>(LoopCount));

					NewSlot.AnimTrack.AnimSegments.Add(NewSegment);
					Results.Add(FString::Printf(TEXT("+ SlotTrack: %s (anim: %s, %.2fs)"),
						*SlotName, *AnimAsset->GetName(), AnimAsset->GetPlayLength()));
				}
				else
				{
					Results.Add(FString::Printf(TEXT("+ SlotTrack: %s (WARNING: animation not found: %s)"),
						*SlotName, *AnimPath));
				}
			}
			else
			{
				Results.Add(FString::Printf(TEXT("+ SlotTrack: %s (empty)"), *SlotName));
			}

			AddedCount++;
		}
	}

	// ========== Remove Slot Tracks ==========
	const TArray<TSharedPtr<FJsonValue>>* RemoveSlotTracks;
	if (Args->TryGetArrayField(TEXT("remove_slot_track"), RemoveSlotTracks))
	{
		TArray<int32> IndicesToRemove;

		for (const TSharedPtr<FJsonValue>& Value : *RemoveSlotTracks)
		{
			FString SlotName;
			if (!Value->TryGetString(SlotName) || SlotName.IsEmpty()) continue;

			bool bFound = false;
			for (int32 i = 0; i < Montage->SlotAnimTracks.Num(); i++)
			{
				if (Montage->SlotAnimTracks[i].SlotName == FName(*SlotName))
				{
					IndicesToRemove.AddUnique(i);
					bFound = true;
					break;
				}
			}
			if (!bFound)
			{
				Results.Add(FString::Printf(TEXT("! remove_slot_track: '%s' not found"), *SlotName));
			}
		}

		// Remove in reverse order to avoid index invalidation
		IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 Idx : IndicesToRemove)
		{
			FString RemovedName = Montage->SlotAnimTracks[Idx].SlotName.ToString();
			Montage->SlotAnimTracks.RemoveAt(Idx);
			Results.Add(FString::Printf(TEXT("- SlotTrack: %s"), *RemovedName));
			RemovedCount++;
		}
	}

	// ========== Add Anim Segments ==========
	const TArray<TSharedPtr<FJsonValue>>* AddSegments;
	if (Args->TryGetArrayField(TEXT("add_anim_segment"), AddSegments))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddSegments)
		{
			const TSharedPtr<FJsonObject>* SegObj;
			if (!Value->TryGetObject(SegObj)) continue;

			FString SlotName, AnimPath;
			(*SegObj)->TryGetStringField(TEXT("slot_name"), SlotName);
			(*SegObj)->TryGetStringField(TEXT("animation"), AnimPath);

			if (SlotName.IsEmpty())
			{
				Results.Add(TEXT("! add_anim_segment: missing 'slot_name'"));
				continue;
			}
			if (AnimPath.IsEmpty())
			{
				Results.Add(TEXT("! add_anim_segment: missing 'animation'"));
				continue;
			}

			// Find the target slot track
			FSlotAnimationTrack* TargetSlot = nullptr;
			for (FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
			{
				if (Slot.SlotName == FName(*SlotName))
				{
					TargetSlot = &Slot;
					break;
				}
			}

			if (!TargetSlot)
			{
				Results.Add(FString::Printf(TEXT("! add_anim_segment: slot '%s' not found"), *SlotName));
				continue;
			}

			// Load animation asset
			UAnimSequenceBase* AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *AnimPath);
			if (!AnimAsset)
			{
				FString BuiltPath = NeoStackToolUtils::BuildAssetPath(AnimPath, TEXT(""));
				AnimAsset = LoadObject<UAnimSequenceBase>(nullptr, *BuiltPath);
			}

			if (!AnimAsset)
			{
				Results.Add(FString::Printf(TEXT("! add_anim_segment: animation not found: %s"), *AnimPath));
				continue;
			}

			FAnimSegment NewSegment;
			NewSegment.SetAnimReference(AnimAsset, true);

			// Start position: default appends after last existing segment
			double StartPos = -1.0;
			if (!(*SegObj)->TryGetNumberField(TEXT("start_pos"), StartPos) || StartPos < 0.0)
			{
				float LastEnd = 0.0f;
				for (const FAnimSegment& Existing : TargetSlot->AnimTrack.AnimSegments)
				{
					float SegEnd = Existing.StartPos + Existing.GetLength();
					if (SegEnd > LastEnd) LastEnd = SegEnd;
				}
				NewSegment.StartPos = LastEnd;
			}
			else
			{
				NewSegment.StartPos = static_cast<float>(StartPos);
			}

			// Animation time range
			double AnimStartTime = 0.0, AnimEndTime = 0.0;
			(*SegObj)->TryGetNumberField(TEXT("anim_start_time"), AnimStartTime);
			if (!(*SegObj)->TryGetNumberField(TEXT("anim_end_time"), AnimEndTime) || AnimEndTime <= 0.0)
			{
				AnimEndTime = static_cast<double>(AnimAsset->GetPlayLength());
			}
			NewSegment.AnimStartTime = static_cast<float>(FMath::Max(AnimStartTime, 0.0));
			NewSegment.AnimEndTime = static_cast<float>(FMath::Min(AnimEndTime, static_cast<double>(AnimAsset->GetPlayLength())));

			// Play rate and loop count
			double PlayRate = 1.0, LoopCount = 1.0;
			if ((*SegObj)->TryGetNumberField(TEXT("play_rate"), PlayRate))
				NewSegment.AnimPlayRate = static_cast<float>(PlayRate);
			else
				NewSegment.AnimPlayRate = 1.0f;

			if ((*SegObj)->TryGetNumberField(TEXT("loop_count"), LoopCount))
				NewSegment.LoopingCount = FMath::Max(1, static_cast<int32>(LoopCount));
			else
				NewSegment.LoopingCount = 1;

			TargetSlot->AnimTrack.AnimSegments.Add(NewSegment);
			TargetSlot->AnimTrack.SortAnimSegments();

			Results.Add(FString::Printf(TEXT("+ Segment in '%s': %s at %.2fs (%.2fs - %.2fs, rate=%.2f, loops=%d)"),
				*SlotName, *AnimAsset->GetName(), NewSegment.StartPos,
				NewSegment.AnimStartTime, NewSegment.AnimEndTime,
				NewSegment.AnimPlayRate, NewSegment.LoopingCount));
			AddedCount++;
		}
	}

	// ========== Remove Anim Segments ==========
	const TArray<TSharedPtr<FJsonValue>>* RemoveSegments;
	if (Args->TryGetArrayField(TEXT("remove_anim_segment"), RemoveSegments))
	{
		// Group removals by slot name for safe reverse-order deletion
		TMap<FString, TArray<int32>> SlotIndicesToRemove;

		for (const TSharedPtr<FJsonValue>& Value : *RemoveSegments)
		{
			const TSharedPtr<FJsonObject>* SegObj;
			if (!Value->TryGetObject(SegObj)) continue;

			FString SlotName;
			double SegmentIndex = -1.0;
			(*SegObj)->TryGetStringField(TEXT("slot_name"), SlotName);
			(*SegObj)->TryGetNumberField(TEXT("segment_index"), SegmentIndex);

			if (SlotName.IsEmpty())
			{
				Results.Add(TEXT("! remove_anim_segment: missing 'slot_name'"));
				continue;
			}

			// Find slot and validate index
			FSlotAnimationTrack* TargetSlot = nullptr;
			for (FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
			{
				if (Slot.SlotName == FName(*SlotName))
				{
					TargetSlot = &Slot;
					break;
				}
			}

			if (!TargetSlot)
			{
				Results.Add(FString::Printf(TEXT("! remove_anim_segment: slot '%s' not found"), *SlotName));
				continue;
			}

			int32 Idx = static_cast<int32>(SegmentIndex);
			if (Idx < 0 || Idx >= TargetSlot->AnimTrack.AnimSegments.Num())
			{
				Results.Add(FString::Printf(TEXT("! remove_anim_segment: index %d out of range for slot '%s' (0-%d)"),
					Idx, *SlotName, FMath::Max(0, TargetSlot->AnimTrack.AnimSegments.Num() - 1)));
				continue;
			}

			SlotIndicesToRemove.FindOrAdd(SlotName).AddUnique(Idx);
		}

		// Remove in reverse index order per slot
		for (auto& Pair : SlotIndicesToRemove)
		{
			FSlotAnimationTrack* TargetSlot = nullptr;
			for (FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
			{
				if (Slot.SlotName == FName(*Pair.Key))
				{
					TargetSlot = &Slot;
					break;
				}
			}
			if (!TargetSlot) continue;

			Pair.Value.Sort([](int32 A, int32 B) { return A > B; });
			for (int32 Idx : Pair.Value)
			{
				const FAnimSegment& Seg = TargetSlot->AnimTrack.AnimSegments[Idx];
				FString AnimName = Seg.GetAnimReference() ? Seg.GetAnimReference()->GetName() : TEXT("None");
				TargetSlot->AnimTrack.AnimSegments.RemoveAt(Idx);
				Results.Add(FString::Printf(TEXT("- Segment %d from '%s' (%s)"), Idx, *Pair.Key, *AnimName));
				RemovedCount++;
			}
		}
	}

	// ========== Add Sections ==========
	const TArray<TSharedPtr<FJsonValue>>* AddSections;
	if (Args->TryGetArrayField(TEXT("add_section"), AddSections))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddSections)
		{
			const TSharedPtr<FJsonObject>* SectionObj;
			if (Value->TryGetObject(SectionObj))
			{
				FString SectionName;
				double Time = 0.0;
				(*SectionObj)->TryGetStringField(TEXT("name"), SectionName);
				(*SectionObj)->TryGetNumberField(TEXT("time"), Time);

				if (SectionName.IsEmpty())
				{
					Results.Add(TEXT("! add_section: missing 'name'"));
					continue;
				}

				// Validate time is within montage duration
				float Duration = Montage->GetPlayLength();
				if (Time < 0.0 || Time > Duration)
				{
					Results.Add(FString::Printf(TEXT("! add_section: time %.2f is outside montage duration (0 - %.2f)"),
						Time, Duration));
					continue;
				}

				int32 SectionIdx = Montage->AddAnimCompositeSection(FName(*SectionName), static_cast<float>(Time));
				if (SectionIdx == INDEX_NONE)
				{
					Results.Add(FString::Printf(TEXT("! add_section: '%s' already exists"), *SectionName));
				}
				else
				{
					Results.Add(FString::Printf(TEXT("+ Section: %s at %.2fs"), *SectionName, Time));
					AddedCount++;
				}
			}
		}
	}

	// ========== Remove Sections ==========
	const TArray<TSharedPtr<FJsonValue>>* RemoveSections;
	if (Args->TryGetArrayField(TEXT("remove_section"), RemoveSections))
	{
		for (const TSharedPtr<FJsonValue>& Value : *RemoveSections)
		{
			FString SectionName;
			if (Value->TryGetString(SectionName))
			{
				int32 SectionIdx = Montage->GetSectionIndex(FName(*SectionName));
				if (SectionIdx == INDEX_NONE)
				{
					Results.Add(FString::Printf(TEXT("! remove_section: '%s' not found"), *SectionName));
				}
				else
				{
					// Don't allow removing the default first section
					if (SectionIdx == 0)
					{
						Results.Add(FString::Printf(TEXT("! remove_section: cannot remove first section '%s'"), *SectionName));
					}
					else
					{
						Montage->DeleteAnimCompositeSection(SectionIdx);
						Results.Add(FString::Printf(TEXT("- Section: %s"), *SectionName));
						RemovedCount++;
					}
				}
			}
		}
	}

	// ========== Link Sections ==========
	const TArray<TSharedPtr<FJsonValue>>* LinkSections;
	if (Args->TryGetArrayField(TEXT("link_sections"), LinkSections))
	{
		for (const TSharedPtr<FJsonValue>& Value : *LinkSections)
		{
			const TSharedPtr<FJsonObject>* LinkObj;
			if (Value->TryGetObject(LinkObj))
			{
				FString SectionName, NextSectionName;
				(*LinkObj)->TryGetStringField(TEXT("section"), SectionName);
				(*LinkObj)->TryGetStringField(TEXT("next_section"), NextSectionName);

				if (SectionName.IsEmpty())
				{
					Results.Add(TEXT("! link_sections: missing 'section' name"));
					continue;
				}

				int32 SectionIdx = Montage->GetSectionIndex(FName(*SectionName));
				if (SectionIdx == INDEX_NONE)
				{
					Results.Add(FString::Printf(TEXT("! link_sections: section '%s' not found"), *SectionName));
					continue;
				}

				// Validate target section exists (if not unlinking)
				if (!NextSectionName.IsEmpty())
				{
					int32 NextIdx = Montage->GetSectionIndex(FName(*NextSectionName));
					if (NextIdx == INDEX_NONE)
					{
						Results.Add(FString::Printf(TEXT("! link_sections: next_section '%s' not found"), *NextSectionName));
						continue;
					}
				}

				FCompositeSection& Section = Montage->GetAnimCompositeSection(SectionIdx);
				FName NewNextName = NextSectionName.IsEmpty() ? NAME_None : FName(*NextSectionName);
				Section.NextSectionName = NewNextName;

				if (NextSectionName.IsEmpty())
				{
					Results.Add(FString::Printf(TEXT("= Link: %s -> (end)"), *SectionName));
				}
				else
				{
					Results.Add(FString::Printf(TEXT("= Link: %s -> %s"), *SectionName, *NextSectionName));
				}
				ModifiedCount++;
			}
		}
	}

	// ========== Add Notifies ==========
	const TArray<TSharedPtr<FJsonValue>>* AddNotifies;
	if (Args->TryGetArrayField(TEXT("add_notify"), AddNotifies))
	{
		for (const TSharedPtr<FJsonValue>& Value : *AddNotifies)
		{
			const TSharedPtr<FJsonObject>* NotifyObj;
			if (Value->TryGetObject(NotifyObj))
			{
				FString NotifyName, TypeName;
				double Time = 0.0;
				double Duration = 0.0;
				bool bBranchingPoint = false;

				(*NotifyObj)->TryGetStringField(TEXT("name"), NotifyName);
				(*NotifyObj)->TryGetNumberField(TEXT("time"), Time);
				(*NotifyObj)->TryGetStringField(TEXT("type"), TypeName);
				(*NotifyObj)->TryGetNumberField(TEXT("duration"), Duration);
				(*NotifyObj)->TryGetBoolField(TEXT("branching_point"), bBranchingPoint);

				if (NotifyName.IsEmpty())
				{
					Results.Add(TEXT("! add_notify: missing 'name'"));
					continue;
				}

				float PlayLength = Montage->GetPlayLength();
				if (Time < 0.0 || Time > PlayLength)
				{
					Results.Add(FString::Printf(TEXT("! add_notify: time %.2f is outside montage duration (0 - %.2f)"),
						Time, PlayLength));
					continue;
				}

				FAnimNotifyEvent NewEvent;
				NewEvent.NotifyName = FName(*NotifyName);
				NewEvent.Link(Montage, static_cast<float>(Time));
				NewEvent.TriggerTimeOffset = GetTriggerTimeOffsetForType(Montage->CalculateOffsetForNotify(static_cast<float>(Time)));
				NewEvent.TrackIndex = 0;

				if (bBranchingPoint)
				{
					NewEvent.MontageTickType = EMontageNotifyTickType::BranchingPoint;
				}

				// Try to create a typed notify if class specified
				if (!TypeName.IsEmpty())
				{
					UClass* NotifyClass = FindAnimNotifyClass(TypeName);
					if (NotifyClass)
					{
						if (NotifyClass->IsChildOf(UAnimNotifyState::StaticClass()))
						{
							UAnimNotifyState* NotifyState = NewObject<UAnimNotifyState>(Montage, NotifyClass);
							NewEvent.NotifyStateClass = NotifyState;
							NewEvent.Duration = static_cast<float>(Duration > 0.0 ? Duration : 0.5);
							NewEvent.EndLink.Link(Montage, static_cast<float>(Time + NewEvent.Duration));
						}
						else
						{
							UAnimNotify* Notify = NewObject<UAnimNotify>(Montage, NotifyClass);
							NewEvent.Notify = Notify;
						}
					}
					else
					{
						Results.Add(FString::Printf(TEXT("! add_notify: notify class '%s' not found, adding as named notify"), *TypeName));
					}
				}

				Montage->Notifies.Add(NewEvent);

				FString Extra;
				if (Duration > 0.0) Extra += FString::Printf(TEXT(" duration=%.2fs"), Duration);
				if (bBranchingPoint) Extra += TEXT(" [BranchingPoint]");
				Results.Add(FString::Printf(TEXT("+ Notify: %s at %.2fs%s"), *NotifyName, Time, *Extra));
				AddedCount++;
			}
		}
	}

	// ========== Remove Notifies ==========
	const TArray<TSharedPtr<FJsonValue>>* RemoveNotifies;
	if (Args->TryGetArrayField(TEXT("remove_notify"), RemoveNotifies))
	{
		// Collect indices to remove (in reverse order to avoid invalidation)
		TArray<int32> IndicesToRemove;

		for (const TSharedPtr<FJsonValue>& Value : *RemoveNotifies)
		{
			FString NotifyName;
			double NotifyIndex;

			if (Value->TryGetString(NotifyName))
			{
				// Find by name
				bool bFound = false;
				for (int32 i = 0; i < Montage->Notifies.Num(); i++)
				{
					FString EvtName;
					const FAnimNotifyEvent& Evt = Montage->Notifies[i];
					if (Evt.Notify) EvtName = Evt.Notify->GetNotifyName();
					else if (Evt.NotifyStateClass) EvtName = Evt.NotifyStateClass->GetNotifyName();
					else EvtName = Evt.NotifyName.ToString();

					if (EvtName.Equals(NotifyName, ESearchCase::IgnoreCase))
					{
						IndicesToRemove.AddUnique(i);
						bFound = true;
						break;
					}
				}
				if (!bFound)
				{
					Results.Add(FString::Printf(TEXT("! remove_notify: '%s' not found"), *NotifyName));
				}
			}
			else if (Value->TryGetNumber(NotifyIndex))
			{
				int32 Idx = static_cast<int32>(NotifyIndex);
				if (Idx >= 0 && Idx < Montage->Notifies.Num())
				{
					IndicesToRemove.AddUnique(Idx);
				}
				else
				{
					Results.Add(FString::Printf(TEXT("! remove_notify: index %d out of range (0-%d)"),
						Idx, Montage->Notifies.Num() - 1));
				}
			}
		}

		// Remove in reverse order
		IndicesToRemove.Sort([](int32 A, int32 B) { return A > B; });
		for (int32 Idx : IndicesToRemove)
		{
			FString RemovedName = Montage->Notifies[Idx].NotifyName.ToString();
			Montage->Notifies.RemoveAt(Idx);
			Results.Add(FString::Printf(TEXT("- Notify: %s (index %d)"), *RemovedName, Idx));
			RemovedCount++;
		}
	}

	// Refresh caches after modifications
	Montage->RefreshCacheData();
	Montage->MarkPackageDirty();

	// Build output
	FString Output = FString::Printf(TEXT("# EDIT AnimMontage %s\n"), *Montage->GetName());
	for (const FString& R : Results)
	{
		Output += R + TEXT("\n");
	}
	Output += FString::Printf(TEXT("= %d added, %d removed, %d modified\n"), AddedCount, RemovedCount, ModifiedCount);

	return FToolResult::Ok(Output);
}
