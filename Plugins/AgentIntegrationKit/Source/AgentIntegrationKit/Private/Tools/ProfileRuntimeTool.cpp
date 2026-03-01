// Copyright 2026 Betide Studio. All Rights Reserved.

#include "Tools/ProfileRuntimeTool.h"
#include "ACPSettings.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7
#include "Misc/StringOutputDevice.h"
#endif
#include "ProfilingDebugging/TraceAuxiliary.h"

namespace
{
	struct FProfileRuntimeSessionState
	{
		bool bActive = false;
		FString Mode;
		FString SessionName;
		FString Channels;
		FString TraceFilePath;
		FString CsvCaptureName;
		FDateTime StartedAt;
		TArray<FString> StartCommands;
	};

	FCriticalSection GProfileRuntimeStateLock;
	FProfileRuntimeSessionState GProfileRuntimeState;
	FString GLastTraceArtifactPath;
	FString GLastCsvCaptureName;
	FString GLastCsvArtifactPath;
	FString GLastCaptureMode;

	struct FTraceSummaryArtifacts
	{
		FString ScopesCsv;
		FString CountersCsv;
		FString BookmarksCsv;
		FString TelemetryCsv;
	};

	struct FTopScopeRow
	{
		FString Name;
		uint64 Count = 0;
		double TotalDurationSeconds = 0.0;
		double MeanDurationSeconds = 0.0;
		double MaxDurationSeconds = 0.0;
	};

	static bool ExecuteConsoleCommand(const FString& Command, FString& OutResult)
	{
		if (!GEngine)
		{
			OutResult = TEXT("Engine is not available.");
			return false;
		}

		FStringOutputDevice Ar;
		const bool bOk = GEngine->Exec(nullptr, *Command, Ar);
		OutResult = Ar;
		return bOk;
	}

	static FString BuildDefaultSessionName()
	{
		return FString::Printf(TEXT("aik_profile_%s"), *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")));
	}

	static FString SanitizeSessionName(const FString& InSessionName)
	{
		FString Out = InSessionName;
		Out.TrimStartAndEndInline();
		if (Out.IsEmpty())
		{
			return BuildDefaultSessionName();
		}

		for (int32 Index = 0; Index < Out.Len(); ++Index)
		{
			const TCHAR C = Out[Index];
			const bool bIsAllowed =
				(C >= 'a' && C <= 'z') ||
				(C >= 'A' && C <= 'Z') ||
				(C >= '0' && C <= '9') ||
				C == '_' || C == '-';
			if (!bIsAllowed)
			{
				Out[Index] = '_';
			}
		}

		return Out;
	}

	static FString BuildDefaultTracePath(const FString& SessionName)
	{
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Profiling") / TEXT("AIK");
		IFileManager::Get().MakeDirectory(*Directory, true);
		return FPaths::ConvertRelativePathToFull(Directory / FString::Printf(TEXT("%s.utrace"), *SessionName));
	}

	static FString NormalizePathForRead(const FString& InPath)
	{
		FString Path = InPath;
		Path.TrimStartAndEndInline();
		if (Path.IsEmpty())
		{
			return Path;
		}
		if (FPaths::IsRelative(Path))
		{
			Path = FPaths::ConvertRelativePathToFull(UACPSettings::GetWorkingDirectory() / Path);
		}
		FPaths::NormalizeFilename(Path);
		return Path;
	}

	static FString FindNewestTraceArtifact(const FDateTime& NotBefore)
	{
		TArray<FString> FoundFiles;
		const FString SearchRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir());
		IFileManager::Get().FindFilesRecursive(FoundFiles, *SearchRoot, TEXT("*.utrace"), true, false, false);

		FDateTime BestTime = FDateTime::MinValue();
		FString BestFile;
		for (const FString& File : FoundFiles)
		{
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*File);
			if (Stamp >= NotBefore && Stamp > BestTime)
			{
				BestTime = Stamp;
				BestFile = File;
			}
		}
		return BestFile;
	}

	static FString GetCsvProfilingDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProfilingDir() / TEXT("CSV"));
	}

	static TArray<FString> FindCsvArtifactsByPrefix(const FString& Prefix)
	{
		TArray<FString> Results;
		const FString CsvDir = GetCsvProfilingDirectory();
		if (!IFileManager::Get().DirectoryExists(*CsvDir))
		{
			return Results;
		}

		TArray<FString> FileNames;
		IFileManager::Get().FindFiles(FileNames, *(CsvDir / TEXT("*")), true, false);
		for (const FString& FileName : FileNames)
		{
			if (FileName.StartsWith(Prefix, ESearchCase::IgnoreCase))
			{
				FString FullPath = CsvDir / FileName;
				FPaths::NormalizeFilename(FullPath);
				Results.Add(FullPath);
			}
		}
		return Results;
	}

	static FString FindBestCsvArtifactForCapture(const FString& CaptureName)
	{
		const TArray<FString> Candidates = FindCsvArtifactsByPrefix(CaptureName);
		if (Candidates.Num() == 0)
		{
			return TEXT("");
		}

		FDateTime BestTime = FDateTime::MinValue();
		FString BestPath;
		for (const FString& Candidate : Candidates)
		{
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*Candidate);
			if (Stamp >= BestTime)
			{
				BestTime = Stamp;
				BestPath = Candidate;
			}
		}

		for (const FString& Candidate : Candidates)
		{
			if (FPaths::GetCleanFilename(Candidate).Equals(CaptureName, ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
		for (const FString& Candidate : Candidates)
		{
			if (Candidate.EndsWith(TEXT(".csv"), ESearchCase::IgnoreCase))
			{
				return Candidate;
			}
		}
		return BestPath;
	}

	static FString FindNewestCsvArtifact(const FDateTime& NotBefore)
	{
		const FString CsvDir = GetCsvProfilingDirectory();
		if (!IFileManager::Get().DirectoryExists(*CsvDir))
		{
			return TEXT("");
		}

		TArray<FString> FileNames;
		IFileManager::Get().FindFiles(FileNames, *(CsvDir / TEXT("*")), true, false);
		FDateTime BestTime = FDateTime::MinValue();
		FString BestPath;
		for (const FString& FileName : FileNames)
		{
			FString FullPath = CsvDir / FileName;
			FPaths::NormalizeFilename(FullPath);
			const FDateTime Stamp = IFileManager::Get().GetTimeStamp(*FullPath);
			if (Stamp >= NotBefore && Stamp > BestTime)
			{
				BestTime = Stamp;
				BestPath = FullPath;
			}
		}
		return BestPath;
	}

	static FString NormalizeMode(const FString& InMode)
	{
		FString Mode = InMode;
		Mode.TrimStartAndEndInline();
		Mode = Mode.ToLower();
		if (Mode.IsEmpty())
		{
			Mode = TEXT("trace");
		}
		return Mode;
	}

	static bool IsSupportedMode(const FString& Mode)
	{
		return Mode == TEXT("trace") || Mode == TEXT("csv") || Mode == TEXT("statfile");
	}

	static bool IsTraceConnectionActive(FString* OutDestination = nullptr)
	{
		const bool bConnected = FTraceAuxiliary::IsConnected();
		const FString Destination = FTraceAuxiliary::GetTraceDestinationString();
		const bool bHasDestination = !Destination.IsEmpty();
		if (OutDestination)
		{
			*OutDestination = Destination;
		}
		return bConnected || bHasDestination;
	}

	static bool WaitForTraceConnection(double TimeoutSeconds, FString& OutDestination)
	{
		const double EndTime = FPlatformTime::Seconds() + TimeoutSeconds;
		while (FPlatformTime::Seconds() <= EndTime)
		{
			if (IsTraceConnectionActive(&OutDestination))
			{
				return true;
			}
			FPlatformProcess::SleepNoStats(0.05f);
		}
		return IsTraceConnectionActive(&OutDestination);
	}

	static FString BuildEditorCmdExecutablePath()
	{
		TArray<FString> Candidates;
		const FString ExecutableDir = FPaths::GetPath(FPlatformProcess::ExecutablePath());

#if PLATFORM_WINDOWS
		const FString EditorCmdName = TEXT("UnrealEditor-Cmd.exe");
		const FString EditorName = TEXT("UnrealEditor.exe");
		const FString PlatformDir = TEXT("Win64");
#elif PLATFORM_MAC
		const FString EditorCmdName = TEXT("UnrealEditor-Cmd");
		const FString EditorName = TEXT("UnrealEditor");
		const FString PlatformDir = TEXT("Mac");
#else
		const FString EditorCmdName = TEXT("UnrealEditor-Cmd");
		const FString EditorName = TEXT("UnrealEditor");
		const FString PlatformDir = TEXT("Linux");
#endif

		Candidates.Add(ExecutableDir / EditorCmdName);
		Candidates.Add(ExecutableDir / EditorName);
		Candidates.Add(FPaths::ConvertRelativePathToFull(ExecutableDir / TEXT("../../../") / EditorCmdName));
		Candidates.Add(FPaths::ConvertRelativePathToFull(ExecutableDir / TEXT("../../../") / EditorName));
		Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries") / PlatformDir / EditorCmdName));
		Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries") / PlatformDir / EditorName));
		Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::RootDir() / TEXT("Engine") / TEXT("Binaries") / PlatformDir / EditorCmdName));
		Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::RootDir() / TEXT("Engine") / TEXT("Binaries") / PlatformDir / EditorName));

		for (FString Candidate : Candidates)
		{
			FPaths::NormalizeFilename(Candidate);
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return TEXT("");
	}

	static FTraceSummaryArtifacts BuildSummaryArtifacts(const FString& TraceFilePath)
	{
		const FString TraceDir = FPaths::GetPath(TraceFilePath);
		const FString TraceBase = FPaths::GetBaseFilename(TraceFilePath);

		FTraceSummaryArtifacts Artifacts;
		Artifacts.ScopesCsv = TraceDir / FString::Printf(TEXT("%sScopes.csv"), *TraceBase);
		Artifacts.CountersCsv = TraceDir / FString::Printf(TEXT("%sCounters.csv"), *TraceBase);
		Artifacts.BookmarksCsv = TraceDir / FString::Printf(TEXT("%sBookmarks.csv"), *TraceBase);
		Artifacts.TelemetryCsv = TraceDir / FString::Printf(TEXT("%sTelemetry.csv"), *TraceBase);
		return Artifacts;
	}

	static bool ParseUInt64Field(const FString& Text, uint64& OutValue)
	{
		const TCHAR* Start = *Text;
		TCHAR* End = nullptr;
		OutValue = FCString::Strtoui64(Start, &End, 10);
		return End != Start;
	}

	static bool ParseDoubleField(const FString& Text, double& OutValue)
	{
		OutValue = FCString::Atod(*Text);
		return true;
	}

	static bool ReadTopScopesFromCsv(const FString& ScopesCsvPath, int32 TopCount, TArray<FTopScopeRow>& OutRows, FString& OutError)
	{
		OutRows.Reset();

		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *ScopesCsvPath))
		{
			OutError = FString::Printf(TEXT("Failed to read scopes csv: %s"), *ScopesCsvPath);
			return false;
		}

		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);
		if (Lines.Num() <= 1)
		{
			OutError = FString::Printf(TEXT("Scopes csv has no rows: %s"), *ScopesCsvPath);
			return false;
		}

		TArray<FTopScopeRow> ParsedRows;
		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			if (Line.IsEmpty())
			{
				continue;
			}

			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT(","), false);
			if (Cells.Num() < 13)
			{
				continue;
			}

			FTopScopeRow Row;
			Row.Name = Cells[0];
			if (Row.Name.IsEmpty())
			{
				continue;
			}

			ParseUInt64Field(Cells[1], Row.Count);
			ParseDoubleField(Cells[3], Row.TotalDurationSeconds);
			ParseDoubleField(Cells[11], Row.MaxDurationSeconds);
			ParseDoubleField(Cells[12], Row.MeanDurationSeconds);
			if (!FMath::IsFinite(Row.TotalDurationSeconds) || Row.TotalDurationSeconds < 0.0)
			{
				continue;
			}
			ParsedRows.Add(Row);
		}

		if (ParsedRows.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Scopes csv had no parseable rows: %s"), *ScopesCsvPath);
			return false;
		}

		ParsedRows.Sort([](const FTopScopeRow& A, const FTopScopeRow& B)
		{
			return A.TotalDurationSeconds > B.TotalDurationSeconds;
		});

		const int32 ClampedTop = FMath::Clamp(TopCount, 1, 50);
		OutRows.Append(ParsedRows.GetData(), FMath::Min(ClampedTop, ParsedRows.Num()));
		return true;
	}

	struct FNumericSeries
	{
		int32 Count = 0;
		double Sum = 0.0;
		double Min = TNumericLimits<double>::Max();
		double Max = -TNumericLimits<double>::Max();
		TArray<double> Values;

		void Add(double Value)
		{
			if (!FMath::IsFinite(Value))
			{
				return;
			}
			++Count;
			Sum += Value;
			Min = FMath::Min(Min, Value);
			Max = FMath::Max(Max, Value);
			Values.Add(Value);
		}

		double Mean() const
		{
			return Count > 0 ? (Sum / double(Count)) : 0.0;
		}

		double Percentile(double Pct) const
		{
			if (Values.Num() == 0)
			{
				return 0.0;
			}
			TArray<double> Sorted = Values;
			Sorted.Sort();
			const double ClampedPct = FMath::Clamp(Pct, 0.0, 100.0);
			const double Rank = (ClampedPct / 100.0) * double(Sorted.Num() - 1);
			const int32 Index = FMath::Clamp(FMath::RoundToInt(Rank), 0, Sorted.Num() - 1);
			return Sorted[Index];
		}
	};

	struct FFrameBreakdown
	{
		int32 FrameIndex = 0;
		double FrameMs = 0.0;
		double GameMs = 0.0;
		double RenderMs = 0.0;
		double GpuMs = 0.0;
	};

	static bool GetFiniteCellValue(const TArray<FString>& Cells, int32 Index, double& OutValue)
	{
		if (Index < 0 || Index >= Cells.Num())
		{
			return false;
		}
		OutValue = FCString::Atod(*Cells[Index]);
		return FMath::IsFinite(OutValue);
	}

	static int32 FindColumnIndex(const TArray<FString>& HeaderCells, const TArray<FString>& CandidateNames)
	{
		for (int32 i = 0; i < HeaderCells.Num(); ++i)
		{
			for (const FString& Candidate : CandidateNames)
			{
				if (HeaderCells[i].Equals(Candidate, ESearchCase::IgnoreCase))
				{
					return i;
				}
			}
		}
		return INDEX_NONE;
	}

	static bool AnalyzeCsvArtifact(const FString& CsvPath, int32 TopCount, FString& OutResult, FString& OutError)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *CsvPath))
		{
			OutError = FString::Printf(TEXT("Failed to read CSV artifact: %s"), *CsvPath);
			return false;
		}

		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);
		if (Lines.Num() < 2)
		{
			OutError = FString::Printf(TEXT("CSV artifact has insufficient rows: %s"), *CsvPath);
			return false;
		}

		TArray<FString> HeaderCells;
		Lines[0].ParseIntoArray(HeaderCells, TEXT(","), false);
		if (HeaderCells.Num() < 2)
		{
			OutError = FString::Printf(TEXT("CSV artifact header could not be parsed: %s"), *CsvPath);
			return false;
		}

		const int32 FrameIdx = FindColumnIndex(HeaderCells, { TEXT("FrameTime") });
		const int32 GameIdx = FindColumnIndex(HeaderCells, { TEXT("GameThreadTime"), TEXT("GameThreadTime_CriticalPath") });
		const int32 RenderIdx = FindColumnIndex(HeaderCells, { TEXT("RenderThreadTime"), TEXT("RenderThreadTime_CriticalPath") });
		const int32 GpuIdx = FindColumnIndex(HeaderCells, { TEXT("GPUTime") });
		const int32 RhiIdx = FindColumnIndex(HeaderCells, { TEXT("RHIThreadTime") });

		if (FrameIdx == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("CSV artifact does not contain FrameTime column: %s"), *CsvPath);
			return false;
		}

		FNumericSeries FrameSeries;
		FNumericSeries GameSeries;
		FNumericSeries RenderSeries;
		FNumericSeries GpuSeries;
		FNumericSeries RhiSeries;

		int32 HitchOver33 = 0;
		int32 HitchOver50 = 0;
		int32 BoundByGame = 0;
		int32 BoundByRender = 0;
		int32 BoundByGpu = 0;
		TArray<FFrameBreakdown> WorstFrames;
		WorstFrames.Reserve(Lines.Num());

		for (int32 LineIndex = 1; LineIndex < Lines.Num(); ++LineIndex)
		{
			const FString& Line = Lines[LineIndex];
			if (Line.IsEmpty())
			{
				continue;
			}

			TArray<FString> Cells;
			Line.ParseIntoArray(Cells, TEXT(","), false);
			if (Cells.Num() <= FrameIdx)
			{
				continue;
			}

			double FrameMs = 0.0;
			if (!GetFiniteCellValue(Cells, FrameIdx, FrameMs))
			{
				continue;
			}

			FrameSeries.Add(FrameMs);
			if (FrameMs > 33.333)
			{
				++HitchOver33;
			}
			if (FrameMs > 50.0)
			{
				++HitchOver50;
			}

			double GameMs = 0.0;
			double RenderMs = 0.0;
			double GpuMs = 0.0;
			double RhiMs = 0.0;

			const bool bHasGame = GetFiniteCellValue(Cells, GameIdx, GameMs);
			const bool bHasRender = GetFiniteCellValue(Cells, RenderIdx, RenderMs);
			const bool bHasGpu = GetFiniteCellValue(Cells, GpuIdx, GpuMs);
			const bool bHasRhi = GetFiniteCellValue(Cells, RhiIdx, RhiMs);

			if (bHasGame) GameSeries.Add(GameMs);
			if (bHasRender) RenderSeries.Add(RenderMs);
			if (bHasGpu) GpuSeries.Add(GpuMs);
			if (bHasRhi) RhiSeries.Add(RhiMs);

			double DominantValue = -TNumericLimits<double>::Max();
			FString Dominant;
			if (bHasGame && GameMs > DominantValue)
			{
				DominantValue = GameMs;
				Dominant = TEXT("game");
			}
			if (bHasRender && RenderMs > DominantValue)
			{
				DominantValue = RenderMs;
				Dominant = TEXT("render");
			}
			if (bHasGpu && GpuMs > DominantValue)
			{
				DominantValue = GpuMs;
				Dominant = TEXT("gpu");
			}
			if (Dominant == TEXT("game")) ++BoundByGame;
			else if (Dominant == TEXT("render")) ++BoundByRender;
			else if (Dominant == TEXT("gpu")) ++BoundByGpu;

			FFrameBreakdown Breakdown;
			Breakdown.FrameIndex = LineIndex;
			Breakdown.FrameMs = FrameMs;
			Breakdown.GameMs = bHasGame ? GameMs : 0.0;
			Breakdown.RenderMs = bHasRender ? RenderMs : 0.0;
			Breakdown.GpuMs = bHasGpu ? GpuMs : 0.0;
			WorstFrames.Add(Breakdown);
		}

		if (FrameSeries.Count == 0)
		{
			OutError = FString::Printf(TEXT("CSV artifact did not contain any parseable frame rows: %s"), *CsvPath);
			return false;
		}

		WorstFrames.Sort([](const FFrameBreakdown& A, const FFrameBreakdown& B)
		{
			return A.FrameMs > B.FrameMs;
		});

		const int32 TopWorst = FMath::Clamp(TopCount, 1, 50);
		FString Result;
		Result += TEXT("PROFILE ANALYZED\n");
		Result += FString::Printf(TEXT("analysis_mode=csv_direct\n"));
		Result += FString::Printf(TEXT("artifact_csv=%s\n"), *CsvPath);
		Result += FString::Printf(TEXT("frames=%d\n"), FrameSeries.Count);
		Result += FString::Printf(TEXT("frame_time_avg_ms=%.3f\n"), FrameSeries.Mean());
		Result += FString::Printf(TEXT("frame_time_p95_ms=%.3f\n"), FrameSeries.Percentile(95.0));
		Result += FString::Printf(TEXT("frame_time_p99_ms=%.3f\n"), FrameSeries.Percentile(99.0));
		Result += FString::Printf(TEXT("frame_time_max_ms=%.3f\n"), FrameSeries.Max);
		Result += FString::Printf(TEXT("hitches_over_33ms=%d\n"), HitchOver33);
		Result += FString::Printf(TEXT("hitches_over_50ms=%d\n"), HitchOver50);
		if (GameSeries.Count > 0)
		{
			Result += FString::Printf(TEXT("game_thread_avg_ms=%.3f\n"), GameSeries.Mean());
			Result += FString::Printf(TEXT("game_thread_p95_ms=%.3f\n"), GameSeries.Percentile(95.0));
		}
		if (RenderSeries.Count > 0)
		{
			Result += FString::Printf(TEXT("render_thread_avg_ms=%.3f\n"), RenderSeries.Mean());
			Result += FString::Printf(TEXT("render_thread_p95_ms=%.3f\n"), RenderSeries.Percentile(95.0));
		}
		if (GpuSeries.Count > 0)
		{
			Result += FString::Printf(TEXT("gpu_avg_ms=%.3f\n"), GpuSeries.Mean());
			Result += FString::Printf(TEXT("gpu_p95_ms=%.3f\n"), GpuSeries.Percentile(95.0));
		}
		if (RhiSeries.Count > 0)
		{
			Result += FString::Printf(TEXT("rhi_thread_avg_ms=%.3f\n"), RhiSeries.Mean());
		}

		const int32 BoundSamples = BoundByGame + BoundByRender + BoundByGpu;
		if (BoundSamples > 0)
		{
			const double Inv = 100.0 / double(BoundSamples);
			Result += FString::Printf(TEXT("bound_by_game_pct=%.1f\n"), double(BoundByGame) * Inv);
			Result += FString::Printf(TEXT("bound_by_render_pct=%.1f\n"), double(BoundByRender) * Inv);
			Result += FString::Printf(TEXT("bound_by_gpu_pct=%.1f\n"), double(BoundByGpu) * Inv);
		}

		Result += TEXT("worst_frames:\n");
		for (int32 i = 0; i < FMath::Min(TopWorst, WorstFrames.Num()); ++i)
		{
			const FFrameBreakdown& Row = WorstFrames[i];
			Result += FString::Printf(
				TEXT("- row=%d\tframe_ms=%.3f\tgame_ms=%.3f\trender_ms=%.3f\tgpu_ms=%.3f\n"),
				Row.FrameIndex,
				Row.FrameMs,
				Row.GameMs,
				Row.RenderMs,
				Row.GpuMs);
		}

		OutResult = MoveTemp(Result);
		return true;
	}

	static FString FormatStatusUnsafe()
	{
		if (!GProfileRuntimeState.bActive)
		{
			return TEXT("PROFILE STATUS\nactive=false\n");
		}

		FString Output;
		Output += TEXT("PROFILE STATUS\n");
		Output += TEXT("active=true\n");
		Output += FString::Printf(TEXT("mode=%s\n"), *GProfileRuntimeState.Mode);
		Output += FString::Printf(TEXT("session=%s\n"), *GProfileRuntimeState.SessionName);
		Output += FString::Printf(TEXT("started_at=%s\n"), *GProfileRuntimeState.StartedAt.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		if (!GProfileRuntimeState.Channels.IsEmpty())
		{
			Output += FString::Printf(TEXT("channels=%s\n"), *GProfileRuntimeState.Channels);
		}
		if (!GProfileRuntimeState.TraceFilePath.IsEmpty())
		{
			Output += FString::Printf(TEXT("trace_file=%s\n"), *GProfileRuntimeState.TraceFilePath);
		}
		if (!GProfileRuntimeState.CsvCaptureName.IsEmpty())
		{
			Output += FString::Printf(TEXT("csv_capture=%s\n"), *GProfileRuntimeState.CsvCaptureName);
		}
		return Output;
	}
}

TSharedPtr<FJsonObject> FProfileRuntimeTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> OperationProp = MakeShared<FJsonObject>();
	OperationProp->SetStringField(TEXT("type"), TEXT("string"));
	OperationProp->SetStringField(TEXT("description"),
		TEXT("Operation: "
			 "'start' begins capture, "
			 "'stop' ends capture after user finishes PIE/playtest, "
			 "'status' returns active session state, "
			 "'analyze' runs UE SummarizeTrace and returns CSV artifacts for agent-side analysis."));
	TArray<TSharedPtr<FJsonValue>> OperationEnum;
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("start")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("stop")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("status")));
	OperationEnum.Add(MakeShared<FJsonValueString>(TEXT("analyze")));
	OperationProp->SetArrayField(TEXT("enum"), OperationEnum);
	Properties->SetObjectField(TEXT("operation"), OperationProp);

	TSharedPtr<FJsonObject> ModeProp = MakeShared<FJsonObject>();
	ModeProp->SetStringField(TEXT("type"), TEXT("string"));
	ModeProp->SetStringField(TEXT("description"), TEXT("Capture mode for start: trace (default), csv, statfile."));
	TArray<TSharedPtr<FJsonValue>> ModeEnum;
	ModeEnum.Add(MakeShared<FJsonValueString>(TEXT("trace")));
	ModeEnum.Add(MakeShared<FJsonValueString>(TEXT("csv")));
	ModeEnum.Add(MakeShared<FJsonValueString>(TEXT("statfile")));
	ModeProp->SetArrayField(TEXT("enum"), ModeEnum);
	Properties->SetObjectField(TEXT("mode"), ModeProp);

	TSharedPtr<FJsonObject> SessionProp = MakeShared<FJsonObject>();
	SessionProp->SetStringField(TEXT("type"), TEXT("string"));
	SessionProp->SetStringField(TEXT("description"), TEXT("Optional session name. Auto-generated when omitted."));
	Properties->SetObjectField(TEXT("session"), SessionProp);

	TSharedPtr<FJsonObject> ChannelsProp = MakeShared<FJsonObject>();
	ChannelsProp->SetStringField(TEXT("type"), TEXT("string"));
	ChannelsProp->SetStringField(TEXT("description"), TEXT("Trace channels for trace mode. Default: cpu,gpu,frame,bookmark,log."));
	Properties->SetObjectField(TEXT("channels"), ChannelsProp);

	TSharedPtr<FJsonObject> TraceFileProp = MakeShared<FJsonObject>();
	TraceFileProp->SetStringField(TEXT("type"), TEXT("string"));
	TraceFileProp->SetStringField(TEXT("description"), TEXT("For start: optional output path for .utrace. For analyze: optional artifact path (.utrace or CSV); if omitted, most recent known capture is used."));
	Properties->SetObjectField(TEXT("trace_file"), TraceFileProp);

	TSharedPtr<FJsonObject> TopProp = MakeShared<FJsonObject>();
	TopProp->SetStringField(TEXT("type"), TEXT("integer"));
	TopProp->SetStringField(TEXT("description"), TEXT("Analyze only: number of top scopes to return from Scopes CSV preview (default 10, max 50)."));
	Properties->SetObjectField(TEXT("top"), TopProp);

	TSharedPtr<FJsonObject> PreviewProp = MakeShared<FJsonObject>();
	PreviewProp->SetStringField(TEXT("type"), TEXT("boolean"));
	PreviewProp->SetStringField(TEXT("description"), TEXT("Analyze only: include parsed top-scope preview from generated Scopes CSV (default true)."));
	Properties->SetObjectField(TEXT("preview"), PreviewProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("operation")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

FToolResult FProfileRuntimeTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// Run on game thread for engine exec command safety.
	if (!IsInGameThread())
	{
		FToolResult GameThreadResult;
		FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool();
		AsyncTask(ENamedThreads::GameThread, [this, &Args, &GameThreadResult, DoneEvent]()
		{
			GameThreadResult = Execute(Args);
			DoneEvent->Trigger();
		});
		DoneEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
		return GameThreadResult;
	}

	FString Operation;
	if (!Args->TryGetStringField(TEXT("operation"), Operation) || Operation.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: operation"));
	}
	Operation = Operation.ToLower();

	if (Operation == TEXT("status"))
	{
		FScopeLock Lock(&GProfileRuntimeStateLock);
		return FToolResult::Ok(FormatStatusUnsafe());
	}

	if (Operation == TEXT("analyze"))
	{
		int32 TopCount = 10;
		Args->TryGetNumberField(TEXT("top"), TopCount);
		TopCount = FMath::Clamp(TopCount, 1, 50);

		bool bPreview = true;
		Args->TryGetBoolField(TEXT("preview"), bPreview);

		FString ArtifactPath;
		Args->TryGetStringField(TEXT("trace_file"), ArtifactPath);
		ArtifactPath = NormalizePathForRead(ArtifactPath);

		{
			FScopeLock Lock(&GProfileRuntimeStateLock);
			if (GProfileRuntimeState.bActive)
			{
				return FToolResult::Fail(TEXT("A profiling session is still active. Call operation='stop' before operation='analyze'."));
			}

			if (ArtifactPath.IsEmpty())
			{
				if (GLastCaptureMode == TEXT("trace") && !GLastTraceArtifactPath.IsEmpty() && FPaths::FileExists(GLastTraceArtifactPath))
				{
					ArtifactPath = GLastTraceArtifactPath;
				}
				else if (GLastCaptureMode == TEXT("csv"))
				{
					if (!GLastCsvArtifactPath.IsEmpty() && FPaths::FileExists(GLastCsvArtifactPath))
					{
						ArtifactPath = GLastCsvArtifactPath;
					}
					else if (!GLastCsvCaptureName.IsEmpty())
					{
						ArtifactPath = FindBestCsvArtifactForCapture(GLastCsvCaptureName);
					}
				}
			}
		}

		if (ArtifactPath.IsEmpty())
		{
			const FString NewestTrace = FindNewestTraceArtifact(FDateTime::MinValue());
			const FString NewestCsv = FindNewestCsvArtifact(FDateTime::MinValue());
			if (!NewestTrace.IsEmpty() && !NewestCsv.IsEmpty())
			{
				const FDateTime TraceStamp = IFileManager::Get().GetTimeStamp(*NewestTrace);
				const FDateTime CsvStamp = IFileManager::Get().GetTimeStamp(*NewestCsv);
				ArtifactPath = (TraceStamp >= CsvStamp) ? NewestTrace : NewestCsv;
			}
			else
			{
				ArtifactPath = !NewestTrace.IsEmpty() ? NewestTrace : NewestCsv;
			}
		}

		if (ArtifactPath.IsEmpty())
		{
			return FToolResult::Fail(TEXT("No profiling artifact found to analyze. Provide trace_file or run start/stop first."));
		}
		if (!FPaths::FileExists(ArtifactPath))
		{
			return FToolResult::Fail(FString::Printf(TEXT("Profiling artifact not found: %s"), *ArtifactPath));
		}

		const bool bIsTrace = ArtifactPath.EndsWith(TEXT(".utrace"), ESearchCase::IgnoreCase);
		if (!bIsTrace)
		{
			if (ArtifactPath.EndsWith(TEXT(".csv.gz"), ESearchCase::IgnoreCase))
			{
				return FToolResult::Fail(FString::Printf(
					TEXT("Compressed CSV profiling artifacts are not supported for direct parsing: %s. Disable CSV compression (csv.CompressionMode=0) and capture again."),
					*ArtifactPath));
			}

			FString CsvResult;
			FString CsvError;
			if (!AnalyzeCsvArtifact(ArtifactPath, TopCount, CsvResult, CsvError))
			{
				return FToolResult::Fail(CsvError);
			}

			{
				FScopeLock Lock(&GProfileRuntimeStateLock);
				GLastCsvArtifactPath = ArtifactPath;
				GLastCaptureMode = TEXT("csv");
			}
			return FToolResult::Ok(CsvResult);
		}

		{
			FScopeLock Lock(&GProfileRuntimeStateLock);
			GLastTraceArtifactPath = ArtifactPath;
			GLastCaptureMode = TEXT("trace");
		}

		const FString EditorCmdPath = BuildEditorCmdExecutablePath();
		if (EditorCmdPath.IsEmpty())
		{
			return FToolResult::Fail(TEXT("Failed to locate UnrealEditor-Cmd/UnrealEditor executable for SummarizeTrace."));
		}

		FString ProjectFilePath = FPaths::GetProjectFilePath();
		ProjectFilePath = NormalizePathForRead(ProjectFilePath);
		if (ProjectFilePath.IsEmpty() || !FPaths::FileExists(ProjectFilePath))
		{
			return FToolResult::Fail(FString::Printf(TEXT("Project file was not found: %s"), *ProjectFilePath));
		}

		const FString CommandLine = FString::Printf(
			TEXT("\"%s\" -run=SummarizeTrace -inputfile=\"%s\" -alltelemetry -skipbaseline -nop4 -nosplash -unattended"),
			*ProjectFilePath,
			*ArtifactPath);

		int32 ReturnCode = -1;
		FString StdOut;
		FString StdErr;
		const bool bExecOk = FPlatformProcess::ExecProcess(*EditorCmdPath, *CommandLine, &ReturnCode, &StdOut, &StdErr);
		if (!bExecOk)
		{
			return FToolResult::Fail(FString::Printf(TEXT("Failed to execute SummarizeTrace process: %s %s"), *EditorCmdPath, *CommandLine));
		}
		if (ReturnCode != 0)
		{
			FString Failure = FString::Printf(TEXT("SummarizeTrace failed (exit_code=%d)\ncommand=%s %s"), ReturnCode, *EditorCmdPath, *CommandLine);
			if (!StdOut.IsEmpty())
			{
				Failure += TEXT("\nstdout:\n");
				Failure += StdOut.Left(4000);
			}
			if (!StdErr.IsEmpty())
			{
				Failure += TEXT("\nstderr:\n");
				Failure += StdErr.Left(4000);
			}
			return FToolResult::Fail(Failure);
		}

		const FTraceSummaryArtifacts Artifacts = BuildSummaryArtifacts(ArtifactPath);
		const bool bScopesExists = FPaths::FileExists(Artifacts.ScopesCsv);
		const bool bCountersExists = FPaths::FileExists(Artifacts.CountersCsv);
		const bool bBookmarksExists = FPaths::FileExists(Artifacts.BookmarksCsv);
		const bool bTelemetryExists = FPaths::FileExists(Artifacts.TelemetryCsv);
		if (!bScopesExists && !bCountersExists && !bBookmarksExists)
		{
			FString Failure = FString::Printf(
				TEXT("SummarizeTrace completed but expected CSV artifacts were not found.\ntrace_file=%s\ncommand=%s %s"),
				*ArtifactPath,
				*EditorCmdPath,
				*CommandLine);
			return FToolResult::Fail(Failure);
		}

		FString Result;
		Result += TEXT("PROFILE ANALYZED\n");
		Result += FString::Printf(TEXT("trace_file=%s\n"), *ArtifactPath);
		Result += TEXT("analysis_mode=summarize_trace_commandlet\n");
		Result += FString::Printf(TEXT("artifact_scopes_csv=%s\n"), *Artifacts.ScopesCsv);
		Result += FString::Printf(TEXT("artifact_scopes_exists=%s\n"), bScopesExists ? TEXT("true") : TEXT("false"));
		Result += FString::Printf(TEXT("artifact_counters_csv=%s\n"), *Artifacts.CountersCsv);
		Result += FString::Printf(TEXT("artifact_counters_exists=%s\n"), bCountersExists ? TEXT("true") : TEXT("false"));
		Result += FString::Printf(TEXT("artifact_bookmarks_csv=%s\n"), *Artifacts.BookmarksCsv);
		Result += FString::Printf(TEXT("artifact_bookmarks_exists=%s\n"), bBookmarksExists ? TEXT("true") : TEXT("false"));
		Result += FString::Printf(TEXT("artifact_telemetry_csv=%s\n"), *Artifacts.TelemetryCsv);
		Result += FString::Printf(TEXT("artifact_telemetry_exists=%s\n"), bTelemetryExists ? TEXT("true") : TEXT("false"));
		Result += TEXT("commands:\n");
		Result += FString::Printf(TEXT("- %s %s\n"), *EditorCmdPath, *CommandLine);

		if (bPreview && bScopesExists)
		{
			TArray<FTopScopeRow> TopScopes;
			FString ParseError;
			if (ReadTopScopesFromCsv(Artifacts.ScopesCsv, TopCount, TopScopes, ParseError))
			{
				Result += TEXT("top_scopes_by_total_duration_seconds:\n");
				for (const FTopScopeRow& Row : TopScopes)
				{
					Result += FString::Printf(
						TEXT("- name=%s\tcount=%llu\ttotal_s=%.6f\tmean_s=%.6f\tmax_s=%.6f\n"),
						*Row.Name,
						Row.Count,
						Row.TotalDurationSeconds,
						Row.MeanDurationSeconds,
						Row.MaxDurationSeconds);
				}
			}
			else
			{
				Result += FString::Printf(TEXT("warning=Failed to parse scopes preview: %s\n"), *ParseError);
			}
		}

		return FToolResult::Ok(Result);
	}

	if (Operation == TEXT("start"))
	{
		FString Mode;
		Args->TryGetStringField(TEXT("mode"), Mode);
		Mode = NormalizeMode(Mode);

		if (!IsSupportedMode(Mode))
		{
			return FToolResult::Fail(FString::Printf(TEXT("Unsupported mode '%s'. Valid: trace, csv, statfile"), *Mode));
		}

		FString SessionName;
		Args->TryGetStringField(TEXT("session"), SessionName);
		SessionName = SanitizeSessionName(SessionName);

		FString TraceChannels = TEXT("cpu,gpu,frame,bookmark,log");
		Args->TryGetStringField(TEXT("channels"), TraceChannels);
		TraceChannels.TrimStartAndEndInline();
		if (TraceChannels.IsEmpty())
		{
			TraceChannels = TEXT("cpu,gpu,frame,bookmark,log");
		}

		{
			FScopeLock Lock(&GProfileRuntimeStateLock);
			if (GProfileRuntimeState.bActive)
			{
				return FToolResult::Fail(FString::Printf(TEXT("A profiling session is already active: %s"), *GProfileRuntimeState.SessionName));
			}
		}

		TArray<FString> ExecutedCommands;
		TArray<FString> CommandOutputs;

		if (Mode == TEXT("trace"))
		{
			FString TraceFilePath;
			Args->TryGetStringField(TEXT("trace_file"), TraceFilePath);
			TraceFilePath.TrimStartAndEndInline();
			if (TraceFilePath.IsEmpty())
			{
				TraceFilePath = BuildDefaultTracePath(SessionName);
			}
			else if (FPaths::IsRelative(TraceFilePath))
			{
				TraceFilePath = FPaths::ConvertRelativePathToFull(UACPSettings::GetWorkingDirectory() / TraceFilePath);
			}

			IFileManager::Get().MakeDirectory(*FPaths::GetPath(TraceFilePath), true);

			const FString Command = FString::Printf(TEXT("Trace.File \"%s\" %s"), *TraceFilePath, *TraceChannels);
			FString Output;
			const bool bExecOk = ExecuteConsoleCommand(Command, Output);
			ExecutedCommands.Add(Command);
			if (!Output.IsEmpty())
			{
				CommandOutputs.Add(Output);
			}

			if (!bExecOk)
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *Command));
			}

			// Some engine states report trace connectivity a moment after command dispatch.
			// Wait briefly, then fall back to Trace.Start if needed.
			FString RuntimeTraceDest;
			bool bConnected = WaitForTraceConnection(1.5, RuntimeTraceDest);
			if (!bConnected)
			{
				const FString FallbackCommand = FString::Printf(TEXT("Trace.Start %s"), *TraceChannels);
				FString FallbackOutput;
				const bool bFallbackExecOk = ExecuteConsoleCommand(FallbackCommand, FallbackOutput);
				ExecutedCommands.Add(FallbackCommand);
				if (!FallbackOutput.IsEmpty())
				{
					CommandOutputs.Add(FallbackOutput);
				}

				if (bFallbackExecOk)
				{
					bConnected = WaitForTraceConnection(1.5, RuntimeTraceDest);
				}

				if (!bConnected)
				{
					FString Failure = FString::Printf(TEXT("Trace start did not connect. Commands attempted:\n- %s\n- %s"), *Command, *FallbackCommand);
					if (!Output.IsEmpty())
					{
						Failure += TEXT("\nTrace.File output:\n");
						Failure += Output;
					}
					if (!FallbackOutput.IsEmpty())
					{
						Failure += TEXT("\nTrace.Start output:\n");
						Failure += FallbackOutput;
					}
					return FToolResult::Fail(Failure);
				}
			}

			// Use the engine-reported destination to avoid path mismatches.
			if (!RuntimeTraceDest.IsEmpty())
			{
				TraceFilePath = RuntimeTraceDest;
			}

			FScopeLock Lock(&GProfileRuntimeStateLock);
			GProfileRuntimeState = FProfileRuntimeSessionState();
			GProfileRuntimeState.bActive = true;
			GProfileRuntimeState.Mode = Mode;
			GProfileRuntimeState.SessionName = SessionName;
			GProfileRuntimeState.Channels = TraceChannels;
			GProfileRuntimeState.TraceFilePath = TraceFilePath;
			GProfileRuntimeState.StartedAt = FDateTime::Now();
			GProfileRuntimeState.StartCommands = ExecutedCommands;
		}
		else if (Mode == TEXT("csv"))
		{
			const FString CsvCaptureName = SessionName;
			const FString CmdSetName = FString::Printf(TEXT("CsvProfile StartFile=%s"), *CsvCaptureName);
			const FString CmdStart = TEXT("CsvProfile Start");

			FString OutputSetName;
			if (!ExecuteConsoleCommand(CmdSetName, OutputSetName))
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *CmdSetName));
			}
			ExecutedCommands.Add(CmdSetName);
			if (!OutputSetName.IsEmpty())
			{
				CommandOutputs.Add(OutputSetName);
			}

			FString OutputStart;
			if (!ExecuteConsoleCommand(CmdStart, OutputStart))
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *CmdStart));
			}
			ExecutedCommands.Add(CmdStart);
			if (!OutputStart.IsEmpty())
			{
				CommandOutputs.Add(OutputStart);
			}

			FScopeLock Lock(&GProfileRuntimeStateLock);
			GProfileRuntimeState = FProfileRuntimeSessionState();
			GProfileRuntimeState.bActive = true;
			GProfileRuntimeState.Mode = Mode;
			GProfileRuntimeState.SessionName = SessionName;
			GProfileRuntimeState.CsvCaptureName = CsvCaptureName;
			GProfileRuntimeState.StartedAt = FDateTime::Now();
			GProfileRuntimeState.StartCommands = ExecutedCommands;
		}
		else if (Mode == TEXT("statfile"))
		{
			const FString CmdStart = TEXT("stat startfile");
			FString OutputStart;
			if (!ExecuteConsoleCommand(CmdStart, OutputStart))
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *CmdStart));
			}
			ExecutedCommands.Add(CmdStart);
			if (!OutputStart.IsEmpty())
			{
				CommandOutputs.Add(OutputStart);
			}

			FScopeLock Lock(&GProfileRuntimeStateLock);
			GProfileRuntimeState = FProfileRuntimeSessionState();
			GProfileRuntimeState.bActive = true;
			GProfileRuntimeState.Mode = Mode;
			GProfileRuntimeState.SessionName = SessionName;
			GProfileRuntimeState.StartedAt = FDateTime::Now();
			GProfileRuntimeState.StartCommands = ExecutedCommands;
		}

		FString Result;
		Result += TEXT("PROFILE STARTED\n");
		Result += FString::Printf(TEXT("session=%s\n"), *SessionName);
		Result += FString::Printf(TEXT("mode=%s\n"), *Mode);
		if (Mode == TEXT("trace"))
		{
			Result += FString::Printf(TEXT("channels=%s\n"), *TraceChannels);
			FScopeLock Lock(&GProfileRuntimeStateLock);
			Result += FString::Printf(TEXT("trace_file=%s\n"), *GProfileRuntimeState.TraceFilePath);
		}
		if (Mode == TEXT("csv"))
		{
			Result += FString::Printf(TEXT("csv_capture=%s\n"), *SessionName);
		}
		Result += TEXT("commands:\n");
		for (const FString& Command : ExecutedCommands)
		{
			Result += FString::Printf(TEXT("- %s\n"), *Command);
		}
		if (CommandOutputs.Num() > 0)
		{
			Result += TEXT("command_output:\n");
			for (const FString& Output : CommandOutputs)
			{
				if (!Output.IsEmpty())
				{
					Result += Output + TEXT("\n");
				}
			}
		}
		return FToolResult::Ok(Result);
	}

	if (Operation == TEXT("stop"))
	{
		FProfileRuntimeSessionState SessionCopy;
		{
			FScopeLock Lock(&GProfileRuntimeStateLock);
			if (!GProfileRuntimeState.bActive)
			{
				return FToolResult::Fail(TEXT("No active profiling session."));
			}
			SessionCopy = GProfileRuntimeState;
		}

		TArray<FString> StopCommands;
		TArray<FString> CommandOutputs;

		if (SessionCopy.Mode == TEXT("trace"))
		{
			const FString CmdStop = TEXT("Trace.Stop");
			FString OutputStop;
			const bool bOk = ExecuteConsoleCommand(CmdStop, OutputStop);
			StopCommands.Add(CmdStop);
			if (!OutputStop.IsEmpty())
			{
				CommandOutputs.Add(OutputStop);
			}
			if (!bOk)
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *CmdStop));
			}
		}
		else if (SessionCopy.Mode == TEXT("csv"))
		{
			const FString CmdStop = TEXT("CsvProfile Stop");
			FString OutputStop;
			const bool bOk = ExecuteConsoleCommand(CmdStop, OutputStop);
			StopCommands.Add(CmdStop);
			if (!OutputStop.IsEmpty())
			{
				CommandOutputs.Add(OutputStop);
			}
			if (!bOk)
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *CmdStop));
			}
		}
			else if (SessionCopy.Mode == TEXT("statfile"))
			{
			const FString CmdStop = TEXT("stat stopfile");
			FString OutputStop;
			const bool bOk = ExecuteConsoleCommand(CmdStop, OutputStop);
			StopCommands.Add(CmdStop);
			if (!OutputStop.IsEmpty())
			{
				CommandOutputs.Add(OutputStop);
			}
			if (!bOk)
			{
				return FToolResult::Fail(FString::Printf(TEXT("Failed to execute command: %s"), *CmdStop));
			}
		}

			{
				FScopeLock Lock(&GProfileRuntimeStateLock);
				GProfileRuntimeState = FProfileRuntimeSessionState();
			}

			bool bTraceArtifactExists = true;
			if (SessionCopy.Mode == TEXT("trace") && !SessionCopy.TraceFilePath.IsEmpty())
			{
				bTraceArtifactExists = FPaths::FileExists(SessionCopy.TraceFilePath);
				if (!bTraceArtifactExists)
				{
					const FString FallbackTrace = FindNewestTraceArtifact(SessionCopy.StartedAt - FTimespan::FromSeconds(5));
					if (!FallbackTrace.IsEmpty())
					{
						SessionCopy.TraceFilePath = FallbackTrace;
						bTraceArtifactExists = true;
					}
				}
			}

			if (SessionCopy.Mode == TEXT("trace"))
			{
				FScopeLock Lock(&GProfileRuntimeStateLock);
				GLastTraceArtifactPath = bTraceArtifactExists ? SessionCopy.TraceFilePath : TEXT("");
			}

			FString Result;
			Result += TEXT("PROFILE STOPPED\n");
			Result += FString::Printf(TEXT("session=%s\n"), *SessionCopy.SessionName);
			Result += FString::Printf(TEXT("mode=%s\n"), *SessionCopy.Mode);
			if (!SessionCopy.TraceFilePath.IsEmpty())
			{
				Result += FString::Printf(TEXT("artifact_trace=%s\n"), *SessionCopy.TraceFilePath);
				Result += FString::Printf(TEXT("artifact_trace_exists=%s\n"), bTraceArtifactExists ? TEXT("true") : TEXT("false"));
				if (!bTraceArtifactExists)
				{
					Result += TEXT("warning=Trace stopped but no .utrace artifact was found on disk.\n");
				}
			}
		if (!SessionCopy.CsvCaptureName.IsEmpty())
		{
			const FString CsvDir = FPaths::ProfilingDir() / TEXT("CSV");
			Result += FString::Printf(TEXT("artifact_csv_guess=%s/%s.csv\n"), *CsvDir, *SessionCopy.CsvCaptureName);
			Result += FString::Printf(TEXT("artifact_csv_guess_bin=%s/%s.csv.bin\n"), *CsvDir, *SessionCopy.CsvCaptureName);
		}
		Result += TEXT("commands:\n");
		for (const FString& Command : StopCommands)
		{
			Result += FString::Printf(TEXT("- %s\n"), *Command);
		}
		if (CommandOutputs.Num() > 0)
		{
			Result += TEXT("command_output:\n");
			for (const FString& Output : CommandOutputs)
			{
				if (!Output.IsEmpty())
				{
					Result += Output + TEXT("\n");
				}
			}
		}

		return FToolResult::Ok(Result);
	}

	return FToolResult::Fail(FString::Printf(TEXT("Unknown operation: %s. Valid: start, stop, status, analyze"), *Operation));
}
