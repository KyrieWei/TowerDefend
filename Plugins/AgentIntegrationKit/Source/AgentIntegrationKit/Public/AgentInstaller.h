// Copyright 2026 Betide Studio. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

struct FPrerequisiteStatus
{
	bool bBundledBunAvailable = false;
	FString BundledBunPath;
	FString BundledBunVersion;
};

struct FAgentInstallInfo
{
	FString AgentName;

	// Base CLI (the actual tool like claude, cursor)
	FString BaseExecutableName;
	FString BaseInstallCommand_Mac;
	FString BaseInstallCommand_Win;
	FString BaseInstallCommand_Linux;
	FString BaseInstallUrl;

	// ACP Adapter
	FString AdapterDirName;          // Directory name under bundled Adapters/ (empty if no adapter)
	FString AdapterEntryPointFile;   // e.g. "dist/index.js" relative to adapter dir
	FString AdapterNpmPackage;       // npm package name for remote install (e.g. "@google/gemini-cli")
	bool bAdapterIsBundled = false;  // true if source is in Source/ThirdParty/Adapters/
	bool bAdapterIsNativeBinary = false; // true for Rust adapters (codex-acp), false for JS

	// Auth credential file (relative to HOME)
	FString AuthCheckFile;

	bool RequiresAdapter() const { return !AdapterDirName.IsEmpty() || !AdapterNpmPackage.IsEmpty(); }
	bool RequiresBaseCLI() const { return !BaseExecutableName.IsEmpty(); }

	FString GetBaseInstallCommand() const
	{
#if PLATFORM_MAC
		return BaseInstallCommand_Mac;
#elif PLATFORM_WINDOWS
		return BaseInstallCommand_Win;
#else
		return BaseInstallCommand_Linux;
#endif
	}
};

DECLARE_DELEGATE_OneParam(FOnInstallProgress, const FString& /* StatusMessage */);
DECLARE_DELEGATE_TwoParams(FOnInstallComplete, bool /* bSuccess */, const FString& /* ErrorMessage */);

class AGENTINTEGRATIONKIT_API FAgentInstaller
{
public:
	static FAgentInstaller& Get();

	// Bundled runtime
	static FString GetBundledBunPath();
	static bool EnsureBunExecutable();
	static bool EnsureNativeAdapterExecutable(const FString& BinaryPath);

	// Adapter paths
	static FString GetBundledAdaptersDir();
	static FString GetManagedAdaptersDir();  // ~/.agentintegrationkit/adapters/
	static FString GetAdapterEntryPoint(const FAgentInstallInfo& Info);

	// Executable resolution (for base CLIs like claude, codex, cursor)
	TArray<FString> GetExtendedPaths() const;
	bool ResolveExecutable(const FString& ExecutableName, FString& OutResolvedPath) const;
	bool ResolveExecutableViaLoginShell(const FString& ExecutableName, FString& OutResolvedPath) const;

	// Prerequisites
	FPrerequisiteStatus CheckPrerequisites();

	// Agent info database
	static FAgentInstallInfo GetAgentInstallInfo(const FString& AgentName);
	static TArray<FAgentInstallInfo> GetAllAgentInstallInfos();

	// Installation (now only for base CLIs — adapters are bundled)
	void InstallAgentAsync(
		const FString& AgentName,
		FOnInstallProgress OnProgress,
		FOnInstallComplete OnComplete
	);

private:
	FAgentInstaller() = default;

	void RunInstallOnBackgroundThread(
		const FString& AgentName,
		FOnInstallProgress OnProgress,
		FOnInstallComplete OnComplete
	);

	bool InstallBaseCLI(const FAgentInstallInfo& Info, FString& OutError);
	bool LaunchExternalInstallTerminal(const FString& AgentName, const FString& Command, FString& OutError) const;
	FString GetLoginShellPath() const;
	FString BuildShellCommand(const FString& Command) const;
	bool RunShellCommand(const FString& Command, FString& OutStdOut, FString& OutStdErr, int32& OutReturnCode) const;
	bool ParseVersion(const FString& VersionOutput, FString& OutVersion) const;

	mutable FCriticalSection CacheLock;
	mutable TMap<FString, FString> ResolvedPathCache;
	mutable FDateTime LastCacheRefresh;
	static constexpr double CacheTTLSeconds = 300.0;
};
