// Copyright 2026 Betide Studio. All Rights Reserved.

#include "AgentIntegrationKitModule.h"

DEFINE_LOG_CATEGORY(LogAgentIntegrationKit);
#include "ACPSettingsCustomization.h"
#include "MCPServer.h"
#include "ACPAttachmentManager.h"
#include "AgentUsageMonitor.h"

#include "LevelEditor.h"
#include "ToolMenus.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Reply.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

// Content Browser
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"

// Version check & auto-update
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Interfaces/IPluginManager.h"
#include "ACPSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Containers/Ticker.h"
#include "Misc/MessageDialog.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Async/Async.h"

// Blueprint/Graph context
#include "GraphEditorModule.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "AssetRegistry/AssetData.h"

// WebBrowser for embedded web UI
#include "SWebBrowser.h"
#include "WebBrowserModule.h"
#include "IWebBrowserSingleton.h"
#include "IWebBrowserWindow.h"
#include "WebUIBridge.h"

#define LOCTEXT_NAMESPACE "FAgentIntegrationKitModule"

const FName FAgentIntegrationKitModule::AgentChatTabName(TEXT("AgentChat"));
FPluginUpdateInfo FAgentIntegrationKitModule::CachedUpdateInfo;

// Update notification state (file-static, not class members — keeps header clean)
static TSharedPtr<SNotificationItem> GUpdateNotification;
static FTSTicker::FDelegateHandle GProgressTickerHandle;

// Forward declarations for static helpers (defined after ShutdownModule)
static void StopProgressTicker();
static bool TickUpdateProgress(float);
static void ShowUpdateNotification();

void FAgentIntegrationKitModule::StartupModule()
{
	// Guard against running during cook/commandlet (module type is EditorNoCommandlet,
	// but this is defense-in-depth in case the module is loaded unexpectedly)
	if (IsRunningCommandlet())
	{
		return;
	}

	// Start MCP server to expose Unreal tools to AI agents
	FMCPServer::Get().Start(9315);

	// Register settings detail customization (adds description text to categories)
	FACPSettingsCustomization::Register();

	// Register the Agent Chat tab spawner (WebUI)
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		AgentChatTabName,
		FOnSpawnTab::CreateRaw(this, &FAgentIntegrationKitModule::SpawnAgentChatTab))
		.SetDisplayName(LOCTEXT("AgentChatTabTitle", "Agent Chat"))
		.SetTooltipText(LOCTEXT("AgentChatTabTooltip", "Open the AI Agent Chat window"))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetToolsCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"));

	// Register menus
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FAgentIntegrationKitModule::RegisterMenus));

	// Register Content Browser context menu extension for Blueprints
	RegisterContentBrowserExtension();

	// Initialize usage monitor (polls Claude Code / Codex rate limit APIs)
	FAgentUsageMonitor::Get().Initialize();

	// Check for plugin updates 5 seconds after startup (non-blocking, one-shot)
	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) -> bool
		{
			CheckForPluginUpdate();
			return false; // One-shot, don't repeat
		}),
		5.0f);
}

void FAgentIntegrationKitModule::ShutdownModule()
{
	// If startup was skipped (commandlet mode), nothing to tear down
	if (IsRunningCommandlet())
	{
		return;
	}

	// Clean up update notification
	StopProgressTicker();
	if (GUpdateNotification.IsValid())
	{
		GUpdateNotification->ExpireAndFadeout();
		GUpdateNotification.Reset();
	}

	// Unregister settings detail customization
	FACPSettingsCustomization::Unregister();

	// Shutdown usage monitor
	FAgentUsageMonitor::Get().Shutdown();

	// Stop MCP server
	FMCPServer::Get().Stop();

	// Unregister Content Browser extender
	if (ContentBrowserExtenderHandle.IsValid())
	{
		FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenders = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();
		CBMenuExtenders.RemoveAll([this](const FContentBrowserMenuExtender_SelectedAssets& Delegate)
		{
			return Delegate.GetHandle() == ContentBrowserExtenderHandle;
		});
		ContentBrowserExtenderHandle.Reset();
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);

	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(AgentChatTabName);

	UnbindWebUIInputMethodSystem();

	if (InputMethodSystemSlatePreShutdownDelegateHandle.IsValid())
	{
		if (FSlateApplication::IsInitialized())
		{
			FSlateApplication::Get().OnPreShutdown().Remove(InputMethodSystemSlatePreShutdownDelegateHandle);
		}
		InputMethodSystemSlatePreShutdownDelegateHandle.Reset();
	}

	// Clean up bridge — during editor exit, GC may have already torn down the UObject
	// subsystem, so IsValid()/RemoveFromRoot() can crash in FUObjectArray::IndexToObject.
	// Only touch the pointer if GC is still operational.
	// Clean up browser window — unlink from dock tab before destroying
	if (WebUIBrowserWindow.IsValid())
	{
		WebUIBrowserWindow->OnUnhandledKeyDown().Unbind();
		WebUIBrowserWindow->OnUnhandledKeyUp().Unbind();
#if ENGINE_MINOR_VERSION >= 7
		WebUIBrowserWindow->SetParentDockTab(nullptr);
#endif
		WebUIBrowserWindow.Reset();
	}
	WebUIBrowserWidget.Reset();

	if (WebUIBridgeInstance && !GExitPurge)
	{
		if (IsValid(WebUIBridgeInstance))
		{
			WebUIBridgeInstance->RemoveFromRoot();
		}
		WebUIBridgeInstance = nullptr;
	}
}

void FAgentIntegrationKitModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	// Status bar button to open Agent Chat
	UToolMenu* StatusBarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.StatusBar.ToolBar");
	if (StatusBarMenu)
	{
		FToolMenuSection& Section = StatusBarMenu->AddSection(
			"AgentIntegrationKit",
			FText::GetEmpty(),
			FToolMenuInsert("SourceControl", EToolMenuInsertType::Before)
		);

		const TSharedRef<SWidget> AgentChatStatusWidget =
			SNew(SButton)
			.ButtonStyle(&FAppStyle::Get().GetWidgetStyle<FButtonStyle>("StatusBar.StatusBarButton"))
			.ContentPadding(FMargin(6.0f, 0.0f))
			.ToolTipText(LOCTEXT("StatusBarAgentChatTooltip", "Open the AI Agent Chat window"))
			.OnClicked_Lambda([this]()
			{
				OpenAgentChatWindow();
				return FReply::Handled();
			})
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("Icons.Comment"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(FMargin(5.0f, 0.0f, 0.0f, 0.0f))
				[
					SNew(STextBlock)
					.Text(LOCTEXT("StatusBarAgentChat", "Agent Chat"))
					.TextStyle(&FAppStyle::Get().GetWidgetStyle<FTextBlockStyle>("NormalText"))
				]
			];

		FToolMenuEntry StatusBarEntry = FToolMenuEntry::InitWidget(
			"OpenAgentChat",
			AgentChatStatusWidget,
			FText::GetEmpty(),
			true,
			false
		);
		Section.AddEntry(StatusBarEntry);
	}

	// Add to Window menu
	UToolMenu* WindowMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	FToolMenuSection& WindowSection = WindowMenu->FindOrAddSection("AI");
	WindowSection.AddMenuEntry(
		"OpenAgentChat",
		LOCTEXT("OpenAgentChatMenuLabel", "Agent Chat"),
		LOCTEXT("OpenAgentChatMenuTooltip", "Open the AI Agent Chat window"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
		FUIAction(FExecuteAction::CreateRaw(this, &FAgentIntegrationKitModule::OpenAgentChatWindow))
	);

	// Register node context menu extension
	RegisterNodeContextMenuExtension();
}

void FAgentIntegrationKitModule::RegisterNodeContextMenuExtension()
{
	// Extend the base EdGraphNode context menu
	// This will add our "Select as Context" option to ALL graph nodes
	UToolMenu* NodeMenu = UToolMenus::Get()->ExtendMenu("GraphEditor.GraphNodeContextMenu.EdGraphNode");
	if (NodeMenu)
	{
		NodeMenu->AddDynamicSection(
			"AgentIntegrationKit",
			FNewToolMenuDelegate::CreateLambda([](UToolMenu* InMenu)
			{
				UGraphNodeContextMenuContext* Context = InMenu->FindContext<UGraphNodeContextMenuContext>();
				if (!Context || !Context->Node)
				{
					return;
				}

				FToolMenuSection& AISection = InMenu->FindOrAddSection("AgentIntegrationKit");
				AISection.Label = LOCTEXT("AIContextSection", "AI Context");

				// Capture node pointer for the action
				const UEdGraphNode* Node = Context->Node;

				AISection.AddMenuEntry(
					"SelectAsContext",
					LOCTEXT("SelectAsContext", "Select as Context"),
					LOCTEXT("SelectAsContextTooltip", "Attach this node's data as context for the AI agent"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
					FUIAction(
						FExecuteAction::CreateLambda([Node]()
						{
							if (Node)
							{
								FACPAttachmentManager::Get().AddNodeFromGraph(Node);
							}
						})
					)
				);
			})
		);
	}
}

void FAgentIntegrationKitModule::RegisterContentBrowserExtension()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
	TArray<FContentBrowserMenuExtender_SelectedAssets>& CBMenuExtenders = ContentBrowserModule.GetAllAssetViewContextMenuExtenders();

	CBMenuExtenders.Add(FContentBrowserMenuExtender_SelectedAssets::CreateRaw(
		this, &FAgentIntegrationKitModule::OnExtendContentBrowserAssetMenu));

	ContentBrowserExtenderHandle = CBMenuExtenders.Last().GetHandle();
}

TSharedRef<FExtender> FAgentIntegrationKitModule::OnExtendContentBrowserAssetMenu(const TArray<FAssetData>& SelectedAssets)
{
	TSharedRef<FExtender> Extender = MakeShared<FExtender>();

	// Check if any selected assets are Blueprints
	bool bHasBlueprint = false;
	for (const FAssetData& Asset : SelectedAssets)
	{
		if (Asset.AssetClassPath.GetAssetName() == UBlueprint::StaticClass()->GetFName() ||
			Asset.AssetClassPath.GetAssetName().ToString().Contains(TEXT("Blueprint")))
		{
			bHasBlueprint = true;
			break;
		}
	}

	if (bHasBlueprint)
	{
		Extender->AddMenuExtension(
			"CommonAssetActions",
			EExtensionHook::After,
			nullptr,
			FMenuExtensionDelegate::CreateLambda([SelectedAssets](FMenuBuilder& MenuBuilder)
			{
				MenuBuilder.BeginSection("AIContext", LOCTEXT("AIContextSection", "AI Context"));
				MenuBuilder.AddMenuEntry(
					LOCTEXT("SelectBPAsContext", "Select as AI Context"),
					LOCTEXT("SelectBPAsContextTooltip", "Attach this blueprint's structure as context for the AI agent"),
					FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
					FUIAction(FExecuteAction::CreateLambda([SelectedAssets]()
					{
						for (const FAssetData& Asset : SelectedAssets)
						{
							UObject* LoadedAsset = Asset.GetAsset();
							if (UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset))
							{
								FACPAttachmentManager::Get().AddBlueprintAsset(Blueprint);
							}
						}
					}))
				);
				MenuBuilder.EndSection();
			})
		);
	}

	return Extender;
}

// ============================================
// Update notification helpers
// ============================================

static void StopProgressTicker()
{
	if (GProgressTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(GProgressTickerHandle);
		GProgressTickerHandle.Reset();
	}
}

static bool TickUpdateProgress(float)
{
	if (!GUpdateNotification.IsValid())
	{
		StopProgressTicker();
		return false;
	}

	auto& Info = FAgentIntegrationKitModule::GetUpdateInfoMutable();

	switch (Info.State)
	{
	case EPluginUpdateState::Downloading:
	{
		int32 Pct = FMath::RoundToInt(Info.DownloadProgress * 100.0f);
		GUpdateNotification->SetSubText(FText::FromString(
			FString::Printf(TEXT("Downloading... %d%%"), Pct)));
		return true; // Keep ticking
	}
	case EPluginUpdateState::Downloaded:
		GUpdateNotification->SetText(FText::FromString(
			FString::Printf(TEXT("Agent Integration Kit v%s downloaded"), *Info.LatestVersion)));
		GUpdateNotification->SetSubText(FText::FromString(TEXT("Ready to install. This will require an editor restart.")));
		GUpdateNotification->SetCompletionState(SNotificationItem::CS_Success);
		return false; // Stop ticking
	case EPluginUpdateState::Failed:
		GUpdateNotification->SetText(FText::FromString(TEXT("Update failed")));
		GUpdateNotification->SetSubText(FText::FromString(Info.ErrorMessage));
		GUpdateNotification->SetCompletionState(SNotificationItem::CS_Fail);
		return false;
	default:
		return false;
	}
}

static void ShowUpdateNotification()
{
	// Expire any existing notification
	if (GUpdateNotification.IsValid())
	{
		GUpdateNotification->ExpireAndFadeout();
		GUpdateNotification.Reset();
	}
	StopProgressTicker();

	const auto& Info = FAgentIntegrationKitModule::GetUpdateInfo();

	FNotificationInfo NotifInfo(FText::FromString(
		FString::Printf(TEXT("Agent Integration Kit v%s available"), *Info.LatestVersion)));
	NotifInfo.bFireAndForget = false;
	NotifInfo.bUseSuccessFailIcons = true;
	NotifInfo.bUseLargeFont = true;
	NotifInfo.SubText = FText::FromString(TEXT("A new version is available"));

	const UACPSettings* Settings = UACPSettings::Get();
	const FString BetideToken = Settings->GetBetideApiToken();

	if (Info.bDownloadAvailable && !BetideToken.IsEmpty())
	{
		// Download button — visible in initial state (CS_None)
		FNotificationButtonInfo DownloadBtn(
			FText::FromString(TEXT("Download")),
			FText::FromString(TEXT("Download the update")),
			FSimpleDelegate::CreateLambda([]()
			{
				FAgentIntegrationKitModule::DownloadUpdate();
				if (GUpdateNotification.IsValid())
				{
					GUpdateNotification->SetCompletionState(SNotificationItem::CS_Pending);
					GUpdateNotification->SetSubText(FText::FromString(TEXT("Downloading... 0%")));
					// Start progress ticker (update every 0.25s)
					StopProgressTicker();
					GProgressTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
						FTickerDelegate::CreateStatic(&TickUpdateProgress), 0.25f);
				}
			}),
			SNotificationItem::CS_None);
		NotifInfo.ButtonDetails.Add(DownloadBtn);

		// Install button — visible after download completes (CS_Success)
		FNotificationButtonInfo InstallBtn(
			FText::FromString(TEXT("Install")),
			FText::FromString(TEXT("Install the update (requires editor restart)")),
			FSimpleDelegate::CreateStatic(&FAgentIntegrationKitModule::InstallUpdate),
			SNotificationItem::CS_Success);
		NotifInfo.ButtonDetails.Add(InstallBtn);
	}
	else if (Info.bDownloadAvailable)
	{
		// File exists for this platform but no token configured
		NotifInfo.SubText = FText::FromString(TEXT("Set API token in Project Settings or BETIDE_API_TOKEN env var to enable auto-update"));
		NotifInfo.HyperlinkText = FText::FromString(TEXT("Get it at betide.studio"));
		NotifInfo.Hyperlink = FSimpleDelegate::CreateLambda([]()
		{
			FPlatformProcess::LaunchURL(TEXT("https://betide.studio/plugins/agent-integration-kit"), nullptr, nullptr);
		});
	}
	else
	{
		// No downloadable file for this platform
		NotifInfo.SubText = FText::FromString(TEXT("No auto-update available for your platform yet"));
		NotifInfo.HyperlinkText = FText::FromString(TEXT("Download from betide.studio"));
		NotifInfo.Hyperlink = FSimpleDelegate::CreateLambda([]()
		{
			FPlatformProcess::LaunchURL(TEXT("https://betide.studio/plugins/agent-integration-kit"), nullptr, nullptr);
		});
	}

	// Dismiss button — visible in initial and fail states
	FNotificationButtonInfo DismissBtn(
		FText::FromString(TEXT("Dismiss")),
		FText::GetEmpty(),
		FSimpleDelegate::CreateLambda([]()
		{
			FAgentIntegrationKitModule::GetUpdateInfoMutable().bDismissed = true;
			if (GUpdateNotification.IsValid())
			{
				GUpdateNotification->ExpireAndFadeout();
				GUpdateNotification.Reset();
			}
			StopProgressTicker();
		}),
		SNotificationItem::CS_None);
	DismissBtn.VisibilityOnFail = EVisibility::Visible;
	NotifInfo.ButtonDetails.Add(DismissBtn);

	GUpdateNotification = FSlateNotificationManager::Get().AddNotification(NotifInfo);
	if (GUpdateNotification.IsValid())
	{
		GUpdateNotification->SetCompletionState(SNotificationItem::CS_None);
	}
}

// ============================================
// Update check / download / install
// ============================================

void FAgentIntegrationKitModule::CheckForPluginUpdate()
{
	// Reset so banner hides while we re-check
	CachedUpdateInfo = FPluginUpdateInfo();
	CachedUpdateInfo.State = EPluginUpdateState::Checking;

	const UACPSettings* Settings = UACPSettings::Get();
	if (!Settings->bCheckForUpdates)
	{
		CachedUpdateInfo.State = EPluginUpdateState::None;
		return;
	}

	// Get current plugin version from the .uplugin descriptor
	FString CurrentVersion = TEXT("0.1");
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AgentIntegrationKit"));
	if (Plugin.IsValid())
	{
		CurrentVersion = Plugin->GetDescriptor().VersionName;
	}

	FString EngineVersion = FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	FString Platform = TEXT("Win64");
#if PLATFORM_MAC
	Platform = TEXT("Mac");
#endif
	const bool bBeta = Settings->bUseBetaChannel;

	FString Url = FString::Printf(
		TEXT("https://betide.studio/api/plugins/agent-integration-kit/version?current=%s&engine=%s&platform=%s%s"),
		*FGenericPlatformHttp::UrlEncode(CurrentVersion),
		*FGenericPlatformHttp::UrlEncode(EngineVersion),
		*FGenericPlatformHttp::UrlEncode(Platform),
		bBeta ? TEXT("&channel=beta") : TEXT(""));

	const FString BetideToken = Settings->GetBetideApiToken();
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(Url);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
	if (!BetideToken.IsEmpty())
	{
		Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BetideToken));
	}
	Request->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (!bConnectedSuccessfully || !Response.IsValid() || Response->GetResponseCode() != 200)
			{
				CachedUpdateInfo.State = EPluginUpdateState::None;
				CachedUpdateInfo.bChecked = true;
				return;
			}

			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
			{
				CachedUpdateInfo.State = EPluginUpdateState::None;
				CachedUpdateInfo.bChecked = true;
				return;
			}

			FPluginUpdateInfo Info;
			Info.bChecked = true;
			Json->TryGetBoolField(TEXT("updateAvailable"), Info.bUpdateAvailable);
			Json->TryGetBoolField(TEXT("downloadAvailable"), Info.bDownloadAvailable);
			Json->TryGetStringField(TEXT("latestVersion"), Info.LatestVersion);
			Json->TryGetStringField(TEXT("changelog"), Info.Changelog);
			Info.State = Info.bUpdateAvailable ? EPluginUpdateState::UpdateAvailable : EPluginUpdateState::None;

			// Store on game thread and show notification
			AsyncTask(ENamedThreads::GameThread, [Info]()
			{
				CachedUpdateInfo = Info;
				if (Info.bUpdateAvailable)
				{
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Update available: v%s"), *Info.LatestVersion);
					ShowUpdateNotification();

					// Auto-download if configured and a file exists for this platform
					if (Info.bDownloadAvailable)
					{
						const UACPSettings* Settings = UACPSettings::Get();
						if (Settings->bAutoDownloadUpdates && !Settings->GetBetideApiToken().IsEmpty())
						{
							DownloadUpdate();
							// Transition notification to downloading state
							if (GUpdateNotification.IsValid())
							{
								GUpdateNotification->SetCompletionState(SNotificationItem::CS_Pending);
								GUpdateNotification->SetSubText(FText::FromString(TEXT("Downloading... 0%")));
								StopProgressTicker();
								GProgressTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
									FTickerDelegate::CreateStatic(&TickUpdateProgress), 0.25f);
							}
						}
					}
				}
			});
		});

	Request->ProcessRequest();
}

FString FAgentIntegrationKitModule::GetUpdateCacheDir()
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AgentIntegrationKit-Updates"));
}

void FAgentIntegrationKitModule::DownloadUpdate()
{
	const UACPSettings* Settings = UACPSettings::Get();
	const FString BetideToken = Settings->GetBetideApiToken();
	if (BetideToken.IsEmpty())
	{
		CachedUpdateInfo.State = EPluginUpdateState::Failed;
		CachedUpdateInfo.ErrorMessage = TEXT("No API token configured. Set it in Project Settings or BETIDE_API_TOKEN env var.");
		return;
	}

	// Don't re-download if already downloading or downloaded
	if (CachedUpdateInfo.State == EPluginUpdateState::Downloading ||
		CachedUpdateInfo.State == EPluginUpdateState::Downloaded ||
		CachedUpdateInfo.State == EPluginUpdateState::Installing)
	{
		return;
	}

	CachedUpdateInfo.State = EPluginUpdateState::Downloading;
	CachedUpdateInfo.DownloadProgress = 0.0f;
	CachedUpdateInfo.ErrorMessage.Empty();

	FString EngineVersion = FString::Printf(TEXT("%d.%d"), ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
	FString Platform = TEXT("Win64");
#if PLATFORM_MAC
	Platform = TEXT("Mac");
#endif
	const bool bBeta = Settings->bUseBetaChannel;

	FString Url = FString::Printf(
		TEXT("https://betide.studio/api/external/download?slug=agent-integration-kit&engine=%s&platform=%s%s"),
		*FGenericPlatformHttp::UrlEncode(EngineVersion),
		*FGenericPlatformHttp::UrlEncode(Platform),
		bBeta ? TEXT("&channel=beta") : TEXT(""));

	// Step 1: Get the signed download URL from betide.studio
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MetaRequest = FHttpModule::Get().CreateRequest();
	MetaRequest->SetURL(Url);
	MetaRequest->SetVerb(TEXT("GET"));
	MetaRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
	MetaRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *BetideToken));
	MetaRequest->OnProcessRequestComplete().BindLambda(
		[](FHttpRequestPtr, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			if (!bConnectedSuccessfully || !Response.IsValid())
			{
				CachedUpdateInfo.State = EPluginUpdateState::Failed;
				CachedUpdateInfo.ErrorMessage = TEXT("Failed to connect to betide.studio");
				return;
			}

			const int32 Code = Response->GetResponseCode();
			if (Code != 200)
			{
				// Parse error message from response
				TSharedPtr<FJsonObject> ErrJson;
				TSharedRef<TJsonReader<>> ErrReader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
				FString ErrMsg;
				if (FJsonSerializer::Deserialize(ErrReader, ErrJson) && ErrJson.IsValid())
				{
					ErrJson->TryGetStringField(TEXT("error"), ErrMsg);
				}

				if (Code == 401)
				{
					CachedUpdateInfo.ErrorMessage = ErrMsg.IsEmpty() ? TEXT("Invalid API token") : ErrMsg;
				}
				else if (Code == 403)
				{
					CachedUpdateInfo.ErrorMessage = ErrMsg.IsEmpty() ? TEXT("No access — verify your purchase at betide.studio") : ErrMsg;
				}
				else if (Code == 404)
				{
					CachedUpdateInfo.ErrorMessage = ErrMsg.IsEmpty() ? TEXT("No update file found for your engine version") : ErrMsg;
				}
				else
				{
					CachedUpdateInfo.ErrorMessage = FString::Printf(TEXT("Server error (%d): %s"), Code, *ErrMsg);
				}
				CachedUpdateInfo.State = EPluginUpdateState::Failed;
				return;
			}

			// Parse download metadata
			TSharedPtr<FJsonObject> Json;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Response->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, Json) || !Json.IsValid())
			{
				CachedUpdateInfo.State = EPluginUpdateState::Failed;
				CachedUpdateInfo.ErrorMessage = TEXT("Invalid response from server");
				return;
			}

			FString SignedUrl, FileName, Checksum, Version;
			Json->TryGetStringField(TEXT("url"), SignedUrl);
			Json->TryGetStringField(TEXT("fileName"), FileName);
			Json->TryGetStringField(TEXT("checksum"), Checksum);
			Json->TryGetStringField(TEXT("version"), Version);

			int64 FileSize = 0;
			double FileSizeDouble = 0;
			if (Json->TryGetNumberField(TEXT("fileSize"), FileSizeDouble))
			{
				FileSize = static_cast<int64>(FileSizeDouble);
			}

			if (SignedUrl.IsEmpty() || FileName.IsEmpty())
			{
				CachedUpdateInfo.State = EPluginUpdateState::Failed;
				CachedUpdateInfo.ErrorMessage = TEXT("Server returned empty download URL");
				return;
			}

			CachedUpdateInfo.DownloadUrl = SignedUrl;
			CachedUpdateInfo.FileName = FileName;
			CachedUpdateInfo.FileSize = FileSize;
			CachedUpdateInfo.Checksum = Checksum;
			CachedUpdateInfo.DownloadedVersion = Version;

			// Ensure cache directory exists and clean up old downloads
			FString CacheDir = GetUpdateCacheDir();
			IFileManager::Get().MakeDirectory(*CacheDir, true);

			TArray<FString> OldFiles;
			IFileManager::Get().FindFiles(OldFiles, *(CacheDir / TEXT("*.zip")), true, false);
			for (const FString& OldFile : OldFiles)
			{
				IFileManager::Get().Delete(*(CacheDir / OldFile));
			}

			FString SavePath = FPaths::ConvertRelativePathToFull(CacheDir / FileName);

			// Step 2: Download the actual zip file from signed GCS URL
			TSharedRef<IHttpRequest, ESPMode::ThreadSafe> DownloadRequest = FHttpModule::Get().CreateRequest();
			DownloadRequest->SetURL(SignedUrl);
			DownloadRequest->SetVerb(TEXT("GET"));

			// Track download progress
			if (FileSize > 0)
			{
				DownloadRequest->OnRequestProgress64().BindLambda(
					[FileSize](FHttpRequestPtr, uint64 /*BytesSent*/, uint64 BytesReceived)
					{
						CachedUpdateInfo.DownloadProgress = FMath::Clamp(
							static_cast<float>(BytesReceived) / static_cast<float>(FileSize), 0.0f, 1.0f);
					});
			}

			DownloadRequest->OnProcessRequestComplete().BindLambda(
				[SavePath](FHttpRequestPtr, FHttpResponsePtr DlResponse, bool bDlConnected)
				{
					if (!bDlConnected || !DlResponse.IsValid() || DlResponse->GetResponseCode() != 200)
					{
						CachedUpdateInfo.State = EPluginUpdateState::Failed;
						CachedUpdateInfo.ErrorMessage = TEXT("Failed to download update file from CDN");
						return;
					}

					const TArray<uint8>& Content = DlResponse->GetContent();
					if (Content.Num() == 0)
					{
						CachedUpdateInfo.State = EPluginUpdateState::Failed;
						CachedUpdateInfo.ErrorMessage = TEXT("Downloaded file is empty");
						return;
					}

					// Save zip to disk
					if (!FFileHelper::SaveArrayToFile(Content, *SavePath))
					{
						CachedUpdateInfo.State = EPluginUpdateState::Failed;
						CachedUpdateInfo.ErrorMessage = FString::Printf(TEXT("Failed to save update to: %s"), *SavePath);
						return;
					}

					CachedUpdateInfo.DownloadedZipPath = SavePath;
					CachedUpdateInfo.DownloadProgress = 1.0f;
					CachedUpdateInfo.State = EPluginUpdateState::Downloaded;
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Update v%s downloaded to: %s (%lld bytes)"),
						*CachedUpdateInfo.DownloadedVersion, *SavePath, Content.Num());
				});

			DownloadRequest->ProcessRequest();
		});

	MetaRequest->ProcessRequest();
}

void FAgentIntegrationKitModule::InstallUpdate()
{
	if (CachedUpdateInfo.State != EPluginUpdateState::Downloaded)
	{
		return;
	}

	const FString ZipPath = FPaths::ConvertRelativePathToFull(CachedUpdateInfo.DownloadedZipPath);
	if (!FPaths::FileExists(ZipPath))
	{
		CachedUpdateInfo.State = EPluginUpdateState::Failed;
		CachedUpdateInfo.ErrorMessage = TEXT("Downloaded file no longer exists");
		return;
	}

	// Get plugin install directory
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AgentIntegrationKit"));
	if (!Plugin.IsValid())
	{
		CachedUpdateInfo.State = EPluginUpdateState::Failed;
		CachedUpdateInfo.ErrorMessage = TEXT("Could not locate plugin directory");
		return;
	}

	const FString PluginDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
	const FString PluginParentDir = FPaths::GetPath(PluginDir);
	const FString BackupDir = PluginDir + TEXT("_backup");
	const FString ScriptDir = FPaths::ConvertRelativePathToFull(GetUpdateCacheDir());

	// Confirm with user before closing editor
	EAppReturnType::Type Result = FMessageDialog::Open(EAppMsgType::OkCancel,
		FText::FromString(FString::Printf(
			TEXT("Agent Integration Kit v%s is ready to install.\n\n"
				"The editor will close automatically. The updater will:\n"
				"  1. Back up the current plugin\n"
				"  2. Extract the new version\n"
				"  3. Clean up\n\n"
				"You can relaunch the editor after the update completes.\n\n"
				"Press OK to proceed, or Cancel to install later."),
			*CachedUpdateInfo.LatestVersion)));

	if (Result != EAppReturnType::Ok)
	{
		return;
	}

	// Generate platform-specific updater script
	FString ScriptContent;
	FString ScriptPath;
	FString LaunchExe;
	FString LaunchArgs;

#if PLATFORM_WINDOWS
	// Normalize paths to backslashes for Windows batch script
	FString WinZipPath = ZipPath;
	FString WinPluginDir = PluginDir;
	FString WinPluginParentDir = PluginParentDir;
	FString WinBackupDir = BackupDir;
	FPaths::MakePlatformFilename(WinZipPath);
	FPaths::MakePlatformFilename(WinPluginDir);
	FPaths::MakePlatformFilename(WinPluginParentDir);
	FPaths::MakePlatformFilename(WinBackupDir);

	ScriptPath = ScriptDir / TEXT("updater.bat");
	FPaths::MakePlatformFilename(ScriptPath);

	ScriptContent = FString::Printf(TEXT(
		"@echo off\r\n"
		"echo.\r\n"
		"echo ============================================\r\n"
		"echo   Agent Integration Kit - Auto Updater\r\n"
		"echo ============================================\r\n"
		"echo.\r\n"
		"echo Waiting for Unreal Editor to close...\r\n"
		":WAIT_EDITOR\r\n"
		"tasklist /FI \"IMAGENAME eq UnrealEditor.exe\" 2>NUL | find /I \"UnrealEditor.exe\" >NUL\r\n"
		"if %%ERRORLEVEL%% == 0 (\r\n"
		"    timeout /t 2 /nobreak >NUL\r\n"
		"    goto WAIT_EDITOR\r\n"
		")\r\n"
		"echo Editor closed.\r\n"
		"echo Waiting for file handles to release...\r\n"
		"timeout /t 5 /nobreak >NUL\r\n"
		"echo.\r\n"
		"\r\n"
		"echo Backing up current plugin...\r\n"
		"if exist \"%s\" rmdir /S /Q \"%s\"\r\n"
		"\r\n"
		":RETRY_MOVE\r\n"
		"move /Y \"%s\" \"%s\" >NUL 2>&1\r\n"
		"if %%ERRORLEVEL%% == 0 goto MOVE_OK\r\n"
		"echo.\r\n"
		"echo Plugin files are still locked by another process.\r\n"
		"echo.\r\n"
		"echo Checking for processes that may be locking files...\r\n"
		"set FOUND_BLOCKER=0\r\n"
		"tasklist /FI \"IMAGENAME eq devenv.exe\" 2>NUL | find /I \"devenv.exe\" >NUL && (\r\n"
		"    echo   [!] Visual Studio ^(devenv.exe^) is running\r\n"
		"    set FOUND_BLOCKER=1\r\n"
		")\r\n"
		"tasklist /FI \"IMAGENAME eq rider64.exe\" 2>NUL | find /I \"rider64.exe\" >NUL && (\r\n"
		"    echo   [!] JetBrains Rider ^(rider64.exe^) is running\r\n"
		"    set FOUND_BLOCKER=1\r\n"
		")\r\n"
		"tasklist /FI \"IMAGENAME eq Code.exe\" 2>NUL | find /I \"Code.exe\" >NUL && (\r\n"
		"    echo   [!] VS Code ^(Code.exe^) is running\r\n"
		"    set FOUND_BLOCKER=1\r\n"
		")\r\n"
		"tasklist /FI \"IMAGENAME eq ShaderCompileWorker.exe\" 2>NUL | find /I \"ShaderCompileWorker.exe\" >NUL && (\r\n"
		"    echo   [!] Shader Compile Worker is still running\r\n"
		"    set FOUND_BLOCKER=1\r\n"
		")\r\n"
		"tasklist /FI \"IMAGENAME eq UnrealBuildAccelerator.exe\" 2>NUL | find /I \"UnrealBuildAccelerator.exe\" >NUL && (\r\n"
		"    echo   [!] Unreal Build Accelerator is still running\r\n"
		"    set FOUND_BLOCKER=1\r\n"
		")\r\n"
		"if %%FOUND_BLOCKER%% == 0 (\r\n"
		"    echo   No known blockers found. Files may still be releasing...\r\n"
		")\r\n"
		"echo.\r\n"
		"echo Close the above program^(s^), then press any key to retry.\r\n"
		"echo Or close this window to cancel the update.\r\n"
		"pause >NUL\r\n"
		"goto RETRY_MOVE\r\n"
		"\r\n"
		":MOVE_OK\r\n"
		"echo Backup created successfully.\r\n"
		"echo Extracting update...\r\n"
		"powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\"\r\n"
		"if %%ERRORLEVEL%% == 0 (\r\n"
		"    echo.\r\n"
		"    echo ============================================\r\n"
		"    echo   Update installed successfully!\r\n"
		"    echo ============================================\r\n"
		"    rmdir /S /Q \"%s\"\r\n"
		"    del /F /Q \"%s\"\r\n"
		"    echo.\r\n"
		"    echo You can now restart the Unreal Editor.\r\n"
		") else (\r\n"
		"    echo.\r\n"
		"    echo ERROR: Extraction failed! Restoring backup...\r\n"
		"    if exist \"%s\" rmdir /S /Q \"%s\"\r\n"
		"    move /Y \"%s\" \"%s\"\r\n"
		"    echo Backup restored. Your plugin is unchanged.\r\n"
		")\r\n"
		"echo.\r\n"
		"pause\r\n"),
		*WinBackupDir, *WinBackupDir,                 // rmdir backup if leftover
		*WinPluginDir, *WinBackupDir,                 // move plugin -> backup (retry loop)
		*WinZipPath, *WinPluginParentDir,             // extract zip
		*WinBackupDir,                                // remove backup on success
		*WinZipPath,                                  // remove zip on success
		*WinPluginDir, *WinPluginDir,                 // remove failed extract on fail
		*WinBackupDir, *WinPluginDir);                // restore backup on fail
	LaunchExe = TEXT("cmd.exe");
	LaunchArgs = FString::Printf(TEXT("/c start \"AIK Updater\" /wait cmd.exe /c \"\"%s\"\""), *ScriptPath);
#else
	ScriptPath = FPaths::ConvertRelativePathToFull(ScriptDir / TEXT("updater.sh"));
	ScriptContent = FString::Printf(TEXT(
		"#!/bin/bash\n"
		"echo\n"
		"echo '============================================'\n"
		"echo '  Agent Integration Kit - Auto Updater'\n"
		"echo '============================================'\n"
		"echo\n"
		"echo 'Waiting for Unreal Editor to close...'\n"
		"while pgrep -x 'UnrealEditor' > /dev/null 2>&1; do\n"
		"    sleep 2\n"
		"done\n"
		"echo 'Editor closed. Installing update...'\n"
		"echo\n"
		"echo 'Backing up current plugin...'\n"
		"rm -rf '%s'\n"
		"mv '%s' '%s'\n"
		"if [ $? -ne 0 ]; then\n"
		"    echo 'ERROR: Failed to back up plugin directory.'\n"
		"    read -p 'Press Enter to exit...'\n"
		"    exit 1\n"
		"fi\n"
		"echo 'Extracting update...'\n"
		"unzip -o '%s' -d '%s'\n"
		"if [ $? -eq 0 ]; then\n"
		"    echo\n"
		"    echo 'Update installed successfully!'\n"
		"    rm -rf '%s'\n"
		"    rm -f '%s'\n"
		"    echo 'You can now restart the Unreal Editor.'\n"
		"else\n"
		"    echo\n"
		"    echo 'ERROR: Update failed! Restoring backup...'\n"
		"    rm -rf '%s'\n"
		"    mv '%s' '%s'\n"
		"    echo 'Backup restored. Your plugin is unchanged.'\n"
		"fi\n"
		"echo\n"
		"read -p 'Press Enter to exit...'\n"),
		*BackupDir,                       // rm backup if leftover
		*PluginDir, *BackupDir,           // mv plugin -> backup
		*ZipPath, *PluginParentDir,       // unzip
		*BackupDir,                       // remove backup on success
		*ZipPath,                         // remove zip on success
		*PluginDir,                       // remove failed extract on fail
		*BackupDir, *PluginDir);          // restore backup on fail
	LaunchExe = TEXT("/usr/bin/open");
	LaunchArgs = FString::Printf(TEXT("-a Terminal \"%s\""), *ScriptPath);
#endif

	// Write script
	if (!FFileHelper::SaveStringToFile(ScriptContent, *ScriptPath))
	{
		CachedUpdateInfo.State = EPluginUpdateState::Failed;
		CachedUpdateInfo.ErrorMessage = TEXT("Failed to write updater script");
		return;
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Updater script written to: %s"), *ScriptPath);

#if !PLATFORM_WINDOWS
	// Make script executable on macOS/Linux
	FPlatformProcess::ExecProcess(TEXT("/bin/chmod"), *FString::Printf(TEXT("+x \"%s\""), *ScriptPath), nullptr, nullptr, nullptr);
#endif

	// Launch the updater script detached with a visible console window
	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*LaunchExe, *LaunchArgs,
		true,   // bLaunchDetached
		false,  // bLaunchHidden
		false,  // bLaunchReallyHidden
		nullptr, 0, nullptr, nullptr);

	if (!ProcHandle.IsValid())
	{
		CachedUpdateInfo.State = EPluginUpdateState::Failed;
		CachedUpdateInfo.ErrorMessage = TEXT("Failed to launch updater script");
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Failed to launch updater: %s %s"), *LaunchExe, *LaunchArgs);
		return;
	}

	CachedUpdateInfo.State = EPluginUpdateState::Installing;
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Updater launched. Requesting editor shutdown."));

	// Request a clean editor shutdown so the updater script can proceed
	GEngine->DeferredCommands.Add(TEXT("QUIT_EDITOR"));
}

void FAgentIntegrationKitModule::OpenAgentChatWindow()
{
	FGlobalTabmanager::Get()->TryInvokeTab(AgentChatTabName);
}

void FAgentIntegrationKitModule::BindWebUIInputMethodSystem(const TSharedPtr<SWebBrowser>& Browser)
{
	WebUIInputMethodBrowserWidget = Browser;

	if (!Browser.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	ITextInputMethodSystem* InputMethodSystem = FSlateApplication::Get().GetTextInputMethodSystem();
	if (!InputMethodSystem)
	{
		UE_LOG(LogAgentIntegrationKit, Warning,
			TEXT("WebUI: No Input Method System available; non-ASCII input may not work in chat."));
		return;
	}

	Browser->BindInputMethodSystem(InputMethodSystem);

	if (!InputMethodSystemSlatePreShutdownDelegateHandle.IsValid())
	{
		InputMethodSystemSlatePreShutdownDelegateHandle =
			FSlateApplication::Get().OnPreShutdown().AddRaw(this, &FAgentIntegrationKitModule::UnbindWebUIInputMethodSystem);
	}
}

void FAgentIntegrationKitModule::UnbindWebUIInputMethodSystem()
{
	if (FSlateApplication::IsInitialized())
	{
		if (TSharedPtr<SWebBrowser> Browser = WebUIInputMethodBrowserWidget.Pin())
		{
			Browser->UnbindInputMethodSystem();
		}
	}

	WebUIInputMethodBrowserWidget.Reset();
}

TSharedRef<SDockTab> FAgentIntegrationKitModule::SpawnAgentChatTab(const FSpawnTabArgs& SpawnTabArgs)
{
	// Create the bridge UObject (once, reused across tab reopens)
	if (!WebUIBridgeInstance)
	{
		WebUIBridgeInstance = NewObject<UWebUIBridge>();
		WebUIBridgeInstance->AddToRoot(); // Prevent GC
	}

	// Resolve URL based on settings
	FString URL;
	const UACPSettings* Settings = UACPSettings::Get();

	if (Settings && Settings->bUseDevServer)
	{
		// Dev server override — always use localhost for live debugging
		URL = FString::Printf(TEXT("http://localhost:%d"), Settings->DevServerPort);
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUI: Using live dev server at %s"), *URL);
	}
	else
	{
		const EWebUISource UISource = Settings ? Settings->WebUISource : EWebUISource::Hosted;

		if (UISource == EWebUISource::Hosted)
		{
			URL = Settings ? Settings->HostedWebUIUrl : TEXT("https://ueinterface.neostack.dev/");
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUI: Loading hosted UI from %s"), *URL);
		}
		else
		{
			// Local: prefer static build, fall back to Vite dev server
			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AgentIntegrationKit"));
			if (Plugin.IsValid())
			{
				FString StaticBuildPath = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Source/WebUI/build/index.html"));
				FPaths::NormalizeFilename(StaticBuildPath);
				StaticBuildPath = FPaths::ConvertRelativePathToFull(StaticBuildPath);

				if (FPaths::FileExists(StaticBuildPath))
				{
					StaticBuildPath.ReplaceInline(TEXT("\\"), TEXT("/"));
					StaticBuildPath.ReplaceInline(TEXT(" "), TEXT("%20"));
					URL = FString::Printf(TEXT("file:///%s"), *StaticBuildPath);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUI: Loading local build from %s"), *URL);
				}
			}

			if (URL.IsEmpty())
			{
				URL = TEXT("http://localhost:5173");
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUI: Local build not found, using dev server at %s"), *URL);
			}
		}
	}

	// Create browser window via IWebBrowserSingleton (matches Bridge/Fab plugin pattern).
	// On macOS, this creates the native WKWebView. Without SetParentDockTab below,
	// the native view stays always-visible and covers other editor panels/popups.
	FCreateBrowserWindowSettings WindowSettings;
	WindowSettings.InitialURL = URL;
	WindowSettings.BrowserFrameRate = 60;
	WindowSettings.bShowErrorMessage = true;

	IWebBrowserSingleton* WebBrowserSingleton = IWebBrowserModule::Get().GetSingleton();
	WebUIBrowserWindow = WebBrowserSingleton ? WebBrowserSingleton->CreateBrowserWindow(WindowSettings) : nullptr;
	if (WebUIBrowserWindow.IsValid())
	{
		WebUIBrowserWindow->OnUnhandledKeyDown().BindLambda([this](const FKeyEvent&)
		{
			if (!FSlateApplication::IsInitialized())
			{
				return false;
			}

			const TSharedPtr<SWidget> BrowserWidget = WebUIBrowserWidget.Pin();
			return BrowserWidget.IsValid() && FSlateApplication::Get().HasFocusedDescendants(BrowserWidget.ToSharedRef());
		});
		WebUIBrowserWindow->OnUnhandledKeyUp().BindLambda([this](const FKeyEvent&)
		{
			if (!FSlateApplication::IsInitialized())
			{
				return false;
			}

			const TSharedPtr<SWidget> BrowserWidget = WebUIBrowserWidget.Pin();
			return BrowserWidget.IsValid() && FSlateApplication::Get().HasFocusedDescendants(BrowserWidget.ToSharedRef());
		});
	}

	TSharedPtr<SWebBrowser> Browser;

	TSharedRef<SDockTab> Tab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.OnTabClosed_Lambda([this](TSharedRef<SDockTab>)
		{
			UnbindWebUIInputMethodSystem();

			// Unlink browser from dock tab so native view is hidden
			if (WebUIBrowserWindow.IsValid())
			{
				WebUIBrowserWindow->OnUnhandledKeyDown().Unbind();
				WebUIBrowserWindow->OnUnhandledKeyUp().Unbind();
			}
#if ENGINE_MINOR_VERSION >= 7
			if (WebUIBrowserWindow.IsValid())
			{
				WebUIBrowserWindow->SetParentDockTab(nullptr);
			}
#endif
		})
		[
			SAssignNew(Browser, SWebBrowser, WebUIBrowserWindow)
				.ShowControls(false)
				.ShowAddressBar(false)
				.ShowErrorMessage(true)
				.SupportsTransparency(false)
				.OnBeforePopup(FOnBeforePopupDelegate::CreateLambda(
					[](FString PopupUrl, FString)
					{
						if (PopupUrl.IsEmpty())
						{
							return true;
						}

						FString ErrorString;
						FPlatformProcess::LaunchURL(*PopupUrl, TEXT(""), &ErrorString);
						if (!ErrorString.IsEmpty())
						{
							UE_LOG(LogAgentIntegrationKit, Warning,
								TEXT("WebUI: Failed to open popup URL '%s': %s"), *PopupUrl, *ErrorString);
						}

						// Block opening a second embedded browser window; use system browser instead.
						return true;
					}))
				.OnLoadCompleted(FSimpleDelegate::CreateLambda([this]()
					{
						const TWeakPtr<SWidget> WeakBrowser = WebUIBrowserWidget;
						AsyncTask(ENamedThreads::GameThread, [WeakBrowser]()
						{
							if (!FSlateApplication::IsInitialized())
							{
								return;
							}

							if (const TSharedPtr<SWidget> BrowserWidget = WeakBrowser.Pin())
							{
								FSlateApplication::Get().SetKeyboardFocus(BrowserWidget, EFocusCause::SetDirectly);
							}
						});
					}))
				.BrowserFrameRate(60)
				.OnConsoleMessage(FOnConsoleMessageDelegate::CreateLambda(
					[](const FString& Message, const FString& Source, int32 Line, EWebBrowserConsoleLogSeverity Severity)
					{
						const UACPSettings* Settings = UACPSettings::Get();
						const bool bLogAllBrowserConsole = Settings && Settings->bVerboseLogging;
						const TCHAR* SeverityText = TEXT("default");

						switch (Severity)
						{
						case EWebBrowserConsoleLogSeverity::Verbose: SeverityText = TEXT("verbose"); break;
						case EWebBrowserConsoleLogSeverity::Debug:   SeverityText = TEXT("debug"); break;
						case EWebBrowserConsoleLogSeverity::Info:    SeverityText = TEXT("info"); break;
						case EWebBrowserConsoleLogSeverity::Warning: SeverityText = TEXT("warning"); break;
						case EWebBrowserConsoleLogSeverity::Error:   SeverityText = TEXT("error"); break;
						case EWebBrowserConsoleLogSeverity::Fatal:   SeverityText = TEXT("fatal"); break;
						default: break;
						}

						const FString SafeSource = Source.IsEmpty() ? TEXT("<unknown>") : Source;
						const FString ConsoleLine = FString::Printf(
							TEXT("WebUI Console [%s] %s:%d %s"),
							SeverityText,
							*SafeSource,
							Line,
							*Message);

						switch (Severity)
						{
						case EWebBrowserConsoleLogSeverity::Warning:
							UE_LOG(LogAgentIntegrationKit, Warning, TEXT("%s"), *ConsoleLine);
							break;
						case EWebBrowserConsoleLogSeverity::Error:
						case EWebBrowserConsoleLogSeverity::Fatal:
							UE_LOG(LogAgentIntegrationKit, Error, TEXT("%s"), *ConsoleLine);
							break;
						default:
							if (bLogAllBrowserConsole)
							{
								UE_LOG(LogAgentIntegrationKit, Log, TEXT("%s"), *ConsoleLine);
							}
							break;
						}
					}))
		];

	// CRITICAL: Link browser to dock tab — on macOS this subscribes to tab
	// foregrounding events and toggles native WKWebView visibility so the
	// browser only shows when its tab is active (fixes z-ordering over other panels)
#if ENGINE_MINOR_VERSION >= 7
	if (WebUIBrowserWindow.IsValid())
	{
		WebUIBrowserWindow->SetParentDockTab(Tab);
	}
#endif

	// Fix z-ordering on tab drag/relocation (matches Bridge/Fab plugin workaround)
	WebUIBrowserWidget = Browser;
	BindWebUIInputMethodSystem(Browser);
	Tab->SetOnTabDraggedOverDockArea(
		FSimpleDelegate::CreateLambda([WeakBrowser = TWeakPtr<SWidget>(Browser)]()
		{
			if (TSharedPtr<SWidget> Pinned = WeakBrowser.Pin())
			{
				Pinned->Invalidate(EInvalidateWidgetReason::Layout);
			}
		})
	);
	Tab->SetOnTabRelocated(
		FSimpleDelegate::CreateLambda([WeakBrowser = TWeakPtr<SWidget>(Browser)]()
		{
			if (TSharedPtr<SWidget> Pinned = WeakBrowser.Pin())
			{
				Pinned->Invalidate(EInvalidateWidgetReason::Layout);
			}
		})
	);

	// Bind the bridge so JS can access it as window.ue.bridge.*
	if (Browser.IsValid() && WebUIBridgeInstance)
	{
		Browser->BindUObject(TEXT("bridge"), WebUIBridgeInstance, true);
		UE_LOG(LogAgentIntegrationKit, Log, TEXT("WebUI: Bridge bound to browser as window.ue.bridge"));
	}

	return Tab;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FAgentIntegrationKitModule, AgentIntegrationKit)
