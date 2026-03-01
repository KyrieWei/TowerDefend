// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Tool for generating images via OpenAI-compatible Images API.
 * Uses the /v1/images/generations endpoint (e.g. gpt-image-1).
 * Default endpoint: http://api-skynetyu.woa.com/v1 (configurable in Project Settings).
 *
 * 【异步设计】Execute() 发起 HTTP 请求后立即返回，不阻塞 Game Thread。
 * 图片生成完成后通过右下角编辑器通知 (FSlateNotificationManager) 告知用户结果。
 */
class AGENTINTEGRATIONKIT_API FGenerateImageTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("generate_image"); }

	virtual FString GetDescription() const override
	{
		return TEXT("Generate an image from a text prompt using an OpenAI-compatible Images API (e.g. gpt-image-1). "
			"The generated image is automatically imported as a Texture2D asset in the project. "
			"Supports various sizes. The request is async - you'll get a confirmation that "
			"generation started, and a notification will appear when it completes.");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;

	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/**
	 * 发起异步 HTTP 图片生成请求，立即返回（不阻塞）。
	 * 生成完成后在 Game Thread 上导入资产并弹出编辑器通知。
	 */
	void FireAndForgetGenerateImage(const FString& Prompt, const FString& Model, const FString& Size, const FString& AssetPath, const FString& AssetName);

	/**
	 * HTTP 请求完成后的回调（可能在任意线程），负责解析响应、导入资产、弹出通知。
	 * 资产导入和 Slate 通知必须在 Game Thread 上执行，内部会自行调度。
	 */
	void OnImageGenerated(bool bHttpSuccess, int32 ResponseCode, const FString& ResponseContent,
		const FString& Prompt, const FString& Model, const FString& Size,
		const FString& AssetPath, const FString& AssetName);

	/**
	 * Extract image data from OpenAI Images API response.
	 * Supports both b64_json and url response formats.
	 * Response format: { "data": [{ "b64_json": "..." }] } or { "data": [{ "url": "..." }] }
	 */
	bool ExtractImageFromResponse(const TSharedPtr<FJsonObject>& Response, FString& OutBase64Data, FString& OutError);

	/**
	 * 在 Game Thread 上弹出编辑器右下角通知。
	 */
	static void ShowEditorNotification(const FText& Message, bool bSuccess);
};
