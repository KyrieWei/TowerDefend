// Copyright 2025 Betide Studio. All Rights Reserved.

#include "Tools/ExploreTool.h"
#include "ACPSettings.h"
#include "AgentIntegrationKitModule.h"
#include "Tools/NeoStackToolUtils.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

TSharedPtr<FJsonObject> FExploreTool::GetInputSchema() const
{
	TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

	TSharedPtr<FJsonObject> PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Directory path to explore (e.g., /Game/Blueprints or Source/MyProject)"));
	Properties->SetObjectField(TEXT("path"), PathProp);

	TSharedPtr<FJsonObject> PatternProp = MakeShared<FJsonObject>();
	PatternProp->SetStringField(TEXT("type"), TEXT("string"));
	PatternProp->SetStringField(TEXT("description"), TEXT("Glob pattern to filter results (e.g., *.cpp, BP_*)"));
	Properties->SetObjectField(TEXT("pattern"), PatternProp);

	TSharedPtr<FJsonObject> QueryProp = MakeShared<FJsonObject>();
	QueryProp->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetStringField(TEXT("description"), TEXT("Text or regex to search in file contents or Blueprint properties"));
	Properties->SetObjectField(TEXT("query"), QueryProp);

	TSharedPtr<FJsonObject> TypeProp = MakeShared<FJsonObject>();
	TypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeProp->SetStringField(TEXT("description"), TEXT("Filter by type: files, folders, both, or asset class name"));
	Properties->SetObjectField(TEXT("type"), TypeProp);

	TSharedPtr<FJsonObject> OffsetProp = MakeShared<FJsonObject>();
	OffsetProp->SetStringField(TEXT("type"), TEXT("integer"));
	OffsetProp->SetStringField(TEXT("description"), TEXT("Results offset for pagination"));
	Properties->SetObjectField(TEXT("offset"), OffsetProp);

	TSharedPtr<FJsonObject> LimitProp = MakeShared<FJsonObject>();
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"), TEXT("Max results to return (default: 50)"));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	TSharedPtr<FJsonObject> ContextProp = MakeShared<FJsonObject>();
	ContextProp->SetStringField(TEXT("type"), TEXT("integer"));
	ContextProp->SetStringField(TEXT("description"), TEXT("Lines of context around search matches"));
	Properties->SetObjectField(TEXT("context"), ContextProp);

	TSharedPtr<FJsonObject> RecursiveProp = MakeShared<FJsonObject>();
	RecursiveProp->SetStringField(TEXT("type"), TEXT("boolean"));
	RecursiveProp->SetStringField(TEXT("description"), TEXT("Search subdirectories recursively (default: true)"));
	Properties->SetObjectField(TEXT("recursive"), RecursiveProp);

	TSharedPtr<FJsonObject> FilterProp = MakeShared<FJsonObject>();
	FilterProp->SetStringField(TEXT("type"), TEXT("object"));
	FilterProp->SetStringField(TEXT("description"), TEXT("Blueprint filter criteria"));
	TSharedPtr<FJsonObject> FilterProps = MakeShared<FJsonObject>();
	TSharedPtr<FJsonObject> ParentFilter = MakeShared<FJsonObject>();
	ParentFilter->SetStringField(TEXT("type"), TEXT("string"));
	ParentFilter->SetStringField(TEXT("description"), TEXT("Filter by parent class"));
	FilterProps->SetObjectField(TEXT("parent"), ParentFilter);
	TSharedPtr<FJsonObject> ComponentFilter = MakeShared<FJsonObject>();
	ComponentFilter->SetStringField(TEXT("type"), TEXT("string"));
	ComponentFilter->SetStringField(TEXT("description"), TEXT("Filter by component type"));
	FilterProps->SetObjectField(TEXT("component"), ComponentFilter);
	TSharedPtr<FJsonObject> InterfaceFilter = MakeShared<FJsonObject>();
	InterfaceFilter->SetStringField(TEXT("type"), TEXT("string"));
	InterfaceFilter->SetStringField(TEXT("description"), TEXT("Filter by implemented interface"));
	FilterProps->SetObjectField(TEXT("interface"), InterfaceFilter);
	TSharedPtr<FJsonObject> ReferencesFilter = MakeShared<FJsonObject>();
	ReferencesFilter->SetStringField(TEXT("type"), TEXT("string"));
	ReferencesFilter->SetStringField(TEXT("description"), TEXT("Filter by referenced asset"));
	FilterProps->SetObjectField(TEXT("references"), ReferencesFilter);
	TSharedPtr<FJsonObject> ReferencedByFilter = MakeShared<FJsonObject>();
	ReferencedByFilter->SetStringField(TEXT("type"), TEXT("string"));
	ReferencedByFilter->SetStringField(TEXT("description"), TEXT("Filter by assets that reference this"));
	FilterProps->SetObjectField(TEXT("referenced_by"), ReferencedByFilter);
	FilterProp->SetObjectField(TEXT("properties"), FilterProps);
	Properties->SetObjectField(TEXT("filter"), FilterProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	return Schema;
}

FToolResult FExploreTool::Execute(const TSharedPtr<FJsonObject>& Args)
{
	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	if (!Args.IsValid())
	{
		return FToolResult::Fail(TEXT("Invalid arguments"));
	}

	FString Path, Pattern, Query, Type;
	int32 Offset = 0;
	int32 Limit = 50;
	int32 Context = 0;
	bool bRecursive = true;
	FBlueprintFilter Filter;

	Args->TryGetStringField(TEXT("path"), Path);
	Args->TryGetStringField(TEXT("pattern"), Pattern);
	Args->TryGetStringField(TEXT("query"), Query);
	Args->TryGetStringField(TEXT("type"), Type);
	Args->TryGetNumberField(TEXT("offset"), Offset);
	Args->TryGetNumberField(TEXT("limit"), Limit);
	Args->TryGetNumberField(TEXT("context"), Context);
	Args->TryGetBoolField(TEXT("recursive"), bRecursive);

	// Parse filter object
	const TSharedPtr<FJsonObject>* FilterObj;
	if (Args->TryGetObjectField(TEXT("filter"), FilterObj))
	{
		(*FilterObj)->TryGetStringField(TEXT("parent"), Filter.Parent);
		(*FilterObj)->TryGetStringField(TEXT("component"), Filter.Component);
		(*FilterObj)->TryGetStringField(TEXT("interface"), Filter.Interface);
		(*FilterObj)->TryGetStringField(TEXT("references"), Filter.References);
		(*FilterObj)->TryGetStringField(TEXT("referenced_by"), Filter.ReferencedBy);
	}

	// Default type
	if (Type.IsEmpty())
	{
		Type = TEXT("all");
	}

	// Clamp values
	Offset = FMath::Max(0, Offset);
	Limit = FMath::Clamp(Limit, 1, 200);
	Context = FMath::Clamp(Context, 0, 10);

	// Route based on path and type
	bool bIsAssetSearch = Path.StartsWith(TEXT("/Game")) ||
		Type.Equals(TEXT("blueprints"), ESearchCase::IgnoreCase) ||
		Type.Equals(TEXT("materials"), ESearchCase::IgnoreCase) ||
		Type.Equals(TEXT("textures"), ESearchCase::IgnoreCase) ||
		Type.Equals(TEXT("assets"), ESearchCase::IgnoreCase);

	if (bIsAssetSearch)
	{
		return ExploreAssets(Path, Pattern, Query, Type, Filter, Offset, Limit);
	}
	else
	{
		return ExploreFiles(Path, Pattern, Query, Type, bRecursive, Context, Offset, Limit);
	}
}

FToolResult FExploreTool::ExploreFiles(const FString& Path, const FString& Pattern, const FString& Query,
	const FString& Type, bool bRecursive, int32 Context, int32 Offset, int32 Limit)
{
	FString FullPath = NeoStackToolUtils::BuildFilePath(TEXT(""), Path);

	if (!FPaths::DirectoryExists(FullPath))
	{
		return FToolResult::Fail(FString::Printf(TEXT("Directory not found: %s"), *FullPath));
	}

	// If query is provided, search code
	if (!Query.IsEmpty())
	{
		return FToolResult::Ok(SearchCode(FullPath, Pattern, Query, bRecursive, Context, Offset, Limit));
	}

	// Otherwise list directory
	return FToolResult::Ok(ListDirectory(FullPath, Pattern, Type, bRecursive, Offset, Limit));
}

FToolResult FExploreTool::ExploreAssets(const FString& Path, const FString& Pattern, const FString& Query,
	const FString& Type, const FBlueprintFilter& Filter, int32 Offset, int32 Limit)
{
	FString AssetPath = Path.IsEmpty() ? TEXT("/Game") : Path;
	if (!AssetPath.StartsWith(TEXT("/Game")))
	{
		AssetPath = FString::Printf(TEXT("/Game/%s"), *AssetPath);
	}

	// If searching blueprints with criteria
	if (Type.Equals(TEXT("blueprints"), ESearchCase::IgnoreCase) ||
		!Filter.Parent.IsEmpty() || !Filter.Component.IsEmpty() ||
		!Filter.Interface.IsEmpty() || !Filter.References.IsEmpty() ||
		!Filter.ReferencedBy.IsEmpty())
	{
		return FToolResult::Ok(SearchBlueprints(AssetPath, Pattern, Query, Filter, Offset, Limit));
	}

	// Otherwise list assets
	return FToolResult::Ok(ListAssets(AssetPath, Pattern, Type, Offset, Limit));
}

FString FExploreTool::ListDirectory(const FString& FullPath, const FString& Pattern, const FString& Type,
	bool bRecursive, int32 Offset, int32 Limit)
{
	TArray<FString> Folders;
	TArray<FString> Files;

	IFileManager& FileManager = IFileManager::Get();

	bool bIncludeFolders = Type.Equals(TEXT("all"), ESearchCase::IgnoreCase) ||
		Type.Equals(TEXT("folders"), ESearchCase::IgnoreCase);
	bool bIncludeFiles = Type.Equals(TEXT("all"), ESearchCase::IgnoreCase) ||
		Type.Equals(TEXT("files"), ESearchCase::IgnoreCase) ||
		Type.Equals(TEXT("code"), ESearchCase::IgnoreCase);

	// Visitor to collect files and folders
	class FFileVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		TArray<FString>& Folders;
		TArray<FString>& Files;
		FString BasePath;
		FString ProjectDir;
		FString Pattern;
		bool bIncludeFolders;
		bool bIncludeFiles;
		FExploreTool* Tool;

		FFileVisitor(TArray<FString>& InFolders, TArray<FString>& InFiles, const FString& InBasePath,
			const FString& InPattern, bool bInFolders, bool bInFiles, FExploreTool* InTool)
			: Folders(InFolders), Files(InFiles), BasePath(InBasePath), Pattern(InPattern),
			bIncludeFolders(bInFolders), bIncludeFiles(bInFiles), Tool(InTool)
		{
			ProjectDir = UACPSettings::GetWorkingDirectory();
		}

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			FString FullName = FilenameOrDirectory;
			FString Name = FPaths::GetCleanFilename(FullName);

			// Skip hidden files/folders (except .gitignore itself)
			if (Name.StartsWith(TEXT(".")) && !Name.Equals(TEXT(".gitignore"))) return true;

			// Get path relative to project for gitignore matching
			FString RelToProject = FullName;
			FPaths::MakePathRelativeTo(RelToProject, *ProjectDir);

			// Check gitignore
			if (Tool->IsIgnoredByGitignore(RelToProject, bIsDirectory))
			{
				return true;
			}

			// Apply pattern filter
			if (!Pattern.IsEmpty() && !Tool->MatchesPattern(Name, Pattern))
			{
				return true;
			}

			// Get relative path for output
			FString RelPath = FullName;
			FPaths::MakePathRelativeTo(RelPath, *BasePath);

			if (bIsDirectory && bIncludeFolders)
			{
				Folders.Add(RelPath);
			}
			else if (!bIsDirectory && bIncludeFiles)
			{
				Files.Add(RelPath);
			}

			return true;
		}
	};

	FFileVisitor Visitor(Folders, Files, FullPath + TEXT("/"), Pattern, bIncludeFolders, bIncludeFiles, this);

	if (bRecursive)
	{
		FileManager.IterateDirectoryRecursively(*FullPath, Visitor);
	}
	else
	{
		FileManager.IterateDirectory(*FullPath, Visitor);
	}

	// Sort
	Folders.Sort();
	Files.Sort();

	// Build output
	int32 TotalFolders = Folders.Num();
	int32 TotalFiles = Files.Num();
	int32 Total = TotalFolders + TotalFiles;

	FString RelPath = FullPath;
	FPaths::MakePathRelativeTo(RelPath, *UACPSettings::GetWorkingDirectory());

	FString Output = FString::Printf(TEXT("# DIR %s folders=%d files=%d\n"), *RelPath, TotalFolders, TotalFiles);

	// Combine and paginate
	TArray<TPair<FString, bool>> AllItems; // path, isFolder
	for (const FString& F : Folders) AllItems.Add(TPair<FString, bool>(F, true));
	for (const FString& F : Files) AllItems.Add(TPair<FString, bool>(F, false));

	int32 StartIdx = Offset;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const auto& Item = AllItems[i];
		if (Item.Value)
		{
			Output += FString::Printf(TEXT("D\t%s\n"), *Item.Key);
		}
		else
		{
			Output += FString::Printf(TEXT("F\t%s\n"), *Item.Key);
		}
	}

	if (EndIdx < Total)
	{
		Output += FString::Printf(TEXT("# MORE offset=%d remaining=%d\n"), EndIdx, Total - EndIdx);
	}

	return Output;
}

FString FExploreTool::SearchCode(const FString& FullPath, const FString& Pattern, const FString& Query,
	bool bRecursive, int32 Context, int32 Offset, int32 Limit)
{
	TArray<FString> Files;
	IFileManager& FileManager = IFileManager::Get();

	// Default pattern for code search
	FString SearchPattern = Pattern.IsEmpty() ? TEXT("*") : Pattern;

	// Collect matching files
	class FCodeFileVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		TArray<FString>& Files;
		FString Pattern;
		FString ProjectDir;
		FExploreTool* Tool;

		FCodeFileVisitor(TArray<FString>& InFiles, const FString& InPattern, FExploreTool* InTool)
			: Files(InFiles), Pattern(InPattern), Tool(InTool)
		{
			ProjectDir = UACPSettings::GetWorkingDirectory();
		}

		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			FString FullName = FilenameOrDirectory;
			FString Name = FPaths::GetCleanFilename(FullName);

			// Get path relative to project for gitignore matching
			FString RelToProject = FullName;
			FPaths::MakePathRelativeTo(RelToProject, *ProjectDir);

			// Check gitignore
			if (Tool->IsIgnoredByGitignore(RelToProject, bIsDirectory))
			{
				return true;
			}

			if (bIsDirectory) return true;

			// Skip non-text files
			FString Ext = FPaths::GetExtension(Name).ToLower();
			if (Ext != TEXT("cpp") && Ext != TEXT("h") && Ext != TEXT("c") &&
				Ext != TEXT("hpp") && Ext != TEXT("cs") && Ext != TEXT("txt") &&
				Ext != TEXT("ini") && Ext != TEXT("json") && Ext != TEXT("xml") &&
				Ext != TEXT("yaml") && Ext != TEXT("md") && Ext != TEXT("py"))
			{
				return true;
			}

			if (!Pattern.IsEmpty() && !Tool->MatchesPattern(Name, Pattern))
			{
				return true;
			}

			Files.Add(FilenameOrDirectory);
			return true;
		}
	};

	FCodeFileVisitor Visitor(Files, SearchPattern, this);

	if (bRecursive)
	{
		FileManager.IterateDirectoryRecursively(*FullPath, Visitor);
	}
	else
	{
		FileManager.IterateDirectory(*FullPath, Visitor);
	}

	// Search in files
	struct FMatch
	{
		FString File;
		int32 Line;
		FString Content;
		TArray<FString> ContextBefore;
		TArray<FString> ContextAfter;
	};

	TArray<FMatch> Matches;
	FString QueryLower = Query.ToLower();

	for (const FString& FilePath : Files)
	{
		FString Content;
		if (!FFileHelper::LoadFileToString(Content, *FilePath))
		{
			continue;
		}

		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);

		for (int32 i = 0; i < Lines.Num(); i++)
		{
			if (Lines[i].ToLower().Contains(QueryLower))
			{
				FMatch Match;
				Match.File = FilePath;
				Match.Line = i + 1;
				Match.Content = Lines[i];

				// Get context
				for (int32 j = FMath::Max(0, i - Context); j < i; j++)
				{
					Match.ContextBefore.Add(Lines[j]);
				}
				for (int32 j = i + 1; j <= FMath::Min(Lines.Num() - 1, i + Context); j++)
				{
					Match.ContextAfter.Add(Lines[j]);
				}

				Matches.Add(Match);
			}
		}
	}

	// Build output
	int32 Total = Matches.Num();
	int32 StartIdx = Offset;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# SEARCH \"%s\" matches=%d\n"), *Query, Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const FMatch& M = Matches[i];
		FString RelFile = M.File;
		FPaths::MakePathRelativeTo(RelFile, *UACPSettings::GetWorkingDirectory());

		Output += FString::Printf(TEXT("\n%s:%d\n"), *RelFile, M.Line);

		// Context before
		int32 CtxLineNum = M.Line - M.ContextBefore.Num();
		for (const FString& Ctx : M.ContextBefore)
		{
			Output += FString::Printf(TEXT("%d\t%s\n"), CtxLineNum++, *Ctx);
		}

		// Match line
		Output += FString::Printf(TEXT("%d>\t%s\n"), M.Line, *M.Content);

		// Context after
		CtxLineNum = M.Line + 1;
		for (const FString& Ctx : M.ContextAfter)
		{
			Output += FString::Printf(TEXT("%d\t%s\n"), CtxLineNum++, *Ctx);
		}
	}

	if (EndIdx < Total)
	{
		Output += FString::Printf(TEXT("\n# MORE offset=%d remaining=%d\n"), EndIdx, Total - EndIdx);
	}

	return Output;
}

FString FExploreTool::ListAssets(const FString& AssetPath, const FString& Pattern, const FString& Type, int32 Offset, int32 Limit)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*AssetPath), Assets, true);

	// Filter by type and pattern
	TArray<FAssetData> FilteredAssets;
	for (const FAssetData& Asset : Assets)
	{
		FString AssetName = Asset.AssetName.ToString();

		// Pattern filter
		if (!Pattern.IsEmpty() && !MatchesPattern(AssetName, Pattern))
		{
			continue;
		}

		// Type filter
		FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (Type.Equals(TEXT("blueprints"), ESearchCase::IgnoreCase))
		{
			if (!ClassName.Contains(TEXT("Blueprint"))) continue;
		}
		else if (Type.Equals(TEXT("materials"), ESearchCase::IgnoreCase))
		{
			if (!ClassName.Contains(TEXT("Material"))) continue;
		}
		else if (Type.Equals(TEXT("textures"), ESearchCase::IgnoreCase))
		{
			if (!ClassName.Contains(TEXT("Texture"))) continue;
		}

		FilteredAssets.Add(Asset);
	}

	// Sort by name
	FilteredAssets.Sort([](const FAssetData& A, const FAssetData& B) {
		return A.AssetName.ToString() < B.AssetName.ToString();
	});

	// Build output
	int32 Total = FilteredAssets.Num();
	int32 StartIdx = Offset;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# ASSETS %s count=%d\n"), *AssetPath, Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const FAssetData& Asset = FilteredAssets[i];
		FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		Output += FString::Printf(TEXT("%s\t%s\t%s\n"),
			*Asset.AssetName.ToString(),
			*ClassName,
			*Asset.PackagePath.ToString());
	}

	if (EndIdx < Total)
	{
		Output += FString::Printf(TEXT("# MORE offset=%d remaining=%d\n"), EndIdx, Total - EndIdx);
	}

	return Output;
}

FString FExploreTool::SearchBlueprints(const FString& AssetPath, const FString& Pattern, const FString& Query,
	const FBlueprintFilter& Filter, int32 Offset, int32 Limit)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(FName(*AssetPath), Assets, true);

	// Filter to Blueprints and apply criteria
	TArray<TPair<FAssetData, UBlueprint*>> MatchingBPs;

	for (const FAssetData& Asset : Assets)
	{
		FString ClassName = Asset.AssetClassPath.GetAssetName().ToString();
		if (!ClassName.Contains(TEXT("Blueprint"))) continue;

		FString AssetName = Asset.AssetName.ToString();
		if (!Pattern.IsEmpty() && !MatchesPattern(AssetName, Pattern))
		{
			continue;
		}

		// Load Blueprint to check filters
		UBlueprint* BP = Cast<UBlueprint>(Asset.GetAsset());
		if (!BP) continue;

		if (MatchesFilter(BP, Query, Filter))
		{
			MatchingBPs.Add(TPair<FAssetData, UBlueprint*>(Asset, BP));
		}
	}

	// Sort
	MatchingBPs.Sort([](const auto& A, const auto& B) {
		return A.Key.AssetName.ToString() < B.Key.AssetName.ToString();
	});

	// Build output
	int32 Total = MatchingBPs.Num();
	int32 StartIdx = Offset;
	int32 EndIdx = FMath::Min(StartIdx + Limit, Total);

	FString Output = FString::Printf(TEXT("# BLUEPRINTS %s count=%d\n"), *AssetPath, Total);

	for (int32 i = StartIdx; i < EndIdx; i++)
	{
		const FAssetData& Asset = MatchingBPs[i].Key;
		UBlueprint* BP = MatchingBPs[i].Value;
		FString ParentName = BP->ParentClass ? BP->ParentClass->GetName() : TEXT("None");

		int32 VarCount = BP->NewVariables.Num();
		int32 CompCount = BP->SimpleConstructionScript ? BP->SimpleConstructionScript->GetAllNodes().Num() : 0;
		int32 GraphCount = BP->UbergraphPages.Num() + BP->FunctionGraphs.Num() + BP->MacroGraphs.Num();

		// Output: name, parent, path, stats
		Output += FString::Printf(TEXT("%s\t%s\t%s\tvars=%d comps=%d graphs=%d\n"),
			*BP->GetName(), *ParentName, *Asset.PackagePath.ToString(), VarCount, CompCount, GraphCount);
	}

	if (EndIdx < Total)
	{
		Output += FString::Printf(TEXT("# MORE offset=%d remaining=%d\n"), EndIdx, Total - EndIdx);
	}

	return Output;
}

bool FExploreTool::MatchesFilter(UBlueprint* Blueprint, const FString& Query, const FBlueprintFilter& Filter)
{
	// Check parent class filter
	if (!Filter.Parent.IsEmpty())
	{
		if (!Blueprint->ParentClass) return false;

		FString ParentName = Blueprint->ParentClass->GetName();
		if (!ParentName.Contains(Filter.Parent)) return false;
	}

	// Check component filter
	if (!Filter.Component.IsEmpty())
	{
		if (!HasComponent(Blueprint, Filter.Component)) return false;
	}

	// Check interface filter
	if (!Filter.Interface.IsEmpty())
	{
		if (!HasInterface(Blueprint, Filter.Interface)) return false;
	}

	// Check references filter
	if (!Filter.References.IsEmpty())
	{
		if (!ReferencesAsset(Blueprint, Filter.References)) return false;
	}

	// Check query (searches variables, functions, components)
	if (!Query.IsEmpty())
	{
		bool bFound = false;

		// Search in variables
		for (const FBPVariableDescription& Var : Blueprint->NewVariables)
		{
			if (MatchesQuery(Var.VarName.ToString(), Query))
			{
				bFound = true;
				break;
			}
		}

		// Search in function names
		if (!bFound)
		{
			for (UEdGraph* Graph : Blueprint->FunctionGraphs)
			{
				if (MatchesQuery(Graph->GetName(), Query))
				{
					bFound = true;
					break;
				}
			}
		}

		// Search in components
		if (!bFound && Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
			{
				if (Node && MatchesQuery(Node->GetVariableName().ToString(), Query))
				{
					bFound = true;
					break;
				}
			}
		}

		if (!bFound) return false;
	}

	return true;
}

bool FExploreTool::HasComponent(UBlueprint* Blueprint, const FString& ComponentName)
{
	if (!Blueprint->SimpleConstructionScript) return false;

	for (USCS_Node* Node : Blueprint->SimpleConstructionScript->GetAllNodes())
	{
		if (!Node || !Node->ComponentTemplate) continue;

		FString ClassName = Node->ComponentTemplate->GetClass()->GetName();
		FString VarName = Node->GetVariableName().ToString();

		if (ClassName.Contains(ComponentName) || VarName.Contains(ComponentName))
		{
			return true;
		}
	}

	return false;
}

bool FExploreTool::HasInterface(UBlueprint* Blueprint, const FString& InterfaceName)
{
	for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
	{
		if (Interface.Interface && Interface.Interface->GetName().Contains(InterfaceName))
		{
			return true;
		}
	}

	return false;
}

bool FExploreTool::ReferencesAsset(UBlueprint* Blueprint, const FString& AssetName)
{
	// Get asset references
	TArray<FName> Dependencies;
	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

	FString PackageName = Blueprint->GetOutermost()->GetName();
	AssetRegistryModule.Get().GetDependencies(FName(*PackageName), Dependencies);

	for (const FName& Dep : Dependencies)
	{
		if (Dep.ToString().Contains(AssetName))
		{
			return true;
		}
	}

	return false;
}

bool FExploreTool::MatchesQuery(const FString& Text, const FString& Query)
{
	return Text.Contains(Query);
}

bool FExploreTool::MatchesPattern(const FString& Name, const FString& Pattern)
{
	// Simple glob matching: * = any chars, ? = single char
	if (Pattern.IsEmpty()) return true;
	if (Pattern == TEXT("*")) return true;

	// Convert glob to regex-like matching
	FString PatternLower = Pattern.ToLower();
	FString NameLower = Name.ToLower();

	// Handle *.ext pattern
	if (PatternLower.StartsWith(TEXT("*.")))
	{
		FString Ext = PatternLower.Mid(1); // .ext
		return NameLower.EndsWith(Ext);
	}

	// Handle prefix* pattern
	if (PatternLower.EndsWith(TEXT("*")) && !PatternLower.StartsWith(TEXT("*")))
	{
		FString Prefix = PatternLower.LeftChop(1);
		return NameLower.StartsWith(Prefix);
	}

	// Handle *suffix pattern
	if (PatternLower.StartsWith(TEXT("*")) && !PatternLower.EndsWith(TEXT("*")))
	{
		FString Suffix = PatternLower.Mid(1);
		return NameLower.EndsWith(Suffix);
	}

	// Handle *contains* pattern
	if (PatternLower.StartsWith(TEXT("*")) && PatternLower.EndsWith(TEXT("*")))
	{
		FString Contains = PatternLower.Mid(1, PatternLower.Len() - 2);
		return NameLower.Contains(Contains);
	}

	// Exact match
	return NameLower == PatternLower;
}

void FExploreTool::LoadGitIgnorePatterns()
{
	if (bGitIgnoreLoaded) return;
	bGitIgnoreLoaded = true;

	FString GitIgnorePath = FPaths::Combine(UACPSettings::GetWorkingDirectory(), TEXT(".gitignore"));
	FString Content;

	if (FFileHelper::LoadFileToString(Content, *GitIgnorePath))
	{
		TArray<FString> Lines;
		Content.ParseIntoArrayLines(Lines);

		for (const FString& Line : Lines)
		{
			FString Trimmed = Line.TrimStartAndEnd();
			// Skip empty lines and comments
			if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("#")))
			{
				continue;
			}
			GitIgnorePatterns.Add(Trimmed);
		}

		UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("[AIK] Loaded %d gitignore patterns"), GitIgnorePatterns.Num());
	}
}

bool FExploreTool::IsIgnoredByGitignore(const FString& RelativePath, bool bIsDirectory)
{
	LoadGitIgnorePatterns();

	// Normalize path separators
	FString NormalizedPath = RelativePath.Replace(TEXT("\\"), TEXT("/"));
	if (NormalizedPath.StartsWith(TEXT("/")))
	{
		NormalizedPath = NormalizedPath.Mid(1);
	}

	// Get just the filename for simple pattern matching
	FString FileName = FPaths::GetCleanFilename(NormalizedPath);

	for (const FString& Pattern : GitIgnorePatterns)
	{
		bool bNegation = Pattern.StartsWith(TEXT("!"));
		FString ActualPattern = bNegation ? Pattern.Mid(1) : Pattern;

		// Directory-only patterns end with /
		bool bDirOnly = ActualPattern.EndsWith(TEXT("/"));
		if (bDirOnly)
		{
			ActualPattern = ActualPattern.LeftChop(1);
			if (!bIsDirectory) continue;
		}

		bool bMatches = false;

		// Pattern with slash matches full path
		if (ActualPattern.Contains(TEXT("/")))
		{
			// Match against full relative path
			bMatches = MatchesPattern(NormalizedPath, ActualPattern);
		}
		else
		{
			// Simple pattern matches any filename
			bMatches = MatchesPattern(FileName, ActualPattern);
		}

		if (bMatches)
		{
			// Negation pattern un-ignores
			if (bNegation) return false;
			return true;
		}
	}

	return false;
}
