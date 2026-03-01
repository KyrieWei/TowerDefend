// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

class UBlueprint;

/**
 * Tool for reading Unreal Engine logs with advanced navigation:
 *
 * Operations:
 * - output_log: Read the main output log (from Saved/Logs/) with pagination
 * - message_log: Read message logs (BlueprintLog, MapCheck, PIE, LoadErrors, etc.)
 * - compile_blueprint: Compile a blueprint and return any errors/warnings
 * - list_logs: List available message logs
 * - livecoding_status: Check Live Coding status (enabled, compiling, last result)
 * - livecoding_compile: Trigger a Live Coding C++ compile and return raw output
 *
 * Features:
 * - offset/limit: Read specific line ranges (like reading a file)
 * - tail: Read the last N lines
 * - search: Filter lines by keyword(s)
 * - severity: Filter by log severity (Error, Warning, Display, Log, etc.)
 * - Returns total line count for navigation
 *
 * This is essential for AI to diagnose issues with blueprints and see editor errors.
 */
class AGENTINTEGRATIONKIT_API FReadLogsTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("read_logs"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Read editor logs with search and pagination. "
					"Operations: 'output_log' (main log with offset/limit/tail/search), "
					"'message_log' (BlueprintLog, MapCheck, PIE, etc.), "
					"'compile_blueprint' (compile BP and get errors), "
					"'list_logs' (show available message logs), "
					"'livecoding_status' (check C++ Live Coding status), "
					"'livecoding_compile' (trigger C++ compile, get raw errors). "
					"Returns total_lines for navigation.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Result metadata for log reading */
	struct FLogReadResult
	{
		TArray<FString> Lines;
		int32 TotalLines = 0;
		int32 MatchedLines = 0;
		int32 StartLine = 0;
		int32 EndLine = 0;
		FString LogPath;
	};

	/** Read the main output log file with advanced options */
	FToolResult ReadOutputLog(int32 Offset, int32 Limit, int32 Tail, const TArray<FString>& SearchTerms, const FString& Severity);

	/** Read output log file and return structured result */
	FLogReadResult ReadLogFile(const FString& FilePath, int32 Offset, int32 Limit, int32 Tail,
							   const TArray<FString>& SearchTerms, const FString& Severity);

	/** Read a message log (BlueprintLog, MapCheck, PIE, etc.) */
	FToolResult ReadMessageLog(const FString& LogName, const FString& Severity, int32 Limit);

	/** Compile a blueprint and return errors/warnings */
	FToolResult CompileBlueprint(const FString& AssetPath, bool bForce = false);

	/** List available message logs */
	FToolResult ListMessageLogs();

	/** Get Live Coding status (enabled, compiling, last result) */
	FToolResult GetLiveCodingStatus();

	/** Trigger a Live Coding compile and return raw compiler output */
	FToolResult TriggerLiveCodingCompile();

	/** Get the path to the current output log file */
	FString GetOutputLogPath();

	/** Check if a line matches search terms (case-insensitive, all terms must match) */
	bool MatchesSearch(const FString& Line, const TArray<FString>& SearchTerms);

	/** Check if a line matches severity filter */
	bool MatchesSeverity(const FString& Line, const FString& Severity);

	/** Format the result with metadata header */
	FString FormatLogResult(const FLogReadResult& Result, const FString& Operation);

	/** Convert message severity enum to string */
	FString SeverityToString(int32 Severity);

	/** Parse severity string to filter value */
	FString NormalizeSeverity(const FString& SeverityStr);
};
