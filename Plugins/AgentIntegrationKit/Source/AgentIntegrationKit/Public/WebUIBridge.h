// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "WebJSFunction.h"
#include "ACPTypes.h"
#include "WebUIBridge.generated.h"

/**
 * Bridge object exposed to the WebUI via SWebBrowser::BindUObject.
 * JavaScript accesses this as window.ue.bridge.*
 * All UFUNCTION methods become callable from JS and return Promises.
 */
UCLASS()
class AGENTINTEGRATIONKIT_API UWebUIBridge : public UObject
{
	GENERATED_BODY()

public:

	// ── Agent Discovery ──────────────────────────────────────────────

	/** Returns JSON array of available agents with their config/status */
	UFUNCTION()
	FString GetAgents();

	/** Returns the last used agent name (persisted across sessions) */
	UFUNCTION()
	FString GetLastUsedAgent();

	// ── Onboarding ──────────────────────────────────────────────────

	/** Returns whether the onboarding wizard has been completed or skipped */
	UFUNCTION()
	bool GetOnboardingCompleted();

	/** Mark the onboarding wizard as completed (or skipped). Persists to config. */
	UFUNCTION()
	void SetOnboardingCompleted();

	// ── Session Lifecycle ────────────────────────────────────────────

	/** Create a new session. Returns JSON: {sessionId, agentName, title} */
	UFUNCTION()
	FString CreateSession(const FString& AgentName);

	/** Returns JSON array of all session metadata (saved + active) */
	UFUNCTION()
	FString GetSessions();

	/** Resume a saved session — loads from disk into memory. Returns JSON: {success, error?} */
	UFUNCTION()
	FString ResumeSession(const FString& SessionId);

	/** Returns JSON with full session data including messages */
	UFUNCTION()
	FString GetSessionMessages(const FString& SessionId);

	/** Build a continuation draft from one session for a different target agent.
	 *  Returns JSON: {success, draftPrompt?, error?} */
	UFUNCTION()
	FString BuildContinuationDraft(const FString& SourceSessionId, const FString& TargetAgentName, const FString& SummaryMode);

	/** Start async continuation draft generation. Returns JSON: {success, requestId?, pending, error?} */
	UFUNCTION()
	FString RequestContinuationDraft(const FString& SourceSessionId, const FString& TargetAgentName, const FString& SummaryMode);

	/** Returns JSON: {provider, modelId, defaultDetail, hasOpenRouterKey} */
	UFUNCTION()
	FString GetContinuationSummarySettings();

	/** Set continuation summary provider ("openrouter" or "local") */
	UFUNCTION()
	void SetContinuationSummaryProvider(const FString& Provider);

	/** Set continuation summary model ID (OpenRouter model id) */
	UFUNCTION()
	void SetContinuationSummaryModel(const FString& ModelId);

	/** Set default continuation summary detail ("compact" or "detailed") */
	UFUNCTION()
	void SetContinuationSummaryDefaultDetail(const FString& Detail);

	// ── Messaging ────────────────────────────────────────────────────

	/** Send a user prompt to a session. Streaming updates arrive via the OnMessage callback. */
	UFUNCTION()
	void SendPrompt(const FString& SessionId, const FString& Text);

	/** Cancel the current streaming prompt in a session */
	UFUNCTION()
	void CancelPrompt(const FString& SessionId);

	// ── Model & Reasoning ───────────────────────────────────────────

	/** Returns JSON: {models: [{id, name, description, supportsReasoning}], currentModelId: "..."} */
	UFUNCTION()
	FString GetModels(const FString& AgentName);

	/** Returns JSON with full model list for agents that support it (OpenRouter). */
	UFUNCTION()
	FString GetAllModels(const FString& AgentName);

	/** Set the active model for an agent */
	UFUNCTION()
	void SetModel(const FString& AgentName, const FString& ModelId);

	/** Returns the current reasoning effort level for an agent: "none", "low", "medium", "high", "max" */
	UFUNCTION()
	FString GetReasoningLevel(const FString& AgentName);

	/** Set reasoning effort level for an agent: "none", "low", "medium", "high", "max" */
	UFUNCTION()
	void SetReasoningLevel(const FString& AgentName, const FString& Level);

	// ── Mode Selection ──────────────────────────────────────────────

	/** Returns JSON: {modes: [{id, name, description}], currentModeId: "..."} */
	UFUNCTION()
	FString GetModes(const FString& AgentName);

	/** Set the active mode for an agent session */
	UFUNCTION()
	void SetMode(const FString& AgentName, const FString& ModeId);

	// ── Agent Setup ─────────────────────────────────────────────────

	/** Returns JSON with install info: {agentName, baseExecutableName, installCommand, installUrl, requiresAdapter, requiresBaseCLI} */
	UFUNCTION()
	FString GetAgentInstallInfo(const FString& AgentName);

	/** Start async agent installation. Progress/completion arrive via OnInstallProgress/OnInstallComplete callbacks. */
	UFUNCTION()
	void InstallAgent(const FString& AgentName);

	/** Re-check and return current status for an agent (invalidates cache). Returns JSON: {status, statusMessage} */
	UFUNCTION()
	FString RefreshAgentStatus(const FString& AgentName);

	/** Copy text to the system clipboard */
	UFUNCTION()
	void CopyToClipboard(const FString& Text);

	/** Read text from the system clipboard */
	UFUNCTION()
	FString GetClipboardText();

	/** Open a URL in the system default browser */
	UFUNCTION()
	void OpenUrl(const FString& Url);

	/** Open an asset or source file. Handles /Game/ paths, filesystem paths, and file:line format. */
	UFUNCTION()
	void OpenPath(const FString& Path, int32 Line);

	/** Open the Agent Integration Kit settings in the UE Project Settings panel */
	UFUNCTION()
	void OpenPluginSettings();

	/** Restart the Unreal Editor (prompts to save if needed) */
	UFUNCTION()
	void RestartEditor();

	/** Trigger the plugin's async update check flow */
	UFUNCTION()
	void CheckForPluginUpdate();

	// ── Agent Authentication ────────────────────────────────────────

	/** Get available auth methods for an agent. Returns JSON array: [{id, name, description, isTerminalAuth}] */
	UFUNCTION()
	FString GetAuthMethods(const FString& AgentName);

	/** Start agent login with a specific auth method. Completion arrives via OnLoginComplete callback. */
	UFUNCTION()
	void StartAgentLogin(const FString& AgentName, const FString& MethodId);

	/** Register a JS callback for login completion: callback(agentName: string, success: bool, errorMessage: string) */
	UFUNCTION()
	void BindOnLoginComplete(FWebJSFunction Callback);

	// ── Agent Usage / Rate Limits ───────────────────────────────────

	/** Returns JSON with rate limit data for an agent. Triggers a fetch if no cached data. */
	UFUNCTION()
	FString GetAgentUsage(const FString& AgentName);

	/** Force-refresh usage data for an agent. Result arrives via OnUsageUpdated callback. */
	UFUNCTION()
	void RefreshAgentUsage(const FString& AgentName);

	// ── Streaming Callbacks (JS → C++ → JS) ─────────────────────────

	/** Register a JS callback for streaming message updates: callback(sessionId, updateJson) */
	UFUNCTION()
	void BindOnMessage(FWebJSFunction Callback);

	/** Register a JS callback for agent state changes: callback(sessionId, agentName, state, message) */
	UFUNCTION()
	void BindOnStateChanged(FWebJSFunction Callback);

	/** Register a JS callback for permission requests: callback(sessionId, requestJson) */
	UFUNCTION()
	void BindOnPermissionRequest(FWebJSFunction Callback);

	/** Register a JS callback for mode availability: callback(agentName, modesJson) */
	UFUNCTION()
	void BindOnModesAvailable(FWebJSFunction Callback);

	/** Register a JS callback for mode changes: callback(agentName, modeId) */
	UFUNCTION()
	void BindOnModeChanged(FWebJSFunction Callback);

	/** Register a JS callback for install progress: callback(agentName, message) */
	UFUNCTION()
	void BindOnInstallProgress(FWebJSFunction Callback);

	/** Register a JS callback for install completion: callback(agentName, success, errorMessage) */
	UFUNCTION()
	void BindOnInstallComplete(FWebJSFunction Callback);

	/** Register a JS callback for model availability: callback(agentName, modelsJson) */
	UFUNCTION()
	void BindOnModelsAvailable(FWebJSFunction Callback);

	/** Register a JS callback for slash commands availability: callback(sessionId, commandsJson) */
	UFUNCTION()
	void BindOnCommandsAvailable(FWebJSFunction Callback);

	/** Register a JS callback for plan/todo updates: callback(sessionId, planJson) */
	UFUNCTION()
	void BindOnPlanUpdate(FWebJSFunction Callback);

	/** Register a JS callback for agent usage/rate-limit updates: callback(agentName, usageJson) */
	UFUNCTION()
	void BindOnUsageUpdated(FWebJSFunction Callback);

	/** Register a JS callback for MCP tool readiness: callback(sessionId, status) where status is "waiting"|"ready"|"timeout" */
	UFUNCTION()
	void BindOnMcpStatus(FWebJSFunction Callback);

	/** Register a JS callback for continuation draft completion: callback(requestId, resultJson) */
	UFUNCTION()
	void BindOnContinuationDraftReady(FWebJSFunction Callback);

	/** Respond to a permission request. OutcomeMetaJson is optional JSON for AskUserQuestion answers. */
	UFUNCTION()
	void RespondToPermission(const FString& AgentName, int32 RequestId, const FString& OptionId, const FString& OutcomeMetaJson);

	// ── Attachments ─────────────────────────────────────────────────

	/** Paste image from system clipboard into attachments. Returns JSON: {success, error?} */
	UFUNCTION()
	FString PasteClipboardImage();

	/** Open native file picker for attachments (images + common docs). Returns JSON: {success, count} */
	UFUNCTION()
	FString OpenImagePicker();

	/** Add an image from base64 data (JS drag-drop). Returns JSON: {success, attachmentId?} */
	UFUNCTION()
	FString AddImageFromBase64(const FString& Base64Data, const FString& MimeType, int32 Width, int32 Height, const FString& DisplayName);

	/** Add a generic file from base64 data (JS drag-drop). Returns JSON: {success, attachmentId?} */
	UFUNCTION()
	FString AddFileFromBase64(const FString& Base64Data, const FString& MimeType, const FString& DisplayName);

	/** Remove an attachment by its GUID string */
	UFUNCTION()
	void RemoveAttachment(const FString& AttachmentId);

	/** Get current attachments as JSON array (metadata only, no base64) */
	UFUNCTION()
	FString GetAttachments();

	/** Register a JS callback for attachment changes: callback(attachmentsJson) */
	UFUNCTION()
	void BindOnAttachmentsChanged(FWebJSFunction Callback);

	// ── Tool Profiles & Settings ────────────────────────────────────

	/** Returns JSON array of all tools with enabled state and any description overrides for the given profile */
	UFUNCTION()
	FString GetTools(const FString& ProfileId);

	/** Returns JSON: {profiles: [...], activeProfileId: "..."} */
	UFUNCTION()
	FString GetProfiles();

	/** Returns JSON with full profile detail including customInstructions and toolDescriptionOverrides */
	UFUNCTION()
	FString GetProfileDetail(const FString& ProfileId);

	/** Set the active profile. Empty string = no profile (all tools enabled). */
	UFUNCTION()
	void SetActiveProfile(const FString& ProfileId);

	/** Toggle a tool's global enabled state (DisabledTools set) */
	UFUNCTION()
	void SetToolEnabled(const FString& ToolName, bool bEnabled);

	/** Toggle a tool within a specific profile's EnabledTools whitelist */
	UFUNCTION()
	void SetProfileToolEnabled(const FString& ProfileId, const FString& ToolName, bool bEnabled);

	/** Create a custom profile. Returns JSON: {profileId: "..."} */
	UFUNCTION()
	FString CreateProfile(const FString& DisplayName, const FString& Description);

	/** Delete a custom profile. Returns JSON: {success: bool} */
	UFUNCTION()
	FString DeleteProfile(const FString& ProfileId);

	/** Update profile metadata (name, description, customInstructions). Returns JSON: {success: bool} */
	UFUNCTION()
	FString UpdateProfile(const FString& ProfileId, const FString& DisplayName, const FString& Description, const FString& CustomInstructions);

	/** Set or clear a tool description override for a profile. Empty override = clear. */
	UFUNCTION()
	void SetToolDescriptionOverride(const FString& ProfileId, const FString& ToolName, const FString& DescriptionOverride);

	// ── Context Mentions ────────────────────────────────────────────

	/** Search for assets/files to attach via @ mention. Returns JSON array of {name, path, category, type} */
	UFUNCTION()
	FString SearchContextItems(const FString& Query);

	// ── Session Management ──────────────────────────────────────────

	/** Delete a session (closes active + removes agent's native file). Returns JSON: {success: bool} */
	UFUNCTION()
	FString DeleteSession(const FString& SessionId);

	/** Export a loaded session to a Markdown file. Returns JSON: {success, canceled?, savedPath?, error?} */
	UFUNCTION()
	FString ExportSessionToMarkdown(const FString& SessionId);

	/** Register a JS callback for session list updates: callback(agentName, sessionsJson) */
	UFUNCTION()
	void BindOnSessionListUpdated(FWebJSFunction Callback);

	/** Manually refresh session lists from all connected (or connectable) agents.
	 *  Returns JSON: {connectingCount: number} — how many agents are being connected for listing. */
	UFUNCTION()
	FString RefreshSessionList();

	// ── Source Control ──────────────────────────────────────────────

	/** Returns JSON: {enabled, provider, branch, changesCount, connected} */
	UFUNCTION()
	FString GetSourceControlStatus();

	/** Open the UE source control changelists tab */
	UFUNCTION()
	void OpenSourceControlChangelist();

	/** Open the UE check-in/submit dialog */
	UFUNCTION()
	void OpenSourceControlSubmit();

private:
	/** Stored JS callbacks for streaming */
	FWebJSFunction OnMessageCallback;
	FWebJSFunction OnStateChangedCallback;
	FWebJSFunction OnPermissionRequestCallback;
	FWebJSFunction OnModesAvailableCallback;
	FWebJSFunction OnModeChangedCallback;
	FWebJSFunction OnInstallProgressCallback;
	FWebJSFunction OnInstallCompleteCallback;
	FWebJSFunction OnCommandsAvailableCallback;
	FWebJSFunction OnPlanUpdateCallback;
	FWebJSFunction OnModelsAvailableCallback;
	FWebJSFunction OnUsageUpdatedCallback;
	FWebJSFunction OnAttachmentsChangedCallback;
	FWebJSFunction OnLoginCompleteCallback;
	FWebJSFunction OnMcpStatusCallback;
	FWebJSFunction OnSessionListUpdatedCallback;
	FWebJSFunction OnContinuationDraftReadyCallback;

	/** Monotonic request id for async continuation draft jobs */
	int32 NextContinuationDraftRequestId = 1;

	/** Delegate handles for cleanup */
	FDelegateHandle AgentMessageHandle;
	FDelegateHandle AgentStateHandle;
	FDelegateHandle AgentErrorHandle;
	FDelegateHandle AgentAuthCompleteHandle;
	FDelegateHandle PermissionRequestHandle;
	FDelegateHandle ModesAvailableHandle;
	FDelegateHandle ModeChangedHandle;
	FDelegateHandle CommandsAvailableHandle;
	FDelegateHandle PlanUpdateHandle;
	FDelegateHandle ModelsAvailableHandle;
	FDelegateHandle UsageUpdatedHandle;
	FDelegateHandle MeshyBalanceHandle;
	FDelegateHandle AttachmentsChangedHandle;
	FDelegateHandle McpToolsDiscoveredHandle;
	FDelegateHandle SessionListUpdatedHandle;

	/** MCP tools discovery timeout */
	FTSTicker::FDelegateHandle McpTimeoutTickerHandle;
	FString McpWaitingSessionId;  // Session ID waiting for MCP tools

	/** Fire MCP status callback and clean up listeners */
	void NotifyMcpStatus(const FString& SessionId, const FString& Status);

	/** Bind to agent manager delegates */
	void BindDelegates();
	void UnbindDelegates();

	/** Per-session streaming message index (mirrors Slate UI's CurrentStreamingMessageIndex) */
	TMap<FString, int32> StreamingMessageIndices;

	/** Serialize a single message to JSON */
	static TSharedPtr<FJsonObject> MessageToJson(const struct FACPChatMessage& Message);
	static TSharedPtr<FJsonObject> ContentBlockToJson(const struct FACPContentBlock& Block);
};
