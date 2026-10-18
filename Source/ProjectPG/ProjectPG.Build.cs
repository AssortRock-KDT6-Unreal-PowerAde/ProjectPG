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
			"PCG", "Landscape", "UMG", "StateTreeModule", "GameplayStateTreeModule",
			// 소리(Audio/): 프로젝트 설정 칸, 바닥 재질 종류
			"DeveloperSettings", "PhysicsCore"
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
				"StateTreeEditorModule", // PGBuildEnemyAI 명령: 몬스터 StateTree 에셋을 코드로 만들 때만 씀
				"UnrealEd"
			});
		}

		PrivateIncludePaths.Add(ModuleDirectory);

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		// Uncomment if you are using online features
		// PrivateDependencyModuleNames.Add("OnlineSubsystem");

		// To include OnlineSubsystemSteam, add it to the plugins section in your uproject file with the Enabled attribute set to true
	}
}
