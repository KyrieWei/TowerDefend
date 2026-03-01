// Copyright 2026 Betide Studio. All Rights Reserved.

#include "WebUIBridge.h"
#include "AgentIntegrationKitModule.h"
#include "ACPAgentManager.h"
#include "ACPClient.h"
#include "ACPSessionManager.h"
#include "ACPClaudeCodeHistoryReader.h"
#include "ACPSettings.h"
#include "ACPTypes.h"
#include "AgentInstaller.h"
#include "AgentUsageMonitor.h"
#include "ACPAttachmentManager.h"
#include "ACPClipboardImageReader.h"
#include "OpenRouterClient.h"
#include "MCPServer.h"
#include "MCPTypes.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "Framework/Application/SlateApplication.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Blueprint.h"
#include "WidgetBlueprint.h"
#include "Animation/AnimBlueprint.h"
#include "BehaviorTree/BehaviorTree.h"
#include "Materials/Material.h"
#include "Engine/DataTable.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformApplicationMisc.h"
#include "ISettingsModule.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"
#include "SourceCodeNavigation.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Tools/ReadFileTool.h"
#include "ISourceControlModule.h"
#include "ISourceControlProvider.h"
#include "ISourceControlWindowsModule.h"
#include "SourceControlWindows.h"
#include "UnrealEdMisc.h"

// Helper: serialize a FJsonObject to compact JSON string
static FString JsonToString(const TSharedRef<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Obj, Writer);
	return Out;
}

// Helper: serialize a JSON array to compact string
static FString JsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& Arr)
{
	FString Out;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
	FJsonSerializer::Serialize(Arr, Writer);
	return Out;
}

// ── Agent Discovery ──────────────────────────────────────────────────

FString UWebUIBridge::GetAgents()
{
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	TArray<FACPAgentConfig> Configs = AgentMgr.GetAllAgentConfigs();

	TArray<TSharedPtr<FJsonValue>> AgentsArray;

	for (const FACPAgentConfig& Config : Configs)
	{
		TSharedPtr<FJsonObject> AgentObj = MakeShared<FJsonObject>();
		AgentObj->SetStringField(TEXT("id"), Config.AgentName);
		AgentObj->SetStringField(TEXT("name"), Config.AgentName);

		// Map status enum to string
		FString StatusStr;
		switch (Config.Status)
		{
		case EACPAgentStatus::Available:     StatusStr = TEXT("available"); break;
		case EACPAgentStatus::NotInstalled:  StatusStr = TEXT("not_installed"); break;
		case EACPAgentStatus::MissingApiKey: StatusStr = TEXT("missing_key"); break;
		default:                             StatusStr = TEXT("unknown"); break;
		}
		AgentObj->SetStringField(TEXT("status"), StatusStr);
		AgentObj->SetStringField(TEXT("statusMessage"), Config.StatusMessage);
		AgentObj->SetBoolField(TEXT("isBuiltIn"), Config.bIsBuiltIn);
		AgentObj->SetBoolField(TEXT("isConnected"), AgentMgr.IsConnectedToAgent(Config.AgentName));

		AgentsArray.Add(MakeShared<FJsonValueObject>(AgentObj));
	}

	return JsonArrayToString(AgentsArray);
}

FString UWebUIBridge::GetLastUsedAgent()
{
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		return Settings->LastUsedAgentName;
	}
	return FString();
}

// ── Onboarding ──────────────────────────────────────────────────────

bool UWebUIBridge::GetOnboardingCompleted()
{
	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings)
	{
		return true; // Never block the user if settings unavailable
	}

	// Auto-upgrade: if they've used the plugin before, skip onboarding
	if (!Settings->bOnboardingCompleted && !Settings->LastUsedAgentName.IsEmpty())
	{
		Settings->bOnboardingCompleted = true;
		Settings->SaveConfig();
	}

	return Settings->bOnboardingCompleted;
}

void UWebUIBridge::SetOnboardingCompleted()
{
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->bOnboardingCompleted = true;
		Settings->SaveConfig();
	}
}

// ── Session Lifecycle ────────────────────────────────────────────────

FString UWebUIBridge::CreateSession(const FString& AgentName)
{
	FACPSessionManager& SessionMgr = FACPSessionManager::Get();
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();

	FString SessionId = SessionMgr.CreateSession(AgentName);

	// Register with agent manager so SendPrompt can route messages
	AgentMgr.RegisterSession(SessionId, AgentName);

	// Gemini CLI sessions are process-coupled in ACP mode (model/context selected at launch).
	// Force a fresh subprocess for each "new chat" so sessions are truly isolated.
	if (AgentName == TEXT("Gemini CLI") && AgentMgr.IsConnectedToAgent(AgentName))
	{
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Forcing fresh Gemini CLI process for new chat session %s"), *SessionId);
		AgentMgr.DisconnectFromAgent(AgentName);
	}

	// Connect to the agent if not already connected
	if (!AgentMgr.IsConnectedToAgent(AgentName))
	{
		AgentMgr.ConnectToAgent(AgentName);
	}

	// If agent is ready or already in a session, create a new external session immediately.
	// Otherwise queue it so NewSession fires automatically when Ready.
	// OpenRouter uses its own native client type, so route it separately.
	if (AgentMgr.IsOpenRouterAgent(AgentName))
	{
		TSharedPtr<FOpenRouterClient> OpenRouterClient = AgentMgr.GetOpenRouterClient(AgentName);
		if (OpenRouterClient.IsValid())
		{
			// OpenRouter NewSession is local state setup; start it immediately.
			OpenRouterClient->SetUnrealSessionId(SessionId);
			OpenRouterClient->NewSession(UACPSettings::GetWorkingDirectory());
		}
	}
	else
	{
		TSharedPtr<FACPClient> Client = AgentMgr.GetClient(AgentName);
		if (Client.IsValid())
		{
			const EACPClientState ClientState = Client->GetState();
			if (ClientState == EACPClientState::Ready || ClientState == EACPClientState::InSession)
			{
				Client->SetUnrealSessionId(SessionId);
				Client->NewSession(UACPSettings::GetWorkingDirectory());
			}
			else
			{
				// Agent still connecting/initializing — queue NewSession for when it becomes Ready
				AgentMgr.QueuePendingNewSession(AgentName, SessionId);
			}
		}
	}

	// Persist agent as last-used
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->LastUsedAgentName = AgentName;
		Settings->SaveConfig();
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("sessionId"), SessionId);
	Result->SetStringField(TEXT("agentName"), AgentName);

	return JsonToString(Result);
}

FString UWebUIBridge::GetSessions()
{
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	FACPSessionManager& SessionMgr = FACPSessionManager::Get();

	TSet<FString> SeenSessionIds;
	TArray<TSharedPtr<FJsonValue>> SessionsArray;

	// Build a set of agent session IDs that correspond to active Unreal sessions.
	// This lets us deduplicate: remote lists use agent IDs, active sessions use Unreal GUIDs.
	TSet<FString> KnownAgentSessionIds;
	TMap<FString, const FACPActiveSession*> AgentIdToActiveSession;
	TArray<FString> ActiveIds = SessionMgr.GetActiveSessionIds();
	for (const FString& Id : ActiveIds)
	{
		const FACPActiveSession* Active = SessionMgr.GetActiveSession(Id);
		if (Active && !Active->Metadata.AgentSessionId.IsEmpty())
		{
			KnownAgentSessionIds.Add(Active->Metadata.AgentSessionId);
			AgentIdToActiveSession.Add(Active->Metadata.AgentSessionId, Active);
		}
	}

	// 1. Remote sessions from each agent's cached ACP list
	TArray<FString> AgentNames = AgentMgr.GetAvailableAgentNames();
	for (const FString& AgentName : AgentNames)
	{
		TArray<FACPRemoteSessionEntry> RemoteSessions = AgentMgr.GetCachedSessionList(AgentName);
		for (const FACPRemoteSessionEntry& Entry : RemoteSessions)
		{
			if (SeenSessionIds.Contains(Entry.SessionId)) continue;

			// If this remote session corresponds to an active Unreal session,
			// update the active session's title from the remote data and skip
			// the remote entry (the active session will be listed in section 2)
			if (KnownAgentSessionIds.Contains(Entry.SessionId))
			{
				if (const FACPActiveSession** ActivePtr = AgentIdToActiveSession.Find(Entry.SessionId))
				{
					if (!Entry.Title.IsEmpty())
					{
						SessionMgr.UpdateSessionTitle((*ActivePtr)->Metadata.SessionId, Entry.Title);
					}
				}
				continue;
			}

			SeenSessionIds.Add(Entry.SessionId);

			TSharedPtr<FJsonObject> SessionObj = MakeShared<FJsonObject>();
			SessionObj->SetStringField(TEXT("sessionId"), Entry.SessionId);
			SessionObj->SetStringField(TEXT("agentName"), AgentName);
			SessionObj->SetStringField(TEXT("title"), Entry.Title);
			if (Entry.UpdatedAt.GetTicks() > 0)
			{
				SessionObj->SetStringField(TEXT("lastModifiedAt"), Entry.UpdatedAt.ToIso8601());
			}

			const FACPActiveSession* Active = SessionMgr.GetActiveSession(Entry.SessionId);
			SessionObj->SetBoolField(TEXT("isConnected"), Active ? Active->bIsConnected : false);
			SessionObj->SetBoolField(TEXT("isActive"), Active != nullptr);

			SessionsArray.Add(MakeShared<FJsonValueObject>(SessionObj));
		}
	}

	// 2. Active in-memory sessions not in remote lists (OpenRouter, newly created)
	for (const FString& Id : ActiveIds)
	{
		if (SeenSessionIds.Contains(Id)) continue;
		SeenSessionIds.Add(Id);

		const FACPActiveSession* Active = SessionMgr.GetActiveSession(Id);
		if (!Active) continue;

		TSharedPtr<FJsonObject> SessionObj = MakeShared<FJsonObject>();
		SessionObj->SetStringField(TEXT("sessionId"), Active->Metadata.SessionId);
		SessionObj->SetStringField(TEXT("agentName"), Active->Metadata.AgentName);
		SessionObj->SetStringField(TEXT("title"), Active->Metadata.Title);
		SessionObj->SetNumberField(TEXT("messageCount"), Active->Metadata.MessageCount);
		if (Active->Metadata.CreatedAt.GetTicks() > 0)
		{
			SessionObj->SetStringField(TEXT("createdAt"), Active->Metadata.CreatedAt.ToIso8601());
		}
		if (Active->Metadata.LastModifiedAt.GetTicks() > 0)
		{
			SessionObj->SetStringField(TEXT("lastModifiedAt"), Active->Metadata.LastModifiedAt.ToIso8601());
		}
		SessionObj->SetBoolField(TEXT("isConnected"), Active->bIsConnected);
		SessionObj->SetBoolField(TEXT("isActive"), true);

		SessionsArray.Add(MakeShared<FJsonValueObject>(SessionObj));
	}

	return JsonArrayToString(SessionsArray);
}

FString UWebUIBridge::ResumeSession(const FString& SessionId)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (SessionId.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Empty session ID"));
		return JsonToString(Result);
	}

	FACPSessionManager& SessionMgr = FACPSessionManager::Get();
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();

	// If already active, just switch to it
	const FACPActiveSession* ActiveSession = SessionMgr.GetActiveSession(SessionId);
	if (ActiveSession)
	{
		SessionMgr.SwitchToSession(SessionId);
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("agentName"), ActiveSession->Metadata.AgentName);
		return JsonToString(Result);
	}

	// Find which agent owns this session by checking cached ACP lists
	FString AgentName;
	for (const FString& Name : AgentMgr.GetAvailableAgentNames())
	{
		TArray<FACPRemoteSessionEntry> Sessions = AgentMgr.GetCachedSessionList(Name);
		for (const FACPRemoteSessionEntry& Entry : Sessions)
		{
			if (Entry.SessionId == SessionId)
			{
				AgentName = Name;
				break;
			}
		}
		if (!AgentName.IsEmpty()) break;
	}

	if (AgentName.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Session not found in any agent's session list"));
		return JsonToString(Result);
	}

	// Create empty active session — messages will arrive via ACP replay
	if (!SessionMgr.ResumeSession(SessionId))
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Failed to create active session"));
		return JsonToString(Result);
	}

	// Set agent name on the session metadata
	if (FACPActiveSession* Session = SessionMgr.GetActiveSession(SessionId))
	{
		Session->Metadata.AgentName = AgentName;
	}

	// Register session with agent manager
	AgentMgr.RegisterSession(SessionId, AgentName);

	// Connect to the agent if not already connected
	if (!AgentMgr.IsConnectedToAgent(AgentName))
	{
		AgentMgr.ConnectToAgent(AgentName);
	}

	// If agent is ready, load the session (messages arrive via replay notifications)
	TSharedPtr<FACPClient> Client = AgentMgr.GetClient(AgentName);
	if (Client.IsValid())
	{
		const EACPClientState ClientState = Client->GetState();
		if (ClientState == EACPClientState::Ready || ClientState == EACPClientState::InSession)
		{
			FString WorkingDirectory = UACPSettings::GetWorkingDirectory();
			Client->SetUnrealSessionId(SessionId);

			const FACPAgentCapabilities& Caps = Client->GetAgentCapabilities();
			// Prefer session/load over session/resume — load replays message history
			// as session/update notifications so the UI can display past messages.
			// session/resume only restores the agent's internal context without replay.
			if (Caps.bSupportsLoadSession)
			{
				Client->LoadSession(SessionId, WorkingDirectory);
			}
			else if (Caps.bSupportsResumeSession)
			{
				Client->ResumeSession(SessionId, WorkingDirectory);
			}
			else
			{
				Client->NewSession(WorkingDirectory);
			}
		}
		else
		{
			// Agent still connecting — queue for when it becomes Ready
			AgentMgr.QueuePendingNewSession(AgentName, SessionId);
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("loading"), true);
	Result->SetStringField(TEXT("agentName"), AgentName);
	return JsonToString(Result);
}

FString UWebUIBridge::GetSessionMessages(const FString& SessionId)
{
	FACPSessionManager& SessionMgr = FACPSessionManager::Get();
	const FACPActiveSession* Session = SessionMgr.GetActiveSession(SessionId);

	if (!Session)
	{
		return TEXT("[]");
	}

	TArray<TSharedPtr<FJsonValue>> MessagesArray;
	for (const FACPChatMessage& Msg : Session->Messages)
	{
		TSharedPtr<FJsonObject> MsgJson = MessageToJson(Msg);
		if (MsgJson.IsValid())
		{
			MessagesArray.Add(MakeShared<FJsonValueObject>(MsgJson));
		}
	}

	return JsonArrayToString(MessagesArray);
}

static FString NormalizeSummaryText(const FString& Input)
{
	FString Out = Input;
	Out.ReplaceInline(TEXT("\r"), TEXT(" "));
	Out.ReplaceInline(TEXT("\n"), TEXT(" "));
	Out.ReplaceInline(TEXT("\t"), TEXT(" "));
	while (Out.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
	Out.TrimStartAndEndInline();
	return Out;
}

static FString NormalizeSummaryProvider(const FString& Provider)
{
	FString Out = Provider;
	Out.TrimStartAndEndInline();
	Out = Out.ToLower();
	if (Out == TEXT("openrouter"))
	{
		return TEXT("openrouter");
	}
	return TEXT("local");
}

static FString NormalizeSummaryDetail(const FString& Detail)
{
	FString Out = Detail;
	Out.TrimStartAndEndInline();
	Out = Out.ToLower();
	if (Out == TEXT("detailed"))
	{
		return TEXT("detailed");
	}
	return TEXT("compact");
}

static FString TruncateSummaryText(const FString& Input, const int32 MaxChars)
{
	if (MaxChars <= 0)
	{
		return FString();
	}
	if (Input.Len() <= MaxChars)
	{
		return Input;
	}
	if (MaxChars <= 3)
	{
		return Input.Left(MaxChars);
	}
	return Input.Left(MaxChars - 3) + TEXT("...");
}

static FString MessageRoleToSummaryLabel(const EACPMessageRole Role)
{
	switch (Role)
	{
	case EACPMessageRole::User: return TEXT("User");
	case EACPMessageRole::Assistant: return TEXT("Assistant");
	case EACPMessageRole::System: return TEXT("System");
	default: return TEXT("Unknown");
	}
}

static FString ContentBlockToSummaryText(const FACPContentBlock& Block)
{
	switch (Block.Type)
	{
	case EACPContentBlockType::Text:
	case EACPContentBlockType::System:
	case EACPContentBlockType::Error:
		return Block.Text;
	case EACPContentBlockType::ToolCall:
		if (!Block.ToolName.IsEmpty())
		{
			return FString::Printf(TEXT("[Tool call: %s]"), *Block.ToolName);
		}
		return TEXT("[Tool call]");
	case EACPContentBlockType::ToolResult:
		if (!Block.ToolResultContent.IsEmpty())
		{
			const TCHAR* ResultStatus = Block.bToolSuccess ? TEXT("success") : TEXT("error");
			return FString::Printf(TEXT("[Tool result (%s): %s]"), ResultStatus, *Block.ToolResultContent);
		}
		return TEXT("[Tool result]");
	default:
		// Skip thought/image by default to keep continuation concise.
		return FString();
	}
}

static FString ContentBlockToTranscriptText(const FACPContentBlock& Block, const bool bDetailed)
{
	switch (Block.Type)
	{
	case EACPContentBlockType::Text:
	case EACPContentBlockType::System:
	case EACPContentBlockType::Error:
		return NormalizeSummaryText(Block.Text);
	case EACPContentBlockType::Thought:
		return bDetailed ? NormalizeSummaryText(FString::Printf(TEXT("[thought] %s"), *Block.Text)) : FString();
	case EACPContentBlockType::ToolCall:
	{
		FString Line = Block.ToolName.IsEmpty()
			? TEXT("[tool_call]")
			: FString::Printf(TEXT("[tool_call %s]"), *Block.ToolName);
		if (!Block.ToolArguments.IsEmpty())
		{
			const int32 MaxArgs = bDetailed ? 1800 : 700;
			Line += FString::Printf(TEXT(" args=%s"), *TruncateSummaryText(NormalizeSummaryText(Block.ToolArguments), MaxArgs));
		}
		return Line;
	}
	case EACPContentBlockType::ToolResult:
	{
		const TCHAR* ResultStatus = Block.bToolSuccess ? TEXT("success") : TEXT("error");
		const int32 MaxResult = bDetailed ? 3200 : 1200;
		FString ResultText = NormalizeSummaryText(Block.ToolResultContent);
		if (ResultText.IsEmpty())
		{
			ResultText = TEXT("(empty)");
		}
		return FString::Printf(TEXT("[tool_result %s] %s"), ResultStatus, *TruncateSummaryText(ResultText, MaxResult));
	}
	default:
		return FString();
	}
}

static bool ValidateContinuationSourceSession(const FString& SourceSessionId, const FACPActiveSession*& OutSession, FString& OutError)
{
	OutSession = nullptr;
	if (SourceSessionId.IsEmpty())
	{
		OutError = TEXT("Source session ID is empty");
		return false;
	}

	FACPSessionManager& SessionMgr = FACPSessionManager::Get();
	const FACPActiveSession* SourceSession = SessionMgr.GetActiveSession(SourceSessionId);
	if (!SourceSession)
	{
		OutError = TEXT("Source session is not loaded. Open the chat first, then try again.");
		return false;
	}
	if (SourceSession->Messages.Num() == 0)
	{
		OutError = TEXT("Source session has no messages to summarize.");
		return false;
	}
	if (SourceSession->Messages.Last().bIsStreaming)
	{
		OutError = TEXT("Source session is still streaming. Wait for completion, then continue.");
		return false;
	}

	OutSession = SourceSession;
	return true;
}

static FString BuildContinuationTranscript(const FACPActiveSession& Session, const bool bDetailed)
{
	const int32 MaxTranscriptChars = bDetailed ? 500000 : 250000;
	FString Transcript;
	Transcript.Reserve(FMath::Min(MaxTranscriptChars, 120000));

	int32 Turn = 1;
	for (const FACPChatMessage& Message : Session.Messages)
	{
		Transcript += FString::Printf(TEXT("Turn %d (%s):\n"), Turn++, *MessageRoleToSummaryLabel(Message.Role));
		for (const FACPContentBlock& Block : Message.ContentBlocks)
		{
			const FString BlockText = ContentBlockToTranscriptText(Block, bDetailed);
			if (!BlockText.IsEmpty())
			{
				Transcript += FString::Printf(TEXT("- %s\n"), *BlockText);
			}
			if (Transcript.Len() >= MaxTranscriptChars)
			{
				Transcript += TEXT("[transcript truncated due to size]\n");
				return Transcript;
			}
		}
		Transcript += TEXT("\n");
		if (Transcript.Len() >= MaxTranscriptChars)
		{
			Transcript += TEXT("[transcript truncated due to size]\n");
			return Transcript;
		}
	}
	return Transcript;
}

static FString BuildOpenRouterHandoffPrompt(const FACPActiveSession& Session, const FString& TargetAgentName, const FString& Detail)
{
	const bool bDetailed = Detail == TEXT("detailed");
	const FString Transcript = BuildContinuationTranscript(Session, bDetailed);

	FString Prompt;
	Prompt += TEXT("Your task is to create a detailed handoff summary of the conversation so far for another coding agent.\n");
	Prompt += FString::Printf(TEXT("Source agent: %s\n"), *Session.Metadata.AgentName);
	Prompt += FString::Printf(TEXT("Target agent: %s\n"), *TargetAgentName);
	Prompt += FString::Printf(TEXT("Requested detail: %s\n\n"), bDetailed ? TEXT("detailed") : TEXT("compact"));
	Prompt += TEXT("Output requirements:\n");
	Prompt += TEXT("1) Return ONLY the handoff summary text (no meta commentary, no XML tags).\n");
	Prompt += TEXT("2) Focus on explicit user requests and the assistant's concrete actions.\n");
	Prompt += TEXT("3) Preserve technical detail: exact file paths, function names/signatures, API names, errors, and concrete edits.\n");
	Prompt += TEXT("4) If something is unknown, write \"Unknown\" rather than guessing.\n");
	Prompt += TEXT("5) Prefer chronological clarity and include the latest unfinished work.\n");
	Prompt += TEXT("6) Keep summary concise but complete; prioritize continuity for implementation work.\n");
	Prompt += TEXT("7) If detail is compact, shorten each section but keep all headings.\n\n");
	Prompt += TEXT("Use exactly these markdown sections in order:\n");
	Prompt += TEXT("## Primary Request and Intent\n");
	Prompt += TEXT("## Key Technical Concepts\n");
	Prompt += TEXT("## Files and Code Sections\n");
	Prompt += TEXT("## Problem Solving\n");
	Prompt += TEXT("## Pending Tasks\n");
	Prompt += TEXT("## Current Work\n");
	Prompt += TEXT("## Optional Next Step\n");
	Prompt += TEXT("## Recent Conversation Evidence\n\n");
	Prompt += TEXT("Section-specific rules:\n");
	Prompt += TEXT("- Key Technical Concepts: bullet list.\n");
	Prompt += TEXT("- Files and Code Sections: bullet list with absolute or workspace-relative file paths; include why each file matters and what changed.\n");
	Prompt += TEXT("- Pending Tasks: only items explicitly requested and not completed.\n");
	Prompt += TEXT("- Optional Next Step: one concrete next action directly aligned with the latest user request.\n");
	Prompt += TEXT("- Recent Conversation Evidence: include 1-3 short verbatim quotes from the latest relevant user/assistant turns.\n\n");
	Prompt += TEXT("Conversation transcript:\n");
	Prompt += Transcript;
	return Prompt;
}

static FString ExtractOpenRouterContent(const TSharedPtr<FJsonObject>& MessageObj)
{
	if (!MessageObj.IsValid())
	{
		return FString();
	}

	FString Content;
	if (MessageObj->TryGetStringField(TEXT("content"), Content))
	{
		return Content;
	}

	const TArray<TSharedPtr<FJsonValue>>* ContentArray = nullptr;
	if (MessageObj->TryGetArrayField(TEXT("content"), ContentArray) && ContentArray)
	{
		FString Combined;
		for (const TSharedPtr<FJsonValue>& Entry : *ContentArray)
		{
			if (!Entry.IsValid())
			{
				continue;
			}

			// Some providers emit string chunks directly, others emit typed objects.
			FString Chunk;
			if (Entry->TryGetString(Chunk))
			{
				Combined += Chunk;
				continue;
			}

			const TSharedPtr<FJsonObject> EntryObj = Entry->AsObject();
			if (!EntryObj.IsValid())
			{
				continue;
			}
			if (EntryObj->TryGetStringField(TEXT("text"), Chunk))
			{
				Combined += Chunk;
				continue;
			}
			if (EntryObj->TryGetStringField(TEXT("summary"), Chunk))
			{
				Combined += Chunk;
			}
		}
		return Combined;
	}

	return FString();
}

static FString MessageToSummaryText(const FACPChatMessage& Message, const int32 MaxCharsPerMessage)
{
	FString Combined;
	for (const FACPContentBlock& Block : Message.ContentBlocks)
	{
		const FString BlockText = NormalizeSummaryText(ContentBlockToSummaryText(Block));
		if (BlockText.IsEmpty())
		{
			continue;
		}

		if (!Combined.IsEmpty())
		{
			Combined += TEXT(" ");
		}
		Combined += BlockText;
	}

	Combined = NormalizeSummaryText(Combined);
	return TruncateSummaryText(Combined, MaxCharsPerMessage);
}

FString UWebUIBridge::BuildContinuationDraft(const FString& SourceSessionId, const FString& TargetAgentName, const FString& SummaryMode)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);

	if (TargetAgentName.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Target agent name is empty"));
		return JsonToString(Result);
	}

	const FString NormalizedDetail = NormalizeSummaryDetail(SummaryMode);
	const bool bDetailed = NormalizedDetail == TEXT("detailed");

	const FACPActiveSession* SourceSession = nullptr;
	FString ValidationError;
	if (!ValidateContinuationSourceSession(SourceSessionId, SourceSession, ValidationError))
	{
		Result->SetStringField(TEXT("error"), ValidationError);
		return JsonToString(Result);
	}

	const int32 MaxCharsPerMessage = bDetailed ? 520 : 300;
	const int32 HeadLineCount = bDetailed ? 6 : 3;
	const int32 TailLineCount = bDetailed ? 14 : 8;
	const int32 MaxDraftChars = bDetailed ? 12000 : 7000;

	struct FSummaryLine
	{
		FString Role;
		FString Text;
	};

	TArray<FSummaryLine> Lines;
	Lines.Reserve(SourceSession->Messages.Num());

	FString FirstUserMessage;
	FString LastUserMessage;
	FString LastAssistantMessage;

	for (const FACPChatMessage& Message : SourceSession->Messages)
	{
		const FString SummaryText = MessageToSummaryText(Message, MaxCharsPerMessage);
		if (SummaryText.IsEmpty())
		{
			continue;
		}

		FSummaryLine Line;
		Line.Role = MessageRoleToSummaryLabel(Message.Role);
		Line.Text = SummaryText;
		Lines.Add(Line);

		if (Message.Role == EACPMessageRole::User)
		{
			if (FirstUserMessage.IsEmpty())
			{
				FirstUserMessage = SummaryText;
			}
			LastUserMessage = SummaryText;
		}
		else if (Message.Role == EACPMessageRole::Assistant)
		{
			LastAssistantMessage = SummaryText;
		}
	}

	if (Lines.Num() == 0)
	{
		Result->SetStringField(TEXT("error"), TEXT("No text content found in source session messages."));
		return JsonToString(Result);
	}

	TSet<int32> IncludedIndices;
	for (int32 i = 0; i < FMath::Min(HeadLineCount, Lines.Num()); ++i)
	{
		IncludedIndices.Add(i);
	}
	for (int32 i = FMath::Max(0, Lines.Num() - TailLineCount); i < Lines.Num(); ++i)
	{
		IncludedIndices.Add(i);
	}

	TArray<int32> OrderedIndices = IncludedIndices.Array();
	OrderedIndices.Sort();

	FString Draft;
	Draft += TEXT("Continue this task in the new chat.\n\n");
	Draft += FString::Printf(TEXT("Previous agent: %s\n"), *SourceSession->Metadata.AgentName);
	Draft += FString::Printf(TEXT("Target agent: %s\n"), *TargetAgentName);
	Draft += FString::Printf(TEXT("Summary style: %s\n\n"), bDetailed ? TEXT("Detailed") : TEXT("Compact"));
	Draft += FString::Printf(TEXT("Turns analyzed: %d\n\n"), Lines.Num());

	if (!FirstUserMessage.IsEmpty())
	{
		Draft += FString::Printf(TEXT("Primary objective:\n%s\n\n"), *FirstUserMessage);
	}
	if (!LastUserMessage.IsEmpty())
	{
		Draft += FString::Printf(TEXT("Latest user request:\n%s\n\n"), *LastUserMessage);
	}
	if (!LastAssistantMessage.IsEmpty())
	{
		Draft += FString::Printf(TEXT("Latest assistant output:\n%s\n\n"), *LastAssistantMessage);
	}

	Draft += TEXT("Conversation digest (selected turns):\n");
	int32 DigestIndex = 1;
	for (const int32 LineIndex : OrderedIndices)
	{
		const FSummaryLine& Line = Lines[LineIndex];
		Draft += FString::Printf(TEXT("%d. %s: %s\n"), DigestIndex++, *Line.Role, *Line.Text);
		if (Draft.Len() > MaxDraftChars)
		{
			Draft += TEXT("...\n");
			break;
		}
	}
	if (OrderedIndices.Num() < Lines.Num())
	{
		Draft += FString::Printf(TEXT("... (%d condensed turns omitted)\n"), Lines.Num() - OrderedIndices.Num());
	}

	Draft += TEXT("\nIf anything important is missing, ask me one clarifying question before continuing.");
	Draft = TruncateSummaryText(Draft, MaxDraftChars);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("sourceSessionId"), SourceSessionId);
	Result->SetStringField(TEXT("targetAgentName"), TargetAgentName);
	Result->SetStringField(TEXT("summaryMode"), NormalizedDetail);
	Result->SetStringField(TEXT("draftPrompt"), Draft);
	Result->SetStringField(TEXT("providerUsed"), TEXT("local"));
	return JsonToString(Result);
}

FString UWebUIBridge::RequestContinuationDraft(const FString& SourceSessionId, const FString& TargetAgentName, const FString& SummaryMode)
{
	TSharedRef<FJsonObject> StartResult = MakeShared<FJsonObject>();
	StartResult->SetBoolField(TEXT("success"), false);
	StartResult->SetBoolField(TEXT("pending"), false);

	if (TargetAgentName.IsEmpty())
	{
		StartResult->SetStringField(TEXT("error"), TEXT("Target agent name is empty"));
		return JsonToString(StartResult);
	}

	UACPSettings* Settings = UACPSettings::Get();
	const FString RequestedProvider = NormalizeSummaryProvider(Settings ? Settings->ContinuationSummaryProvider : TEXT("openrouter"));
	FString EffectiveDetail = NormalizeSummaryDetail(SummaryMode);
	if (SummaryMode.IsEmpty() && Settings)
	{
		EffectiveDetail = NormalizeSummaryDetail(Settings->ContinuationSummaryDefaultDetail);
	}

	const int32 RequestId = NextContinuationDraftRequestId++;
	StartResult->SetBoolField(TEXT("success"), true);
	StartResult->SetBoolField(TEXT("pending"), true);
	StartResult->SetNumberField(TEXT("requestId"), RequestId);
	StartResult->SetStringField(TEXT("providerRequested"), RequestedProvider);
	StartResult->SetStringField(TEXT("summaryMode"), EffectiveDetail);

	const FString CompletionModel = [Settings]() -> FString
	{
		if (Settings)
		{
			FString Model = Settings->ContinuationSummaryModel;
			Model.TrimStartAndEndInline();
			if (!Model.IsEmpty())
			{
				return Model;
			}

			Model = Settings->OpenRouterDefaultModel;
			Model.TrimStartAndEndInline();
			if (!Model.IsEmpty())
			{
				return Model;
			}
		}
		return FString(TEXT("x-ai/grok-4.1-fast"));
	}();

	const bool bCanUseOpenRouter =
		RequestedProvider == TEXT("openrouter") &&
		Settings &&
		Settings->HasOpenRouterAuth();

	// OpenRouter summarization path (primary)
	if (bCanUseOpenRouter)
	{
		const FACPActiveSession* SourceSession = nullptr;
		FString ValidationError;
		if (!ValidateContinuationSourceSession(SourceSessionId, SourceSession, ValidationError))
		{
			StartResult->SetBoolField(TEXT("success"), false);
			StartResult->SetBoolField(TEXT("pending"), false);
			StartResult->SetStringField(TEXT("error"), ValidationError);
			return JsonToString(StartResult);
		}

		const FString Prompt = BuildOpenRouterHandoffPrompt(*SourceSession, TargetAgentName, EffectiveDetail);
		const bool bDetailed = EffectiveDetail == TEXT("detailed");

		TSharedRef<FJsonObject> RequestObj = MakeShared<FJsonObject>();
		RequestObj->SetStringField(TEXT("model"), CompletionModel);
		RequestObj->SetBoolField(TEXT("stream"), false);
		RequestObj->SetNumberField(TEXT("temperature"), 0.2);
		RequestObj->SetNumberField(TEXT("max_tokens"), bDetailed ? 8192 : 4096);

		TArray<TSharedPtr<FJsonValue>> MessagesArray;
		{
			TSharedRef<FJsonObject> SystemMsg = MakeShared<FJsonObject>();
			SystemMsg->SetStringField(TEXT("role"), TEXT("system"));
			SystemMsg->SetStringField(TEXT("content"), TEXT("You generate high-accuracy coding-session handoff summaries with strict markdown structure. Preserve technical details, avoid hallucinations, and prioritize explicit user intent plus latest unfinished work."));
			MessagesArray.Add(MakeShared<FJsonValueObject>(SystemMsg));
		}
		{
			TSharedRef<FJsonObject> UserMsg = MakeShared<FJsonObject>();
			UserMsg->SetStringField(TEXT("role"), TEXT("user"));
			UserMsg->SetStringField(TEXT("content"), Prompt);
			MessagesArray.Add(MakeShared<FJsonValueObject>(UserMsg));
		}
		RequestObj->SetArrayField(TEXT("messages"), MessagesArray);

		FString RequestBody;
		{
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBody);
			FJsonSerializer::Serialize(RequestObj, Writer);
		}

		const FString ApiKey = Settings->GetOpenRouterAuthToken();
		TWeakObjectPtr<UWebUIBridge> WeakThis(this);
		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(Settings->GetOpenRouterChatCompletionsUrl());
		HttpRequest->SetVerb(TEXT("POST"));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
		HttpRequest->SetHeader(TEXT("HTTP-Referer"), TEXT("https://github.com/betidestudio/AgentIntegrationKit"));
		HttpRequest->SetHeader(TEXT("X-Title"), TEXT("Agent Integration Kit"));
		HttpRequest->SetContentAsString(RequestBody);
		HttpRequest->OnProcessRequestComplete().BindLambda(
			[WeakThis, RequestId, SourceSessionId, TargetAgentName, EffectiveDetail](FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
			{
				TSharedRef<FJsonObject> FinalResult = MakeShared<FJsonObject>();
				FinalResult->SetBoolField(TEXT("success"), false);
				FinalResult->SetStringField(TEXT("sourceSessionId"), SourceSessionId);
				FinalResult->SetStringField(TEXT("targetAgentName"), TargetAgentName);
				FinalResult->SetStringField(TEXT("summaryMode"), EffectiveDetail);
				FinalResult->SetStringField(TEXT("providerUsed"), TEXT("openrouter"));

				if (!bSuccess || !Response.IsValid())
				{
					FinalResult->SetStringField(TEXT("error"), TEXT("Failed to reach OpenRouter for summary generation."));
				}
				else if (Response->GetResponseCode() != 200)
				{
					FinalResult->SetStringField(
						TEXT("error"),
						FString::Printf(TEXT("OpenRouter summary request failed (%d): %s"), Response->GetResponseCode(), *TruncateSummaryText(Response->GetContentAsString(), 1200))
					);
				}
				else
				{
					TSharedPtr<FJsonObject> RootObj;
					const FString Body = Response->GetContentAsString();
					TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
					if (!FJsonSerializer::Deserialize(Reader, RootObj) || !RootObj.IsValid())
					{
						FinalResult->SetStringField(TEXT("error"), TEXT("Failed to parse OpenRouter summary response."));
					}
					else
					{
						const TArray<TSharedPtr<FJsonValue>>* Choices = nullptr;
						if (!RootObj->TryGetArrayField(TEXT("choices"), Choices) || !Choices || Choices->Num() == 0)
						{
							FinalResult->SetStringField(TEXT("error"), TEXT("OpenRouter summary response contained no choices."));
						}
						else
						{
							const TSharedPtr<FJsonObject> ChoiceObj = (*Choices)[0]->AsObject();
							if (!ChoiceObj.IsValid() || !ChoiceObj->HasField(TEXT("message")))
							{
								FinalResult->SetStringField(TEXT("error"), TEXT("OpenRouter summary response missing message content."));
							}
							else
							{
								const TSharedPtr<FJsonObject> MessageObj = ChoiceObj->GetObjectField(TEXT("message"));
								FString DraftPrompt = NormalizeSummaryText(ExtractOpenRouterContent(MessageObj));
								if (DraftPrompt.IsEmpty())
								{
									FinalResult->SetStringField(TEXT("error"), TEXT("OpenRouter returned an empty summary."));
								}
								else
								{
									FinalResult->SetBoolField(TEXT("success"), true);
									FinalResult->SetStringField(TEXT("draftPrompt"), DraftPrompt);
								}
							}
						}
					}
				}

				const FString FinalJson = JsonToString(FinalResult);
				if (UWebUIBridge* Self = WeakThis.Get())
				{
					if (Self->OnContinuationDraftReadyCallback.IsValid())
					{
						Self->OnContinuationDraftReadyCallback(RequestId, FinalJson);
					}
				}
			}
		);

		if (!HttpRequest->ProcessRequest())
		{
			StartResult->SetBoolField(TEXT("success"), false);
			StartResult->SetBoolField(TEXT("pending"), false);
			StartResult->SetStringField(TEXT("error"), TEXT("Failed to start OpenRouter summary request."));
			return JsonToString(StartResult);
		}

		StartResult->SetStringField(TEXT("providerUsed"), TEXT("openrouter"));
		StartResult->SetStringField(TEXT("modelId"), CompletionModel);
		return JsonToString(StartResult);
	}

	// Local fallback path
	FString LocalResultJson = BuildContinuationDraft(SourceSessionId, TargetAgentName, EffectiveDetail);
	StartResult->SetStringField(TEXT("providerUsed"), TEXT("local"));
	if (RequestedProvider == TEXT("openrouter"))
	{
		StartResult->SetStringField(TEXT("fallbackReason"), TEXT("OpenRouter not available or API key missing; using local summary."));
	}

	TWeakObjectPtr<UWebUIBridge> WeakThis(this);
	AsyncTask(ENamedThreads::GameThread, [WeakThis, RequestId, LocalResultJson]()
	{
		if (UWebUIBridge* Self = WeakThis.Get())
		{
			if (Self->OnContinuationDraftReadyCallback.IsValid())
			{
				Self->OnContinuationDraftReadyCallback(RequestId, LocalResultJson);
			}
		}
	});

	return JsonToString(StartResult);
}

FString UWebUIBridge::GetContinuationSummarySettings()
{
	const UACPSettings* Settings = UACPSettings::Get();

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("provider"), NormalizeSummaryProvider(Settings ? Settings->ContinuationSummaryProvider : TEXT("openrouter")));

	FString ModelId = Settings ? Settings->ContinuationSummaryModel : FString();
	ModelId.TrimStartAndEndInline();
	if (ModelId.IsEmpty() && Settings)
	{
		ModelId = Settings->OpenRouterDefaultModel;
		ModelId.TrimStartAndEndInline();
	}
	if (ModelId.IsEmpty())
	{
		ModelId = TEXT("x-ai/grok-4.1-fast");
	}
	Result->SetStringField(TEXT("modelId"), ModelId);
	Result->SetStringField(TEXT("defaultDetail"), NormalizeSummaryDetail(Settings ? Settings->ContinuationSummaryDefaultDetail : TEXT("compact")));
	Result->SetBoolField(TEXT("hasOpenRouterKey"), Settings && Settings->HasOpenRouterAuth());
	return JsonToString(Result);
}

void UWebUIBridge::SetContinuationSummaryProvider(const FString& Provider)
{
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->ContinuationSummaryProvider = NormalizeSummaryProvider(Provider);
		Settings->SaveConfig();
	}
}

void UWebUIBridge::SetContinuationSummaryModel(const FString& ModelId)
{
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		FString Normalized = ModelId;
		Normalized.TrimStartAndEndInline();
		if (!Normalized.IsEmpty())
		{
			Settings->ContinuationSummaryModel = Normalized;
			Settings->SaveConfig();
		}
	}
}

void UWebUIBridge::SetContinuationSummaryDefaultDetail(const FString& Detail)
{
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->ContinuationSummaryDefaultDetail = NormalizeSummaryDetail(Detail);
		Settings->SaveConfig();
	}
}

// ── Messaging ────────────────────────────────────────────────────────

namespace
{
	// Keep automatic @ mention context bounded so adapter/model limits are not exceeded.
	constexpr int32 MaxMentionPathsPerPrompt = 4;
	constexpr int32 MentionContextLineLimit = 40;
	constexpr int32 MaxMentionItemChars = 6000;
	constexpr int32 MaxMentionContextChars = 24000;

	static FString TruncateForPromptBudget(const FString& Input, int32 MaxChars, bool& bOutTruncated)
	{
		bOutTruncated = false;
		if (MaxChars <= 0 || Input.Len() <= MaxChars)
		{
			return Input;
		}

		const FString Suffix = TEXT("\n...[truncated for prompt size]\n");
		const int32 KeepChars = FMath::Max(0, MaxChars - Suffix.Len());
		bOutTruncated = true;
		return Input.Left(KeepChars) + Suffix;
	}
}

// Helper: parse @/Game/... and @Source/... paths from message text (mirrors SAgentChatWindow::ParseAtMentionPaths)
static TArray<FString> ParseAtMentionPaths(const FString& MessageText)
{
	TArray<FString> Paths;
	int32 SearchStart = 0;
	while (SearchStart < MessageText.Len())
	{
		int32 AtIndex = MessageText.Find(TEXT("@"), ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchStart);
		if (AtIndex == INDEX_NONE) break;

		int32 PathStart = AtIndex + 1;
		if (PathStart >= MessageText.Len()) break;

		int32 PathEnd = PathStart;
		while (PathEnd < MessageText.Len() && !FChar::IsWhitespace(MessageText[PathEnd]))
		{
			++PathEnd;
		}

		FString Path = MessageText.Mid(PathStart, PathEnd - PathStart);
		if (Path.StartsWith(TEXT("/")) || Path.StartsWith(TEXT("Source/")))
		{
			Paths.AddUnique(Path);
		}
		SearchStart = PathEnd;
	}
	return Paths;
}

// Helper: resolve paths to context text using ReadFileTool (mirrors SAgentChatWindow::BuildContextForPaths)
static FString BuildContextForPaths(const TArray<FString>& Paths)
{
	if (Paths.Num() == 0) return FString();

	FString ContextText = TEXT("## Referenced Context\n\n");
	FReadFileTool ReadTool;
	const int32 PathsToProcess = FMath::Min(Paths.Num(), MaxMentionPathsPerPrompt);
	int32 TruncatedItemCount = 0;
	int32 OmittedItemCount = 0;

	for (int32 PathIndex = 0; PathIndex < PathsToProcess; ++PathIndex)
	{
		const FString& Path = Paths[PathIndex];
		TSharedPtr<FJsonObject> Args = MakeShared<FJsonObject>();
		Args->SetStringField(TEXT("name"), Path);
		Args->SetNumberField(TEXT("limit"), MentionContextLineLimit);

		TArray<TSharedPtr<FJsonValue>> IncludeArray;
		IncludeArray.Add(MakeShared<FJsonValueString>(TEXT("summary")));
		IncludeArray.Add(MakeShared<FJsonValueString>(TEXT("variables")));
		IncludeArray.Add(MakeShared<FJsonValueString>(TEXT("components")));
		Args->SetArrayField(TEXT("include"), IncludeArray);

		FToolResult Result = ReadTool.Execute(Args);
		FString DisplayName = FPaths::GetCleanFilename(Path);
		FString EntryBody = Result.bSuccess
			? Result.Output
			: FString::Printf(TEXT("Error: %s"), *Result.Output);

		bool bEntryTruncated = false;
		EntryBody = TruncateForPromptBudget(EntryBody, MaxMentionItemChars, bEntryTruncated);
		if (bEntryTruncated)
		{
			++TruncatedItemCount;
		}

		const FString EntryText = FString::Printf(TEXT("### %s\n```\n%s\n```\n\n"), *DisplayName, *EntryBody);
		const int32 RemainingBudget = MaxMentionContextChars - ContextText.Len();
		if (EntryText.Len() > RemainingBudget)
		{
			OmittedItemCount += (PathsToProcess - PathIndex);
			break;
		}

		ContextText += EntryText;
	}

	if (Paths.Num() > PathsToProcess)
	{
		OmittedItemCount += (Paths.Num() - PathsToProcess);
	}

	if (TruncatedItemCount > 0 || OmittedItemCount > 0)
	{
		const FString SizeNote = FString::Printf(
			TEXT("> Note: mention context was size-limited (truncated=%d, omitted=%d).\n\n"),
			TruncatedItemCount,
			OmittedItemCount);
		const int32 RemainingBudget = MaxMentionContextChars - ContextText.Len();
		if (RemainingBudget > 0)
		{
			bool bIgnored = false;
			ContextText += TruncateForPromptBudget(SizeNote, RemainingBudget, bIgnored);
		}
	}

	return ContextText;
}

void UWebUIBridge::SendPrompt(const FString& SessionId, const FString& Text)
{
	const FString TrimmedSessionId = SessionId.TrimStartAndEnd();
	FString TrimmedText = Text;
	TrimmedText.TrimStartAndEndInline();

	if (TrimmedSessionId.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("WebUIBridge: SendPrompt ignored - empty session ID"));
		return;
	}

	if (TrimmedText.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("WebUIBridge: SendPrompt ignored - empty text for session %s"), *TrimmedSessionId);
		return;
	}

	FACPSessionManager& SessionMgr = FACPSessionManager::Get();
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	const FACPActiveSession* Session = SessionMgr.GetActiveSession(TrimmedSessionId);
	if (!Session)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("WebUIBridge: SendPrompt ignored - session not found: %s"), *TrimmedSessionId);
		return;
	}

	// Parse @ mentions and resolve context (same as Slate UI)
	TArray<FString> MentionedPaths = ParseAtMentionPaths(TrimmedText);
	FString ContextPrefix = BuildContextForPaths(MentionedPaths);

	// Build the full prompt with context prepended
	FString FullPrompt = ContextPrefix.IsEmpty() ? TrimmedText : (ContextPrefix + TrimmedText);

	// Add user message to session (show original text, not with context)
	SessionMgr.AddUserMessage(TrimmedSessionId, TrimmedText);

	// Get the agent for this session
	FString AgentName = AgentMgr.GetSessionAgent(TrimmedSessionId);
	if (AgentName.IsEmpty())
	{
		AgentName = Session->Metadata.AgentName;
	}

	if (!AgentName.IsEmpty())
	{
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Sending prompt for session %s via agent %s (user_len=%d, mention_context_len=%d, full_len=%d, mentions=%d)"),
			*TrimmedSessionId, *AgentName, TrimmedText.Len(), ContextPrefix.Len(), FullPrompt.Len(), MentionedPaths.Num());
		AgentMgr.SendPromptToSession(TrimmedSessionId, AgentName, FullPrompt);
	}
	else
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("WebUIBridge: SendPrompt ignored - no agent mapped for session %s"), *TrimmedSessionId);
	}
}

void UWebUIBridge::CancelPrompt(const FString& SessionId)
{
	if (!SessionId.IsEmpty())
	{
		FACPAgentManager::Get().CancelSessionPrompt(SessionId);
	}
}

// ── Model & Reasoning ───────────────────────────────────────────────

FString UWebUIBridge::GetModels(const FString& AgentName)
{
	if (AgentName.IsEmpty()) return TEXT("[]");

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	FACPSessionModelState ModelState = AgentMgr.GetAgentModelState(AgentName);

	// Check if ACP client has reasoning effort options (from config_option_update)
	bool bACPHasReasoning = false;
	TSharedPtr<FACPClient> ACPClient = AgentMgr.GetClient(AgentName);
	if (ACPClient.IsValid() && ACPClient->SupportsReasoningEffortControl())
	{
		bACPHasReasoning = true;
	}

	TArray<TSharedPtr<FJsonValue>> ModelsArray;

	for (const FACPModelInfo& Model : ModelState.AvailableModels)
	{
		TSharedPtr<FJsonObject> ModelObj = MakeShared<FJsonObject>();
		ModelObj->SetStringField(TEXT("id"), Model.ModelId);
		ModelObj->SetStringField(TEXT("name"), Model.Name);
		ModelObj->SetStringField(TEXT("description"), Model.Description);
		// Model supports reasoning if it has SupportedParameters (OpenRouter) OR the ACP client has reasoning options
		ModelObj->SetBoolField(TEXT("supportsReasoning"), Model.SupportsReasoning() || bACPHasReasoning);
		ModelsArray.Add(MakeShared<FJsonValueObject>(ModelObj));
	}

	// Include current model ID
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("models"), ModelsArray);
	Result->SetStringField(TEXT("currentModelId"), ModelState.CurrentModelId);

	return JsonToString(Result);
}

FString UWebUIBridge::GetAllModels(const FString& AgentName)
{
	if (AgentName.IsEmpty()) return TEXT("[]");

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	FACPSessionModelState ModelState = AgentMgr.GetAgentModelState(AgentName);
	const TArray<FACPModelInfo> FullModels = AgentMgr.GetAgentFullModelList(AgentName);

	// Check if ACP client has reasoning effort options (from config_option_update)
	bool bACPHasReasoning = false;
	TSharedPtr<FACPClient> ACPClient = AgentMgr.GetClient(AgentName);
	if (ACPClient.IsValid() && ACPClient->SupportsReasoningEffortControl())
	{
		bACPHasReasoning = true;
	}

	TArray<TSharedPtr<FJsonValue>> ModelsArray;
	for (const FACPModelInfo& Model : FullModels)
	{
		TSharedPtr<FJsonObject> ModelObj = MakeShared<FJsonObject>();
		ModelObj->SetStringField(TEXT("id"), Model.ModelId);
		ModelObj->SetStringField(TEXT("name"), Model.Name);
		ModelObj->SetStringField(TEXT("description"), Model.Description);
		ModelObj->SetBoolField(TEXT("supportsReasoning"), Model.SupportsReasoning() || bACPHasReasoning);
		ModelsArray.Add(MakeShared<FJsonValueObject>(ModelObj));
	}

	// Fallback to curated list if full list is unavailable.
	if (ModelsArray.Num() == 0)
	{
		for (const FACPModelInfo& Model : ModelState.AvailableModels)
		{
			TSharedPtr<FJsonObject> ModelObj = MakeShared<FJsonObject>();
			ModelObj->SetStringField(TEXT("id"), Model.ModelId);
			ModelObj->SetStringField(TEXT("name"), Model.Name);
			ModelObj->SetStringField(TEXT("description"), Model.Description);
			ModelObj->SetBoolField(TEXT("supportsReasoning"), Model.SupportsReasoning() || bACPHasReasoning);
			ModelsArray.Add(MakeShared<FJsonValueObject>(ModelObj));
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("models"), ModelsArray);
	Result->SetStringField(TEXT("currentModelId"), ModelState.CurrentModelId);
	return JsonToString(Result);
}

void UWebUIBridge::SetModel(const FString& AgentName, const FString& ModelId)
{
	if (AgentName.IsEmpty() || ModelId.IsEmpty()) return;

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	AgentMgr.SetAgentModel(AgentName, ModelId);

	// Persist the selection
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->SaveModelForAgent(AgentName, ModelId);
	}
}

FString UWebUIBridge::GetReasoningLevel(const FString& AgentName)
{
	if (AgentName.IsEmpty())
	{
		return TEXT("high");
	}

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	UACPSettings* Settings = UACPSettings::Get();

	// Prefer persisted value as fallback
	const FString SavedLevel = Settings ? Settings->GetSavedReasoningForAgent(AgentName) : FString();

	// OpenRouter uses local reasoning state.
	if (AgentName == TEXT("OpenRouter"))
	{
		TSharedPtr<FOpenRouterClient> ORClient = AgentMgr.GetOpenRouterClient(TEXT("OpenRouter"));
		if (ORClient.IsValid())
		{
			if (!SavedLevel.IsEmpty())
			{
				const bool bEnableSavedReasoning = SavedLevel != TEXT("none");
				ORClient->SetReasoningEnabled(bEnableSavedReasoning);
				if (bEnableSavedReasoning)
				{
					ORClient->SetReasoningEffort(SavedLevel);
				}
			}

			if (!ORClient->IsReasoningEnabled())
			{
				return TEXT("none");
			}

			const FString Effort = ORClient->GetReasoningEffort();
			if (!Effort.IsEmpty())
			{
				return Effort;
			}
		}

		return SavedLevel.IsEmpty() ? TEXT("high") : SavedLevel;
	}

	// Check the specific ACP client for this agent.
	TSharedPtr<FACPClient> Client = AgentMgr.GetClient(AgentName);
	if (Client.IsValid() && Client->SupportsReasoningEffortControl())
	{
		const FString& Effort = Client->GetCurrentReasoningEffort();
		if (!Effort.IsEmpty())
		{
			// Map ACP thinking values to UI reasoning levels
			if (Effort == TEXT("off")) return TEXT("none");
			return Effort;
		}
	}

	return SavedLevel.IsEmpty() ? TEXT("high") : SavedLevel;
}

void UWebUIBridge::SetReasoningLevel(const FString& AgentName, const FString& Level)
{
	if (AgentName.IsEmpty() || Level.IsEmpty()) return;

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->SaveReasoningForAgent(AgentName, Level);
	}

	// OpenRouter reasoning is configured directly on its client.
	if (AgentName == TEXT("OpenRouter"))
	{
		TSharedPtr<FOpenRouterClient> ORClient = AgentMgr.GetOpenRouterClient(TEXT("OpenRouter"));
		if (!ORClient.IsValid()) return;

		const bool bEnabled = Level != TEXT("none");
		ORClient->SetReasoningEnabled(bEnabled);
		if (bEnabled)
		{
			ORClient->SetReasoningEffort(Level);
		}
		return;
	}

	// ACP client — map UI level to thinking config option value
	FString ThinkingValue = Level == TEXT("none") ? TEXT("off") : Level;
	TSharedPtr<FACPClient> Client = AgentMgr.GetClient(AgentName);
	if (Client.IsValid() && Client->SupportsReasoningEffortControl())
	{
		Client->SetReasoningEffort(ThinkingValue);
	}
}

// ── Mode Selection ──────────────────────────────────────────────────

FString UWebUIBridge::GetModes(const FString& AgentName)
{
	if (AgentName.IsEmpty()) return TEXT("{\"modes\":[],\"currentModeId\":\"\"}");

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	FACPSessionModeState ModeState = AgentMgr.GetAgentModeState(AgentName);

	TArray<TSharedPtr<FJsonValue>> ModesArray;
	for (const FACPSessionMode& Mode : ModeState.AvailableModes)
	{
		TSharedPtr<FJsonObject> ModeObj = MakeShared<FJsonObject>();
		ModeObj->SetStringField(TEXT("id"), Mode.ModeId);
		ModeObj->SetStringField(TEXT("name"), Mode.Name);
		ModeObj->SetStringField(TEXT("description"), Mode.Description);
		ModesArray.Add(MakeShared<FJsonValueObject>(ModeObj));
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("modes"), ModesArray);
	Result->SetStringField(TEXT("currentModeId"), ModeState.CurrentModeId);

	return JsonToString(Result);
}

void UWebUIBridge::SetMode(const FString& AgentName, const FString& ModeId)
{
	if (AgentName.IsEmpty() || ModeId.IsEmpty()) return;
	FACPAgentManager::Get().SetAgentMode(AgentName, ModeId);

	// Persist the selection
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->SaveModeForAgent(AgentName, ModeId);
	}
}

// ── Tool Profiles & Settings ────────────────────────────────────────

/** Convert raw_tool_name to "Raw Tool Name" */
static FString FormatToolDisplayName(const FString& RawName)
{
	FString Result = RawName;
	Result.ReplaceInline(TEXT("_"), TEXT(" "));
	bool bCapitalizeNext = true;
	for (int32 i = 0; i < Result.Len(); ++i)
	{
		if (bCapitalizeNext && FChar::IsAlpha(Result[i]))
		{
			Result[i] = FChar::ToUpper(Result[i]);
			bCapitalizeNext = false;
		}
		else if (Result[i] == ' ')
		{
			bCapitalizeNext = true;
		}
	}
	return Result;
}

/** Hardcoded tool metadata — mirrors SSettingsPanel::BuildToolList */
struct FWebToolMeta
{
	const TCHAR* Description;
	const TCHAR* ExtendedDescription;
	const TCHAR* Category;
};

static const TMap<FString, FWebToolMeta>& GetToolMetadata()
{
	static TMap<FString, FWebToolMeta> Meta;
	if (Meta.Num() == 0)
	{
		Meta.Add(TEXT("execute_python"), { TEXT("Python scripting for UE APIs"), TEXT("Execute Python scripts with access to 1000+ Unreal Engine APIs. Can create assets, modify properties, automate tasks, and interact with the editor."), TEXT("Scripting") });
		Meta.Add(TEXT("read_asset"), { TEXT("Read and inspect assets"), TEXT("Read any asset type with deep introspection. Dedicated readers for Blueprints, Materials, Animations, Niagara, Sequences, and 30+ more types."), TEXT("Asset Reading") });
		Meta.Add(TEXT("edit_blueprint"), { TEXT("Create & Edit Blueprints"), TEXT("Create/edit Blueprints and route Enhanced Input assets via asset_domain='enhanced_input'."), TEXT("Blueprint") });
		Meta.Add(TEXT("edit_graph"), { TEXT("Edit Graph Nodes + Search"), TEXT("Create/connect nodes in Blueprint, Material, PCG, MetaSound, and BehaviorTree graphs. Includes integrated node search via operation=find_nodes/query."), TEXT("Graphs") });
		Meta.Add(TEXT("edit_ai_tree"), { TEXT("Edit AI Trees"), TEXT("Unified AI tree editor for BehaviorTree/Blackboard and StateTree assets."), TEXT("AI") });
		Meta.Add(TEXT("edit_niagara"), { TEXT("Edit Niagara VFX"), TEXT("Modify Niagara particle systems including emitters, modules, and parameters."), TEXT("VFX") });
		Meta.Add(TEXT("edit_sequencer"), { TEXT("Edit Level Sequences"), TEXT("Edit Level Sequences for cinematics. Bind actors, add tracks, and set keyframes."), TEXT("Cinematics") });
		Meta.Add(TEXT("edit_rigging"), { TEXT("Edit Rigging"), TEXT("Unified rigging editor for motion stack (IK Rig/Retargeter/Pose Search) and Control Rig."), TEXT("Animation") });
		Meta.Add(TEXT("edit_animation_asset"), { TEXT("Edit Animation Assets"), TEXT("Unified editor for Animation Montage, AnimSequence, and BlendSpace/AimOffset assets."), TEXT("Animation") });
		Meta.Add(TEXT("edit_character_asset"), { TEXT("Edit Character Assets"), TEXT("Unified editor for Skeleton and PhysicsAsset (ragdoll/collision) workflows."), TEXT("Animation") });
		Meta.Add(TEXT("configure_asset"), { TEXT("Configure asset & node properties"), TEXT("Read and set properties on any asset or graph node using UE reflection."), TEXT("Properties") });
		Meta.Add(TEXT("edit_data_structure"), { TEXT("Edit Structs, Enums, StringTables"), TEXT("Create and modify User Defined Structs, Enums, DataTables, and StringTables."), TEXT("Data") });
		Meta.Add(TEXT("generate_asset"), { TEXT("Generate Assets"), TEXT("Unified generation for images (OpenRouter) and 3D models (Meshy)."), TEXT("Generation") });
		Meta.Add(TEXT("read_logs"), { TEXT("Read editor logs"), TEXT("Read Unreal Engine output logs and Blueprint compilation errors."), TEXT("Debug") });
		Meta.Add(TEXT("screenshot"), { TEXT("Screenshot with camera control"), TEXT("Capture screenshots from viewport or asset editor with full camera control."), TEXT("Visualization") });
		Meta.Add(TEXT("explore"), { TEXT("Explore project content"), TEXT("Browse and list project assets by path, type, or search query."), TEXT("Asset Reading") });
		Meta.Add(TEXT("create_file"), { TEXT("Create files"), TEXT("Create new files in the project directory (scripts, config, text)."), TEXT("Scripting") });
	}
	return Meta;
}

FString UWebUIBridge::GetTools(const FString& ProfileId)
{
	const UACPSettings* Settings = UACPSettings::Get();
	const TMap<FString, FWebToolMeta>& ToolMeta = GetToolMetadata();

	// Determine which profile (if any) we're viewing tools for
	const FAgentProfile* Profile = nullptr;
	if (!ProfileId.IsEmpty() && Settings)
	{
		Profile = Settings->FindProfileById(ProfileId);
	}

	const TMap<FString, FMCPToolDefinition>& MCPTools = FMCPServer::Get().GetRegisteredTools();

	TArray<TSharedPtr<FJsonValue>> ToolsArray;
	for (const auto& Pair : MCPTools)
	{
		FString Description = Pair.Value.Description;
		FString ExtendedDescription;
		FString Category = TEXT("Other");

		if (const FWebToolMeta* Meta = ToolMeta.Find(Pair.Key))
		{
			Description = Meta->Description;
			ExtendedDescription = Meta->ExtendedDescription;
			Category = Meta->Category;
		}

		// Determine enabled state
		bool bEnabled;
		if (Profile)
		{
			bEnabled = Profile->EnabledTools.Num() == 0 || Profile->EnabledTools.Contains(Pair.Key);
		}
		else
		{
			bEnabled = Settings ? Settings->IsToolEnabled(Pair.Key) : true;
		}

		// Check for description override in the profile
		FString DescriptionOverride;
		if (Profile)
		{
			if (const FString* Override = Profile->ToolDescriptionOverrides.Find(Pair.Key))
			{
				DescriptionOverride = *Override;
			}
		}

		TSharedPtr<FJsonObject> ToolObj = MakeShared<FJsonObject>();
		ToolObj->SetStringField(TEXT("name"), Pair.Key);
		ToolObj->SetStringField(TEXT("displayName"), FormatToolDisplayName(Pair.Key));
		ToolObj->SetStringField(TEXT("description"), Description);
		ToolObj->SetStringField(TEXT("extendedDescription"), ExtendedDescription);
		ToolObj->SetStringField(TEXT("category"), Category);
		ToolObj->SetBoolField(TEXT("enabled"), bEnabled);
		ToolObj->SetStringField(TEXT("descriptionOverride"), DescriptionOverride);
		ToolsArray.Add(MakeShared<FJsonValueObject>(ToolObj));
	}

	return JsonArrayToString(ToolsArray);
}

FString UWebUIBridge::GetProfiles()
{
	UACPSettings* Settings = UACPSettings::Get();
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> ProfilesArray;
	if (Settings)
	{
		for (const FAgentProfile& Profile : Settings->Profiles)
		{
			TSharedPtr<FJsonObject> PObj = MakeShared<FJsonObject>();
			PObj->SetStringField(TEXT("profileId"), Profile.ProfileId);
			PObj->SetStringField(TEXT("displayName"), Profile.DisplayName);
			PObj->SetStringField(TEXT("description"), Profile.Description);
			PObj->SetBoolField(TEXT("isBuiltIn"), Profile.bIsBuiltIn);
			PObj->SetBoolField(TEXT("isActive"), Settings->ActiveProfileId == Profile.ProfileId);
			PObj->SetNumberField(TEXT("enabledToolCount"), Profile.EnabledTools.Num());
			ProfilesArray.Add(MakeShared<FJsonValueObject>(PObj));
		}
		Result->SetStringField(TEXT("activeProfileId"), Settings->ActiveProfileId);
	}
	Result->SetArrayField(TEXT("profiles"), ProfilesArray);

	return JsonToString(Result);
}

void UWebUIBridge::SetActiveProfile(const FString& ProfileId)
{
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->SetActiveProfile(ProfileId);
	}
}

void UWebUIBridge::SetToolEnabled(const FString& ToolName, bool bEnabled)
{
	if (ToolName.IsEmpty()) return;
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->SetToolEnabled(ToolName, bEnabled);
	}
}

void UWebUIBridge::SetProfileToolEnabled(const FString& ProfileId, const FString& ToolName, bool bEnabled)
{
	if (ProfileId.IsEmpty() || ToolName.IsEmpty()) return;

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings) return;

	FAgentProfile* Profile = Settings->FindProfileByIdMutable(ProfileId);
	if (!Profile) return;

	if (Profile->EnabledTools.Num() == 0)
	{
		// Transition from "all enabled" to explicit whitelist — populate with all tools first
		const TMap<FString, FMCPToolDefinition>& MCPTools = FMCPServer::Get().GetRegisteredTools();
		for (const auto& Pair : MCPTools)
		{
			Profile->EnabledTools.Add(Pair.Key);
		}
	}

	if (bEnabled)
	{
		Profile->EnabledTools.Add(ToolName);
	}
	else
	{
		Profile->EnabledTools.Remove(ToolName);
	}

	Settings->SaveConfig();
}

FString UWebUIBridge::CreateProfile(const FString& DisplayName, const FString& Description)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (DisplayName.IsEmpty())
	{
		Result->SetStringField(TEXT("profileId"), TEXT(""));
		return JsonToString(Result);
	}

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings)
	{
		Result->SetStringField(TEXT("profileId"), TEXT(""));
		return JsonToString(Result);
	}

	FAgentProfile NewProfile;
	NewProfile.ProfileId = FGuid::NewGuid().ToString();
	NewProfile.DisplayName = DisplayName;
	NewProfile.Description = Description;
	NewProfile.bIsBuiltIn = false;

	Settings->AddCustomProfile(NewProfile);

	Result->SetStringField(TEXT("profileId"), NewProfile.ProfileId);
	return JsonToString(Result);
}

FString UWebUIBridge::DeleteProfile(const FString& ProfileId)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (ProfileId.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		return JsonToString(Result);
	}

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings)
	{
		Result->SetBoolField(TEXT("success"), false);
		return JsonToString(Result);
	}

	// Check it's not built-in
	if (const FAgentProfile* Profile = Settings->FindProfileById(ProfileId))
	{
		if (Profile->bIsBuiltIn)
		{
			Result->SetBoolField(TEXT("success"), false);
			return JsonToString(Result);
		}
	}

	Settings->RemoveCustomProfile(ProfileId);
	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
}

FString UWebUIBridge::GetProfileDetail(const FString& ProfileId)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings || ProfileId.IsEmpty())
	{
		Result->SetBoolField(TEXT("found"), false);
		return JsonToString(Result);
	}

	const FAgentProfile* Profile = Settings->FindProfileById(ProfileId);
	if (!Profile)
	{
		Result->SetBoolField(TEXT("found"), false);
		return JsonToString(Result);
	}

	Result->SetBoolField(TEXT("found"), true);
	Result->SetStringField(TEXT("profileId"), Profile->ProfileId);
	Result->SetStringField(TEXT("displayName"), Profile->DisplayName);
	Result->SetStringField(TEXT("description"), Profile->Description);
	Result->SetBoolField(TEXT("isBuiltIn"), Profile->bIsBuiltIn);
	Result->SetStringField(TEXT("customInstructions"), Profile->CustomInstructions);

	// Tool description overrides
	TSharedPtr<FJsonObject> OverridesObj = MakeShared<FJsonObject>();
	for (const auto& Pair : Profile->ToolDescriptionOverrides)
	{
		OverridesObj->SetStringField(Pair.Key, Pair.Value);
	}
	Result->SetObjectField(TEXT("toolDescriptionOverrides"), OverridesObj);

	return JsonToString(Result);
}

FString UWebUIBridge::UpdateProfile(const FString& ProfileId, const FString& DisplayName, const FString& Description, const FString& CustomInstructions)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings || ProfileId.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		return JsonToString(Result);
	}

	FAgentProfile* Profile = Settings->FindProfileByIdMutable(ProfileId);
	if (!Profile)
	{
		Result->SetBoolField(TEXT("success"), false);
		return JsonToString(Result);
	}

	if (!DisplayName.IsEmpty())
	{
		Profile->DisplayName = DisplayName;
	}
	Profile->Description = Description;
	Profile->CustomInstructions = CustomInstructions;
	Settings->SaveConfig();

	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
}

void UWebUIBridge::SetToolDescriptionOverride(const FString& ProfileId, const FString& ToolName, const FString& DescriptionOverride)
{
	if (ProfileId.IsEmpty() || ToolName.IsEmpty()) return;

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings) return;

	FAgentProfile* Profile = Settings->FindProfileByIdMutable(ProfileId);
	if (!Profile) return;

	if (DescriptionOverride.IsEmpty())
	{
		Profile->ToolDescriptionOverrides.Remove(ToolName);
	}
	else
	{
		Profile->ToolDescriptionOverrides.Add(ToolName, DescriptionOverride);
	}
	Settings->SaveConfig();
}

// ── Context Mentions ────────────────────────────────────────────────

FString UWebUIBridge::SearchContextItems(const FString& Query)
{
	TArray<TSharedPtr<FJsonValue>> Results;
	const FString LowerQuery = Query.ToLower();
	const int32 MaxResults = 50;

	// Scan blueprints and assets from the asset registry
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();

	const UACPSettings* Settings = UACPSettings::Get();
	bool bIncludeEngine = Settings ? Settings->bIncludeEngineContent : false;
	bool bIncludePlugins = Settings ? Settings->bIncludePluginContent : false;

	struct FAssetTypeInfo
	{
		UClass* Class;
		FString Category;
		FString Type;
	};

	TArray<FAssetTypeInfo> AssetTypes = {
		{ UBlueprint::StaticClass(),       TEXT("Blueprints"),            TEXT("blueprint") },
		{ UWidgetBlueprint::StaticClass(), TEXT("Widget Blueprints"),     TEXT("widget") },
		{ UAnimBlueprint::StaticClass(),   TEXT("Animation Blueprints"),  TEXT("anim_blueprint") },
		{ UBehaviorTree::StaticClass(),    TEXT("Behavior Trees"),        TEXT("behavior_tree") },
		{ UMaterial::StaticClass(),        TEXT("Materials"),             TEXT("material") },
		{ UDataTable::StaticClass(),       TEXT("Data Tables"),           TEXT("data_table") }
	};

	for (const FAssetTypeInfo& TypeInfo : AssetTypes)
	{
		if (Results.Num() >= MaxResults) break;

		TArray<FAssetData> Assets;
		AssetRegistry.GetAssetsByClass(TypeInfo.Class->GetClassPathName(), Assets, true);

		for (const FAssetData& Asset : Assets)
		{
			if (Results.Num() >= MaxResults) break;

			FString PackagePath = Asset.PackagePath.ToString();

			// Scope filtering
			if (PackagePath.StartsWith(TEXT("/Game")))
			{ /* always include */ }
			else if (PackagePath.StartsWith(TEXT("/Engine")) && bIncludeEngine)
			{ /* include */ }
			else if (bIncludePlugins && !PackagePath.StartsWith(TEXT("/Engine")))
			{ /* plugin content */ }
			else
			{
				continue;
			}

			FString Name = Asset.AssetName.ToString();
			FString Path = Asset.PackageName.ToString();

			// Filter by query
			if (!LowerQuery.IsEmpty())
			{
				if (!Name.ToLower().Contains(LowerQuery) && !Path.ToLower().Contains(LowerQuery))
				{
					continue;
				}
			}

			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Name);
			Item->SetStringField(TEXT("path"), Path);
			Item->SetStringField(TEXT("category"), TypeInfo.Category);
			Item->SetStringField(TEXT("type"), TypeInfo.Type);
			Results.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	// Scan C++ files from project Source folder
	if (Results.Num() < MaxResults)
	{
		FString SourceDir = UACPSettings::GetWorkingDirectory() / TEXT("Source");
		if (FPaths::DirectoryExists(SourceDir))
		{
			TArray<FString> FoundFiles;
			IFileManager::Get().FindFilesRecursive(FoundFiles, *SourceDir, TEXT("*.h"), true, false);
			IFileManager::Get().FindFilesRecursive(FoundFiles, *SourceDir, TEXT("*.cpp"), true, false);

			for (const FString& FilePath : FoundFiles)
			{
				if (Results.Num() >= MaxResults) break;

				FString FileName = FPaths::GetCleanFilename(FilePath);
				FString Extension = FPaths::GetExtension(FilePath).ToLower();

				// Filter by query
				if (!LowerQuery.IsEmpty())
				{
					if (!FileName.ToLower().Contains(LowerQuery) && !FilePath.ToLower().Contains(LowerQuery))
					{
						continue;
					}
				}

				FString RelativePath = FilePath;
				FPaths::MakePathRelativeTo(RelativePath, *UACPSettings::GetWorkingDirectory());

				FString Category = Extension == TEXT("h") ? TEXT("C++ Headers") : TEXT("C++ Sources");
				FString Type = Extension == TEXT("h") ? TEXT("cpp_header") : TEXT("cpp_source");

				TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
				Item->SetStringField(TEXT("name"), FileName);
				Item->SetStringField(TEXT("path"), RelativePath);
				Item->SetStringField(TEXT("category"), Category);
				Item->SetStringField(TEXT("type"), Type);
				Results.Add(MakeShared<FJsonValueObject>(Item));
			}
		}
	}

	return JsonArrayToString(Results);
}

static FString SanitizeExportFilename(FString Name)
{
	Name.TrimStartAndEndInline();
	if (Name.IsEmpty())
	{
		Name = TEXT("chat-session");
	}

	Name.ReplaceInline(TEXT("/"), TEXT("-"));
	Name.ReplaceInline(TEXT("\\"), TEXT("-"));
	Name.ReplaceInline(TEXT(":"), TEXT("-"));
	Name.ReplaceInline(TEXT("\""), TEXT(""));
	Name.ReplaceInline(TEXT("<"), TEXT(""));
	Name.ReplaceInline(TEXT(">"), TEXT(""));
	Name.ReplaceInline(TEXT("|"), TEXT("-"));
	Name.ReplaceInline(TEXT("*"), TEXT(""));
	Name.ReplaceInline(TEXT("?"), TEXT(""));
	Name.ReplaceInline(TEXT("\n"), TEXT(" "));
	Name.ReplaceInline(TEXT("\r"), TEXT(" "));
	Name.ReplaceInline(TEXT("\t"), TEXT(" "));
	while (Name.ReplaceInline(TEXT("  "), TEXT(" ")) > 0) {}
	Name.TrimStartAndEndInline();

	if (Name.Len() > 64)
	{
		Name = Name.Left(64);
		Name.TrimEndInline();
	}
	if (Name.IsEmpty())
	{
		Name = TEXT("chat-session");
	}
	return Name;
}

static FString BuildSessionMarkdown(const FACPActiveSession& Session)
{
	const FString SessionTitle = Session.Metadata.Title.IsEmpty() ? TEXT("New chat") : Session.Metadata.Title;
	const FString CreatedAt = Session.Metadata.CreatedAt.GetTicks() > 0
		? Session.Metadata.CreatedAt.ToString(TEXT("%Y-%m-%d %H:%M:%S"))
		: TEXT("Unknown");
	const FString LastModifiedAt = Session.Metadata.LastModifiedAt.GetTicks() > 0
		? Session.Metadata.LastModifiedAt.ToString(TEXT("%Y-%m-%d %H:%M:%S"))
		: TEXT("Unknown");

	FString Markdown;
	Markdown.Reserve(32768);
	Markdown += FString::Printf(TEXT("# %s\n\n"), *SessionTitle);
	Markdown += FString::Printf(TEXT("- Agent: `%s`\n"), *Session.Metadata.AgentName);
	Markdown += FString::Printf(TEXT("- Session ID: `%s`\n"), *Session.Metadata.SessionId);
	Markdown += FString::Printf(TEXT("- Created: `%s`\n"), *CreatedAt);
	Markdown += FString::Printf(TEXT("- Last Modified: `%s`\n"), *LastModifiedAt);
	Markdown += FString::Printf(TEXT("- Message Count: `%d`\n\n"), Session.Messages.Num());
	Markdown += TEXT("---\n\n");

	if (Session.Messages.Num() == 0)
	{
		Markdown += TEXT("_No messages in this session._\n");
		return Markdown;
	}

	for (const FACPChatMessage& Message : Session.Messages)
	{
		FString Heading = MessageRoleToSummaryLabel(Message.Role);
		if (Message.Timestamp.GetTicks() > 0)
		{
			Heading += FString::Printf(TEXT(" (%s)"), *Message.Timestamp.ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		}
		Markdown += FString::Printf(TEXT("## %s\n\n"), *Heading);

		for (const FACPContentBlock& Block : Message.ContentBlocks)
		{
			switch (Block.Type)
			{
			case EACPContentBlockType::Text:
				Markdown += Block.Text + TEXT("\n\n");
				break;

			case EACPContentBlockType::Thought:
				Markdown += TEXT("<details>\n<summary>Thinking</summary>\n\n");
				Markdown += Block.Text + TEXT("\n\n");
				Markdown += TEXT("</details>\n\n");
				break;

			case EACPContentBlockType::ToolCall:
				Markdown += FString::Printf(TEXT("### Tool Call: %s\n\n"), Block.ToolName.IsEmpty() ? TEXT("tool") : *Block.ToolName);
				if (!Block.ToolArguments.IsEmpty())
				{
					Markdown += TEXT("```json\n");
					Markdown += Block.ToolArguments;
					Markdown += TEXT("\n```\n\n");
				}
				break;

			case EACPContentBlockType::ToolResult:
			{
				const TCHAR* ResultStatus = Block.bToolSuccess ? TEXT("Success") : TEXT("Error");
				Markdown += FString::Printf(TEXT("### Tool Result (%s)\n\n"), ResultStatus);
				FString ToolResult = Block.ToolResultContent;
				const int32 MaxResultChars = 120000;
				bool bTruncated = false;
				if (ToolResult.Len() > MaxResultChars)
				{
					ToolResult = ToolResult.Left(MaxResultChars);
					bTruncated = true;
				}
				if (!ToolResult.IsEmpty())
				{
					Markdown += TEXT("```\n");
					Markdown += ToolResult;
					Markdown += TEXT("\n```\n\n");
				}
				if (Block.ToolResultImages.Num() > 0)
				{
					Markdown += FString::Printf(TEXT("- Images: %d\n\n"), Block.ToolResultImages.Num());
				}
				if (bTruncated)
				{
					Markdown += TEXT("_Tool result truncated for export size limits._\n\n");
				}
				break;
			}

			case EACPContentBlockType::Error:
				Markdown += FString::Printf(TEXT("> **Error:** %s\n\n"), *Block.Text);
				break;

			case EACPContentBlockType::System:
				Markdown += FString::Printf(TEXT("> **System:** %s\n\n"), *Block.Text);
				break;

			case EACPContentBlockType::Image:
				Markdown += TEXT("_Image block omitted from markdown export._\n\n");
				break;

			default:
				break;
			}
		}
	}

	return Markdown;
}

// ── Session Management ──────────────────────────────────────────────

FString UWebUIBridge::DeleteSession(const FString& SessionId)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (SessionId.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		return JsonToString(Result);
	}

	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	FACPSessionManager& SessionMgr = FACPSessionManager::Get();

	// Find the agent that owns this session and resolve the agent's native session ID.
	// The SessionId from JS may be a Unreal GUID (for sessions created through the UI)
	// or the agent's native session ID (for sessions only listed from the remote list).
	// The JSONL file on disk is named after the agent's native ID, not the Unreal GUID.
	FString AgentName = AgentMgr.GetSessionAgent(SessionId);
	FString AgentSessionId;

	// Get the agent's native session ID from the active session metadata (before closing it)
	const FACPActiveSession* ActiveSession = SessionMgr.GetActiveSession(SessionId);
	if (ActiveSession)
	{
		AgentSessionId = ActiveSession->Metadata.AgentSessionId;
	}

	if (AgentName.IsEmpty())
	{
		// Try cached session lists (these use agent-native IDs)
		for (const FString& Name : AgentMgr.GetAvailableAgentNames())
		{
			TArray<FACPRemoteSessionEntry> Sessions = AgentMgr.GetCachedSessionList(Name);
			for (const FACPRemoteSessionEntry& Entry : Sessions)
			{
				if (Entry.SessionId == SessionId)
				{
					AgentName = Name;
					break;
				}
			}
			if (!AgentName.IsEmpty()) break;
		}
	}

	// The ID to use for file deletion: prefer the agent's native ID, fall back to SessionId
	// (which is already the native ID for sessions that were never opened through the UI)
	FString FileSessionId = AgentSessionId.IsEmpty() ? SessionId : AgentSessionId;

	// Close the active session
	SessionMgr.CloseSession(SessionId);
	AgentMgr.UnregisterSession(SessionId);

	// Delete the agent's native session file on disk.
	if (!AgentName.IsEmpty())
	{
		if (AgentName.Equals(TEXT("Claude Code"), ESearchCase::IgnoreCase))
		{
			FString SessionFilePath = FACPClaudeCodeHistoryReader::GetSessionJsonlPath(FileSessionId, UACPSettings::GetWorkingDirectory());
			if (FPaths::FileExists(SessionFilePath))
			{
				IFileManager::Get().Delete(*SessionFilePath);
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Deleted Claude Code session file: %s"), *SessionFilePath);
			}
			else
			{
				UE_LOG(LogAgentIntegrationKit, Warning, TEXT("WebUIBridge: Claude Code session file not found for deletion: %s"), *SessionFilePath);
			}
		}
		else if (AgentName.Equals(TEXT("Codex CLI"), ESearchCase::IgnoreCase))
		{
			// Codex stores sessions as: ~/.codex/sessions/[YYYY/MM/DD/]rollout-YYYY-MM-DDThh-mm-ss-<session-uuid>.jsonl
			// The session UUID is the last component of the filename before .jsonl
			FString CodexHome = FPaths::Combine(FPlatformProcess::UserHomeDir(), TEXT(".codex"));
			FString SessionsDir = FPaths::Combine(CodexHome, TEXT("sessions"));

			if (FPaths::DirectoryExists(SessionsDir))
			{
				// Search recursively for the file containing the session UUID
				TArray<FString> FoundFiles;
				FString SearchPattern = FString::Printf(TEXT("*-%s.jsonl"), *FileSessionId);
				IFileManager::Get().FindFilesRecursive(FoundFiles, *SessionsDir, *SearchPattern, true, false);

				if (FoundFiles.Num() > 0)
				{
					IFileManager::Get().Delete(*FoundFiles[0]);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Deleted Codex session file: %s"), *FoundFiles[0]);
				}
				else
				{
					UE_LOG(LogAgentIntegrationKit, Warning, TEXT("WebUIBridge: Codex session file not found for deletion (searched %s for *-%s.jsonl)"), *SessionsDir, *FileSessionId);
				}
			}

			// Also check archived sessions
			FString ArchivedDir = FPaths::Combine(CodexHome, TEXT("archived_sessions"));
			if (FPaths::DirectoryExists(ArchivedDir))
			{
				TArray<FString> ArchivedFiles;
				FString SearchPattern = FString::Printf(TEXT("*-%s.jsonl"), *FileSessionId);
				IFileManager::Get().FindFilesRecursive(ArchivedFiles, *ArchivedDir, *SearchPattern, true, false);

				if (ArchivedFiles.Num() > 0)
				{
					IFileManager::Get().Delete(*ArchivedFiles[0]);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Deleted archived Codex session file: %s"), *ArchivedFiles[0]);
				}
			}
		}

		// Refresh the cached session list so the deleted session is removed
		AgentMgr.RequestSessionList(AgentName);
	}

	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
}

FString UWebUIBridge::ExportSessionToMarkdown(const FString& SessionId)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetBoolField(TEXT("canceled"), false);

	if (SessionId.IsEmpty())
	{
		Result->SetStringField(TEXT("error"), TEXT("Empty session ID"));
		return JsonToString(Result);
	}

	FACPSessionManager& SessionMgr = FACPSessionManager::Get();
	const FACPActiveSession* Session = SessionMgr.GetActiveSession(SessionId);
	if (!Session)
	{
		Result->SetStringField(
			TEXT("error"),
			TEXT("Session is not loaded in memory. Open the chat once to load ACP history, then export.")
		);
		return JsonToString(Result);
	}

	if (Session->Messages.Num() == 0 && Session->bIsLoadingHistory)
	{
		Result->SetStringField(
			TEXT("error"),
			TEXT("Session history is still loading from ACP. Wait a moment and try export again.")
		);
		return JsonToString(Result);
	}

	const FString Markdown = BuildSessionMarkdown(*Session);

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		Result->SetStringField(TEXT("error"), TEXT("Desktop platform unavailable"));
		return JsonToString(Result);
	}

	const void* ParentWindowHandle = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
		? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	const FString DefaultFileName = SanitizeExportFilename(Session->Metadata.Title) + TEXT(".md");
	TArray<FString> SaveFilenames;
	const bool bDialogAccepted = DesktopPlatform->SaveFileDialog(
		ParentWindowHandle,
		TEXT("Export Conversation"),
		FPaths::ProjectSavedDir(),
		DefaultFileName,
		TEXT("Markdown Files (*.md)|*.md"),
		0,
		SaveFilenames
	);

	if (!bDialogAccepted || SaveFilenames.Num() == 0)
	{
		Result->SetBoolField(TEXT("canceled"), true);
		return JsonToString(Result);
	}

	const FString& SavePath = SaveFilenames[0];
	const bool bSaved = FFileHelper::SaveStringToFile(
		Markdown,
		*SavePath,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM
	);
	if (!bSaved)
	{
		Result->SetStringField(TEXT("error"), TEXT("Failed to write markdown file"));
		return JsonToString(Result);
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("savedPath"), SavePath);
	return JsonToString(Result);
}

// ── Source Control ──────────────────────────────────────────────────

FString UWebUIBridge::GetSourceControlStatus()
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

#if ENGINE_MINOR_VERSION >= 6
	ISourceControlModule* SCModule = ISourceControlModule::GetPtr();
#else
	ISourceControlModule* SCModule = FModuleManager::GetModulePtr<ISourceControlModule>("SourceControl");
#endif
	if (!SCModule || !SCModule->IsEnabled())
	{
		Result->SetBoolField(TEXT("enabled"), false);
		Result->SetStringField(TEXT("provider"), TEXT(""));
		Result->SetStringField(TEXT("branch"), TEXT(""));
		Result->SetNumberField(TEXT("changesCount"), -1);
		Result->SetBoolField(TEXT("connected"), false);
		return JsonToString(Result);
	}

	ISourceControlProvider& Provider = SCModule->GetProvider();
	Result->SetBoolField(TEXT("enabled"), true);
	Result->SetStringField(TEXT("provider"), Provider.GetName().ToString());
	Result->SetBoolField(TEXT("connected"), Provider.IsAvailable());

	TMap<ISourceControlProvider::EStatus, FString> Status = Provider.GetStatus();
	FString* Branch = Status.Find(ISourceControlProvider::EStatus::Branch);
	Result->SetStringField(TEXT("branch"), Branch ? *Branch : TEXT(""));

	TOptional<int> NumChanges = Provider.GetNumLocalChanges();
	Result->SetNumberField(TEXT("changesCount"), NumChanges.IsSet() ? NumChanges.GetValue() : -1);

	return JsonToString(Result);
}

void UWebUIBridge::OpenSourceControlChangelist()
{
	ISourceControlWindowsModule& SCWindows = ISourceControlWindowsModule::Get();
	if (SCWindows.CanShowChangelistsTab())
	{
		SCWindows.ShowChangelistsTab();
	}
}

void UWebUIBridge::OpenSourceControlSubmit()
{
	FSourceControlWindows::ChoosePackagesToCheckIn();
}

// ── Agent Setup ─────────────────────────────────────────────────────

FString UWebUIBridge::GetAgentInstallInfo(const FString& AgentName)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (AgentName.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Empty agent name"));
		return JsonToString(Result);
	}

	FAgentInstallInfo Info = FAgentInstaller::GetAgentInstallInfo(AgentName);

	Result->SetStringField(TEXT("agentName"), Info.AgentName);
	Result->SetStringField(TEXT("baseExecutableName"), Info.BaseExecutableName);
	Result->SetStringField(TEXT("installCommand"), Info.GetBaseInstallCommand());
	Result->SetStringField(TEXT("installUrl"), Info.BaseInstallUrl);
	Result->SetBoolField(TEXT("requiresAdapter"), Info.RequiresAdapter());
	Result->SetBoolField(TEXT("requiresBaseCLI"), Info.RequiresBaseCLI());

	return JsonToString(Result);
}

void UWebUIBridge::InstallAgent(const FString& AgentName)
{
	if (AgentName.IsEmpty()) return;

	TWeakObjectPtr<UWebUIBridge> WeakThis(this);

	FAgentInstaller::Get().InstallAgentAsync(
		AgentName,
		// Progress callback
		FOnInstallProgress::CreateLambda([WeakThis, AgentName](const FString& StatusMessage)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, AgentName, StatusMessage]()
			{
				if (UWebUIBridge* Self = WeakThis.Get())
				{
					if (Self->OnInstallProgressCallback.IsValid())
					{
						Self->OnInstallProgressCallback(AgentName, StatusMessage);
					}
				}
			});
		}),
		// Completion callback
		FOnInstallComplete::CreateLambda([WeakThis, AgentName](bool bSuccess, const FString& ErrorMessage)
		{
			AsyncTask(ENamedThreads::GameThread, [WeakThis, AgentName, bSuccess, ErrorMessage]()
			{
				if (UWebUIBridge* Self = WeakThis.Get())
				{
					if (Self->OnInstallCompleteCallback.IsValid())
					{
						Self->OnInstallCompleteCallback(AgentName, bSuccess, ErrorMessage);
					}
				}
			});
		})
	);
}

FString UWebUIBridge::RefreshAgentStatus(const FString& AgentName)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (AgentName.IsEmpty())
	{
		Result->SetStringField(TEXT("status"), TEXT("unknown"));
		Result->SetStringField(TEXT("statusMessage"), TEXT("Empty agent name"));
		return JsonToString(Result);
	}

	// Invalidate cache and re-evaluate
	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->InvalidateAgentStatusCache();
	}

	// Re-fetch configs (this triggers re-evaluation of all agents)
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	TArray<FACPAgentConfig> Configs = AgentMgr.GetAllAgentConfigs();

	// Find the requested agent
	for (const FACPAgentConfig& Config : Configs)
	{
		if (Config.AgentName == AgentName)
		{
			FString StatusStr;
			switch (Config.Status)
			{
			case EACPAgentStatus::Available:     StatusStr = TEXT("available"); break;
			case EACPAgentStatus::NotInstalled:  StatusStr = TEXT("not_installed"); break;
			case EACPAgentStatus::MissingApiKey: StatusStr = TEXT("missing_key"); break;
			default:                             StatusStr = TEXT("unknown"); break;
			}
			Result->SetStringField(TEXT("status"), StatusStr);
			Result->SetStringField(TEXT("statusMessage"), Config.StatusMessage);
			return JsonToString(Result);
		}
	}

	Result->SetStringField(TEXT("status"), TEXT("unknown"));
	Result->SetStringField(TEXT("statusMessage"), TEXT("Agent not found"));
	return JsonToString(Result);
}

void UWebUIBridge::CopyToClipboard(const FString& Text)
{
	FPlatformApplicationMisc::ClipboardCopy(*Text);
}

FString UWebUIBridge::GetClipboardText()
{
	FString ClipboardContent;
	FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);
	return ClipboardContent;
}

void UWebUIBridge::OpenUrl(const FString& Url)
{
	if (!Url.IsEmpty())
	{
		FPlatformProcess::LaunchURL(*Url, nullptr, nullptr);
	}
}

void UWebUIBridge::OpenPath(const FString& Path, int32 Line)
{
	if (Path.IsEmpty()) return;

	// Check if it's a UE asset path (/Game/, /Engine/, /Script/, or any /MountPoint/ path)
	if (Path.StartsWith(TEXT("/")))
	{
		// Try to find as an asset first
		FAssetRegistryModule& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		FAssetData AssetData = AssetRegistry.Get().GetAssetByObjectPath(FSoftObjectPath(Path));

		if (!AssetData.IsValid())
		{
			// Try with .0 suffix stripped (some paths include sub-object)
			FString CleanPath = Path;
			int32 DotIndex;
			if (CleanPath.FindLastChar('.', DotIndex))
			{
				CleanPath = CleanPath.Left(DotIndex);
				AssetData = AssetRegistry.Get().GetAssetByObjectPath(FSoftObjectPath(CleanPath));
			}
		}

		if (!AssetData.IsValid())
		{
			// Package path without .AssetName suffix (e.g. /Game/Path/BP_C7 → /Game/Path/BP_C7.BP_C7)
			FString AssetName = FPaths::GetBaseFilename(Path);
			if (!AssetName.IsEmpty())
			{
				FString FullObjectPath = Path + TEXT(".") + AssetName;
				AssetData = AssetRegistry.Get().GetAssetByObjectPath(FSoftObjectPath(FullObjectPath));
			}
		}

		if (!AssetData.IsValid())
		{
			// Fall back to package name lookup (handles any asset in the package)
			TArray<FAssetData> PackageAssets;
			AssetRegistry.Get().GetAssetsByPackageName(*Path, PackageAssets);
			if (PackageAssets.Num() > 0)
			{
				AssetData = PackageAssets[0];
			}
		}

		if (AssetData.IsValid())
		{
			// Sync Content Browser to the asset
			TArray<FAssetData> Assets = { AssetData };
			IContentBrowserSingleton::Get().SyncBrowserToAssets(Assets, /*bAllowLockedBrowsers=*/true, /*bFocusContentBrowser=*/true);

			// Load the asset (GetAsset() only returns already-loaded objects — returns null after GC)
			UObject* LoadedAsset = AssetData.GetAsset();
			if (!LoadedAsset)
			{
				LoadedAsset = LoadObject<UObject>(nullptr, *AssetData.GetObjectPathString());
			}

			if (LoadedAsset)
			{
				if (UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
				{
					EditorSubsystem->OpenEditorForAsset(LoadedAsset);
				}
			}
			return;
		}
	}

	// Check if it's a filesystem source file
	FString FullPath = Path;
	if (!FPaths::FileExists(FullPath))
	{
		// Try relative to project
		FullPath = FPaths::Combine(UACPSettings::GetWorkingDirectory(), Path);
	}

	if (FPaths::FileExists(FullPath))
	{
		FString Extension = FPaths::GetExtension(FullPath).ToLower();
		if (Extension == TEXT("h") || Extension == TEXT("cpp") || Extension == TEXT("c") ||
			Extension == TEXT("cs") || Extension == TEXT("py") || Extension == TEXT("js") ||
			Extension == TEXT("ts") || Extension == TEXT("ini") || Extension == TEXT("txt"))
		{
			FSourceCodeNavigation::OpenSourceFile(FullPath, Line, 0);
			return;
		}
	}

	// Last resort: try loading as UObject path
	if (Path.StartsWith(TEXT("/")))
	{
		if (UObject* Obj = LoadObject<UObject>(nullptr, *Path))
		{
			if (UAssetEditorSubsystem* EditorSubsystem = GEditor->GetEditorSubsystem<UAssetEditorSubsystem>())
			{
				EditorSubsystem->OpenEditorForAsset(Obj);
			}
		}
	}
}

void UWebUIBridge::OpenPluginSettings()
{
	ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings");
	if (SettingsModule)
	{
		SettingsModule->ShowViewer(TEXT("Project"), TEXT("Plugins"), TEXT("Agent Integration Kit"));
	}
}

void UWebUIBridge::RestartEditor()
{
	FUnrealEdMisc::Get().RestartEditor(false);
}

void UWebUIBridge::CheckForPluginUpdate()
{
	FAgentIntegrationKitModule::CheckForPluginUpdate();
}

// ── Agent Authentication ────────────────────────────────────────────

FString UWebUIBridge::GetAuthMethods(const FString& AgentName)
{
	TArray<FACPAuthMethod> Methods = FACPAgentManager::Get().GetAuthMethods(AgentName);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FACPAuthMethod& M : Methods)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), M.Id);
		Obj->SetStringField(TEXT("name"), M.Name);
		Obj->SetStringField(TEXT("description"), M.Description);
		Obj->SetBoolField(TEXT("isTerminalAuth"), M.bIsTerminalAuth);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}

	FString Out;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Arr, Writer);
	return Out;
}

void UWebUIBridge::StartAgentLogin(const FString& AgentName, const FString& MethodId)
{
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Starting login for %s (method: %s)"), *AgentName, *MethodId);
	FACPAgentManager::Get().AuthenticateAgent(AgentName, MethodId);
}

void UWebUIBridge::BindOnLoginComplete(FWebJSFunction Callback)
{
	OnLoginCompleteCallback = Callback;
	BindDelegates();
}

// ── Agent Usage / Rate Limits ────────────────────────────────────────

// Helper: serialize rate limit window to JSON
static TSharedPtr<FJsonObject> RateLimitWindowToJson(const FAgentRateLimitWindow& Window)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetNumberField(TEXT("usedPercent"), Window.UsedPercent);
	Obj->SetStringField(TEXT("resetsAt"), Window.ResetsAt.ToIso8601());
	Obj->SetNumberField(TEXT("windowDurationMinutes"), Window.WindowDurationMinutes);
	Obj->SetBoolField(TEXT("hasData"), Window.HasData());
	return Obj;
}

// Helper: serialize full rate limit data to JSON string (includes Meshy balance)
static FString RateLimitDataToJsonString(const FAgentRateLimitData& Data)
{
	TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetBoolField(TEXT("hasData"), Data.bHasData);
	Obj->SetBoolField(TEXT("isLoading"), Data.bIsLoading);
	Obj->SetStringField(TEXT("errorMessage"), Data.ErrorMessage);
	Obj->SetStringField(TEXT("agentName"), Data.AgentName);
	Obj->SetStringField(TEXT("planType"), Data.PlanType);
	Obj->SetStringField(TEXT("lastUpdated"), Data.LastUpdated.ToIso8601());

	// Rate limit windows
	Obj->SetObjectField(TEXT("primary"), RateLimitWindowToJson(Data.Primary));
	Obj->SetObjectField(TEXT("secondary"), RateLimitWindowToJson(Data.Secondary));
	Obj->SetObjectField(TEXT("modelSpecific"), RateLimitWindowToJson(Data.ModelSpecific));
	Obj->SetStringField(TEXT("modelSpecificLabel"), Data.ModelSpecificLabel);

	// Extra usage (Claude Extra)
	TSharedRef<FJsonObject> ExtraObj = MakeShared<FJsonObject>();
	ExtraObj->SetBoolField(TEXT("isEnabled"), Data.ExtraUsage.bIsEnabled);
	ExtraObj->SetNumberField(TEXT("usedAmount"), Data.ExtraUsage.UsedAmount);
	ExtraObj->SetNumberField(TEXT("limitAmount"), Data.ExtraUsage.LimitAmount);
	ExtraObj->SetStringField(TEXT("currencyCode"), Data.ExtraUsage.CurrencyCode);
	ExtraObj->SetBoolField(TEXT("hasData"), Data.ExtraUsage.HasData());
	Obj->SetObjectField(TEXT("extraUsage"), ExtraObj);

	// Meshy credits (global, not per-agent)
	TSharedRef<FJsonObject> MeshyObj = MakeShared<FJsonObject>();
	UACPSettings* Settings = UACPSettings::Get();
	bool bMeshyConfigured = Settings && Settings->HasMeshyAuth();
	FAgentUsageMonitor& Monitor = FAgentUsageMonitor::Get();
	MeshyObj->SetBoolField(TEXT("configured"), bMeshyConfigured);
	MeshyObj->SetNumberField(TEXT("balance"), Monitor.GetCachedMeshyBalance());
	MeshyObj->SetBoolField(TEXT("isLoading"), Monitor.IsMeshyBalanceLoading());
	MeshyObj->SetStringField(TEXT("error"), Monitor.GetMeshyBalanceError());
	Obj->SetObjectField(TEXT("meshy"), MeshyObj);

	return JsonToString(Obj);
}

FString UWebUIBridge::GetAgentUsage(const FString& AgentName)
{
	if (AgentName.IsEmpty())
	{
		return TEXT("{\"hasData\":false}");
	}

	FAgentUsageMonitor& Monitor = FAgentUsageMonitor::Get();

	// If this agent is supported, request an update (non-blocking, will fire callback when done)
	if (FAgentUsageMonitor::IsAgentSupported(AgentName))
	{
		Monitor.RequestUsageUpdate(AgentName);
	}

	// Also trigger Meshy balance fetch if configured
	UACPSettings* Settings = UACPSettings::Get();
	if (Settings && Settings->HasMeshyAuth())
	{
		Monitor.RequestMeshyBalanceUpdate();
	}

	// Return whatever is cached (may be empty if first fetch)
	const FAgentRateLimitData& Data = Monitor.GetCachedUsage(AgentName);
	return RateLimitDataToJsonString(Data);
}

void UWebUIBridge::RefreshAgentUsage(const FString& AgentName)
{
	if (AgentName.IsEmpty()) return;

	FAgentUsageMonitor& Monitor = FAgentUsageMonitor::Get();

	if (FAgentUsageMonitor::IsAgentSupported(AgentName))
	{
		Monitor.RequestUsageUpdate(AgentName);
	}

	// Also refresh Meshy balance
	UACPSettings* Settings = UACPSettings::Get();
	if (Settings && Settings->HasMeshyAuth())
	{
		Monitor.RequestMeshyBalanceUpdate();
	}
}

void UWebUIBridge::BindOnUsageUpdated(FWebJSFunction Callback)
{
	OnUsageUpdatedCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnMcpStatus(FWebJSFunction Callback)
{
	OnMcpStatusCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnContinuationDraftReady(FWebJSFunction Callback)
{
	OnContinuationDraftReadyCallback = Callback;
}

void UWebUIBridge::BindOnSessionListUpdated(FWebJSFunction Callback)
{
	OnSessionListUpdatedCallback = Callback;
	BindDelegates();

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: BindOnSessionListUpdated called"));

	// Immediately push any already-cached session lists so the UI doesn't miss
	// data that arrived before this callback was bound.
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	TArray<FString> AllAgents = AgentMgr.GetAvailableAgentNames();

	for (const FString& AgentName : AllAgents)
	{
		TArray<FACPRemoteSessionEntry> Sessions = AgentMgr.GetCachedSessionList(AgentName);
		if (Sessions.Num() > 0)
		{
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Pushing %d cached sessions for '%s'"), Sessions.Num(), *AgentName);
			AgentMgr.OnAgentSessionListReceived.Broadcast(AgentName, Sessions);
		}
	}

	// Proactively connect ACP agents that aren't connected yet so their
	// session lists get fetched. This must happen AFTER the callback is set
	// (above) so push updates reach the JS side when they arrive.
	int32 ConnectingCount = 0;
	for (const FString& AgentName : AllAgents)
	{
		if (AgentMgr.IsOpenRouterAgent(AgentName)) continue;
		if (AgentMgr.IsConnectedToAgent(AgentName))
		{
			// Already connected — just request the session list again
			AgentMgr.RequestSessionList(AgentName);
			continue;
		}

		FACPAgentConfig* Config = AgentMgr.GetAgentConfig(AgentName);
		if (Config && Config->Status == EACPAgentStatus::Available)
		{
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Auto-connecting agent '%s' for session listing"), *AgentName);
			AgentMgr.ConnectToAgent(AgentName);
			ConnectingCount++;
		}
	}
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: Auto-connecting %d agents for session listing"), ConnectingCount);
}

FString UWebUIBridge::RefreshSessionList()
{
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();
	TArray<FString> AllAgents = AgentMgr.GetAvailableAgentNames();

	int32 ConnectingCount = 0;
	for (const FString& AgentName : AllAgents)
	{
		if (AgentMgr.IsOpenRouterAgent(AgentName)) continue;

		if (AgentMgr.IsConnectedToAgent(AgentName))
		{
			// Already connected — just request the session list
			AgentMgr.RequestSessionList(AgentName);
		}
		else
		{
			FACPAgentConfig* Config = AgentMgr.GetAgentConfig(AgentName);
			if (Config && Config->Status == EACPAgentStatus::Available)
			{
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: RefreshSessionList: connecting agent '%s'"), *AgentName);
				AgentMgr.ConnectToAgent(AgentName);
				ConnectingCount++;
			}
		}
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetNumberField(TEXT("connectingCount"), ConnectingCount);
	return JsonToString(Result);
}

void UWebUIBridge::NotifyMcpStatus(const FString& SessionId, const FString& Status)
{
	// Copy SessionId before clearing McpWaitingSessionId — callers pass
	// McpWaitingSessionId by const ref, so clearing it would alias-invalidate SessionId.
	const FString CapturedSessionId = SessionId;

	// Clean up MCP listeners
	if (McpToolsDiscoveredHandle.IsValid())
	{
		FMCPServer::Get().OnClientToolsDiscovered.Remove(McpToolsDiscoveredHandle);
		McpToolsDiscoveredHandle.Reset();
	}
	if (McpTimeoutTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(McpTimeoutTickerHandle);
		McpTimeoutTickerHandle.Reset();
	}
	McpWaitingSessionId.Empty();

	// Fire JS callback
	if (OnMcpStatusCallback.IsValid())
	{
		TWeakObjectPtr<UWebUIBridge> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSessionId, Status]()
		{
			if (UWebUIBridge* Self = WeakThis.Get())
			{
				if (Self->OnMcpStatusCallback.IsValid())
				{
					Self->OnMcpStatusCallback(CapturedSessionId, Status);
				}
			}
		});
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUIBridge: MCP status '%s' for session %s"), *Status, *CapturedSessionId);
}

// ── Attachments ──────────────────────────────────────────────────────

// Helper: serialize attachment list to JSON string (metadata only, no base64)
static FString SerializeAttachmentList(const TArray<FACPContextAttachment>& Attachments)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FACPContextAttachment& Att : Attachments)
	{
		TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("id"), Att.AttachmentId.ToString());

		FString TypeStr;
		FString DisplayName;
		switch (Att.Type)
		{
		case EACPAttachmentType::BlueprintNode:
			TypeStr = TEXT("blueprint_node");
			DisplayName = Att.NodeAttachment.NodeTitle;
			break;
		case EACPAttachmentType::Blueprint:
			TypeStr = TEXT("blueprint");
			DisplayName = Att.BlueprintAttachment.DisplayName;
			break;
		case EACPAttachmentType::ImageAsset:
			TypeStr = TEXT("image");
			DisplayName = Att.ImageAttachment.DisplayName;
			Obj->SetStringField(TEXT("mimeType"), Att.ImageAttachment.MimeType);
			Obj->SetNumberField(TEXT("width"), Att.ImageAttachment.Width);
			Obj->SetNumberField(TEXT("height"), Att.ImageAttachment.Height);
			break;
		case EACPAttachmentType::FileAsset:
			TypeStr = TEXT("file");
			DisplayName = Att.FileAttachment.DisplayName;
			Obj->SetStringField(TEXT("mimeType"), Att.FileAttachment.MimeType);
			Obj->SetNumberField(TEXT("sizeBytes"), static_cast<double>(Att.FileAttachment.SizeBytes));
			Obj->SetBoolField(TEXT("hasExtractedText"), Att.FileAttachment.bHasExtractedText);
			break;
		}
		Obj->SetStringField(TEXT("type"), TypeStr);
		Obj->SetStringField(TEXT("displayName"), DisplayName);
		Arr.Add(MakeShared<FJsonValueObject>(Obj));
	}
	return JsonArrayToString(Arr);
}

FString UWebUIBridge::PasteClipboardImage()
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	FACPClipboardImageData ClipData = FACPClipboardImageReader::ReadImageFromClipboard();
	if (!ClipData.bIsValid)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("No image on clipboard"));
		return JsonToString(Result);
	}

	FACPAttachmentManager& AttMgr = FACPAttachmentManager::Get();

	if (ClipData.EncodedData.Num() > 0)
	{
		// macOS path — already PNG/JPEG encoded
		AttMgr.AddImageFromEncodedData(ClipData.EncodedData, ClipData.MimeType, ClipData.Width, ClipData.Height, TEXT("Pasted Image"));
	}
	else if (ClipData.RawPixels.Num() > 0)
	{
		// Windows path — raw BGRA pixels
		AttMgr.AddImageFromRawData(ClipData.RawPixels, ClipData.Width, ClipData.Height, TEXT("Pasted Image"));
	}

	Result->SetBoolField(TEXT("success"), true);
	return JsonToString(Result);
}

FString UWebUIBridge::OpenImagePicker()
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetNumberField(TEXT("count"), 0);
		return JsonToString(Result);
	}

	TArray<FString> OutFiles;
	const void* ParentWindowHandle = FSlateApplication::Get().GetActiveTopLevelWindow().IsValid()
		? FSlateApplication::Get().GetActiveTopLevelWindow()->GetNativeWindow()->GetOSWindowHandle()
		: nullptr;

	if (DesktopPlatform->OpenFileDialog(
		ParentWindowHandle,
		TEXT("Select Attachments"),
		FPaths::ProjectDir(),
		TEXT(""),
		TEXT("Supported Files (*.png;*.jpg;*.jpeg;*.bmp;*.pdf;*.txt;*.md;*.json;*.csv;*.xml;*.yaml;*.yml;*.log;*.ini)|*.png;*.jpg;*.jpeg;*.bmp;*.pdf;*.txt;*.md;*.json;*.csv;*.xml;*.yaml;*.yml;*.log;*.ini"),
		EFileDialogFlags::Multiple,
		OutFiles))
	{
		FACPAttachmentManager& AttMgr = FACPAttachmentManager::Get();
		for (const FString& FilePath : OutFiles)
		{
			const FString Ext = FPaths::GetExtension(FilePath).ToLower();
			if (Ext == TEXT("png") || Ext == TEXT("jpg") || Ext == TEXT("jpeg") || Ext == TEXT("bmp"))
			{
				AttMgr.AddImageFromFile(FilePath);
			}
			else
			{
				AttMgr.AddFileFromPath(FilePath);
			}
		}
		Result->SetBoolField(TEXT("success"), true);
		Result->SetNumberField(TEXT("count"), OutFiles.Num());
	}
	else
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetNumberField(TEXT("count"), 0);
	}

	return JsonToString(Result);
}

FString UWebUIBridge::AddImageFromBase64(const FString& Base64Data, const FString& MimeType, int32 Width, int32 Height, const FString& DisplayName)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (Base64Data.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Empty base64 data"));
		return JsonToString(Result);
	}

	TArray<uint8> DecodedBytes;
	if (!FBase64::Decode(Base64Data, DecodedBytes))
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Failed to decode base64"));
		return JsonToString(Result);
	}

	FACPAttachmentManager& AttMgr = FACPAttachmentManager::Get();
	AttMgr.AddImageFromEncodedData(DecodedBytes, MimeType.IsEmpty() ? TEXT("image/png") : MimeType, Width, Height,
		DisplayName.IsEmpty() ? TEXT("Dropped Image") : DisplayName);

	// Return the ID of the last added attachment
	const TArray<FACPContextAttachment>& Atts = AttMgr.GetAttachments();
	if (Atts.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("attachmentId"), Atts.Last().AttachmentId.ToString());
	}
	else
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Attachment was not added"));
	}

	return JsonToString(Result);
}

FString UWebUIBridge::AddFileFromBase64(const FString& Base64Data, const FString& MimeType, const FString& DisplayName)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	if (Base64Data.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Empty base64 data"));
		return JsonToString(Result);
	}

	TArray<uint8> DecodedBytes;
	if (!FBase64::Decode(Base64Data, DecodedBytes))
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Failed to decode base64"));
		return JsonToString(Result);
	}

	FACPAttachmentManager& AttMgr = FACPAttachmentManager::Get();
	AttMgr.AddFileFromEncodedData(
		DecodedBytes,
		MimeType.IsEmpty() ? TEXT("application/octet-stream") : MimeType,
		DisplayName.IsEmpty() ? TEXT("Dropped File") : DisplayName);

	const TArray<FACPContextAttachment>& Atts = AttMgr.GetAttachments();
	if (Atts.Num() > 0)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("attachmentId"), Atts.Last().AttachmentId.ToString());
	}
	else
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Attachment was not added"));
	}

	return JsonToString(Result);
}

void UWebUIBridge::RemoveAttachment(const FString& AttachmentId)
{
	FGuid Guid;
	if (FGuid::Parse(AttachmentId, Guid))
	{
		FACPAttachmentManager::Get().RemoveAttachment(Guid);
	}
}

FString UWebUIBridge::GetAttachments()
{
	return SerializeAttachmentList(FACPAttachmentManager::Get().GetAttachments());
}

void UWebUIBridge::BindOnAttachmentsChanged(FWebJSFunction Callback)
{
	OnAttachmentsChangedCallback = Callback;

	// Subscribe to attachment manager delegate (not part of BindDelegates since it's on AttachmentManager, not AgentManager)
	if (!AttachmentsChangedHandle.IsValid())
	{
		AttachmentsChangedHandle = FACPAttachmentManager::Get().OnAttachmentsChanged.AddLambda(
			[this]()
			{
				if (!OnAttachmentsChangedCallback.IsValid()) return;

				FString JsonStr = SerializeAttachmentList(FACPAttachmentManager::Get().GetAttachments());

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnAttachmentsChangedCallback.IsValid())
						{
							Self->OnAttachmentsChangedCallback(CapturedJson);
						}
					}
				});
			}
		);
	}

}

void UWebUIBridge::BindOnInstallProgress(FWebJSFunction Callback)
{
	OnInstallProgressCallback = Callback;
}

void UWebUIBridge::BindOnInstallComplete(FWebJSFunction Callback)
{
	OnInstallCompleteCallback = Callback;
}

void UWebUIBridge::BindOnModelsAvailable(FWebJSFunction Callback)
{
	OnModelsAvailableCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnCommandsAvailable(FWebJSFunction Callback)
{
	OnCommandsAvailableCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnPlanUpdate(FWebJSFunction Callback)
{
	OnPlanUpdateCallback = Callback;
	BindDelegates();
}

// ── Streaming Callbacks ──────────────────────────────────────────────

void UWebUIBridge::BindOnMessage(FWebJSFunction Callback)
{
	OnMessageCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnStateChanged(FWebJSFunction Callback)
{
	OnStateChangedCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnPermissionRequest(FWebJSFunction Callback)
{
	OnPermissionRequestCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnModesAvailable(FWebJSFunction Callback)
{
	OnModesAvailableCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::BindOnModeChanged(FWebJSFunction Callback)
{
	OnModeChangedCallback = Callback;
	BindDelegates();
}

void UWebUIBridge::RespondToPermission(const FString& AgentName, int32 RequestId, const FString& OptionId, const FString& OutcomeMetaJson)
{
	if (AgentName.IsEmpty() || OptionId.IsEmpty()) return;

	TSharedPtr<FJsonObject> OutcomeMeta;
	if (!OutcomeMetaJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OutcomeMetaJson);
		FJsonSerializer::Deserialize(Reader, OutcomeMeta);
	}

	FACPAgentManager::Get().RespondToPermissionRequest(AgentName, RequestId, OptionId, OutcomeMeta);
}

void UWebUIBridge::BindDelegates()
{
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();

	// Only bind once
	if (!AgentMessageHandle.IsValid())
	{
		AgentMessageHandle = AgentMgr.OnAgentMessage.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const FACPSessionUpdate& Update)
			{
				// ── Persist streaming content to SessionManager (mirrors Slate UI) ──
				// Without this, switching sessions loses assistant messages because
				// GetSessionMessages() reads from the session manager, not the JS store.
				if (Update.UpdateType != EACPUpdateType::UsageUpdate && Update.UpdateType != EACPUpdateType::Plan)
				{
					FACPSessionManager& SessionMgr = FACPSessionManager::Get();

					// UserMessageChunk (from history replay) — finish any in-progress assistant message,
					// then add the user message directly
					if (Update.UpdateType == EACPUpdateType::UserMessageChunk)
					{
						int32* MsgIdxPtr = StreamingMessageIndices.Find(SessionId);
						if (MsgIdxPtr && *MsgIdxPtr != INDEX_NONE)
						{
							SessionMgr.FinishMessage(SessionId, *MsgIdxPtr);
							*MsgIdxPtr = INDEX_NONE;
						}
						SessionMgr.AddUserMessage(SessionId, Update.TextChunk);
					}
					else
					{
						// Start a new assistant message if one isn't active for this session
						int32* MsgIdxPtr = StreamingMessageIndices.Find(SessionId);
						if (!MsgIdxPtr || *MsgIdxPtr == INDEX_NONE)
						{
							int32 NewIdx = SessionMgr.StartAssistantMessage(SessionId);
							StreamingMessageIndices.Add(SessionId, NewIdx);
							MsgIdxPtr = StreamingMessageIndices.Find(SessionId);
						}

						int32 MsgIdx = *MsgIdxPtr;

							bool bFinishStreamingAfterUpdate = false;
							switch (Update.UpdateType)
							{
						case EACPUpdateType::AgentMessageChunk:
							if (Update.bIsSystemStatus)
							{
								SessionMgr.AppendStreamingText(SessionId, MsgIdx, EACPContentBlockType::System, Update.TextChunk);
							}
							else
							{
								SessionMgr.AppendStreamingText(SessionId, MsgIdx, EACPContentBlockType::Text, Update.TextChunk);
							}
							break;

						case EACPUpdateType::AgentThoughtChunk:
							SessionMgr.AppendStreamingText(SessionId, MsgIdx, EACPContentBlockType::Thought, Update.TextChunk);
							break;

						case EACPUpdateType::ToolCall:
							{
								FACPContentBlock Block;
								Block.Type = EACPContentBlockType::ToolCall;
								Block.ToolCallId = Update.ToolCallId;
								Block.ToolName = Update.ToolName;
								Block.ToolArguments = Update.ToolArguments;
								Block.ParentToolCallId = Update.ParentToolCallId;
								SessionMgr.AppendContentBlock(SessionId, MsgIdx, Block);
							}
							break;

						case EACPUpdateType::ToolCallUpdate:
							{
								FACPContentBlock Block;
								Block.Type = EACPContentBlockType::ToolResult;
								Block.ToolCallId = Update.ToolCallId;
								Block.ToolResultContent = Update.ToolResult;
								Block.bToolSuccess = Update.bToolSuccess;
								Block.ToolResultImages = Update.ToolResultImages;
								Block.ParentToolCallId = Update.ParentToolCallId;
								SessionMgr.AppendContentBlock(SessionId, MsgIdx, Block);
							}
							break;

							case EACPUpdateType::Error:
								{
									FACPContentBlock Block;
									Block.Type = EACPContentBlockType::Error;
									Block.Text = Update.ErrorMessage.IsEmpty() ? Update.TextChunk : Update.ErrorMessage;
									SessionMgr.AppendContentBlock(SessionId, MsgIdx, Block);
									bFinishStreamingAfterUpdate = true;
								}
								break;

							default:
								break;
							}

							if (bFinishStreamingAfterUpdate)
							{
								SessionMgr.FinishMessage(SessionId, MsgIdx);
								*MsgIdxPtr = INDEX_NONE;
							}
						}
					}

				if (!OnMessageCallback.IsValid()) return;

				// Serialize the update to JSON
				TSharedRef<FJsonObject> UpdateJson = MakeShared<FJsonObject>();
				UpdateJson->SetStringField(TEXT("agentName"), AgentName);

				// Map update type
				FString TypeStr;
				switch (Update.UpdateType)
				{
				case EACPUpdateType::AgentMessageChunk:  TypeStr = TEXT("text_chunk"); break;
				case EACPUpdateType::AgentThoughtChunk:  TypeStr = TEXT("thought_chunk"); break;
				case EACPUpdateType::ToolCall:            TypeStr = TEXT("tool_call"); break;
				case EACPUpdateType::ToolCallUpdate:      TypeStr = TEXT("tool_result"); break;
				case EACPUpdateType::Error:               TypeStr = TEXT("error"); break;
				case EACPUpdateType::UserMessageChunk:    TypeStr = TEXT("user_message_chunk"); break;
				case EACPUpdateType::UsageUpdate:         TypeStr = TEXT("usage"); break;
				case EACPUpdateType::Plan:                TypeStr = TEXT("plan"); break;
				default:                                  TypeStr = TEXT("unknown"); break;
				}
				UpdateJson->SetStringField(TEXT("type"), TypeStr);
				UpdateJson->SetStringField(TEXT("text"), Update.TextChunk);

				if (Update.bIsSystemStatus)
				{
					UpdateJson->SetStringField(TEXT("systemStatus"), Update.SystemStatus);
				}

				if (!Update.ToolCallId.IsEmpty())
				{
					UpdateJson->SetStringField(TEXT("toolCallId"), Update.ToolCallId);
					UpdateJson->SetStringField(TEXT("toolName"), Update.ToolName);
					UpdateJson->SetStringField(TEXT("toolArguments"), Update.ToolArguments);
					UpdateJson->SetStringField(TEXT("toolResult"), Update.ToolResult);
					UpdateJson->SetBoolField(TEXT("toolSuccess"), Update.bToolSuccess);
					if (!Update.ParentToolCallId.IsEmpty())
					{
						UpdateJson->SetStringField(TEXT("parentToolCallId"), Update.ParentToolCallId);
					}

					// Serialize tool result images
					if (Update.ToolResultImages.Num() > 0)
					{
						TArray<TSharedPtr<FJsonValue>> ImagesArr;
						for (const FACPToolResultImage& Img : Update.ToolResultImages)
						{
							TSharedRef<FJsonObject> ImgObj = MakeShared<FJsonObject>();
							ImgObj->SetStringField(TEXT("base64"), Img.Base64Data);
							ImgObj->SetStringField(TEXT("mimeType"), Img.MimeType);
							ImgObj->SetNumberField(TEXT("width"), Img.Width);
							ImgObj->SetNumberField(TEXT("height"), Img.Height);
							ImagesArr.Add(MakeShared<FJsonValueObject>(ImgObj));
						}
						UpdateJson->SetArrayField(TEXT("images"), ImagesArr);
					}
				}

				if (!Update.ErrorMessage.IsEmpty())
				{
					UpdateJson->SetStringField(TEXT("errorMessage"), Update.ErrorMessage);
					UpdateJson->SetNumberField(TEXT("errorCode"), Update.ErrorCode);
				}

				// Serialize usage data for usage updates
				if (Update.UpdateType == EACPUpdateType::UsageUpdate)
				{
					const FACPUsageData& U = Update.Usage;
					UpdateJson->SetNumberField(TEXT("inputTokens"), U.InputTokens);
					UpdateJson->SetNumberField(TEXT("outputTokens"), U.OutputTokens);
					UpdateJson->SetNumberField(TEXT("totalTokens"), U.TotalTokens);
					UpdateJson->SetNumberField(TEXT("cacheReadTokens"), U.CacheReadTokens);
					UpdateJson->SetNumberField(TEXT("cacheCreationTokens"), U.CacheCreationTokens);
					UpdateJson->SetNumberField(TEXT("reasoningTokens"), U.ReasoningTokens);
					UpdateJson->SetNumberField(TEXT("costAmount"), U.CostAmount);
					UpdateJson->SetStringField(TEXT("costCurrency"), U.CostCurrency);
					UpdateJson->SetNumberField(TEXT("turnCostUSD"), U.TurnCostUSD);
					UpdateJson->SetNumberField(TEXT("contextUsed"), U.ContextUsed);
					UpdateJson->SetNumberField(TEXT("contextSize"), U.ContextSize);
					UpdateJson->SetNumberField(TEXT("numTurns"), U.NumTurns);
					UpdateJson->SetNumberField(TEXT("durationMs"), U.DurationMs);

					if (U.ModelUsage.Num() > 0)
					{
						TArray<TSharedPtr<FJsonValue>> ModelArr;
						for (const FModelUsageEntry& M : U.ModelUsage)
						{
							TSharedRef<FJsonObject> MObj = MakeShared<FJsonObject>();
							MObj->SetStringField(TEXT("modelName"), M.ModelName);
							MObj->SetNumberField(TEXT("inputTokens"), M.InputTokens);
							MObj->SetNumberField(TEXT("outputTokens"), M.OutputTokens);
							MObj->SetNumberField(TEXT("cacheReadTokens"), M.CacheReadTokens);
							MObj->SetNumberField(TEXT("cacheCreationTokens"), M.CacheCreationTokens);
							MObj->SetNumberField(TEXT("costUSD"), M.CostUSD);
							MObj->SetNumberField(TEXT("contextWindow"), M.ContextWindow);
							MObj->SetNumberField(TEXT("maxOutputTokens"), M.MaxOutputTokens);
							ModelArr.Add(MakeShared<FJsonValueObject>(MObj));
						}
						UpdateJson->SetArrayField(TEXT("modelUsage"), ModelArr);
					}
				}

				FString JsonStr = JsonToString(UpdateJson);

				// Dispatch to game thread — FWebJSFunction calls WKWebView which requires main thread
				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSessionId = SessionId, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnMessageCallback.IsValid())
						{
							Self->OnMessageCallback(CapturedSessionId, CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!AgentStateHandle.IsValid())
	{
		AgentStateHandle = AgentMgr.OnAgentStateChanged.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, EACPClientState State, const FString& Message)
			{
				// Finalize streaming message when agent finishes prompting
				if (State == EACPClientState::Ready || State == EACPClientState::InSession)
				{
					int32* MsgIdxPtr = StreamingMessageIndices.Find(SessionId);
					if (MsgIdxPtr && *MsgIdxPtr != INDEX_NONE)
					{
						FACPSessionManager& SessionMgr = FACPSessionManager::Get();
						const FACPActiveSession* Session = SessionMgr.GetActiveSession(SessionId);
						if (Session && Session->Messages.IsValidIndex(*MsgIdxPtr))
						{
							SessionMgr.UpdateMessage(SessionId, *MsgIdxPtr, Session->Messages[*MsgIdxPtr]);
							SessionMgr.FinishMessage(SessionId, *MsgIdxPtr);
						}
						*MsgIdxPtr = INDEX_NONE;
					}
				}

				if (!OnStateChangedCallback.IsValid()) return;

				FString StateStr;
				switch (State)
				{
				case EACPClientState::Disconnected:  StateStr = TEXT("disconnected"); break;
				case EACPClientState::Connecting:     StateStr = TEXT("connecting"); break;
				case EACPClientState::Initializing:   StateStr = TEXT("initializing"); break;
				case EACPClientState::Ready:          StateStr = TEXT("ready"); break;
				case EACPClientState::InSession:      StateStr = TEXT("in_session"); break;
				case EACPClientState::Prompting:      StateStr = TEXT("prompting"); break;
				case EACPClientState::Error:          StateStr = TEXT("error"); break;
				default:                              StateStr = TEXT("unknown"); break;
				}

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, SessionId, AgentName, StateStr, Message, State]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnStateChangedCallback.IsValid())
						{
							Self->OnStateChangedCallback(SessionId, AgentName, StateStr, Message);
						}

						// Gate chat input until MCP tools are discovered by the ACP agent.
						// ACP agents get MCP config in session/new but discover tools asynchronously.
						if (State == EACPClientState::InSession
							&& FMCPServer::Get().IsRunning()
							&& !FACPAgentManager::Get().IsOpenRouterAgent(AgentName)
							&& Self->McpWaitingSessionId.IsEmpty())  // Not already waiting
						{
							Self->McpWaitingSessionId = SessionId;

							// Fire "waiting" status to JS
							if (Self->OnMcpStatusCallback.IsValid())
							{
								Self->OnMcpStatusCallback(SessionId, TEXT("waiting"));
							}

							// Check if MCP tools were already discovered (race: tools/list
							// can complete before we register the listener)
							if (FMCPServer::Get().HasClientDiscoveredTools())
							{
								Self->NotifyMcpStatus(Self->McpWaitingSessionId, TEXT("ready"));
								// NotifyMcpStatus clears McpWaitingSessionId, skip listener/timeout
							}
							else
							{
							// Listen for MCP client tools/list completion
							if (!Self->McpToolsDiscoveredHandle.IsValid())
							{
								Self->McpToolsDiscoveredHandle = FMCPServer::Get().OnClientToolsDiscovered.AddLambda(
									[WeakThis]()
									{
										if (UWebUIBridge* S = WeakThis.Get())
										{
											if (!S->McpWaitingSessionId.IsEmpty())
											{
												S->NotifyMcpStatus(S->McpWaitingSessionId, TEXT("ready"));
											}
										}
									});
							}

							// Timeout fallback — unblock after 15 seconds
							Self->McpTimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
								FTickerDelegate::CreateLambda([WeakThis](float) -> bool
								{
									if (UWebUIBridge* S = WeakThis.Get())
									{
										if (!S->McpWaitingSessionId.IsEmpty())
										{
											S->NotifyMcpStatus(S->McpWaitingSessionId, TEXT("timeout"));
										}
									}
									return false; // one-shot
								}), 15.0f);

							UE_LOG(LogAgentIntegrationKit, Log,
								TEXT("WebUIBridge: Waiting for MCP tools discovery (session %s, agent %s)"),
								*SessionId, *AgentName);
							} // else (tools not yet discovered)
						}
					}
				});
			}
		);
	}

		if (!AgentErrorHandle.IsValid())
		{
			AgentErrorHandle = AgentMgr.OnAgentError.AddLambda(
				[this](const FString& SessionId, const FString& AgentName, int32 ErrorCode, const FString& ErrorMessage)
				{
					FString RoutedSessionId = SessionId;
					if (RoutedSessionId.IsEmpty())
					{
						RoutedSessionId = FACPAgentManager::Get().GetActiveSessionForAgent(AgentName);
					}

					// Finalize persisted streaming message on hard errors so reloading
					// old sessions does not leave messages marked as still streaming.
					int32* MsgIdxPtr = StreamingMessageIndices.Find(RoutedSessionId);
					if (MsgIdxPtr && *MsgIdxPtr != INDEX_NONE)
					{
						FACPSessionManager& SessionMgr = FACPSessionManager::Get();
						SessionMgr.FinishMessage(RoutedSessionId, *MsgIdxPtr);
						*MsgIdxPtr = INDEX_NONE;
					}

					if (!OnMessageCallback.IsValid()) return;

				// Construct a JSON error update matching the streaming update format
				TSharedRef<FJsonObject> UpdateJson = MakeShared<FJsonObject>();
				UpdateJson->SetStringField(TEXT("agentName"), AgentName);
				UpdateJson->SetStringField(TEXT("type"), TEXT("error"));
				UpdateJson->SetStringField(TEXT("text"), ErrorMessage);
				UpdateJson->SetStringField(TEXT("errorMessage"), ErrorMessage);
				UpdateJson->SetNumberField(TEXT("errorCode"), ErrorCode);

				FString JsonStr = JsonToString(UpdateJson);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSessionId = RoutedSessionId, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnMessageCallback.IsValid())
						{
							Self->OnMessageCallback(CapturedSessionId, CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!PermissionRequestHandle.IsValid())
	{
		PermissionRequestHandle = AgentMgr.OnAgentPermissionRequest.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const FACPPermissionRequest& Request)
			{
				if (!OnPermissionRequestCallback.IsValid()) return;

				TSharedRef<FJsonObject> ReqJson = MakeShared<FJsonObject>();
				ReqJson->SetStringField(TEXT("agentName"), AgentName);
				ReqJson->SetNumberField(TEXT("requestId"), Request.RequestId);
				ReqJson->SetBoolField(TEXT("isAskUserQuestion"), Request.bIsAskUserQuestion);

				// Tool call info
				TSharedPtr<FJsonObject> ToolCallObj = MakeShared<FJsonObject>();
				ToolCallObj->SetStringField(TEXT("toolCallId"), Request.ToolCall.ToolCallId);
				ToolCallObj->SetStringField(TEXT("title"), Request.ToolCall.Title);
				ToolCallObj->SetStringField(TEXT("rawInput"), Request.ToolCall.RawInput);
				ReqJson->SetObjectField(TEXT("toolCall"), ToolCallObj);

				// Permission options
				TArray<TSharedPtr<FJsonValue>> OptionsArr;
				for (const FACPPermissionOption& Opt : Request.Options)
				{
					TSharedPtr<FJsonObject> OptObj = MakeShared<FJsonObject>();
					OptObj->SetStringField(TEXT("optionId"), Opt.OptionId);
					OptObj->SetStringField(TEXT("name"), Opt.Name);
					OptObj->SetStringField(TEXT("kind"), Opt.Kind);
					OptionsArr.Add(MakeShared<FJsonValueObject>(OptObj));
				}
				ReqJson->SetArrayField(TEXT("options"), OptionsArr);

				// Questions (for AskUserQuestion)
				TArray<TSharedPtr<FJsonValue>> QuestionsArr;
				for (const FACPQuestion& Q : Request.Questions)
				{
					TSharedPtr<FJsonObject> QObj = MakeShared<FJsonObject>();
					QObj->SetStringField(TEXT("question"), Q.Question);
					QObj->SetStringField(TEXT("header"), Q.Header);
					QObj->SetBoolField(TEXT("multiSelect"), Q.bMultiSelect);

					TArray<TSharedPtr<FJsonValue>> QOptsArr;
					for (const FACPQuestionOption& QOpt : Q.Options)
					{
						TSharedPtr<FJsonObject> QOptObj = MakeShared<FJsonObject>();
						QOptObj->SetStringField(TEXT("label"), QOpt.Label);
						QOptObj->SetStringField(TEXT("description"), QOpt.Description);
						QOptsArr.Add(MakeShared<FJsonValueObject>(QOptObj));
					}
					QObj->SetArrayField(TEXT("options"), QOptsArr);
					QuestionsArr.Add(MakeShared<FJsonValueObject>(QObj));
				}
				ReqJson->SetArrayField(TEXT("questions"), QuestionsArr);

				FString JsonStr = JsonToString(ReqJson);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSessionId = SessionId, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnPermissionRequestCallback.IsValid())
						{
							Self->OnPermissionRequestCallback(CapturedSessionId, CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!ModesAvailableHandle.IsValid())
	{
		ModesAvailableHandle = AgentMgr.OnAgentModesAvailable.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const FACPSessionModeState& ModeState)
			{
				if (!OnModesAvailableCallback.IsValid()) return;

				TArray<TSharedPtr<FJsonValue>> ModesArr;
				for (const FACPSessionMode& Mode : ModeState.AvailableModes)
				{
					TSharedPtr<FJsonObject> ModeObj = MakeShared<FJsonObject>();
					ModeObj->SetStringField(TEXT("id"), Mode.ModeId);
					ModeObj->SetStringField(TEXT("name"), Mode.Name);
					ModeObj->SetStringField(TEXT("description"), Mode.Description);
					ModesArr.Add(MakeShared<FJsonValueObject>(ModeObj));
				}

				TSharedRef<FJsonObject> ResultJson = MakeShared<FJsonObject>();
				ResultJson->SetArrayField(TEXT("modes"), ModesArr);
				ResultJson->SetStringField(TEXT("currentModeId"), ModeState.CurrentModeId);

				FString JsonStr = JsonToString(ResultJson);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedAgentName = AgentName, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnModesAvailableCallback.IsValid())
						{
							Self->OnModesAvailableCallback(CapturedAgentName, CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!ModeChangedHandle.IsValid())
	{
		ModeChangedHandle = AgentMgr.OnAgentModeChanged.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const FString& ModeId)
			{
				if (!OnModeChangedCallback.IsValid()) return;

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, AgentName, ModeId]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnModeChangedCallback.IsValid())
						{
							Self->OnModeChangedCallback(AgentName, ModeId);
						}
					}
				});
			}
		);
	}

	if (!ModelsAvailableHandle.IsValid())
	{
		ModelsAvailableHandle = AgentMgr.OnAgentModelsAvailable.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const FACPSessionModelState& ModelState)
			{
				if (!OnModelsAvailableCallback.IsValid()) return;

				// Check if ACP client has reasoning effort options
					bool bACPHasReasoning = false;
					FACPAgentManager& Mgr = FACPAgentManager::Get();
					TSharedPtr<FACPClient> ACPClient = Mgr.GetClient(AgentName);
					if (ACPClient.IsValid() && ACPClient->SupportsReasoningEffortControl())
					{
						bACPHasReasoning = true;
					}

				TSharedRef<FJsonObject> ResultJson = MakeShared<FJsonObject>();

				TArray<TSharedPtr<FJsonValue>> ModelsArr;
				for (const FACPModelInfo& Model : ModelState.AvailableModels)
				{
					TSharedRef<FJsonObject> MObj = MakeShared<FJsonObject>();
					MObj->SetStringField(TEXT("id"), Model.ModelId);
					MObj->SetStringField(TEXT("name"), Model.Name);
					MObj->SetStringField(TEXT("description"), Model.Description);
					MObj->SetBoolField(TEXT("supportsReasoning"), Model.SupportsReasoning() || bACPHasReasoning);
					ModelsArr.Add(MakeShared<FJsonValueObject>(MObj));
				}
				ResultJson->SetArrayField(TEXT("models"), ModelsArr);
				ResultJson->SetStringField(TEXT("currentModelId"), ModelState.CurrentModelId);

				FString JsonStr = JsonToString(ResultJson);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedAgentName = AgentName, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnModelsAvailableCallback.IsValid())
						{
							Self->OnModelsAvailableCallback(CapturedAgentName, CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!CommandsAvailableHandle.IsValid())
	{
		CommandsAvailableHandle = AgentMgr.OnAgentCommandsAvailable.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const TArray<FACPSlashCommand>& Commands)
			{
				if (!OnCommandsAvailableCallback.IsValid()) return;

				TArray<TSharedPtr<FJsonValue>> CmdsArr;
				for (const FACPSlashCommand& Cmd : Commands)
				{
					TSharedRef<FJsonObject> CmdObj = MakeShared<FJsonObject>();
					CmdObj->SetStringField(TEXT("name"), Cmd.Name);
					CmdObj->SetStringField(TEXT("description"), Cmd.Description);
					CmdObj->SetStringField(TEXT("inputHint"), Cmd.InputHint);
					CmdsArr.Add(MakeShared<FJsonValueObject>(CmdObj));
				}

				FString JsonStr = JsonArrayToString(CmdsArr);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSessionId = SessionId, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnCommandsAvailableCallback.IsValid())
						{
							Self->OnCommandsAvailableCallback(CapturedSessionId, CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!PlanUpdateHandle.IsValid())
	{
		PlanUpdateHandle = AgentMgr.OnAgentPlanUpdate.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, const FACPPlan& Plan)
			{
				if (!OnPlanUpdateCallback.IsValid()) return;

				TSharedRef<FJsonObject> PlanJson = MakeShared<FJsonObject>();

				TArray<TSharedPtr<FJsonValue>> EntriesArr;
				for (const FACPPlanEntry& Entry : Plan.Entries)
				{
					TSharedRef<FJsonObject> EntryObj = MakeShared<FJsonObject>();
					EntryObj->SetStringField(TEXT("content"), Entry.Content);
					EntryObj->SetStringField(TEXT("activeForm"), Entry.ActiveForm);

					FString PriorityStr;
					switch (Entry.Priority)
					{
					case EACPPlanEntryPriority::High:   PriorityStr = TEXT("high"); break;
					case EACPPlanEntryPriority::Medium: PriorityStr = TEXT("medium"); break;
					case EACPPlanEntryPriority::Low:    PriorityStr = TEXT("low"); break;
					}
					EntryObj->SetStringField(TEXT("priority"), PriorityStr);

					FString StatusStr;
					switch (Entry.Status)
					{
					case EACPPlanEntryStatus::Pending:    StatusStr = TEXT("pending"); break;
					case EACPPlanEntryStatus::InProgress: StatusStr = TEXT("in_progress"); break;
					case EACPPlanEntryStatus::Completed:  StatusStr = TEXT("completed"); break;
					}
					EntryObj->SetStringField(TEXT("status"), StatusStr);

					EntriesArr.Add(MakeShared<FJsonValueObject>(EntryObj));
				}
				PlanJson->SetArrayField(TEXT("entries"), EntriesArr);
				PlanJson->SetNumberField(TEXT("completedCount"), Plan.GetCompletedCount());
				PlanJson->SetNumberField(TEXT("totalCount"), Plan.Entries.Num());

				FString JsonStr = JsonToString(PlanJson);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedSessionId = SessionId, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnPlanUpdateCallback.IsValid())
						{
							Self->OnPlanUpdateCallback(CapturedSessionId, CapturedJson);
						}
					}
				});
			}
		);
	}

	// Usage data updates (from FAgentUsageMonitor, not FACPAgentManager)
	if (!UsageUpdatedHandle.IsValid())
	{
		UsageUpdatedHandle = FAgentUsageMonitor::Get().OnUsageDataUpdated.AddLambda(
			[this](const FString& AgentName, const FAgentRateLimitData& Data)
			{
				if (!OnUsageUpdatedCallback.IsValid()) return;

				FString JsonStr = RateLimitDataToJsonString(Data);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedAgentName = AgentName, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnUsageUpdatedCallback.IsValid())
						{
							Self->OnUsageUpdatedCallback(CapturedAgentName, CapturedJson);
						}
					}
				});
			}
		);
	}

	// Meshy balance updates — re-push usage data so UI sees updated Meshy fields
	if (!MeshyBalanceHandle.IsValid())
	{
		MeshyBalanceHandle = FAgentUsageMonitor::Get().OnMeshyBalanceUpdated.AddLambda(
			[this](bool /*bSuccess*/, int32 /*Balance*/)
			{
				if (!OnUsageUpdatedCallback.IsValid()) return;

				// Re-serialize usage data for all cached agents so Meshy fields are fresh.
				// We push an update with agent name "_meshy" so the UI knows to refresh its cached data.
				// The RateLimitDataToJsonString reads Meshy state from the monitor singleton.
				FAgentRateLimitData DummyData;
				DummyData.AgentName = TEXT("_meshy");
				FString JsonStr = RateLimitDataToJsonString(DummyData);

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, CapturedJson = MoveTemp(JsonStr)]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnUsageUpdatedCallback.IsValid())
						{
							Self->OnUsageUpdatedCallback(TEXT("_meshy"), CapturedJson);
						}
					}
				});
			}
		);
	}

	if (!AgentAuthCompleteHandle.IsValid())
	{
		AgentAuthCompleteHandle = AgentMgr.OnAgentAuthComplete.AddLambda(
			[this](const FString& SessionId, const FString& AgentName, bool bSuccess, const FString& Error)
			{
				if (!OnLoginCompleteCallback.IsValid()) return;

				TWeakObjectPtr<UWebUIBridge> WeakThis(this);
				AsyncTask(ENamedThreads::GameThread, [WeakThis, AgentName, bSuccess, Error]()
				{
					if (UWebUIBridge* Self = WeakThis.Get())
					{
						if (Self->OnLoginCompleteCallback.IsValid())
						{
							Self->OnLoginCompleteCallback(AgentName, bSuccess, Error);
						}
					}
				});
			}
		);
	}

	if (!SessionListUpdatedHandle.IsValid())
	{
		SessionListUpdatedHandle = AgentMgr.OnAgentSessionListReceived.AddLambda(
			[this](const FString& AgentName, const TArray<FACPRemoteSessionEntry>& Sessions)
			{
				if (!OnSessionListUpdatedCallback.IsValid()) return;

				// Build a map from agent session ID → Unreal session ID for active sessions.
				// When a remote session matches an active session, we replace the agent ID
				// with the Unreal ID so the JS merge logic can match and update titles.
				FACPSessionManager& SessionMgr = FACPSessionManager::Get();
				TMap<FString, FString> AgentIdToUnrealId;
				TArray<FString> ActiveIds = SessionMgr.GetActiveSessionIds();
				for (const FString& Id : ActiveIds)
				{
					const FACPActiveSession* Active = SessionMgr.GetActiveSession(Id);
					if (Active && !Active->Metadata.AgentSessionId.IsEmpty())
					{
						AgentIdToUnrealId.Add(Active->Metadata.AgentSessionId, Id);
					}
				}

				// Serialize the session list to JSON
				TArray<TSharedPtr<FJsonValue>> SessionsArray;
				for (const FACPRemoteSessionEntry& Entry : Sessions)
				{
					TSharedRef<FJsonObject> SessionObj = MakeShared<FJsonObject>();

					// If this remote session maps to an active Unreal session,
					// use the Unreal ID so JS dedup/merge works correctly
					FString UseSessionId = Entry.SessionId;
					if (const FString* UnrealId = AgentIdToUnrealId.Find(Entry.SessionId))
					{
						UseSessionId = *UnrealId;

						// Also update the active session's title in the session manager
						if (!Entry.Title.IsEmpty())
						{
							SessionMgr.UpdateSessionTitle(*UnrealId, Entry.Title);
						}
					}

					SessionObj->SetStringField(TEXT("sessionId"), UseSessionId);
					SessionObj->SetStringField(TEXT("title"), Entry.Title);
					if (Entry.UpdatedAt.GetTicks() > 0)
					{
						SessionObj->SetStringField(TEXT("lastModifiedAt"), Entry.UpdatedAt.ToIso8601());
					}
					SessionsArray.Add(MakeShared<FJsonValueObject>(SessionObj));
				}
				FString SessionsJson = JsonArrayToString(SessionsArray);

				// This delegate fires on the game thread (ProcessLine dispatches there).
				// Call the JS callback directly — no need for a second AsyncTask dispatch
				// which can race with GC and cause the callback to silently drop.
				OnSessionListUpdatedCallback(AgentName, SessionsJson);
			}
		);
	}
}

void UWebUIBridge::UnbindDelegates()
{
	FACPAgentManager& AgentMgr = FACPAgentManager::Get();

	if (AgentMessageHandle.IsValid())
	{
		AgentMgr.OnAgentMessage.Remove(AgentMessageHandle);
		AgentMessageHandle.Reset();
	}
	if (AgentStateHandle.IsValid())
	{
		AgentMgr.OnAgentStateChanged.Remove(AgentStateHandle);
		AgentStateHandle.Reset();
	}
	if (AgentErrorHandle.IsValid())
	{
		AgentMgr.OnAgentError.Remove(AgentErrorHandle);
		AgentErrorHandle.Reset();
	}
	if (PermissionRequestHandle.IsValid())
	{
		AgentMgr.OnAgentPermissionRequest.Remove(PermissionRequestHandle);
		PermissionRequestHandle.Reset();
	}
	if (ModesAvailableHandle.IsValid())
	{
		AgentMgr.OnAgentModesAvailable.Remove(ModesAvailableHandle);
		ModesAvailableHandle.Reset();
	}
	if (ModeChangedHandle.IsValid())
	{
		AgentMgr.OnAgentModeChanged.Remove(ModeChangedHandle);
		ModeChangedHandle.Reset();
	}
	if (CommandsAvailableHandle.IsValid())
	{
		AgentMgr.OnAgentCommandsAvailable.Remove(CommandsAvailableHandle);
		CommandsAvailableHandle.Reset();
	}
	if (PlanUpdateHandle.IsValid())
	{
		AgentMgr.OnAgentPlanUpdate.Remove(PlanUpdateHandle);
		PlanUpdateHandle.Reset();
	}
	if (ModelsAvailableHandle.IsValid())
	{
		AgentMgr.OnAgentModelsAvailable.Remove(ModelsAvailableHandle);
		ModelsAvailableHandle.Reset();
	}
	if (UsageUpdatedHandle.IsValid())
	{
		FAgentUsageMonitor::Get().OnUsageDataUpdated.Remove(UsageUpdatedHandle);
		UsageUpdatedHandle.Reset();
	}
	if (MeshyBalanceHandle.IsValid())
	{
		FAgentUsageMonitor::Get().OnMeshyBalanceUpdated.Remove(MeshyBalanceHandle);
		MeshyBalanceHandle.Reset();
	}
	if (AttachmentsChangedHandle.IsValid())
	{
		FACPAttachmentManager::Get().OnAttachmentsChanged.Remove(AttachmentsChangedHandle);
		AttachmentsChangedHandle.Reset();
	}
	if (AgentAuthCompleteHandle.IsValid())
	{
		AgentMgr.OnAgentAuthComplete.Remove(AgentAuthCompleteHandle);
		AgentAuthCompleteHandle.Reset();
	}
	if (McpToolsDiscoveredHandle.IsValid())
	{
		FMCPServer::Get().OnClientToolsDiscovered.Remove(McpToolsDiscoveredHandle);
		McpToolsDiscoveredHandle.Reset();
	}
	if (McpTimeoutTickerHandle.IsValid())
	{
		FTSTicker::RemoveTicker(McpTimeoutTickerHandle);
		McpTimeoutTickerHandle.Reset();
	}
	if (SessionListUpdatedHandle.IsValid())
	{
		AgentMgr.OnAgentSessionListReceived.Remove(SessionListUpdatedHandle);
		SessionListUpdatedHandle.Reset();
	}
}

// ── JSON Serialization Helpers ───────────────────────────────────────

TSharedPtr<FJsonObject> UWebUIBridge::ContentBlockToJson(const FACPContentBlock& Block)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	FString TypeStr;
	switch (Block.Type)
	{
	case EACPContentBlockType::Text:       TypeStr = TEXT("text"); break;
	case EACPContentBlockType::Thought:    TypeStr = TEXT("thought"); break;
	case EACPContentBlockType::ToolCall:   TypeStr = TEXT("tool_call"); break;
	case EACPContentBlockType::ToolResult: TypeStr = TEXT("tool_result"); break;
	case EACPContentBlockType::Image:      TypeStr = TEXT("image"); break;
	case EACPContentBlockType::Error:      TypeStr = TEXT("error"); break;
	case EACPContentBlockType::System:     TypeStr = TEXT("system"); break;
	default:                               TypeStr = TEXT("unknown"); break;
	}
	Obj->SetStringField(TEXT("type"), TypeStr);
	Obj->SetStringField(TEXT("text"), Block.Text);
	Obj->SetBoolField(TEXT("isStreaming"), Block.bIsStreaming);

	if (Block.Type == EACPContentBlockType::ToolCall)
	{
		Obj->SetStringField(TEXT("toolCallId"), Block.ToolCallId);
		Obj->SetStringField(TEXT("toolName"), Block.ToolName);
		Obj->SetStringField(TEXT("toolArguments"), Block.ToolArguments);
		if (!Block.ParentToolCallId.IsEmpty())
		{
			Obj->SetStringField(TEXT("parentToolCallId"), Block.ParentToolCallId);
		}
	}

	if (Block.Type == EACPContentBlockType::ToolResult)
	{
		Obj->SetStringField(TEXT("toolCallId"), Block.ToolCallId);
		Obj->SetStringField(TEXT("toolResult"), Block.ToolResultContent);
		Obj->SetBoolField(TEXT("toolSuccess"), Block.bToolSuccess);
		if (!Block.ParentToolCallId.IsEmpty())
		{
			Obj->SetStringField(TEXT("parentToolCallId"), Block.ParentToolCallId);
		}

		// Serialize tool result images (base64 + metadata)
		if (Block.ToolResultImages.Num() > 0)
		{
			Obj->SetNumberField(TEXT("imageCount"), Block.ToolResultImages.Num());

			TArray<TSharedPtr<FJsonValue>> ImagesArr;
			for (const FACPToolResultImage& Img : Block.ToolResultImages)
			{
				TSharedRef<FJsonObject> ImgObj = MakeShared<FJsonObject>();
				ImgObj->SetStringField(TEXT("base64"), Img.Base64Data);
				ImgObj->SetStringField(TEXT("mimeType"), Img.MimeType);
				ImgObj->SetNumberField(TEXT("width"), Img.Width);
				ImgObj->SetNumberField(TEXT("height"), Img.Height);
				ImagesArr.Add(MakeShared<FJsonValueObject>(ImgObj));
			}
			Obj->SetArrayField(TEXT("images"), ImagesArr);
		}
	}

	return Obj;
}

TSharedPtr<FJsonObject> UWebUIBridge::MessageToJson(const FACPChatMessage& Message)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();

	Obj->SetStringField(TEXT("messageId"), Message.MessageId.ToString());

	FString RoleStr;
	switch (Message.Role)
	{
	case EACPMessageRole::User:      RoleStr = TEXT("user"); break;
	case EACPMessageRole::Assistant: RoleStr = TEXT("assistant"); break;
	case EACPMessageRole::System:    RoleStr = TEXT("system"); break;
	default:                         RoleStr = TEXT("unknown"); break;
	}
	Obj->SetStringField(TEXT("role"), RoleStr);
	Obj->SetBoolField(TEXT("isStreaming"), Message.bIsStreaming);
	Obj->SetStringField(TEXT("timestamp"), Message.Timestamp.ToIso8601());

	// Content blocks
	TArray<TSharedPtr<FJsonValue>> BlocksArray;
	for (const FACPContentBlock& Block : Message.ContentBlocks)
	{
		TSharedPtr<FJsonObject> BlockJson = ContentBlockToJson(Block);
		if (BlockJson.IsValid())
		{
			BlocksArray.Add(MakeShared<FJsonValueObject>(BlockJson));
		}
	}
	Obj->SetArrayField(TEXT("contentBlocks"), BlocksArray);

	return Obj;
}
