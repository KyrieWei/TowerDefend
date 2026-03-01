// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/ReadLogsTool.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

// Engine includes
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Logging/MessageLog.h"
#include "Logging/TokenizedMessage.h"
#include "MessageLogModule.h"
#include "IMessageLogListing.h"
#include "Modules/ModuleManager.h"

// Blueprint compilation
#include "Engine/Blueprint.h"
#include "Animation/AnimBlueprint.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/CompilerResultsLog.h"
#include "AssetRegistry/AssetRegistryModule.h"

// Live instance detection for compile safety
#include "Editor.h"
#include "EngineUtils.h"

// Live Coding (C++ hot reload)
#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

TSharedPtr<FJsonObject> FReadLogsTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// Operation type
	TSharedPtr<FJsonObject> OperationProp = MakeShared<FJsonObject>();
	OperationProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> OperationEnum;
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("output_log")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("message_log")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("compile_blueprint")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("list_logs")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("livecoding_status")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("livecoding_compile")));
	OperationProp->SetArrayField(TEXT("enum"), OperationEnum);
	OperationProp->SetStringField(TEXT("description"),
		TEXT("Operation: 'output_log' (main editor log - includes PIE/gameplay logs, use search:['LogPlayLevel'] or tail for recent PIE output), "
			 "'message_log' (specific logs: log_name='PIE' for Play-In-Editor, 'BlueprintLog' for compilation), "
			 "'compile_blueprint' (compile BP and get errors), 'list_logs' (show available logs), "
			 "'livecoding_status' (C++ Live Coding status), 'livecoding_compile' (trigger C++ compile)"));
	Properties->SetObjectField(TEXT("operation"), OperationProp);

	// Offset - for output_log pagination
	TSharedPtr<FJsonObject> OffsetProp = MakeShared<FJsonObject>();
	OffsetProp->SetStringField(TEXT("type"), TEXT("integer"));
	OffsetProp->SetStringField(TEXT("description"),
		TEXT("Start reading from this line number (0-based). Use with limit for pagination. "
			 "Example: offset=100, limit=50 reads lines 100-149."));
	Properties->SetObjectField(TEXT("offset"), OffsetProp);

	// Limit - max lines to return
	TSharedPtr<FJsonObject> LimitProp = MakeShared<FJsonObject>();
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"),
		TEXT("Maximum lines to return (default: 100). Use with offset for pagination."));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	// Tail - read last N lines
	TSharedPtr<FJsonObject> TailProp = MakeShared<FJsonObject>();
	TailProp->SetStringField(TEXT("type"), TEXT("integer"));
	TailProp->SetStringField(TEXT("description"),
		TEXT("Read the last N lines of the log (like 'tail -n'). Overrides offset if specified. "
			 "Useful for seeing recent activity."));
	Properties->SetObjectField(TEXT("tail"), TailProp);

	// Search - keyword filter
	TSharedPtr<FJsonObject> SearchProp = MakeShared<FJsonObject>();
	SearchProp->SetStringField(TEXT("type"), TEXT("array"));
	TSharedPtr<FJsonObject> SearchItems = MakeShared<FJsonObject>();
	SearchItems->SetStringField(TEXT("type"), TEXT("string"));
	SearchProp->SetObjectField(TEXT("items"), SearchItems);
	SearchProp->SetStringField(TEXT("description"),
		TEXT("Search terms to filter lines (case-insensitive). ALL terms must match. "
			 "Example: ['Error', 'Blueprint'] finds lines containing both words."));
	Properties->SetObjectField(TEXT("search"), SearchProp);

	// Severity filter
	TSharedPtr<FJsonObject> SeverityProp = MakeShared<FJsonObject>();
	SeverityProp->SetStringField(TEXT("type"), TEXT("string"));
	TArray<TSharedPtr<FJsonValue>> SeverityEnum;
	SeverityEnum.Add(MakeShared<FJsonValueString>(TEXT("all")));
	SeverityEnum.Add(MakeShared<FJsonValueString>(TEXT("error")));
	SeverityEnum.Add(MakeShared<FJsonValueString>(TEXT("warning")));
	SeverityEnum.Add(MakeShared<FJsonValueString>(TEXT("display")));
	SeverityEnum.Add(MakeShared<FJsonValueString>(TEXT("log")));
	SeverityProp->SetArrayField(TEXT("enum"), SeverityEnum);
	SeverityProp->SetStringField(TEXT("description"),
		TEXT("Filter by severity level. For output_log: Error, Warning, Display, Log. "
			 "For message_log: Error, Warning, Info. Default: 'all'."));
	Properties->SetObjectField(TEXT("severity"), SeverityProp);

	// Log name - for message_log operation
	TSharedPtr<FJsonObject> LogNameProp = MakeShared<FJsonObject>();
	LogNameProp->SetStringField(TEXT("type"), TEXT("string"));
	LogNameProp->SetStringField(TEXT("description"),
		TEXT("For message_log operation: name of the log to read. "
			 "Common logs: 'BlueprintLog' (BP compilation), 'MapCheck' (level errors), "
			 "'PIE' (Play-In-Editor), 'LoadErrors' (asset loading). Use list_logs to see all."));
	Properties->SetObjectField(TEXT("log_name"), LogNameProp);

	// Asset path - for compile_blueprint operation
	TSharedPtr<FJsonObject> AssetProp = MakeShared<FJsonObject>();
	AssetProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetProp->SetStringField(TEXT("description"),
		TEXT("For compile_blueprint: asset path (e.g., '/Game/Blueprints/MyBP' or just 'MyBP' with path parameter)"));
	Properties->SetObjectField(TEXT("asset"), AssetProp);

	// Path - asset folder for compile_blueprint
	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"),
		TEXT("Asset folder path for compile_blueprint (default: /Game)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	// Force - bypass live instance safety check for compile_blueprint
	TSharedPtr<FJsonObject> ForceProp = MakeShared<FJsonObject>();
	ForceProp->SetStringField(TEXT("type"), TEXT("boolean"));
	ForceProp->SetStringField(TEXT("description"),
		TEXT("For compile_blueprint: force compilation even when live instances exist in the world (risk of crash after Live Coding). Default: false."));
	Properties->SetObjectField(TEXT("force"), ForceProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("operation")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FReadLogsTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Operation;
	if (!Args->TryGetStringField(TEXT("operation"), Operation))
	{
		return FToolResult::Fail(TEXT("Missing required parameter: operation"));
	}

	Operation = Operation.ToLower();

	if (Operation == TEXT("output_log"))
	{
		// Parse pagination parameters
		int32 Offset = 0;
		int32 Limit = 100;
		int32 Tail = 0;

		if (Args->HasField(TEXT("offset")))
		{
			Offset = FMath::Max(0, static_cast<int32>(Args->GetNumberField(TEXT("offset"))));
		}
		if (Args->HasField(TEXT("limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 2000);
		}
		if (Args->HasField(TEXT("tail")))
		{
			Tail = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("tail"))), 1, 5000);
		}

		// Parse search terms
		TArray<FString> SearchTerms;
		const TArray<TSharedPtr<FJsonValue>>* SearchArray;
		if (Args->TryGetArrayField(TEXT("search"), SearchArray))
		{
			for (const auto& Value : *SearchArray)
			{
				FString Term;
				if (Value->TryGetString(Term) && !Term.IsEmpty())
				{
					SearchTerms.Add(Term.ToLower());
				}
			}
		}

		// Parse severity
		FString Severity;
		Args->TryGetStringField(TEXT("severity"), Severity);

		return ReadOutputLog(Offset, Limit, Tail, SearchTerms, Severity);
	}
	else if (Operation == TEXT("message_log"))
	{
		FString LogName;
		if (!Args->TryGetStringField(TEXT("log_name"), LogName))
		{
			return FToolResult::Fail(TEXT("Missing required parameter for message_log: log_name. Use list_logs to see available logs."));
		}

		FString Severity;
		Args->TryGetStringField(TEXT("severity"), Severity);

		int32 Limit = 100;
		if (Args->HasField(TEXT("limit")))
		{
			Limit = FMath::Clamp(static_cast<int32>(Args->GetNumberField(TEXT("limit"))), 1, 500);
		}

		return ReadMessageLog(LogName, Severity, Limit);
	}
	else if (Operation == TEXT("compile_blueprint"))
	{
		FString AssetName;
		if (!Args->TryGetStringField(TEXT("asset"), AssetName))
		{
			return FToolResult::Fail(TEXT("Missing required parameter for compile_blueprint: asset"));
		}

		FString Path;
		Args->TryGetStringField(TEXT("path"), Path);

		// Build full asset path
		FString FullAssetPath;
		if (AssetName.StartsWith(TEXT("/Game/")) || AssetName.StartsWith(TEXT("/Engine/")))
		{
			FullAssetPath = AssetName;
			if (!FullAssetPath.Contains(TEXT(".")))
			{
				FString BaseName = FPaths::GetBaseFilename(FullAssetPath);
				FullAssetPath = FullAssetPath + TEXT(".") + BaseName;
			}
		}
		else
		{
			if (Path.IsEmpty())
			{
				Path = TEXT("/Game");
			}
			if (!Path.StartsWith(TEXT("/Game")) && !Path.StartsWith(TEXT("/Engine")))
			{
				Path = FString::Printf(TEXT("/Game/%s"), *Path);
			}
			FullAssetPath = Path / AssetName + TEXT(".") + AssetName;
		}

		bool bForce = false;
		if (Args->HasField(TEXT("force")))
		{
			bForce = Args->GetBoolField(TEXT("force"));
		}

		return CompileBlueprint(FullAssetPath, bForce);
	}
	else if (Operation == TEXT("list_logs"))
	{
		return ListMessageLogs();
	}
	else if (Operation == TEXT("livecoding_status"))
	{
		return GetLiveCodingStatus();
	}
	else if (Operation == TEXT("livecoding_compile"))
	{
		return TriggerLiveCodingCompile();
	}
	else
	{
		return FToolResult::Fail(FString::Printf(TEXT("Unknown operation: %s. Valid: output_log, message_log, compile_blueprint, list_logs, livecoding_status, livecoding_compile"), *Operation));
	}
}

FString FReadLogsTool::GetOutputLogPath()
{
	// Get the current log file path
	FString LogDir = FPaths::ProjectLogDir();

	// Find the most recent .log file
	TArray<FString> LogFiles;
	IFileManager::Get().FindFiles(LogFiles, *LogDir, TEXT("*.log"));

	if (LogFiles.Num() == 0)
	{
		return FString();
	}

	// Sort by modification time to get the most recent
	FString MostRecentLog;
	FDateTime MostRecentTime = FDateTime::MinValue();

	for (const FString& LogFile : LogFiles)
	{
		FString FullPath = LogDir / LogFile;
		FDateTime ModTime = IFileManager::Get().GetTimeStamp(*FullPath);
		if (ModTime > MostRecentTime)
		{
			MostRecentTime = ModTime;
			MostRecentLog = FullPath;
		}
	}

	return MostRecentLog;
}

bool FReadLogsTool::MatchesSearch(const FString& Line, const TArray<FString>& SearchTerms)
{
	if (SearchTerms.Num() == 0)
	{
		return true;
	}

	FString LowerLine = Line.ToLower();
	for (const FString& Term : SearchTerms)
	{
		if (!LowerLine.Contains(Term))
		{
			return false;
		}
	}
	return true;
}

bool FReadLogsTool::MatchesSeverity(const FString& Line, const FString& Severity)
{
	if (Severity.IsEmpty() || Severity == TEXT("all"))
	{
		return true;
	}

	FString NormSeverity = NormalizeSeverity(Severity);

	// UE log format: [timestamp][category]: Severity: message
	// or: LogCategory: Error: message
	// or just: Error: message

	if (NormSeverity == TEXT("error"))
	{
		return Line.Contains(TEXT("Error:")) || Line.Contains(TEXT("Error]")) ||
			   Line.Contains(TEXT(": Error")) || Line.Contains(TEXT("LogError"));
	}
	else if (NormSeverity == TEXT("warning"))
	{
		return Line.Contains(TEXT("Warning:")) || Line.Contains(TEXT("Warning]")) ||
			   Line.Contains(TEXT(": Warning")) || Line.Contains(TEXT("LogWarning"));
	}
	else if (NormSeverity == TEXT("display"))
	{
		return Line.Contains(TEXT("Display:")) || Line.Contains(TEXT("Display]"));
	}
	else if (NormSeverity == TEXT("log"))
	{
		// Log is basically everything that's not error/warning
		return !Line.Contains(TEXT("Error:")) && !Line.Contains(TEXT("Warning:"));
	}

	return true;
}

FString FReadLogsTool::NormalizeSeverity(const FString& SeverityStr)
{
	FString Lower = SeverityStr.ToLower();
	if (Lower == TEXT("err") || Lower == TEXT("errors"))
	{
		return TEXT("error");
	}
	if (Lower == TEXT("warn") || Lower == TEXT("warnings"))
	{
		return TEXT("warning");
	}
	return Lower;
}

FReadLogsTool::FLogReadResult FReadLogsTool::ReadLogFile(const FString& FilePath, int32 Offset, int32 Limit, int32 Tail,
	const TArray<FString>& SearchTerms, const FString& Severity)
{
	FLogReadResult Result;
	Result.LogPath = FilePath;

	if (FilePath.IsEmpty() || !FPaths::FileExists(FilePath))
	{
		return Result;
	}

	// Read all lines
	TArray<FString> AllLines;
	FFileHelper::LoadFileToStringArray(AllLines, *FilePath);

	Result.TotalLines = AllLines.Num();

	// Apply filters to get matching lines with their original line numbers
	TArray<TPair<int32, FString>> FilteredLines;
	for (int32 i = 0; i < AllLines.Num(); ++i)
	{
		const FString& Line = AllLines[i];

		if (MatchesSeverity(Line, Severity) && MatchesSearch(Line, SearchTerms))
		{
			FilteredLines.Add(TPair<int32, FString>(i + 1, Line)); // 1-based line numbers
		}
	}

	Result.MatchedLines = FilteredLines.Num();

	if (FilteredLines.Num() == 0)
	{
		return Result;
	}

	// Apply pagination
	int32 StartIndex, EndIndex;

	if (Tail > 0)
	{
		// Tail mode: get last N lines
		StartIndex = FMath::Max(0, FilteredLines.Num() - Tail);
		EndIndex = FilteredLines.Num();
	}
	else
	{
		// Offset/limit mode
		StartIndex = FMath::Min(Offset, FilteredLines.Num());
		EndIndex = FMath::Min(StartIndex + Limit, FilteredLines.Num());
	}

	if (StartIndex < FilteredLines.Num())
	{
		Result.StartLine = FilteredLines[StartIndex].Key;
	}
	if (EndIndex > 0 && EndIndex <= FilteredLines.Num())
	{
		Result.EndLine = FilteredLines[EndIndex - 1].Key;
	}

	// Extract the lines for output
	for (int32 i = StartIndex; i < EndIndex; ++i)
	{
		// Include line number prefix
		Result.Lines.Add(FString::Printf(TEXT("%5d: %s"), FilteredLines[i].Key, *FilteredLines[i].Value));
	}

	return Result;
}

FString FReadLogsTool::FormatLogResult(const FLogReadResult& Result, const FString& Operation)
{
	FString Output;

	Output += FString::Printf(TEXT("# %s\n"), *Operation.ToUpper());
	Output += FString::Printf(TEXT("Path: %s\n"), *Result.LogPath);
	Output += FString::Printf(TEXT("Total Lines: %d\n"), Result.TotalLines);

	if (Result.MatchedLines != Result.TotalLines)
	{
		Output += FString::Printf(TEXT("Matched Lines: %d\n"), Result.MatchedLines);
	}

	if (Result.Lines.Num() > 0)
	{
		Output += FString::Printf(TEXT("Showing: lines %d-%d (%d lines)\n\n"),
			Result.StartLine, Result.EndLine, Result.Lines.Num());

		for (const FString& Line : Result.Lines)
		{
			Output += Line + TEXT("\n");
		}

		// Navigation hints
		if (Result.EndLine < Result.TotalLines)
		{
			Output += FString::Printf(TEXT("\n--- More lines available. Use offset=%d to continue ---\n"), Result.EndLine);
		}
	}
	else
	{
		Output += TEXT("\nNo matching lines found.\n");
	}

	return Output;
}

FToolResult FReadLogsTool::ReadOutputLog(int32 Offset, int32 Limit, int32 Tail,
	const TArray<FString>& SearchTerms, const FString& Severity)
{
	// Flush the output log to ensure we read the latest entries (critical during/after PIE)
	if (GLog)
	{
		GLog->Flush();
	}

	FString LogFilePath = GetOutputLogPath();

	if (LogFilePath.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Could not find output log file in Saved/Logs/"));
	}

	FLogReadResult Result = ReadLogFile(LogFilePath, Offset, Limit, Tail, SearchTerms, Severity);

	if (Result.TotalLines == 0)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Log file is empty or could not be read: %s"), *LogFilePath));
	}

	return FToolResult::Ok(FormatLogResult(Result, TEXT("OUTPUT_LOG")));
}

FToolResult FReadLogsTool::ReadMessageLog(const FString& LogName, const FString& Severity, int32 Limit)
{
	// Get the message log module
	FMessageLogModule* MessageLogModule = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog");
	if (!MessageLogModule)
	{
		return FToolResult::Fail(TEXT("MessageLog module not available"));
	}

	// If the log isn't registered yet, try to register it (some logs like PIE are created on-demand)
	if (!MessageLogModule->IsRegisteredLogListing(FName(*LogName)))
	{
		FMessageLogInitializationOptions InitOptions;
		InitOptions.bShowPages = false;
		InitOptions.bAllowClear = true;
		FMessageLog(FName(*LogName));

		// Re-check after registration attempt
		if (!MessageLogModule->IsRegisteredLogListing(FName(*LogName)))
		{
			return FToolResult::Fail(FString::Printf(
				TEXT("Message log '%s' not found. Use operation='list_logs' to see available logs."), *LogName));
		}
	}

	TSharedRef<IMessageLogListing> Listing = MessageLogModule->GetLogListing(FName(*LogName));

	const TArray<TSharedRef<FTokenizedMessage>>& Messages = Listing->GetFilteredMessages();

	FString Output;
	Output += FString::Printf(TEXT("# MESSAGE_LOG: %s\n"), *LogName);
	Output += FString::Printf(TEXT("Total Messages: %d\n\n"), Messages.Num());

	if (Messages.Num() == 0)
	{
		Output += TEXT("No messages in this log.\n");
		return FToolResult::Ok(Output);
	}

	FString NormSeverity = NormalizeSeverity(Severity);
	int32 Count = 0;
	int32 ShownCount = 0;

	// Process messages (most recent first typically)
	for (int32 i = Messages.Num() - 1; i >= 0 && ShownCount < Limit; --i)
	{
		const TSharedRef<FTokenizedMessage>& Message = Messages[i];
		EMessageSeverity::Type MsgSeverity = Message->GetSeverity();

		// Apply severity filter
		if (!NormSeverity.IsEmpty() && NormSeverity != TEXT("all"))
		{
			if (NormSeverity == TEXT("error") && MsgSeverity != EMessageSeverity::Error)
			{
				continue;
			}
			if (NormSeverity == TEXT("warning") && MsgSeverity != EMessageSeverity::Warning &&
				MsgSeverity != EMessageSeverity::PerformanceWarning)
			{
				continue;
			}
			if (NormSeverity == TEXT("info") && MsgSeverity != EMessageSeverity::Info)
			{
				continue;
			}
		}

		FString SeverityStr = SeverityToString(static_cast<int32>(MsgSeverity));
		FString MessageText = Message->ToText().ToString();

		Output += FString::Printf(TEXT("[%s] %s\n"), *SeverityStr, *MessageText);
		ShownCount++;
	}

	if (ShownCount == 0)
	{
		Output += FString::Printf(TEXT("No messages matching severity '%s'.\n"), *Severity);
	}
	else
	{
		Output += FString::Printf(TEXT("\nShowed %d of %d messages.\n"), ShownCount, Messages.Num());
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadLogsTool::CompileBlueprint(const FString& AssetPath, bool bForce)
{
	// Load the asset
	UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
	if (!Asset)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset not found: %s"), *AssetPath));
	}

	UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
	if (!Blueprint)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Asset is not a Blueprint: %s (%s)"),
			*AssetPath, *Asset->GetClass()->GetName()));
	}

	// Validate blueprint is in a compilable state to avoid crashes
	// FRepLayout::AddReferencedObjects can crash if GeneratedClass is null/invalid
	if (!Blueprint->GeneratedClass)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Blueprint '%s' has no generated class - it may be corrupted or partially loaded. Cannot compile."), *Blueprint->GetName()));
	}

	if (Blueprint->bBeingCompiled)
	{
		return FToolResult::Fail(FString::Printf(TEXT("Blueprint '%s' is already being compiled."), *Blueprint->GetName()));
	}

	// Hard-block compilation during PIE. UNetDriver caches FRepLayout per class, holding
	// FProperty* pointers to the old GeneratedClass properties. Compilation frees those
	// properties during PurgeClass/reinstancing, but RemoveClassRepLayoutReferences() is
	// only called on level unload — not during compilation. This causes use-after-free
	// crashes in FRepLayout::AddReferencedObjects(). Affects ALL blueprint types, not
	// just Actor blueprints. No force override — PIE compilation is simply unsafe.
	if (GEditor && GEditor->IsPlayingSessionInEditor())
	{
		return FToolResult::Fail(FString::Printf(
			TEXT("Cannot compile '%s' during PIE. Stop PIE first, then compile."),
			*Blueprint->GetName()));
	}

	// Live Coding check: patched code runs against stale memory layouts from pre-patch
	// instances, causing access violations and ContainerPtr assertion failures.
#if PLATFORM_WINDOWS
	if (Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(AActor::StaticClass()))
	{
		bool bLiveCodingActive = false;
		if (ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME))
		{
			bLiveCodingActive = LiveCoding->HasStarted();
		}

		if (bLiveCodingActive && !bForce)
		{
			// Check if any instances exist in any world
			bool bHasInstances = false;
			if (GEngine)
			{
				TSubclassOf<AActor> ActorClass = static_cast<UClass*>(Blueprint->GeneratedClass);
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					UWorld* World = Context.World();
					if (!World) continue;
					for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
					{
						if (IsValid(*It)) { bHasInstances = true; break; }
					}
					if (bHasInstances) break;
				}
			}

			if (bHasInstances)
			{
				return FToolResult::Fail(FString::Printf(
					TEXT("Cannot compile '%s': Live Coding patches are active and instances exist. "
						 "Compiling will trigger reinstancing, which crashes against stale memory layouts.\n\n"
						 "Fix: Delete instances or restart the editor.\n"
						 "Override: Pass force: true to compile anyway (risk of editor crash)."),
					*Blueprint->GetName()));
			}
		}
	}
#endif

	FString Output;
	Output += FString::Printf(TEXT("# COMPILE_BLUEPRINT: %s\n\n"), *Blueprint->GetName());

	// Get status before compilation
	EBlueprintStatus StatusBefore = Blueprint->Status;
	Output += FString::Printf(TEXT("Status Before: %s\n"),
		StatusBefore == BS_UpToDate ? TEXT("UpToDate") :
		StatusBefore == BS_Dirty ? TEXT("Dirty") :
		StatusBefore == BS_Error ? TEXT("Error") :
		StatusBefore == BS_BeingCreated ? TEXT("BeingCreated") :
		TEXT("Unknown"));

	// Create a compiler results log to capture messages
	FCompilerResultsLog CompileLog;
	CompileLog.bSilentMode = false;
	CompileLog.bAnnotateMentionedNodes = true;

	// Full compile matching editor Compile button behavior.
	// SkipReinstancing is internal-only — engine ensure(!bSkipReinstancing) in CompileSynchronouslyImpl fires EXC_BREAKPOINT.
	// SkipGarbageCollection was a workaround for stale FRepLayout — no longer needed since structural
	// operations now go through proper engine APIs (AddMacroGraph, AddUbergraphPage, etc.)
	// that trigger skeleton regeneration via MarkBlueprintAsStructurallyModified.
	FKismetEditorUtilities::CompileBlueprint(Blueprint,
		EBlueprintCompileOptions::SkipFiBSearchMetaUpdate,
		&CompileLog);

	// Get status after compilation
	EBlueprintStatus StatusAfter = Blueprint->Status;
	Output += FString::Printf(TEXT("Status After: %s\n"),
		StatusAfter == BS_UpToDate ? TEXT("UpToDate") :
		StatusAfter == BS_Dirty ? TEXT("Dirty") :
		StatusAfter == BS_Error ? TEXT("Error") :
		StatusAfter == BS_BeingCreated ? TEXT("BeingCreated") :
		TEXT("Unknown"));

	// Report compilation results
	Output += FString::Printf(TEXT("Errors: %d\n"), CompileLog.NumErrors);
	Output += FString::Printf(TEXT("Warnings: %d\n\n"), CompileLog.NumWarnings);

	if (CompileLog.Messages.Num() > 0)
	{
		Output += TEXT("## Messages:\n\n");

		for (const TSharedRef<FTokenizedMessage>& Message : CompileLog.Messages)
		{
			FString SeverityStr = SeverityToString(static_cast<int32>(Message->GetSeverity()));
			FString MessageText = Message->ToText().ToString();

			// Clean up the message text for readability
			MessageText.ReplaceInline(TEXT("\r\n"), TEXT(" "), ESearchCase::CaseSensitive);
			MessageText.ReplaceInline(TEXT("\n"), TEXT(" "), ESearchCase::CaseSensitive);

			Output += FString::Printf(TEXT("[%s] %s\n"), *SeverityStr, *MessageText);
		}
	}
	else if (CompileLog.NumErrors == 0 && CompileLog.NumWarnings == 0)
	{
		Output += TEXT("Compilation successful - no errors or warnings.\n");
	}

	// Also check the BlueprintLog message log for any additional messages
	FMessageLogModule* MessageLogModule = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog");
	if (MessageLogModule && MessageLogModule->IsRegisteredLogListing(FName("BlueprintLog")))
	{
		TSharedRef<IMessageLogListing> BlueprintLog = MessageLogModule->GetLogListing(FName("BlueprintLog"));
		const TArray<TSharedRef<FTokenizedMessage>>& LogMessages = BlueprintLog->GetFilteredMessages();

		// Show recent messages that might be related to this compilation
		int32 RecentMsgCount = 0;
		for (int32 i = LogMessages.Num() - 1; i >= 0 && RecentMsgCount < 10; --i)
		{
			const TSharedRef<FTokenizedMessage>& Msg = LogMessages[i];
			FString MsgText = Msg->ToText().ToString();

			// Check if message relates to this blueprint
			if (MsgText.Contains(Blueprint->GetName()))
			{
				if (RecentMsgCount == 0)
				{
					Output += TEXT("\n## Recent BlueprintLog Messages:\n\n");
				}

				FString SeverityStr = SeverityToString(static_cast<int32>(Msg->GetSeverity()));
				Output += FString::Printf(TEXT("[%s] %s\n"), *SeverityStr, *MsgText);
				RecentMsgCount++;
			}
		}
	}

	return FToolResult::Ok(Output);
}

FToolResult FReadLogsTool::ListMessageLogs()
{
	FString Output;
	Output += TEXT("# AVAILABLE MESSAGE LOGS\n\n");

	// These are the common message logs in UE
	TArray<FString> CommonLogs = {
		TEXT("BlueprintLog"),
		TEXT("MapCheck"),
		TEXT("PIE"),
		TEXT("LoadErrors"),
		TEXT("EditorErrors"),
		TEXT("SlateStyleLog"),
		TEXT("AssetCheck"),
		TEXT("LightingResults"),
		TEXT("BuildAndSubmitErrors"),
		TEXT("PackagingResults"),
		TEXT("LocalizationService"),
		TEXT("HLODResults"),
		TEXT("AutomationTestingLog")
	};

	FMessageLogModule* MessageLogModule = FModuleManager::GetModulePtr<FMessageLogModule>("MessageLog");
	if (!MessageLogModule)
	{
		Output += TEXT("MessageLog module not available.\n");
		return FToolResult::Ok(Output);
	}

	Output += TEXT("## Registered Logs:\n\n");

	int32 FoundCount = 0;
	for (const FString& LogName : CommonLogs)
	{
		if (MessageLogModule->IsRegisteredLogListing(FName(*LogName)))
		{
			TSharedRef<IMessageLogListing> Listing = MessageLogModule->GetLogListing(FName(*LogName));
			int32 MsgCount = Listing->NumMessages(EMessageSeverity::Info);

			Output += FString::Printf(TEXT("- %s (%d messages)\n"), *LogName, MsgCount);
			FoundCount++;
		}
	}

	if (FoundCount == 0)
	{
		Output += TEXT("No message logs currently registered.\n");
	}

	Output += TEXT("\n## Common Log Descriptions:\n\n");
	Output += TEXT("- BlueprintLog: Blueprint compilation errors and warnings\n");
	Output += TEXT("- MapCheck: Level/map validation errors\n");
	Output += TEXT("- PIE: Play-In-Editor messages\n");
	Output += TEXT("- LoadErrors: Asset loading failures\n");
	Output += TEXT("- EditorErrors: General editor errors\n");
	Output += TEXT("- AssetCheck: Asset validation results\n");
	Output += TEXT("- LightingResults: Lighting build results\n");

	return FToolResult::Ok(Output);
}

FString FReadLogsTool::SeverityToString(int32 Severity)
{
	switch (static_cast<EMessageSeverity::Type>(Severity))
	{
	case EMessageSeverity::Error:
		return TEXT("ERROR");
	case EMessageSeverity::PerformanceWarning:
		return TEXT("PERF_WARN");
	case EMessageSeverity::Warning:
		return TEXT("WARNING");
	case EMessageSeverity::Info:
	default:
		return TEXT("INFO");
	}
}

FToolResult FReadLogsTool::GetLiveCodingStatus()
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding)
	{
		return FToolResult::Fail(TEXT("Live Coding module not available. Make sure Live Coding is enabled in Editor Preferences."));
	}

	FString Output;
	Output += TEXT("# LIVE CODING STATUS\n\n");

	// Basic status
	bool bEnabledByDefault = LiveCoding->IsEnabledByDefault();
	bool bEnabledForSession = LiveCoding->IsEnabledForSession();
	bool bHasStarted = LiveCoding->HasStarted();
	bool bIsCompiling = LiveCoding->IsCompiling();

	Output += FString::Printf(TEXT("Enabled by Default: %s\n"), bEnabledByDefault ? TEXT("Yes") : TEXT("No"));
	Output += FString::Printf(TEXT("Enabled for Session: %s\n"), bEnabledForSession ? TEXT("Yes") : TEXT("No"));
	Output += FString::Printf(TEXT("Has Started: %s\n"), bHasStarted ? TEXT("Yes") : TEXT("No"));
	Output += FString::Printf(TEXT("Currently Compiling: %s\n"), bIsCompiling ? TEXT("Yes") : TEXT("No"));

	// Can we enable it?
	bool bCanEnable = LiveCoding->CanEnableForSession();
	Output += FString::Printf(TEXT("Can Enable for Session: %s\n"), bCanEnable ? TEXT("Yes") : TEXT("No (modules may have been hot-reloaded)"));

	Output += TEXT("\n## Usage\n\n");
	if (!bEnabledForSession && bCanEnable)
	{
		Output += TEXT("Live Coding is not enabled. Enable it in Editor Preferences > Live Coding, or use 'livecoding_compile' to auto-enable and compile.\n");
	}
	else if (bEnabledForSession && bHasStarted)
	{
		Output += TEXT("Live Coding is active. Use 'livecoding_compile' to trigger a C++ compilation.\n");
	}
	else if (!bCanEnable)
	{
		Output += TEXT("Cannot enable Live Coding - modules may have been hot-reloaded using the legacy system. Restart the editor to use Live Coding.\n");
	}

	return FToolResult::Ok(Output);
#else
	return FToolResult::Fail(TEXT("Live Coding is only available on Windows."));
#endif
}

FToolResult FReadLogsTool::TriggerLiveCodingCompile()
{
#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding)
	{
		return FToolResult::Fail(TEXT("Live Coding module not available. Make sure Live Coding is enabled in Editor Preferences."));
	}

	// Check if we can enable Live Coding
	if (!LiveCoding->IsEnabledForSession())
	{
		if (!LiveCoding->CanEnableForSession())
		{
			return FToolResult::Fail(TEXT("Cannot enable Live Coding - modules may have been hot-reloaded using the legacy system. Restart the editor to use Live Coding."));
		}

		// Enable Live Coding for this session
		LiveCoding->EnableForSession(true);
	}

	// Check if already compiling
	if (LiveCoding->IsCompiling())
	{
		return FToolResult::Fail(TEXT("Live Coding is already compiling. Wait for the current compilation to finish."));
	}

	FString Output;
	Output += TEXT("# LIVE CODING COMPILE\n\n");

	// Trigger the compile and wait for result
	ELiveCodingCompileResult CompileResult = ELiveCodingCompileResult::Failure;
	bool bSuccess = LiveCoding->Compile(ELiveCodingCompileFlags::WaitForCompletion, &CompileResult);

	// Report result
	FString ResultStr;
	switch (CompileResult)
	{
	case ELiveCodingCompileResult::Success:
		ResultStr = TEXT("SUCCESS - Code was recompiled and patched");
		break;
	case ELiveCodingCompileResult::NoChanges:
		ResultStr = TEXT("NO_CHANGES - No code changes detected");
		break;
	case ELiveCodingCompileResult::Failure:
		ResultStr = TEXT("FAILURE - Compilation failed (see errors below)");
		break;
	case ELiveCodingCompileResult::Cancelled:
		ResultStr = TEXT("CANCELLED - Compilation was cancelled");
		break;
	case ELiveCodingCompileResult::InProgress:
		ResultStr = TEXT("IN_PROGRESS - Compilation started (async)");
		break;
	case ELiveCodingCompileResult::CompileStillActive:
		ResultStr = TEXT("COMPILE_STILL_ACTIVE - Previous compilation still running");
		break;
	case ELiveCodingCompileResult::NotStarted:
		ResultStr = TEXT("NOT_STARTED - Live Coding console could not be started");
		break;
	default:
		ResultStr = TEXT("UNKNOWN");
		break;
	}

	Output += FString::Printf(TEXT("Result: %s\n\n"), *ResultStr);

	// Read the output log to get compiler messages
	// Live Coding logs to LogLiveCoding category
	Output += TEXT("## Compiler Output (from LogLiveCoding)\n\n");

	TArray<FString> SearchTerms;
	SearchTerms.Add(TEXT("livecoding"));

	FLogReadResult LogResult = ReadLogFile(GetOutputLogPath(), 0, 200, 100, SearchTerms, TEXT("all"));

	if (LogResult.Lines.Num() > 0)
	{
		for (const FString& Line : LogResult.Lines)
		{
			Output += Line + TEXT("\n");
		}
	}
	else
	{
		Output += TEXT("No Live Coding log entries found. Check the full output_log for compiler errors.\n");
	}

	// If compilation failed, suggest checking the full log
	if (CompileResult == ELiveCodingCompileResult::Failure)
	{
		Output += TEXT("\n## Tip\n\n");
		Output += TEXT("Compilation failed. To see full compiler errors, use:\n");
		Output += TEXT("  read_logs with operation='output_log', search=['error'], tail=50\n");
	}

	return FToolResult::Ok(Output);
#else
	return FToolResult::Fail(TEXT("Live Coding is only available on Windows."));
#endif
}
