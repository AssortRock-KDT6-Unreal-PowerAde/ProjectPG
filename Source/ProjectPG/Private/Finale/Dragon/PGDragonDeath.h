// 드래곤 죽음 — 죽는 동작, 추락, 땅에 부딪힘과 먼지.
// 2026-09-26 SOLID(한 책임): 드래곤 액터(APGDragonBoss)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 드래곤 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 드래곤 액터에 그대로 두고 Dragon-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 드래곤 액터의 비공개 멤버를 읽어야 해서 드래곤 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGDragonBoss.h"
#include "PGDragonDeath.generated.h"

UCLASS(Transient)
class UPGDragonDeath : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGDragonBoss* InDragon) { Dragon = InDragon; }
	virtual UWorld* GetWorld() const override { return Dragon ? Dragon->GetWorld() : nullptr; }

	void TickDying(float DeltaSeconds);
	// 죽음의 단계 전환. 각각 TickDying 위 주석 참고.
	void BeginDeathFall();
	void CrashLand(bool bFromTheSky);
	// 처박힌 자리 둘레에 흙먼지를 여러 개 띄운다. Wave 0 = 닿는 순간, 1·2 = 조금 뒤에 번지는 먼지.
	void SpawnCrashDust(const FVector& Feet, int32 Wave);
	// 멀티(9/27): 처박힘의 그림(폭발 구·흔들림·먼지). 서버가 MulticastCrashFx 로 모든 화면에서 부른다.
	void PlayCrashFx(const FVector& Feet, int32 Wave);
	void PlayCrashDust(const FVector& Feet, int32 Wave); // 먼지만(Wave 별 배치)
	// 클라이언트: 폭발 구를 이 화면 시계로 부풀린다(서버는 TickDying 의 Crashed 단계가 한다).
	void TickCrashBlastLocal();

private:
	UPROPERTY()
	TObjectPtr<APGDragonBoss> Dragon;

	// 땅에 처박힐 때 한 번 부풀었다 사라지는 폭발 구(미사일 폭발과 같은 메시·재질).
	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> CrashBlast;
	FVector2D DeathTarget = FVector2D::ZeroVector; // 떨어질 자리(맵 안쪽 땅)
	float DeathRollSign = 1.0f;                  // 어느 쪽으로 기울며 떨어지나
	float DeathSpinDegPerSec = 0.0f;             // 떨어지며 도는 속도(도/초)
	float CrashTime = 0.0f;                      // 땅에 닿은 시각(StateTimer)
	float DieAnimSeconds = 0.0f;
	double LocalCrashAt = -1.0;                  // 클라이언트: 이 화면에서 처박힘을 받은 시각(음수 = 아직)
};
