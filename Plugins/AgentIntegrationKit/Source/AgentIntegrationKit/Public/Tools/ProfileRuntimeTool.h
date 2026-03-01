// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Runtime profiling control tool.
 *
 * This tool controls profiling capture sessions and returns raw capture paths.
 * It does not perform diagnosis; agent reasoning happens after data capture.
 *
 * Operations:
 * - start: starts a profiling session
 * - stop: stops the active profiling session
 * - status: returns current profiling session status
 * - analyze: converts a .utrace into CSV artifacts the agent can read
 *
 * Modes:
 * - trace (default): Unreal Insights trace capture (.utrace) via Trace.File / Trace.Stop
 * - csv: CSV profiler capture via CsvProfile Start/Stop
 * - statfile: legacy stat capture via stat startfile/stopfile
 */
class AGENTINTEGRATIONKIT_API FProfileRuntimeTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("profile_runtime"); }
	virtual FString GetDescription() const override
	{
		return TEXT(
			"Control runtime profiling captures for playtest workflows. "
			"Recommended flow for stutter/perf reports: call start, ask the user to run PIE/playtest, then call stop when user confirms done. "
			"Then call analyze so the agent can read engine-generated CSV artifacts without manual Unreal Insights inspection. "
			"Operations: 'start', 'stop', 'status', 'analyze'. "
			"Default mode is Unreal Insights trace capture (trace). "
			"Optional modes: csv, statfile. "
			"Returns capture artifact paths and executed commands; does not invent diagnosis.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;
};
