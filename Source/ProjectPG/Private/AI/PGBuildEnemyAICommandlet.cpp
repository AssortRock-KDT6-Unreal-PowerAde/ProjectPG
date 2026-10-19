#include "AI/PGBuildEnemyAICommandlet.h"

#if WITH_EDITOR
#include "AI/CustomEnemyAIController.h"
#include "AI/EnemyAIConfig.h"
#include "AI/EnemyAITags.h"
#include "AI/EnemyStateTreeNodes.h"
#include "AI/EnvQueryContext_EnemyTarget.h"
#include "Characters/CustomEnemyCharacter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/StateTreeAIComponentSchema.h"
#include "DataProviders/AIDataProvider_QueryParams.h"
#include "EditorAssetLibrary.h"
#include "Engine/Blueprint.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryOption.h"
#include "EnvironmentQuery/Contexts/EnvQueryContext_Querier.h"
#include "EnvironmentQuery/Generators/EnvQueryGenerator_SimpleGrid.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Distance.h"
#include "EnvironmentQuery/Tests/EnvQueryTest_Trace.h"
#include "StateTree.h"
#include "StateTreeCompilerLog.h"
#include "StateTreeEditingSubsystem.h"
#include "StateTreeEditorData.h"
#include "StateTreeState.h"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBuildEnemyAI, Log, All);

#if WITH_EDITOR
namespace
{
	// 만들 에셋 자리. 게임 코드가 이 글자로 에셋을 읽지는 않는다(게임은 BP → DA 칸을 따라감).
	const TCHAR* StateTreePath = TEXT("/Game/PG/AI/ST_Enemy");
	const TCHAR* CoverQueryPath = TEXT("/Game/PG/AI/EQS_FindCover");
	const TCHAR* ConfigPath = TEXT("/Game/PG/AI/DA_EnemyAI_Rifle");
	const TCHAR* EnemyBlueprintPath = TEXT("/Game/PG/Blueprint/Characters/CBP_CustomEnemyCharacter.CBP_CustomEnemyCharacter");

	template<class T>
	T* LoadOrCreateAsset(const FString& PackagePath)
	{
		const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
		if (T* Existing = LoadObject<T>(nullptr, *(PackagePath + TEXT(".") + AssetName))) return Existing;
		UPackage* Package = CreatePackage(*PackagePath);
		T* Asset = NewObject<T>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		UE_LOG(LogBuildEnemyAI, Display, TEXT("새로 만듦: %s"), *PackagePath);
		return Asset;
	}

	// 엔진 칸 중 C++ 에서 바로 못 건드리는(protected) 칸을 이름으로 채운다.
	void SetClassField(UObject* Object, const TCHAR* Field, UClass* Value)
	{
		FClassProperty* Prop = FindFProperty<FClassProperty>(Object->GetClass(), Field);
		if (ensureMsgf(Prop, TEXT("%s 에 %s 칸이 없음"), *Object->GetClass()->GetName(), Field))
			Prop->SetObjectPropertyValue_InContainer(Object, Value);
	}

	void SetEnumField(UObject* Object, const TCHAR* Field, int64 Value)
	{
		FEnumProperty* Prop = FindFProperty<FEnumProperty>(Object->GetClass(), Field);
		if (ensureMsgf(Prop, TEXT("%s 에 %s 칸이 없음"), *Object->GetClass()->GetName(), Field))
			Prop->GetUnderlyingProperty()->SetIntPropertyValue(Prop->ContainerPtrToValuePtr<void>(Object), Value);
	}

	// 엄폐 EQS: 몬스터 둘레 격자 점 → 목표 눈에서 가려진 점만 남김 → 가까운 순.
	UEnvQuery* BuildCoverQuery(bool bForce)
	{
		const bool bExists = FPackageName::DoesPackageExist(CoverQueryPath);
		UEnvQuery* Query = LoadOrCreateAsset<UEnvQuery>(CoverQueryPath);
		if (bExists && !bForce) return Query;  // 이미 있으면 에디터에서 고친 내용을 그대로 둔다
		Query->Modify();
		Query->GetOptionsMutable().Reset();

		UEnvQueryOption* Option = NewObject<UEnvQueryOption>(Query, NAME_None, RF_Transactional);
		UEnvQueryGenerator_SimpleGrid* Grid = NewObject<UEnvQueryGenerator_SimpleGrid>(Option, NAME_None, RF_Transactional);
		Grid->GenerateAround = UEnvQueryContext_Querier::StaticClass();
		// 격자 반 크기 = 몬스터 표의 CoverSearchRadius. AI 컨트롤러가 같은 이름의 값으로 넘겨 준다.
		UAIDataProvider_QueryParams* RadiusParam = NewObject<UAIDataProvider_QueryParams>(Grid, NAME_None, RF_Transactional);
		RadiusParam->ParamName = TEXT("CoverSearchRadius");
		Grid->GridSize.DataBinding = RadiusParam;
		Grid->GridSize.DataField = TEXT("FloatValue");
		Grid->GridSize.DefaultValue = 800.f;
		Grid->SpaceBetween.DefaultValue = 150.f;
		Option->Generator = Grid;

		UEnvQueryTest_Trace* Hidden = NewObject<UEnvQueryTest_Trace>(Option, NAME_None, RF_Transactional);
		Hidden->Context = UEnvQueryContext_EnemyTarget::StaticClass();
		Hidden->TraceFromContext.DefaultValue = true;
		Hidden->ContextHeightOffset.DefaultValue = 60.f;  // 목표 눈 높이 쯤
		Hidden->ItemHeightOffset.DefaultValue = 50.f;     // 웅크린 몬스터 머리 쯤
		Hidden->TestPurpose = EEnvTestPurpose::Filter;
		Hidden->FilterType = EEnvTestFilterType::Match;
		Hidden->BoolValue.DefaultValue = true;           // 선이 막혀야(= 가려져야) 남김
		Option->Tests.Add(Hidden);

		UEnvQueryTest_Distance* Near = NewObject<UEnvQueryTest_Distance>(Option, NAME_None, RF_Transactional);
		Near->DistanceTo = UEnvQueryContext_Querier::StaticClass();
		Near->TestPurpose = EEnvTestPurpose::Score;
		Near->ScoringEquation = EEnvTestScoreEquation::InverseLinear;  // 가까울수록 높은 점수
		Option->Tests.Add(Near);

		Query->GetOptionsMutable().Add(Option);
		Query->MarkPackageDirty();
		return Query;
	}

	// 상태 트리. 그림은 Private/AI/README.md 와 같다. 바꾸면 그림도 같이 고칠 것.
	UStateTree* BuildStateTree(bool bForce)
	{
		const bool bExists = FPackageName::DoesPackageExist(StateTreePath);
		UStateTree* Tree = LoadOrCreateAsset<UStateTree>(StateTreePath);
		if (bExists && !bForce) return Tree;  // 이미 있으면 에디터에서 고친 내용을 그대로 둔다
		Tree->Modify();

		UStateTreeEditorData* Editor = NewObject<UStateTreeEditorData>(Tree, NAME_None, RF_Transactional);
		UStateTreeAIComponentSchema* Schema = NewObject<UStateTreeAIComponentSchema>(Editor, NAME_None, RF_Transactional);
		SetClassField(Schema, TEXT("AIControllerClass"), ACustomEnemyAIController::StaticClass());
		// 할 일이 없을 땐 상태 트리도 쉰다(기다리기·사격 작업이 정한 시각에만 깨어남).
		SetEnumField(Schema, TEXT("ScheduledTickPolicy"), static_cast<int64>(EStateTreeComponentSchemaScheduledTickPolicy::Allowed));
		Editor->Schema = Schema;
		Tree->EditorData = Editor;

		using ETrigger = EStateTreeTransitionTrigger;
		using EType = EStateTreeTransitionType;
		UStateTreeState& Root = Editor->AddRootState();

		// 상태부터 다 만들고, 서로 가리키는 전이는 그 뒤에 잇는다.
		UStateTreeState& Idle = Root.AddChildState(TEXT("Idle"));
		UStateTreeState& PatrolMove = Idle.AddChildState(TEXT("PatrolMove"));
		UStateTreeState& PatrolWait = Idle.AddChildState(TEXT("PatrolWait"));

		UStateTreeState& Combat = Root.AddChildState(TEXT("Combat"));
		UStateTreeState& TakeCover = Combat.AddChildState(TEXT("TakeCover"));
		UStateTreeState& FindCover = TakeCover.AddChildState(TEXT("FindCover"));
		UStateTreeState& MoveToCover = TakeCover.AddChildState(TEXT("MoveToCover"));
		UStateTreeState& HoldCover = TakeCover.AddChildState(TEXT("HoldCover"));
		UStateTreeState& MoveToPeek = TakeCover.AddChildState(TEXT("MoveToPeek"));
		UStateTreeState& PeekFire = TakeCover.AddChildState(TEXT("PeekFire"));
		UStateTreeState& Engage = Combat.AddChildState(TEXT("Engage"));
		UStateTreeState& Approach = Combat.AddChildState(TEXT("Approach"));
		UStateTreeState& ApproachMove = Approach.AddChildState(TEXT("ApproachMove"));
		UStateTreeState& ApproachHold = Approach.AddChildState(TEXT("ApproachHold"));

		UStateTreeState& Search = Root.AddChildState(TEXT("Search"));
		UStateTreeState& SearchMove = Search.AddChildState(TEXT("SearchMove"));
		UStateTreeState& SearchLook = Search.AddChildState(TEXT("SearchLook"));

		// 순찰: 걸어감 → 서 있음 → 반복. 목표가 생기면 전투.
		PatrolMove.AddTask<FEnemyMoveToTask>().GetInstanceData().Goal = EEnemyMoveGoal::PatrolPoint;
		PatrolMove.AddTransition(ETrigger::OnStateCompleted, EType::NextState);
		PatrolWait.AddTask<FEnemyWaitTask>().GetInstanceData().Kind = EEnemyWaitKind::Patrol;
		PatrolWait.AddTransition(ETrigger::OnStateCompleted, EType::GotoState, &PatrolMove);
		Idle.AddTransition(ETrigger::OnEvent, EnemyAITags::TargetFound, EType::GotoState, &Combat);

		// 전투: 위에서부터 조건이 맞는 첫 자식을 고른다(엄폐 → 서서 쏘기 → 다가가기).
		// 자식이 끝날 때마다 Combat 으로 돌아와 다시 고른다. 목표를 포기하면 수색.
		Combat.AddEnterCondition<FEnemyHasTargetCondition>();
		Combat.AddTransition(ETrigger::OnEvent, EnemyAITags::TargetLost, EType::GotoState, &Search);

		TakeCover.AddEnterCondition<FEnemyCoverReadyCondition>();
		FindCover.AddTask<FEnemyFindCoverTask>();
		FindCover.AddTransition(ETrigger::OnStateSucceeded, EType::NextState);
		FindCover.AddTransition(ETrigger::OnStateFailed, EType::GotoState, &Combat);
		MoveToCover.AddTask<FEnemyMoveToTask>().GetInstanceData().Goal = EEnemyMoveGoal::Cover;
		MoveToCover.AddTransition(ETrigger::OnStateSucceeded, EType::NextState);
		MoveToCover.AddTransition(ETrigger::OnStateFailed, EType::GotoState, &Combat);
		HoldCover.AddTask<FEnemyWaitTask>().GetInstanceData().Kind = EEnemyWaitKind::Cover;
		HoldCover.AddTransition(ETrigger::OnStateCompleted, EType::NextState);
		MoveToPeek.AddTask<FEnemyMoveToTask>().GetInstanceData().Goal = EEnemyMoveGoal::Peek;
		MoveToPeek.AddTransition(ETrigger::OnStateSucceeded, EType::NextState);
		MoveToPeek.AddTransition(ETrigger::OnStateFailed, EType::GotoState, &Combat);
		PeekFire.AddTask<FEnemyFireTask>();
		PeekFire.AddTransition(ETrigger::OnStateCompleted, EType::GotoState, &Combat);

		Engage.AddEnterCondition<FEnemyCanShootCondition>();
		Engage.AddTask<FEnemyFireTask>();
		Engage.AddTransition(ETrigger::OnStateCompleted, EType::GotoState, &Combat);

		// 다가가는 중에 다시 보이면 곧바로 다시 고른다(쏠 수 있으면 Engage).
		Approach.AddTransition(ETrigger::OnEvent, EnemyAITags::TargetSeen, EType::GotoState, &Combat);
		ApproachMove.AddTask<FEnemyMoveToTask>().GetInstanceData().Goal = EEnemyMoveGoal::Target;
		ApproachMove.AddTransition(ETrigger::OnStateSucceeded, EType::GotoState, &Combat);
		ApproachMove.AddTransition(ETrigger::OnStateFailed, EType::NextState);
		ApproachHold.AddTask<FEnemyWaitTask>().GetInstanceData().Kind = EEnemyWaitKind::Hold;
		ApproachHold.AddTransition(ETrigger::OnStateCompleted, EType::GotoState, &Combat);

		// 수색: 마지막 위치로 가서 둘러보고 순찰로. 그 사이 다시 보이면 전투.
		Search.AddTransition(ETrigger::OnEvent, EnemyAITags::TargetFound, EType::GotoState, &Combat);
		SearchMove.AddTask<FEnemyMoveToTask>().GetInstanceData().Goal = EEnemyMoveGoal::LastKnownTarget;
		SearchMove.AddTransition(ETrigger::OnStateCompleted, EType::NextState);
		SearchLook.AddTask<FEnemyWaitTask>().GetInstanceData().Kind = EEnemyWaitKind::Search;
		SearchLook.AddTransition(ETrigger::OnStateCompleted, EType::GotoState, &Idle);

		FStateTreeCompilerLog Log;
		if (!UStateTreeEditingSubsystem::CompileStateTree(Tree, Log))
		{
			Log.DumpToLog(LogBuildEnemyAI);
			UE_LOG(LogBuildEnemyAI, Error, TEXT("ST_Enemy 컴파일 실패(위 로그 참고)"));
			return nullptr;
		}
		Tree->MarkPackageDirty();
		return Tree;
	}
}
#endif

int32 UPGBuildEnemyAICommandlet::Main(const FString& Params)
{
#if WITH_EDITOR
	// -Force 를 붙이면 ST·EQS 를 코드 내용으로 덮어쓴다(에디터에서 고친 것은 사라짐).
	const bool bForce = FParse::Param(*Params, TEXT("Force"));
	UEnvQuery* CoverQuery = BuildCoverQuery(bForce);
	UStateTree* Tree = BuildStateTree(bForce);
	if (!Tree) return 1;

	// DA 는 처음 만들 때만 숫자가 C++ 기본값이고, 이후엔 기획이 고친 숫자를 덮어쓰지 않는다.
	UEnemyAIConfig* Config = LoadOrCreateAsset<UEnemyAIConfig>(ConfigPath);
	Config->Modify();
	Config->StateTree = Tree;
	Config->CoverQuery = CoverQuery;
	Config->MarkPackageDirty();

	UBlueprint* EnemyBlueprint = LoadObject<UBlueprint>(nullptr, EnemyBlueprintPath);
	UObject* EnemyDefaults = EnemyBlueprint && EnemyBlueprint->GeneratedClass ? EnemyBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
	FObjectProperty* ConfigField = FindFProperty<FObjectProperty>(ACustomEnemyCharacter::StaticClass(), TEXT("AIConfig"));
	if (!EnemyDefaults || !ConfigField)
	{
		UE_LOG(LogBuildEnemyAI, Error, TEXT("몬스터 BP(%s)를 못 찾아 AIConfig 를 못 넣음"), EnemyBlueprintPath);
		return 1;
	}
	EnemyBlueprint->Modify();
	EnemyDefaults->Modify();
	ConfigField->SetObjectPropertyValue_InContainer(EnemyDefaults, Config);
	EnemyBlueprint->MarkPackageDirty();

	bool bSaved = true;
	for (UObject* Asset : TArray<UObject*>{ CoverQuery, Tree, Config, EnemyBlueprint })
		bSaved &= UEditorAssetLibrary::SaveLoadedAsset(Asset, false);
	UE_LOG(LogBuildEnemyAI, Display, TEXT("몬스터 AI 에셋 %s"), bSaved ? TEXT("저장 완료") : TEXT("저장 실패"));
	return bSaved ? 0 : 1;
#else
	UE_LOG(LogBuildEnemyAI, Error, TEXT("에디터에서만 돌릴 수 있음"));
	return 1;
#endif
}
