// Copyright 2026 Betide Studio. All Rights Reserved.

#include "AgentInstaller.h"
#include "AgentIntegrationKitModule.h"
#include "ACPSettings.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Async/Async.h"

namespace
{
static bool EnsureUnixBinaryExecutable(const FString& BinaryPath, const TCHAR* BinaryLabel, bool bClearMacQuarantine)
{
#if PLATFORM_MAC || PLATFORM_LINUX
	if (BinaryPath.IsEmpty() || !IFileManager::Get().FileExists(*BinaryPath))
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("AgentInstaller: %s path is missing: %s"), BinaryLabel, *BinaryPath);
		return false;
	}

	FString StdOut, StdErr;
	int32 ReturnCode = -1;
	FPlatformProcess::ExecProcess(
		TEXT("/bin/chmod"),
		*FString::Printf(TEXT("+x \"%s\""), *BinaryPath),
		&ReturnCode, &StdOut, &StdErr);
	if (ReturnCode != 0)
	{
		UE_LOG(LogAgentIntegrationKit, Warning, TEXT("AgentInstaller: Failed to set execute permission on %s: %s"), BinaryLabel, *StdErr);
	}

#if PLATFORM_MAC
	if (bClearMacQuarantine)
	{
		StdOut.Empty();
		StdErr.Empty();
		ReturnCode = -1;
		FPlatformProcess::ExecProcess(
			TEXT("/usr/bin/xattr"),
			*FString::Printf(TEXT("-d com.apple.quarantine \"%s\""), *BinaryPath),
			&ReturnCode, &StdOut, &StdErr);
		if (ReturnCode != 0 && !StdErr.Contains(TEXT("No such xattr")))
		{
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("AgentInstaller: Could not clear quarantine for %s: %s"), BinaryLabel, *StdErr);
		}
	}
#endif

	StdOut.Empty();
	StdErr.Empty();
	ReturnCode = -1;
	FPlatformProcess::ExecProcess(
		TEXT("/bin/test"),
		*FString::Printf(TEXT("-x \"%s\""), *BinaryPath),
		&ReturnCode, &StdOut, &StdErr);
	if (ReturnCode != 0)
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("AgentInstaller: %s is not executable: %s"), BinaryLabel, *BinaryPath);
		return false;
	}

	return true;
#else
	return true;
#endif
}

static FString MakeSafeFileStem(const FString& Value)
{
	FString Result;
	Result.Reserve(Value.Len());
	for (const TCHAR Ch : Value)
	{
		if ((Ch >= TEXT('a') && Ch <= TEXT('z')) ||
			(Ch >= TEXT('A') && Ch <= TEXT('Z')) ||
			(Ch >= TEXT('0') && Ch <= TEXT('9')))
		{
			Result.AppendChar(Ch);
		}
		else
		{
			Result.AppendChar(TEXT('-'));
		}
	}
	Result.TrimStartAndEndInline();
	if (Result.IsEmpty())
	{
		Result = TEXT("agent");
	}
	return Result;
}

static FString GetManagedBundledAdapterDir(const FAgentInstallInfo& Info)
{
	if (!Info.bAdapterIsBundled || Info.AdapterDirName.IsEmpty())
	{
		return FString();
	}

	FString ManagedRoot = FAgentInstaller::GetManagedAdaptersDir();
	if (ManagedRoot.IsEmpty())
	{
		return FString();
	}

	return FPaths::Combine(ManagedRoot, TEXT("bundled"), Info.AdapterDirName);
}

static bool IsBunLockfileReplaceError(const FString& ErrorText)
{
	if (ErrorText.IsEmpty())
	{
		return false;
	}

	FString Lower = ErrorText;
	Lower.ToLowerInline();
	return Lower.Contains(TEXT("failed to replace old lockfile with new lockfile on disk"))
		|| (Lower.Contains(TEXT("lockfile")) && Lower.Contains(TEXT("einval")));
}

static void DeleteBunLockfiles(const FString& Directory)
{
	if (Directory.IsEmpty())
	{
		return;
	}

	IFileManager& FileManager = IFileManager::Get();
	FileManager.Delete(*FPaths::Combine(Directory, TEXT("bun.lock")), false, true);
	FileManager.Delete(*FPaths::Combine(Directory, TEXT("bun.lockb")), false, true);
}

static void ResetAdapterInstallArtifacts(const FString& Directory)
{
	if (Directory.IsEmpty())
	{
		return;
	}

	IFileManager& FileManager = IFileManager::Get();
	FileManager.DeleteDirectory(*FPaths::Combine(Directory, TEXT("node_modules")), false, true);
	DeleteBunLockfiles(Directory);
}

static bool ResolveInstalledClaudeExecutablePath(FString& OutResolvedPath)
{
	if (FAgentInstaller::Get().ResolveExecutable(TEXT("claude-internal.cmd"), OutResolvedPath))
	{
		return true;
	}

	TArray<FString> CandidatePaths;
#if PLATFORM_WINDOWS
	FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT("AppData/Roaming/npm/claude-internal.cmd")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/claude.exe")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/claude.cmd")));
		CandidatePaths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin/claude")));
	}
#else
	FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!HomeDir.IsEmpty())
	{
		CandidatePaths.Add(FPaths::Combine(HomeDir, TEXT(".local/bin/claude")));
	}
#endif

	for (FString Candidate : CandidatePaths)
	{
		FPaths::NormalizeFilename(Candidate);
		if (IFileManager::Get().FileExists(*Candidate))
		{
			OutResolvedPath = Candidate;
			return true;
		}
	}

	return false;
}

static void AutoSaveClaudeExecutablePath(const FString& DetectedPath)
{
	if (DetectedPath.IsEmpty())
	{
		return;
	}

	UACPSettings* Settings = UACPSettings::Get();
	if (!Settings || !Settings->bAutoSaveClaudeCodeExecutablePathAfterInstall)
	{
		return;
	}

	if (Settings->ClaudeCodeExecutablePath.FilePath.Equals(DetectedPath, ESearchCase::CaseSensitive))
	{
		return;
	}

	Settings->ClaudeCodeExecutablePath.FilePath = DetectedPath;
	Settings->SaveConfig();
	UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: Saved detected Claude executable path: %s"), *DetectedPath);
}
}

FAgentInstaller& FAgentInstaller::Get()
{
	static FAgentInstaller Instance;
	return Instance;
}

// ============================================
// Bundled Runtime
// ============================================

FString FAgentInstaller::GetBundledBunPath()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AgentIntegrationKit"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogAgentIntegrationKit, Error, TEXT("AgentInstaller: Could not find AgentIntegrationKit plugin"));
		return FString();
	}

	FString PluginDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
	FString BunDir = FPaths::Combine(PluginDir, TEXT("Source"), TEXT("ThirdParty"), TEXT("Bun"));

#if PLATFORM_WINDOWS
	FString BunPath = FPaths::Combine(BunDir, TEXT("Win64"), TEXT("bun.exe"));
#elif PLATFORM_MAC
	#if PLATFORM_CPU_ARM_FAMILY
		FString BunPath = FPaths::Combine(BunDir, TEXT("Mac-arm64"), TEXT("bun"));
	#else
		FString BunPath = FPaths::Combine(BunDir, TEXT("Mac-x64"), TEXT("bun"));
	#endif
#elif PLATFORM_LINUX
	FString BunPath = FPaths::Combine(BunDir, TEXT("Linux-x64"), TEXT("bun"));
#else
	FString BunPath;
#endif

	if (!BunPath.IsEmpty() && IFileManager::Get().FileExists(*BunPath))
	{
		return BunPath;
	}

	UE_LOG(LogAgentIntegrationKit, Error, TEXT("AgentInstaller: Bundled Bun binary not found at: %s"), *BunPath);
	return FString();
}

bool FAgentInstaller::EnsureBunExecutable()
{
	FString BunPath = GetBundledBunPath();
	if (BunPath.IsEmpty())
	{
		return false;
	}

	return EnsureUnixBinaryExecutable(BunPath, TEXT("bundled Bun binary"), false);
}

bool FAgentInstaller::EnsureNativeAdapterExecutable(const FString& BinaryPath)
{
#if PLATFORM_WINDOWS
	return !BinaryPath.IsEmpty() && IFileManager::Get().FileExists(*BinaryPath);
#else
	return EnsureUnixBinaryExecutable(BinaryPath, TEXT("native adapter binary"), true);
#endif
}

// ============================================
// Adapter Paths
// ============================================

FString FAgentInstaller::GetBundledAdaptersDir()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("AgentIntegrationKit"));
	if (!Plugin.IsValid())
	{
		return FString();
	}

	FString PluginDir = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
	return FPaths::Combine(PluginDir, TEXT("Source"), TEXT("ThirdParty"), TEXT("Adapters"));
}

FString FAgentInstaller::GetManagedAdaptersDir()
{
	FString HomeDir;
#if PLATFORM_WINDOWS
	HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
#else
	HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
#endif
	if (HomeDir.IsEmpty())
	{
		return FString();
	}

	return FPaths::Combine(HomeDir, TEXT(".agentintegrationkit"), TEXT("adapters"));
}

FString FAgentInstaller::GetAdapterEntryPoint(const FAgentInstallInfo& Info)
{
	// Native binary adapters: resolve per-platform binary from bundled dir
	if (Info.bAdapterIsNativeBinary && Info.bAdapterIsBundled && !Info.AdapterDirName.IsEmpty())
	{
		FString BundledDir = GetBundledAdaptersDir();
		if (!BundledDir.IsEmpty())
		{
			FString PlatformDir;
			FString BinaryName;
#if PLATFORM_WINDOWS
			PlatformDir = TEXT("win32-x64");
			BinaryName = Info.AdapterDirName + TEXT(".exe");
#elif PLATFORM_MAC
	#if PLATFORM_CPU_ARM_FAMILY
			PlatformDir = TEXT("darwin-arm64");
	#else
			PlatformDir = TEXT("darwin-x64");
	#endif
			BinaryName = Info.AdapterDirName;
#elif PLATFORM_LINUX
			PlatformDir = TEXT("linux-x64");
			BinaryName = Info.AdapterDirName;
#endif
			FString BinaryPath = FPaths::Combine(BundledDir, Info.AdapterDirName, TEXT("bin"), PlatformDir, BinaryName);
			FPaths::NormalizeFilename(BinaryPath);
			if (IFileManager::Get().FileExists(*BinaryPath))
			{
				return BinaryPath;
			}
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("AgentInstaller: Native adapter binary not found: %s"), *BinaryPath);
		}

		// Fallback: check managed dir via npm (platform binary in optional dependency package)
		if (!Info.AdapterNpmPackage.IsEmpty())
		{
			FString ManagedDir = GetManagedAdaptersDir();
			if (!ManagedDir.IsEmpty())
			{
				// npm distributes platform binaries as optional deps: @zed-industries/codex-acp-<platform>
				FString PlatformPkg;
				FString BinName;
#if PLATFORM_WINDOWS
	#if PLATFORM_CPU_ARM_FAMILY
				PlatformPkg = Info.AdapterDirName + TEXT("-win32-arm64");
	#else
				PlatformPkg = Info.AdapterDirName + TEXT("-win32-x64");
	#endif
				BinName = Info.AdapterDirName + TEXT(".exe");
#elif PLATFORM_MAC
	#if PLATFORM_CPU_ARM_FAMILY
				PlatformPkg = Info.AdapterDirName + TEXT("-darwin-arm64");
	#else
				PlatformPkg = Info.AdapterDirName + TEXT("-darwin-x64");
	#endif
				BinName = Info.AdapterDirName;
#elif PLATFORM_LINUX
	#if PLATFORM_CPU_ARM_FAMILY
				PlatformPkg = Info.AdapterDirName + TEXT("-linux-arm64");
	#else
				PlatformPkg = Info.AdapterDirName + TEXT("-linux-x64");
	#endif
				BinName = Info.AdapterDirName;
#endif
				// The binary lives at: node_modules/@zed-industries/<adapter>-<platform>/bin/<binary>
				FString NpmScope = Info.AdapterNpmPackage.Left(Info.AdapterNpmPackage.Find(TEXT("/")));
				FString NpmBinaryPath = FPaths::Combine(ManagedDir, TEXT("node_modules"), NpmScope, PlatformPkg, TEXT("bin"), BinName);
				FPaths::NormalizeFilename(NpmBinaryPath);
				if (IFileManager::Get().FileExists(*NpmBinaryPath))
				{
					return NpmBinaryPath;
				}
			}
		}

		return FString();
	}

	// JS adapters need an entry point file
	if (Info.AdapterEntryPointFile.IsEmpty())
	{
		return FString();
	}

	// 0. Check managed copy of bundled adapters first (user-writable build location)
	if (Info.bAdapterIsBundled && !Info.AdapterDirName.IsEmpty())
	{
		FString ManagedBundledDir = GetManagedBundledAdapterDir(Info);
		if (!ManagedBundledDir.IsEmpty())
		{
			FString EntryPath = FPaths::Combine(ManagedBundledDir, Info.AdapterEntryPointFile);
			FPaths::NormalizeFilename(EntryPath);
			if (IFileManager::Get().FileExists(*EntryPath))
			{
				return EntryPath;
			}
		}
	}

	// 1. Check bundled adapters (Source/ThirdParty/Adapters/<DirName>/<EntryPoint>)
	if (Info.bAdapterIsBundled && !Info.AdapterDirName.IsEmpty())
	{
		FString BundledDir = GetBundledAdaptersDir();
		if (!BundledDir.IsEmpty())
		{
			FString EntryPath = FPaths::Combine(BundledDir, Info.AdapterDirName, Info.AdapterEntryPointFile);
			FPaths::NormalizeFilename(EntryPath);
			if (IFileManager::Get().FileExists(*EntryPath))
			{
				return EntryPath;
			}
		}
	}

	// 2. Check managed adapters (~/.agentintegrationkit/adapters/node_modules/<NpmPackage>/<EntryPoint>)
	if (!Info.AdapterNpmPackage.IsEmpty())
	{
		FString ManagedDir = GetManagedAdaptersDir();
		if (!ManagedDir.IsEmpty())
		{
			FString EntryPath = FPaths::Combine(ManagedDir, TEXT("node_modules"), Info.AdapterNpmPackage, Info.AdapterEntryPointFile);
			FPaths::NormalizeFilename(EntryPath);
			if (IFileManager::Get().FileExists(*EntryPath))
			{
				return EntryPath;
			}
		}
	}

	UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("AgentInstaller: Adapter entry point not found for %s"), *Info.AgentName);
	return FString();
}

// ============================================
// Executable Resolution (for base CLIs)
// ============================================

TArray<FString> FAgentInstaller::GetExtendedPaths() const
{
	TArray<FString> Paths;

#if PLATFORM_MAC
	FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!HomeDir.IsEmpty())
	{
		Paths.Add(FPaths::Combine(HomeDir, TEXT("bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".bun/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".npm-global/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".nvm/current/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT("n/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".local/share/pnpm")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".local/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".opencode/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".asdf/shims")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".volta/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".fnm/current/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".proto/shims")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".local/share/mise/shims")));
	}
	Paths.Add(TEXT("/usr/local/bin"));
	Paths.Add(TEXT("/opt/homebrew/bin"));
	Paths.Add(TEXT("/usr/bin"));
#elif PLATFORM_WINDOWS
	FString UserProfile = FPlatformMisc::GetEnvironmentVariable(TEXT("USERPROFILE"));
	if (!UserProfile.IsEmpty())
	{
		// Claude Code installs here via winget/install.ps1 (often not added to PATH)
		Paths.Add(FPaths::Combine(UserProfile, TEXT(".local/bin")));
		// Codex CLI
		Paths.Add(FPaths::Combine(UserProfile, TEXT(".codex/bin")));
		Paths.Add(FPaths::Combine(UserProfile, TEXT(".bun/bin")));
		Paths.Add(FPaths::Combine(UserProfile, TEXT(".volta/bin")));
		Paths.Add(FPaths::Combine(UserProfile, TEXT(".fnm/current/bin")));
		Paths.Add(FPaths::Combine(UserProfile, TEXT(".proto/shims")));
	}
	FString AppData = FPlatformMisc::GetEnvironmentVariable(TEXT("APPDATA"));
	if (!AppData.IsEmpty())
	{
		Paths.Add(FPaths::Combine(AppData, TEXT("npm")));
	}
	FString LocalAppData = FPlatformMisc::GetEnvironmentVariable(TEXT("LOCALAPPDATA"));
	if (!LocalAppData.IsEmpty())
	{
		Paths.Add(FPaths::Combine(LocalAppData, TEXT("Volta/bin")));
		Paths.Add(FPaths::Combine(LocalAppData, TEXT("fnm_multishells")));
	}
	FString ProgramFiles = FPlatformMisc::GetEnvironmentVariable(TEXT("ProgramFiles"));
	if (!ProgramFiles.IsEmpty())
	{
		Paths.Add(FPaths::Combine(ProgramFiles, TEXT("nodejs")));
	}
#else
	// Linux
	FString HomeDir = FPlatformMisc::GetEnvironmentVariable(TEXT("HOME"));
	if (!HomeDir.IsEmpty())
	{
		Paths.Add(FPaths::Combine(HomeDir, TEXT("bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".bun/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".npm-global/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".local/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".opencode/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".nvm/current/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".asdf/shims")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".volta/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".fnm/current/bin")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".proto/shims")));
		Paths.Add(FPaths::Combine(HomeDir, TEXT(".local/share/mise/shims")));
	}
	Paths.Add(TEXT("/usr/local/bin"));
	Paths.Add(TEXT("/usr/bin"));
#endif

	return Paths;
}

bool FAgentInstaller::ResolveExecutable(const FString& ExecutableName, FString& OutResolvedPath) const
{
	// Check cache first
	{
		FScopeLock Lock(&CacheLock);
		FDateTime Now = FDateTime::UtcNow();
		if ((Now - LastCacheRefresh).GetTotalSeconds() < CacheTTLSeconds)
		{
			if (const FString* CachedPath = ResolvedPathCache.Find(ExecutableName))
			{
				if (!CachedPath->IsEmpty() && IFileManager::Get().FileExists(**CachedPath))
				{
					OutResolvedPath = *CachedPath;
					return true;
				}
			}
		}
		else
		{
			ResolvedPathCache.Empty();
			LastCacheRefresh = Now;
		}
	}

	// If it's an absolute path, check if file exists
	if (!FPaths::IsRelative(ExecutableName) || ExecutableName.Contains(TEXT("/")) || ExecutableName.Contains(TEXT("\\")))
	{
		FString NormalizedPath = ExecutableName;
		FPaths::NormalizeFilename(NormalizedPath);

		if (IFileManager::Get().FileExists(*NormalizedPath))
		{
			OutResolvedPath = NormalizedPath;
			FScopeLock Lock(&CacheLock);
			ResolvedPathCache.Add(ExecutableName, NormalizedPath);
			return true;
		}
		return false;
	}

	// Check extended paths
	TArray<FString> SearchPaths = GetExtendedPaths();
	for (const FString& BasePath : SearchPaths)
	{
		FString FullPath = FPaths::Combine(BasePath, ExecutableName);
		if (IFileManager::Get().FileExists(*FullPath))
		{
			OutResolvedPath = FullPath;
			FScopeLock Lock(&CacheLock);
			ResolvedPathCache.Add(ExecutableName, FullPath);
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("AgentInstaller: Found %s at %s"), *ExecutableName, *FullPath);
			return true;
		}

#if PLATFORM_WINDOWS
		// Try with .cmd extension
		FString CmdPath = FullPath + TEXT(".cmd");
		if (IFileManager::Get().FileExists(*CmdPath))
		{
			OutResolvedPath = CmdPath;
			FScopeLock Lock(&CacheLock);
			ResolvedPathCache.Add(ExecutableName, CmdPath);
			return true;
		}
		// Try with .exe extension
		FString ExePath = FullPath + TEXT(".exe");
		if (IFileManager::Get().FileExists(*ExePath))
		{
			OutResolvedPath = ExePath;
			FScopeLock Lock(&CacheLock);
			ResolvedPathCache.Add(ExecutableName, ExePath);
			return true;
		}
#endif
	}

	// Try login shell resolution as fallback
	if (ResolveExecutableViaLoginShell(ExecutableName, OutResolvedPath))
	{
		FScopeLock Lock(&CacheLock);
		ResolvedPathCache.Add(ExecutableName, OutResolvedPath);
		return true;
	}

	return false;
}

bool FAgentInstaller::ResolveExecutableViaLoginShell(const FString& ExecutableName, FString& OutResolvedPath) const
{
#if PLATFORM_WINDOWS
	FString StdOut, StdErr;
	int32 ReturnCode = -1;
	FPlatformProcess::ExecProcess(TEXT("where"), *ExecutableName, &ReturnCode, &StdOut, &StdErr);
	if (ReturnCode == 0 && !StdOut.IsEmpty())
	{
		StdOut.TrimStartAndEndInline();

		TArray<FString> Results;
		StdOut.ParseIntoArrayLines(Results, true);

		if (Results.Num() == 0)
		{
			return false;
		}

		if (Results.Num() == 1)
		{
			OutResolvedPath = Results[0];
			OutResolvedPath.TrimStartAndEndInline();
			return true;
		}

		// Multiple results - prefer CLI wrappers (.cmd, .bat) over desktop apps (.exe)
		FString BestCmdPath;
		FString BestExePath;

		for (const FString& Path : Results)
		{
			FString TrimmedPath = Path;
			TrimmedPath.TrimStartAndEndInline();

			if (TrimmedPath.EndsWith(TEXT(".cmd"), ESearchCase::IgnoreCase) ||
			    TrimmedPath.EndsWith(TEXT(".bat"), ESearchCase::IgnoreCase))
			{
				if (BestCmdPath.IsEmpty())
				{
					BestCmdPath = TrimmedPath;
				}
			}
			else if (TrimmedPath.EndsWith(TEXT(".exe"), ESearchCase::IgnoreCase))
			{
				if (BestExePath.IsEmpty())
				{
					BestExePath = TrimmedPath;
				}
			}
		}

		if (!BestCmdPath.IsEmpty())
		{
			OutResolvedPath = BestCmdPath;
			return true;
		}
		if (!BestExePath.IsEmpty())
		{
			OutResolvedPath = BestExePath;
			return true;
		}

		OutResolvedPath = Results[0];
		OutResolvedPath.TrimStartAndEndInline();
		return true;
	}
	return false;
#else
	FString ShellPath = GetLoginShellPath();
	if (ShellPath.IsEmpty())
	{
		ShellPath = TEXT("/bin/bash");
	}

	FString Command = FString::Printf(TEXT("which %s"), *ExecutableName);
	FString StdOut, StdErr;
	int32 ReturnCode = -1;

	FPlatformProcess::ExecProcess(*ShellPath, *BuildShellCommand(Command), &ReturnCode, &StdOut, &StdErr);

	if (ReturnCode == 0 && !StdOut.IsEmpty())
	{
		StdOut.TrimStartAndEndInline();
		int32 NewlineIndex;
		if (StdOut.FindChar(TEXT('\n'), NewlineIndex))
		{
			OutResolvedPath = StdOut.Left(NewlineIndex);
		}
		else
		{
			OutResolvedPath = StdOut;
		}
		OutResolvedPath.TrimStartAndEndInline();

		if (!OutResolvedPath.IsEmpty() && !OutResolvedPath.Contains(TEXT("not found")))
		{
			UE_LOG(LogAgentIntegrationKit, Verbose, TEXT("AgentInstaller: Login shell resolved %s to %s"), *ExecutableName, *OutResolvedPath);
			return true;
		}
	}

	return false;
#endif
}

// ============================================
// Prerequisites
// ============================================

FPrerequisiteStatus FAgentInstaller::CheckPrerequisites()
{
	FPrerequisiteStatus Status;

	Status.BundledBunPath = GetBundledBunPath();
	Status.bBundledBunAvailable = !Status.BundledBunPath.IsEmpty();

	if (Status.bBundledBunAvailable)
	{
		EnsureBunExecutable();

		FString StdOut, StdErr;
		int32 ReturnCode = -1;
		FPlatformProcess::ExecProcess(*Status.BundledBunPath, TEXT("--version"),
			&ReturnCode, &StdOut, &StdErr);
		if (ReturnCode == 0)
		{
			ParseVersion(StdOut, Status.BundledBunVersion);
		}
	}

	return Status;
}

// ============================================
// Agent Install Info Database
// ============================================

FAgentInstallInfo FAgentInstaller::GetAgentInstallInfo(const FString& AgentName)
{
	TArray<FAgentInstallInfo> AllInfos = GetAllAgentInstallInfos();
	for (const FAgentInstallInfo& Info : AllInfos)
	{
		if (Info.AgentName == AgentName)
		{
			return Info;
		}
	}
	return FAgentInstallInfo();
}

TArray<FAgentInstallInfo> FAgentInstaller::GetAllAgentInstallInfos()
{
	TArray<FAgentInstallInfo> Infos;

	// Claude Code: Requires base CLI + ACP adapter (TypeScript, bundled source + npm fallback)
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("Claude Code");
		Info.BaseExecutableName = TEXT("claude-internal");
		Info.BaseInstallCommand_Mac = TEXT("curl -fsSL https://claude.ai/install.sh | bash");
		Info.BaseInstallCommand_Win = TEXT("powershell -NoProfile -ExecutionPolicy Bypass -Command \"$s=(Invoke-RestMethod https://claude.ai/install.ps1); Invoke-Expression $s\"");
		Info.BaseInstallCommand_Linux = TEXT("curl -fsSL https://claude.ai/install.sh | bash");
		Info.BaseInstallUrl = TEXT("https://docs.anthropic.com/en/docs/claude-code/setup");
		Info.AdapterDirName = TEXT("claude-code-acp");
		Info.AdapterEntryPointFile = TEXT("dist/index.js");
		Info.AdapterNpmPackage = TEXT("@zed-industries/claude-code-acp");
		Info.bAdapterIsBundled = true;
		Info.AuthCheckFile = TEXT(".claude/.credentials.json");
		Infos.Add(Info);
	}

	// Gemini CLI: Google's AI agent with native ACP support via --experimental-acp flag
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("Gemini CLI");
		Info.BaseExecutableName = TEXT("gemini");
		Info.BaseInstallCommand_Mac = TEXT("npm install -g @google/gemini-cli");
		Info.BaseInstallCommand_Win = TEXT("npm install -g @google/gemini-cli");
		Info.BaseInstallCommand_Linux = TEXT("npm install -g @google/gemini-cli");
		Info.BaseInstallUrl = TEXT("https://github.com/google-gemini/gemini-cli");
		// No adapter — Gemini CLI speaks ACP natively with --experimental-acp
		Infos.Add(Info);
	}

	// Codex CLI: bundled native binary ACP adapter + requires Codex base CLI
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("Codex CLI");
		Info.BaseExecutableName = TEXT("codex");
		Info.BaseInstallCommand_Mac = TEXT("brew install --cask codex");
		Info.BaseInstallCommand_Win = TEXT("npm install -g @openai/codex");
		Info.BaseInstallCommand_Linux = TEXT("npm install -g @openai/codex");
		Info.BaseInstallUrl = TEXT("https://github.com/openai/codex");
		Info.AdapterDirName = TEXT("codex-acp");
		Info.bAdapterIsBundled = true;
		Info.bAdapterIsNativeBinary = true;
		Info.AdapterNpmPackage = TEXT("@zed-industries/codex-acp"); // fallback if binary missing
		Info.AuthCheckFile = TEXT(".codex/auth.json");
		Infos.Add(Info);
	}

	// OpenCode: Go binary, self-contained with 'acp' subcommand
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("OpenCode");
		Info.BaseExecutableName = TEXT("opencode");
		Info.BaseInstallCommand_Mac = TEXT("curl -fsSL https://opencode.ai/install | bash");
		Info.BaseInstallCommand_Win = TEXT("npm i -g opencode-ai@latest");
		Info.BaseInstallCommand_Linux = TEXT("curl -fsSL https://opencode.ai/install | bash");
		Info.BaseInstallUrl = TEXT("https://opencode.ai/docs/installation");
		// No adapter — OpenCode is a native binary with built-in ACP
		Infos.Add(Info);
	}

	// Cursor Agent: Requires Cursor Agent CLI + community ACP adapter
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("Cursor Agent");
		Info.BaseExecutableName = TEXT("cursor-agent");
		Info.BaseInstallCommand_Mac = TEXT("curl https://cursor.com/install -fsS | bash");
		Info.BaseInstallCommand_Win = TEXT("curl https://cursor.com/install -fsS | bash");
		Info.BaseInstallCommand_Linux = TEXT("curl https://cursor.com/install -fsS | bash");
		Info.BaseInstallUrl = TEXT("https://docs.cursor.com/en/cli/installation");
		Info.AdapterEntryPointFile = TEXT("dist/index.js");
		Info.AdapterNpmPackage = TEXT("@blowmage/cursor-agent-acp");
		Infos.Add(Info);
	}

	// Kimi CLI: native ACP via "acp" subcommand
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("Kimi CLI");
		Info.BaseExecutableName = TEXT("kimi");
		Info.BaseInstallCommand_Mac = TEXT("curl -LsSf https://code.kimi.com/install.sh | bash");
		Info.BaseInstallCommand_Win = TEXT("powershell -NoProfile -ExecutionPolicy Bypass -Command \"$s=(Invoke-RestMethod https://code.kimi.com/install.ps1); Invoke-Expression $s\"");
		Info.BaseInstallCommand_Linux = TEXT("curl -LsSf https://code.kimi.com/install.sh | bash");
		Info.BaseInstallUrl = TEXT("https://moonshotai.github.io/kimi-cli/en/guides/getting-started.html");
		// No adapter — Kimi CLI speaks ACP with the "acp" subcommand
		Infos.Add(Info);
	}

	// Copilot CLI: GitHub's coding agent with native ACP support via --acp flag
	{
		FAgentInstallInfo Info;
		Info.AgentName = TEXT("Copilot CLI");
		Info.BaseExecutableName = TEXT("copilot");
		Info.BaseInstallCommand_Mac = TEXT("brew install copilot-cli");
		Info.BaseInstallCommand_Win = TEXT("winget install GitHub.Copilot");
		Info.BaseInstallCommand_Linux = TEXT("npm install -g @github/copilot");
		Info.BaseInstallUrl = TEXT("https://docs.github.com/en/copilot/how-tos/set-up/install-copilot-in-the-cli");
		// No adapter — Copilot CLI speaks ACP natively with --acp
		Infos.Add(Info);
	}

	return Infos;
}

// ============================================
// Installation
// ============================================

void FAgentInstaller::InstallAgentAsync(
	const FString& AgentName,
	FOnInstallProgress OnProgress,
	FOnInstallComplete OnComplete)
{
	Async(EAsyncExecution::Thread, [this, AgentName, OnProgress, OnComplete]()
	{
		RunInstallOnBackgroundThread(AgentName, OnProgress, OnComplete);
	});
}

bool FAgentInstaller::InstallBaseCLI(const FAgentInstallInfo& Info, FString& OutError)
{
	if (!Info.RequiresBaseCLI())
	{
		return true;
	}

	FString ResolvedPath;
	if (ResolveExecutable(Info.BaseExecutableName, ResolvedPath))
	{
		return true;
	}

	FString InstallCommand = Info.GetBaseInstallCommand();

#if PLATFORM_MAC || PLATFORM_LINUX
	// Kimi's wrapper script can fail in embedded/non-interactive shells even after downloading uv.
	// Use a deterministic uv-based install path for automatic setup.
	if (Info.BaseExecutableName.Equals(TEXT("kimi"), ESearchCase::IgnoreCase))
	{
		InstallCommand =
			TEXT("export UV_INSTALL_DIR=\"$HOME/.local/bin\"; ")
			TEXT("if [ ! -x \"$HOME/.local/bin/uv\" ]; then curl -LsSf https://astral.sh/uv/install.sh | sh; fi; ")
			TEXT("\"$HOME/.local/bin/uv\" tool install --python 3.13 kimi-cli || ")
			TEXT("\"$HOME/.local/bin/uv\" tool upgrade kimi-cli --no-cache");
	}
#endif

	if (InstallCommand.IsEmpty())
	{
		OutError = FString::Printf(TEXT("Automatic installation is not configured for %s CLI."), *Info.BaseExecutableName);
		return false;
	}
	const FString DisplayInstallCommand = InstallCommand;
	const bool bIsClaudeCli = Info.BaseExecutableName.Equals(TEXT("claude"), ESearchCase::IgnoreCase);
	FString InProcessInstallNote;

	if (bIsClaudeCli)
	{
		const UACPSettings* Settings = UACPSettings::Get();
		const bool bTryInProcess = !Settings || Settings->bInstallClaudeInProcessFirst;
		if (bTryInProcess)
		{
			FString StdOut, StdErr;
			int32 ReturnCode = -1;
			const bool bExecOk = RunShellCommand(InstallCommand, StdOut, StdErr, ReturnCode);
			if (bExecOk && ReturnCode == 0)
			{
				FString DetectedClaudePath;
				if (ResolveInstalledClaudeExecutablePath(DetectedClaudePath))
				{
					AutoSaveClaudeExecutablePath(DetectedClaudePath);
					return true;
				}

				InProcessInstallNote = TEXT("Claude installer ran, but the executable path could not be resolved in this session.");
			}
			else
			{
				FString ErrorDetail = StdErr.IsEmpty() ? StdOut : StdErr;
				if (ErrorDetail.IsEmpty())
				{
					ErrorDetail = bExecOk
						? FString::Printf(TEXT("Installer exited with code %d."), ReturnCode)
						: TEXT("Failed to execute installer command.");
				}
				InProcessInstallNote = FString::Printf(TEXT("In-process install attempt failed: %s"), *ErrorDetail);
			}
		}
	}

	FString ExternalTerminalError;
	if (LaunchExternalInstallTerminal(Info.AgentName, InstallCommand, ExternalTerminalError))
	{
		OutError = FString::Printf(
			TEXT("%s CLI is required. An external terminal installer has been launched.\n\n")
			TEXT("Complete any prompts there (including admin password if requested), then return here and click 'Try Again'.\n\n")
			TEXT("Tried command:\n\n")
			TEXT("  %s"),
			*Info.BaseExecutableName,
			*DisplayInstallCommand
		);
		if (!InProcessInstallNote.IsEmpty())
		{
			OutError += FString::Printf(TEXT("\n\n%s"), *InProcessInstallNote);
		}
		if (!ExternalTerminalError.IsEmpty())
		{
			OutError += FString::Printf(TEXT("\n\nExternal terminal:\n%s"), *ExternalTerminalError);
		}
		if (!Info.BaseInstallUrl.IsEmpty())
		{
			OutError += FString::Printf(TEXT("\n\nDownload page: %s"), *Info.BaseInstallUrl);
		}
		return false;
	}

	OutError = FString::Printf(
		TEXT("%s CLI is required, but failed to launch external installer terminal.\n\n")
		TEXT("Tried command:\n\n")
		TEXT("  %s"),
		*Info.BaseExecutableName,
		*DisplayInstallCommand
	);
	if (!InProcessInstallNote.IsEmpty())
	{
		OutError += FString::Printf(TEXT("\n\n%s"), *InProcessInstallNote);
	}

	if (!ExternalTerminalError.IsEmpty())
	{
		OutError += FString::Printf(TEXT("\n\nError:\n\n%s"), *ExternalTerminalError);
	}

	if (!Info.BaseInstallUrl.IsEmpty())
	{
		OutError += FString::Printf(TEXT("\n\nDownload page: %s"), *Info.BaseInstallUrl);
	}

	OutError += TEXT("\n\nIf needed, run the command manually in an elevated terminal and click 'Try Again'.");
	return false;
}

void FAgentInstaller::RunInstallOnBackgroundThread(
	const FString& AgentName,
	FOnInstallProgress OnProgress,
	FOnInstallComplete OnComplete)
{
	auto NotifyProgress = [OnProgress](const FString& Msg)
	{
		Async(EAsyncExecution::TaskGraphMainThread, [OnProgress, Msg]()
		{
			OnProgress.ExecuteIfBound(Msg);
		});
	};

	auto NotifyComplete = [OnComplete](bool bSuccess, const FString& Msg)
	{
		Async(EAsyncExecution::TaskGraphMainThread, [OnComplete, bSuccess, Msg]()
		{
			OnComplete.ExecuteIfBound(bSuccess, Msg);
		});
	};

	FAgentInstallInfo Info = GetAgentInstallInfo(AgentName);
	if (!Info.RequiresAdapter() && !Info.RequiresBaseCLI())
	{
		NotifyComplete(false, FString::Printf(TEXT("Unknown agent: %s"), *AgentName));
		return;
	}

	// Step 1: Verify bundled Bun (needed for JS adapters)
	if (Info.RequiresAdapter() && !Info.bAdapterIsNativeBinary)
	{
		NotifyProgress(TEXT("Checking bundled runtime..."));
		FString BunPath = GetBundledBunPath();
		if (BunPath.IsEmpty())
		{
			NotifyComplete(false, TEXT("Bundled Bun runtime not found. The plugin installation may be incomplete.\n\nTry reinstalling Agent Integration Kit."));
			return;
		}
		EnsureBunExecutable();
	}

	// Step 2: Ensure adapter is available
	if (Info.RequiresAdapter())
	{
		FString EntryPath = GetAdapterEntryPoint(Info);
		if (!EntryPath.IsEmpty() && Info.bAdapterIsBundled && !Info.bAdapterIsNativeBinary)
		{
			const FString AdapterRoot = FPaths::GetPath(FPaths::GetPath(EntryPath));
			const FString NodeModulesDir = FPaths::Combine(AdapterRoot, TEXT("node_modules"));
			if (!IFileManager::Get().DirectoryExists(*NodeModulesDir))
			{
				UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: Bundled adapter entry exists but dependencies missing at %s; reinstall required"), *NodeModulesDir);
				EntryPath.Empty();
			}
		}
		if (EntryPath.IsEmpty())
		{
			if (Info.bAdapterIsNativeBinary)
			{
				// Native binary adapter — bundled binary missing for this platform
				// Fall back to npm install if package available
				if (!Info.AdapterNpmPackage.IsEmpty())
				{
					NotifyProgress(FString::Printf(TEXT("Installing %s adapter..."), *AgentName));

					FString BunPath = GetBundledBunPath();
					if (BunPath.IsEmpty())
					{
						NotifyComplete(false, TEXT("Bundled Bun runtime not found. Try reinstalling Agent Integration Kit."));
						return;
					}
					EnsureBunExecutable();

					FString ManagedDir = GetManagedAdaptersDir();
					if (ManagedDir.IsEmpty())
					{
						NotifyComplete(false, TEXT("Could not determine home directory for adapter installation."));
						return;
					}

					IFileManager::Get().MakeDirectory(*ManagedDir, true);
					FString PackageJsonPath = FPaths::Combine(ManagedDir, TEXT("package.json"));
					if (!IFileManager::Get().FileExists(*PackageJsonPath))
					{
						FFileHelper::SaveStringToFile(TEXT("{\"name\":\"aik-adapters\",\"private\":true}"), *PackageJsonPath);
					}

					FString StdOut, StdErr;
					int32 ReturnCode = -1;
					FString AddArgs = FString::Printf(TEXT("add \"%s\" --cwd \"%s\""), *Info.AdapterNpmPackage, *ManagedDir);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: Running: %s %s"), *BunPath, *AddArgs);
					FPlatformProcess::ExecProcess(*BunPath, *AddArgs, &ReturnCode, &StdOut, &StdErr);

					UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: bun add %s returned %d"), *Info.AdapterNpmPackage, ReturnCode);
					if (ReturnCode != 0)
					{
						FString ErrorDetail = StdErr.IsEmpty() ? StdOut : StdErr;
						if (ErrorDetail.IsEmpty())
						{
							ErrorDetail = FString::Printf(TEXT("bun add exited with code %d (no output).\n\nBun path: %s"), ReturnCode, *BunPath);
						}
						NotifyComplete(false, FString::Printf(TEXT("Failed to install %s adapter:\n\n%s"), *AgentName, *ErrorDetail));
						return;
					}
				}
				else
				{
					NotifyComplete(false, FString::Printf(TEXT("Native adapter binary not found for %s on this platform."), *AgentName));
					return;
				}
			}
			else
			{
				// JS adapter
				FString BunPath = GetBundledBunPath();

				if (Info.bAdapterIsBundled && !Info.AdapterDirName.IsEmpty())
				{
					// Bundled adapter source exists — build it
					NotifyProgress(FString::Printf(TEXT("Building %s adapter..."), *Info.AdapterDirName));

					FString BundledDir = GetBundledAdaptersDir();
					FString SourceAdapterDir = FPaths::Combine(BundledDir, Info.AdapterDirName);

					// Validate adapter directory exists
					if (!IFileManager::Get().DirectoryExists(*SourceAdapterDir))
					{
						NotifyComplete(false, FString::Printf(TEXT("Adapter source directory not found:\n%s\n\nTry reinstalling Agent Integration Kit."), *SourceAdapterDir));
						return;
					}

					FString AdapterDir = GetManagedBundledAdapterDir(Info);
					if (AdapterDir.IsEmpty())
					{
						NotifyComplete(false, TEXT("Could not determine writable adapter build directory in home folder."));
						return;
					}

					IFileManager::Get().MakeDirectory(*FPaths::GetPath(AdapterDir), true);
					if (!IFileManager::Get().DirectoryExists(*AdapterDir))
					{
						IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
						if (!PlatformFile.CopyDirectoryTree(*AdapterDir, *SourceAdapterDir, true))
						{
							NotifyComplete(false, FString::Printf(
								TEXT("Failed to copy bundled adapter to writable location:\nSource: %s\nTarget: %s"),
								*SourceAdapterDir, *AdapterDir));
							return;
						}
					}

						FString StdOut, StdErr;
						int32 ReturnCode = -1;

						FString InstallArgs = FString::Printf(TEXT("install --cwd \"%s\""), *AdapterDir);
						UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: Running: %s %s"), *BunPath, *InstallArgs);
						DeleteBunLockfiles(AdapterDir);
						FPlatformProcess::ExecProcess(*BunPath, *InstallArgs, &ReturnCode, &StdOut, &StdErr);

						UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: bun install returned %d, stdout=%d chars, stderr=%d chars"), ReturnCode, StdOut.Len(), StdErr.Len());
						if (ReturnCode != 0)
						{
							FString ErrorDetail = StdErr.IsEmpty() ? StdOut : StdErr;
							if (IsBunLockfileReplaceError(ErrorDetail))
							{
								NotifyProgress(TEXT("Recovering from Bun lockfile error and retrying..."));
								UE_LOG(LogAgentIntegrationKit, Warning, TEXT("AgentInstaller: bun install lockfile error detected, resetting adapter artifacts in %s"), *AdapterDir);

								ResetAdapterInstallArtifacts(AdapterDir);
								StdOut.Empty();
								StdErr.Empty();
								ReturnCode = -1;
								FPlatformProcess::ExecProcess(*BunPath, *InstallArgs, &ReturnCode, &StdOut, &StdErr);

								UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: bun install retry returned %d, stdout=%d chars, stderr=%d chars"), ReturnCode, StdOut.Len(), StdErr.Len());
								if (ReturnCode != 0)
								{
									ErrorDetail = StdErr.IsEmpty() ? StdOut : StdErr;
								}
							}

							if (ReturnCode != 0)
							{
								if (ErrorDetail.IsEmpty())
								{
									ErrorDetail = FString::Printf(TEXT("bun install exited with code %d (no output).\n\nBun path: %s\nAdapter dir: %s"), ReturnCode, *BunPath, *AdapterDir);
								}
								NotifyComplete(false, FString::Printf(
									TEXT("Failed to install adapter dependencies:\n\n%s\n\nAdapter dir: %s\n\nIf this persists, close Unreal and delete the adapter cache folder above, then try setup again."),
									*ErrorDetail, *AdapterDir));
								return;
							}
						}

					StdOut.Empty(); StdErr.Empty(); ReturnCode = -1;
					FString BuildArgs = FString::Printf(TEXT("run --cwd \"%s\" build"), *AdapterDir);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: Running: %s %s"), *BunPath, *BuildArgs);
					FPlatformProcess::ExecProcess(*BunPath, *BuildArgs, &ReturnCode, &StdOut, &StdErr);

					UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: bun run build returned %d, stdout=%d chars, stderr=%d chars"), ReturnCode, StdOut.Len(), StdErr.Len());
					if (ReturnCode != 0)
					{
						FString ErrorDetail = StdErr.IsEmpty() ? StdOut : StdErr;
						if (ErrorDetail.IsEmpty())
						{
							ErrorDetail = FString::Printf(TEXT("bun run build exited with code %d (no output).\n\nBun path: %s\nAdapter dir: %s"), ReturnCode, *BunPath, *AdapterDir);
						}
						NotifyComplete(false, FString::Printf(TEXT("Failed to build adapter:\n\n%s"), *ErrorDetail));
						return;
					}
				}
				else if (!Info.AdapterNpmPackage.IsEmpty())
				{
					// Install from npm into managed directory
					NotifyProgress(FString::Printf(TEXT("Installing %s adapter..."), *AgentName));

					FString ManagedDir = GetManagedAdaptersDir();
					if (ManagedDir.IsEmpty())
					{
						NotifyComplete(false, TEXT("Could not determine home directory for adapter installation."));
						return;
					}

					IFileManager::Get().MakeDirectory(*ManagedDir, true);
					FString PackageJsonPath = FPaths::Combine(ManagedDir, TEXT("package.json"));
					if (!IFileManager::Get().FileExists(*PackageJsonPath))
					{
						FFileHelper::SaveStringToFile(TEXT("{\"name\":\"aik-adapters\",\"private\":true}"), *PackageJsonPath);
					}

					FString StdOut, StdErr;
					int32 ReturnCode = -1;
					FString AddArgs = FString::Printf(TEXT("add \"%s\" --cwd \"%s\""), *Info.AdapterNpmPackage, *ManagedDir);
					UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: Running: %s %s"), *BunPath, *AddArgs);
					FPlatformProcess::ExecProcess(*BunPath, *AddArgs, &ReturnCode, &StdOut, &StdErr);

					UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: bun add %s returned %d"), *Info.AdapterNpmPackage, ReturnCode);
					if (!StdErr.IsEmpty())
					{
						UE_LOG(LogAgentIntegrationKit, Log, TEXT("AgentInstaller: stderr: %s"), *StdErr);
					}

					if (ReturnCode != 0)
					{
						FString ErrorDetail = StdErr.IsEmpty() ? StdOut : StdErr;
						if (ErrorDetail.IsEmpty())
						{
							ErrorDetail = FString::Printf(TEXT("bun add exited with code %d (no output).\n\nBun path: %s"), ReturnCode, *BunPath);
						}
						NotifyComplete(false, FString::Printf(TEXT("Failed to install %s adapter:\n\n%s"), *AgentName, *ErrorDetail));
						return;
					}
				}
			}

			// Re-check entry point
			EntryPath = GetAdapterEntryPoint(Info);
			if (EntryPath.IsEmpty())
			{
				NotifyComplete(false, FString::Printf(TEXT("Adapter installed but entry point not found for %s."), *AgentName));
				return;
			}
		}

		if (Info.bAdapterIsNativeBinary)
		{
			NotifyProgress(TEXT("Preparing native adapter binary..."));
			if (!EnsureNativeAdapterExecutable(EntryPath))
			{
				NotifyComplete(false, FString::Printf(
					TEXT("%s adapter binary exists but is not executable.\n\nPath: %s\n\nTry reinstalling Agent Integration Kit or fix file permissions."),
					*AgentName, *EntryPath));
				return;
			}
		}
	}

	// Step 3: Check and install base CLI if required
	if (Info.RequiresBaseCLI())
	{
		FString ResolvedPath;
		if (!ResolveExecutable(Info.BaseExecutableName, ResolvedPath))
		{
			NotifyProgress(FString::Printf(TEXT("Installing %s CLI..."), *Info.BaseExecutableName));

			FString Error;
			if (!InstallBaseCLI(Info, Error))
			{
				NotifyComplete(false, Error);
				return;
			}
		}
	}

	// Clear cache
	{
		FScopeLock Lock(&CacheLock);
		ResolvedPathCache.Empty();
	}

	if (UACPSettings* Settings = UACPSettings::Get())
	{
		Settings->InvalidateAgentStatusCache();
	}

	NotifyComplete(true, FString());
}

// ============================================
// Utilities
// ============================================

bool FAgentInstaller::LaunchExternalInstallTerminal(const FString& AgentName, const FString& Command, FString& OutError) const
{
	OutError.Empty();
	if (Command.IsEmpty())
	{
		OutError = TEXT("No install command provided.");
		return false;
	}

	const FString ScriptRootDir = FPaths::Combine(FPlatformProcess::UserTempDir(), TEXT("AgentIntegrationKit"), TEXT("installers"));
	IFileManager::Get().MakeDirectory(*ScriptRootDir, true);

	const FString SafeAgent = MakeSafeFileStem(AgentName.IsEmpty() ? TEXT("agent") : AgentName);
	const FString UniqueSuffix = FString::Printf(TEXT("%lld"), FDateTime::UtcNow().GetTicks());

	FString ScriptPath;
	FString ScriptContent;
	FString LaunchExe;
	FString LaunchArgs;

#if PLATFORM_WINDOWS
	ScriptPath = FPaths::Combine(ScriptRootDir, FString::Printf(TEXT("install-%s-%s.bat"), *SafeAgent, *UniqueSuffix));
	FPaths::MakePlatformFilename(ScriptPath);
	const FString WinAgentName = AgentName.IsEmpty() ? TEXT("Agent CLI") : AgentName;

	ScriptContent += TEXT("@echo off\r\n");
	ScriptContent += TEXT("echo.\r\n");
	ScriptContent += TEXT("echo ============================================\r\n");
	ScriptContent += TEXT("echo   Agent Integration Kit - CLI Installer\r\n");
	ScriptContent += TEXT("echo ============================================\r\n");
	ScriptContent += FString::Printf(TEXT("echo Agent: %s\r\n"), *WinAgentName);
	ScriptContent += TEXT("echo.\r\n");
	ScriptContent += TEXT("echo Running install command...\r\n");
	ScriptContent += Command + TEXT("\r\n");
	ScriptContent += TEXT("echo.\r\n");
	ScriptContent += TEXT("if %ERRORLEVEL% neq 0 (\r\n");
	ScriptContent += TEXT("  echo Install command returned error %ERRORLEVEL%.\r\n");
	ScriptContent += TEXT("  echo If needed, rerun this command in an Administrator terminal.\r\n");
	ScriptContent += TEXT(")\r\n");
	ScriptContent += TEXT("echo Return to Unreal and click Try Again.\r\n");
	ScriptContent += TEXT("echo.\r\n");
	ScriptContent += TEXT("pause\r\n");

	LaunchExe = TEXT("cmd.exe");
	LaunchArgs = FString::Printf(TEXT("/c start \"AIK Installer\" cmd.exe /k \"\"%s\"\""), *ScriptPath);
#elif PLATFORM_MAC || PLATFORM_LINUX
	ScriptPath = FPaths::Combine(ScriptRootDir, FString::Printf(TEXT("install-%s-%s.sh"), *SafeAgent, *UniqueSuffix));
	const FString UnixAgentName = AgentName.IsEmpty() ? FString(TEXT("Agent CLI")) : AgentName;

	ScriptContent += TEXT("#!/bin/bash\n");
	ScriptContent += TEXT("echo\n");
	ScriptContent += TEXT("echo '============================================'\n");
	ScriptContent += TEXT("echo '  Agent Integration Kit - CLI Installer'\n");
	ScriptContent += TEXT("echo '============================================'\n");
	ScriptContent += FString::Printf(TEXT("echo 'Agent: %s'\n"), *UnixAgentName);
	ScriptContent += TEXT("echo\n");
	ScriptContent += TEXT("INSTALL_CMD=$(cat <<'AIK_INSTALL_CMD'\n");
	ScriptContent += Command + TEXT("\n");
	ScriptContent += TEXT("AIK_INSTALL_CMD\n");
	ScriptContent += TEXT(")\n");
	ScriptContent += TEXT("echo 'Running install command...'\n");
	ScriptContent += TEXT("/bin/bash -lc \"$INSTALL_CMD\"\n");
	ScriptContent += TEXT("EXIT_CODE=$?\n");
	ScriptContent += TEXT("if [ $EXIT_CODE -ne 0 ]; then\n");
	ScriptContent += TEXT("  echo\n");
	ScriptContent += TEXT("  echo 'Install command failed. Attempting elevated retry (sudo)...'\n");
	ScriptContent += TEXT("  if command -v sudo >/dev/null 2>&1; then\n");
	ScriptContent += TEXT("    sudo /bin/bash -lc \"$INSTALL_CMD\"\n");
	ScriptContent += TEXT("    EXIT_CODE=$?\n");
	ScriptContent += TEXT("  fi\n");
	ScriptContent += TEXT("fi\n");
	ScriptContent += TEXT("echo\n");
	ScriptContent += TEXT("if [ $EXIT_CODE -eq 0 ]; then\n");
	ScriptContent += TEXT("  echo 'Install command finished successfully.'\n");
	ScriptContent += TEXT("else\n");
	ScriptContent += TEXT("  echo \"Install command failed (exit $EXIT_CODE).\"\n");
	ScriptContent += TEXT("fi\n");
	ScriptContent += TEXT("echo 'Return to Unreal and click Try Again.'\n");
	ScriptContent += TEXT("echo\n");
	ScriptContent += TEXT("read -r -p 'Press Enter to close...' _\n");

	FPaths::NormalizeFilename(ScriptPath);
#if PLATFORM_MAC
	LaunchExe = TEXT("/usr/bin/open");
	LaunchArgs = FString::Printf(TEXT("-a Terminal \"%s\""), *ScriptPath);
#else
	// Best-effort launch for common Linux terminal apps.
	const FString LinuxLaunch = FString::Printf(
		TEXT("if command -v x-terminal-emulator >/dev/null 2>&1; then x-terminal-emulator -e \"%s\"; ")
		TEXT("elif command -v gnome-terminal >/dev/null 2>&1; then gnome-terminal -- \"%s\"; ")
		TEXT("elif command -v konsole >/dev/null 2>&1; then konsole -e \"%s\"; ")
		TEXT("elif command -v xterm >/dev/null 2>&1; then xterm -e \"%s\"; ")
		TEXT("else exit 127; fi"),
		*ScriptPath, *ScriptPath, *ScriptPath, *ScriptPath);
	LaunchExe = TEXT("/bin/bash");
	LaunchArgs = BuildShellCommand(LinuxLaunch);
#endif
#else
	OutError = TEXT("External terminal install is not supported on this platform.");
	return false;
#endif

	if (!FFileHelper::SaveStringToFile(ScriptContent, *ScriptPath))
	{
		OutError = FString::Printf(TEXT("Failed to write installer script: %s"), *ScriptPath);
		return false;
	}

#if PLATFORM_MAC || PLATFORM_LINUX
	FPlatformProcess::ExecProcess(TEXT("/bin/chmod"), *FString::Printf(TEXT("+x \"%s\""), *ScriptPath), nullptr, nullptr, nullptr);
#endif

	FProcHandle ProcHandle = FPlatformProcess::CreateProc(
		*LaunchExe, *LaunchArgs,
		true,   // detached
		false,  // hidden
		false,  // really hidden
		nullptr, 0, nullptr, nullptr);

	if (!ProcHandle.IsValid())
	{
		OutError = FString::Printf(TEXT("Failed to launch external installer terminal: %s %s"), *LaunchExe, *LaunchArgs);
		return false;
	}

	OutError = FString::Printf(TEXT("Launched external installer terminal using script:\n%s"), *ScriptPath);
	return true;
}

FString FAgentInstaller::GetLoginShellPath() const
{
#if PLATFORM_MAC || PLATFORM_LINUX
	FString Shell = FPlatformMisc::GetEnvironmentVariable(TEXT("SHELL"));
	if (!Shell.IsEmpty())
	{
		return Shell;
	}
#endif
	return FString();
}

FString FAgentInstaller::BuildShellCommand(const FString& Command) const
{
#if PLATFORM_WINDOWS
	return FString::Printf(TEXT("/c %s"), *Command);
#else
	// Ensure user-local install locations are immediately discoverable by installer scripts.
	const FString CommandWithPathBootstrap = FString::Printf(
		TEXT("export PATH=\"$HOME/.local/bin:$HOME/.cargo/bin:$HOME/.bun/bin:$PATH\"; %s"),
		*Command
	);

	FString Escaped = CommandWithPathBootstrap;
	Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
	Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
	Escaped.ReplaceInline(TEXT("`"), TEXT("\\`"));
	return FString::Printf(TEXT("-l -c \"%s\""), *Escaped);
#endif
}

bool FAgentInstaller::RunShellCommand(const FString& Command, FString& OutStdOut, FString& OutStdErr, int32& OutReturnCode) const
{
#if PLATFORM_WINDOWS
	// Prefer PowerShell so script-style commands (Invoke-RestMethod, pipes, etc.) work.
	FString PsCommand = Command;
	PsCommand.ReplaceInline(TEXT("\""), TEXT("`\""));
	const FString PsArgs = FString::Printf(TEXT("-NoProfile -ExecutionPolicy Bypass -Command \"%s\""), *PsCommand);

	if (FPlatformProcess::ExecProcess(TEXT("powershell.exe"), *PsArgs, &OutReturnCode, &OutStdOut, &OutStdErr))
	{
		if (OutReturnCode == 0)
		{
			return true;
		}
	}

	OutStdOut.Empty();
	OutStdErr.Empty();
	OutReturnCode = -1;
	return FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *FString::Printf(TEXT("/c %s"), *Command), &OutReturnCode, &OutStdOut, &OutStdErr);
#else
	FString ShellPath = GetLoginShellPath();
	if (ShellPath.IsEmpty())
	{
		ShellPath = TEXT("/bin/bash");
	}
	return FPlatformProcess::ExecProcess(*ShellPath, *BuildShellCommand(Command), &OutReturnCode, &OutStdOut, &OutStdErr);
#endif
}

bool FAgentInstaller::ParseVersion(const FString& VersionOutput, FString& OutVersion) const
{
	FString Trimmed = VersionOutput;
	Trimmed.TrimStartAndEndInline();

	if (Trimmed.StartsWith(TEXT("v")))
	{
		OutVersion = Trimmed.Mid(1);
	}
	else
	{
		OutVersion = Trimmed;
	}

	int32 NewlineIndex;
	if (OutVersion.FindChar(TEXT('\n'), NewlineIndex))
	{
		OutVersion = OutVersion.Left(NewlineIndex);
	}

	return !OutVersion.IsEmpty();
}
