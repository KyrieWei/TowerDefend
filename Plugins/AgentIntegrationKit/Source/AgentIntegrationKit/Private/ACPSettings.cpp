// Copyright 2026 Betide Studio. All Rights Reserved.

#include "ACPSettings.h"
#include "AgentInstaller.h"
#include "AgentIntegrationKitModule.h"
#include "MCPServer.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "ACPSettings"

static bool IsExecutableAvailable(const FString& ExecutablePath, FString& OutResolvedPath)
{
	return FAgentInstaller::Get().ResolveExecutable(ExecutablePath, OutResolvedPath);
}

static FString NormalizeOpenRouterBaseUrl(const FString& InBaseUrl)
{
	FString BaseUrl = InBaseUrl;
	BaseUrl.TrimStartAndEndInline();
	if (BaseUrl.IsEmpty())
	{
		BaseUrl = TEXT("https://openrouter.ai/api/v1");
	}

	while (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}

	if (BaseUrl.EndsWith(TEXT("/chat/completions")))
	{
		BaseUrl.LeftChopInline(FCString::Strlen(TEXT("/chat/completions")), EAllowShrinking::No);
	}
	else if (BaseUrl.EndsWith(TEXT("/models")))
	{
		BaseUrl.LeftChopInline(FCString::Strlen(TEXT("/models")), EAllowShrinking::No);
	}

	while (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}

	return BaseUrl;
}

static FString BuildOpenRouterUrl(const FString& BaseUrl, const TCHAR* EndpointPath)
{
	return NormalizeOpenRouterBaseUrl(BaseUrl) + EndpointPath;
}

static bool ResolveClaudeCodeExecutableForAdapter(const FFilePath& PreferredPath, FString& OutResolvedPath)
{
	if (!PreferredPath.FilePath.IsEmpty() && IsExecutableAvailable(PreferredPath.FilePath, OutResolvedPath))
	{
		return true;
	}

	if (FAgentInstaller::Get().ResolveExecutable(TEXT("claude-internal.cmd"), OutResolvedPath))
	{
		return true;
	}

	TArray<FString> CandidatePaths;
#if PLATFORM_WINDOWS
	const FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT("AppData/Roaming/npm/claude-internal.cmd")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/claude.exe")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/claude.cmd")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/claude")));
	}
#else
	const FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!HomeDir.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(HomeDir, TEXT(".local/bin/claude")));
	}
#endif

	for (FString Candidate : CandidatePaths)
	{
		FPaths::NormalizeFilename(Candidate);
		if (IFileManager::Get().FileExists(*Candidate))
		{
			OutResolvedPath = Candidate;
			return true;
		}
	}

	return false;
}

UACPSettings::UACPSettings()
{
	EnsureBuiltInProfiles();
}

#if WITH_EDITOR
FText UACPSettings::GetSectionText() const
{
	return LOCTEXT("SectionText", "Agent Integration Kit");
}

FText UACPSettings::GetSectionDescription() const
{
	return LOCTEXT("SectionDescription", "Configure AI agent connections and API keys for the Agent Integration Kit.");
}

void UACPSettings::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	SaveConfig();

	// Broadcast font size change
	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UACPSettings, ChatFontSize))
	{
		OnChatFontSizeChanged.Broadcast();
	}

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(UACPSettings, MCPServerPort))
	{
		if (FMCPServer::Get().IsRunning())
		{
			const int32 RequestedPort = FMath::Clamp(MCPServerPort, 1, 65535);
			FMCPServer::Get().Stop();
			FMCPServer::Get().Start(RequestedPort);
		}
	}
}
#endif

UACPSettings* UACPSettings::Get()
{
	return GetMutableDefault<UACPSettings>();
}

FString UACPSettings::GetWorkingDirectory()
{
	if (const UACPSettings* Settings = Get())
	{
		if (!Settings->ClaudeCodeWorkingDirectory.Path.IsEmpty())
		{
			return Settings->ClaudeCodeWorkingDirectory.Path;
		}
	}
	return FPaths::ProjectDir();
}

// Helper: build agent config for agents with bundled adapters (runs via Bun).
// Connect() handles the actual Bun + adapter resolution. GetAgentConfigs() just
// sets status based on whether the adapter and (optional) base CLI are available.
static FACPAgentConfig BuildBundledAdapterConfig(
	const FString& AgentName,
	const FAgentInstallInfo& InstallInfo,
	const FFilePath& UserPathOverride,
	const TArray<FString>& ExtraArguments = {},
	const FFilePath& BaseCliPathOverride = {})
{
	FACPAgentConfig Config;
	Config.AgentName = AgentName;
	Config.bIsBuiltIn = false;
	Config.InstallInstructions = InstallInfo.GetBaseInstallCommand();
	Config.WorkingDirectory = UACPSettings::GetWorkingDirectory();
	Config.Arguments = ExtraArguments;

	bool bUserSpecifiedPath = !UserPathOverride.FilePath.IsEmpty();

	if (bUserSpecifiedPath)
	{
		// Power user: custom path takes full priority
		Config.ExecutablePath = UserPathOverride.FilePath;
		FString ResolvedPath;
		if (IsExecutableAvailable(Config.ExecutablePath, ResolvedPath))
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready (custom path)");
		}
		else
		{
			Config.Status = EACPAgentStatus::NotInstalled;
			Config.StatusMessage = FString::Printf(TEXT("Specified path not found: %s"), *UserPathOverride.FilePath);
		}
		return Config;
	}

	// Check adapter entry point (bundled or managed)
	bool bHasAdapter = false;
	if (InstallInfo.RequiresAdapter())
	{
		FString AdapterEntry = FAgentInstaller::GetAdapterEntryPoint(InstallInfo);
		bHasAdapter = !AdapterEntry.IsEmpty();
	}

	// Check bundled Bun runtime
	bool bHasBun = !FAgentInstaller::GetBundledBunPath().IsEmpty();

	// Check base CLI if required
	bool bHasBaseCli = true;
	if (InstallInfo.RequiresBaseCLI())
	{
		FString ResolvedPath;
		bHasBaseCli = false;
		if (!BaseCliPathOverride.FilePath.IsEmpty())
		{
			bHasBaseCli = IsExecutableAvailable(BaseCliPathOverride.FilePath, ResolvedPath);
		}
		if (!bHasBaseCli)
		{
			bHasBaseCli = FAgentInstaller::Get().ResolveExecutable(InstallInfo.BaseExecutableName, ResolvedPath);
		}
	}

	// ExecutablePath is intentionally left empty for bundled adapter agents —
	// Connect() resolves it to the bundled Bun path at connection time
	Config.ExecutablePath = TEXT("");

	// Determine status
	if (!bHasBun && InstallInfo.RequiresAdapter() && !InstallInfo.bAdapterIsNativeBinary)
	{
		Config.Status = EACPAgentStatus::NotInstalled;
		Config.StatusMessage = TEXT("Bundled runtime missing. Try reinstalling Agent Integration Kit.");
	}
	else if (!bHasAdapter && InstallInfo.RequiresAdapter())
	{
		Config.Status = EACPAgentStatus::NotInstalled;
		Config.StatusMessage = TEXT("Adapter not installed. Click Install to set it up.");
	}
	else if (!bHasBaseCli && InstallInfo.RequiresBaseCLI())
	{
		Config.Status = EACPAgentStatus::NotInstalled;
		Config.StatusMessage = FString::Printf(TEXT("%s CLI not installed. %s"),
			*InstallInfo.BaseExecutableName, *InstallInfo.GetBaseInstallCommand());
	}
	else
	{
		Config.Status = EACPAgentStatus::Available;
		Config.StatusMessage = TEXT("Ready");
	}

	return Config;
}

TArray<FACPAgentConfig> UACPSettings::GetAgentConfigs() const
{
	TArray<FACPAgentConfig> Configs;
	FString ResolvedPath;

	// OpenRouter (built-in, native C++ - no external executable required)
	{
		FACPAgentConfig Config;
		Config.AgentName = TEXT("OpenRouter");
		Config.bIsBuiltIn = true;
		Config.InstallInstructions = TEXT("Built-in agent. Configure Betide API token (credits mode) or OpenRouter API key.");
		Config.ExecutablePath = TEXT("");
		Config.ApiKey = GetOpenRouterAuthToken();
		Config.ModelId = OpenRouterDefaultModel;
		Config.WorkingDirectory = UACPSettings::GetWorkingDirectory();

		if (!HasOpenRouterAuth())
		{
			Config.Status = EACPAgentStatus::MissingApiKey;
			Config.StatusMessage = ShouldUseBetideCredits()
				? TEXT("Betide API token not configured. Set it in Project Settings or BETIDE_API_TOKEN env var.")
				: TEXT("OpenRouter API key not configured. Set it in Project Settings > Plugins > Agent Integration Kit.");
		}
		else
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready");
		}
		Configs.Add(Config);
	}

	// Claude Code (bundled adapter + requires Claude CLI)
	{
		FAgentInstallInfo Info = FAgentInstaller::GetAgentInstallInfo(TEXT("Claude Code"));
		FACPAgentConfig Config = BuildBundledAdapterConfig(
			TEXT("Claude Code"),
			Info,
			ClaudeCodePath,
			{},
			ClaudeCodeExecutablePath);

		if (!ClaudeCodeWorkingDirectory.Path.IsEmpty())
		{
			Config.WorkingDirectory = ClaudeCodeWorkingDirectory.Path;
		}

		FString ResolvedClaudeExecutable;
		if (ResolveClaudeCodeExecutableForAdapter(ClaudeCodeExecutablePath, ResolvedClaudeExecutable))
		{
			Config.EnvironmentVariables.Add(TEXT("CLAUDE_CODE_EXECUTABLE"), ResolvedClaudeExecutable);
		}

		Configs.Add(Config);
	}

	// Gemini CLI (native ACP via --experimental-acp flag, no adapter needed)
	{
		const FAgentInstallInfo GeminiInstallInfo = FAgentInstaller::GetAgentInstallInfo(TEXT("Gemini CLI"));
		FACPAgentConfig Config;
		Config.AgentName = TEXT("Gemini CLI");
		Config.bIsBuiltIn = false;
		Config.InstallInstructions = GeminiInstallInfo.GetBaseInstallCommand();

		bool bUserSpecifiedPath = !GeminiCliPath.FilePath.IsEmpty();
		bool bFound = false;

		if (bUserSpecifiedPath)
		{
			Config.ExecutablePath = GeminiCliPath.FilePath;
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
		}
		else
		{
#if PLATFORM_WINDOWS
			if (IsExecutableAvailable(TEXT("gemini.cmd"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else if (IsExecutableAvailable(TEXT("gemini"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else
			{
				Config.ExecutablePath = TEXT("gemini.cmd");
			}
#else
			Config.ExecutablePath = TEXT("gemini");
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
			if (bFound)
			{
				Config.ExecutablePath = ResolvedPath;
			}
#endif
		}

		Config.Arguments.Add(TEXT("--experimental-acp"));
		Config.WorkingDirectory = UACPSettings::GetWorkingDirectory();

		if (bFound)
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready");
		}
		else
		{
			Config.Status = EACPAgentStatus::NotInstalled;
			Config.StatusMessage = bUserSpecifiedPath
				? FString::Printf(TEXT("Specified path not found: %s"), *GeminiCliPath.FilePath)
				: FString::Printf(TEXT("Not installed. Run: %s"), *GeminiInstallInfo.GetBaseInstallCommand());
		}
		Configs.Add(Config);
	}

	// Codex CLI (npm adapter + requires Codex base CLI)
	{
		FAgentInstallInfo Info = FAgentInstaller::GetAgentInstallInfo(TEXT("Codex CLI"));
		Configs.Add(BuildBundledAdapterConfig(TEXT("Codex CLI"), Info, CodexCliPath));
	}

	// OpenCode (ACP-compatible Go binary, no adapter needed)
	{
		const FAgentInstallInfo OpenCodeInstallInfo = FAgentInstaller::GetAgentInstallInfo(TEXT("OpenCode"));
		FACPAgentConfig Config;
		Config.AgentName = TEXT("OpenCode");
		Config.bIsBuiltIn = false;
		Config.InstallInstructions = OpenCodeInstallInfo.GetBaseInstallCommand();

		bool bUserSpecifiedPath = !OpenCodePath.FilePath.IsEmpty();
		bool bFound = false;

		if (bUserSpecifiedPath)
		{
			Config.ExecutablePath = OpenCodePath.FilePath;
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
		}
		else
		{
#if PLATFORM_WINDOWS
			if (IsExecutableAvailable(TEXT("opencode.cmd"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else if (IsExecutableAvailable(TEXT("opencode"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else
			{
			Config.ExecutablePath = TEXT("opencode.cmd");
		}
#else
			Config.ExecutablePath = TEXT("opencode");
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
			if (bFound)
			{
				Config.ExecutablePath = ResolvedPath;
			}
#endif
		}

		Config.Arguments.Add(TEXT("acp"));
		Config.WorkingDirectory = UACPSettings::GetWorkingDirectory();

		if (bFound)
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready");
		}
		else
		{
			Config.Status = EACPAgentStatus::NotInstalled;
			Config.StatusMessage = bUserSpecifiedPath
				? FString::Printf(TEXT("Specified path not found: %s"), *OpenCodePath.FilePath)
				: FString::Printf(TEXT("Not installed. Run: %s"), *OpenCodeInstallInfo.GetBaseInstallCommand());
		}
		Configs.Add(Config);
	}

	// Cursor Agent (community ACP adapter + requires cursor-agent CLI)
	{
		FAgentInstallInfo Info = FAgentInstaller::GetAgentInstallInfo(TEXT("Cursor Agent"));
		Configs.Add(BuildBundledAdapterConfig(TEXT("Cursor Agent"), Info, CursorAgentPath));
	}

	// Kimi CLI (native ACP via "acp" subcommand, no adapter needed)
	{
		const FAgentInstallInfo KimiInstallInfo = FAgentInstaller::GetAgentInstallInfo(TEXT("Kimi CLI"));
		FACPAgentConfig Config;
		Config.AgentName = TEXT("Kimi CLI");
		Config.bIsBuiltIn = false;
		Config.InstallInstructions = KimiInstallInfo.GetBaseInstallCommand();

		bool bUserSpecifiedPath = !KimiCliPath.FilePath.IsEmpty();
		bool bFound = false;

		if (bUserSpecifiedPath)
		{
			Config.ExecutablePath = KimiCliPath.FilePath;
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
		}
		else
		{
#if PLATFORM_WINDOWS
			if (IsExecutableAvailable(TEXT("kimi.cmd"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else if (IsExecutableAvailable(TEXT("kimi"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else
			{
			Config.ExecutablePath = TEXT("kimi.cmd");
		}
#else
			Config.ExecutablePath = TEXT("kimi");
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
			if (bFound)
			{
				Config.ExecutablePath = ResolvedPath;
			}
#endif
		}

		Config.Arguments.Add(TEXT("acp"));
		Config.WorkingDirectory = UACPSettings::GetWorkingDirectory();

		if (bFound)
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready");
		}
		else
		{
			Config.Status = EACPAgentStatus::NotInstalled;
			Config.StatusMessage = bUserSpecifiedPath
				? FString::Printf(TEXT("Specified path not found: %s"), *KimiCliPath.FilePath)
				: FString::Printf(TEXT("Not installed. Run: %s"), *KimiInstallInfo.GetBaseInstallCommand());
		}
		Configs.Add(Config);
	}

	// Copilot CLI (native ACP via --acp flag, no adapter needed)
	{
		const FAgentInstallInfo CopilotInstallInfo = FAgentInstaller::GetAgentInstallInfo(TEXT("Copilot CLI"));
		FACPAgentConfig Config;
		Config.AgentName = TEXT("Copilot CLI");
		Config.bIsBuiltIn = false;
		Config.InstallInstructions = CopilotInstallInfo.GetBaseInstallCommand();

		bool bUserSpecifiedPath = !CopilotCliPath.FilePath.IsEmpty();
		bool bFound = false;

		if (bUserSpecifiedPath)
		{
			Config.ExecutablePath = CopilotCliPath.FilePath;
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
		}
		else
		{
#if PLATFORM_WINDOWS
			if (IsExecutableAvailable(TEXT("copilot.cmd"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else if (IsExecutableAvailable(TEXT("copilot"), ResolvedPath))
			{
				Config.ExecutablePath = ResolvedPath;
				bFound = true;
			}
			else
			{
				Config.ExecutablePath = TEXT("copilot.cmd");
			}
#else
			Config.ExecutablePath = TEXT("copilot");
			bFound = IsExecutableAvailable(Config.ExecutablePath, ResolvedPath);
			if (bFound)
			{
				Config.ExecutablePath = ResolvedPath;
			}
#endif
		}

		Config.Arguments.Add(TEXT("--acp"));
		Config.WorkingDirectory = UACPSettings::GetWorkingDirectory();

		if (bFound)
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready");
		}
		else
		{
			Config.Status = EACPAgentStatus::NotInstalled;
			Config.StatusMessage = bUserSpecifiedPath
				? FString::Printf(TEXT("Specified path not found: %s"), *CopilotCliPath.FilePath)
				: FString::Printf(TEXT("Not installed. Run: %s"), *CopilotInstallInfo.GetBaseInstallCommand());
		}
		Configs.Add(Config);
	}

	// Custom agents from settings
	for (const FACPAgentSettingsEntry& Entry : CustomAgents)
	{
		if (Entry.AgentName.IsEmpty() || Entry.ExecutablePath.FilePath.IsEmpty())
		{
			continue;
		}

		FACPAgentConfig Config;
		Config.AgentName = Entry.AgentName;
		Config.ExecutablePath = Entry.ExecutablePath.FilePath;
		Config.Arguments = Entry.Arguments;
		Config.WorkingDirectory = Entry.WorkingDirectory.Path.IsEmpty() ? UACPSettings::GetWorkingDirectory() : Entry.WorkingDirectory.Path;
		Config.EnvironmentVariables = Entry.EnvironmentVariables;
		Config.ApiKey = Entry.ApiKey;
		Config.ModelId = Entry.ModelId;
		Config.bIsBuiltIn = false;
		Config.InstallInstructions = TEXT("Custom agent - check your configuration.");

		if (IsExecutableAvailable(Config.ExecutablePath, ResolvedPath))
		{
			Config.Status = EACPAgentStatus::Available;
			Config.StatusMessage = TEXT("Ready");
		}
		else
		{
			Config.Status = EACPAgentStatus::NotInstalled;
			Config.StatusMessage = FString::Printf(TEXT("Executable not found: %s"), *Entry.ExecutablePath.FilePath);
		}
		Configs.Add(Config);
	}

	return Configs;
}

FString UACPSettings::GetSavedModelForAgent(const FString& AgentName) const
{
	if (const FString* SavedModel = SelectedModelPerAgent.Find(AgentName))
	{
		return *SavedModel;
	}
	return FString();
}

void UACPSettings::SaveModelForAgent(const FString& AgentName, const FString& ModelId)
{
	if (ModelId.IsEmpty())
	{
		SelectedModelPerAgent.Remove(AgentName);
	}
	else
	{
		SelectedModelPerAgent.Add(AgentName, ModelId);
	}

	// Save to config immediately
	SaveConfig();
}

FString UACPSettings::GetSavedModeForAgent(const FString& AgentName) const
{
	if (const FString* SavedMode = SelectedModePerAgent.Find(AgentName))
	{
		return *SavedMode;
	}
	return FString();
}

void UACPSettings::SaveModeForAgent(const FString& AgentName, const FString& ModeId)
{
	if (ModeId.IsEmpty())
	{
		SelectedModePerAgent.Remove(AgentName);
	}
	else
	{
		SelectedModePerAgent.Add(AgentName, ModeId);
	}

	// Save to config immediately
	SaveConfig();
}

FString UACPSettings::GetSavedReasoningForAgent(const FString& AgentName) const
{
	if (const FString* SavedReasoning = SelectedReasoningPerAgent.Find(AgentName))
	{
		return *SavedReasoning;
	}
	return FString();
}

void UACPSettings::SaveReasoningForAgent(const FString& AgentName, const FString& ReasoningLevel)
{
	if (ReasoningLevel.IsEmpty())
	{
		SelectedReasoningPerAgent.Remove(AgentName);
	}
	else
	{
		SelectedReasoningPerAgent.Add(AgentName, ReasoningLevel);
	}

	// Save to config immediately
	SaveConfig();
}

bool UACPSettings::IsToolEnabled(const FString& ToolName) const
{
	// Global disable always wins
	if (DisabledTools.Contains(ToolName))
	{
		return false;
	}

	// If an active profile has a non-empty EnabledTools whitelist, check it
	if (const FAgentProfile* Profile = GetActiveProfile())
	{
		if (Profile->EnabledTools.Num() > 0)
		{
			return Profile->EnabledTools.Contains(ToolName);
		}
	}

	return true;
}

void UACPSettings::SetToolEnabled(const FString& ToolName, bool bEnabled)
{
	if (bEnabled)
	{
		DisabledTools.Remove(ToolName);
	}
	else
	{
		DisabledTools.Add(ToolName);
	}
	SaveConfig();
}

bool UACPSettings::ShouldUseBetideCredits() const
{
	return bUseBetideCredits;
}

FString UACPSettings::GetBetideApiToken() const
{
	FString Token = BetideApiToken.TrimStartAndEnd();
	if (!Token.IsEmpty())
	{
		return Token;
	}

	// Allow a machine/user-level token that works across projects and engine installs.
	FString EnvToken = FPlatformMisc::GetEnvironmentVariable(TEXT("BETIDE_API_TOKEN")).TrimStartAndEnd();
	if (!EnvToken.IsEmpty())
	{
		return EnvToken;
	}

	// Backward-compatible alias.
	return FPlatformMisc::GetEnvironmentVariable(TEXT("NEOSTACK_API_TOKEN")).TrimStartAndEnd();
}

bool UACPSettings::HasOpenRouterAuth() const
{
	return ShouldUseBetideCredits()
		? !GetBetideApiToken().IsEmpty()
		: !OpenRouterApiKey.TrimStartAndEnd().IsEmpty();
}

bool UACPSettings::HasMeshyAuth() const
{
	return ShouldUseBetideCredits()
		? !GetBetideApiToken().IsEmpty()
		: !MeshyApiKey.TrimStartAndEnd().IsEmpty();
}

bool UACPSettings::HasFalAuth() const
{
	return ShouldUseBetideCredits()
		? !GetBetideApiToken().IsEmpty()
		: !FalApiKey.TrimStartAndEnd().IsEmpty();
}

FString UACPSettings::GetOpenRouterAuthToken() const
{
	return ShouldUseBetideCredits() ? GetBetideApiToken() : OpenRouterApiKey;
}

FString UACPSettings::GetMeshyAuthToken() const
{
	return ShouldUseBetideCredits() ? GetBetideApiToken() : MeshyApiKey;
}

FString UACPSettings::GetFalAuthToken() const
{
	return ShouldUseBetideCredits() ? GetBetideApiToken() : FalApiKey;
}

FString UACPSettings::GetOpenRouterChatCompletionsUrl() const
{
	return ShouldUseBetideCredits()
		? TEXT("https://betide.studio/api/proxy/chat")
		: BuildOpenRouterUrl(OpenRouterBaseUrl, TEXT("/chat/completions"));
}

FString UACPSettings::GetOpenRouterImageGenerationUrl() const
{
	return ShouldUseBetideCredits()
		? TEXT("https://betide.studio/api/proxy/image")
		: BuildOpenRouterUrl(OpenRouterBaseUrl, TEXT("/chat/completions"));
}

FString UACPSettings::GetImageGenerationBaseUrl() const
{
	FString BaseUrl = ImageGenerationBaseUrl.TrimStartAndEnd();
	if (BaseUrl.IsEmpty())
	{
		BaseUrl = TEXT("http://api-skynetyu.woa.com/v1");
	}
	while (BaseUrl.EndsWith(TEXT("/")))
	{
		BaseUrl.LeftChopInline(1, EAllowShrinking::No);
	}
	return BaseUrl;
}

FString UACPSettings::GetImageGenerationApiKey() const
{
	const FString ImageKey = ImageGenerationApiKey.TrimStartAndEnd();
	if (!ImageKey.IsEmpty())
	{
		return ImageKey;
	}
	// Fall back to OpenRouter API key
	return OpenRouterApiKey.TrimStartAndEnd();
}

FString UACPSettings::GetOpenRouterModelsUrl() const
{
	return ShouldUseBetideCredits()
		? TEXT("https://openrouter.ai/api/v1/models")
		: BuildOpenRouterUrl(OpenRouterBaseUrl, TEXT("/models"));
}

FString UACPSettings::GetMeshyBaseUrl() const
{
	return ShouldUseBetideCredits()
		? TEXT("https://betide.studio/api/proxy/meshy")
		: TEXT("https://api.meshy.ai");
}

FString UACPSettings::GetFalSubmitUrl() const
{
	return ShouldUseBetideCredits()
		? TEXT("https://betide.studio/api/proxy/fal/submit")
		: TEXT("https://queue.fal.run");
}

FString UACPSettings::GetFalStatusProxyUrl() const
{
	return TEXT("https://betide.studio/api/proxy/fal/status");
}

FString UACPSettings::GetFalResultProxyUrl() const
{
	return TEXT("https://betide.studio/api/proxy/fal/result");
}

FString UACPSettings::GetFalCancelProxyUrl() const
{
	return TEXT("https://betide.studio/api/proxy/fal/cancel");
}

// ============================================
// Profile Management
// ============================================

const FAgentProfile* UACPSettings::GetActiveProfile() const
{
	if (ActiveProfileId.IsEmpty())
	{
		return nullptr;
	}
	return FindProfileById(ActiveProfileId);
}

const FAgentProfile* UACPSettings::FindProfileById(const FString& ProfileId) const
{
	for (const FAgentProfile& Profile : Profiles)
	{
		if (Profile.ProfileId == ProfileId)
		{
			return &Profile;
		}
	}
	return nullptr;
}

FAgentProfile* UACPSettings::FindProfileByIdMutable(const FString& ProfileId)
{
	for (FAgentProfile& Profile : Profiles)
	{
		if (Profile.ProfileId == ProfileId)
		{
			return &Profile;
		}
	}
	return nullptr;
}

void UACPSettings::SetActiveProfile(const FString& ProfileId)
{
	ActiveProfileId = ProfileId;
	SaveConfig();
}

void UACPSettings::AddCustomProfile(const FAgentProfile& Profile)
{
	Profiles.Add(Profile);
	SaveConfig();
}

void UACPSettings::RemoveCustomProfile(const FString& ProfileId)
{
	Profiles.RemoveAll([&ProfileId](const FAgentProfile& P)
	{
		return P.ProfileId == ProfileId && !P.bIsBuiltIn;
	});

	// If the removed profile was active, clear the selection
	if (ActiveProfileId == ProfileId)
	{
		ActiveProfileId.Empty();
	}
	SaveConfig();
}

FString UACPSettings::GetEffectiveToolDescription(const FString& ToolName, const FString& DefaultDescription) const
{
	if (const FAgentProfile* Profile = GetActiveProfile())
	{
		if (const FString* Override = Profile->ToolDescriptionOverrides.Find(ToolName))
		{
			if (!Override->IsEmpty())
			{
				return *Override;
			}
		}
	}
	return DefaultDescription;
}

FString UACPSettings::GetProfileSystemPromptAppend() const
{
	FString Result = ACPSystemPromptAppend;

	if (const FAgentProfile* Profile = GetActiveProfile())
	{
		if (!Profile->CustomInstructions.IsEmpty())
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT("\n\n");
			}
			Result += TEXT("=== ACTIVE PROFILE: ") + Profile->DisplayName + TEXT(" ===\n");
			Result += Profile->CustomInstructions;
		}
	}

	return Result;
}

void UACPSettings::EnsureBuiltInProfiles()
{
	auto HasProfile = [this](const FString& Id) -> bool
	{
		return FindProfileById(Id) != nullptr;
	};

	// --- Full Toolkit (all tools, no specialization) ---
	if (!HasProfile(TEXT("builtin_full")))
	{
		FAgentProfile P;
		P.ProfileId = TEXT("builtin_full");
		P.DisplayName = TEXT("Full Toolkit");
		P.Description = TEXT("All tools enabled, no specialization");
		P.bIsBuiltIn = true;
		// Empty EnabledTools = all tools
		Profiles.Add(MoveTemp(P));
	}

	// --- Animation ---
	if (!HasProfile(TEXT("builtin_animation")))
	{
		FAgentProfile P;
		P.ProfileId = TEXT("builtin_animation");
		P.DisplayName = TEXT("Animation");
		P.Description = TEXT("Motion matching, IK, retargeting, montages, anim blueprints");
		P.bIsBuiltIn = true;
		P.EnabledTools = {
			TEXT("execute_python"),
			TEXT("read_asset"),
			TEXT("edit_blueprint"),
			TEXT("edit_rigging"),
			TEXT("edit_animation_asset"),
			TEXT("edit_character_asset"),
			TEXT("edit_graph"),
			TEXT("read_logs"),
			TEXT("screenshot")
		};
		P.CustomInstructions = TEXT(
			"You are working in an ANIMATION-focused context.\n"
			"- edit_blueprint: Focus on Animation Blueprints (AnimGraphs, State Machines, Blend Spaces)\n"
			"- Use motion matching (Pose Search) for locomotion, not traditional state machines when appropriate\n"
			"- When setting up retargeting: create IK Rig first, then IK Retargeter\n"
			"- Always read_asset before modifying any animation asset to understand its current state"
		);
		P.ToolDescriptionOverrides.Add(TEXT("edit_blueprint"),
			TEXT("Edit Animation Blueprint graphs including AnimGraphs, State Machines, Blend Space players, "
				 "and locomotion logic. Add anim nodes, configure transitions, and set up motion matching choosers."));
		Profiles.Add(MoveTemp(P));
	}

	// --- Blueprint & Gameplay ---
	if (!HasProfile(TEXT("builtin_blueprint")))
	{
		FAgentProfile P;
		P.ProfileId = TEXT("builtin_blueprint");
		P.DisplayName = TEXT("Blueprint & Gameplay");
		P.Description = TEXT("Blueprint logic, components, gameplay systems, enhanced input");
		P.bIsBuiltIn = true;
		P.EnabledTools = {
			TEXT("execute_python"),
			TEXT("read_asset"),
			TEXT("edit_blueprint"),
			TEXT("edit_graph"),
			TEXT("edit_ai_tree"),
			TEXT("edit_data_structure"),
			TEXT("read_logs"),
			TEXT("screenshot")
		};
		P.CustomInstructions = TEXT(
			"You are working in a GAMEPLAY/BLUEPRINT-focused context.\n"
			"- Prioritize clean Blueprint architecture: use interfaces, components, and event dispatchers\n"
			"- For AI characters, use Behavior Trees or State Trees as appropriate\n"
			"- Set up Enhanced Input for all player controls\n"
			"- Always compile after editing and check read_logs for errors"
		);
		Profiles.Add(MoveTemp(P));
	}

	// --- Cinematics ---
	if (!HasProfile(TEXT("builtin_cinematics")))
	{
		FAgentProfile P;
		P.ProfileId = TEXT("builtin_cinematics");
		P.DisplayName = TEXT("Cinematics");
		P.Description = TEXT("Level Sequences, camera work, animation playback");
		P.bIsBuiltIn = true;
		P.EnabledTools = {
			TEXT("execute_python"),
			TEXT("read_asset"),
			TEXT("edit_sequencer"),
			TEXT("edit_animation_asset"),
			TEXT("edit_graph"),
			TEXT("read_logs"),
			TEXT("screenshot")
		};
		P.CustomInstructions = TEXT(
			"You are working in a CINEMATICS/SEQUENCER-focused context.\n"
			"- Use edit_sequencer for Level Sequence editing (camera cuts, transforms, keyframes)\n"
			"- Use list_track_types and list_bindings to discover available options dynamically before editing\n"
			"- Build shots in passes like a human editor: rough cut pass, visual review pass, refinement pass\n"
			"- Use execute_shot_plan to block first-pass shots from beats, then refine with manual sequencer edits\n"
			"- Use analyze_camera_cuts after each pass to check pacing, gaps/overlaps, repeated angles, and review timestamps\n"
			"- For character animation in sequences, bind skeletal mesh actors and add animation tracks\n"
			"- Use screenshot at multiple moments per shot (start/middle/end), then adjust transforms/cuts and iterate\n"
			"- Avoid repetitive camera placement patterns; vary shot size, angle, and duration based on scene intent"
		);
		Profiles.Add(MoveTemp(P));
	}

	// --- VFX & Materials ---
	if (!HasProfile(TEXT("builtin_vfx")))
	{
		FAgentProfile P;
		P.ProfileId = TEXT("builtin_vfx");
		P.DisplayName = TEXT("VFX & Materials");
		P.Description = TEXT("Niagara particles, Material graphs, visual effects");
		P.bIsBuiltIn = true;
		P.EnabledTools = {
			TEXT("execute_python"),
			TEXT("read_asset"),
			TEXT("edit_niagara"),
			TEXT("edit_graph"),
			TEXT("read_logs"),
			TEXT("screenshot"),
			TEXT("generate_asset")
		};
		P.CustomInstructions = TEXT(
			"You are working in a VFX/MATERIALS-focused context.\n"
			"- edit_graph: Use for Material and PCG graph editing\n"
			"- edit_niagara: Use for particle system creation and modification\n"
			"- Use edit_graph with operation='find_nodes' to discover available Material expression and Niagara module node types\n"
			"- generate_asset with asset_type='image' can create textures for use in materials"
		);
		P.ToolDescriptionOverrides.Add(TEXT("edit_graph"),
			TEXT("Edit Material graphs and PCG graphs. Create material expressions, connect nodes for "
				 "shader logic, and build procedural generation graphs. Use operation='find_nodes' first to discover "
				 "available node types."));
		Profiles.Add(MoveTemp(P));
	}
}

void UACPSettings::RefreshAgentStatus()
{
	FScopeLock Lock(&StatusCacheLock);
	CachedAgentStatus.Empty();
	LastStatusRefresh = FDateTime::UtcNow();

	TArray<FACPAgentConfig> Configs = GetAgentConfigs();
	for (const FACPAgentConfig& Config : Configs)
	{
		CachedAgentStatus.Add(Config.AgentName, Config.Status);
	}
}

void UACPSettings::InvalidateAgentStatusCache()
{
	FScopeLock Lock(&StatusCacheLock);
	CachedAgentStatus.Empty();
	LastStatusRefresh = FDateTime::MinValue();
}

bool UACPSettings::IsAgentStatusStale() const
{
	FScopeLock Lock(&StatusCacheLock);
	if (CachedAgentStatus.Num() == 0)
	{
		return true;
	}
	FDateTime Now = FDateTime::UtcNow();
	return (Now - LastStatusRefresh).GetTotalSeconds() > StatusCacheTTLSeconds;
}

#undef LOCTEXT_NAMESPACE
