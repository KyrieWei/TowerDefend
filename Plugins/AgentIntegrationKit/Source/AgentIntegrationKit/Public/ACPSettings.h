// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "ACPTypes.h"
#include "ACPSettings.generated.h"

DECLARE_MULTICAST_DELEGATE(FOnChatFontSizeChanged);

/** Where the chat WebUI is loaded from */
UENUM()
enum class EWebUISource : uint8
{
	/** Load from hosted CDN (faster updates, requires internet) */
	Hosted		UMETA(DisplayName = "Hosted (Recommended)"),
	/** Load from the local build bundled with the plugin */
	Local		UMETA(DisplayName = "Local"),
};

/**
 * Agent configuration stored in settings
 */
USTRUCT(BlueprintType)
struct FACPAgentSettingsEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Agent")
	FString AgentName;

	UPROPERTY(EditAnywhere, Category = "Agent", meta = (FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath ExecutablePath;

	UPROPERTY(EditAnywhere, Category = "Agent")
	TArray<FString> Arguments;

	UPROPERTY(EditAnywhere, Category = "Agent", meta = (RelativeToGameDir))
	FDirectoryPath WorkingDirectory;

	UPROPERTY(EditAnywhere, Category = "Agent")
	TMap<FString, FString> EnvironmentVariables;

	// For agents that need API keys
	UPROPERTY(EditAnywhere, Category = "Agent", meta = (PasswordField = true))
	FString ApiKey;

	// Model ID for agents that support model selection
	UPROPERTY(EditAnywhere, Category = "Agent")
	FString ModelId;
};

/**
 * Agent profile — a named configuration that controls which tools are exposed
 * and provides specialized instructions/description overrides per domain.
 */
USTRUCT()
struct FAgentProfile
{
	GENERATED_BODY()

	/** Stable internal key (e.g., "builtin_animation" or a GUID for custom profiles) */
	UPROPERTY(config)
	FString ProfileId;

	/** User-facing display name */
	UPROPERTY(config)
	FString DisplayName;

	/** Short description shown in tooltips */
	UPROPERTY(config)
	FString Description;

	/** Built-in profiles can be edited but not deleted */
	UPROPERTY(config)
	bool bIsBuiltIn = false;

	/** Whitelist of enabled tool names. Empty set = all tools enabled. */
	UPROPERTY(config)
	TSet<FString> EnabledTools;

	/** Custom instructions appended to the system prompt when this profile is active */
	UPROPERTY(config)
	FString CustomInstructions;

	/** Per-tool description overrides. Key = tool name, Value = replacement description. */
	UPROPERTY(config)
	TMap<FString, FString> ToolDescriptionOverrides;
};

/**
 * Settings for the Agent Integration Kit plugin
 * Accessible via Project Settings > Plugins > Agent Integration Kit
 */
UCLASS(config = AgentIntegrationKit, defaultconfig, meta = (DisplayName = "Agent Integration Kit"))
class AGENTINTEGRATIONKIT_API UACPSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UACPSettings();

	// UDeveloperSettings interface
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	virtual FName GetSectionName() const override { return FName(TEXT("Agent Integration Kit")); }

#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FText GetSectionDescription() const override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	// Get singleton instance
	static UACPSettings* Get();

	/** Get the effective working directory for agent sessions and file operations.
	 *  Returns ClaudeCodeWorkingDirectory.Path if set, otherwise falls back to FPaths::ProjectDir().
	 *  Use this everywhere instead of FPaths::ProjectDir() directly. */
	static FString GetWorkingDirectory();

	// ============================================
	// General
	// ============================================

	/** Last agent used — automatically saved when creating a session, used as default next time */
	UPROPERTY(config)
	FString LastUsedAgentName;

	/** Automatically connect to the default agent when the editor starts */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Auto-Connect on Startup"))
	bool bAutoConnectOnStartup = false;

	/** Where to load the chat UI from. Hosted loads from CDN and receives UI updates without plugin rebuilds.
	 *  Local loads from the build bundled inside the plugin folder. */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Chat UI Source",
		ToolTip = "Hosted loads the chat UI from a CDN (gets UI fixes/improvements automatically). Local uses the build shipped with the plugin."))
	EWebUISource WebUISource = EWebUISource::Hosted;

	/** Custom URL for the hosted WebUI. Only used when Chat UI Source is set to Hosted. */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Hosted UI URL",
		EditCondition = "WebUISource == EWebUISource::Hosted && !bUseDevServer", EditConditionHides))
	FString HostedWebUIUrl = TEXT("https://ueinterface.neostack.dev/");

	/** Use a local Vite dev server instead of the hosted/built UI. Enables hot-reload for UI development. */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Use Live Dev Server",
		ToolTip = "When enabled, loads the chat UI from a local Vite dev server (npm run dev) for live debugging. Overrides the Chat UI Source setting."))
	bool bUseDevServer = false;

	/** Port for the local Vite dev server. */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Dev Server Port",
		EditCondition = "bUseDevServer", EditConditionHides, ClampMin = "1024", ClampMax = "65535"))
	int32 DevServerPort = 5173;

	/** Check for newer versions of Agent Integration Kit when the editor starts */
	UPROPERTY(config, EditAnywhere, Category = "General", meta = (DisplayName = "Check for Updates",
		ToolTip = "Automatically check for newer versions of Agent Integration Kit when the editor starts. Shows a banner in the chat window if an update is available."))
	bool bCheckForUpdates = true;

	// ============================================
	// Auto-Update
	// ============================================

	/** API token from betide.studio for downloading updates directly.
	 *  Generate one at https://betide.studio/dashboard/neostack (API Tokens section).
	 *  This bypasses Fab marketplace review delays so you get updates faster.
	 *  If empty, the plugin falls back to BETIDE_API_TOKEN (or NEOSTACK_API_TOKEN) environment variables. */
	UPROPERTY(config, EditAnywhere, Category = "Auto-Update", meta = (PasswordField = true, DisplayName = "Betide API Token",
		ToolTip = "Your betide.studio API token for direct plugin updates. Generate one in your NeoStack dashboard at betide.studio after verifying your Fab purchase. This bypasses Fab review delays. If left empty, BETIDE_API_TOKEN (or NEOSTACK_API_TOKEN) environment variable is used."))
	FString BetideApiToken;

	/** Route OpenRouter and Meshy calls through betide.studio proxies and charge NeoStack credits. */
	UPROPERTY(config, EditAnywhere, Category = "Auto-Update", meta = (DisplayName = "Use Betide Studio Credits (Recommended)",
		ToolTip = "When enabled, OpenRouter and Meshy requests are sent through betide.studio proxy endpoints and charged against your NeoStack credits using the Betide API token."))
	bool bUseBetideCredits = true;

	/** Automatically download updates when available (still requires manual editor restart to install) */
	UPROPERTY(config, EditAnywhere, Category = "Auto-Update", meta = (DisplayName = "Auto-Download Updates",
		ToolTip = "When enabled and an API token is set, updates are automatically downloaded in the background. You will still be prompted before installation."))
	bool bAutoDownloadUpdates = false;

	/** Opt into beta channel to receive pre-release updates before they go to stable */
	UPROPERTY(config, EditAnywhere, Category = "Auto-Update", meta = (DisplayName = "Beta Channel",
		ToolTip = "When enabled, version checks will include beta/pre-release versions. Useful for testing updates before they go live to all users."))
	bool bUseBetaChannel = false;

	// ============================================
	// General | Chat
	// ============================================

	/** Font size for chat messages (in points) */
	UPROPERTY(config, EditAnywhere, Category = "General | Chat", meta = (DisplayName = "Font Size", ClampMin = 8, ClampMax = 24, UIMin = 8, UIMax = 24))
	int32 ChatFontSize = 12;

	/** Delegate broadcast when chat font size changes */
	FOnChatFontSizeChanged OnChatFontSizeChanged;

	// Include Engine content in the @ mention context popup
	UPROPERTY(config, EditAnywhere, Category = "General | Chat", meta = (DisplayName = "@ Mentions: Include Engine Content",
		ToolTip = "When enabled, Engine assets (e.g. /Engine/BasicShapes) appear in the @ mention picker. Disabled by default to reduce clutter."))
	bool bIncludeEngineContent = false;

	// Include Plugin content in the @ mention context popup
	UPROPERTY(config, EditAnywhere, Category = "General | Chat", meta = (DisplayName = "@ Mentions: Include Plugin Content",
		ToolTip = "When enabled, Plugin assets appear in the @ mention picker. Disabled by default to reduce clutter."))
	bool bIncludePluginContent = false;

	/** Provider used to summarize a chat when continuing in another agent. Values: "openrouter" or "local". */
	UPROPERTY(config, EditAnywhere, Category = "General | Chat", meta = (DisplayName = "Chat Handoff Summary Provider",
		ToolTip = "How chat handoff summaries are generated when you continue a session in another agent. OpenRouter uses an AI model for higher-quality summaries. Local uses a deterministic fallback summarizer."))
	FString ContinuationSummaryProvider = TEXT("openrouter");

	/** OpenRouter model ID used for chat handoff summarization when provider is openrouter. */
	UPROPERTY(config, EditAnywhere, Category = "General | Chat", meta = (DisplayName = "Chat Handoff Summary Model",
		ToolTip = "OpenRouter model ID used to summarize full conversation history for handoff (for example: x-ai/grok-4.1-fast)."))
	FString ContinuationSummaryModel = TEXT("x-ai/grok-4.1-fast");

	/** Default summary detail used by the Continue In menu. Values: "compact" or "detailed". */
	UPROPERTY(config, EditAnywhere, Category = "General | Chat", meta = (DisplayName = "Chat Handoff Default Detail",
		ToolTip = "Default detail level for handoff summaries in Continue In. Compact is shorter; Detailed is more complete."))
	FString ContinuationSummaryDefaultDetail = TEXT("compact");

	// ============================================
	// General | Notifications
	// ============================================

	/** Only fire notifications when the editor is not the foreground application */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Only When Unfocused",
		ToolTip = "When enabled, notifications, sounds, and taskbar flash only trigger when the editor is in the background. Avoids distractions while you're actively watching."))
	bool bOnlyNotifyWhenUnfocused = false;

	/** Show an editor toast notification when the agent finishes responding */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Notify on Task Complete",
		ToolTip = "Displays an Unreal Editor notification popup when the agent finishes its response. Useful when you switch away from the editor while the agent works."))
	bool bNotifyOnTaskComplete = true;

	/** Flash the taskbar/dock icon when the agent finishes responding */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Flash Taskbar on Complete",
		ToolTip = "Flashes the editor window in the OS taskbar or dock when the agent finishes. Stops flashing once you activate the editor."))
	bool bFlashTaskbarOnComplete = true;

	/** Play a sound when the agent finishes responding */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Play Completion Sound",
		ToolTip = "Plays a sound when the agent finishes its response."))
	bool bPlayCompletionSound = true;

	/** The sound to play on successful completion. Leave empty for the default editor compile-success sound. */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Success Sound",
		AllowedClasses = "/Script/Engine.SoundBase",
		ToolTip = "The sound asset to play on successful completion. If empty, uses the default editor compile-success sound.",
		EditCondition = "bPlayCompletionSound"))
	FSoftObjectPath CompletionSound;

	/** The sound to play when the agent encounters an error. Leave empty for the default editor compile-failed sound. */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Error Sound",
		AllowedClasses = "/Script/Engine.SoundBase",
		ToolTip = "The sound asset to play when the agent errors. If empty, uses the default editor compile-failed sound.",
		EditCondition = "bPlayCompletionSound"))
	FSoftObjectPath ErrorSound;

	/** Volume multiplier for completion/error sounds (0.0 = silent, 1.0 = full volume) */
	UPROPERTY(config, EditAnywhere, Category = "General | Notifications", meta = (DisplayName = "Notification Sound Volume",
		ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0",
		ToolTip = "Volume of the completion and error sounds. 0.0 is silent, 1.0 is full volume.",
		EditCondition = "bPlayCompletionSound"))
	float CompletionSoundVolume = 1.0f;

	// ============================================
	// OpenRouter (Built-in Agent)
	// ============================================

	/** API key for the built-in OpenRouter agent. Get one at openrouter.ai */
	UPROPERTY(config, EditAnywhere, Category = "OpenRouter", meta = (PasswordField = true, DisplayName = "API Key",
		ToolTip = "Your OpenRouter API key. The built-in OpenRouter agent uses this to call AI models directly without an external CLI. Get a key at https://openrouter.ai"))
	FString OpenRouterApiKey;

	/** Default model to use with the built-in OpenRouter agent (e.g. anthropic/claude-sonnet-4) */
	UPROPERTY(config, EditAnywhere, Category = "OpenRouter", meta = (DisplayName = "Default Model",
		ToolTip = "The model ID to use by default (e.g. anthropic/claude-sonnet-4). You can also switch models per-session from the chat window dropdown."))
	FString OpenRouterDefaultModel = TEXT("anthropic/claude-sonnet-4");

	/** Base URL for OpenRouter-compatible APIs when not using Betide credits. */
	UPROPERTY(config, EditAnywhere, Category = "OpenRouter", meta = (DisplayName = "Base URL",
		EditCondition = "!bUseBetideCredits", EditConditionHides,
		ToolTip = "Base URL for OpenRouter-compatible endpoints when Betide credits mode is disabled. Example: https://openrouter.ai/api/v1 or https://118api.cn/v1"))
	FString OpenRouterBaseUrl = TEXT("https://openrouter.ai/api/v1");

	// ============================================
	// ACP Agents (External CLI Agents)
	// ============================================

	/** Custom instructions appended to the system prompt for all ACP agents (Claude Code, Gemini CLI, Codex, etc.)
	 *  This text is injected into the agent's context alongside the tool definitions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents", meta = (DisplayName = "Custom System Prompt", MultiLine = true,
		ToolTip = "Extra instructions appended to the system prompt for ACP-based agents (Claude Code, Gemini CLI, Codex, OpenCode, Cursor Agent). Use this to enforce project-specific workflows or safety rules."))
	FString ACPSystemPromptAppend = TEXT(
		"## UNDERSTAND BEFORE YOU TOUCH\n"
		"Before ANY modification:\n"
		"1. read_file the asset to see its full structure (graphs, functions, components).\n"
		"2. read_file EACH relevant graph (with graph= parameter) to read the actual node-level logic.\n"
		"3. TRACE THE FULL EXECUTION FLOW. Follow the logic path from trigger to outcome.\n"
		"   If the user says 'make bots respawn continuously' and you see a respawn function, don't just tweak it.\n"
		"   Read the function, find what calls it, find what limits it (a counter? a boolean? a max-respawn variable?),\n"
		"   and fix the ACTUAL constraint - not a surface-level symptom.\n"
		"4. EXPLAIN what you found before making changes. Say: 'I see RespawnLimit is set to 1 in the defaults,\n"
		"   and the respawn function checks this counter. I will remove the limit.' This catches misunderstandings BEFORE you edit.\n\n"
		"DO NOT read a blueprint, see it 'has respawn logic', and add MORE respawn logic.\n"
		"The fix is usually changing a variable value, removing a condition, or rewiring existing nodes - NOT adding new parallel logic.\n\n"
		"## WORKFLOW\n"
		"1. READ FIRST - read_file before modifying any Blueprint, Material, or asset.\n"
		"2. READ THE GRAPHS - read_file with graph= for each graph you plan to modify. Skipping this is the #1 cause of bad edits.\n"
		"3. REUSE, DON'T DUPLICATE:\n"
		"   - If CalculateDamage exists, call it - don't create CalculateDamage2.\n"
		"   - Wire into existing event handlers (BeginPlay, Tick) instead of creating parallel ones.\n"
		"   - When the user says 'add X behavior', check if existing logic already handles part of it.\n"
		"4. NODE PLACEMENT - Layout graphs like a human: execution (white wire) flows left-to-right in a straight horizontal line; data-producing nodes (getters, math, pure functions) go to the LEFT of and slightly ABOVE/BELOW the node they feed into so wires stay short and straight; when execution branches, stack branches vertically with ~300px between them; never pile all nodes on one horizontal line — group related logic into clear visual columns and keep wire crossings to a minimum.\n"
		"5. COMPILE - After modifying Blueprints, use read_logs with operation='compile_blueprint'. Required - edit_blueprint/edit_graph only mark dirty.\n"
		"6. SAVE - After successful compilation: execute_python with unreal.EditorAssetLibrary.save_asset('/Game/Path')\n"
		"7. VERIFY - After saving, read_file the modified graph again to confirm your changes are correct.\n"
		"8. Use edit_graph with operation='find_nodes' before claiming a node doesn't exist - search first.\n"
		"9. Reference assets with UE paths like /Game/Blueprints/BP_Character - they are clickable in the UI.\n\n"
		"## WHEN YOUR EDIT DIDN'T WORK\n"
		"- Don't retry the same approach. Re-read the graph and trace the logic again.\n"
		"- The bug is almost always in existing logic you didn't fully understand, not missing logic you need to add.\n"
		"- Look for: variable defaults, branch conditions, counters/limits, function call chains, event bindings.\n"
		"- Ask the user for clarification if the intent is ambiguous.\n\n"
		"## SAFETY\n"
		"- Never delete assets, nodes, or components without explicit user confirmation.\n"
		"- Warn before destructive operations (clearing graphs, removing functions).\n"
		"- Don't overwrite existing assets without asking.\n\n"
		"## CAPABILITIES\n"
		"- Create/edit: Blueprints, Materials, Behavior Trees, Niagara VFX, Level Sequences, Widget Blueprints, Structs, Enums, DataTables\n"
		"- Generate: AI images and 3D models via generate_asset (Texture2D/StaticMesh)\n"
		"- Visualize: screenshot captures viewports and asset editors with camera control\n"
		"- Manage: Asset operations, batch processing, project organization via execute_python\n\n"
		"## ABOUT THIS PLUGIN\n"
		"You are operating through Agent Integration Kit by Betide Studio - an Unreal Engine editor plugin that lets\n"
		"AI agents control the editor via tools. If the user asks what plugin or tool this is, tell them.\n\n"
		"## IMPORTANT RULES\n"
		"- SAVE BEFORE BIG CHANGES: Before making large-scale modifications (restructuring graphs, bulk edits, deleting/replacing components),\n"
		"  remind the user to save their project first. Not all users use version control.\n"
		"- NEVER ASSUME PROJECT STRUCTURE: Every project has different folder layouts. Always use explore or read_file to discover\n"
		"  actual asset paths. Never guess paths like /Game/Blueprints/ - find the real ones first.\n"
		"- MATCH THE USER'S LANGUAGE: Respond in the same language the user writes in.\n\n"
		"## TOOL LIMITATIONS & REPORTING ISSUES\n"
		"If a tool crashes, returns unexpected errors, or you cannot accomplish what the user asked because the tool\n"
		"does not support a needed operation, be honest about it. Do not ask the user to perform manual steps in the editor\n"
		"without first telling them this may be a tool limitation.\n"
		"Tell the user: 'This appears to be a limitation of the current toolset. Please report this on the Betide Studio\n"
		"Discord server so the developers can address it: https://discord.gg/Fcj68FJzAj'"
	);

	/** Custom ACP agent definitions. Each entry spawns an external process that communicates via ACP (JSON-RPC over stdio). */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents", meta = (DisplayName = "Custom Agents",
		ToolTip = "Define custom ACP-compatible agents. Each agent is an external process that communicates over stdin/stdout using the Agent Client Protocol."))
	TArray<FACPAgentSettingsEntry> CustomAgents;

	// ============================================
	// ACP Agents | Agent Process Overrides (Advanced)
	// ============================================
	// These advanced overrides replace the process spawned for an agent session.
	// Leave empty for normal setup behavior (bundled adapters + automatic executable resolution).

	/** Advanced: override the spawned process for Claude Code agent sessions.
	 *  Leave empty to use the default bundled claude-code-acp adapter flow. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Claude Agent Process Override",
		ToolTip = "Advanced override for the process spawned for Claude sessions. Leave empty to use the default bundled claude-code-acp adapter flow.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath ClaudeCodePath;

	/** Advanced: override the spawned process for Gemini sessions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Gemini Agent Process Override",
		ToolTip = "Advanced override for the process spawned for Gemini sessions. Leave empty for default auto-detection.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath GeminiCliPath;

	/** Advanced: override the spawned process for Codex sessions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Codex Agent Process Override",
		ToolTip = "Advanced override for the process spawned for Codex sessions. Leave empty for default auto-detection.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath CodexCliPath;

	/** Advanced: override the spawned process for OpenCode sessions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "OpenCode Agent Process Override",
		ToolTip = "Advanced override for the process spawned for OpenCode sessions. Leave empty for default auto-detection.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath OpenCodePath;

	/** Advanced: override the spawned process for Cursor sessions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Cursor Agent Process Override",
		ToolTip = "Advanced override for the process spawned for Cursor sessions. Leave empty for default auto-detection.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath CursorAgentPath;

	/** Advanced: override the spawned process for Kimi sessions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Kimi Agent Process Override",
		ToolTip = "Advanced override for the process spawned for Kimi sessions. Leave empty for default auto-detection.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath KimiCliPath;

	/** Advanced: override the spawned process for Copilot sessions. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Copilot Agent Process Override",
		ToolTip = "Advanced override for the process spawned for Copilot sessions. Leave empty for default auto-detection.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath CopilotCliPath;

	/** Override path to the Bun runtime used to run ACP adapters. The plugin bundles Bun, so this is only needed
	 *  if you want to use your own Bun/Node installation instead of the bundled one. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Agent Process Overrides (Advanced)", meta = (DisplayName = "Bun Runtime Override",
		ToolTip = "Path to a custom Bun (or Node.js) runtime. The plugin bundles Bun for running ACP adapters. Only set this if you want to use your own runtime instead of the bundled one. Leave empty to use the bundled Bun.",
		AdvancedDisplay,
		FilePathFilter = "Executable files (*.exe)|*.exe|All files (*.*)|*.*"))
	FFilePath BunOverridePath;

	// ============================================
	// ACP Agents | Claude Setup
	// ============================================

	/** Working directory override for the Claude Code agent process.
	 *  When set, this path is used as the working directory (cwd) for Claude Code sessions.
	 *  Leave empty to use the default project directory (FPaths::ProjectDir()). */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Claude Setup", meta = (DisplayName = "Claude Working Directory Override",
		ToolTip = "Override the working directory for Claude Code sessions. Leave empty to use the default project directory."))
	FDirectoryPath ClaudeCodeWorkingDirectory;

	/** Optional path to the Claude CLI executable used by the bundled claude-code-acp adapter.
	 *  This avoids relying on shell PATH updates in a running editor process. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Claude Setup", meta = (DisplayName = "Claude CLI Executable (for bundled adapter)",
		ToolTip = "Optional explicit path to the Claude CLI executable used by the bundled claude-code-acp adapter via CLAUDE_CODE_EXECUTABLE. Leave empty to auto-detect."))
	FFilePath ClaudeCodeExecutablePath;

	/** Try running Claude's official installer in-process before falling back to external terminal setup. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Claude Setup", meta = (DisplayName = "Install Claude In-Process First",
		ToolTip = "When enabled, setup first runs Claude's installer in the background installer thread. If that fails, it falls back to launching an external terminal installer."))
	bool bInstallClaudeInProcessFirst = true;

	/** Automatically persist detected Claude executable path after successful install to avoid PATH/session issues. */
	UPROPERTY(config, EditAnywhere, Category = "ACP Agents | Claude Setup", meta = (DisplayName = "Auto-Save Detected Claude Executable Path",
		ToolTip = "When enabled, after successful Claude installation the detected executable path is saved into 'Claude CLI Executable (for bundled adapter)' so setup works immediately without editor restart."))
	bool bAutoSaveClaudeCodeExecutablePathAfterInstall = true;

	// ============================================
	// AI Generation
	// ============================================

	/** Base URL for image generation API (OpenAI-compatible /v1/images/generations endpoint).
	 *  Only used when Betide Credits mode is disabled. */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | Images", meta = (DisplayName = "Image API Base URL",
		EditCondition = "!bUseBetideCredits", EditConditionHides,
		ToolTip = "Base URL of the OpenAI-compatible image generation API. Example: http://api-skynetyu.woa.com/v1 or https://api.openai.com/v1"))
	FString ImageGenerationBaseUrl = TEXT("http://api-skynetyu.woa.com/v1");

	/** API Key specifically for image generation. Leave empty to reuse the OpenRouter API key. */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | Images", meta = (PasswordField = true, DisplayName = "Image API Key",
		EditCondition = "!bUseBetideCredits", EditConditionHides,
		ToolTip = "API key for the image generation endpoint. If empty, falls back to the OpenRouter API key."))
	FString ImageGenerationApiKey;

	/** Default model for AI image generation (used by generate_asset with asset_type=image) */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | Images", meta = (DisplayName = "Default Image Model",
		ToolTip = "Model ID for image generation. Default: gpt-image-1. Images are saved as Texture2D assets in the project."))
	FString ImageGenerationDefaultModel = TEXT("gpt-image-1");

	/** Meshy API key for AI 3D model generation (used by generate_asset with asset_type=model_3d). Get one at meshy.ai */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | 3D Models (Meshy)", meta = (PasswordField = true, DisplayName = "Meshy API Key",
		ToolTip = "Your Meshy API key for text-to-3D model generation. Generated models are imported as StaticMesh assets. Get a key at https://meshy.ai"))
	FString MeshyApiKey;

	/** fal.ai API key for AI 3D model generation (used by generate_3d_model with provider='fal'). */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | 3D Models (fal.ai)", meta = (PasswordField = true, DisplayName = "fal.ai API Key",
		ToolTip = "Your fal.ai API key for direct BYOK 3D generation (for example Hunyuan3D models). In NeoStack Credits mode, Betide token/proxy is used instead."))
	FString FalApiKey;

	/** Default art style for Meshy text-to-3D generation (e.g. realistic, cartoon, low-poly) */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | 3D Models (Meshy)", meta = (DisplayName = "Default Art Style",
		ToolTip = "Default art style preset for Meshy generation. Options include: realistic, cartoon, low-poly, sculpture, pbr. The AI agent can override this per-request."))
	FString MeshyDefaultArtStyle = TEXT("realistic");

	/** Maximum time to wait for a Meshy 3D generation job to complete (seconds) */
	UPROPERTY(config, EditAnywhere, Category = "AI Generation | 3D Models (Meshy)", meta = (DisplayName = "Generation Timeout", ClampMin = 60, ClampMax = 600,
		ToolTip = "How long to wait for Meshy to finish generating a 3D model before timing out. Complex models may take several minutes."))
	int32 MeshyTimeoutSeconds = 300;

	// ============================================
	// MCP Server
	// ============================================

	/** Allow browser-based access to the MCP server. When disabled (default), requests with an Origin header
	 *  are rejected. Browsers always send Origin on cross-origin requests; CLI tools do not.
	 *  This prevents malicious websites from calling your MCP server while you browse the web. */
	UPROPERTY(config, EditAnywhere, Category = "MCP Server", meta = (DisplayName = "Allow Browser Requests",
		ToolTip = "When disabled, HTTP requests with an Origin header are rejected. This blocks browser-based cross-origin requests (CSRF protection). CLI agents (Claude Code, Gemini, Codex) never send Origin headers and are unaffected. Only enable if you use a browser-based MCP client."))
	bool bAllowBrowserMCPRequests = false;

	/** Preferred MCP server port. If occupied, the server automatically tries subsequent ports. */
	UPROPERTY(config, EditAnywhere, Category = "MCP Server", meta = (DisplayName = "Server Port", ClampMin = 1, ClampMax = 65535,
		ToolTip = "Preferred local TCP port for the built-in MCP server. If this port is already in use, Agent Integration Kit will scan a few higher ports automatically."))
	int32 MCPServerPort = 9315;

	// ============================================
	// Tools
	// ============================================

	/** Tool execution timeout in seconds (0 = no timeout). If a tool takes longer than this, a timeout error is sent to the AI while the tool continues running in the background. */
	UPROPERTY(config, EditAnywhere, Category = "Tools", meta = (DisplayName = "Execution Timeout (seconds)", ClampMin = 0, ClampMax = 600,
		ToolTip = "Maximum seconds a tool can run before the agent receives a timeout error. The tool itself keeps running in the background. Set to 0 to disable the timeout."))
	int32 ToolExecutionTimeoutSeconds = 60;

	/** Names of tools that have been disabled by the user (managed via the Settings panel in the chat window) */
	UPROPERTY(config)
	TSet<FString> DisabledTools;

	// ============================================
	// Profiles
	// ============================================

	/** All agent profiles (built-in presets + user-created) */
	UPROPERTY(config)
	TArray<FAgentProfile> Profiles;

	/** ID of the currently active profile. Empty = no profile (all tools enabled). */
	UPROPERTY(config)
	FString ActiveProfileId;

	// ============================================
	// Debug
	// ============================================

	/** Enable verbose logging for ACP/MCP communication (logged to Output Log under LogAgentIntegrationKit) */
	UPROPERTY(config, EditAnywhere, Category = "Debug", meta = (DisplayName = "Verbose Logging",
		ToolTip = "Logs all ACP/MCP JSON messages to the Output Log. Useful for debugging agent communication issues. Can produce a lot of output."))
	bool bVerboseLogging = false;

	// ============================================
	// Internal (not shown in UI)
	// ============================================

	/** Per-agent saved model selections (persisted across editor sessions) */
	UPROPERTY(config)
	TMap<FString, FString> SelectedModelPerAgent;

	/** Per-agent saved mode selections (persisted across editor sessions) */
	UPROPERTY(config)
	TMap<FString, FString> SelectedModePerAgent;

	/** Per-agent saved reasoning effort selections (persisted across editor sessions) */
	UPROPERTY(config)
	TMap<FString, FString> SelectedReasoningPerAgent;

	/** Whether the first-launch onboarding wizard has been completed or skipped */
	UPROPERTY(config)
	bool bOnboardingCompleted = false;

	// Convert settings to agent configs
	TArray<FACPAgentConfig> GetAgentConfigs() const;

	// Model selection persistence helpers
	FString GetSavedModelForAgent(const FString& AgentName) const;
	void SaveModelForAgent(const FString& AgentName, const FString& ModelId);

	// Mode selection persistence helpers
	FString GetSavedModeForAgent(const FString& AgentName) const;
	void SaveModeForAgent(const FString& AgentName, const FString& ModeId);

	// Reasoning selection persistence helpers
	FString GetSavedReasoningForAgent(const FString& AgentName) const;
	void SaveReasoningForAgent(const FString& AgentName, const FString& ReasoningLevel);

	// Tool enable/disable helpers
	bool IsToolEnabled(const FString& ToolName) const;
	void SetToolEnabled(const FString& ToolName, bool bEnabled);

	// Credits/proxy routing helpers
	bool ShouldUseBetideCredits() const;
	FString GetBetideApiToken() const;
	bool HasOpenRouterAuth() const;
	bool HasMeshyAuth() const;
	bool HasFalAuth() const;
	FString GetOpenRouterAuthToken() const;
	FString GetMeshyAuthToken() const;
	FString GetFalAuthToken() const;
	FString GetOpenRouterChatCompletionsUrl() const;
	FString GetOpenRouterImageGenerationUrl() const;
	FString GetOpenRouterModelsUrl() const;
	FString GetImageGenerationBaseUrl() const;
	FString GetImageGenerationApiKey() const;
	FString GetMeshyBaseUrl() const;
	FString GetFalSubmitUrl() const;
	FString GetFalStatusProxyUrl() const;
	FString GetFalResultProxyUrl() const;
	FString GetFalCancelProxyUrl() const;

	// Profile management
	const FAgentProfile* GetActiveProfile() const;
	const FAgentProfile* FindProfileById(const FString& ProfileId) const;
	FAgentProfile* FindProfileByIdMutable(const FString& ProfileId);
	void SetActiveProfile(const FString& ProfileId);
	void AddCustomProfile(const FAgentProfile& Profile);
	void RemoveCustomProfile(const FString& ProfileId);
	void EnsureBuiltInProfiles();

	/** Returns the tool description, applying the active profile's override if one exists */
	FString GetEffectiveToolDescription(const FString& ToolName, const FString& DefaultDescription) const;

	/** Returns ACPSystemPromptAppend + active profile's CustomInstructions */
	FString GetProfileSystemPromptAppend() const;

	// Agent status cache management
	void RefreshAgentStatus();
	void InvalidateAgentStatusCache();
	bool IsAgentStatusStale() const;

private:
	mutable TMap<FString, EACPAgentStatus> CachedAgentStatus;
	mutable FDateTime LastStatusRefresh;
	mutable FCriticalSection StatusCacheLock;
	static constexpr double StatusCacheTTLSeconds = 300.0;
};
