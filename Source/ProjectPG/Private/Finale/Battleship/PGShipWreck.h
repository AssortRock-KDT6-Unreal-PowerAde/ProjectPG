// 전함 추락 — 격추된 뒤 떨어지고 부서지는 연출.
// 2026-09-26 SOLID(한 책임): 전함 액터(APGBattleshipActor)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 전함 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 전함 액터에 그대로 두고 Ship-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 전함 액터의 비공개 멤버를 읽어야 해서 전함 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGBattleshipActor.h"
#include "PGShipWreck.generated.h"

UCLASS(Transient)
class UPGShipWreck : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGBattleshipActor* InShip) { Ship = InShip; }
	virtual UWorld* GetWorld() const override { return Ship ? Ship->GetWorld() : nullptr; }

	// ---- 추락(체력 0) — .cpp 의 "추락" 절 주석 참고 ----
	void BeginWreck(AActor* Killer, float LastHit);   // TakeDamage 가 체력 0 을 본 순간 한 번
	void TickWreck(float DeltaSeconds);               // 기울며 떨어지다 땅에 닿으면 WreckTouchdown
	void WreckTouchdown();                            // 땅에 닿는 순간: 큰 폭발·흙먼지·소품 날리기·불 줄이기
	void SpawnWreckPop(const FVector& Local, bool bWithFire); // 선체 한 자리의 폭발(구 + 흙먼지). bWithFire 면 불을 남긴다
	// 멀티(9/27): 그림만. 서버가 MulticastWreckPop / MulticastWreckTouchdown 으로 모든 컴퓨터에서 부른다.
	void PlayWreckPopFx(const FVector& Local, bool bWithFire);
	void PlayTouchdownFx(const FVector& Feet);
	bool FindGroundZBelow(const FVector& From, float& OutZ) const; // 배 바로 아래 땅 높이(WorldStatic 만). 못 찾으면 false
	void KnockPropsAround(const FVector& Where, float Reach, int32 Budget); // 둘레 소품 날리기(드래곤 KnockAround 와 같은 요령)

private:
	UPROPERTY()
	TObjectPtr<APGBattleshipActor> Ship;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UParticleSystemComponent>> WreckFires;
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> WreckFireFx;
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> WreckDustFx;
	bool bWreckGroundFound = false;   // 아래 땅을 선 검사로 찾았나(못 찾으면 z=20 기준으로 떨어진다)
	float WreckElapsed = 0.0f;
	float WreckFallSpeed = 0.0f;
	float WreckGroundZ = 20.0f;
	float WreckGroundScanTime = 0.0f; // 미끄러지는 동안 0.5초마다 땅을 다시 잰다
	float WreckNextPopIn = 0.0f;
	int32 WreckPopCount = 0;
	float WreckRollSign = 1.0f;
	float WreckKnockLeft = 0.0f;
	float WreckKnockTimer = 0.0f;
};
