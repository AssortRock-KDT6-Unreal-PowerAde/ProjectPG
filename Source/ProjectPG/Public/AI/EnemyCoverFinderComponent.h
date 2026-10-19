#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "Components/ActorComponent.h"
#include "EnemyCoverFinderComponent.generated.h"

class ACustomCharacter;
class ACustomEnemyCharacter;
class UEnemyAIConfig;
struct FEnvQueryResult;

// 몬스터가 숨을 엄폐 자리와, 그 옆에서 몸을 내밀어 쏠 자리를 찾는다(AI 컨트롤러에 붙음, 서버 전용).
// 순서: EQS_FindCover 로 "목표 눈에서 가려진 점"을 가까운 순으로 받음
//       → 앞에서부터 "고정된 벽 뒤인가" + "옆으로 한 걸음 나가면 목표가 보이는가"를 확인
//       → 처음 맞는 점을 저장하고 끝 알림.
// EQS 가 CoverQueryTimeout 안에 답을 안 주면 Error 로그 후 실패로 끝낸다.
UCLASS(ClassGroup=AI)
class PROJECTPG_API UEnemyCoverFinderComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UEnemyCoverFinderComponent();

	// 찾기 시작. 끝나면 OnDone(찾았나)을 한 번 부른다. 시작 못 하면 false(OnDone 안 부름).
	bool StartSearch(ACustomEnemyCharacter* Enemy, ACustomCharacter* Target, const UEnemyAIConfig& Config, TFunction<void(bool)> OnDone);
	// 진행 중인 찾기를 버린다(OnDone 안 부름).
	void Cancel();
	// 마지막 찾기 이후 쉬는 시간이 끝났나.
	bool IsCooldownOver() const;

	const FVector& GetCoverLocation() const { return CoverLocation; }
	const FVector& GetPeekLocation() const { return PeekLocation; }

protected:
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void HandleQueryDone(TSharedPtr<FEnvQueryResult> Result);
	void HandleQueryTimeout();
	void Finish(bool bFound);
	bool IsHiddenBehindStatic(const FVector& Cover) const;
	bool FindPeekFrom(const FVector& Cover, FVector& OutPeek) const;
	bool CanSeeTargetFrom(const FVector& Eye) const;

	TWeakObjectPtr<ACustomEnemyCharacter> Enemy;
	TWeakObjectPtr<ACustomCharacter> Target;
	UPROPERTY(Transient)
	TObjectPtr<const UEnemyAIConfig> Config;

	TFunction<void(bool)> PendingDone;
	int32 QueryId = INDEX_NONE;
	FTimerHandle QueryTimeoutTimer;
	double NextSearchTime = 0.;
	FVector CoverLocation = FVector::ZeroVector;
	FVector PeekLocation = FVector::ZeroVector;
};
