// Copyright 2025 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/NeoStackToolBase.h"

/**
 * Tool for exploring and searching project files and assets
 * - List directories (files, folders, or both)
 * - Search code with regex/text
 * - Find Blueprints by criteria (parent, component, interface, etc.)
 */
class AGENTINTEGRATIONKIT_API FExploreTool : public FNeoStackToolBase
{
public:
	virtual FString GetName() const override { return TEXT("explore"); }
	virtual FString GetDescription() const override
	{
		return TEXT("Explore and search project files, code, and assets");
	}

	virtual TSharedPtr<FJsonObject> GetInputSchema() const override;
	virtual FToolResult Execute(const TSharedPtr<FJsonObject>& Args) override;

private:
	/** Filter options for Blueprint searches */
	struct FBlueprintFilter
	{
		FString Parent;
		FString Component;
		FString Interface;
		FString References;
		FString ReferencedBy;
	};

	/** Explore filesystem (files/folders) */
	FToolResult ExploreFiles(const FString& Path, const FString& Pattern, const FString& Query,
		const FString& Type, bool bRecursive, int32 Context, int32 Offset, int32 Limit);

	/** Explore UE assets */
	FToolResult ExploreAssets(const FString& Path, const FString& Pattern, const FString& Query,
		const FString& Type, const FBlueprintFilter& Filter, int32 Offset, int32 Limit);

	/** List directory contents */
	FString ListDirectory(const FString& FullPath, const FString& Pattern, const FString& Type,
		bool bRecursive, int32 Offset, int32 Limit);

	/** Search code in files */
	FString SearchCode(const FString& FullPath, const FString& Pattern, const FString& Query,
		bool bRecursive, int32 Context, int32 Offset, int32 Limit);

	/** List assets in path */
	FString ListAssets(const FString& AssetPath, const FString& Pattern, const FString& Type, int32 Offset, int32 Limit);

	/** Search Blueprints by criteria */
	FString SearchBlueprints(const FString& AssetPath, const FString& Pattern, const FString& Query,
		const FBlueprintFilter& Filter, int32 Offset, int32 Limit);

	/** Check if Blueprint matches filter */
	bool MatchesFilter(class UBlueprint* Blueprint, const FString& Query, const FBlueprintFilter& Filter);

	/** Check if Blueprint has component */
	bool HasComponent(class UBlueprint* Blueprint, const FString& ComponentName);

	/** Check if Blueprint implements interface */
	bool HasInterface(class UBlueprint* Blueprint, const FString& InterfaceName);

	/** Check if Blueprint references asset */
	bool ReferencesAsset(class UBlueprint* Blueprint, const FString& AssetName);

	/** Check if text matches query (case-insensitive) */
	bool MatchesQuery(const FString& Text, const FString& Query);

	/** Match glob pattern */
	bool MatchesPattern(const FString& Name, const FString& Pattern);

	/** Load .gitignore patterns from project root */
	void LoadGitIgnorePatterns();

	/** Check if path should be ignored based on gitignore */
	bool IsIgnoredByGitignore(const FString& RelativePath, bool bIsDirectory);

	/** Cached gitignore patterns */
	TArray<FString> GitIgnorePatterns;
	bool bGitIgnoreLoaded = false;
};
