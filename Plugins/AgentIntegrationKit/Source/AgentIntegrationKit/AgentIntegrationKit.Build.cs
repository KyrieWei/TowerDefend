// Copyright 2025-2026 Betide Studio. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.IO;
using UnrealBuildTool;

public class AgentIntegrationKit : ModuleRules
{
	public AgentIntegrationKit(ReadOnlyTargetRules Target) : base(Target)
	{
		// Optional integrations are enabled only when the backing engine/project plugin exists
		// and the target does not explicitly disable it.
		bool bWithPoseSearch = IsOptionalPluginAvailable(Target, "PoseSearch");
		bool bWithStructUtils = IsOptionalPluginAvailable(Target, "StructUtils");
		bool bWithCommonUI = IsOptionalPluginAvailable(Target, "CommonUI");

		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"Slate",
			"SlateCore",
			"InputCore",
			"Json",
			"JsonUtilities",
			"HTTP",
			"HTTPServer",
			"RenderCore",
			"RHI",
			"Renderer"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"EditorStyle",
			"ToolMenus",
			"WorkspaceMenuStructure",
			"Projects",
			"DeveloperSettings",
			"ApplicationCore",
			"EditorFramework",
			// Blueprint tools dependencies
			"AssetTools",
			"AssetRegistry",
			"Kismet",
			"KismetCompiler",
			"BlueprintGraph",
			"UMG",
			"UMGEditor",
			// NeoStack tools dependencies
			"AIModule",
			"AIGraph",
			"AnimGraph",
			"AnimGraphRuntime",
			"MaterialEditor",
			"GraphEditor",
			"PhysicsCore",
			"PhysicsUtilities",
			// Context attachment dependencies
			"ContentBrowser",
			"DesktopPlatform",
			// Settings detail customization
			"PropertyEditor",
			"ImageWrapper",
			// WebBrowser for embedded web UI
			"WebBrowser",
			// Level Editor tab management (viewport activation)
			"LevelEditor",
			// Python scripting
			"PythonScriptPlugin",
			// Logging and diagnostics
			"MessageLog",
			// Niagara VFX support
			"Niagara",
			"NiagaraEditor",
			// Behavior Tree editor graph support
			"BehaviorTreeEditor",
			// Environment Query System editor support (conditionally added below for UE 5.6+)
			// Level Sequence / Cinematics support
			"LevelSequence",
			"MovieScene",
			"MovieSceneTracks",
			// IK Rig / Control Rig support
			"IKRig",
			"IKRigEditor",
			"ControlRig",
			"ControlRigDeveloper",
			"RigVM",
			"RigVMDeveloper",
			"PBIK",
			"AssetDefinition",
			// MetaSound support
			"MetasoundEngine",
			"MetasoundFrontend",
			"MetasoundEditor",
			// Chooser / ChooserTable support
			"Chooser",
			// Enhanced Input support
			"EnhancedInput",
			// Asset management (UEditorAssetLibrary)
			"EditorScriptingUtilities",
			// Gameplay Ability System support
			"GameplayAbilities"
		});

		// Environment Query System editor support - only available in UE 5.6+
		if (Target.Version.MinorVersion >= 6)
		{
			PrivateDependencyModuleNames.Add("EnvironmentQueryEditor");
		}

		// PCG (Procedural Content Generation) support - only available in UE 5.7+
		if (Target.Version.MinorVersion >= 7)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"PCG",
				"PCGEditor"
			});
		}

		// StateTree support
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"StateTreeModule",
			"StateTreeEditorModule",
			"GameplayTags",
			"GameplayTagsEditor",
			"PropertyBindingUtils"
		});

		// Optional: Pose Search / Motion Matching (set bWithPoseSearch = false if unavailable)
		if (bWithPoseSearch)
		{
			PrivateDependencyModuleNames.Add("PoseSearch");
			PrivateDependencyModuleNames.Add("PoseSearchEditor");
		}
		PublicDefinitions.Add("WITH_POSE_SEARCH=" + (bWithPoseSearch ? "1" : "0"));

		// Optional: StructUtils / PropertyBag (set bWithStructUtils = false if unavailable)
		if (bWithStructUtils)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
		PublicDefinitions.Add("WITH_STRUCT_UTILS=" + (bWithStructUtils ? "1" : "0"));

		// Optional: CommonUI (set bWithCommonUI = false if unavailable)
		if (bWithCommonUI)
		{
			PrivateDependencyModuleNames.Add("CommonUI");
		}
		PublicDefinitions.Add("WITH_COMMON_UI=" + (bWithCommonUI ? "1" : "0"));

		// Source control integration (branch info, changelists UI)
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"SourceControl",
			"SourceControlWindows"
		});

		// Live Coding support (Windows only)
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
		}

		// AppKit framework for clipboard image reading (macOS)
		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			PublicFrameworks.Add("AppKit");
		}

		// Bundled Bun runtime for ACP adapter execution (Source/ThirdParty survives Binaries rebuild)
		string BunPath = Path.Combine(PluginDirectory, "Source", "ThirdParty", "Bun");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			RuntimeDependencies.Add(Path.Combine(BunPath, "Win64", "bun.exe"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			// Both architectures — editor may run native arm64 or under Rosetta x64
			RuntimeDependencies.Add(Path.Combine(BunPath, "Mac-arm64", "bun"));
			RuntimeDependencies.Add(Path.Combine(BunPath, "Mac-x64", "bun"));
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			RuntimeDependencies.Add(Path.Combine(BunPath, "Linux-x64", "bun"));
		}

		// Bundled ACP adapter: claude-code-acp (TypeScript, runs via Bun)
		string ClaudeAdapterPath = Path.Combine(PluginDirectory, "Source", "ThirdParty", "Adapters", "claude-code-acp");
		if (Directory.Exists(ClaudeAdapterPath))
		{
			foreach (string FilePath in Directory.EnumerateFiles(ClaudeAdapterPath, "*", SearchOption.AllDirectories))
			{
				RuntimeDependencies.Add(FilePath);
			}
		}

		// Bundled ACP adapter: codex-acp (Rust native binary, per-platform)
		string CodexAdapterBinPath = Path.Combine(PluginDirectory, "Source", "ThirdParty", "Adapters", "codex-acp", "bin");
		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			string CodexBinary = Path.Combine(CodexAdapterBinPath, "win32-x64", "codex-acp.exe");
			if (File.Exists(CodexBinary))
				RuntimeDependencies.Add(CodexBinary);
		}
		else if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			string CodexArm64 = Path.Combine(CodexAdapterBinPath, "darwin-arm64", "codex-acp");
			string CodexX64 = Path.Combine(CodexAdapterBinPath, "darwin-x64", "codex-acp");
			if (File.Exists(CodexArm64))
				RuntimeDependencies.Add(CodexArm64);
			if (File.Exists(CodexX64))
				RuntimeDependencies.Add(CodexX64);
		}
		else if (Target.Platform == UnrealTargetPlatform.Linux)
		{
			string CodexBinary = Path.Combine(CodexAdapterBinPath, "linux-x64", "codex-acp");
			if (File.Exists(CodexBinary))
				RuntimeDependencies.Add(CodexBinary);
		}
	}

	private bool IsOptionalPluginAvailable(ReadOnlyTargetRules Target, string PluginName)
	{
		if (IsPluginExplicitlyDisabled(Target, PluginName))
		{
			return false;
		}

		return IsPluginDescriptorAvailable(Target, PluginName);
	}

	private static bool IsPluginExplicitlyDisabled(ReadOnlyTargetRules Target, string PluginName)
	{
		foreach (string DisabledPlugin in Target.DisablePlugins)
		{
			if (DisabledPlugin.Equals(PluginName, StringComparison.OrdinalIgnoreCase))
			{
				return true;
			}
		}
		return false;
	}

	private bool IsPluginDescriptorAvailable(ReadOnlyTargetRules Target, string PluginName)
	{
		foreach (string CandidatePath in GetLikelyPluginDescriptorPaths(Target, PluginName))
		{
			if (File.Exists(CandidatePath))
			{
				return true;
			}
		}

		// Fallback for custom engine/plugin layouts.
		string EnginePluginsDir = Path.Combine(EngineDirectory, "Plugins");
		if (ContainsPluginDescriptorRecursive(EnginePluginsDir, PluginName))
		{
			return true;
		}

		if (Target.ProjectFile != null)
		{
			string ProjectDir = Path.GetDirectoryName(Target.ProjectFile.FullName);
			if (!string.IsNullOrEmpty(ProjectDir))
			{
				string ProjectPluginsDir = Path.Combine(ProjectDir, "Plugins");
				if (ContainsPluginDescriptorRecursive(ProjectPluginsDir, PluginName))
				{
					return true;
				}
			}
		}

		return false;
	}

	private IEnumerable<string> GetLikelyPluginDescriptorPaths(ReadOnlyTargetRules Target, string PluginName)
	{
		// Known engine plugin locations for optional dependencies we gate in this module.
		if (PluginName.Equals("PoseSearch", StringComparison.OrdinalIgnoreCase))
		{
			yield return Path.Combine(EngineDirectory, "Plugins", "Animation", "PoseSearch", "PoseSearch.uplugin");
		}
		else if (PluginName.Equals("StructUtils", StringComparison.OrdinalIgnoreCase))
		{
			yield return Path.Combine(EngineDirectory, "Plugins", "Experimental", "StructUtils", "StructUtils.uplugin");
		}
		else if (PluginName.Equals("CommonUI", StringComparison.OrdinalIgnoreCase))
		{
			yield return Path.Combine(EngineDirectory, "Plugins", "Runtime", "CommonUI", "CommonUI.uplugin");
		}

		if (Target.ProjectFile != null)
		{
			string ProjectDir = Path.GetDirectoryName(Target.ProjectFile.FullName);
			if (!string.IsNullOrEmpty(ProjectDir))
			{
				yield return Path.Combine(ProjectDir, "Plugins", PluginName, $"{PluginName}.uplugin");
			}
		}
	}

	private static bool ContainsPluginDescriptorRecursive(string RootDir, string PluginName)
	{
		if (!Directory.Exists(RootDir))
		{
			return false;
		}

		try
		{
			string TargetFile = $"{PluginName}.uplugin";
			foreach (string _ in Directory.EnumerateFiles(RootDir, TargetFile, SearchOption.AllDirectories))
			{
				return true;
			}
		}
		catch (Exception)
		{
			return false;
		}

		return false;
	}
}
