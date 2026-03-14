// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;

public class TowerDefendEditorTarget : TargetRules
{
	public TowerDefendEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.V6;
		IncludeOrderVersion = EngineIncludeOrderVersion.Unreal5_7;
		BuildEnvironment = TargetBuildEnvironment.Unique;
		ExtraModuleNames.Add("TowerDefend");
	}
}
