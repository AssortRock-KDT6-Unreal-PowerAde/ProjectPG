// 몬스터 AI. 보고(시각) 듣고(청각) 알아채면 쫓아가서 사거리 안에서 때린다.
//
// 세 칸으로 나눈다 (2026-09-23):
//   감각 — 누구를 표적으로 삼나(UpdateTargetFromSenses). 두 방식이 같이 쓴다.
//   동작 — 집으로 돌아가기·깨어나기·때리기·쫓아가기(TickLeash, WakeIfClose, TryAttackOrSmash, TickChase). 두 방식이 같이 쓴다.
//   판단 — 지금 어느 동작을 할지. 이것만 두 가지다:
//     코드 방식(Classic)  : Tick 안의 if 순서 — Monster/AI/Classic/MonsterAIClassicBrain.cpp
//     StateTree 방식      : 상태 나무 에셋 ST_PGMonster — Monster/AI/StateTree/
// 왜 이렇게 나눴나: 감각·동작이 같아야 "판단 틀만 바꿨을 때 무엇이 달라지나" 를 공평하게 잴 수 있다
//   (측정: Monster/AI/PGMonsterAIStats, 기록: Docs/MonsterAIBenchmark_2026-09-23.md).
// 어느 방식으로 도나: 콘솔 PG.MonsterAI.Mode (0 = 코드, 1 = StateTree, 기본 1) 또는 명령줄 -PGMonsterAI=Classic|StateTree.
//   StateTree 에셋이 없으면 코드 방식으로 돈다(경고 한 줄).
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "MonsterAIController.generated.h"

class APGMonsterCharacter;
class UAIPerceptionComponent;
class UAISenseConfig_Hearing;
class UAISenseConfig_Sight;
class UPGMonsterStateTreeComponent;
class UStateTree;

UCLASS()
class PROJECTPG_API AMonsterAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMonsterAIController();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void OnPossess(APawn* InPawn) override;

	UFUNCTION(BlueprintCallable, Category = "Monster")
	AActor* GetTarget() const { return Target.Get(); }

	// 맞았다: 쏜 쪽이 표적이 될 수 있으면 바로 그쪽을 본다. 시야(앞 160도)만으로는 등 뒤에서 쏘는 플레이어를 끝까지 몰랐다.
	void NotifyDamagedBy(AActor* Attacker);

	// 몸 가장자리에서 이 거리 안의 플레이어는 방향과 상관없이 알아챈다(발소리·기척). 크리처 바로 뒤에 서 있어도 가만히 있던 문제.
	UPROPERTY(EditAnywhere, Category = "Monster")
	float CloseSenseRadius = 600.0f;

	// 귀와 눈을 관리하는 담당자. 감각에 뭔가 걸리면 "감지됐다"고 알려줌
	UPROPERTY(VisibleAnywhere, Category = "Monster")
	TObjectPtr<UAIPerceptionComponent> AIPerceptionComponent;

	// 마지막으로 감지한 뒤 이 시간이 지나면 포기한다.
	UPROPERTY(EditAnywhere, Category = "Monster")
	float LoseTargetSeconds = 15.0f;

	// 이동 목표를 다시 요청하는 간격. 매 프레임 MoveTo를 부르면 경로를 계속 새로 짠다.
	UPROPERTY(EditAnywhere, Category = "Monster")
	float RepathInterval = 0.4f;

	// StateTree 방식에서 쓰는 상태 나무. 만드는 명령: PG.BuildMonsterStateTree(에디터).
	UPROPERTY(EditAnywhere, Category = "Monster|AI")
	TSoftObjectPtr<UStateTree> MonsterStateTree;

	// 지금 StateTree 로 판단하나(아니면 코드 방식).
	bool IsUsingStateTree() const { return bUseStateTree; }

	// ---- StateTree 과제·조건이 부르는 것 (코드 방식도 같은 함수를 쓴다) ----
	APGMonsterCharacter* GetMonster() const;
	// 지금 쫓을 수 있는 표적(무효·죽은 표적이면 nullptr). 표적을 고르는 건 감각(Tick)이 한다.
	AActor* GetLiveTarget() const;
	// 영역을 벗어났거나 집으로 돌아가는 중이다.
	bool ShouldReturnHome() const;
	// 집으로 돌아가기 한 번. true = 아직 가는 중, false = 도착했거나 갈 필요 없음.
	bool TickReturnHome();
	// 잠복 중: 표적이 깨어나는 거리 안이면 깨어난다.
	void WakeIfClose(APGMonsterCharacter* Monster, float Distance);
	// 사거리 안: 보이면 때리고, 벽이 막으면 큰 몸은 벽을 부순다. 무언가 했으면 true(이번엔 움직이지 않는다).
	bool TryAttackOrSmash(APGMonsterCharacter* Monster, AActor* TargetActor, float Distance);
	// 쫓아가기: 길찾기(0.4초마다) + 막힘 감지 + 길이 없으면 직진·벽 부수기.
	void TickChase(APGMonsterCharacter* Monster, AActor* TargetActor, float Distance);

protected:
	// 코드 방식 판단(Monster/AI/Classic/MonsterAIClassicBrain.cpp).
	void TickClassic(APGMonsterCharacter* Monster, float Now);
	// 감각: 표적을 다시 고르고, 놓쳤으면 멈추고 잠복으로. 쫓을 표적이 있으면 그 표적, 없으면 nullptr.
	AActor* UpdateTargetFromSenses(APGMonsterCharacter* Monster, float Now);
	// 방식을 정한다(처음 한 번). StateTree 로 정했으면 에셋을 읽어 부품에 끼운다.
	void ResolveMode();
	bool bModeResolved = false;
	bool bUseStateTree = false;

	// StateTree 방식의 판단 부품. 코드 방식이면 꺼 둔다(틱 안 함).
	UPROPERTY(VisibleAnywhere, Category = "Monster|AI")
	TObjectPtr<UPGMonsterStateTreeComponent> StateTreeComponent;

	UFUNCTION()
	void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

	bool IsValidTarget(const AActor* Actor) const;
	// 표적 점수(낮을수록 좋음): 거리, 몬스터는 ×3 → 플레이어 우선.
	float TargetScore(const AActor* Candidate) const;
	// 지금 표적을 버리고 Candidate 로 갈아탈지. 30% 넘게 좋을 때만(멀티에서 두 플레이어 사이 두리번 방지).
	bool ShouldSwitchTarget(const AActor* Candidate) const;
	// 바로 옆에서 때린 몬스터를 받아치는 중이면 이 시각까지 표적 고정.
	float RetaliateUntil = -1.0f;

	// 지금 감각에 걸려 있는 것 중 가장 가까운 유효 표적. 없으면 nullptr.
	// 왜 필요한가: OnTargetPerceptionUpdated 는 "새로 보였다 / 사라졌다" 순간에만 온다. 빈 차는 표적이 아니라 무시했는데,
	// 그 차에 플레이어가 타도 시야 상태는 그대로라 이벤트가 다시 안 온다 → 차 안의 플레이어를 영영 못 알아봤다.
	AActor* FindPerceivedTarget(bool* bOutTargetStillPerceived = nullptr) const;
	// CloseSenseRadius 안의 가장 가까운 플레이어 폰. 시각·청각 목록과 따로 본다.
	AActor* FindCloseTarget() const;
	float LastPerceptionPollTime = -1000.0f;

	// 영역을 벗어나 집으로 돌아가는 중. 이 동안은 아무도 표적으로 잡지 않는다.
	bool bReturningHome = false;
	float LastReturnMoveTime = -1000.0f;
	// true 면 이번 Tick 은 돌아가기만 했다(나머지 판단을 건너뛴다).
	bool TickLeash(APGMonsterCharacter* Monster, float Now);

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;

	TWeakObjectPtr<AActor> Target;
	float LastSensedTime = -1000.0f;
	float LastRepathTime = -1000.0f;
	// 내비메시 경로(A*)를 못 찾으면 목표를 향해 그냥 걷는다. 내비가 아직 안 생겼거나 캡슐이 커서 길이 막힐 때의 보험.
	bool bDirectChase = false;
	// 막힘 감지: 1초마다 위치를 재서 거의 안 움직였으면 경로가 있어도 몸이 걸린 것 → 잠시 직진.
	FVector StuckCheckLocation = FVector::ZeroVector;
	float StuckCheckTime = -1000.0f;
	float DirectChaseUntil = -1000.0f;
};
