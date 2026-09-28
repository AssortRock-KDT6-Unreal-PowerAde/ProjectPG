// PG.BuildMonsterStateTree — 몬스터 StateTree 에셋(/Game/PG/AI/ST_PGMonster)을 코드로 짓고 컴파일해 저장한다. (2026-09-23, 에디터 전용)
//
// 왜 코드로 짓나: 나무 모양을 글로 남겨 두면 누가 언제 다시 지어도 같은 나무가 나온다(시험·비교의 조건이 흔들리지 않는다).
//   다 지은 뒤에는 에디터에서 ST_PGMonster 를 열어 그림으로 보고 고쳐도 된다 — 단, 이 명령을 다시 돌리면 코드 모양으로 덮어쓴다.
// 실행: 에디터 콘솔 PG.BuildMonsterStateTree, 또는 에디터를 끈 채
//   UnrealEditor-Cmd.exe <uproject> -run=pythonscript -script="E:/ProjectTest2/Tools/build_monster_statetree.py" -unattended -nosplash
#include "CoreMinimal.h"

#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Monster/AI/StateTree/PGMonsterStateTreeTasks.h"
#include "Objects/PGObjectTypes.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#include "UObject/Package.h"

namespace PGMonsterStateTreeBuilder
{
	static const TCHAR* PackagePath = TEXT("/Game/PG/AI/ST_PGMonster");
	static const TCHAR* AssetName = TEXT("ST_PGMonster");

	// 나무 모양은 PGMonsterStateTreeTasks.h 맨 위 그림과 같다. 순서 = 우선순위(코드 방식의 if 순서).
	static void BuildTree(UStateTreeEditorData& EditorData)
	{
		UStateTreeState& Root = EditorData.AddSubTree(FName(TEXT("Root")));

		// 각 칸의 과제가 끝나면(Succeeded) Root 로 돌아가 위에서부터 다시 고른다.
		auto BackToRoot = [&Root](UStateTreeState& State)
		{
			State.AddTransition(EStateTreeTransitionTrigger::OnStateCompleted, EStateTreeTransitionType::GotoState, &Root);
		};

		// 한가한 칸(집에 가기·잠복·쉬기)은 0.25초마다만 생각한다 — 코드 방식의 "표적이 없거나 잠복이면 0.25초" 와 같은 규칙.
		// 엔진 StateTree 부품은 틱 간격을 스스로 정해서(예약 틱) 컨트롤러가 준 간격을 덮어쓴다. 그래서 칸에 적는다.
		// 처음엔 이걸 안 적어 한가한 몬스터 17마리의 나무가 매 프레임 돌았고, 평소 비용이 코드 방식의 50배로 나왔다(9/23 측정).
		auto ThinkSlowly = [](UStateTreeState& State)
		{
			State.bHasCustomTickRate = true;
			State.CustomTickRate = 0.25f;
		};

		UStateTreeState& ReturnHome = Root.AddChildState(FName(TEXT("ReturnHome")));
		ReturnHome.AddEnterCondition<FPGMonsterCondition_ShouldReturnHome>();
		ReturnHome.AddTask<FPGMonsterTask_ReturnHome>();
		ThinkSlowly(ReturnHome);
		BackToRoot(ReturnHome);

		UStateTreeState& Dormant = Root.AddChildState(FName(TEXT("Dormant")));
		Dormant.AddEnterCondition<FPGMonsterCondition_IsDormant>();
		Dormant.AddTask<FPGMonsterTask_WaitDormant>();
		ThinkSlowly(Dormant);
		BackToRoot(Dormant);

		UStateTreeState& Engage = Root.AddChildState(FName(TEXT("Engage")));
		Engage.AddEnterCondition<FPGMonsterCondition_HasTarget>();

		UStateTreeState& Attack = Engage.AddChildState(FName(TEXT("Attack")));
		Attack.AddEnterCondition<FPGMonsterCondition_InAttackRange>();
		Attack.AddTask<FPGMonsterTask_Attack>();
		BackToRoot(Attack);

		UStateTreeState& Chase = Engage.AddChildState(FName(TEXT("Chase")));
		Chase.AddTask<FPGMonsterTask_Chase>();
		BackToRoot(Chase);

		UStateTreeState& Idle = Root.AddChildState(FName(TEXT("Idle")));
		Idle.AddTask<FPGMonsterTask_Idle>();
		ThinkSlowly(Idle);
		BackToRoot(Idle);
	}

	static void Build()
	{
		UPackage* Package = CreatePackage(PackagePath);
		Package->FullyLoad();
		UStateTree* Tree = FindObject<UStateTree>(Package, AssetName);
		const bool bNew = Tree == nullptr;
		if (bNew)
			Tree = NewObject<UStateTree>(Package, AssetName, RF_Public | RF_Standalone | RF_Transactional);

		// 편집 데이터는 매번 새로 만든다(예전 모양이 섞이지 않게).
		UStateTreeEditorData* EditorData = NewObject<UStateTreeEditorData>(Tree, FName(), RF_Transactional);
		EditorData->Schema = NewObject<UStateTreeAIComponentSchema>(EditorData);
		Tree->EditorData = EditorData;
		BuildTree(*EditorData);

		FStateTreeCompilerLog Log;
		const bool bCompiled = UStateTreeEditingSubsystem::CompileStateTree(Tree, Log);
		Log.DumpToLog(LogPGObjects);
		if (!bCompiled)
		{
			UE_LOG(LogPGObjects, Error, TEXT("PG.BuildMonsterStateTree: compile FAILED — asset not saved"));
			return;
		}
		if (bNew)
			FAssetRegistryModule::AssetCreated(Tree);
		Package->MarkPackageDirty();
		const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
		UE_LOG(LogPGObjects, Display, TEXT("PG.BuildMonsterStateTree: %s %s (compiled, saved=%d)"),
			bNew ? TEXT("created") : TEXT("rebuilt"), *Tree->GetPathName(), bSaved ? 1 : 0);
	}

	static FAutoConsoleCommand BuildCommand(
		TEXT("PG.BuildMonsterStateTree"),
		TEXT("Builds, compiles and saves the monster StateTree asset /Game/PG/AI/ST_PGMonster from code (editor only)."),
		FConsoleCommandDelegate::CreateStatic(&Build));
}
#endif
