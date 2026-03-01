// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/GenerateImageTool.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/AssetImportUtils.h"
#include "ACPSettings.h"
#include "Json.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Async/Async.h"
#include "Misc/FileHelper.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

// ─────────────────────────────────────────────────────────────────────────────
// GetInputSchema - 定义 MCP 工具参数 schema
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FGenerateImageTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	// prompt (required)
	TSharedPtr<FJsonObject> PromptProp = MakeShared<FJsonObject>();
	PromptProp->SetStringField(TEXT("type"), TEXT("string"));
	PromptProp->SetStringField(TEXT("description"), TEXT("Detailed description of the image to generate. Be specific about style, composition, colors, and subject matter."));
	Properties->SetObjectField(TEXT("prompt"), PromptProp);

	// model (optional)
	TSharedPtr<FJsonObject> ModelProp = MakeShared<FJsonObject>();
	ModelProp->SetStringField(TEXT("type"), TEXT("string"));
	ModelProp->SetStringField(TEXT("description"), TEXT("Image generation model to use. Default: gpt-image-1"));
	Properties->SetObjectField(TEXT("model"), ModelProp);

	// size (optional)
	TSharedPtr<FJsonObject> SizeProp = MakeShared<FJsonObject>();
	SizeProp->SetStringField(TEXT("type"), TEXT("string"));
	SizeProp->SetStringField(TEXT("description"), TEXT("Image size. Options: 1024x1024 (square, default), 1536x1024 (landscape), 1024x1536 (portrait), auto"));
	Properties->SetObjectField(TEXT("size"), SizeProp);

	// asset_path (optional)
	TSharedPtr<FJsonObject> AssetPathProp = MakeShared<FJsonObject>();
	AssetPathProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetPathProp->SetStringField(TEXT("description"), TEXT("Unreal asset path where the texture will be imported. Default: /Game/GeneratedImages"));
	Properties->SetObjectField(TEXT("asset_path"), AssetPathProp);

	// asset_name (optional)
	TSharedPtr<FJsonObject> AssetNameProp = MakeShared<FJsonObject>();
	AssetNameProp->SetStringField(TEXT("type"), TEXT("string"));
	AssetNameProp->SetStringField(TEXT("description"), TEXT("Name for the imported texture asset. Auto-generated from prompt if not provided."));
	Properties->SetObjectField(TEXT("asset_name"), AssetNameProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	// Required fields
	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("prompt")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute - MCP 工具入口，参数校验后发起异步请求并立即返回
// ─────────────────────────────────────────────────────────────────────────────

FToolResult FGenerateImageTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	// Parse parameters
	FString Prompt;
	if (!Args->TryGetStringField(TEXT("prompt"), Prompt) || Prompt.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Missing required parameter: prompt"));
	}

	// Get settings
	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings)
	{
		return FToolResult::Fail(TEXT("Failed to load plugin settings"));
	}

	// Resolve API key: prefer dedicated image key, fall back to OpenRouter key
	const FString ApiKey = Settings->GetImageGenerationApiKey();
	if (ApiKey.IsEmpty())
	{
		return FToolResult::Fail(TEXT("Image generation API key not configured. Set it in Project Settings > Plugins > Agent Integration Kit > AI Generation > Images"));
	}

	// Optional parameters
	FString Model;
	if (!Args->TryGetStringField(TEXT("model"), Model) || Model.IsEmpty())
	{
		Model = Settings->ImageGenerationDefaultModel.IsEmpty() ? TEXT("gpt-image-1") : Settings->ImageGenerationDefaultModel;
	}

	FString Size;
	if (!Args->TryGetStringField(TEXT("size"), Size) || Size.IsEmpty())
	{
		Size = TEXT("1024x1024");
	}

	FString AssetPath;
	if (!Args->TryGetStringField(TEXT("asset_path"), AssetPath) || AssetPath.IsEmpty())
	{
		AssetPath = TEXT("/Game/GeneratedImages");
	}

	FString AssetName;
	Args->TryGetStringField(TEXT("asset_name"), AssetName);

	// 发起异步请求——不阻塞 Game Thread
	FireAndForgetGenerateImage(Prompt, Model, Size, AssetPath, AssetName);

	// 立即返回确认消息给 MCP 客户端
	return FToolResult::Ok(FString::Printf(
		TEXT("Image generation request submitted.\n\n"
		     "Prompt: %s\nModel: %s\nSize: %s\nTarget: %s/%s\n\n"
		     "The request is running in the background. "
		     "An editor notification will appear when the image is ready."),
		*Prompt, *Model, *Size, *AssetPath,
		AssetName.IsEmpty() ? TEXT("(auto-named)") : *AssetName));
}

// ─────────────────────────────────────────────────────────────────────────────
// FireAndForgetGenerateImage - 发起 HTTP 请求，绑定回调，立即返回
// ─────────────────────────────────────────────────────────────────────────────

void FGenerateImageTool::FireAndForgetGenerateImage(
	const FString& Prompt, const FString& Model, const FString& Size,
	const FString& AssetPath, const FString& AssetName)
{
	UACPSettings* Settings = UACPSettings::Get();

	// Resolve API key
	const FString ApiKey = Settings->GetImageGenerationApiKey();

	// Resolve base URL
	const FString BaseUrl = Settings->GetImageGenerationBaseUrl();

	// Build request body — OpenAI Images Generations API format
	TSharedRef<FJsonObject> RequestBody = MakeShared<FJsonObject>();
	RequestBody->SetStringField(TEXT("model"), Model);
	RequestBody->SetStringField(TEXT("prompt"), Prompt);
	RequestBody->SetStringField(TEXT("size"), Size);
	RequestBody->SetNumberField(TEXT("n"), 1);
	// 注意：不传 response_format，部分网关（如内部 API）不支持该参数。
	// 响应可能是 b64_json 或 url 格式，ExtractImageFromResponse 会同时处理。

	FString RequestBodyStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&RequestBodyStr);
	FJsonSerializer::Serialize(RequestBody, Writer);

	// Create HTTP request
	FString RequestUrl = FString::Printf(TEXT("%s/images/generations"), *BaseUrl);
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(RequestUrl);
	Request->SetVerb(TEXT("POST"));
	Request->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
	Request->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *ApiKey));
	Request->SetContentAsString(RequestBodyStr);
	Request->SetTimeout(300.0f); // 5 分钟总超时（生图通常需要 30-120 秒）
	Request->SetActivityTimeout(300.0f); // 5 分钟活动超时——生图 API 在计算期间不会返回任何数据，必须覆盖引擎默认的 30 秒活动超时

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Image generation request fired (async) -> %s"), *RequestUrl);

	// 在 Game Thread 弹一个"正在生成"的通知（当前可能不在 Game Thread，必须调度）
	FString NotifyPrompt = Prompt.Left(60);
	AsyncTask(ENamedThreads::GameThread, [NotifyPrompt]()
	{
		ShowEditorNotification(
			FText::Format(FText::FromString(TEXT("Generating image...\n{0}")),
				FText::FromString(NotifyPrompt)),
			true);
	});

	// 绑定回调 —— 按值捕获所有需要的字符串，确保生命周期安全
	Request->OnProcessRequestComplete().BindLambda(
		[this, Prompt, Model, Size, AssetPath, AssetName]
		(FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bConnectedSuccessfully)
		{
			int32 ResponseCode = 0;
			FString ResponseContent;

			if (bConnectedSuccessfully && Response.IsValid())
			{
				ResponseCode = Response->GetResponseCode();
				ResponseContent = Response->GetContentAsString();
			}

			// 回调可能在任意线程，OnImageGenerated 内部会调度到 Game Thread
			OnImageGenerated(
				bConnectedSuccessfully && Response.IsValid(),
				ResponseCode, ResponseContent,
				Prompt, Model, Size, AssetPath, AssetName);
		});

	// 发射！不等待。
	Request->ProcessRequest();
}

// ─────────────────────────────────────────────────────────────────────────────
// OnImageGenerated - HTTP 完成回调，解析响应 → 导入资产 → 弹通知
// ─────────────────────────────────────────────────────────────────────────────

void FGenerateImageTool::OnImageGenerated(
	bool bHttpSuccess, int32 ResponseCode, const FString& ResponseContent,
	const FString& Prompt, const FString& Model, const FString& Size,
	const FString& AssetPath, const FString& AssetName)
{
	// ── 1. HTTP 连接失败 ──
	if (!bHttpSuccess)
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Image generation HTTP request failed (bConnectedSuccessfully=false). This is likely caused by activity timeout - the API server did not respond within the timeout period."));
		AsyncTask(ENamedThreads::GameThread, []()
		{
			ShowEditorNotification(FText::FromString(TEXT("Image generation failed: API server did not respond within timeout. Check your network or try again later.")), false);
		});
		return;
	}

	// ── 2. HTTP 错误码 ──
	if (ResponseCode < 200 || ResponseCode >= 300)
	{
		// 尝试从 JSON 中提取错误消息
		FString ErrorMsg;
		TSharedPtr<FJsonObject> ErrorJson;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
		if (FJsonSerializer::Deserialize(Reader, ErrorJson) && ErrorJson.IsValid())
		{
			const TSharedPtr<FJsonObject>* ErrorObj;
			if (ErrorJson->TryGetObjectField(TEXT("error"), ErrorObj))
			{
				(*ErrorObj)->TryGetStringField(TEXT("message"), ErrorMsg);
			}
		}
		if (ErrorMsg.IsEmpty())
		{
			ErrorMsg = ResponseContent.Left(200);
		}

		UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Image generation API error (%d): %s"), ResponseCode, *ErrorMsg);
		AsyncTask(ENamedThreads::GameThread, [ResponseCode, ErrorMsg]()
		{
			ShowEditorNotification(
				FText::Format(FText::FromString(TEXT("Image generation failed ({0})\n{1}")),
					FText::AsNumber(ResponseCode),
					FText::FromString(ErrorMsg.Left(100))),
				false);
		});
		return;
	}

	// ── 3. 解析 JSON 响应 ──
	TSharedPtr<FJsonObject> ResponseJson;
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseContent);
		if (!FJsonSerializer::Deserialize(Reader, ResponseJson) || !ResponseJson.IsValid())
		{
			UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Failed to parse image generation API response as JSON"));
			AsyncTask(ENamedThreads::GameThread, []()
			{
				ShowEditorNotification(FText::FromString(TEXT("Image generation failed: Could not parse API response.")), false);
			});
			return;
		}
	}

	// ── 4. 提取 base64 / URL 图片数据 ──
	FString Base64Data;
	FString ExtractError;
	if (!ExtractImageFromResponse(ResponseJson, Base64Data, ExtractError))
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] %s"), *ExtractError);
		AsyncTask(ENamedThreads::GameThread, [ExtractError]()
		{
			ShowEditorNotification(
				FText::Format(FText::FromString(TEXT("Image generation failed\n{0}")),
					FText::FromString(ExtractError.Left(100))),
				false);
		});
		return;
	}

	UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Image generated successfully (%d chars), scheduling import on Game Thread..."), Base64Data.Len());

	// ── 5. 在 Game Thread 上执行资产导入 + 弹成功通知 ──
	// 资产导入（UFactory / EditorAssetLibrary 等）必须在 Game Thread 上执行
	AsyncTask(ENamedThreads::GameThread,
		[Base64Data = MoveTemp(Base64Data), Prompt, Model, Size, AssetPath, AssetName]()
		{
			FString SaveError;
			FString TempFilePath;

			if (Base64Data.StartsWith(TEXT("URL:")))
			{
				// URL 模式：需要下载图片（同步，因为已经在 Game Thread 的 AsyncTask 里了）
				FString ImageUrl = Base64Data.Mid(4);
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Image returned as URL, downloading: %s"), *ImageUrl);

				// 用同步 HTTP 下载
				TSharedRef<IHttpRequest, ESPMode::ThreadSafe> DlReq = FHttpModule::Get().CreateRequest();
				DlReq->SetURL(ImageUrl);
				DlReq->SetVerb(TEXT("GET"));
				DlReq->SetTimeout(60.0f);

				// 使用 FEvent 做简易同步等待
				FEvent* DlEvent = FPlatformProcess::GetSynchEventFromPool(false);
				TArray<uint8> DlBytes;
				bool bDlSuccess = false;

				DlReq->OnProcessRequestComplete().BindLambda(
					[&DlBytes, &bDlSuccess, DlEvent](FHttpRequestPtr, FHttpResponsePtr Resp, bool bOk)
					{
						if (bOk && Resp.IsValid() && Resp->GetResponseCode() == 200)
						{
							DlBytes = Resp->GetContent();
							bDlSuccess = true;
						}
						DlEvent->Trigger();
					});
				DlReq->ProcessRequest();
				DlEvent->Wait(60000); // 60 秒超时
				FPlatformProcess::ReturnSynchEventToPool(DlEvent);

				if (!bDlSuccess || DlBytes.Num() == 0)
				{
					UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Failed to download generated image from URL"));
					ShowEditorNotification(FText::FromString(TEXT("Image download failed.")), false);
					return;
				}

				// 保存下载的字节到临时文件
				TempFilePath = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), TEXT("AIK_"), TEXT(".png"));
				if (!FFileHelper::SaveArrayToFile(DlBytes, *TempFilePath))
				{
					UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Failed to save downloaded image to temp file"));
					ShowEditorNotification(FText::FromString(TEXT("Image generation failed: Could not save temp file.")), false);
					return;
				}
			}
			else
			{
				// Base64 模式：直接解码保存
				TempFilePath = AssetImportUtils::SaveBase64ToTempFile(Base64Data, TEXT("png"), SaveError);
			}

			if (TempFilePath.IsEmpty())
			{
				UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Failed to save temp file: %s"), *SaveError);
				ShowEditorNotification(
					FText::Format(FText::FromString(TEXT("Image import failed\n{0}")), FText::FromString(SaveError.Left(100))),
					false);
				return;
			}

			// 生成资产名称
			FString FinalAssetName = AssetName;
			if (FinalAssetName.IsEmpty())
			{
				FString CleanPrompt = Prompt.Left(50);
				FinalAssetName = AssetImportUtils::SanitizeAssetName(CleanPrompt);
			}

			// 导入纹理
			FString ImportError;
			UTexture2D* ImportedTexture = AssetImportUtils::ImportTexture(TempFilePath, AssetPath, FinalAssetName, ImportError);

			// 清理临时文件
			IFileManager::Get().Delete(*TempFilePath);

			if (!ImportedTexture)
			{
				UE_LOG(LogAgentIntegrationKit, Error, TEXT("[AIK] Texture import failed: %s"), *ImportError);
				ShowEditorNotification(
					FText::Format(FText::FromString(TEXT("Image import failed\n{0}")), FText::FromString(ImportError.Left(100))),
					false);
				return;
			}

			FString FullAssetPath = ImportedTexture->GetPathName();
			UE_LOG(LogAgentIntegrationKit, Log, TEXT("[AIK] Image imported successfully: %s"), *FullAssetPath);

			ShowEditorNotification(
				FText::Format(FText::FromString(TEXT("Image generation complete!\n{0}\nModel: {1}  Size: {2}")),
					FText::FromString(FullAssetPath),
					FText::FromString(Model),
					FText::FromString(Size)),
				true);
		});
}

// ─────────────────────────────────────────────────────────────────────────────
// ExtractImageFromResponse - 从 OpenAI Images API 响应中提取图片数据
// ─────────────────────────────────────────────────────────────────────────────

bool FGenerateImageTool::ExtractImageFromResponse(const TSharedPtr<FJsonObject>& Response, FString& OutBase64Data, FString& OutError)
{
	if (!Response.IsValid())
	{
		OutError = TEXT("Invalid response object");
		return false;
	}

	// OpenAI Images API response format: { "data": [{ "b64_json": "..." }] }
	const TArray<TSharedPtr<FJsonValue>>* DataArray;
	if (!Response->TryGetArrayField(TEXT("data"), DataArray) || DataArray->Num() == 0)
	{
		// Check for error object in response
		const TSharedPtr<FJsonObject>* ErrorObj;
		if (Response->TryGetObjectField(TEXT("error"), ErrorObj))
		{
			FString Message;
			if ((*ErrorObj)->TryGetStringField(TEXT("message"), Message))
			{
				OutError = FString::Printf(TEXT("Image generation failed: %s"), *Message);
				return false;
			}
		}
		OutError = TEXT("No data array in response. The API may have returned an unexpected format.");
		return false;
	}

	const TSharedPtr<FJsonObject>* DataObj;
	if (!(*DataArray)[0]->TryGetObject(DataObj))
	{
		OutError = TEXT("Invalid data object in response");
		return false;
	}

	// 优先尝试 b64_json，其次尝试 url
	if ((*DataObj)->TryGetStringField(TEXT("b64_json"), OutBase64Data) && !OutBase64Data.IsEmpty())
	{
		// 拿到了 base64 数据，直接返回
	}
	else
	{
		// 没有 b64_json，尝试获取 url
		FString ImageUrl;
		if ((*DataObj)->TryGetStringField(TEXT("url"), ImageUrl) && !ImageUrl.IsEmpty())
		{
			// 标记为 URL 格式，由调用方下载
			OutBase64Data = TEXT("URL:") + ImageUrl;
		}
		else
		{
			OutError = TEXT("Response contains neither 'b64_json' nor 'url' field.");
			return false;
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] Successfully extracted image data from response (%d chars)"), OutBase64Data.Len());
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// ShowEditorNotification - 编辑器右下角 toast 通知
// ─────────────────────────────────────────────────────────────────────────────

void FGenerateImageTool::ShowEditorNotification(const FText& Message, bool bSuccess)
{
	// 必须在 Game Thread 上调用
	check(IsInGameThread());

	FNotificationInfo Info(Message);
	Info.ExpireDuration = bSuccess ? 15.0f : 20.0f;
	Info.bUseSuccessFailIcons = true;
	Info.bFireAndForget = true;

	TSharedPtr<SNotificationItem> Notification = FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
}
