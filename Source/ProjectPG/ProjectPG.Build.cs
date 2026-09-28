// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class ProjectPG : ModuleRules
{
	public ProjectPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "NavigationSystem", "AIModule", "GameplayTasks", "ChaosVehicles", "PCG", "DeveloperSettings", "UMG", "Slate", "SlateCore",
			// 팀 코드(인벤토리·UI·서버·GAS) 합치기 2026-09-19
			"WebSockets", "Json", "JsonUtilities", "GameplayTags", "GameplayAbilities" });

		PrivateDependencyModuleNames.AddRange(new string[] { "Landscape", "RenderCore", "RHI" }); // RenderCore·RHI: PG.PerfSweep 이 GPU·스레드 시간을 읽는다
		// Media·MediaAssets: 타이틀 배경 영상(Content/Movies/PG_TitleLoop.mp4 가 있을 때만 재생). 엔진 기본 모듈이라 플러그인 설정은 필요 없다.
		PrivateDependencyModuleNames.AddRange(new string[] { "Media", "MediaAssets" });
		// 몬스터 AI 의 StateTree 방식(2026-09-23). StateTreeModule = 나무 실행, GameplayStateTreeModule = AI 컨트롤러용 부품.
		PublicDependencyModuleNames.AddRange(new string[] { "StateTreeModule", "GameplayStateTreeModule" });
		// PG.BuildMonsterStateTree 가 나무 에셋(ST_PGMonster)을 코드로 짓고 컴파일한다 — 에디터에서만.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("StateTreeEditorModule");
		}

		// Shoreline meshes are generated, not authored: PG.BuildShoreMeshes builds a
		// height field per shore variant and writes it out as a static mesh asset.
		// GeometryScriptingEditor owns the asset-writing half, so it is editor only.
		if (Target.bBuildEditor)
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
	}
}
