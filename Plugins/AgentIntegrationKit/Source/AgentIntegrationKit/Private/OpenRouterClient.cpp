// Copyright 2025 Betide Studio. All Rights Reserved.

#include "OpenRouterClient.h"
#include "AgentIntegrationKitModule.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/Guid.h"
#include "Async/Async.h"
#include "MCPServer.h"
#include "MCPTypes.h"
#include "Tools/NeoStackToolRegistry.h"
#include "ACPAttachmentManager.h"
#include "ACPSettings.h"

FOpenRouterClient::FOpenRouterClient()
	: bIsCancelled(false)
{
	InitializeCapabilities();
}

const TCHAR* FOpenRouterClient::FeaturedProviders[] = {
	TEXT("anthropic"),
	TEXT("openai"),
	TEXT("google"),
	TEXT("deepseek")
};

namespace
{
	FString ExtractErrorMessageFromResponseBody(const FString& Body)
	{
		TSharedPtr<FJsonObject> Json;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
		if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
		{
			return FString();
		}

		FString Message;
		if (Json->TryGetStringField(TEXT("error"), Message) && !Message.IsEmpty())
		{
			return Message;
		}

		if (Json->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
		{
			return Message;
		}

		const TSharedPtr<FJsonObject>* ErrorObj = nullptr;
		if (Json->TryGetObjectField(TEXT("error"), ErrorObj) && ErrorObj && ErrorObj->IsValid())
		{
			if ((*ErrorObj)->TryGetStringField(TEXT("message"), Message) && !Message.IsEmpty())
			{
				return Message;
			}
		}

		return FString();
	}

	FString BuildOpenRouterErrorMessage(int32 ResponseCode, const FString& Body, bool bUsingNeoStackCredits)
	{
		FString ErrorDetail = ExtractErrorMessageFromResponseBody(Body);
		if (ErrorDetail.IsEmpty())
		{
			ErrorDetail = Body.Left(800);
		}

		if (bUsingNeoStackCredits &&
			ResponseCode == 402 &&
			ErrorDetail.Contains(TEXT("insufficient credits"), ESearchCase::IgnoreCase))
		{
			return FString::Printf(
				TEXT("OpenRouter API error %d: Insufficient NeoStack credits. Top up at https://betide.studio/dashboard/neostack"),
				ResponseCode);
		}

		return FString::Printf(TEXT("OpenRouter API error %d: %s"), ResponseCode, *ErrorDetail);
	}
}

FOpenRouterClient::~FOpenRouterClient()
{
	Disconnect();
}

void FOpenRouterClient::InitializeCapabilities()
{
	AgentCapabilities.bSupportsNewSession = true;
	AgentCapabilities.bSupportsLoadSession = false;
	AgentCapabilities.bSupportsResumeSession = false;
	AgentCapabilities.bSupportsAudio = false;
	AgentCapabilities.bSupportsImage = false;

	SessionModelState.AvailableModels = GetHardcodedModels();
}

TArray<FACPModelInfo> FOpenRouterClient::GetHardcodedModels()
{
	TArray<FACPModelInfo> Models;

	FACPModelInfo Claude;
	Claude.ModelId = TEXT("anthropic/claude-sonnet-4");
	Claude.Name = TEXT("Claude Sonnet 4");
	Claude.Description = TEXT("Anthropic's Claude Sonnet 4");
	Models.Add(Claude);

	FACPModelInfo Claude35;
	Claude35.ModelId = TEXT("anthropic/claude-3.5-sonnet");
	Claude35.Name = TEXT("Claude 3.5 Sonnet");
	Claude35.Description = TEXT("Anthropic's Claude 3.5 Sonnet");
	Models.Add(Claude35);

	FACPModelInfo GPT4;
	GPT4.ModelId = TEXT("openai/gpt-4o");
	GPT4.Name = TEXT("GPT-4o");
	GPT4.Description = TEXT("OpenAI's GPT-4o");
	Models.Add(GPT4);

	FACPModelInfo Gemini;
	Gemini.ModelId = TEXT("google/gemini-2.0-flash-001");
	Gemini.Name = TEXT("Gemini 2.0 Flash");
	Gemini.Description = TEXT("Google's Gemini 2.0 Flash");
	Models.Add(Gemini);

	FACPModelInfo DeepSeek;
	DeepSeek.ModelId = TEXT("deepseek/deepseek-chat");
	DeepSeek.Name = TEXT("DeepSeek Chat");
	DeepSeek.Description = TEXT("DeepSeek's Chat model");
	Models.Add(DeepSeek);

	return Models;
}

FString FOpenRouterClient::GetProviderFromModelId(const FString& ModelId)
{
	int32 SlashIndex = ModelId.Find(TEXT("/"));
	if (SlashIndex != INDEX_NONE)
	{
		return ModelId.Left(SlashIndex).ToLower();
	}
	return TEXT("unknown");
}

void FOpenRouterClient::FetchModelsFromOpenRouter()
{
	if (CachedModels.Num() > 0 && LastModelsFetch.GetTicks() > 0)
	{
		FDateTime Now = FDateTime::Now();
		if ((Now - LastModelsFetch).GetTotalHours() < ModelsCacheTTLHours)
		{
			// Use cached models but filtered
			SessionModelState.AvailableModels = GetCuratedModels();
			OnModelsAvailable.Broadcast(SessionModelState);
			return;
		}
	}
	
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Fetching models from OpenRouter API..."));
	
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	FString ModelsUrl = TEXT("https://openrouter.ai/api/v1/models");
	if (const UACPSettings* Settings = UACPSettings::Get())
	{
		ModelsUrl = Settings->GetOpenRouterModelsUrl();
	}
	Request->SetURL(ModelsUrl);
	Request->SetVerb(TEXT("GET"));
	Request->OnProcessRequestComplete().BindRaw(this, &FOpenRouterClient::OnModelsFetchComplete);
	
	if (!Request->ProcessRequest())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("OpenRouterClient: Failed to initiate models request, using fallback"));
		SessionModelState.AvailableModels = GetCuratedModels(); // Fallback to curated hardcoded
		OnModelsAvailable.Broadcast(SessionModelState);
	}
}

TArray<FACPModelInfo> FOpenRouterClient::GetCuratedModels()
{
	TArray<FACPModelInfo> Curated;
	TSet<FString> AddedIds;

	// 0. Recent Models (Top priority)
	for (const FACPModelInfo& Recent : RecentModels)
	{
		if (!AddedIds.Contains(Recent.ModelId))
		{
			Curated.Add(Recent);
			AddedIds.Add(Recent.ModelId);
		}
	}
	
	// 1. Claude Sonnet 4.5
	if (!AddedIds.Contains(TEXT("anthropic/claude-sonnet-4.5")))
	{
		FACPModelInfo M; 
		M.ModelId = TEXT("anthropic/claude-sonnet-4.5"); 
		M.Name = TEXT("Claude Sonnet 4.5"); 
		M.Description = TEXT("Anthropic's balanced model");
		Curated.Add(M);
		AddedIds.Add(M.ModelId);
	}
	
	// 2. Claude Opus 4.5
	if (!AddedIds.Contains(TEXT("anthropic/claude-opus-4.5")))
	{
		FACPModelInfo M; 
		M.ModelId = TEXT("anthropic/claude-opus-4.5"); 
		M.Name = TEXT("Claude Opus 4.5"); 
		M.Description = TEXT("Anthropic's high-performance model");
		Curated.Add(M);
		AddedIds.Add(M.ModelId);
	}
	
	// 3. Gemini 3 Pro Preview
	if (!AddedIds.Contains(TEXT("google/gemini-3-pro-preview")))
	{
		FACPModelInfo M; 
		M.ModelId = TEXT("google/gemini-3-pro-preview"); 
		M.Name = TEXT("Gemini 3 Pro Preview"); 
		M.Description = TEXT("Google's advanced multimodal model");
		Curated.Add(M);
		AddedIds.Add(M.ModelId);
	}
	
	// 4. Gemini 3 Flash Preview
	if (!AddedIds.Contains(TEXT("google/gemini-3-flash-preview")))
	{
		FACPModelInfo M; 
		M.ModelId = TEXT("google/gemini-3-flash-preview"); 
		M.Name = TEXT("Gemini 3 Flash Preview"); 
		M.Description = TEXT("Google's fast multimodal model");
		Curated.Add(M);
		AddedIds.Add(M.ModelId);
	}
	
	// 5. GLM 4.7
	if (!AddedIds.Contains(TEXT("z-ai/glm-4.7")))
	{
		FACPModelInfo M; 
		M.ModelId = TEXT("z-ai/glm-4.7"); 
		M.Name = TEXT("GLM 4.7"); 
		M.Description = TEXT("Zhipu AI's general purpose model");
		Curated.Add(M);
		AddedIds.Add(M.ModelId);
	}

	// 6. Special "More" option
	{
		FACPModelInfo M; 
		M.ModelId = TEXT("special:browse_all"); 
		M.Name = TEXT("Browse all models..."); 
		M.Description = TEXT("Click to search 400+ available models");
		Curated.Add(M);
	}
	
	return Curated;
}

void FOpenRouterClient::AddRecentModel(const FACPModelInfo& RecentModel)
{
	// Remove if exists (to move to top)
	RecentModels.RemoveAll([&](const FACPModelInfo& M) { return M.ModelId == RecentModel.ModelId; });
	
	// Add to top
	RecentModels.Insert(RecentModel, 0);
	
	// Limit size
	if (RecentModels.Num() > 5)
	{
		RecentModels.SetNum(5);
	}
	
	// Update available models and broadcast
	SessionModelState.AvailableModels = GetCuratedModels();
	OnModelsAvailable.Broadcast(SessionModelState);
}

const FACPModelInfo* FOpenRouterClient::GetModelInfo(const FString& ModelId) const
{
	// First check cached models
	for (const FACPModelInfo& M : CachedModels)
	{
		if (M.ModelId == ModelId)
		{
			return &M;
		}
	}

	// Check recent models
	for (const FACPModelInfo& M : RecentModels)
	{
		if (M.ModelId == ModelId)
		{
			return &M;
		}
	}

	return nullptr;
}

bool FOpenRouterClient::CurrentModelSupportsReasoning() const
{
	const FACPModelInfo* ModelInfo = GetModelInfo(Model);
	if (ModelInfo)
	{
		return ModelInfo->SupportsReasoning();
	}

	// Default: assume major providers support reasoning
	FString LowerModel = Model.ToLower();
	return LowerModel.Contains(TEXT("anthropic/")) ||
	       LowerModel.Contains(TEXT("openai/")) ||
	       LowerModel.Contains(TEXT("deepseek/"));
}

void FOpenRouterClient::OnModelsFetchComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("OpenRouterClient: Models fetch failed (code %d), using fallback"),
			Response.IsValid() ? Response->GetResponseCode() : -1);
		SessionModelState.AvailableModels = GetCuratedModels();
		OnModelsAvailable.Broadcast(SessionModelState);
		return;
	}

	TSharedPtr<FJsonObject> JsonRoot;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
	if (!FJsonSerializer::Deserialize(Reader, JsonRoot) || !JsonRoot.IsValid())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("OpenRouterClient: Failed to parse models response, using fallback"));
		SessionModelState.AvailableModels = GetCuratedModels();
		OnModelsAvailable.Broadcast(SessionModelState);
		return;
	}

	const TArray<TSharedPtr<FJsonValue>>* DataArray;
	if (!JsonRoot->TryGetArrayField(TEXT("data"), DataArray))
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("OpenRouterClient: No 'data' field in models response, using fallback"));
		SessionModelState.AvailableModels = GetCuratedModels();
		OnModelsAvailable.Broadcast(SessionModelState);
		return;
	}

	TArray<FACPModelInfo> AllModels;
	
	for (const TSharedPtr<FJsonValue>& ModelVal : *DataArray)
	{
		TSharedPtr<FJsonObject> ModelObj = ModelVal->AsObject();
		if (!ModelObj.IsValid()) continue;

		FACPModelInfo ModelInfo;
		ModelObj->TryGetStringField(TEXT("id"), ModelInfo.ModelId);
		ModelObj->TryGetStringField(TEXT("name"), ModelInfo.Name);
		ModelObj->TryGetStringField(TEXT("description"), ModelInfo.Description);

		// Parse supported_parameters array
		const TArray<TSharedPtr<FJsonValue>>* SupportedParamsArray;
		if (ModelObj->TryGetArrayField(TEXT("supported_parameters"), SupportedParamsArray))
		{
			for (const TSharedPtr<FJsonValue>& ParamVal : *SupportedParamsArray)
			{
				FString ParamStr;
				if (ParamVal->TryGetString(ParamStr))
				{
					ModelInfo.SupportedParameters.Add(ParamStr);
				}
			}
		}

		if (ModelInfo.ModelId.IsEmpty()) continue;

		AllModels.Add(ModelInfo);
	}

	// Sort alphabetically
	AllModels.Sort([](const FACPModelInfo& A, const FACPModelInfo& B)
	{
		return A.Name < B.Name;
	});

	// Store full list in cache
	CachedModels = AllModels;
	LastModelsFetch = FDateTime::Now();
	
	// Expose only curated list to dropdown
	SessionModelState.AvailableModels = GetCuratedModels();

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Fetched and cached %d models. Showing %d curated."),
		AllModels.Num(), SessionModelState.AvailableModels.Num());

	OnModelsAvailable.Broadcast(SessionModelState);
}

bool FOpenRouterClient::Connect(const FACPAgentConfig& Config)
{
	if (IsConnected())
	{
		Disconnect();
	}

	CurrentConfig = Config;
	SetState(EACPClientState::Connecting, TEXT("Connecting to OpenRouter..."));

	// Validate auth token and route target
	ApiKey = Config.ApiKey;
	BaseUrl = TEXT("https://openrouter.ai/api/v1/chat/completions");
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		ApiKey = Settings->GetOpenRouterAuthToken();
		BaseUrl = Settings->GetOpenRouterChatCompletionsUrl();
	}

	if (ApiKey.IsEmpty())
	{
		SetState(EACPClientState::Error, TEXT("OpenRouter credentials are required"));
		return false;
	}

	// Set model (use config or default)
	Model = Config.ModelId.IsEmpty() ? TEXT("anthropic/claude-sonnet-4") : Config.ModelId;
	SessionModelState.CurrentModelId = Model;

	// OpenRouter doesn't require initialization like ACP agents
	// We go directly to Ready state
	SetState(EACPClientState::Ready, TEXT("Connected to OpenRouter"));

	FetchModelsFromOpenRouter();

	return true;
}

void FOpenRouterClient::Disconnect()
{
	// Cancel any pending request
	if (CurrentRequest.IsValid())
	{
		CurrentRequest->CancelRequest();
		CurrentRequest.Reset();
	}

	// Clear state
	{
		FScopeLock Lock(&StateLock);
		ConversationHistory.Empty();
		CurrentSessionId.Empty();
		StreamBuffer.Empty();
		CurrentResponseText.Empty();
		LastProcessedLength = 0;
	}

	SetState(EACPClientState::Disconnected, TEXT("Disconnected"));
}

void FOpenRouterClient::NewSession(const FString& WorkingDirectory)
{
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Creating new session"));

	// Generate a session ID
	CurrentSessionId = FGuid::NewGuid().ToString();

	// Clear conversation history and reset usage
	{
		FScopeLock Lock(&StateLock);
		ConversationHistory.Empty();
		SessionUsage = FACPUsageData();
	}

	// Add system message
	FOpenRouterMessage SystemMessage;
	SystemMessage.Role = TEXT("system");
	FString SystemPrompt = TEXT("You are a helpful AI assistant integrated into Unreal Engine. Help the user with their game development tasks.");

	// Append custom system prompt + active profile instructions
	UACPSettings* Settings = UACPSettings::Get();
	if (Settings)
	{
		FString EffectivePrompt = Settings->GetProfileSystemPromptAppend();
		if (!EffectivePrompt.IsEmpty())
		{
			SystemPrompt += TEXT("\n\n") + EffectivePrompt;
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Added custom system prompt (with profile instructions)"));
		}
	}

	SystemMessage.Content = SystemPrompt;
	ConversationHistory.Add(SystemMessage);

	SetState(EACPClientState::InSession, TEXT("Session started"));

	// Broadcast models available now that we're in session
	OnModelsAvailable.Broadcast(SessionModelState);
}

void FOpenRouterClient::LoadSession(const FString& SessionId)
{
	// OpenRouter doesn't support loading sessions
	UE_LOG(LogAgentIntegrationKit, Warning, TEXT("OpenRouterClient: LoadSession not supported"));
}

void FOpenRouterClient::SendPrompt(const FString& PromptText)
{
	if (ApiKey.IsEmpty())
	{
		OnError.Broadcast(-1, TEXT("API key not configured"));
		return;
	}

	SetState(EACPClientState::Prompting, TEXT("Processing..."));

	// Reset streaming state
	{
		FScopeLock Lock(&StateLock);
		StreamBuffer.Empty();
		CurrentResponseText.Empty();
		CurrentReasoningText.Empty();
		LastProcessedLength = 0;
		bIsCancelled = false;
		PendingToolCalls.Empty();
		CurrentToolCalls.Empty();
		CurrentToolCallIndex = 0;
		bIsProcessingTools = false;
	}

	// Build user message with attachment context prepended
	FString MessageContent = PromptText;

	// If there are attachments, prepend them as markdown context
	if (FACPAttachmentManager::Get().HasAttachments())
	{
		FString ContextMarkdown = FACPAttachmentManager::Get().SerializeAsMarkdown();
		MessageContent = ContextMarkdown + TEXT("## User Request\n\n") + PromptText;

		// Clear attachments after incorporating (one-shot context)
		FACPAttachmentManager::Get().ClearAllAttachments();
	}

	// Add user message to history
	FOpenRouterMessage UserMessage;
	UserMessage.Role = TEXT("user");
	UserMessage.Content = MessageContent;
	ConversationHistory.Add(UserMessage);

	// Build request body (includes tools)
	FString RequestBody = BuildRequestBody();

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	CurrentRequest = Request;

	Request->SetURL(BaseUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	Request->SetHeader(TEXT("HTTP-Referer"), TEXT("https://github.com/betidestudio/AgentIntegrationKit"));
	Request->SetHeader(TEXT("X-Title"), TEXT("Agent Integration Kit"));
	Request->SetContentAsString(RequestBody);

	Request->OnProcessRequestComplete().BindRaw(this, &FOpenRouterClient::OnRequestComplete);
	Request->OnRequestProgress64().BindRaw(this, &FOpenRouterClient::OnRequestProgress64);

	// Send request
	if (!Request->ProcessRequest())
	{
		SetState(EACPClientState::InSession, TEXT("Ready"));
		OnError.Broadcast(-1, TEXT("Failed to send request to OpenRouter"));
	}
}

void FOpenRouterClient::CancelPrompt()
{
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Cancelling prompt"));

	bIsCancelled = true;

	if (CurrentRequest.IsValid())
	{
		CurrentRequest->CancelRequest();
		CurrentRequest.Reset();
	}

	SetState(EACPClientState::InSession, TEXT("Cancelled"));
}

void FOpenRouterClient::SetModel(const FString& ModelId)
{
	// UI sentinel action, not a real model ID.
	if (ModelId.StartsWith(TEXT("special:")))
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Ignoring special model action '%s'"), *ModelId);
		return;
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Setting model to %s"), *ModelId);

	Model = ModelId;
	SessionModelState.CurrentModelId = ModelId;
}

void FOpenRouterClient::SetMode(const FString& ModeId)
{
	// OpenRouter doesn't have modes, but we support the interface
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: SetMode called with %s (no-op)"), *ModeId);
}

void FOpenRouterClient::RespondToPermissionRequest(int32 RequestId, const FString& OptionId)
{
	// OpenRouter doesn't have permission requests
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: RespondToPermissionRequest called (no-op)"));
}

TArray<FOpenRouterMessage> FOpenRouterClient::GetConversationHistory() const
{
	FScopeLock Lock(&StateLock);
	return ConversationHistory;
}

void FOpenRouterClient::RestoreConversationHistory(const TArray<FOpenRouterMessage>& History)
{
	FScopeLock Lock(&StateLock);
	ConversationHistory = History;
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Restored conversation history with %d messages"), History.Num());

	if (State != EACPClientState::InSession && State != EACPClientState::Prompting)
	{
		SetState(EACPClientState::InSession, TEXT("Session restored"));
	}
}

void FOpenRouterClient::SetState(EACPClientState NewState, const FString& Message)
{
	{
		FScopeLock Lock(&StateLock);
		State = NewState;
	}

	// Don't broadcast during engine shutdown - the task system may be torn down
	if (IsEngineExitRequested())
	{
		return;
	}

	// Broadcast on game thread
	AsyncTask(ENamedThreads::GameThread, [this, NewState, Message]()
	{
		OnStateChanged.Broadcast(NewState, Message);
	});
}

FString FOpenRouterClient::BuildRequestBody()
{
	TSharedRef<FJsonObject> RequestObj = MakeShared<FJsonObject>();

	RequestObj->SetStringField(TEXT("model"), Model);
	RequestObj->SetBoolField(TEXT("stream"), true);

	// Build messages array
	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FOpenRouterMessage& Msg : ConversationHistory)
	{
		TSharedPtr<FJsonObject> MsgObj = MakeShared<FJsonObject>();
		MsgObj->SetStringField(TEXT("role"), Msg.Role);

		// Handle tool response messages
		if (Msg.Role == TEXT("tool"))
		{
			MsgObj->SetStringField(TEXT("tool_call_id"), Msg.ToolCallId);
			MsgObj->SetStringField(TEXT("content"), Msg.Content);
		}
		// Handle assistant messages with tool calls
		else if (Msg.Role == TEXT("assistant") && Msg.ToolCalls.Num() > 0)
		{
			// Content can be null when there are tool calls
			if (!Msg.Content.IsEmpty())
			{
				MsgObj->SetStringField(TEXT("content"), Msg.Content);
			}
			else
			{
				MsgObj->SetField(TEXT("content"), MakeShared<FJsonValueNull>());
			}

			// Add tool_calls array
			TArray<TSharedPtr<FJsonValue>> ToolCallsArray;
			for (const FOpenRouterToolCall& TC : Msg.ToolCalls)
			{
				TSharedPtr<FJsonObject> TCObj = MakeShared<FJsonObject>();
				TCObj->SetStringField(TEXT("id"), TC.Id);
				TCObj->SetStringField(TEXT("type"), TEXT("function"));

				TSharedPtr<FJsonObject> FuncObj = MakeShared<FJsonObject>();
				FuncObj->SetStringField(TEXT("name"), TC.Name);
				FuncObj->SetStringField(TEXT("arguments"), TC.Arguments);
				TCObj->SetObjectField(TEXT("function"), FuncObj);

				ToolCallsArray.Add(MakeShared<FJsonValueObject>(TCObj));
			}
			MsgObj->SetArrayField(TEXT("tool_calls"), ToolCallsArray);
		}
		// Regular message
		else
		{
			MsgObj->SetStringField(TEXT("content"), Msg.Content);
		}

		MessagesArray.Add(MakeShared<FJsonValueObject>(MsgObj));
	}
	RequestObj->SetArrayField(TEXT("messages"), MessagesArray);

	// Build tools array from MCP registered tools
	TArray<TSharedPtr<FJsonValue>> ToolsArray = BuildToolsArray();
	if (ToolsArray.Num() > 0)
	{
		RequestObj->SetArrayField(TEXT("tools"), ToolsArray);
	}

	// Optional parameters
	RequestObj->SetNumberField(TEXT("temperature"), 0.7);
	RequestObj->SetNumberField(TEXT("max_tokens"), 16384);

	// Add reasoning support if enabled and model supports it
	if (bReasoningEnabled && CurrentModelSupportsReasoning())
	{
		TSharedPtr<FJsonObject> ReasoningObj = MakeShared<FJsonObject>();
		ReasoningObj->SetStringField(TEXT("effort"), ReasoningEffort);
		RequestObj->SetObjectField(TEXT("reasoning"), ReasoningObj);

		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Reasoning enabled with effort '%s'"), *ReasoningEffort);
	}

	// Serialize to string
	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	FJsonSerializer::Serialize(RequestObj, Writer);

	return OutputString;
}

TArray<TSharedPtr<FJsonValue>> FOpenRouterClient::BuildToolsArray()
{
	const UACPSettings* Settings = UACPSettings::Get();
	TArray<TSharedPtr<FJsonValue>> ToolsArray;

	// Get tools from MCP server
	const TMap<FString, FMCPToolDefinition>& MCPTools = FMCPServer::Get().GetRegisteredTools();

	for (const auto& Pair : MCPTools)
	{
		if (Settings && !Settings->IsToolEnabled(Pair.Key))
		{
			continue;
		}

		const FMCPToolDefinition& Tool = Pair.Value;

		// Apply description override from active profile
		FString EffectiveDesc = Tool.Description;
		if (Settings)
		{
			EffectiveDesc = Settings->GetEffectiveToolDescription(Tool.Name, Tool.Description);
		}

		TSharedPtr<FJsonObject> ToolDef = BuildToolDefinition(Tool.Name, EffectiveDesc, Tool.InputSchema);
		if (ToolDef.IsValid())
		{
			ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDef));
		}
	}

	// Also get tools from NeoStack registry
	FNeoStackToolRegistry& Registry = FNeoStackToolRegistry::Get();
	TArray<FString> ToolNames = Registry.GetToolNames();

	for (const FString& ToolName : ToolNames)
	{
		// Skip if already added from MCP or disabled
		if (MCPTools.Contains(ToolName))
		{
			continue;
		}
		if (Settings && !Settings->IsToolEnabled(ToolName))
		{
			continue;
		}

		FNeoStackToolBase* Tool = Registry.GetTool(ToolName);
		if (Tool)
		{
			TSharedPtr<FJsonObject> Schema = Tool->GetInputSchema();
			if (!Schema.IsValid())
			{
				Schema = MakeShared<FJsonObject>();
				Schema->SetStringField(TEXT("type"), TEXT("object"));
				Schema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
			}

			// Apply description override from active profile
			FString EffectiveDesc = Tool->GetDescription();
			if (Settings)
			{
				EffectiveDesc = Settings->GetEffectiveToolDescription(Tool->GetName(), Tool->GetDescription());
			}

			TSharedPtr<FJsonObject> ToolDef = BuildToolDefinition(Tool->GetName(), EffectiveDesc, Schema);
			if (ToolDef.IsValid())
			{
				ToolsArray.Add(MakeShared<FJsonValueObject>(ToolDef));
			}
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Built %d tools for API request"), ToolsArray.Num());

	return ToolsArray;
}

TSharedPtr<FJsonObject> FOpenRouterClient::BuildToolDefinition(const FString& ToolName, const FString& Description, TSharedPtr<FJsonObject> InputSchema)
{
	TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
	ToolObj->SetStringField(TEXT("type"), TEXT("function"));

	TSharedPtr<FJsonObject> FunctionObj = MakeShared<FJsonObject>();
	FunctionObj->SetStringField(TEXT("name"), ToolName);
	FunctionObj->SetStringField(TEXT("description"), Description);

	// Use provided schema or create empty one
	if (InputSchema.IsValid())
	{
		FunctionObj->SetObjectField(TEXT("parameters"), InputSchema);
	}
	else
	{
		TSharedPtr<FJsonObject> EmptySchema = MakeShared<FJsonObject>();
		EmptySchema->SetStringField(TEXT("type"), TEXT("object"));
		EmptySchema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
		FunctionObj->SetObjectField(TEXT("parameters"), EmptySchema);
	}

	ToolObj->SetObjectField(TEXT("function"), FunctionObj);

	return ToolObj;
}

void FOpenRouterClient::OnRequestProgress64(FHttpRequestPtr Request, uint64 BytesSent, uint64 BytesReceived)
{
	if (bIsCancelled)
	{
		return;
	}

	// Get response content so far
	FHttpResponsePtr Response = Request->GetResponse();
	if (!Response.IsValid())
	{
		return;
	}

	FString Content = Response->GetContentAsString();

	// Only process new data
	if (Content.Len() > LastProcessedLength)
	{
		FString NewData = Content.Mid(LastProcessedLength);
		LastProcessedLength = Content.Len();

		// Add to buffer and process
		StreamBuffer += NewData;
		ProcessStreamBuffer();
	}
}

void FOpenRouterClient::OnRequestComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	CurrentRequest.Reset();

	if (bIsCancelled)
	{
		return;
	}

	if (!bSuccess || !Response.IsValid())
	{
		FString ErrorMsg = TEXT("Request failed");
		if (Response.IsValid())
		{
			ErrorMsg = FString::Printf(TEXT("HTTP %d: %s"), Response->GetResponseCode(), *Response->GetContentAsString());
		}

		UE_LOG(LogAgentIntegrationKit, Error, TEXT("OpenRouterClient: %s"), *ErrorMsg);

		AsyncTask(ENamedThreads::GameThread, [this, ErrorMsg]()
		{
			SetState(EACPClientState::InSession, TEXT("Ready"));
			OnError.Broadcast(-1, ErrorMsg);
		});
		return;
	}

	int32 ResponseCode = Response->GetResponseCode();
	if (ResponseCode != 200)
	{
		const FString ResponseBody = Response->GetContentAsString();
		bool bUsingNeoStackCredits = false;
		if (const UACPSettings* Settings = UACPSettings::Get())
		{
			bUsingNeoStackCredits = Settings->ShouldUseBetideCredits();
		}

		const FString ErrorMsg = BuildOpenRouterErrorMessage(ResponseCode, ResponseBody, bUsingNeoStackCredits);

		UE_LOG(LogAgentIntegrationKit, Error, TEXT("OpenRouterClient: %s"), *ErrorMsg);

		AsyncTask(ENamedThreads::GameThread, [this, ResponseCode, ErrorMsg]()
		{
			SetState(EACPClientState::InSession, TEXT("Ready"));
			OnError.Broadcast(ResponseCode, ErrorMsg);
		});
		return;
	}

	// Process any remaining data in buffer
	FString FinalContent = Response->GetContentAsString();
	if (FinalContent.Len() > LastProcessedLength)
	{
		StreamBuffer += FinalContent.Mid(LastProcessedLength);
		ProcessStreamBuffer();
	}

	// Check if we have tool calls to process
	if (CurrentToolCalls.Num() > 0)
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Response includes %d tool calls"), CurrentToolCalls.Num());

		// Add assistant message with tool calls to history
		FOpenRouterMessage AssistantMessage;
		AssistantMessage.Role = TEXT("assistant");
		AssistantMessage.Content = CurrentResponseText;
		AssistantMessage.ToolCalls = CurrentToolCalls;
		ConversationHistory.Add(AssistantMessage);

		// Copy tool calls to pending and process them
		PendingToolCalls = CurrentToolCalls;
		CurrentToolCalls.Empty();
		CurrentToolCallIndex = 0;
		bIsProcessingTools = true;

		// Start processing tool calls on game thread
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			ProcessToolCalls(PendingToolCalls);
		});
	}
	else
	{
		// No tool calls - this is a final response
		// Add assistant response to history
		if (!CurrentResponseText.IsEmpty())
		{
			FOpenRouterMessage AssistantMessage;
			AssistantMessage.Role = TEXT("assistant");
			AssistantMessage.Content = CurrentResponseText;
			ConversationHistory.Add(AssistantMessage);
		}

		// Broadcast completion response
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetStringField(TEXT("text"), CurrentResponseText);
			OnResponse.Broadcast(Result);

			SetState(EACPClientState::InSession, TEXT("Ready"));
		});
	}
}

void FOpenRouterClient::ProcessStreamBuffer()
{
	// Process complete SSE lines (double newline separated)
	int32 Pos;
	while (StreamBuffer.FindChar(TEXT('\n'), Pos))
	{
		FString Line = StreamBuffer.Left(Pos);
		StreamBuffer = StreamBuffer.Mid(Pos + 1);

		Line.TrimStartAndEndInline();
		if (!Line.IsEmpty())
		{
			ProcessSSELine(Line);
		}
	}
}

void FOpenRouterClient::ProcessSSELine(const FString& Line)
{
	// SSE format: "data: {json}"
	if (!Line.StartsWith(TEXT("data: ")))
	{
		return;
	}

	FString JsonStr = Line.Mid(6); // Skip "data: "

	// Check for stream end
	if (JsonStr == TEXT("[DONE]"))
	{
		return;
	}

	// Parse JSON
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Failed to parse SSE JSON: %s"), *JsonStr);
		return;
	}

	// Check for usage object (present in the final SSE chunk)
	if (JsonObject->HasField(TEXT("usage")))
	{
		TSharedPtr<FJsonObject> UsageObj = JsonObject->GetObjectField(TEXT("usage"));
		if (UsageObj.IsValid())
		{
			FACPUsageData TurnUsage;

			// Parse token counts
			int32 PromptTokens = 0, CompletionTokens = 0, TotalTokens = 0;
			UsageObj->TryGetNumberField(TEXT("prompt_tokens"), PromptTokens);
			UsageObj->TryGetNumberField(TEXT("completion_tokens"), CompletionTokens);
			UsageObj->TryGetNumberField(TEXT("total_tokens"), TotalTokens);

			TurnUsage.InputTokens = PromptTokens;
			TurnUsage.OutputTokens = CompletionTokens;
			TurnUsage.TotalTokens = TotalTokens;

			// Parse cached tokens if available
			if (UsageObj->HasField(TEXT("prompt_tokens_details")))
			{
				TSharedPtr<FJsonObject> PromptDetails = UsageObj->GetObjectField(TEXT("prompt_tokens_details"));
				if (PromptDetails.IsValid())
				{
					int32 CachedTokens = 0;
					PromptDetails->TryGetNumberField(TEXT("cached_tokens"), CachedTokens);
					TurnUsage.CachedTokens = CachedTokens;
				}
			}

			// Parse reasoning tokens if available
			if (UsageObj->HasField(TEXT("completion_tokens_details")))
			{
				TSharedPtr<FJsonObject> CompletionDetails = UsageObj->GetObjectField(TEXT("completion_tokens_details"));
				if (CompletionDetails.IsValid())
				{
					int32 ReasoningTokens = 0;
					CompletionDetails->TryGetNumberField(TEXT("reasoning_tokens"), ReasoningTokens);
					TurnUsage.ReasoningTokens = ReasoningTokens;
				}
			}

			// Parse cost (OpenRouter specific)
			// Try regular cost first, fall back to upstream_inference_cost for BYOK users
			double Cost = 0.0;
			if (UsageObj->TryGetNumberField(TEXT("cost"), Cost) && Cost > 0.0)
			{
				TurnUsage.CostAmount = Cost;
				TurnUsage.CostCurrency = TEXT("USD");
			}
			else if (UsageObj->HasField(TEXT("cost_details")))
			{
				TSharedPtr<FJsonObject> CostDetails = UsageObj->GetObjectField(TEXT("cost_details"));
				if (CostDetails.IsValid())
				{
					double UpstreamCost = 0.0;
					if (CostDetails->TryGetNumberField(TEXT("upstream_inference_cost"), UpstreamCost) && UpstreamCost > 0.0)
					{
						TurnUsage.CostAmount = UpstreamCost;
						TurnUsage.CostCurrency = TEXT("USD");
					}
				}
			}

			// Accumulate to session totals
			SessionUsage.InputTokens += TurnUsage.InputTokens;
			SessionUsage.OutputTokens += TurnUsage.OutputTokens;
			SessionUsage.TotalTokens += TurnUsage.TotalTokens;
			SessionUsage.CachedTokens += TurnUsage.CachedTokens;
			SessionUsage.ReasoningTokens += TurnUsage.ReasoningTokens;
			SessionUsage.CostAmount += TurnUsage.CostAmount;
			SessionUsage.CostCurrency = TurnUsage.CostCurrency;

			UE_LOG(LogAgentIntegrationKit, Log, TEXT("OpenRouterClient: Usage - Turn: %d tokens, $%.6f | Session: %d tokens, $%.6f"),
				TurnUsage.TotalTokens, TurnUsage.CostAmount, SessionUsage.TotalTokens, SessionUsage.CostAmount);

			// Broadcast usage update
			FACPSessionUpdate UsageUpdate;
			UsageUpdate.UpdateType = EACPUpdateType::UsageUpdate;
			UsageUpdate.Usage = SessionUsage;

			AsyncTask(ENamedThreads::GameThread, [this, UsageUpdate]()
			{
				OnSessionUpdate.Broadcast(UsageUpdate);
			});
		}
	}

	// Extract content from choices[0].delta
	const TArray<TSharedPtr<FJsonValue>>* ChoicesArray;
	if (!JsonObject->TryGetArrayField(TEXT("choices"), ChoicesArray) || ChoicesArray->Num() == 0)
	{
		return;
	}

	TSharedPtr<FJsonObject> ChoiceObj = (*ChoicesArray)[0]->AsObject();
	if (!ChoiceObj.IsValid())
	{
		return;
	}

	TSharedPtr<FJsonObject> DeltaObj = ChoiceObj->GetObjectField(TEXT("delta"));
	if (!DeltaObj.IsValid())
	{
		return;
	}

	// Check for tool calls in delta
	const TArray<TSharedPtr<FJsonValue>>* ToolCallsArray;
	if (DeltaObj->TryGetArrayField(TEXT("tool_calls"), ToolCallsArray))
	{
		for (const TSharedPtr<FJsonValue>& TCValue : *ToolCallsArray)
		{
			TSharedPtr<FJsonObject> TCObj = TCValue->AsObject();
			if (!TCObj.IsValid())
			{
				continue;
			}

			int32 Index = 0;
			TCObj->TryGetNumberField(TEXT("index"), Index);

			// Ensure CurrentToolCalls has enough elements
			while (CurrentToolCalls.Num() <= Index)
			{
				CurrentToolCalls.Add(FOpenRouterToolCall());
			}

			// Get or create the tool call at this index
			FOpenRouterToolCall& ToolCall = CurrentToolCalls[Index];

			// Extract id if present
			FString Id;
			if (TCObj->TryGetStringField(TEXT("id"), Id))
			{
				ToolCall.Id = Id;
			}

			// Extract function info if present
			TSharedPtr<FJsonObject> FuncObj = TCObj->GetObjectField(TEXT("function"));
			if (FuncObj.IsValid())
			{
				FString Name;
				if (FuncObj->TryGetStringField(TEXT("name"), Name))
				{
					ToolCall.Name = Name;
				}

				FString Args;
				if (FuncObj->TryGetStringField(TEXT("arguments"), Args))
				{
					ToolCall.Arguments += Args; // Arguments stream in chunks
				}
			}
		}
	}

	// Check for reasoning_details (reasoning tokens from OpenRouter)
	const TArray<TSharedPtr<FJsonValue>>* ReasoningDetailsArray;
	if (DeltaObj->TryGetArrayField(TEXT("reasoning_details"), ReasoningDetailsArray))
	{
		for (const TSharedPtr<FJsonValue>& DetailValue : *ReasoningDetailsArray)
		{
			TSharedPtr<FJsonObject> DetailObj = DetailValue->AsObject();
			if (!DetailObj.IsValid())
			{
				continue;
			}

			FString ReasoningType;
			DetailObj->TryGetStringField(TEXT("type"), ReasoningType);

			FString ReasoningText;
			if (ReasoningType == TEXT("reasoning.text"))
			{
				DetailObj->TryGetStringField(TEXT("text"), ReasoningText);
			}
			else if (ReasoningType == TEXT("reasoning.summary"))
			{
				DetailObj->TryGetStringField(TEXT("summary"), ReasoningText);
			}

			if (!ReasoningText.IsEmpty())
			{
				CurrentReasoningText += ReasoningText;

				// Send reasoning as thought chunk
				FACPSessionUpdate ReasoningUpdate;
				ReasoningUpdate.UpdateType = EACPUpdateType::AgentThoughtChunk;
				ReasoningUpdate.TextChunk = ReasoningText;

				AsyncTask(ENamedThreads::GameThread, [this, ReasoningUpdate]()
				{
					OnSessionUpdate.Broadcast(ReasoningUpdate);
				});
			}
		}
	}

	// Check for regular content
	FString Content;
	if (DeltaObj->TryGetStringField(TEXT("content"), Content) && !Content.IsEmpty())
	{
		// Accumulate response
		CurrentResponseText += Content;

		// Send streaming update
		FACPSessionUpdate Update;
		Update.UpdateType = EACPUpdateType::AgentMessageChunk;
		Update.TextChunk = Content;

		AsyncTask(ENamedThreads::GameThread, [this, Update]()
		{
			OnSessionUpdate.Broadcast(Update);
		});
	}
}

// ============================================================================
// Tool Calling Implementation
// ============================================================================

void FOpenRouterClient::ProcessToolCalls(const TArray<FOpenRouterToolCall>& ToolCalls)
{
	if (bIsCancelled)
	{
		SetState(EACPClientState::InSession, TEXT("Cancelled"));
		return;
	}

	if (ToolCalls.Num() == 0)
	{
		// No more tool calls - continue conversation
		ContinueAfterToolExecution();
		return;
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Processing %d tool calls"), ToolCalls.Num());

	// Execute each tool call sequentially
	for (const FOpenRouterToolCall& ToolCall : ToolCalls)
	{
		if (bIsCancelled)
		{
			break;
		}

		ExecuteToolCall(ToolCall);
	}

	// After all tools are executed, continue the conversation
	if (!bIsCancelled)
	{
		ContinueAfterToolExecution();
	}
}

void FOpenRouterClient::ExecuteToolCall(const FOpenRouterToolCall& ToolCall)
{
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Executing tool '%s' (id: %s)"), *ToolCall.Name, *ToolCall.Id);

	// Broadcast tool call start to UI
	BroadcastToolCallStart(ToolCall);

	FString ResultContent;
	bool bSuccess = false;
	TArray<FACPToolResultImage> ResultImages;

	// Try to execute via NeoStack registry first
	FNeoStackToolRegistry& Registry = FNeoStackToolRegistry::Get();
	if (Registry.HasTool(ToolCall.Name))
	{
		FToolResult Result = Registry.Execute(ToolCall.Name, ToolCall.Arguments);
		bSuccess = Result.bSuccess;
		ResultContent = Result.Output;

		// Copy images from tool result
		for (const FToolResultImage& Img : Result.Images)
		{
			FACPToolResultImage ACPImage;
			ACPImage.Base64Data = Img.Base64Data;
			ACPImage.MimeType = Img.MimeType;
			ACPImage.Width = Img.Width;
			ACPImage.Height = Img.Height;
			ResultImages.Add(ACPImage);
		}
	}
	// Try MCP server registered tools
	else
	{
		const TMap<FString, FMCPToolDefinition>& MCPTools = FMCPServer::Get().GetRegisteredTools();
		if (const FMCPToolDefinition* Tool = MCPTools.Find(ToolCall.Name))
		{
			// Parse arguments JSON
			TSharedPtr<FJsonObject> ArgsObj;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ToolCall.Arguments);
			if (FJsonSerializer::Deserialize(Reader, ArgsObj) && ArgsObj.IsValid())
			{
				FMCPToolResult Result = Tool->Handler(ArgsObj);
				bSuccess = Result.bSuccess;
				ResultContent = bSuccess ? Result.Content : Result.ErrorMessage;

				// Copy images from MCP tool result
				for (const FMCPToolResultImage& Img : Result.Images)
				{
					FACPToolResultImage ACPImage;
					ACPImage.Base64Data = Img.Base64Data;
					ACPImage.MimeType = Img.MimeType;
					ACPImage.Width = Img.Width;
					ACPImage.Height = Img.Height;
					ResultImages.Add(ACPImage);
				}
			}
			else
			{
				bSuccess = false;
				ResultContent = TEXT("Failed to parse tool arguments as JSON");
			}
		}
		else
		{
			bSuccess = false;
			ResultContent = FString::Printf(TEXT("Tool '%s' not found"), *ToolCall.Name);
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Tool '%s' result: %s (success: %d, images: %d)"),
		*ToolCall.Name, *ResultContent.Left(200), bSuccess, ResultImages.Num());

	// Broadcast tool result to UI
	BroadcastToolCallResult(ToolCall.Id, bSuccess, ResultContent, ResultImages);

	// Add tool result message to conversation history
	FOpenRouterMessage ToolResultMessage;
	ToolResultMessage.Role = TEXT("tool");
	ToolResultMessage.ToolCallId = ToolCall.Id;
	ToolResultMessage.Content = ResultContent;
	ConversationHistory.Add(ToolResultMessage);
}

void FOpenRouterClient::ContinueAfterToolExecution()
{
	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("OpenRouterClient: Continuing conversation after tool execution"));

	// Reset streaming state for next request
	{
		FScopeLock Lock(&StateLock);
		StreamBuffer.Empty();
		CurrentResponseText.Empty();
		CurrentReasoningText.Empty();
		LastProcessedLength = 0;
		CurrentToolCalls.Empty();
		bIsProcessingTools = false;
	}

	// Build new request body (will include tool results in conversation)
	FString RequestBody = BuildRequestBody();

	// Create HTTP request
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	CurrentRequest = Request;

	Request->SetURL(BaseUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	Request->SetHeader(TEXT("HTTP-Referer"), TEXT("https://github.com/betidestudio/AgentIntegrationKit"));
	Request->SetHeader(TEXT("X-Title"), TEXT("Agent Integration Kit"));
	Request->SetContentAsString(RequestBody);

	Request->OnProcessRequestComplete().BindRaw(this, &FOpenRouterClient::OnRequestComplete);
	Request->OnRequestProgress64().BindRaw(this, &FOpenRouterClient::OnRequestProgress64);

	// Send request
	if (!Request->ProcessRequest())
	{
		SetState(EACPClientState::InSession, TEXT("Ready"));
		OnError.Broadcast(-1, TEXT("Failed to continue conversation after tool execution"));
	}
}

void FOpenRouterClient::BroadcastToolCallStart(const FOpenRouterToolCall& ToolCall)
{
	FACPSessionUpdate Update;
	Update.UpdateType = EACPUpdateType::ToolCall;
	Update.ToolCallId = ToolCall.Id;
	Update.ToolName = ToolCall.Name;
	Update.ToolArguments = ToolCall.Arguments;

	OnSessionUpdate.Broadcast(Update);
}

void FOpenRouterClient::BroadcastToolCallResult(const FString& ToolCallId, bool bSuccess, const FString& Result, const TArray<FACPToolResultImage>& Images)
{
	FACPSessionUpdate Update;
	Update.UpdateType = EACPUpdateType::ToolCallUpdate;
	Update.ToolCallId = ToolCallId;
	Update.ToolResult = Result;
	Update.bToolSuccess = bSuccess;
	Update.ToolResultImages = Images;
	OnSessionUpdate.Broadcast(Update);
}
