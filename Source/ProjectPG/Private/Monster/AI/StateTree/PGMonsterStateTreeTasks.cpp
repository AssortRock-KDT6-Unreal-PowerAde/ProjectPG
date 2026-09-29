#include "Monster/AI/StateTree/PGMonsterStateTreeTasks.h"

#include "Monster/MonsterAIController.h"
#include "Monster/PGMonsterCharacter.h"
#include "StateTreeExecutionContext.h"

namespace
{
	// 나무의 주인 = StateTree 부품이 붙은 컨트롤러.
	AMonsterAIController* PGStMonsterController(const FStateTreeExecutionContext& Context)
	{
		return Cast<AMonsterAIController>(Context.GetOwner());
	}
}

// ---- 과제 ----
// 규칙: 내 칸에 있을 이유가 없어지면 Succeeded → 나무가 Root 부터 다시 고른다. 할 일이 남았으면 Running.

EStateTreeRunStatus FPGMonsterTask_ReturnHome::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMonsterAIController* Controller = PGStMonsterController(Context);
	return (Controller && Controller->TickReturnHome()) ? EStateTreeRunStatus::Running : EStateTreeRunStatus::Succeeded;
}

EStateTreeRunStatus FPGMonsterTask_WaitDormant::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMonsterAIController* Controller = PGStMonsterController(Context);
	APGMonsterCharacter* Monster = Controller ? Controller->GetMonster() : nullptr;
	if (!IsValid(Monster) || !Monster->IsDormant())
		return EStateTreeRunStatus::Succeeded; // 깨어났다 — 다시 고른다(보통 Engage)
	if (AActor* TargetActor = Controller->GetLiveTarget())
		Controller->WakeIfClose(Monster, APGMonsterCharacter::DistanceToTarget(Monster, TargetActor));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FPGMonsterTask_Attack::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMonsterAIController* Controller = PGStMonsterController(Context);
	APGMonsterCharacter* Monster = Controller ? Controller->GetMonster() : nullptr;
	AActor* TargetActor = Controller ? Controller->GetLiveTarget() : nullptr;
	if (!IsValid(Monster) || !TargetActor || Monster->IsDormant() || Controller->ShouldReturnHome())
		return EStateTreeRunStatus::Succeeded;
	if (Monster->IsBusy())
		return EStateTreeRunStatus::Running; // 휘두르는 중 — 끝날 때까지 기다린다
	const float Distance = APGMonsterCharacter::DistanceToTarget(Monster, TargetActor);
	if (Distance > Monster->GetAttackRange())
		return EStateTreeRunStatus::Succeeded; // 멀어졌다 — Chase 로
	// 사거리 안인데 벽에 막혀 때리지도 부수지도 못하면 코드 방식처럼 그 자리에서 돌아서 다가간다.
	if (!Controller->TryAttackOrSmash(Monster, TargetActor, Distance))
		Controller->TickChase(Monster, TargetActor, Distance);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FPGMonsterTask_Chase::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMonsterAIController* Controller = PGStMonsterController(Context);
	APGMonsterCharacter* Monster = Controller ? Controller->GetMonster() : nullptr;
	AActor* TargetActor = Controller ? Controller->GetLiveTarget() : nullptr;
	if (!IsValid(Monster) || !TargetActor || Monster->IsDormant() || Controller->ShouldReturnHome())
		return EStateTreeRunStatus::Succeeded;
	if (Monster->IsBusy())
		return EStateTreeRunStatus::Running;
	const float Distance = APGMonsterCharacter::DistanceToTarget(Monster, TargetActor);
	if (Distance <= Monster->GetAttackRange())
		return EStateTreeRunStatus::Succeeded; // 닿았다 — Attack 으로
	Controller->TickChase(Monster, TargetActor, Distance);
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus FPGMonsterTask_Idle::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	AMonsterAIController* Controller = PGStMonsterController(Context);
	const APGMonsterCharacter* Monster = Controller ? Controller->GetMonster() : nullptr;
	if (!Controller || Controller->GetLiveTarget() || Controller->ShouldReturnHome() || (IsValid(Monster) && Monster->IsDormant()))
		return EStateTreeRunStatus::Succeeded;
	return EStateTreeRunStatus::Running; // 할 일 없음. 감각(컨트롤러 Tick)이 표적을 찾으면 다음 틱에 끝난다
}

// ---- 조건 ----

bool FPGMonsterCondition_ShouldReturnHome::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMonsterAIController* Controller = PGStMonsterController(Context);
	return Controller && Controller->ShouldReturnHome();
}

bool FPGMonsterCondition_IsDormant::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMonsterAIController* Controller = PGStMonsterController(Context);
	const APGMonsterCharacter* Monster = Controller ? Controller->GetMonster() : nullptr;
	return IsValid(Monster) && Monster->IsDormant();
}

bool FPGMonsterCondition_HasTarget::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMonsterAIController* Controller = PGStMonsterController(Context);
	return Controller && Controller->GetLiveTarget() != nullptr;
}

bool FPGMonsterCondition_InAttackRange::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AMonsterAIController* Controller = PGStMonsterController(Context);
	const APGMonsterCharacter* Monster = Controller ? Controller->GetMonster() : nullptr;
	AActor* TargetActor = Controller ? Controller->GetLiveTarget() : nullptr;
	return IsValid(Monster) && TargetActor && APGMonsterCharacter::DistanceToTarget(Monster, TargetActor) <= Monster->GetAttackRange();
}
