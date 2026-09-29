// 몬스터 StateTree 의 과제(Task)와 조건(Condition). (2026-09-23)
//
// 나무 모양(ST_PGMonster, 만드는 코드: PGMonsterStateTreeBuilder.cpp):
//   Root ─ 위에서부터 들어갈 수 있는 첫 칸을 고른다(= 코드 방식 if 순서와 같은 우선순위)
//     ├ ReturnHome  [조건: 영역 밖]      과제: 집으로 돌아가기
//     ├ Dormant     [조건: 잠복 중]      과제: 가까이 오면 깨어나기
//     ├ Engage      [조건: 표적 있음]
//     │   ├ Attack  [조건: 사거리 안]    과제: 때리기(막히면 벽 부수기/돌아가기)
//     │   └ Chase                        과제: 쫓아가기
//     └ Idle                             과제: 쉬기(표적이 생기면 끝)
//   과제는 "내 칸의 조건이 더는 맞지 않으면" 끝(Succeeded)을 알린다 → 나무가 Root 부터 다시 고른다.
//   매 프레임 처음부터 다시 고르지 않고, 바뀔 때만 고른다 — 이게 코드 방식과 가장 큰 차이다.
//
// 과제는 컨트롤러의 함수를 부르기만 한다(감각·동작은 두 방식이 같다). 컨트롤러는 Context.GetOwner()
//   — StateTree 부품이 컨트롤러에 붙어 있어서 주인이 곧 컨트롤러다. 그래서 칸(바인딩)을 따로 잇지 않아도 된다.
#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "PGMonsterStateTreeTasks.generated.h"

// 과제·조건이 따로 기억할 것이 없다(전부 컨트롤러에 있다). 엔진이 "인스턴스 데이터 형식" 을 요구해서 빈 구조체를 둔다.
USTRUCT()
struct PROJECTPG_API FPGMonsterNodeInstanceData
{
	GENERATED_BODY()
};

// ---- 과제 ----

// 공통 뼈대: 인스턴스 데이터 형식만 알려 준다.
USTRUCT(meta = (Hidden))
struct PROJECTPG_API FPGMonsterTaskBase : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FPGMonsterNodeInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT(meta = (DisplayName = "PG Monster: Return Home", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterTask_ReturnHome : public FPGMonsterTaskBase
{
	GENERATED_BODY()
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Wait Dormant", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterTask_WaitDormant : public FPGMonsterTaskBase
{
	GENERATED_BODY()
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Attack", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterTask_Attack : public FPGMonsterTaskBase
{
	GENERATED_BODY()
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Chase", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterTask_Chase : public FPGMonsterTaskBase
{
	GENERATED_BODY()
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Idle", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterTask_Idle : public FPGMonsterTaskBase
{
	GENERATED_BODY()
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
};

// ---- 조건 ----

USTRUCT(meta = (Hidden))
struct PROJECTPG_API FPGMonsterConditionBase : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FPGMonsterNodeInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
};

USTRUCT(meta = (DisplayName = "PG Monster: Should Return Home", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterCondition_ShouldReturnHome : public FPGMonsterConditionBase
{
	GENERATED_BODY()
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Is Dormant", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterCondition_IsDormant : public FPGMonsterConditionBase
{
	GENERATED_BODY()
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Has Target", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterCondition_HasTarget : public FPGMonsterConditionBase
{
	GENERATED_BODY()
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

USTRUCT(meta = (DisplayName = "PG Monster: Target In Attack Range", Category = "PG|Monster"))
struct PROJECTPG_API FPGMonsterCondition_InAttackRange : public FPGMonsterConditionBase
{
	GENERATED_BODY()
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
