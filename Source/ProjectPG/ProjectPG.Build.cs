// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectPG : ModuleRules
{
	public ProjectPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "WebSockets", "Json", "JsonUtilities",
			"AIModule", "GameplayTags", "GameplayTasks", "GameplayAbilities", "Slate", "SlateCore", "NavigationSystem",
			"PCG", "Landscape"
		});

		// PG.BuildShoreMeshes 같은 에디터 전용 에셋 생성 명령용.
		if (Target.bBuildEditor == true)
		{
			PrivateDependencyModuleNames.AddRange(new string[]
			{
				"GeometryCore",
				"GeometryFramework",
				"GeometryScriptingCore",
				"GeometryScriptingEditor",
				"EditorScriptingUtilities",
				"UnrealEd",
			});
		}
		// Wwise 이벤트” 타입이라, 서버 빌드에서 그 타입을 몰라 컴파일이 깨짐
		// 링크는 하고, "서버에서는 소리 안 냄으로."
		PrivateDependencyModuleNames.Add("AkAudio");
		PrivateIncludePaths.Add(ModuleDirectory);

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}