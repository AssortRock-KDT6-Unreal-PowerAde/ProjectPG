// 전함이 쏘는 유도 미사일. 미리 만들어 둔 액터 묶음(풀)에서 꺼내 쓰고 돌려받는다. (2026-09-20)
//
// 왜 풀인가 — 잔해(UPGDebrisSubsystem)에서 이미 겪은 그대로다:
//   쏠 때마다 SpawnActor + NewObject + RegisterComponent 를 하면 그 프레임에 비용이 몰린다.
//   미사일은 한 번에 여러 발이 나가고 드래곤 공중전에서는 계속 나간다 → 판 시작에 MaxAlive 개를 만들어 두고 재사용한다.
//   사용자 제안(9/20): "드래곤 맞으면 다시 객체풀로 재사용하면 되는 거 아니야? 함선에서?" — 그 말이 맞다.
//
// 왜 물리가 아닌가: 전함·탱크와 같은 이유. 유도는 "목표 쪽으로 도는 속도 제한"만 있으면 되고,
//   Chaos 에 400cm 짜리 몸을 수십 개 띄우면 NarrowPhase 가 다시 문제가 된다. 여기선 직선 이동 + 선 트레이스 하나면 끝난다.
//
// 폭발은 파티클 시스템 대신 "구 껍질 메시를 0.3초 동안 부풀리고 지우는" 싸구려 연출이다(프레임 방어).
// 메시가 아직 없어도(모델링 대기) 동작한다 — 안 보이는 채로 날아가 피해만 준다.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGMissileSubsystem.generated.h"

class AActor;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class PROJECTPG_API UPGMissileSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGMissileSubsystem* Get(const UWorld* World);

	// 미사일 한 발을 쏜다. Target 이 있으면 쫓아가고, 없으면 AimPoint(조준점)로 방향을 틀어 간다.
	// AimPoint 를 안 주면(영벡터) 예전처럼 Direction 으로 곧장 간다.
	// 풀이 비어 있으면 가장 오래된 것을 거둬 쓴다(잔해와 같은 규칙).
	bool Launch(const FVector& Start, const FVector& Direction, AActor* Target, AActor* Shooter,
		float DirectDamage, float BlastDamage, float BlastRadius, const FVector& AimPoint = FVector::ZeroVector);

	int32 CountActive() const;

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// 미리 만들어 둘 개수. 드래곤 공중전에서 한 번에 날아다닐 만큼.
	static constexpr int32 MaxAlive = 24;

private:
	struct FMissile
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UStaticMeshComponent> Body;
		TWeakObjectPtr<UStaticMeshComponent> Trail;
		TWeakObjectPtr<UStaticMeshComponent> Blast;
		TArray<TWeakObjectPtr<UStaticMeshComponent>> Shape; // 기본 도형 조립일 때의 조각들
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<AActor> Shooter;
		bool bActive = false;
		bool bExploding = false;
		FVector Pos = FVector::ZeroVector;
		FVector Dir = FVector::ForwardVector;
		float Speed = 0.0f;
		float Age = 0.0f;
		float ExplodeAge = 0.0f;
		float DirectDamage = 0.0f;
		float BlastDamage = 0.0f;
		float BlastRadius = 0.0f;
		FVector AimPoint = FVector::ZeroVector; // 표적이 없을 때 틀어 갈 점
		bool bHasAimPoint = false;
	};

	int32 Acquire();
	bool CreatePooled(FMissile& Missile);
	// 전용 메시가 없을 때 엔진 기본 도형(원통·원뿔·상자·구)으로 대신 조립한다.
	void BuildFallbackShapes(AActor* Actor, FMissile& Missile);
	void Deactivate(FMissile& Missile);
	void StartExplosion(FMissile& Missile, const FVector& Location, AActor* DirectHit);
	void Apply(FMissile& Missile);

	TArray<FMissile> Missiles;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PoolActors; // GC 방지

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BodyMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> TrailMesh;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> BlastMesh;
	// 기본 도형으로 만든 조각들(전용 메시가 왔을 때와 구분해 숨기고 켠다).
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> FallbackParts;
	bool bMeshesResolved = false;
	bool bUsingFallbackShapes = false;
};
