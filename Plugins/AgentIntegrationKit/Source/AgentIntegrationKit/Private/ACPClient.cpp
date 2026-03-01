// Copyright 2025 Betide Studio. All Rights Reserved.

#include "ACPClient.h"
#include "AgentIntegrationKitModule.h"
#include "ACPSettings.h"
#include "AgentInstaller.h"
#include "MCPServer.h"
#include "ACPAttachmentManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Misc/Guid.h"

static void AddPathSegmentIfValid(const FString& Segment, TArray<FString>& InOutSegments)
{
	if (Segment.IsEmpty())
	{
		return;
	}

	FString Normalized = Segment;
	FPaths::NormalizeDirectoryName(Normalized);
	if (Normalized.IsEmpty())
	{
		return;
	}

	for (const FString& Existing : InOutSegments)
	{
#if PLATFORM_WINDOWS
		if (Existing.Equals(Normalized, ESearchCase::IgnoreCase))
#else
		if (Existing.Equals(Normalized, ESearchCase::CaseSensitive))
#endif
		{
			return;
		}
	}

	InOutSegments.Add(Normalized);
}

static FString BuildAugmentedPathForChildProcess(const FString& BasePath, const FString& ExecutablePath)
{
	const FString PathSeparator =
#if PLATFORM_WINDOWS
		TEXT(";");
#else
		TEXT(":");
#endif

	TArray<FString> Segments;
	BasePath.ParseIntoArray(Segments, *PathSeparator, true);

	// Ensure the resolved executable directory is in PATH. This is critical for npm-installed
	// wrappers that execute `env node` (Copilot/Gemini) when Unreal launches with a limited PATH.
	if (!ExecutablePath.IsEmpty() && FPaths::IsRelative(ExecutablePath) == false)
	{
		AddPathSegmentIfValid(FPaths::GetPath(ExecutablePath), Segments);
	}

#if !PLATFORM_WINDOWS
	AddPathSegmentIfValid(TEXT("/opt/homebrew/bin"), Segments);
	AddPathSegmentIfValid(TEXT("/usr/local/bin"), Segments);

	const FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!HomeDir.IsEmpty())
	{
		AddPathSegmentIfValid(FPaths::Combine(HomeDir, TEXT("bin")), Segments);
		AddPathSegmentIfValid(FPaths::Combine(HomeDir, TEXT(".bun/bin")), Segments);
		AddPathSegmentIfValid(FPaths::Combine(HomeDir, TEXT(".local/bin")), Segments);
		AddPathSegmentIfValid(FPaths::Combine(HomeDir, TEXT(".opencode/bin")), Segments);
	}
#endif

	FString Result;
	for (const FString& Segment : Segments)
	{
		if (Segment.IsEmpty())
		{
			continue;
		}

		if (!Result.IsEmpty())
		{
			Result += PathSeparator;
		}
		Result += Segment;
	}

	return Result;
}

struct FEnvVarRestore
{
	FString Name;
	FString PreviousValue;
};

static void ApplyTemporaryEnvVar(
	const FString& Name,
	const FString& Value,
	TArray<FEnvVarRestore>& OutRestoreList)
{
	FEnvVarRestore Backup;
	Backup.Name = Name;
	Backup.PreviousValue = FPlatformMisc::GetEnvironmentVariable(*Name);
	OutRestoreList.Add(Backup);
	FPlatformMisc::SetEnvironmentVar(*Name, *Value);
}

static void RestoreTemporaryEnvVars(const TArray<FEnvVarRestore>& RestoreList)
{
	for (int32 Index = RestoreList.Num() - 1; Index >= 0; --Index)
	{
		const FEnvVarRestore& Backup = RestoreList[Index];
		FPlatformMisc::SetEnvironmentVar(*Backup.Name, *Backup.PreviousValue);
	}
}

static FString BuildCopilotAdditionalMcpConfigFile(const int32 MCPPort)
{
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("aik-copilot-mcp"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	const FString FilePath = FPaths::Combine(
		TempDir,
		FString::Printf(TEXT("mcp-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> McpServers = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> UnrealEntry = MakeShared<FJsonObject>();

	UnrealEntry->SetStringField(TEXT("type"), TEXT("http"));
	UnrealEntry->SetStringField(TEXT("url"), FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), MCPPort));
	TArray<TSharedPtr<FJsonValue>> AllTools;
	AllTools.Add(MakeShared<FJsonValueString>(TEXT("*")));
	UnrealEntry->SetArrayField(TEXT("tools"), AllTools);

	McpServers->SetObjectField(TEXT("unreal-editor"), UnrealEntry);
	RootObj->SetObjectField(TEXT("mcpServers"), McpServers);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		return FilePath;
	}

	return FString();
}

static FString BuildGeminiSystemSettingsFile(const int32 MCPPort)
{
	const FString TempDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("aik-gemini-mcp"));
	IFileManager::Get().MakeDirectory(*TempDir, true);

	const FString FilePath = FPaths::Combine(
		TempDir,
		FString::Printf(TEXT("settings-%s.json"), *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

	TSharedPtr<FJsonObject> RootObj = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> McpServers = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> UnrealEntry = MakeShared<FJsonObject>();

	// Gemini supports httpUrl for streamable HTTP MCP servers.
	UnrealEntry->SetStringField(TEXT("httpUrl"), FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), MCPPort));
	McpServers->SetObjectField(TEXT("unreal-editor"), UnrealEntry);
	RootObj->SetObjectField(TEXT("mcpServers"), McpServers);

	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(RootObj.ToSharedRef(), Writer);

	if (FFileHelper::SaveStringToFile(OutputString, *FilePath))
	{
		return FilePath;
	}

	return FString();
}

static FCriticalSection GAgentProcessLaunchEnvLock;

// Helper to strip ANSI escape sequences from text (terminal color codes, etc.)
static FString StripAnsiCodes(const FString& Input)
{
	FString Result;
	Result.Reserve(Input.Len());

	bool bInEscape = false;
	for (int32 i = 0; i < Input.Len(); ++i)
	{
		TCHAR C = Input[i];

		if (C == 0x1B) // ESC character
		{
			bInEscape = true;
			continue;
		}

		if (bInEscape)
		{
			// ANSI escape sequences end with a letter (a-zA-Z)
			if ((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z'))
			{
				bInEscape = false;
			}
			continue;
		}

		Result.AppendChar(C);
	}

	return Result;
}

// Legacy fallback: check old global npm/bun install locations for users who installed adapters
// via npm before the bundled adapter was available. This helps migration — once they use the
// Install button, the bundled adapter takes over and this path won't be hit anymore.
static FString FindLegacyAdapterPath(const FString& AgentName, const FAgentInstallInfo& InstallInfo)
{
	if (InstallInfo.AdapterEntryPointFile.IsEmpty())
	{
		return FString();
	}

	// Map agent name to legacy npm package path
	FString NpmPackagePath;
	FString EntryPointFile = InstallInfo.AdapterEntryPointFile;

	if (AgentName == TEXT("Claude Code"))
	{
		NpmPackagePath = TEXT("@zed-industries/claude-code-acp");
	}
	else if (AgentName == TEXT("Gemini CLI"))
	{
		NpmPackagePath = TEXT("@google/gemini-cli");
	}
	else if (AgentName == TEXT("Cursor Agent"))
	{
		NpmPackagePath = TEXT("@blowmage/cursor-agent-acp");
	}
	else
	{
		return FString();
	}

	TArray<FString> PossiblePaths;

#if PLATFORM_WINDOWS
	FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(UserProfile, TEXT(".bun/install/global/node_modules"), NpmPackagePath, EntryPointFile));
	}
	FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (!AppData.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(AppData, TEXT("npm/node_modules"), NpmPackagePath, EntryPointFile));
	}
#else
	FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!HomeDir.IsEmpty())
	{
		PossiblePaths.Add(FPaths::Combine(HomeDir, TEXT(".bun/install/global/node_modules"), NpmPackagePath, EntryPointFile));
	}
	PossiblePaths.Add(FPaths::Combine(TEXT("/opt/homebrew/lib/node_modules"), NpmPackagePath, EntryPointFile));
	PossiblePaths.Add(FPaths::Combine(TEXT("/usr/local/lib/node_modules"), NpmPackagePath, EntryPointFile));
#endif

	for (const FString& Path : PossiblePaths)
	{
		FString NormalizedPath = Path;
		FPaths::NormalizeFilename(NormalizedPath);
		if (IFileManager::Get().FileExists(*NormalizedPath))
		{
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Found legacy adapter at %s — consider using the Install button for the bundled version"), *NormalizedPath);
			return NormalizedPath;
		}
	}

	return FString();
}

FACPClient::FACPClient()
	: bStopRequested(false)
{
	// Set default client capabilities
	ClientCapabilities.bSupportsFileSystem = true;
	ClientCapabilities.bSupportsTerminal = false;
	ClientCapabilities.bSupportsAudio = false;
	ClientCapabilities.bSupportsImage = true;  // Enable image support for context attachments
}

FACPClient::~FACPClient()
{
	Disconnect();
}

bool FACPClient::Connect(const FACPAgentConfig& Config)
{
	if (IsConnected())
	{
		Disconnect();
	}

	// Clean up any stale per-process Copilot config file from a previous run.
	if (!CopilotAdditionalMcpConfigPath.IsEmpty())
	{
		IFileManager::Get().Delete(*CopilotAdditionalMcpConfigPath, false, true);
		CopilotAdditionalMcpConfigPath.Empty();
	}
	if (!GeminiSystemSettingsPath.IsEmpty())
	{
		IFileManager::Get().Delete(*GeminiSystemSettingsPath, false, true);
		GeminiSystemSettingsPath.Empty();
	}
	if (!GeminiSystemSettingsPath.IsEmpty())
	{
		IFileManager::Get().Delete(*GeminiSystemSettingsPath, false, true);
		GeminiSystemSettingsPath.Empty();
	}

	CurrentConfig = Config;
	AvailableReasoningEfforts.Empty();
	CurrentReasoningEffort.Empty();
	ReasoningConfigOptionId.Empty();
	SetState(EACPClientState::Connecting, TEXT("Connecting to agent..."));

	// Build command line
	FString ExecutablePath = Config.ExecutablePath;
	FString Arguments;
	for (const FString& Arg : Config.Arguments)
	{
		Arguments += TEXT(" ") + Arg;
	}

	// Resolve executable and adapter entry point for the agent.
	// For agents with JS adapters, use the bundled Bun runtime + adapter.
	// For native binary agents (OpenCode), use the executable directly.
	// User path overrides from settings always take priority.
	{
		FAgentInstallInfo InstallInfo = FAgentInstaller::GetAgentInstallInfo(Config.AgentName);
		bool bHasJSAdapter = InstallInfo.RequiresAdapter() && !InstallInfo.bAdapterIsNativeBinary;

		if (bHasJSAdapter)
		{
			// JS adapter agent — run via bundled Bun
			UACPSettings* Settings = UACPSettings::Get();

			// Check if user has overridden the executable path (power user: custom ACP adapter)
			bool bUserOverride = !Config.ExecutablePath.IsEmpty()
				&& IFileManager::Get().FileExists(*Config.ExecutablePath);

			if (bUserOverride)
			{
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Using user-specified path for %s: %s"), *Config.AgentName, *ExecutablePath);
				// ExecutablePath already set from Config, leave as-is
			}
			else
			{
				// Resolve Bun runtime: check settings override first, then bundled
				FString BunPath;
				if (Settings && !Settings->BunOverridePath.FilePath.IsEmpty())
				{
					if (IFileManager::Get().FileExists(*Settings->BunOverridePath.FilePath))
					{
						BunPath = Settings->BunOverridePath.FilePath;
						UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Using Bun override path: %s"), *BunPath);
					}
					else
					{
						UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Bun override path not found: %s, falling back to bundled"), *Settings->BunOverridePath.FilePath);
					}
				}
				if (BunPath.IsEmpty())
				{
					BunPath = FAgentInstaller::GetBundledBunPath();
				}

				if (BunPath.IsEmpty())
				{
					UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Bundled Bun runtime not found. Plugin installation may be incomplete."));
					SetState(EACPClientState::Error, TEXT("Bundled Bun runtime not found. Try reinstalling Agent Integration Kit."));
					return false;
				}

				FAgentInstaller::EnsureBunExecutable();

				// Resolve adapter entry point: bundled/managed first, then legacy paths
				FString ScriptPath = FAgentInstaller::GetAdapterEntryPoint(InstallInfo);

				// Legacy fallback: check old global npm/bun install locations for migration
				if (ScriptPath.IsEmpty())
				{
					ScriptPath = FindLegacyAdapterPath(Config.AgentName, InstallInfo);
				}

				if (ScriptPath.IsEmpty())
				{
					UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Adapter entry point not found for %s. Use the Install button to set up the adapter."), *Config.AgentName);
					SetState(EACPClientState::Error, FString::Printf(
						TEXT("%s adapter not found. Click the Install button in the agent list to set it up."), *Config.AgentName));
					return false;
				}

				// Rewrite command: bun run <script> [original args]
				Arguments = FString::Printf(TEXT("run \"%s\"%s"), *ScriptPath, *Arguments);
				ExecutablePath = BunPath;
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Launching via bundled Bun: %s %s"), *ExecutablePath, *Arguments);
			}
		}
		else if (InstallInfo.bAdapterIsNativeBinary && InstallInfo.RequiresAdapter())
		{
			// Native binary adapter (e.g. codex-acp) — spawn directly
			FString BinaryPath = FAgentInstaller::GetAdapterEntryPoint(InstallInfo);
			if (!BinaryPath.IsEmpty())
			{
				if (!FAgentInstaller::EnsureNativeAdapterExecutable(BinaryPath))
				{
					UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Native adapter exists but is not executable for %s: %s"), *Config.AgentName, *BinaryPath);
					SetState(EACPClientState::Error, FString::Printf(
						TEXT("%s adapter exists but is not executable. Check file permissions and try again."),
						*Config.AgentName));
					return false;
				}

				ExecutablePath = BinaryPath;
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Launching native adapter: %s %s"), *ExecutablePath, *Arguments);
			}
			else
			{
				UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Native adapter binary not found for %s. Use the Install button."), *Config.AgentName);
				SetState(EACPClientState::Error, FString::Printf(
					TEXT("%s adapter binary not found. Click the Install button to set it up."), *Config.AgentName));
				return false;
			}
		}
		// else: native agent (OpenCode, etc.) — ExecutablePath already set from Config
	}

	// Set working directory
	FString WorkingDir = Config.WorkingDirectory;
	if (WorkingDir.IsEmpty())
	{
		WorkingDir = UACPSettings::GetWorkingDirectory();
	}

	FString CursorAgentExecutableForChild;
#if PLATFORM_WINDOWS
	// On Windows, cursor-acp needs CURSOR_AGENT_EXECUTABLE to locate cursor-agent.
	if (Config.AgentName == TEXT("Cursor Agent"))
	{
		FString StdOut, StdErr;
		int32 ReturnCode = -1;
		FPlatformProcess::ExecProcess(TEXT("where"), TEXT("cursor-agent"), &ReturnCode, &StdOut, &StdErr);
		if (ReturnCode == 0 && !StdOut.IsEmpty())
		{
			int32 NewlineIdx;
			CursorAgentExecutableForChild = StdOut;
			if (CursorAgentExecutableForChild.FindChar(TEXT('\n'), NewlineIdx))
			{
				CursorAgentExecutableForChild = CursorAgentExecutableForChild.Left(NewlineIdx);
			}
			CursorAgentExecutableForChild.TrimStartAndEndInline();

			if (!IFileManager::Get().FileExists(*CursorAgentExecutableForChild))
			{
				CursorAgentExecutableForChild.Empty();
			}
		}
	}
#endif

	// For Copilot CLI, avoid mutating ~/.copilot/mcp-config.json.
	// Instead, pass a per-process additional MCP config file via CLI argument.
	if (Config.AgentName == TEXT("Copilot CLI") && FMCPServer::Get().IsRunning())
	{
		const int32 MCPPort = FMCPServer::Get().GetPort();
		CopilotAdditionalMcpConfigPath = BuildCopilotAdditionalMcpConfigFile(MCPPort);

		if (!CopilotAdditionalMcpConfigPath.IsEmpty())
		{
			const FString AdditionalConfigArg = TEXT("@") + CopilotAdditionalMcpConfigPath;
			Arguments += FString::Printf(TEXT(" --additional-mcp-config \"%s\""), *AdditionalConfigArg);
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Using per-process Copilot MCP config: %s"), *CopilotAdditionalMcpConfigPath);
		}
		else
		{
			UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Failed to create per-process Copilot MCP config file"));
		}
	}

	// For Gemini CLI, inject MCP server via a per-process system settings file
	// instead of requiring mutation of ~/.gemini/settings.json.
	if (Config.AgentName == TEXT("Gemini CLI") && FMCPServer::Get().IsRunning())
	{
		const int32 MCPPort = FMCPServer::Get().GetPort();
		GeminiSystemSettingsPath = BuildGeminiSystemSettingsFile(MCPPort);
		if (!GeminiSystemSettingsPath.IsEmpty())
		{
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Using per-process Gemini settings file: %s"), *GeminiSystemSettingsPath);
		}
		else
		{
			UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Failed to create per-process Gemini settings file"));
		}
	}

	// Create pipes for stdin/stdout
	// bWritePipeLocal parameter: true = ReadPipe is inheritable, false = WritePipe is inheritable
	void* StdinReadPipe = nullptr;
	void* StdoutWritePipe = nullptr;

	// stdin: child reads (ReadPipe inheritable), parent writes (WritePipe local)
	if (!FPlatformProcess::CreatePipe(StdinReadPipe, StdinWritePipe, true))
	{
		SetState(EACPClientState::Error, TEXT("Failed to create stdin pipe"));
		return false;
	}

	// stdout: child writes (WritePipe inheritable), parent reads (ReadPipe local)
	if (!FPlatformProcess::CreatePipe(StdoutReadPipe, StdoutWritePipe, false))
	{
		FPlatformProcess::ClosePipe(StdinReadPipe, StdinWritePipe);
		SetState(EACPClientState::Error, TEXT("Failed to create stdout pipe"));
		return false;
	}

	// Spawn the process.
	// Environment variables in UE are process-global, so serialize launch-time overrides.
	{
		FScopeLock LaunchLock(&GAgentProcessLaunchEnvLock);
		TArray<FEnvVarRestore> RestoreList;

		const FString OriginalPathEnv = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
		const FString AugmentedPathEnv = BuildAugmentedPathForChildProcess(OriginalPathEnv, ExecutablePath);
		const bool bUpdatedPathForChild = !AugmentedPathEnv.IsEmpty() && AugmentedPathEnv != OriginalPathEnv;
		if (bUpdatedPathForChild)
		{
			ApplyTemporaryEnvVar(TEXT("PATH"), AugmentedPathEnv, RestoreList);
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Temporarily augmented PATH for child process launch"));
		}

		if (!CursorAgentExecutableForChild.IsEmpty())
		{
			ApplyTemporaryEnvVar(TEXT("CURSOR_AGENT_EXECUTABLE"), CursorAgentExecutableForChild, RestoreList);
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Set CURSOR_AGENT_EXECUTABLE=%s"), *CursorAgentExecutableForChild);
		}

		if (!GeminiSystemSettingsPath.IsEmpty())
		{
			ApplyTemporaryEnvVar(TEXT("GEMINI_CLI_SYSTEM_SETTINGS_PATH"), GeminiSystemSettingsPath, RestoreList);
		}

		for (const TPair<FString, FString>& Pair : Config.EnvironmentVariables)
		{
			if (!Pair.Key.IsEmpty())
			{
				ApplyTemporaryEnvVar(Pair.Key, Pair.Value, RestoreList);
			}
		}

		ProcessHandle = FPlatformProcess::CreateProc(
			*ExecutablePath,
			*Arguments,
			false,  // bLaunchDetached
			true,   // bLaunchHidden
			true,   // bLaunchReallyHidden
			nullptr, // OutProcessID
			0,      // PriorityModifier
			*WorkingDir,
			StdoutWritePipe, // PipeWriteChild (agent's stdout)
			StdinReadPipe    // PipeReadChild (agent's stdin)
		);

		RestoreTemporaryEnvVars(RestoreList);
	}

	// Close the child-side pipe handles (we keep the parent-side)
	FPlatformProcess::ClosePipe(StdinReadPipe, nullptr);
	FPlatformProcess::ClosePipe(nullptr, StdoutWritePipe);

	if (!ProcessHandle.IsValid())
	{
		FPlatformProcess::ClosePipe(nullptr, StdinWritePipe);
		FPlatformProcess::ClosePipe(StdoutReadPipe, nullptr);
		StdinWritePipe = nullptr;
		StdoutReadPipe = nullptr;
		SetState(EACPClientState::Error, FString::Printf(TEXT("Failed to start agent: %s"), *ExecutablePath));
		return false;
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Process started successfully: %s %s"), *ExecutablePath, *Arguments);
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Working directory: %s"), *WorkingDir);

	// Start the reading thread
	bStopRequested = false;
	ReadThread = FRunnableThread::Create(this, TEXT("ACPClientReader"));

	if (!ReadThread)
	{
		Disconnect();
		SetState(EACPClientState::Error, TEXT("Failed to create reader thread"));
		return false;
	}

	// Give the process a moment to initialize (especially important on Windows with .cmd wrappers)
	FPlatformProcess::Sleep(0.1f);

	// Verify process is still running
	if (!FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Process exited immediately after start"));
		Disconnect();
		SetState(EACPClientState::Error, TEXT("Agent process exited immediately"));
		return false;
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Process verified running, sending initialize..."));

	// Connection established, now initialize
	SetState(EACPClientState::Initializing, TEXT("Initializing ACP session..."));
	Initialize();

	return true;
}

void FACPClient::Disconnect()
{
	// Guard against re-entry: if already disconnected (e.g., destructor calling Disconnect
	// while we're already inside a Disconnect → SetState → OnStateChanged callback chain),
	// skip to avoid infinite recursion / stack overflow.
	if (GetState() == EACPClientState::Disconnected)
	{
		return;
	}

	bStopRequested = true;

	// Wait for read thread to finish
	if (ReadThread)
	{
		ReadThread->WaitForCompletion();
		delete ReadThread;
		ReadThread = nullptr;
	}

	// Close pipes
	{
		FScopeLock Lock(&WriteLock);
		if (StdinWritePipe)
		{
			FPlatformProcess::ClosePipe(nullptr, StdinWritePipe);
			StdinWritePipe = nullptr;
		}
	}

	if (StdoutReadPipe)
	{
		FPlatformProcess::ClosePipe(StdoutReadPipe, nullptr);
		StdoutReadPipe = nullptr;
	}

	// Terminate process
	if (ProcessHandle.IsValid())
	{
		FPlatformProcess::TerminateProc(ProcessHandle);
		FPlatformProcess::CloseProc(ProcessHandle);
		ProcessHandle.Reset();
	}

	if (!CopilotAdditionalMcpConfigPath.IsEmpty())
	{
		IFileManager::Get().Delete(*CopilotAdditionalMcpConfigPath, false, true);
		CopilotAdditionalMcpConfigPath.Empty();
	}

	// Clear state
	{
		FScopeLock Lock(&StateLock);
		PendingRequests.Empty();
		CurrentSessionId.Empty();
		ReadBuffer.Empty();
	}

	SetState(EACPClientState::Disconnected, TEXT("Disconnected"));
}

bool FACPClient::Init()
{
	return true;
}

uint32 FACPClient::Run()
{
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Reader thread started"));

	// Capture a weak reference for AsyncTask lambdas — prevents use-after-free
	// if the client is destroyed before the game thread processes the task.
	TWeakPtr<FACPClient> WeakSelf = AsShared();

	int32 EmptyReadCount = 0;
	double LastLogTime = FPlatformTime::Seconds();

	while (!bStopRequested)
	{
		if (!StdoutReadPipe)
		{
			UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: StdoutReadPipe is null, exiting reader thread"));
			break;
		}

		// Read from stdout
		FString Output = FPlatformProcess::ReadPipe(StdoutReadPipe);
		if (!Output.IsEmpty())
		{
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Read %d chars from pipe: [%s]"), Output.Len(), *Output.Left(200));
			ReadBuffer += Output;
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Buffer now has %d chars"), ReadBuffer.Len());

			// Process complete lines (newline-delimited JSON)
			// Handle both Unix (\n) and Windows (\r\n) line endings
			int32 NewlineIndex;
			while (ReadBuffer.FindChar(TEXT('\n'), NewlineIndex))
			{
				FString Line = ReadBuffer.Left(NewlineIndex);
				ReadBuffer = ReadBuffer.Mid(NewlineIndex + 1);

				// Remove any trailing \r (from CRLF on Windows)
				Line.TrimStartAndEndInline();
				if (!Line.IsEmpty())
				{
					UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Found complete line (%d chars)"), Line.Len());
					// Process on game thread — use weak ptr to guard against client destruction
					AsyncTask(ENamedThreads::GameThread, [WeakSelf, Line]()
					{
						if (TSharedPtr<FACPClient> Self = WeakSelf.Pin())
						{
							Self->ProcessLine(Line);
						}
					});
				}
			}
		}
		else
		{
			EmptyReadCount++;

			// Check if process is still running
			if (!FPlatformProcess::IsProcRunning(ProcessHandle))
			{
				UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Agent process terminated unexpectedly"));
				AsyncTask(ENamedThreads::GameThread, [WeakSelf]()
				{
					if (TSharedPtr<FACPClient> Self = WeakSelf.Pin())
					{
						Self->SetState(EACPClientState::Error, TEXT("Agent process terminated unexpectedly"));
					}
				});
				break;
			}

			// Log periodically (every 5 seconds) to show reader thread is alive
			double CurrentTime = FPlatformTime::Seconds();
			if (CurrentTime - LastLogTime >= 5.0)
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Reader thread alive, waiting for data... (empty reads: %d, buffer: %d chars)"),
					EmptyReadCount, ReadBuffer.Len());
				LastLogTime = CurrentTime;
				EmptyReadCount = 0;
			}

			// Small sleep to avoid busy waiting
			FPlatformProcess::Sleep(0.01f);
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Reader thread exiting"));
	return 0;
}

void FACPClient::Stop()
{
	bStopRequested = true;
}

void FACPClient::Exit()
{
}

int32 FACPClient::SendRequest(const FString& Method, TSharedPtr<FJsonObject> Params)
{
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Preparing request for method: %s"), *Method);

	int32 RequestId;
	{
		FScopeLock Lock(&StateLock);
		RequestId = NextRequestId++;
		PendingRequests.Add(RequestId, Method);
	}

	TSharedRef<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Request->SetNumberField(TEXT("id"), RequestId);
	Request->SetStringField(TEXT("method"), Method);

	if (Params.IsValid())
	{
		Request->SetObjectField(TEXT("params"), Params);
	}

	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(Request, Writer);
	Writer->Close();

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Sending request ID %d for method: %s"), RequestId, *Method);
	SendRawMessage(JsonString);

	return RequestId;
}

void FACPClient::SendNotification(const FString& Method, TSharedPtr<FJsonObject> Params)
{
	TSharedRef<FJsonObject> Notification = MakeShared<FJsonObject>();
	Notification->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Notification->SetStringField(TEXT("method"), Method);

	if (Params.IsValid())
	{
		Notification->SetObjectField(TEXT("params"), Params);
	}

	FString JsonString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
	FJsonSerializer::Serialize(Notification, Writer);
	Writer->Close();

	SendRawMessage(JsonString);
}

void FACPClient::SendRawMessage(const FString& JsonMessage)
{
	FScopeLock Lock(&WriteLock);

	if (!StdinWritePipe)
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Cannot send message, pipe not open"));
		return;
	}

	// Log outgoing message for debugging
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Sending message (%d chars): %s"), JsonMessage.Len(), *JsonMessage);

	// Append newline (NDJSON format)
	FString Message = JsonMessage + TEXT("\n");

	// Write to pipe - return value may be unreliable on some platforms
	bool bWriteSuccess = FPlatformProcess::WritePipe(StdinWritePipe, Message);
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: WritePipe returned: %s"), bWriteSuccess ? TEXT("true") : TEXT("false"));
}

void FACPClient::ProcessLine(const FString& Line)
{
	// Bail if client was already disconnected (stale AsyncTask from reader thread)
	if (GetState() == EACPClientState::Disconnected)
	{
		return;
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: ProcessLine called with %d chars"), Line.Len());

	// Skip lines that don't look like JSON (e.g., stderr log messages that leaked through)
	if (!Line.StartsWith(TEXT("{")))
	{
		// Log as info so we can see what non-JSON output is coming
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Skipping non-JSON line: %s"), *Line);
		return;
	}

	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Line);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Failed to parse JSON: %s"), *Line);
		return;
	}

	// Log all incoming JSON for debugging
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Received JSON: %s"), *Line);

	bool bHasId = JsonObject->HasField(TEXT("id"));
	bool bHasMethod = JsonObject->HasField(TEXT("method"));

	// Check if it's a server request (has both id and method)
	if (bHasId && bHasMethod)
	{
		// Server request - requires a response
		int32 Id = static_cast<int32>(JsonObject->GetNumberField(TEXT("id")));
		FString Method = JsonObject->GetStringField(TEXT("method"));
		TSharedPtr<FJsonObject> Params = JsonObject->HasField(TEXT("params"))
			? JsonObject->GetObjectField(TEXT("params"))
			: MakeShared<FJsonObject>();
		HandleServerRequest(Id, Method, Params);
	}
	else if (bHasId)
	{
		// Response to our request
		int32 Id = static_cast<int32>(JsonObject->GetNumberField(TEXT("id")));

		if (JsonObject->HasField(TEXT("error")))
		{
			TSharedPtr<FJsonObject> Error = JsonObject->GetObjectField(TEXT("error"));
			int32 Code = static_cast<int32>(Error->GetNumberField(TEXT("code")));
			FString Message = Error->GetStringField(TEXT("message"));

			// Check for more detailed message in error.data (agents use different field names)
			if (Error->HasField(TEXT("data")))
			{
				TSharedPtr<FJsonObject> ErrorData = Error->GetObjectField(TEXT("data"));
				if (ErrorData.IsValid())
				{
					FString DetailedMessage;
					// Try data.message first (Codex CLI)
					if (ErrorData->TryGetStringField(TEXT("message"), DetailedMessage) && !DetailedMessage.IsEmpty())
					{
						Message = DetailedMessage;
					}
					// Try data.details (Gemini CLI)
					else if (ErrorData->TryGetStringField(TEXT("details"), DetailedMessage) && !DetailedMessage.IsEmpty())
					{
						Message = DetailedMessage;
					}
				}
			}

			HandleError(Id, Code, Message);
		}
		else if (JsonObject->HasField(TEXT("result")))
		{
			TSharedPtr<FJsonObject> Result = JsonObject->GetObjectField(TEXT("result"));
			HandleResponse(Id, Result);
		}
	}
	else if (bHasMethod)
	{
		// Notification (no id)
		FString Method = JsonObject->GetStringField(TEXT("method"));
		TSharedPtr<FJsonObject> Params = JsonObject->HasField(TEXT("params"))
			? JsonObject->GetObjectField(TEXT("params"))
			: MakeShared<FJsonObject>();
		HandleNotification(Method, Params);
	}
}

void FACPClient::HandleResponse(int32 Id, TSharedPtr<FJsonObject> Result)
{
	FString Method;
	{
		FScopeLock Lock(&StateLock);
		if (FString* MethodPtr = PendingRequests.Find(Id))
		{
			Method = *MethodPtr;
			PendingRequests.Remove(Id);
		}
	}

	if (Method == TEXT("initialize"))
	{
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Received initialize response"));
		// Parse agent capabilities — support both current ACP format (agentCapabilities)
		// and legacy format (capabilities.sessions) for backward compatibility
		if (Result.IsValid())
		{
			if (Result->HasField(TEXT("agentCapabilities")))
			{
				TSharedPtr<FJsonObject> AgentCaps = Result->GetObjectField(TEXT("agentCapabilities"));
				if (AgentCaps.IsValid())
				{
					// loadSession (top-level boolean)
					if (AgentCaps->HasField(TEXT("loadSession")))
					{
						AgentCapabilities.bSupportsLoadSession = AgentCaps->GetBoolField(TEXT("loadSession"));
					}

					// sessionCapabilities.resume, sessionCapabilities.list, etc.
					if (AgentCaps->HasField(TEXT("sessionCapabilities")))
					{
						TSharedPtr<FJsonObject> SessionCaps = AgentCaps->GetObjectField(TEXT("sessionCapabilities"));
						if (SessionCaps.IsValid())
						{
							AgentCapabilities.bSupportsResumeSession = SessionCaps->HasField(TEXT("resume"));
							AgentCapabilities.bSupportsListSessions = SessionCaps->HasField(TEXT("list"));
						}
					}

					// promptCapabilities.image, promptCapabilities.audio
					if (AgentCaps->HasField(TEXT("promptCapabilities")))
					{
						TSharedPtr<FJsonObject> PromptCaps = AgentCaps->GetObjectField(TEXT("promptCapabilities"));
						if (PromptCaps.IsValid())
						{
							AgentCapabilities.bSupportsImage = PromptCaps->HasField(TEXT("image")) && PromptCaps->GetBoolField(TEXT("image"));
							AgentCapabilities.bSupportsAudio = PromptCaps->HasField(TEXT("audio")) && PromptCaps->GetBoolField(TEXT("audio"));
						}
					}

					UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Capabilities - Resume=%d, Load=%d, Image=%d, Audio=%d"),
						AgentCapabilities.bSupportsResumeSession, AgentCapabilities.bSupportsLoadSession,
						AgentCapabilities.bSupportsImage, AgentCapabilities.bSupportsAudio);
				}
			}
			else if (Result->HasField(TEXT("capabilities")))
			{
				// Legacy format fallback
				TSharedPtr<FJsonObject> Caps = Result->GetObjectField(TEXT("capabilities"));

				if (Caps->HasField(TEXT("sessions")))
				{
					TSharedPtr<FJsonObject> Sessions = Caps->GetObjectField(TEXT("sessions"));
					AgentCapabilities.bSupportsNewSession = Sessions->HasField(TEXT("new"));
					AgentCapabilities.bSupportsLoadSession = Sessions->HasField(TEXT("load"));
				}

				if (Caps->HasField(TEXT("prompts")))
				{
					TSharedPtr<FJsonObject> Prompts = Caps->GetObjectField(TEXT("prompts"));
					AgentCapabilities.bSupportsAudio = Prompts->HasField(TEXT("audio")) && Prompts->GetBoolField(TEXT("audio"));
					AgentCapabilities.bSupportsImage = Prompts->HasField(TEXT("image")) && Prompts->GetBoolField(TEXT("image"));
				}
			}
		}

		// Parse authMethods (top-level field in initialize response)
		if (Result.IsValid() && Result->HasField(TEXT("authMethods")))
		{
			const TArray<TSharedPtr<FJsonValue>>* MethodsArray = nullptr;
			if (Result->TryGetArrayField(TEXT("authMethods"), MethodsArray) && MethodsArray)
			{
				for (const auto& Val : *MethodsArray)
				{
					TSharedPtr<FJsonObject> Obj = Val->AsObject();
					if (!Obj.IsValid()) continue;

					FACPAuthMethod AuthMethod;
					Obj->TryGetStringField(TEXT("id"), AuthMethod.Id);
					Obj->TryGetStringField(TEXT("name"), AuthMethod.Name);
					Obj->TryGetStringField(TEXT("description"), AuthMethod.Description);

					// Check for _meta.terminal-auth (client must spawn command externally)
					if (Obj->HasField(TEXT("_meta")))
					{
						TSharedPtr<FJsonObject> Meta = Obj->GetObjectField(TEXT("_meta"));
						if (Meta.IsValid() && Meta->HasField(TEXT("terminal-auth")))
						{
							TSharedPtr<FJsonObject> TA = Meta->GetObjectField(TEXT("terminal-auth"));
							if (TA.IsValid())
							{
								AuthMethod.bIsTerminalAuth = true;
								TA->TryGetStringField(TEXT("command"), AuthMethod.TerminalAuthCommand);
								TA->TryGetStringField(TEXT("label"), AuthMethod.TerminalAuthLabel);

								const TArray<TSharedPtr<FJsonValue>>* ArgsArr = nullptr;
								if (TA->TryGetArrayField(TEXT("args"), ArgsArr) && ArgsArr)
								{
									for (const auto& Arg : *ArgsArr)
									{
										AuthMethod.TerminalAuthArgs.Add(Arg->AsString());
									}
								}
							}
						}
					}

					AgentCapabilities.AuthMethods.Add(AuthMethod);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Auth method: %s (%s) terminal=%d"),
						*AuthMethod.Id, *AuthMethod.Name, AuthMethod.bIsTerminalAuth);
				}
			}
		}

		SetState(EACPClientState::Ready, TEXT("Connected to agent"));
	}
	else if (Method == TEXT("session/new"))
	{
		if (Result.IsValid())
		{
			// Try both "sessionId" and "id" field names for compatibility
			if (Result->HasField(TEXT("sessionId")))
			{
				CurrentSessionId = Result->GetStringField(TEXT("sessionId"));
			}
			else if (Result->HasField(TEXT("id")))
			{
				CurrentSessionId = Result->GetStringField(TEXT("id"));
			}

			// Parse configOptions FIRST so reasoning options are available before model broadcasts
				if (Result->HasField(TEXT("configOptions")))
				{
					const TArray<TSharedPtr<FJsonValue>>* ConfigArray = nullptr;
					if (Result->TryGetArrayField(TEXT("configOptions"), ConfigArray))
					{
						bool bFoundReasoningConfig = false;
						AvailableReasoningEfforts.Empty();
						CurrentReasoningEffort.Empty();
						ReasoningConfigOptionId.Empty();

						for (const TSharedPtr<FJsonValue>& OptionValue : *ConfigArray)
						{
							TSharedPtr<FJsonObject> OptionObj = OptionValue->AsObject();
							if (!OptionObj.IsValid()) continue;

							FString OptionId, Category, CurrentValue;
							OptionObj->TryGetStringField(TEXT("id"), OptionId);
							OptionObj->TryGetStringField(TEXT("category"), Category);
							OptionObj->TryGetStringField(TEXT("currentValue"), CurrentValue);

						const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
						OptionObj->TryGetArrayField(TEXT("options"), OptionsArray);

						if (Category == TEXT("model") && OptionsArray)
						{
							// Agent provides models via configOptions — use unified config path
							bUsesConfigOptions = true;
							SessionModelState.AvailableModels.Empty();
							SessionModelState.CurrentModelId = CurrentValue;
							for (const TSharedPtr<FJsonValue>& OptValue : *OptionsArray)
							{
								TSharedPtr<FJsonObject> OptObj = OptValue->AsObject();
								if (!OptObj.IsValid()) continue;
								FACPModelInfo ModelInfo;
								OptObj->TryGetStringField(TEXT("value"), ModelInfo.ModelId);
								OptObj->TryGetStringField(TEXT("name"), ModelInfo.Name);
								OptObj->TryGetStringField(TEXT("description"), ModelInfo.Description);
								SessionModelState.AvailableModels.Add(ModelInfo);
							}
							OnModelsAvailable.Broadcast(SessionModelState);
						}
						else if (Category == TEXT("mode") && OptionsArray)
						{
							SessionModeState.AvailableModes.Empty();
							SessionModeState.CurrentModeId = CurrentValue;
							for (const TSharedPtr<FJsonValue>& OptValue : *OptionsArray)
							{
								TSharedPtr<FJsonObject> OptObj = OptValue->AsObject();
								if (!OptObj.IsValid()) continue;
								FACPSessionMode ModeInfo;
								OptObj->TryGetStringField(TEXT("value"), ModeInfo.ModeId);
								OptObj->TryGetStringField(TEXT("name"), ModeInfo.Name);
								OptObj->TryGetStringField(TEXT("description"), ModeInfo.Description);
								SessionModeState.AvailableModes.Add(ModeInfo);
							}
							OnModesAvailable.Broadcast(SessionModeState);
						}
							else if (Category == TEXT("thought_level") && OptionsArray)
							{
								bFoundReasoningConfig = true;
								ReasoningConfigOptionId = OptionId.IsEmpty() ? TEXT("thinking") : OptionId;
								AvailableReasoningEfforts.Empty();
								CurrentReasoningEffort = CurrentValue;
								for (const TSharedPtr<FJsonValue>& OptValue : *OptionsArray)
								{
									TSharedPtr<FJsonObject> OptObj = OptValue->AsObject();
									if (!OptObj.IsValid()) continue;
									FString Value;
									OptObj->TryGetStringField(TEXT("value"), Value);
									if (!Value.IsEmpty())
									{
										AvailableReasoningEfforts.Add(Value);
									}
								}

								UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: session/create - %d reasoning options, current: %s"),
									AvailableReasoningEfforts.Num(), *CurrentReasoningEffort);

								// Reapply persisted reasoning level if available and supported.
									if (UACPSettings* Settings = UACPSettings::Get())
									{
										FString SavedReasoning = Settings->GetSavedReasoningForAgent(CurrentConfig.AgentName);
										if (!SavedReasoning.IsEmpty())
										{
											FString SavedThinkingValue = SavedReasoning == TEXT("none") ? TEXT("off") : SavedReasoning;
											if (AvailableReasoningEfforts.Contains(SavedThinkingValue)
												&& SavedThinkingValue != CurrentReasoningEffort
												&& !CurrentSessionId.IsEmpty())
											{
												SetReasoningEffort(SavedThinkingValue);
											}
										}
									}
								}
							}

						if (!bFoundReasoningConfig)
						{
							AvailableReasoningEfforts.Empty();
							CurrentReasoningEffort.Empty();
							ReasoningConfigOptionId.Empty();
						}
						}
					}

			// Parse old-style models object (fallback for agents that don't use configOptions)
			if (!bUsesConfigOptions && Result->HasField(TEXT("models")))
			{
				TSharedPtr<FJsonObject> ModelsObj = Result->GetObjectField(TEXT("models"));
				if (ModelsObj.IsValid())
				{
					SessionModelState.AvailableModels.Empty();

					if (ModelsObj->HasField(TEXT("currentModelId")))
					{
						SessionModelState.CurrentModelId = ModelsObj->GetStringField(TEXT("currentModelId"));
					}

					const TArray<TSharedPtr<FJsonValue>>* AvailableModelsArray;
					if (ModelsObj->TryGetArrayField(TEXT("availableModels"), AvailableModelsArray))
					{
						for (const TSharedPtr<FJsonValue>& ModelValue : *AvailableModelsArray)
						{
							TSharedPtr<FJsonObject> ModelObj = ModelValue->AsObject();
							if (ModelObj.IsValid())
							{
								FACPModelInfo ModelInfo;
								ModelObj->TryGetStringField(TEXT("modelId"), ModelInfo.ModelId);
								ModelObj->TryGetStringField(TEXT("name"), ModelInfo.Name);
								ModelObj->TryGetStringField(TEXT("description"), ModelInfo.Description);
								SessionModelState.AvailableModels.Add(ModelInfo);
							}
						}
					}

					OnModelsAvailable.Broadcast(SessionModelState);
				}
			}

			// Parse old-style modes object (fallback for agents that don't use configOptions)
			if (!bUsesConfigOptions && Result->HasField(TEXT("modes")))
			{
				TSharedPtr<FJsonObject> ModesObj = Result->GetObjectField(TEXT("modes"));
				if (ModesObj.IsValid())
				{
					SessionModeState.AvailableModes.Empty();

					if (ModesObj->HasField(TEXT("currentModeId")))
					{
						SessionModeState.CurrentModeId = ModesObj->GetStringField(TEXT("currentModeId"));
					}

					const TArray<TSharedPtr<FJsonValue>>* AvailableModesArray;
					if (ModesObj->TryGetArrayField(TEXT("availableModes"), AvailableModesArray))
					{
						for (const TSharedPtr<FJsonValue>& ModeValue : *AvailableModesArray)
						{
							TSharedPtr<FJsonObject> ModeObj = ModeValue->AsObject();
							if (ModeObj.IsValid())
							{
								FACPSessionMode ModeInfo;
								ModeObj->TryGetStringField(TEXT("id"), ModeInfo.ModeId);
								ModeObj->TryGetStringField(TEXT("name"), ModeInfo.Name);
								ModeObj->TryGetStringField(TEXT("description"), ModeInfo.Description);
								SessionModeState.AvailableModes.Add(ModeInfo);
							}
						}
					}

					OnModesAvailable.Broadcast(SessionModeState);
				}
			}

			if (!CurrentSessionId.IsEmpty())
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Session created with ID: %s"), *CurrentSessionId);
				// Don't clobber Prompting state — a pending prompt may have already been
				// sent via another session/new response, transitioning us to Prompting.
				if (GetState() != EACPClientState::Prompting)
				{
					SetState(EACPClientState::InSession, TEXT("Session started"));
				}
			}
		}
	}
	else if (Method == TEXT("session/resume") || Method == TEXT("session/load"))
	{
		// CurrentSessionId was already set before sending the request (in ResumeSession/LoadSession)
		// The response may optionally include sessionId (some agents do), but per ACP spec it's not required
		if (Result.IsValid())
		{
			// Update sessionId if the response provides one (some agents may return it)
			if (Result->HasField(TEXT("sessionId")))
			{
				CurrentSessionId = Result->GetStringField(TEXT("sessionId"));
			}

			// Parse unified configOptions first when provided so they remain the source of truth.
			if (Result->HasField(TEXT("configOptions")))
			{
				const TArray<TSharedPtr<FJsonValue>>* ConfigArray = nullptr;
				if (Result->TryGetArrayField(TEXT("configOptions"), ConfigArray) && ConfigArray)
				{
					TSharedRef<FJsonObject> SyntheticUpdate = MakeShared<FJsonObject>();
					SyntheticUpdate->SetStringField(TEXT("updateType"), TEXT("config_option_update"));
					SyntheticUpdate->SetArrayField(TEXT("configOptions"), *ConfigArray);
					ProcessSessionUpdate(SyntheticUpdate);
				}
			}

			// Parse models if provided (same as session/new)
			if (!bUsesConfigOptions && Result->HasField(TEXT("models")))
			{
				TSharedPtr<FJsonObject> ModelsObj = Result->GetObjectField(TEXT("models"));
				if (ModelsObj.IsValid())
				{
					SessionModelState.AvailableModels.Empty();

					if (ModelsObj->HasField(TEXT("currentModelId")))
					{
						SessionModelState.CurrentModelId = ModelsObj->GetStringField(TEXT("currentModelId"));
					}

					const TArray<TSharedPtr<FJsonValue>>* AvailableModelsArray;
					if (ModelsObj->TryGetArrayField(TEXT("availableModels"), AvailableModelsArray))
					{
						for (const TSharedPtr<FJsonValue>& ModelValue : *AvailableModelsArray)
						{
							TSharedPtr<FJsonObject> ModelObj = ModelValue->AsObject();
							if (ModelObj.IsValid())
							{
								FACPModelInfo ModelInfo;
								ModelObj->TryGetStringField(TEXT("modelId"), ModelInfo.ModelId);
								ModelObj->TryGetStringField(TEXT("name"), ModelInfo.Name);
								ModelObj->TryGetStringField(TEXT("description"), ModelInfo.Description);
								SessionModelState.AvailableModels.Add(ModelInfo);
							}
						}
					}

					OnModelsAvailable.Broadcast(SessionModelState);
				}
			}

			// Parse modes if provided (same as session/new)
			if (!bUsesConfigOptions && Result->HasField(TEXT("modes")))
			{
				TSharedPtr<FJsonObject> ModesObj = Result->GetObjectField(TEXT("modes"));
				if (ModesObj.IsValid())
				{
					SessionModeState.AvailableModes.Empty();

					if (ModesObj->HasField(TEXT("currentModeId")))
					{
						SessionModeState.CurrentModeId = ModesObj->GetStringField(TEXT("currentModeId"));
					}

					const TArray<TSharedPtr<FJsonValue>>* AvailableModesArray;
					if (ModesObj->TryGetArrayField(TEXT("availableModes"), AvailableModesArray))
					{
						for (const TSharedPtr<FJsonValue>& ModeValue : *AvailableModesArray)
						{
							TSharedPtr<FJsonObject> ModeObj = ModeValue->AsObject();
							if (ModeObj.IsValid())
							{
								FACPSessionMode ModeInfo;
								ModeObj->TryGetStringField(TEXT("id"), ModeInfo.ModeId);
								ModeObj->TryGetStringField(TEXT("name"), ModeInfo.Name);
								ModeObj->TryGetStringField(TEXT("description"), ModeInfo.Description);
								SessionModeState.AvailableModes.Add(ModeInfo);
							}
						}
					}

					OnModesAvailable.Broadcast(SessionModeState);
				}
			}
		}

		if (!CurrentSessionId.IsEmpty())
		{
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Session %s with ID: %s"),
				Method == TEXT("session/resume") ? TEXT("resumed") : TEXT("loaded"), *CurrentSessionId);
			// Don't clobber Prompting state — a pending prompt may have already been
			// sent via another session response, transitioning us to Prompting.
			if (GetState() != EACPClientState::Prompting)
			{
				SetState(EACPClientState::InSession,
					Method == TEXT("session/resume") ? TEXT("Session resumed") : TEXT("Session loaded"));
			}
		}
		else
		{
			UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: %s response but no session ID available"), *Method);
			SetState(EACPClientState::Error, TEXT("Session restore failed - no session ID"));
		}
	}
	else if (Method == TEXT("session/prompt"))
	{
		// Parse usage from prompt response if available
		if (Result.IsValid() && Result->HasField(TEXT("usage")))
		{
			TSharedPtr<FJsonObject> UsageObj = Result->GetObjectField(TEXT("usage"));
			if (UsageObj.IsValid())
			{
				int32 TotalTokens = 0, InputTokens = 0, OutputTokens = 0;
				int32 ThoughtTokens = 0, CachedRead = 0, CachedWrite = 0;

				UsageObj->TryGetNumberField(TEXT("total_tokens"), TotalTokens);
				UsageObj->TryGetNumberField(TEXT("input_tokens"), InputTokens);
				UsageObj->TryGetNumberField(TEXT("output_tokens"), OutputTokens);
				UsageObj->TryGetNumberField(TEXT("thought_tokens"), ThoughtTokens);

				// Support both ACP field names and Claude API field names
				// ACP: cached_read_tokens, cached_write_tokens
				// Claude: cache_read_input_tokens, cache_creation_input_tokens
				if (!UsageObj->TryGetNumberField(TEXT("cached_read_tokens"), CachedRead))
				{
					UsageObj->TryGetNumberField(TEXT("cache_read_input_tokens"), CachedRead);
				}
				if (!UsageObj->TryGetNumberField(TEXT("cached_write_tokens"), CachedWrite))
				{
					UsageObj->TryGetNumberField(TEXT("cache_creation_input_tokens"), CachedWrite);
				}

				// Update session usage
				SessionUsage.TotalTokens = TotalTokens;
				SessionUsage.InputTokens = InputTokens;
				SessionUsage.OutputTokens = OutputTokens;
				SessionUsage.ReasoningTokens = ThoughtTokens;
				SessionUsage.CachedTokens = CachedRead + CachedWrite;

				UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Prompt usage - Total: %d, Input: %d, Output: %d, Thought: %d"),
					TotalTokens, InputTokens, OutputTokens, ThoughtTokens);

				// Broadcast usage update
				FACPSessionUpdate UsageUpdate;
				UsageUpdate.UpdateType = EACPUpdateType::UsageUpdate;
				UsageUpdate.Usage = SessionUsage;
				OnSessionUpdate.Broadcast(UsageUpdate);
			}
		}

		// Prompt completed
		SetState(EACPClientState::InSession, TEXT("Ready"));
	}
	else if (Method == TEXT("session/set_mode"))
	{
		// Mode change successful
		if (Result.IsValid() && Result->HasField(TEXT("success")))
		{
			bool bSuccess = Result->GetBoolField(TEXT("success"));
			if (bSuccess)
			{
				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Session mode changed successfully"));
				// The current mode will be updated via session/update notification
			}
		}
	}
	else if (Method == TEXT("session/set_config_option"))
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Config option set successfully"));

		// Some adapters include updated configOptions in the response but do not
		// always emit a separate config_option_update notification. Parse here too.
		if (Result.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* ConfigArray = nullptr;
			if (Result->TryGetArrayField(TEXT("configOptions"), ConfigArray) && ConfigArray)
			{
				TSharedRef<FJsonObject> SyntheticUpdate = MakeShared<FJsonObject>();
				SyntheticUpdate->SetStringField(TEXT("updateType"), TEXT("config_option_update"));
				SyntheticUpdate->SetArrayField(TEXT("configOptions"), *ConfigArray);
				ProcessSessionUpdate(SyntheticUpdate);
			}
		}
	}
	else if (Method == TEXT("authenticate"))
	{
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Authentication succeeded"));
		OnAuthComplete.Broadcast(true, TEXT(""));
	}
	else if (Method == TEXT("session/list"))
	{
		TArray<FACPRemoteSessionEntry> Sessions;
		if (Result.IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* SessionsArray = nullptr;
			if (Result->TryGetArrayField(TEXT("sessions"), SessionsArray) && SessionsArray)
			{
				for (const auto& Val : *SessionsArray)
				{
					const TSharedPtr<FJsonObject>* ObjPtr = nullptr;
					if (!Val.IsValid() || !Val->TryGetObject(ObjPtr) || !ObjPtr || !ObjPtr->IsValid()) continue;
					const TSharedPtr<FJsonObject>& Obj = *ObjPtr;

					FACPRemoteSessionEntry Entry;
					Obj->TryGetStringField(TEXT("sessionId"), Entry.SessionId);
					Obj->TryGetStringField(TEXT("title"), Entry.Title);
					Obj->TryGetStringField(TEXT("cwd"), Entry.Cwd);

					FString UpdatedStr;
					if (Obj->TryGetStringField(TEXT("updatedAt"), UpdatedStr))
					{
						FDateTime::ParseIso8601(*UpdatedStr, Entry.UpdatedAt);
					}

					if (!Entry.SessionId.IsEmpty())
					{
						Sessions.Add(MoveTemp(Entry));
					}
				}
			}
		}
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Received %d sessions from agent"), Sessions.Num());
		OnSessionListReceived.Broadcast(Sessions);
	}

	OnResponse.Broadcast(Result);
}

void FACPClient::HandleError(int32 Id, int32 Code, const FString& Message)
{
	FString Method;
	{
		FScopeLock Lock(&StateLock);
		if (FString* MethodPtr = PendingRequests.Find(Id))
		{
			Method = *MethodPtr;
			PendingRequests.Remove(Id);
		}
	}

	UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Error in %s (code %d): %s"), *Method, Code, *Message);

	if (Method == TEXT("initialize"))
	{
		SetState(EACPClientState::Error, FString::Printf(TEXT("Initialization failed: %s"), *Message));
	}
	else if (Method == TEXT("session/resume"))
	{
		// session/resume failed — fall back to session/load if supported, then session/new
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: session/resume failed, attempting fallback"));
		if (AgentCapabilities.bSupportsLoadSession && !CurrentSessionId.IsEmpty())
		{
			// CurrentSessionId was pre-set in ResumeSession(), reuse it for load
			FString SessionIdToLoad = CurrentSessionId;
			CurrentSessionId.Empty(); // Clear so LoadSession can set it fresh
			LoadSession(SessionIdToLoad, FPaths::ConvertRelativePathToFull(UACPSettings::GetWorkingDirectory()));
		}
		else
		{
			CurrentSessionId.Empty(); // Clear stale session ID
			NewSession(UACPSettings::GetWorkingDirectory());
		}
	}
	else if (Method == TEXT("session/load"))
	{
		// session/load failed — fall back to session/new
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: session/load failed, falling back to new session"));
		CurrentSessionId.Empty(); // Clear stale session ID
		NewSession(UACPSettings::GetWorkingDirectory());
	}
	else if (Method == TEXT("session/new"))
	{
		SetState(EACPClientState::Error, FString::Printf(TEXT("Failed to create session: %s"), *Message));
	}
	else if (Method == TEXT("authenticate"))
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("ACPClient: Authentication failed: %s"), *Message);
		OnAuthComplete.Broadcast(false, Message);
	}
	else if (Method == TEXT("session/list"))
	{
		// Agent reported the capability but doesn't implement it — disable and don't retry
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: session/list not supported by agent, disabling"));
		AgentCapabilities.bSupportsListSessions = false;
	}
	else if (Method == TEXT("session/prompt"))
	{
		// Prompt request failed — transition out of Prompting so UI streaming can finalize.
		SetState(EACPClientState::InSession, TEXT("Ready"));
	}
	else
	{
		// Don't clobber Prompting state for non-critical errors (e.g., set_mode error
		// arriving while a prompt is in progress)
		if (GetState() != EACPClientState::Prompting)
		{
			SetState(EACPClientState::InSession, TEXT("Ready"));
		}
	}

	OnError.Broadcast(Code, Message);
}

void FACPClient::HandleNotification(const FString& Method, TSharedPtr<FJsonObject> Params)
{
	if (Method == TEXT("session/update"))
	{
		ProcessSessionUpdate(Params);
	}
	// Handle other notifications as needed
}

void FACPClient::ProcessSessionUpdate(TSharedPtr<FJsonObject> Params)
{
	if (!Params.IsValid())
	{
		return;
	}

	FACPSessionUpdate Update;

	// Track source session for manager-side routing.
	if (!Params->TryGetStringField(TEXT("sessionId"), Update.SessionId))
	{
		Params->TryGetStringField(TEXT("id"), Update.SessionId);
	}

	// ACP spec: params contains sessionId and update object
	// The update object has sessionUpdate (type) and content
	// OpenRouter uses flat format with type/text at top level
	TSharedPtr<FJsonObject> UpdateObj;
	if (Params->HasField(TEXT("update")))
	{
		UpdateObj = Params->GetObjectField(TEXT("update"));
	}
	else
	{
		// Fallback: flat format (OpenRouter agent uses type/text at top level)
		UpdateObj = Params;
	}

	// Get the update type - ACP uses "sessionUpdate", legacy uses "type"
	FString UpdateType;
	if (!UpdateObj->TryGetStringField(TEXT("sessionUpdate"), UpdateType))
	{
		UpdateObj->TryGetStringField(TEXT("type"), UpdateType);
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Processing session update type: '%s'"), *UpdateType);

	if (UpdateType == TEXT("agent_message_chunk"))
	{
		// Check for system status messages (e.g., compaction events) via _meta.systemStatus
		bool bIsSystemStatus = false;
		if (UpdateObj->HasField(TEXT("_meta")))
		{
			TSharedPtr<FJsonObject> Meta = UpdateObj->GetObjectField(TEXT("_meta"));
			if (Meta.IsValid() && Meta->HasField(TEXT("systemStatus")))
			{
				bIsSystemStatus = true;
				Update.SystemStatus = Meta->GetStringField(TEXT("systemStatus"));
			}
		}

		if (bIsSystemStatus)
		{
			// System status message — render as inline status indicator (not regular text)
			Update.UpdateType = EACPUpdateType::AgentMessageChunk;
			Update.bIsSystemStatus = true;
			if (UpdateObj->HasField(TEXT("content")))
			{
				TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
				if (Content.IsValid())
				{
					Update.TextChunk = Content->GetStringField(TEXT("text"));
				}
			}
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: System status: '%s' — %s"), *Update.SystemStatus, *Update.TextChunk);
		}
		else
		{
			Update.UpdateType = EACPUpdateType::AgentMessageChunk;
			// ACP format: content.text, legacy: text
			if (UpdateObj->HasField(TEXT("content")))
			{
				TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
				if (Content.IsValid())
				{
					Update.TextChunk = StripAnsiCodes(Content->GetStringField(TEXT("text")));
				}
			}
			else
			{
				Update.TextChunk = StripAnsiCodes(UpdateObj->GetStringField(TEXT("text")));
			}
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Parsed message chunk: '%s'"), *Update.TextChunk);
		}
	}
	else if (UpdateType == TEXT("agent_thought_chunk"))
	{
		Update.UpdateType = EACPUpdateType::AgentThoughtChunk;
		if (UpdateObj->HasField(TEXT("content")))
		{
			TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
			if (Content.IsValid())
			{
				Update.TextChunk = StripAnsiCodes(Content->GetStringField(TEXT("text")));
			}
		}
		else
		{
			Update.TextChunk = StripAnsiCodes(UpdateObj->GetStringField(TEXT("text")));
		}
	}
	else if (UpdateType == TEXT("user_message_chunk"))
	{
		// User messages arrive during session/load history replay
		Update.UpdateType = EACPUpdateType::UserMessageChunk;
		if (UpdateObj->HasField(TEXT("content")))
		{
			TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
			if (Content.IsValid())
			{
				Update.TextChunk = Content->GetStringField(TEXT("text"));
			}
		}
		else
		{
			Update.TextChunk = UpdateObj->GetStringField(TEXT("text"));
		}
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: User message chunk during replay: '%s'"), *Update.TextChunk.Left(100));
	}
	else if (UpdateType == TEXT("tool_call"))
	{
		Update.UpdateType = EACPUpdateType::ToolCall;

		// Claude Code uses "toolCallId", legacy uses "id"
		if (!UpdateObj->TryGetStringField(TEXT("toolCallId"), Update.ToolCallId))
		{
			UpdateObj->TryGetStringField(TEXT("id"), Update.ToolCallId);
		}

		// Get tool name - try multiple locations:
		// 1. _meta.claudeCode.toolName (Claude Code format)
		// 2. title (display name)
		// 3. name (legacy)
		if (UpdateObj->HasField(TEXT("_meta")))
		{
			TSharedPtr<FJsonObject> Meta = UpdateObj->GetObjectField(TEXT("_meta"));
			if (Meta.IsValid() && Meta->HasField(TEXT("claudeCode")))
			{
				TSharedPtr<FJsonObject> ClaudeCode = Meta->GetObjectField(TEXT("claudeCode"));
				if (ClaudeCode.IsValid())
				{
					ClaudeCode->TryGetStringField(TEXT("toolName"), Update.ToolName);
					ClaudeCode->TryGetStringField(TEXT("parentToolCallId"), Update.ParentToolCallId);
				}
			}
		}

		// Fall back to title or name if toolName not found
		if (Update.ToolName.IsEmpty())
		{
			if (!UpdateObj->TryGetStringField(TEXT("title"), Update.ToolName))
			{
				UpdateObj->TryGetStringField(TEXT("name"), Update.ToolName);
			}
		}

		// If ToolName is empty or just "{}" (Gemini CLI quirk), extract from toolCallId
		if (Update.ToolName.IsEmpty() || Update.ToolName == TEXT("{}"))
		{
			// toolCallId format: "toolname-timestamp" (e.g., "read_asset-1768495861936")
			FString ExtractedName = Update.ToolCallId;
			int32 DashIndex;
			if (ExtractedName.FindLastChar(TEXT('-'), DashIndex))
			{
				ExtractedName = ExtractedName.Left(DashIndex);
			}
			if (!ExtractedName.IsEmpty())
			{
				Update.ToolName = ExtractedName;
			}
		}

		// Arguments - try rawInput (Claude Code) or arguments (legacy)
		if (UpdateObj->HasField(TEXT("rawInput")))
		{
			TSharedPtr<FJsonObject> Args = UpdateObj->GetObjectField(TEXT("rawInput"));
			if (Args.IsValid() && Args->Values.Num() > 0)
			{
				FString ArgsString;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsString);
				FJsonSerializer::Serialize(Args.ToSharedRef(), Writer);
				Update.ToolArguments = ArgsString;
			}
		}
		else if (UpdateObj->HasField(TEXT("arguments")))
		{
			TSharedPtr<FJsonObject> Args = UpdateObj->GetObjectField(TEXT("arguments"));
			if (Args.IsValid())
			{
				FString ArgsString;
				TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&ArgsString);
				FJsonSerializer::Serialize(Args.ToSharedRef(), Writer);
				Update.ToolArguments = ArgsString;
			}
		}

		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Tool call - ID: %s, Name: %s"), *Update.ToolCallId, *Update.ToolName);
	}
	else if (UpdateType == TEXT("tool_call_update"))
	{
		Update.UpdateType = EACPUpdateType::ToolCallUpdate;

		// Claude Code uses "toolCallId", legacy uses "id"
		if (!UpdateObj->TryGetStringField(TEXT("toolCallId"), Update.ToolCallId))
		{
			UpdateObj->TryGetStringField(TEXT("id"), Update.ToolCallId);
		}

		// Extract parentToolCallId from _meta.claudeCode
		if (UpdateObj->HasField(TEXT("_meta")))
		{
			TSharedPtr<FJsonObject> Meta = UpdateObj->GetObjectField(TEXT("_meta"));
			if (Meta.IsValid() && Meta->HasField(TEXT("claudeCode")))
			{
				TSharedPtr<FJsonObject> ClaudeCode = Meta->GetObjectField(TEXT("claudeCode"));
				if (ClaudeCode.IsValid())
				{
					ClaudeCode->TryGetStringField(TEXT("parentToolCallId"), Update.ParentToolCallId);
				}
			}
		}

		// Check status field (Claude Code format)
		FString Status;
		if (UpdateObj->TryGetStringField(TEXT("status"), Status))
		{
			Update.bToolSuccess = (Status == TEXT("completed"));
		}
		else
		{
			Update.bToolSuccess = !UpdateObj->HasField(TEXT("error"));
		}

		// Get result from content array (Claude Code) or result field (legacy)
		if (UpdateObj->HasField(TEXT("content")))
		{
			const TArray<TSharedPtr<FJsonValue>>* ContentArray;
			if (UpdateObj->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray->Num() > 0)
			{
				// Loop through content blocks to extract text and images
				for (const TSharedPtr<FJsonValue>& ContentValue : *ContentArray)
				{
					TSharedPtr<FJsonObject> ContentBlock = ContentValue->AsObject();
					if (!ContentBlock.IsValid())
					{
						continue;
					}

					FString ContentType;
					ContentBlock->TryGetStringField(TEXT("type"), ContentType);

					if (ContentType == TEXT("text"))
					{
						// Extract text content
						FString Text;
						if (ContentBlock->TryGetStringField(TEXT("text"), Text))
						{
							if (!Update.ToolResult.IsEmpty())
							{
								Update.ToolResult += TEXT("\n");
							}
							Update.ToolResult += Text;
						}
						// Also check nested content structure (Claude Code format)
						else if (ContentBlock->HasField(TEXT("content")))
						{
							TSharedPtr<FJsonObject> InnerContent = ContentBlock->GetObjectField(TEXT("content"));
							if (InnerContent.IsValid())
							{
								InnerContent->TryGetStringField(TEXT("text"), Text);
								if (!Update.ToolResult.IsEmpty())
								{
									Update.ToolResult += TEXT("\n");
								}
								Update.ToolResult += Text;
							}
						}
					}
					else if (ContentType == TEXT("image"))
					{
						FACPToolResultImage Image;
						Image.Width = 0;
						Image.Height = 0;

						// Try direct format first: { type: "image", data: "...", mimeType: "..." }
						ContentBlock->TryGetStringField(TEXT("data"), Image.Base64Data);
						ContentBlock->TryGetStringField(TEXT("mimeType"), Image.MimeType);

						// Fall back to Anthropic API format: { type: "image", source: { data: "...", media_type: "...", type: "base64" } }
						if (Image.Base64Data.IsEmpty())
						{
							TSharedPtr<FJsonObject> SourceObj = ContentBlock->GetObjectField(TEXT("source"));
							if (SourceObj.IsValid())
							{
								SourceObj->TryGetStringField(TEXT("data"), Image.Base64Data);
								SourceObj->TryGetStringField(TEXT("media_type"), Image.MimeType);
							}
						}

						if (!Image.Base64Data.IsEmpty())
						{
							Update.ToolResultImages.Add(Image);
							UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Extracted image from tool result (%s)"), *Image.MimeType);
						}
					}
					else if (ContentType == TEXT("content"))
					{
						// ACP wraps content blocks: { type: "content", content: { type: "image"|"text", ... } }
						TSharedPtr<FJsonObject> InnerContent = ContentBlock->GetObjectField(TEXT("content"));
						if (InnerContent.IsValid())
						{
							FString InnerType;
							InnerContent->TryGetStringField(TEXT("type"), InnerType);

							if (InnerType == TEXT("image"))
							{
								FACPToolResultImage Image;
								Image.Width = 0;
								Image.Height = 0;

								// Try ACP direct format first: { type: "image", data: "...", mimeType: "..." }
								InnerContent->TryGetStringField(TEXT("data"), Image.Base64Data);
								InnerContent->TryGetStringField(TEXT("mimeType"), Image.MimeType);

								// Fall back to Anthropic API format: { type: "image", source: { data, media_type } }
								if (Image.Base64Data.IsEmpty())
								{
									TSharedPtr<FJsonObject> SourceObj = InnerContent->GetObjectField(TEXT("source"));
									if (SourceObj.IsValid())
									{
										SourceObj->TryGetStringField(TEXT("data"), Image.Base64Data);
										SourceObj->TryGetStringField(TEXT("media_type"), Image.MimeType);
									}
								}

								if (!Image.Base64Data.IsEmpty())
								{
									Update.ToolResultImages.Add(Image);
									UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Extracted nested image from tool result (%s)"), *Image.MimeType);
								}
							}
							else if (InnerType == TEXT("text"))
							{
								// Also handle nested text in content type
								FString Text;
								if (InnerContent->TryGetStringField(TEXT("text"), Text))
								{
									if (!Update.ToolResult.IsEmpty())
									{
										Update.ToolResult += TEXT("\n");
									}
									Update.ToolResult += Text;
								}
							}
						}
					}
				}
			}
		}
		else
		{
			UpdateObj->TryGetStringField(TEXT("result"), Update.ToolResult);
		}

		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Tool update - ID: %s, Success: %d, Images: %d"), *Update.ToolCallId, Update.bToolSuccess, Update.ToolResultImages.Num());
	}
	else if (UpdateType == TEXT("plan"))
	{
		Update.UpdateType = EACPUpdateType::Plan;

		// Parse plan entries
		const TArray<TSharedPtr<FJsonValue>>* EntriesArray = nullptr;

		// Try content.entries (ACP format) or entries directly
		if (UpdateObj->HasField(TEXT("content")))
		{
			TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
			if (Content.IsValid())
			{
				Content->TryGetArrayField(TEXT("entries"), EntriesArray);
			}
		}
		if (!EntriesArray)
		{
			UpdateObj->TryGetArrayField(TEXT("entries"), EntriesArray);
		}

		if (EntriesArray)
		{
			for (const TSharedPtr<FJsonValue>& EntryValue : *EntriesArray)
			{
				TSharedPtr<FJsonObject> EntryObj = EntryValue->AsObject();
				if (!EntryObj.IsValid())
				{
					continue;
				}

				FACPPlanEntry Entry;
				EntryObj->TryGetStringField(TEXT("content"), Entry.Content);
				EntryObj->TryGetStringField(TEXT("activeForm"), Entry.ActiveForm);

				// Parse priority
				FString PriorityStr;
				if (EntryObj->TryGetStringField(TEXT("priority"), PriorityStr))
				{
					if (PriorityStr == TEXT("high"))
					{
						Entry.Priority = EACPPlanEntryPriority::High;
					}
					else if (PriorityStr == TEXT("low"))
					{
						Entry.Priority = EACPPlanEntryPriority::Low;
					}
					else
					{
						Entry.Priority = EACPPlanEntryPriority::Medium;
					}
				}

				// Parse status
				FString StatusStr;
				if (EntryObj->TryGetStringField(TEXT("status"), StatusStr))
				{
					if (StatusStr == TEXT("completed"))
					{
						Entry.Status = EACPPlanEntryStatus::Completed;
					}
					else if (StatusStr == TEXT("in_progress"))
					{
						Entry.Status = EACPPlanEntryStatus::InProgress;
					}
					else
					{
						Entry.Status = EACPPlanEntryStatus::Pending;
					}
				}

				Update.Plan.Entries.Add(Entry);
			}

			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Plan update - %d entries, %d completed"),
				Update.Plan.Entries.Num(), Update.Plan.GetCompletedCount());
		}
	}
	else if (UpdateType == TEXT("error"))
	{
		Update.UpdateType = EACPUpdateType::Error;
		Update.ErrorCode = static_cast<int32>(UpdateObj->GetNumberField(TEXT("code")));
		Update.ErrorMessage = UpdateObj->GetStringField(TEXT("message"));

		// Check for more detailed message in data (agents use different field names)
		if (UpdateObj->HasField(TEXT("data")))
		{
			TSharedPtr<FJsonObject> ErrorData = UpdateObj->GetObjectField(TEXT("data"));
			if (ErrorData.IsValid())
			{
				FString DetailedMessage;
				// Try data.message first (Codex CLI)
				if (ErrorData->TryGetStringField(TEXT("message"), DetailedMessage) && !DetailedMessage.IsEmpty())
				{
					Update.ErrorMessage = DetailedMessage;
				}
				// Try data.details (Gemini CLI)
				else if (ErrorData->TryGetStringField(TEXT("details"), DetailedMessage) && !DetailedMessage.IsEmpty())
				{
					Update.ErrorMessage = DetailedMessage;
				}
			}
		}
	}
	else if (UpdateType == TEXT("available_commands_update"))
	{
		// Parse available slash commands
		AvailableCommands.Empty();

		const TArray<TSharedPtr<FJsonValue>>* CommandsArray = nullptr;

		// Try content.availableCommands (ACP format) or availableCommands directly
		if (UpdateObj->HasField(TEXT("content")))
		{
			TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
			if (Content.IsValid())
			{
				Content->TryGetArrayField(TEXT("availableCommands"), CommandsArray);
			}
		}
		if (!CommandsArray)
		{
			UpdateObj->TryGetArrayField(TEXT("availableCommands"), CommandsArray);
		}

		if (CommandsArray)
		{
			for (const TSharedPtr<FJsonValue>& CmdValue : *CommandsArray)
			{
				TSharedPtr<FJsonObject> CmdObj = CmdValue->AsObject();
				if (!CmdObj.IsValid())
				{
					continue;
				}

				FACPSlashCommand Command;
				CmdObj->TryGetStringField(TEXT("name"), Command.Name);
				CmdObj->TryGetStringField(TEXT("description"), Command.Description);

				// Parse input hint if present (input can be null or an object)
				const TSharedPtr<FJsonObject>* InputObjPtr = nullptr;
				if (CmdObj->TryGetObjectField(TEXT("input"), InputObjPtr) && InputObjPtr && InputObjPtr->IsValid())
				{
					(*InputObjPtr)->TryGetStringField(TEXT("hint"), Command.InputHint);
				}

				if (!Command.Name.IsEmpty())
				{
					AvailableCommands.Add(Command);
				}
			}

			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Received %d available commands"), AvailableCommands.Num());
			OnCommandsAvailable.Broadcast(AvailableCommands);
		}

		return; // Don't broadcast as regular session update
	}
	else if (UpdateType == TEXT("current_mode_update"))
	{
		// Mode changed - update our state
		FString NewModeId;
		if (UpdateObj->HasField(TEXT("content")))
		{
			TSharedPtr<FJsonObject> Content = UpdateObj->GetObjectField(TEXT("content"));
			if (Content.IsValid())
			{
				Content->TryGetStringField(TEXT("modeId"), NewModeId);
			}
		}
		else
		{
			UpdateObj->TryGetStringField(TEXT("modeId"), NewModeId);
		}

		if (!NewModeId.IsEmpty())
		{
			SessionModeState.CurrentModeId = NewModeId;
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Session mode changed to: %s"), *NewModeId);
			OnModeChanged.Broadcast(NewModeId);
		}
		return; // Don't broadcast as session update
	}
	else if (UpdateType == TEXT("usage_update"))
	{
		Update.UpdateType = EACPUpdateType::UsageUpdate;

		// Parse context window info (required per ACP spec)
		int32 ContextUsed = 0, ContextSize = 0;
		if (UpdateObj->TryGetNumberField(TEXT("used"), ContextUsed))
		{
			Update.Usage.ContextUsed = ContextUsed;
		}
		if (UpdateObj->TryGetNumberField(TEXT("size"), ContextSize))
		{
			Update.Usage.ContextSize = ContextSize;
		}

		// Parse cost info (optional per ACP spec)
		if (UpdateObj->HasField(TEXT("cost")))
		{
			TSharedPtr<FJsonObject> CostObj = UpdateObj->GetObjectField(TEXT("cost"));
			if (CostObj.IsValid())
			{
				double CostAmount = 0.0;
				FString CostCurrency = TEXT("USD");
				CostObj->TryGetNumberField(TEXT("amount"), CostAmount);
				CostObj->TryGetStringField(TEXT("currency"), CostCurrency);

				Update.Usage.CostAmount = CostAmount;
				Update.Usage.CostCurrency = CostCurrency;
			}
		}

		// Parse detailed token breakdown from _meta (sent by our adapter)
		if (UpdateObj->HasField(TEXT("_meta")))
		{
			TSharedPtr<FJsonObject> Meta = UpdateObj->GetObjectField(TEXT("_meta"));
			if (Meta.IsValid())
			{
				int32 InTok = 0, OutTok = 0, CacheRead = 0, CacheCreate = 0;
				Meta->TryGetNumberField(TEXT("inputTokens"), InTok);
				Meta->TryGetNumberField(TEXT("outputTokens"), OutTok);
				Meta->TryGetNumberField(TEXT("cacheReadTokens"), CacheRead);
				Meta->TryGetNumberField(TEXT("cacheCreationTokens"), CacheCreate);

				Update.Usage.InputTokens = InTok;
				Update.Usage.OutputTokens = OutTok;
				Update.Usage.CacheReadTokens = CacheRead;
				Update.Usage.CacheCreationTokens = CacheCreate;
				Update.Usage.CachedTokens = CacheRead + CacheCreate;
				Update.Usage.TotalTokens = InTok + OutTok;

				// Result-only fields
				double TotalCost = 0.0, TurnCost = 0.0;
				int32 NumTurns = 0, DurationMs = 0;
				Meta->TryGetNumberField(TEXT("totalCostUSD"), TotalCost);
				Meta->TryGetNumberField(TEXT("turnCostUSD"), TurnCost);
				Meta->TryGetNumberField(TEXT("numTurns"), NumTurns);
				Meta->TryGetNumberField(TEXT("durationMs"), DurationMs);
				Update.Usage.TurnCostUSD = TurnCost;
				Update.Usage.NumTurns = NumTurns;
				Update.Usage.DurationMs = DurationMs;

				// Per-model breakdown
				if (Meta->HasField(TEXT("modelUsage")))
				{
					TSharedPtr<FJsonObject> ModelObj = Meta->GetObjectField(TEXT("modelUsage"));
					if (ModelObj.IsValid())
					{
						Update.Usage.ModelUsage.Empty();
						for (const auto& Pair : ModelObj->Values)
						{
							TSharedPtr<FJsonObject> MU = Pair.Value->AsObject();
							if (!MU.IsValid()) continue;

							FModelUsageEntry Entry;
							Entry.ModelName = Pair.Key;
							MU->TryGetNumberField(TEXT("inputTokens"), Entry.InputTokens);
							MU->TryGetNumberField(TEXT("outputTokens"), Entry.OutputTokens);
							MU->TryGetNumberField(TEXT("cacheReadTokens"), Entry.CacheReadTokens);
							MU->TryGetNumberField(TEXT("cacheCreationTokens"), Entry.CacheCreationTokens);
							MU->TryGetNumberField(TEXT("costUSD"), Entry.CostUSD);
							MU->TryGetNumberField(TEXT("contextWindow"), Entry.ContextWindow);
							MU->TryGetNumberField(TEXT("maxOutputTokens"), Entry.MaxOutputTokens);
							MU->TryGetNumberField(TEXT("webSearchRequests"), Entry.WebSearchRequests);
							Update.Usage.ModelUsage.Add(MoveTemp(Entry));
						}
					}
				}
			}
		}

		// Update cumulative session usage
		SessionUsage.ContextUsed = ContextUsed;
		SessionUsage.ContextSize = ContextSize;
		SessionUsage.InputTokens = Update.Usage.InputTokens;
		SessionUsage.OutputTokens = Update.Usage.OutputTokens;
		SessionUsage.CacheReadTokens = Update.Usage.CacheReadTokens;
		SessionUsage.CacheCreationTokens = Update.Usage.CacheCreationTokens;
		SessionUsage.CachedTokens = Update.Usage.CachedTokens;
		SessionUsage.TotalTokens = Update.Usage.TotalTokens;
		SessionUsage.TurnCostUSD = Update.Usage.TurnCostUSD;
		SessionUsage.NumTurns = Update.Usage.NumTurns;
		SessionUsage.DurationMs = Update.Usage.DurationMs;
		SessionUsage.ModelUsage = Update.Usage.ModelUsage;
		if (Update.Usage.CostAmount > 0.0)
		{
			SessionUsage.CostAmount = Update.Usage.CostAmount;
			SessionUsage.CostCurrency = Update.Usage.CostCurrency;
		}

		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Usage update - Context: %d/%d, In: %d, Out: %d, Cache: %d+%d, Cost: %s"),
			ContextUsed, ContextSize, Update.Usage.InputTokens, Update.Usage.OutputTokens,
			Update.Usage.CacheReadTokens, Update.Usage.CacheCreationTokens,
			*Update.Usage.GetFormattedCost());
	}
	else if (UpdateType == TEXT("config_option_update"))
	{
		// Unified config options — parse and feed into existing model/mode/thinking infrastructure
		const TArray<TSharedPtr<FJsonValue>>* ConfigArray = nullptr;
		UpdateObj->TryGetArrayField(TEXT("configOptions"), ConfigArray);
		if (!ConfigArray)
		{
			return;
		}

		bool bFoundReasoningConfig = false;
		for (const TSharedPtr<FJsonValue>& OptionValue : *ConfigArray)
		{
			TSharedPtr<FJsonObject> OptionObj = OptionValue->AsObject();
			if (!OptionObj.IsValid())
			{
				continue;
			}

			FString OptionId, Category, CurrentValue;
			OptionObj->TryGetStringField(TEXT("id"), OptionId);
			OptionObj->TryGetStringField(TEXT("category"), Category);
			OptionObj->TryGetStringField(TEXT("currentValue"), CurrentValue);

			const TArray<TSharedPtr<FJsonValue>>* OptionsArray = nullptr;
			OptionObj->TryGetArrayField(TEXT("options"), OptionsArray);

			if (Category == TEXT("model") && OptionsArray)
			{
				// Agent provides models via configOptions — use unified config path
				bUsesConfigOptions = true;
				SessionModelState.AvailableModels.Empty();
				SessionModelState.CurrentModelId = CurrentValue;

				for (const TSharedPtr<FJsonValue>& OptValue : *OptionsArray)
				{
					TSharedPtr<FJsonObject> OptObj = OptValue->AsObject();
					if (!OptObj.IsValid()) continue;

					FACPModelInfo ModelInfo;
					OptObj->TryGetStringField(TEXT("value"), ModelInfo.ModelId);
					OptObj->TryGetStringField(TEXT("name"), ModelInfo.Name);
					OptObj->TryGetStringField(TEXT("description"), ModelInfo.Description);
					SessionModelState.AvailableModels.Add(ModelInfo);
				}

				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: config_option_update - %d models, current: %s"),
					SessionModelState.AvailableModels.Num(), *CurrentValue);
				OnModelsAvailable.Broadcast(SessionModelState);
			}
			else if (Category == TEXT("mode") && OptionsArray)
			{
				// Convert to SessionModeState for existing mode selector UI
				SessionModeState.AvailableModes.Empty();
				SessionModeState.CurrentModeId = CurrentValue;

				for (const TSharedPtr<FJsonValue>& OptValue : *OptionsArray)
				{
					TSharedPtr<FJsonObject> OptObj = OptValue->AsObject();
					if (!OptObj.IsValid()) continue;

					FACPSessionMode ModeInfo;
					OptObj->TryGetStringField(TEXT("value"), ModeInfo.ModeId);
					OptObj->TryGetStringField(TEXT("name"), ModeInfo.Name);
					OptObj->TryGetStringField(TEXT("description"), ModeInfo.Description);
					SessionModeState.AvailableModes.Add(ModeInfo);
				}

				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: config_option_update - %d modes, current: %s"),
					SessionModeState.AvailableModes.Num(), *CurrentValue);
				OnModesAvailable.Broadcast(SessionModeState);
			}
			else if (Category == TEXT("thought_level") && OptionsArray)
			{
				bFoundReasoningConfig = true;
				// Reasoning effort options for agents like Codex
				ReasoningConfigOptionId = OptionId.IsEmpty() ? TEXT("thinking") : OptionId;
				AvailableReasoningEfforts.Empty();
				CurrentReasoningEffort = CurrentValue;

				for (const TSharedPtr<FJsonValue>& OptValue : *OptionsArray)
				{
					TSharedPtr<FJsonObject> OptObj = OptValue->AsObject();
					if (!OptObj.IsValid()) continue;

					FString Value;
					OptObj->TryGetStringField(TEXT("value"), Value);
					if (!Value.IsEmpty())
					{
						AvailableReasoningEfforts.Add(Value);
					}
				}

				UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: config_option_update - %d reasoning options, current: %s"),
					AvailableReasoningEfforts.Num(), *CurrentValue);

				// Reapply persisted reasoning level if available and supported.
					if (UACPSettings* Settings = UACPSettings::Get())
					{
						FString SavedReasoning = Settings->GetSavedReasoningForAgent(CurrentConfig.AgentName);
						if (!SavedReasoning.IsEmpty())
						{
							FString SavedThinkingValue = SavedReasoning == TEXT("none") ? TEXT("off") : SavedReasoning;
							if (AvailableReasoningEfforts.Contains(SavedThinkingValue)
								&& SavedThinkingValue != CurrentReasoningEffort
								&& !CurrentSessionId.IsEmpty())
							{
								SetReasoningEffort(SavedThinkingValue);
							}
						}
				}
			}
		}

		if (!bFoundReasoningConfig)
		{
			AvailableReasoningEfforts.Empty();
			CurrentReasoningEffort.Empty();
			ReasoningConfigOptionId.Empty();
		}

		return; // Don't broadcast as regular session update
	}
	else if (!UpdateType.IsEmpty())
	{
		// Log unknown update types so we can add support for them
		FString UpdateJson;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&UpdateJson);
		FJsonSerializer::Serialize(UpdateObj.ToSharedRef(), Writer);
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Unhandled session update type '%s': %s"), *UpdateType, *UpdateJson);
	}

	OnSessionUpdate.Broadcast(Update);
}

void FACPClient::SetState(EACPClientState NewState, const FString& Message)
{
	{
		FScopeLock Lock(&StateLock);
		State = NewState;
	}

	OnStateChanged.Broadcast(NewState, Message);
}

// ============================================================================
// ACP Protocol Methods
// ============================================================================

void FACPClient::Initialize()
{
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Sending initialize request..."));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

	// Protocol version (integer, currently version 1 per ACP spec)
	Params->SetNumberField(TEXT("protocolVersion"), 1);

	// Client info
	TSharedPtr<FJsonObject> ClientInfo = MakeShared<FJsonObject>();
	ClientInfo->SetStringField(TEXT("name"), TEXT("AgentIntegrationKit"));
	ClientInfo->SetStringField(TEXT("version"), TEXT("1.0.0"));
	Params->SetObjectField(TEXT("clientInfo"), ClientInfo);

	// Client capabilities
	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> FileSystem = MakeShared<FJsonObject>();
	FileSystem->SetBoolField(TEXT("readTextFile"), ClientCapabilities.bSupportsFileSystem);
	FileSystem->SetBoolField(TEXT("writeTextFile"), ClientCapabilities.bSupportsFileSystem);
	Capabilities->SetObjectField(TEXT("fileSystem"), FileSystem);

	TSharedPtr<FJsonObject> Terminal = MakeShared<FJsonObject>();
	Terminal->SetBoolField(TEXT("create"), ClientCapabilities.bSupportsTerminal);
	Capabilities->SetObjectField(TEXT("terminal"), Terminal);

	TSharedPtr<FJsonObject> Prompts = MakeShared<FJsonObject>();
	Prompts->SetBoolField(TEXT("audio"), ClientCapabilities.bSupportsAudio);
	Prompts->SetBoolField(TEXT("image"), ClientCapabilities.bSupportsImage);
	Capabilities->SetObjectField(TEXT("prompts"), Prompts);

	Params->SetObjectField(TEXT("capabilities"), Capabilities);

	SendRequest(TEXT("initialize"), Params);
}

void FACPClient::NewSession(const FString& WorkingDirectory)
{
	// Reset usage tracking for new session
	SessionUsage = FACPUsageData();

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

	// Use absolute path for cwd
	FString AbsolutePath = FPaths::ConvertRelativePathToFull(WorkingDirectory);
	Params->SetStringField(TEXT("cwd"), AbsolutePath);

	// Build mcpServers array
	TArray<TSharedPtr<FJsonValue>> McpServers;

	// Add Unreal MCP server if running
	if (FMCPServer::Get().IsRunning())
	{
		TSharedPtr<FJsonObject> UnrealMcp = MakeShared<FJsonObject>();
		UnrealMcp->SetStringField(TEXT("name"), TEXT("unreal-editor"));
		UnrealMcp->SetStringField(TEXT("type"), TEXT("http"));
		// Use 127.0.0.1 instead of localhost — Node.js fetch() resolves localhost via DNS,
		// which may try IPv6 (::1) first on some systems. The Unreal HTTP server only binds IPv4.
		UnrealMcp->SetStringField(TEXT("url"),
			FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), FMCPServer::Get().GetPort()));

		TArray<TSharedPtr<FJsonValue>> EmptyHeaders;
		UnrealMcp->SetArrayField(TEXT("headers"), EmptyHeaders);

		McpServers.Add(MakeShared<FJsonValueObject>(UnrealMcp));

		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Adding Unreal MCP server to session: %s"),
			*UnrealMcp->GetStringField(TEXT("url")));
	}

	Params->SetArrayField(TEXT("mcpServers"), McpServers);

	// Build _meta object
	TSharedPtr<FJsonObject> Meta = MakeShared<FJsonObject>();
	bool bHasMeta = false;

	// Add custom system prompt + active profile instructions (append mode)
	UACPSettings* Settings = UACPSettings::Get();
	if (Settings)
	{
		FString EffectivePrompt = Settings->GetProfileSystemPromptAppend();
		if (!EffectivePrompt.IsEmpty())
		{
			TSharedPtr<FJsonObject> SystemPrompt = MakeShared<FJsonObject>();
			SystemPrompt->SetStringField(TEXT("append"), EffectivePrompt);
			Meta->SetObjectField(TEXT("systemPrompt"), SystemPrompt);
			bHasMeta = true;

			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Adding custom system prompt to session (with profile instructions)"));
		}
	}

	// Add Claude Code options (thinking tokens, etc.)
	if (MaxThinkingTokens > 0)
	{
		TSharedPtr<FJsonObject> ClaudeCodeObj = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> OptionsObj = MakeShared<FJsonObject>();
		OptionsObj->SetNumberField(TEXT("maxThinkingTokens"), MaxThinkingTokens);
		ClaudeCodeObj->SetObjectField(TEXT("options"), OptionsObj);
		Meta->SetObjectField(TEXT("claudeCode"), ClaudeCodeObj);
		bHasMeta = true;

		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Setting maxThinkingTokens=%d for session"), MaxThinkingTokens);
	}

	if (bHasMeta)
	{
		Params->SetObjectField(TEXT("_meta"), Meta);
	}

	SendRequest(TEXT("session/new"), Params);
}

void FACPClient::LoadSession(const FString& SessionId, const FString& WorkingDirectory)
{
	// Set CurrentSessionId before sending — session/load response does not return sessionId
	CurrentSessionId = SessionId;

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), SessionId);

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(WorkingDirectory);
	// Strip trailing slash — session cwds don't have one
	while (AbsolutePath.Len() > 1 && (AbsolutePath.EndsWith(TEXT("/")) || AbsolutePath.EndsWith(TEXT("\\"))))
	{
		AbsolutePath.LeftChopInline(1);
	}
	Params->SetStringField(TEXT("cwd"), AbsolutePath);

	if (FMCPServer::Get().IsRunning())
	{
		TArray<TSharedPtr<FJsonValue>> McpServers;
		TSharedPtr<FJsonObject> UnrealMcp = MakeShared<FJsonObject>();
		UnrealMcp->SetStringField(TEXT("name"), TEXT("unreal-editor"));
		UnrealMcp->SetStringField(TEXT("type"), TEXT("http"));
		UnrealMcp->SetStringField(TEXT("url"),
			FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), FMCPServer::Get().GetPort()));
		TArray<TSharedPtr<FJsonValue>> EmptyHeaders;
		UnrealMcp->SetArrayField(TEXT("headers"), EmptyHeaders);
		McpServers.Add(MakeShared<FJsonValueObject>(UnrealMcp));
		Params->SetArrayField(TEXT("mcpServers"), McpServers);
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Loading session %s"), *SessionId);
	SendRequest(TEXT("session/load"), Params);
}

void FACPClient::ResumeSession(const FString& SessionId, const FString& WorkingDirectory)
{
	// Set CurrentSessionId before sending — the session/resume response does NOT return
	// a sessionId per ACP spec (the client already has it)
	CurrentSessionId = SessionId;

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), SessionId);

	FString AbsolutePath = FPaths::ConvertRelativePathToFull(WorkingDirectory);
	// Strip trailing slash — session cwds don't have one
	while (AbsolutePath.Len() > 1 && (AbsolutePath.EndsWith(TEXT("/")) || AbsolutePath.EndsWith(TEXT("\\"))))
	{
		AbsolutePath.LeftChopInline(1);
	}
	Params->SetStringField(TEXT("cwd"), AbsolutePath);

	if (FMCPServer::Get().IsRunning())
	{
		TArray<TSharedPtr<FJsonValue>> McpServers;
		TSharedPtr<FJsonObject> UnrealMcp = MakeShared<FJsonObject>();
		UnrealMcp->SetStringField(TEXT("name"), TEXT("unreal-editor"));
		UnrealMcp->SetStringField(TEXT("type"), TEXT("http"));
		UnrealMcp->SetStringField(TEXT("url"),
			FString::Printf(TEXT("http://127.0.0.1:%d/mcp"), FMCPServer::Get().GetPort()));
		TArray<TSharedPtr<FJsonValue>> EmptyHeaders;
		UnrealMcp->SetArrayField(TEXT("headers"), EmptyHeaders);
		McpServers.Add(MakeShared<FJsonValueObject>(UnrealMcp));
		Params->SetArrayField(TEXT("mcpServers"), McpServers);
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Resuming session %s"), *SessionId);
	SendRequest(TEXT("session/resume"), Params);
}

void FACPClient::SendPrompt(const FString& PromptText)
{
	SetState(EACPClientState::Prompting, TEXT("Processing..."));

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();

	// Session ID is required
	Params->SetStringField(TEXT("sessionId"), CurrentSessionId);

	// Build prompt content blocks
	TArray<TSharedPtr<FJsonValue>> Prompt;

	// Add attachment context blocks FIRST (before user text)
	TArray<TSharedPtr<FJsonValue>> AttachmentBlocks = FACPAttachmentManager::Get().SerializeForPrompt();
	if (AttachmentBlocks.Num() > 0)
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Adding %d attachment blocks to prompt"), AttachmentBlocks.Num());
		for (const TSharedPtr<FJsonValue>& Block : AttachmentBlocks)
		{
			Prompt.Add(Block);
		}
	}

	// Add user text block
	TSharedPtr<FJsonObject> TextBlock = MakeShared<FJsonObject>();
	TextBlock->SetStringField(TEXT("type"), TEXT("text"));
	TextBlock->SetStringField(TEXT("text"), PromptText);
	Prompt.Add(MakeShared<FJsonValueObject>(TextBlock));

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Sending prompt with %d content blocks"), Prompt.Num());

	Params->SetArrayField(TEXT("prompt"), Prompt);

	SendRequest(TEXT("session/prompt"), Params);

	// Clear attachments after sending (one-shot context)
	FACPAttachmentManager::Get().ClearAllAttachments();
}

void FACPClient::CancelPrompt()
{
	if (CurrentSessionId.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Cannot cancel - no active session"));
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), CurrentSessionId);

	// Cancel is a notification (no response expected)
	SendNotification(TEXT("session/cancel"), Params);
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Sent cancel request for session %s"), *CurrentSessionId);
}

void FACPClient::SetMode(const FString& ModeId)
{
	if (CurrentSessionId.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Cannot set mode - no active session"));
		return;
	}

	// Use unified config option for agents that support it (Codex), old method for others (Claude Code)
	if (bUsesConfigOptions)
	{
		SetConfigOption(TEXT("mode"), ModeId);
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), CurrentSessionId);
	Params->SetStringField(TEXT("modeId"), ModeId);

	SendRequest(TEXT("session/set_mode"), Params);
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Setting session mode to %s"), *ModeId);
}

void FACPClient::SetModel(const FString& ModelId)
{
	if (CurrentSessionId.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Cannot set model - no active session"));
		return;
	}

	// Gemini CLI ACP currently does not implement session/set_model or
	// session/set_config_option for model changes. Model is selected at launch.
	if (CurrentConfig.AgentName == TEXT("Gemini CLI") && !bUsesConfigOptions)
	{
		SessionModelState.CurrentModelId = ModelId;
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Gemini model set to '%s' for next new connection/session"), *ModelId);
		return;
	}

	// Use unified config option for agents that support it (Codex), old method for others (Claude Code)
	if (bUsesConfigOptions)
	{
		SetConfigOption(TEXT("model"), ModelId);
		SessionModelState.CurrentModelId = ModelId;
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), CurrentSessionId);
	Params->SetStringField(TEXT("modelId"), ModelId);

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Setting model to: %s"), *ModelId);
	SendRequest(TEXT("session/set_model"), Params);

	// Update local state
	SessionModelState.CurrentModelId = ModelId;
}

void FACPClient::SetMaxThinkingTokens(int32 Tokens)
{
	MaxThinkingTokens = Tokens;

	// Send to adapter if we have an active session
	if (!CurrentSessionId.IsEmpty())
	{
		FString Value;
		if (Tokens <= 0)       Value = TEXT("off");
		else if (Tokens <= 2000) Value = TEXT("low");
		else if (Tokens <= 4000) Value = TEXT("medium");
		else                     Value = TEXT("high");

		SetReasoningEffort(Value);
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Sent thinking level '%s' (tokens=%d) to adapter"), *Value, Tokens);
	}
}

void FACPClient::SetReasoningEffort(const FString& Value)
{
	if (CurrentSessionId.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Cannot set reasoning effort - no active session"));
		return;
	}

	if (!SupportsReasoningEffortControl())
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Ignoring reasoning effort '%s' - no reasoning config option available"), *Value);
		return;
	}

	if (!AvailableReasoningEfforts.Contains(Value))
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Ignoring unsupported reasoning effort '%s' for option '%s'"), *Value, *ReasoningConfigOptionId);
		return;
	}

	SetConfigOption(ReasoningConfigOptionId, Value);
}

void FACPClient::Authenticate(const FString& MethodId)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("methodId"), MethodId);

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Sending authenticate with method %s"), *MethodId);
	SendRequest(TEXT("authenticate"), Params);
}

void FACPClient::ListSessions(const FString& WorkingDirectory)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	if (!WorkingDirectory.IsEmpty())
	{
		FString FullCwd = FPaths::ConvertRelativePathToFull(WorkingDirectory);
		// Strip trailing slash — session cwd values don't have one
		while (FullCwd.Len() > 1 && (FullCwd.EndsWith(TEXT("/")) || FullCwd.EndsWith(TEXT("\\"))))
		{
			FullCwd.LeftChopInline(1);
		}
		Params->SetStringField(TEXT("cwd"), FullCwd);
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Listing sessions for cwd=%s"), *WorkingDirectory);
	SendRequest(TEXT("session/list"), Params);
}

void FACPClient::SetConfigOption(const FString& ConfigId, const FString& Value)
{
	if (CurrentSessionId.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Cannot set config option - no active session"));
		return;
	}

	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("sessionId"), CurrentSessionId);
	Params->SetStringField(TEXT("configId"), ConfigId);
	Params->SetStringField(TEXT("value"), Value);

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("ACPClient: Setting config option %s = %s"), *ConfigId, *Value);
	SendRequest(TEXT("session/set_config_option"), Params);
}

void FACPClient::HandleServerRequest(int32 Id, const FString& Method, TSharedPtr<FJsonObject> Params)
{
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Server request - Method: %s, Id: %d"), *Method, Id);

	if (Method == TEXT("session/request_permission"))
	{
		// Parse permission request
		FACPPermissionRequest PermRequest;
		PermRequest.RequestId = Id;

		if (Params->HasField(TEXT("sessionId")))
		{
			PermRequest.SessionId = Params->GetStringField(TEXT("sessionId"));
		}

		// Parse options
		if (Params->HasField(TEXT("options")))
		{
			const TArray<TSharedPtr<FJsonValue>>& OptionsArray = Params->GetArrayField(TEXT("options"));
			for (const TSharedPtr<FJsonValue>& OptionVal : OptionsArray)
			{
				TSharedPtr<FJsonObject> OptionObj = OptionVal->AsObject();
				if (OptionObj.IsValid())
				{
					FACPPermissionOption Option;
					OptionObj->TryGetStringField(TEXT("optionId"), Option.OptionId);
					OptionObj->TryGetStringField(TEXT("name"), Option.Name);
					OptionObj->TryGetStringField(TEXT("kind"), Option.Kind);
					PermRequest.Options.Add(Option);
				}
			}
		}

		// Parse tool call info
		if (Params->HasField(TEXT("toolCall")))
		{
			TSharedPtr<FJsonObject> ToolCallObj = Params->GetObjectField(TEXT("toolCall"));
			if (ToolCallObj.IsValid())
			{
				ToolCallObj->TryGetStringField(TEXT("toolCallId"), PermRequest.ToolCall.ToolCallId);
				ToolCallObj->TryGetStringField(TEXT("title"), PermRequest.ToolCall.Title);

				// Serialize rawInput to string
				if (ToolCallObj->HasField(TEXT("rawInput")))
				{
					TSharedPtr<FJsonObject> RawInput = ToolCallObj->GetObjectField(TEXT("rawInput"));
					if (RawInput.IsValid())
					{
						FString RawInputStr;
						TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RawInputStr);
						FJsonSerializer::Serialize(RawInput.ToSharedRef(), Writer);
						PermRequest.ToolCall.RawInput = RawInputStr;
					}
				}
			}
		}

		// Parse _meta for AskUserQuestion support
		if (Params->HasField(TEXT("_meta")))
		{
			TSharedPtr<FJsonObject> Meta = Params->GetObjectField(TEXT("_meta"));
			if (Meta.IsValid() && Meta->HasField(TEXT("askUserQuestion")))
			{
				PermRequest.bIsAskUserQuestion = true;
				TSharedPtr<FJsonObject> AskObj = Meta->GetObjectField(TEXT("askUserQuestion"));
				if (AskObj.IsValid() && AskObj->HasField(TEXT("questions")))
				{
					const TArray<TSharedPtr<FJsonValue>>& QuestionsArray = AskObj->GetArrayField(TEXT("questions"));
					for (const TSharedPtr<FJsonValue>& QVal : QuestionsArray)
					{
						TSharedPtr<FJsonObject> QObj = QVal->AsObject();
						if (!QObj.IsValid()) continue;

						FACPQuestion Question;
						QObj->TryGetStringField(TEXT("question"), Question.Question);
						QObj->TryGetStringField(TEXT("header"), Question.Header);
						QObj->TryGetBoolField(TEXT("multiSelect"), Question.bMultiSelect);

						if (QObj->HasField(TEXT("options")))
						{
							const TArray<TSharedPtr<FJsonValue>>& OptsArray = QObj->GetArrayField(TEXT("options"));
							for (const TSharedPtr<FJsonValue>& OptVal : OptsArray)
							{
								TSharedPtr<FJsonObject> OptObj = OptVal->AsObject();
								if (!OptObj.IsValid()) continue;

								FACPQuestionOption Opt;
								OptObj->TryGetStringField(TEXT("label"), Opt.Label);
								OptObj->TryGetStringField(TEXT("description"), Opt.Description);
								Question.Options.Add(Opt);
							}
						}

						PermRequest.Questions.Add(Question);
					}
				}
			}
		}

		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Permission request for tool: %s (AskUser=%d)"), *PermRequest.ToolCall.Title, PermRequest.bIsAskUserQuestion ? 1 : 0);

		// Broadcast to UI (already on game thread from ProcessLine dispatch)
		OnPermissionRequest.Broadcast(PermRequest);
	}
	else
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("ACPClient: Unknown server request method: %s"), *Method);
	}
}

void FACPClient::RespondToPermissionRequest(int32 RequestId, const FString& OptionId, TSharedPtr<FJsonObject> OutcomeMeta)
{
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Responding to permission request %d with option: %s"), RequestId, *OptionId);

	// Build JSON-RPC response
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	Response->SetNumberField(TEXT("id"), RequestId);

	// Build result with correct ACP format: { outcome: { outcome: "selected", optionId: "<id>" } }
	// See claude-code-acp/src/tests/acp-agent.test.ts for reference
	TSharedPtr<FJsonObject> OutcomeInner = MakeShared<FJsonObject>();
	OutcomeInner->SetStringField(TEXT("outcome"), TEXT("selected"));
	OutcomeInner->SetStringField(TEXT("optionId"), OptionId);

	// Attach _meta if provided (used for AskUserQuestion answers)
	if (OutcomeMeta.IsValid())
	{
		OutcomeInner->SetObjectField(TEXT("_meta"), OutcomeMeta);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetObjectField(TEXT("outcome"), OutcomeInner);

	Response->SetObjectField(TEXT("result"), Result);

	// Serialize and send
	FString ResponseStr;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ResponseStr);
	FJsonSerializer::Serialize(Response.ToSharedRef(), Writer);

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("ACPClient: Sending permission response: %s"), *ResponseStr);
	SendRawMessage(ResponseStr);
}
