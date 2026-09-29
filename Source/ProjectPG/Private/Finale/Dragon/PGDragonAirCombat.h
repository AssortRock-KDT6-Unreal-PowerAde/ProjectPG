// 드래곤 공중전 — 솟아오르기, 선회·돌진, 표적 고르기, 불 뿜기.
// 2026-09-26 SOLID(한 책임): 드래곤 액터(APGDragonBoss)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 드래곤 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 드래곤 액터에 그대로 두고 Dragon-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 드래곤 액터의 비공개 멤버를 읽어야 해서 드래곤 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGDragonBoss.h"
#include "PGDragonAirCombat.generated.h"

UCLASS(Transient)
class UPGDragonAirCombat : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGDragonBoss* InDragon) { Dragon = InDragon; }
	virtual UWorld* GetWorld() const override { return Dragon ? Dragon->GetWorld() : nullptr; }

	void BeginRise(const FVector& GroundSpot, APGBattleshipActor* InShip);
	float KeepDistanceCm(const APGBattleshipActor* Against) const;
	void TickRise(float DeltaSeconds);
	void TickOrbit(float DeltaSeconds);
	void TickPass(float DeltaSeconds);
	void TickBreath(float DeltaSeconds);
	// 전함 > 밑에 보이는 몬스터 > 플레이어. 없으면 nullptr. OutWhy 에 고른 이유를 적는다(로그용).
	AActor* ChooseTarget(FString& OutWhy) const;
	// 땅에 있을 때 가까운 몬스터·플레이어(없으면 nullptr).
	AActor* FindGroundVictim() const;
	bool IsTargetAlive(const AActor* Target) const;
	// 입 위치(턱 뼈). 없으면 머리, 그것도 없으면 몸 앞쪽 어림값.
	FVector GetMouthLocation() const;
	// 표적에서 겨눌 점. 전함은 한가운데가 아니라 입에서 가장 가까운 선체 표면이다.
	FVector AimPointOf(const AActor* Target) const;
	// 브레스가 닿는지 한 번 재고 피해를 준다(BreathTraceInterval 마다).
	void BreathDamageTick(const FVector& Mouth, const FVector& Dir, float Elapsed);
	// 불꽃 이펙트를 켜고(입에서 Dir 로 Length 만큼) 끈다.
	void SetBreathFx(bool bOn, const FVector& From = FVector::ZeroVector, const FVector& Dir = FVector::ZeroVector, float Length = 0.0f);

private:
	UPROPERTY()
	TObjectPtr<APGDragonBoss> Dragon;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UParticleSystemComponent>> BreathFx;
	// 불길 몸통(입에서 표적 쪽으로 뻗는 원뿔). SetBreathFx 주석 참고.
	UPROPERTY(Transient)
	TObjectPtr<class UStaticMeshComponent> BreathCone;
	float OrbitAngle = 0.0f;
	FVector RiseFrom = FVector::ZeroVector;
	FVector RiseTo = FVector::ZeroVector;
	bool bNextAttackIsBreath = true; // 브레스와 돌진 번갈기
	float BreathStartTime = 0.0f;
	float LastBreathTick = 0.0f;
};
