// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ACPTypes.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

// Forward declarations
struct FACPAgentConfig;
struct FMCPToolDefinition;

// Reuse the same delegate types as FACPClient for seamless integration
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOpenRouterStateChanged, EACPClientState, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenRouterSessionUpdate, const FACPSessionUpdate&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenRouterResponse, const TSharedPtr<FJsonObject>&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnOpenRouterError, int32, const FString&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenRouterModelsAvailable, const FACPSessionModelState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenRouterPermissionRequest, const FACPPermissionRequest&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenRouterModesAvailable, const FACPSessionModeState&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnOpenRouterModeChanged, const FString&);

/**
 * Tool call from OpenRouter response
 */
struct FOpenRouterToolCall
{
	FString Id;
	FString Name;
	FString Arguments; // JSON string
};

/**
 * OpenRouter message structure for conversation history
 */
struct FOpenRouterMessage
{
	FString Role;
	FString Content;

	// For assistant messages with tool calls
	TArray<FOpenRouterToolCall> ToolCalls;

	// For tool response messages
	FString ToolCallId;
};

/**
 * Native C++ client for OpenRouter API
 * Implements the same interface as FACPClient but uses direct HTTP calls
 * instead of spawning a subprocess. This makes it Fab-compliant (no external executables).
 */
class AGENTINTEGRATIONKIT_API FOpenRouterClient
{
public:
	FOpenRouterClient();
	~FOpenRouterClient();

	// Connect to OpenRouter (validates config, doesn't spawn process)
	bool Connect(const FACPAgentConfig& Config);

	// Disconnect
	void Disconnect();

	// Check connection status
	bool IsConnected() const { return State != EACPClientState::Disconnected && State != EACPClientState::Error; }
	EACPClientState GetState() const { return State; }

	// Session management (matching FACPClient interface)
	void NewSession(const FString& WorkingDirectory);
	void LoadSession(const FString& SessionId);
	void SendPrompt(const FString& PromptText);
	void CancelPrompt();
	void SetMode(const FString& ModeId);
	void SetModel(const FString& ModelId);

	// Permission response (no-op for OpenRouter, but needed for interface compatibility)
	void RespondToPermissionRequest(int32 RequestId, const FString& OptionId);

	// Delegates (matching FACPClient interface exactly)
	FOnOpenRouterStateChanged OnStateChanged;
	FOnOpenRouterSessionUpdate OnSessionUpdate;
	FOnOpenRouterResponse OnResponse;
	FOnOpenRouterError OnError;
	FOnOpenRouterModelsAvailable OnModelsAvailable;
	FOnOpenRouterPermissionRequest OnPermissionRequest;
	FOnOpenRouterModesAvailable OnModesAvailable;
	FOnOpenRouterModeChanged OnModeChanged;

	// Get capabilities (matching FACPClient interface)
	const FACPAgentCapabilities& GetAgentCapabilities() const { return AgentCapabilities; }

	// Get available models for the current session
	const FACPSessionModelState& GetModelState() const { return SessionModelState; }

	// Get available modes for the current session
	const FACPSessionModeState& GetModeState() const { return SessionModeState; }

	// Get all cached models (for the picker UI)
	const TArray<FACPModelInfo>& GetAllCachedModels() const { return CachedModels; }

	// Get curated models + "More..." option
	TArray<FACPModelInfo> GetCuratedModels();

	// Add a model to recent list
	void AddRecentModel(const FACPModelInfo& RecentModel);

	// Get model info by ID (returns nullptr if not found)
	const FACPModelInfo* GetModelInfo(const FString& ModelId) const;

	// Check if current model supports reasoning
	bool CurrentModelSupportsReasoning() const;

	// Reasoning configuration
	void SetReasoningEnabled(bool bEnabled) { bReasoningEnabled = bEnabled; }
	bool IsReasoningEnabled() const { return bReasoningEnabled; }
	void SetReasoningEffort(const FString& Effort) { ReasoningEffort = Effort; }
	const FString& GetReasoningEffort() const { return ReasoningEffort; }

	// Session history management (for persistence/resume)
	TArray<FOpenRouterMessage> GetConversationHistory() const;
	void RestoreConversationHistory(const TArray<FOpenRouterMessage>& History);
	FString GetCurrentSessionId() const { return CurrentSessionId; }

	// Unreal session tracking (for multi-chat support)
	void SetUnrealSessionId(const FString& SessionId) { UnrealSessionId = SessionId; }
	FString GetUnrealSessionId() const { return UnrealSessionId; }

	// Usage tracking
	const FACPUsageData& GetSessionUsage() const { return SessionUsage; }
	void ResetSessionUsage() { SessionUsage = FACPUsageData(); }

private:
	// Set state and broadcast
	void SetState(EACPClientState NewState, const FString& Message = TEXT(""));

	// Initialize default capabilities and models
	void InitializeCapabilities();

	// Fetch models from OpenRouter API (no auth required)
	void FetchModelsFromOpenRouter();

	// Callback for models fetch
	void OnModelsFetchComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);

	// Get hardcoded fallback models list
	TArray<FACPModelInfo> GetHardcodedModels();

	// Parse model ID to get provider name
	FString GetProviderFromModelId(const FString& ModelId);

	// Build OpenRouter API request body (with tools)
	FString BuildRequestBody();

	// Build tools array from registered MCP tools
	TArray<TSharedPtr<FJsonValue>> BuildToolsArray();

	// Build single tool definition JSON
	TSharedPtr<FJsonObject> BuildToolDefinition(const FString& ToolName, const FString& Description, TSharedPtr<FJsonObject> InputSchema);

	// HTTP callbacks
	void OnRequestProgress64(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived);
	void OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);

	// Process SSE stream data
	void ProcessStreamBuffer();
	void ProcessSSELine(const FString& Line);

	// Tool calling support
	void ProcessToolCalls(const TArray<FOpenRouterToolCall>& ToolCalls);
	void ExecuteToolCall(const FOpenRouterToolCall& ToolCall);
	void ContinueAfterToolExecution();

	// Broadcast tool call UI updates
	void BroadcastToolCallStart(const FOpenRouterToolCall& ToolCall);
	void BroadcastToolCallResult(const FString& ToolCallId, bool bSuccess, const FString& Result, const TArray<FACPToolResultImage>& Images = TArray<FACPToolResultImage>());

private:
	// Configuration
	FString ApiKey;
	FString Model;
	FString BaseUrl = TEXT("https://openrouter.ai/api/v1/chat/completions");

	// State
	EACPClientState State = EACPClientState::Disconnected;
	FACPAgentConfig CurrentConfig;
	FACPAgentCapabilities AgentCapabilities;

	// Current session
	FString CurrentSessionId;

	// Unreal session ID this client is currently serving (for multi-chat support)
	FString UnrealSessionId;

	// Model/Mode state
	FACPSessionModelState SessionModelState;
	FACPSessionModeState SessionModeState;

	// Model cache for OpenRouter
	TArray<FACPModelInfo> CachedModels;
	TArray<FACPModelInfo> RecentModels;
	FDateTime LastModelsFetch;
	static constexpr double ModelsCacheTTLHours = 24.0;

	// Featured providers that appear at top of list (defined in .cpp)
	static const TCHAR* FeaturedProviders[];

	// Conversation history
	TArray<FOpenRouterMessage> ConversationHistory;

	// Current streaming state
	TSharedPtr<IHttpRequest, ESPMode::ThreadSafe> CurrentRequest;
	FString StreamBuffer;
	FString CurrentResponseText;
	FString CurrentReasoningText; // Accumulated reasoning content
	int32 LastProcessedLength = 0;
	TAtomic<bool> bIsCancelled;

	// Tool calling state
	TArray<FOpenRouterToolCall> PendingToolCalls;
	TArray<FOpenRouterToolCall> CurrentToolCalls; // Tool calls being accumulated from stream
	int32 CurrentToolCallIndex = 0;
	bool bIsProcessingTools = false;

	// Reasoning configuration
	bool bReasoningEnabled = false;
	FString ReasoningEffort = TEXT("medium"); // none, low, medium, high

	// Usage tracking (cumulative for session)
	FACPUsageData SessionUsage;

	// Thread safety
	mutable FCriticalSection StateLock;
};
