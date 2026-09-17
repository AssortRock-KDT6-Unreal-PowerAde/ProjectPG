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
			"AIModule", "GameplayTags", "GameplayTasks", "GameplayAbilities", "Slate", "SlateCore", "NavigationSystem","PCG","Landscape"
        });

		PrivateDependencyModuleNames.AddRange(new string[] { });
		
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