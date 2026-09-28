// 코드 방식 판단 — Tick 안의 if 순서. (2026-09-23 MonsterAIController.cpp 에서 떼어 냄)
//
// 무엇: StateTree 로 바꾸기 전 원래 판단을 그대로 둔다. 순서가 곧 우선순위다:
//   집으로 돌아가기 > (감각) 표적 없음 → 쉼 > 잠복이면 깨어나기 > 동작 중이면 기다림 > 사거리 안이면 때리기 > 쫓아가기
// 왜 남기나: StateTree 방식(ST_PGMonster)과 같은 맵·같은 상황에서 비용을 비교하려고(PG.MonsterAI.Mode 0).
//   감각·동작 함수는 두 방식이 같이 쓴다(MonsterAIController.cpp) — 여기에는 "무엇을 먼저 할까" 만 있다.
// 장단점(면접용): 짧고 빠르고 디버거로 한 줄씩 따라가기 쉽다. 대신 상태가 늘면 if 순서가 얽히고,
//   "지금 어느 상태인가" 가 변수 여러 개(bReturningHome, 잠복, 동작 중…)에 흩어져 눈으로 보기 어렵다.
#include "Monster/MonsterAIController.h"

#include "Monster/PGMonsterCharacter.h"

void AMonsterAIController::TickClassic(APGMonsterCharacter* Monster, float Now)
{
	if (TickLeash(Monster, Now))
		return;
	AActor* TargetActor = UpdateTargetFromSenses(Monster, Now);
	if (!TargetActor)
		return;
	const float Distance = APGMonsterCharacter::DistanceToTarget(Monster, TargetActor);
	if (Monster->IsDormant())
	{
		WakeIfClose(Monster, Distance);
		return;
	}
	if (Monster->IsBusy())
		return;
	if (TryAttackOrSmash(Monster, TargetActor, Distance))
		return;
	TickChase(Monster, TargetActor, Distance);
}
