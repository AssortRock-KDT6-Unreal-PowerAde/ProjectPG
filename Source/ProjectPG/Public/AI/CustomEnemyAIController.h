#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "CustomEnemyAIController.generated.h"

class ACustomCharacter;
class ACustomEnemyCharacter;
class UEnemyAIConfig;
class UStateTreeAIComponent;
class UAISenseConfig_Sight;
class UAISenseConfig_Damage;
class UAISenseConfig_Hearing;
class UEnemyCoverFinderComponent;

// 몬스터 한 마리의 "머리". 서버에만 있다(클라이언트엔 AI 컨트롤러가 안 생김).
// 하는 일:
//  - 눈(AI Perception)으로 보거나 맞으면 목표를 정하고, 상태 트리에 알림을 보낸다.
//  - 귀로 총소리·동료 외침을 들으면 그 소리가 가리키는 플레이어를 목표로 삼는다.
//  - 플레이어를 처음 보면 소리 질러 둘레 동료를 부른다.
//  - 상태 트리(순찰/전투/수색)가 부르는 이동·엄폐 찾기·사격을 실제로 한다.
//  - 기다리는 일(이동, 엄폐 찾기, 목표 놓침)엔 모두 시간 제한이 있고, 넘으면 로그를 남긴다.
// 무엇을 할지 "고르는" 일은 상태 트리 에셋(ST_Enemy)이 하고, 여기엔 "어떻게 하는지"만 있다.
UCLASS()
class PROJECTPG_API ACustomEnemyAIController : public AAIController
{
	GENERATED_BODY()
public:
	ACustomEnemyAIController(const FObjectInitializer& ObjectInitializer);

	// 눈 설정을 맞추고 상태 트리를 켠다. 컨트롤러·몬스터 둘 다 BeginPlay 를 마친 뒤에만 실제로 켠다.
	// 왜: 맵에 놓인 몬스터는 BeginPlay(표 읽기)보다 빙의가 먼저 일어나서, 그때는 감지 거리를 모른다.
	// 그래서 빙의·컨트롤러 BeginPlay·몬스터 BeginPlay 세 곳에서 부르고, 마지막 한 번만 통과한다.
	void StartAI();

	// ── 상태 트리 작업·조건이 읽는 값 ──
	ACustomEnemyCharacter* GetEnemy() const;
	const UEnemyAIConfig* GetConfig() const { return Config; }
	ACustomCharacter* GetTarget() const { return Target.Get(); }
	bool IsTargetVisible() const { return Target.IsValid() && bTargetVisible; }
	// 목표가 보이고 사거리(EnemyTable.AttackRange) 안이면 true.
	bool CanShootTarget() const;
	// 지금 엄폐 자리를 새로 찾아도 되는지(쏠 수 있는 거리 + 쉬는 시간 끝 + EQS 있음).
	bool IsCoverSearchReady() const;
	FVector GetLastKnownTargetLocation() const { return LastKnownTargetLocation; }
	const FVector& GetCoverLocation() const;
	const FVector& GetPeekLocation() const;
	// 순찰 중심(처음 놓인 자리) 둘레에서 걸어갈 수 있는 점 하나. 없으면 false.
	bool PickPatrolPoint(FVector& OutPoint) const;

	// ── 상태 트리 작업이 부르는 동작 ──
	// 이동 시작. 끝나면(도착/실패/시간 초과) OnDone(성공 여부)을 한 번 부른다.
	// - 이미 도착해 있으면 OnDone 을 부르지 않고 Succeeded 를 바로 돌려준다.
	// - 길이 없어 시작조차 못 하면 Failed 를 돌려준다.
	enum class EMoveStart : uint8 { Started, AlreadyThere, Failed };
	EMoveStart MoveWithTimeout(const FVector& Goal, const TCHAR* What, TFunction<void(bool)> OnDone);
	EMoveStart MoveToTargetWithTimeout(TFunction<void(bool)> OnDone);
	// 진행 중인 이동을 멈춘다. 그 이동의 OnDone 은 부르지 않는다(상태가 이미 바뀐 경우).
	void CancelMove();

	// 엄폐 자리 찾기 시작(실제 일은 UEnemyCoverFinderComponent). 찾으면 OnDone(true).
	// 시작 못 하면 false 를 돌려주고 OnDone 은 부르지 않는다.
	bool FindCoverAsync(TFunction<void(bool)> OnDone);
	void CancelCoverSearch();

	// 사격 한 번 확인: 몸이 목표를 향했는지·탄약·재장전을 보고 방아쇠를 당기거나 놓는다.
	// 목표가 없거나 안 보이면 false(사격 상태를 끝내야 함).
	bool UpdateFire();
	// 방아쇠 놓기. 사격 상태가 끝날 때 꼭 부른다.
	void StopFire();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result) override;

private:
	// 눈·귀·피격 감각에 무언가 들어오거나 빠질 때 엔진이 부른다.
	UFUNCTION() void HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);
	// 목표/자기 체력이 바뀌었을 때(0 이하면 죽음).
	UFUNCTION() void HandleTargetHealthChanged(float Current, float Maximum);
	UFUNCTION() void HandleTargetDestroyed(AActor* DestroyedActor);
	UFUNCTION() void HandleOwnHealthChanged(float Current, float Maximum);

	bool IsValidTarget(const AActor* Actor) const;
	void SetTarget(ACustomCharacter* NewTarget);
	// 목표를 눈으로 처음 봤을 때: 외치고, 외쳤으면 둘레 동료에게 "여기 적 있다" 소리를 퍼뜨린다.
	void ShoutTargetSpotted();
	void ForgetTarget(const TCHAR* Reason);
	void HandleLoseTargetTimeout();
	void HandleMoveTimeout();
	void SetCombatStance(bool bInCombat);
	void SendTreeEvent(const struct FGameplayTag& Tag);
	void ShutdownAI(const TCHAR* Reason);
	EMoveStart StartMove(const FAIMoveRequest& Request, const TCHAR* What, TFunction<void(bool)> OnDone);
	float GetMoveTimeLimit() const;

	// StartAI 에서 에셋을 넣은 뒤 붙인다(그 전엔 비어 있음).
	UPROPERTY(Transient)
	TObjectPtr<UStateTreeAIComponent> StateTreeComponent;
	UPROPERTY(VisibleAnywhere, Category="AI")
	TObjectPtr<UEnemyCoverFinderComponent> CoverFinder;
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Hearing> HearingConfig;
	UPROPERTY(Transient)
	TObjectPtr<const UEnemyAIConfig> Config;

	TWeakObjectPtr<ACustomCharacter> Target;
	bool bTargetVisible = false;
	// 지금 목표를 한 번이라도 눈으로 봤나. 소리로만 안 목표는 더 오래 쫓는다(AIConfig.HeardTargetDelay).
	bool bTargetEverSeen = false;
	FVector LastKnownTargetLocation = FVector::ZeroVector;
	FTimerHandle LoseTargetTimer;

	FVector PatrolOrigin = FVector::ZeroVector;

	// 지금 진행 중인 이동 하나(이동은 한 번에 하나뿐).
	TFunction<void(bool)> PendingMoveDone;
	FAIRequestID PendingMoveId;
	FString PendingMoveWhat;
	float PendingMoveLimit = 0.f;
	FTimerHandle MoveTimeoutTimer;

	bool bAIStarted = false;
	bool bFireHeld = false;
	bool bWarnedNoCoverQuery = false;
	// 이 시각 전엔 쏘지 않는다(목표를 본 직후 조준 시간, AIConfig.ReactionTime).
	double FireAllowedTime = 0.0;
	// 점사: 지금 점사가 끝나는 시각 / 다음 점사를 시작해도 되는 시각.
	double BurstEndTime = 0.0;
	double NextBurstTime = 0.0;
};
