// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "AI/EnemyStateTreeNodes.h"
#include "AI/EnemyAILog.h"
#include "AI/CustomEnemyAIController.h"
#include "AI/EnemyAIConfig.h"
#include "Characters/CustomEnemyCharacter.h"
#include "StateTreeExecutionContext.h"
#include "StateTreeAsyncExecutionContext.h"


namespace
{
	ACustomEnemyAIController* GetAI(const FStateTreeExecutionContext& Context)
	{
		// 상태 트리는 컨트롤러에 붙은 컴포넌트가 돌리므로 주인이 곧 컨트롤러다.
		ACustomEnemyAIController* AI = Cast<ACustomEnemyAIController>(Context.GetOwner());
		return AI && AI->GetEnemy() && AI->GetConfig() ? AI : nullptr;
	}

	// 서버 로그에 "누가 어떤 행동을 시작했는지" 남긴다. 멀티 시험 때 상태가 바뀌는 순서를 보려고.
	void LogStep(const ACustomEnemyAIController* AI, const TCHAR* Step)
	{
		UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: %s"), *GetNameSafe(AI->GetPawn()), Step);
	}

	const TCHAR* GoalName(EEnemyMoveGoal Goal)
	{
		switch (Goal)
		{
		case EEnemyMoveGoal::PatrolPoint: return TEXT("순찰 이동");
		case EEnemyMoveGoal::Cover: return TEXT("엄폐로 이동");
		case EEnemyMoveGoal::Peek: return TEXT("쏠 자리로 이동");
		case EEnemyMoveGoal::LastKnownTarget: return TEXT("마지막 위치 수색");
		case EEnemyMoveGoal::Target: return TEXT("목표에게 접근");
		}
		return TEXT("이동");
	}

	EStateTreeRunStatus ToRunStatus(ACustomEnemyAIController::EMoveStart Start)
	{
		switch (Start)
		{
		case ACustomEnemyAIController::EMoveStart::Started: return EStateTreeRunStatus::Running;
		case ACustomEnemyAIController::EMoveStart::AlreadyThere: return EStateTreeRunStatus::Succeeded;
		default: return EStateTreeRunStatus::Failed;
		}
	}
}

// ───────────── 이동 ─────────────

FEnemyMoveToTask::FEnemyMoveToTask()
{
	// 끝은 컨트롤러가 알려 준다(도착·실패·시간 초과). 매 프레임 확인할 일 없음.
	bShouldCallTick = false;
}

EStateTreeRunStatus FEnemyMoveToTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	ACustomEnemyAIController* AI = GetAI(Context);
	if (!AI) return EStateTreeRunStatus::Failed;
	const FInstanceDataType& Data = Context.GetInstanceData(*this);
	LogStep(AI, GoalName(Data.Goal));

	// 끝나면 이 작업만 골라서 끝낸다. 그사이 상태가 바뀌었으면 엔진이 알아서 무시한다.
	auto Finish = [WeakContext = Context.MakeWeakExecutionContext()](bool bSucceeded)
	{
		WeakContext.FinishTask(bSucceeded ? EStateTreeFinishTaskType::Succeeded : EStateTreeFinishTaskType::Failed);
	};

	if (Data.Goal == EEnemyMoveGoal::Target)
	{
		const ACustomEnemyAIController::EMoveStart Start = AI->MoveToTargetWithTimeout(Finish);
		// 이미 가까운데 벽 때문에 안 보이는 경우: 성공으로 끝내면 전투 상태가 곧바로 또 접근을 골라
		// 같은 자리에서 계속 맴돈다 → 실패로 끝내서 상태 트리가 잠깐 멈추게(Hold) 한다.
		if (Start == ACustomEnemyAIController::EMoveStart::AlreadyThere && !AI->IsTargetVisible()) return EStateTreeRunStatus::Failed;
		return ToRunStatus(Start);
	}

	FVector Goal;
	switch (Data.Goal)
	{
	case EEnemyMoveGoal::PatrolPoint:
		if (!AI->PickPatrolPoint(Goal)) return EStateTreeRunStatus::Failed;  // 순찰 반경 0 이거나 네비 없음 → 그 자리에 서 있음
		break;
	case EEnemyMoveGoal::Cover: Goal = AI->GetCoverLocation(); break;
	case EEnemyMoveGoal::Peek: Goal = AI->GetPeekLocation(); break;
	default: Goal = AI->GetLastKnownTargetLocation(); break;
	}
	return ToRunStatus(AI->MoveWithTimeout(Goal, GoalName(Data.Goal), Finish));
}

void FEnemyMoveToTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// 도착 전에 다른 상태로 넘어가면(목표 발견 등) 걷던 걸 멈춘다.
	if (ACustomEnemyAIController* AI = GetAI(Context)) AI->CancelMove();
}

// ───────────── 기다리기 ─────────────

FEnemyWaitTask::FEnemyWaitTask()
{
	bShouldCallTick = true;  // 예약한 시각에만 깨어난다(아래 AddScheduledTickRequest).
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FEnemyWaitTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	ACustomEnemyAIController* AI = GetAI(Context);
	if (!AI) return EStateTreeRunStatus::Failed;
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	const UEnemyAIConfig& Config = *AI->GetConfig();
	switch (Data.Kind)
	{
	case EEnemyWaitKind::Patrol:
		Data.RemainingTime = FMath::FRandRange(Config.PatrolWaitRange.X, FMath::Max(Config.PatrolWaitRange.X, Config.PatrolWaitRange.Y));
		break;
	case EEnemyWaitKind::Cover:
		Data.RemainingTime = AI->GetEnemy()->GetEnemyData().CoverWaitTime;
		AI->GetEnemy()->Crouch();
		LogStep(AI, TEXT("엄폐에서 대기"));
		break;
	case EEnemyWaitKind::Search:
		Data.RemainingTime = Config.SearchWaitTime;
		LogStep(AI, TEXT("수색 자리에서 둘러봄"));
		break;
	case EEnemyWaitKind::Hold:
		Data.RemainingTime = Config.SearchWaitTime;
		LogStep(AI, TEXT("목표에게 갈 수 없어 잠시 멈춤"));
		break;
	}
	if (Data.RemainingTime <= 0.f) return EStateTreeRunStatus::Succeeded;
	Data.TickHandle = Context.AddScheduledTickRequest(FStateTreeScheduledTick::MakeCustomTickRate(Data.RemainingTime));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyWaitTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	// 다른 작업 때문에 예약보다 일찍 깨어날 수 있어서 남은 시간을 직접 센다.
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.RemainingTime -= DeltaTime;
	if (Data.RemainingTime <= 0.f) return EStateTreeRunStatus::Succeeded;
	Context.UpdateScheduledTickRequest(Data.TickHandle, FStateTreeScheduledTick::MakeCustomTickRate(Data.RemainingTime));
	return EStateTreeRunStatus::Running;
}

void FEnemyWaitTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Context.RemoveScheduledTickRequest(Data.TickHandle);
	if (Data.Kind == EEnemyWaitKind::Cover)
		if (ACustomEnemyAIController* AI = GetAI(Context)) AI->GetEnemy()->UnCrouch();
}

// ───────────── 엄폐 찾기 ─────────────

FEnemyFindCoverTask::FEnemyFindCoverTask()
{
	bShouldCallTick = false;  // 끝은 EQS 결과가 알려 준다.
}

EStateTreeRunStatus FEnemyFindCoverTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	ACustomEnemyAIController* AI = GetAI(Context);
	if (!AI) return EStateTreeRunStatus::Failed;
	LogStep(AI, TEXT("엄폐 자리 찾기"));
	const bool bStarted = AI->FindCoverAsync([WeakContext = Context.MakeWeakExecutionContext()](bool bFound)
	{
		WeakContext.FinishTask(bFound ? EStateTreeFinishTaskType::Succeeded : EStateTreeFinishTaskType::Failed);
	});
	return bStarted ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

void FEnemyFindCoverTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (ACustomEnemyAIController* AI = GetAI(Context)) AI->CancelCoverSearch();
}

// ───────────── 사격 ─────────────

FEnemyFireTask::FEnemyFireTask()
{
	bShouldCallTick = true;  // FireCheckInterval 마다만 깨어난다.
	bShouldCopyBoundPropertiesOnTick = false;
}

EStateTreeRunStatus FEnemyFireTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	ACustomEnemyAIController* AI = GetAI(Context);
	if (!AI) return EStateTreeRunStatus::Failed;
	LogStep(AI, TEXT("사격"));
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Elapsed = 0.f;
	if (!AI->UpdateFire()) return EStateTreeRunStatus::Failed;
	Data.TickHandle = Context.AddScheduledTickRequest(FStateTreeScheduledTick::MakeCustomTickRate(AI->GetConfig()->FireCheckInterval));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FEnemyFireTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	ACustomEnemyAIController* AI = GetAI(Context);
	if (!AI) return EStateTreeRunStatus::Failed;
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Data.Elapsed += DeltaTime;
	if (Data.Elapsed >= AI->GetEnemy()->GetEnemyData().AttackDuration) return EStateTreeRunStatus::Succeeded;
	return AI->UpdateFire() ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Failed;
}

void FEnemyFireTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	FInstanceDataType& Data = Context.GetInstanceData(*this);
	Context.RemoveScheduledTickRequest(Data.TickHandle);
	if (ACustomEnemyAIController* AI = GetAI(Context)) AI->StopFire();
}

// ───────────── 조건 ─────────────

bool FEnemyHasTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const ACustomEnemyAIController* AI = GetAI(Context);
	return AI && AI->GetTarget() != nullptr;
}

bool FEnemyCanShootCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const ACustomEnemyAIController* AI = GetAI(Context);
	return AI && AI->CanShootTarget();
}

bool FEnemyCoverReadyCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const ACustomEnemyAIController* AI = GetAI(Context);
	return AI && AI->IsCoverSearchReady();
}
