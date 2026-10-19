#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "StateTreeTaskBase.h"
#include "StateTreeConditionBase.h"
#include "StateTreeExecutionTypes.h"
#include "EnemyStateTreeNodes.generated.h"

// 몬스터 상태 트리(ST_Enemy)에 들어가는 작업·조건 모음.
// 모두 얇다: 실제 이동·엄폐·사격은 ACustomEnemyAIController 가 하고, 여기선 부르고 끝을 알릴 뿐이다.
// 상태 트리 주인(Context.GetOwner())이 그 컨트롤러라서 따로 연결(바인딩)할 칸이 없다.

// 어디로 갈지.
UENUM()
enum class EEnemyMoveGoal : uint8
{
	PatrolPoint,      // 처음 자리 둘레의 아무 점
	Cover,            // 엄폐 찾기 작업이 고른 자리
	Peek,             // 엄폐 옆, 쏠 수 있는 자리
	LastKnownTarget,  // 목표를 마지막으로 본/맞은 자리
	Target,           // 목표 쪽으로 사거리 안까지
};

// 얼마나 기다릴지(숫자는 AIConfig·EnemyTable 에서).
UENUM()
enum class EEnemyWaitKind : uint8
{
	Patrol,  // AIConfig.PatrolWaitRange 사이 무작위
	Cover,   // EnemyTable.CoverWaitTime, 웅크린 채
	Search,  // AIConfig.SearchWaitTime
	Hold,    // 목표에게 갈 길이 없을 때 잠깐 멈춤. AIConfig.SearchWaitTime (그 사이 다시 보이면 바로 전투)
};

// 저장할 값이 없는 작업·조건용 빈 칸. 상태 트리는 작업마다 데이터 칸이 꼭 있어야 컴파일된다(없으면 "missing instance value").
USTRUCT()
struct FEnemyNoInstanceData
{
	GENERATED_BODY()
};

USTRUCT()
struct FEnemyMoveToTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category=Parameter)
	EEnemyMoveGoal Goal = EEnemyMoveGoal::PatrolPoint;
};

// 정해진 곳으로 걸어간다. 도착 → 성공, 길 없음·막힘·시간 초과 → 실패.
USTRUCT(meta=(DisplayName="Enemy Move To", Category="Enemy AI"))
struct PROJECTPG_API FEnemyMoveToTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyMoveToTaskInstanceData;
	FEnemyMoveToTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyWaitTaskInstanceData
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category=Parameter)
	EEnemyWaitKind Kind = EEnemyWaitKind::Patrol;
	float RemainingTime = 0.f;
	UE::StateTree::FScheduledTickHandle TickHandle;
};

// 정해진 시간만큼 서 있다(엄폐면 웅크림). 끝나면 성공.
// 상태 트리에 "이 시간 뒤에 한 번 깨워 줘"라고 예약해서, 기다리는 동안 매 프레임 돌지 않는다.
USTRUCT(meta=(DisplayName="Enemy Wait", Category="Enemy AI"))
struct PROJECTPG_API FEnemyWaitTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyWaitTaskInstanceData;
	FEnemyWaitTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

// EQS 로 엄폐 자리와 그 옆 쏠 자리를 찾는다. 찾음 → 성공, 없음·시간 초과 → 실패.
USTRUCT(meta=(DisplayName="Enemy Find Cover", Category="Enemy AI"))
struct PROJECTPG_API FEnemyFindCoverTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	FEnemyFindCoverTask();
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

USTRUCT()
struct FEnemyFireTaskInstanceData
{
	GENERATED_BODY()
	float Elapsed = 0.f;
	UE::StateTree::FScheduledTickHandle TickHandle;
};

// EnemyTable.AttackDuration 동안 목표를 쏜다. 다 쏨 → 성공, 목표가 안 보이게 됨 → 실패.
// 쏘는 동안엔 AIConfig.FireCheckInterval 마다 깨어나 조준·탄약을 확인한다
// (사격 중엔 몸 방향·탄약이 계속 바뀌어서 알림만으로는 알 수 없음. 쏘지 않을 땐 안 돎).
USTRUCT(meta=(DisplayName="Enemy Fire", Category="Enemy AI"))
struct PROJECTPG_API FEnemyFireTask : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyFireTaskInstanceData;
	FEnemyFireTask();
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
};

// 목표가 있으면 통과.
USTRUCT(meta=(DisplayName="Enemy Has Target", Category="Enemy AI"))
struct PROJECTPG_API FEnemyHasTargetCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// 목표가 보이고 사거리 안이면 통과.
USTRUCT(meta=(DisplayName="Enemy Can Shoot", Category="Enemy AI"))
struct PROJECTPG_API FEnemyCanShootCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

// 쏠 수 있는 거리이고 엄폐 찾기 쉬는 시간이 끝났으면 통과.
USTRUCT(meta=(DisplayName="Enemy Cover Ready", Category="Enemy AI"))
struct PROJECTPG_API FEnemyCoverReadyCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	using FInstanceDataType = FEnemyNoInstanceData;
	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
