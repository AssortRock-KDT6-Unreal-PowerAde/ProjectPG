// StateTree 방식의 판단 부품(엔진 UStateTreeAIComponent 그대로 + 시간 재기). (2026-09-23)
//
// 왜 따로 만들었나: 엔진 부품은 컨트롤러 Tick 과 따로 돈다. 그 시간도 "몬스터가 판단하는 데 든 시간" 에 넣어야
//   코드 방식과 공평하게 비교된다(PGMonsterAIStats). 동작은 한 줄도 바꾸지 않는다.
#pragma once

#include "CoreMinimal.h"
#include "Components/StateTreeAIComponent.h"
#include "PGMonsterStateTreeComponent.generated.h"

UCLASS(ClassGroup = AI)
class PROJECTPG_API UPGMonsterStateTreeComponent : public UStateTreeAIComponent
{
	GENERATED_BODY()

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	// 나무는 몬스터가 생긴 뒤 컨트롤러가 끼운다(방식이 정해진 뒤). 엔진은 생기는 순간 "나무가 없다" 오류를 찍는데
	// 몬스터마다 한 줄씩 수십 줄이 쌓였다(9/23). 아직 안 끼운 것뿐이면 조용히 넘긴다 — 끼운 뒤의 검사는 엔진 그대로.
	virtual void ValidateStateTreeReference() override;
};
