// 몬스터 AI 측정기. (2026-09-23)
//
// 왜: 몬스터 AI 를 "코드 방식(Tick 안의 if)" 에서 StateTree 로 바꾸기 전에, 지금 방식이 얼마나 가벼운지 숫자로 남겨 두고
//   바꾼 뒤 같은 맵(같은 시드)·같은 명령으로 다시 재서 나란히 비교한다(Docs/MonsterAIBenchmark_2026-09-23.md).
// 무엇을 재나:
//   - 생각하는 데 쓴 CPU 시간(마이크로초). 컨트롤러 Tick 에서 부모(AAIController) 몫을 뺀 우리 코드만.
//     StateTree 방식이면 StateTree 부품의 Tick 도 같이 더한다 — 두 방식 모두 "몬스터가 판단하는 데 든 시간 전부" 를 잰다.
//   - 몇 번 생각했나(Tick 횟수), 길찾기 요청 수(MoveTo), 공격 시도 수, 표적을 잡은/놓친 횟수, 깨어난 횟수.
// 비용: 숫자 몇 개를 더하는 것뿐이라 측정 자체가 결과를 흐리지 않는다(시계 읽기 2번 ≈ 수십 나노초).
// 사용: PG.MonsterAI.Stats reset  → 지금부터 다시 센다.   PG.MonsterAI.Stats  → 한 줄로 찍는다(로그 "PGMonsterAIStats:").
#pragma once

#include "CoreMinimal.h"

namespace PGMonsterAIStats
{
	// 생각 시간 재기: 만들 때 시계를 읽고, 사라질 때 차이를 더한다. 함수 중간의 return 이 많아도 빠짐없이 잰다.
	struct PROJECTPG_API FThinkScope
	{
		// bCountThink = false: 시간만 더하고 "생각 횟수" 는 안 센다(StateTree 부품 — 같은 프레임에 컨트롤러가 이미 셌다).
		explicit FThinkScope(bool bInCountThink = true);
		~FThinkScope();
	private:
		uint64 StartCycles;
		bool bCountThink;
	};

	PROJECTPG_API void CountMoveRequest();
	PROJECTPG_API void CountAttack();
	PROJECTPG_API void CountTargetAcquired();
	PROJECTPG_API void CountTargetLost();
	PROJECTPG_API void CountWake();
	PROJECTPG_API void CountReturnHome();
	// StateTree 부품이 한 번 돈 횟수(코드 방식이면 0).
	PROJECTPG_API void CountTreeTick();

	// 지금 어느 방식인지 로그에 같이 찍는다("Classic" / "StateTree").
	PROJECTPG_API void SetModeName(const TCHAR* Name);
}
