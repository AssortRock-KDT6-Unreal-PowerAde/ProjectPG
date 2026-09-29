// 부서진 소품(잔해)을 "미리 만들어 둔 대리 액터 묶음(풀)"으로 연출하는 월드 서브시스템. (2026-09-19)
//
// 왜 만들었나 — 공장·기둥을 부술 때 순간 끊김의 원인을 한 번에 줄이려고:
//  1) 매번 SpawnActor + NewObject + RegisterComponent: 부술 때마다 액터·컴포넌트를 새로 만들어 한 프레임에 비용이 몰렸다.
//     → 판 시작(검은 로딩 화면 동안)에 MaxAlive 개를 미리 만들어 두고, 메시만 바꿔 끼워 재사용한다.
//  2) Chaos 물리: 큰 상자가 건물의 복잡한 충돌(삼각형 수천 개)과 닿으면 NarrowPhase 가 폭발했다(Insights 로 측정).
//     → 진짜 물리는 작은 소품만, 동시에 PG.Debris.MaxPhysics 개까지. 그 이상(공장이 통째로 무너질 때)과 큰 판·나무는
//       중력·바닥 튕김·마찰·벽 반사를 직접 계산하는 "가벼운 흉내"로 돌린다(조각당 선 트레이스 몇 개, 충돌 쌍이 안 생긴다).
//     사용자 피드백: 진짜 물리끼리 부딪히는 맛이 더 자연스러웠다 → 평소엔 진짜 물리, 한꺼번에 몰릴 때만 흉내로 넘어간다.
//  3) 큰 조각을 "가라앉히기"로 때웠더니 땅에 박혀 보였다 → 큰 것·나무는 바닥 모서리를 경첩으로 넘어뜨리고, 누운 뒤에도
//     차·탱크·몬스터가 다시 치면 밀려난다(Kick). 수명이 다하면 화면에 안 보일 때 조용히 거둔다(보이면 15초 기다렸다가 사라짐 — 땅속으로 가라앉히지 않는다).
//
// 가벼운 흉내 상태의 상자(DebrisBox)는 "질의 전용" 충돌이다: 물리로 무엇도 막지 않지만, 차·탱크·몬스터의 물체 종류 스윕
// (WorldDynamic)에는 잡혀서 PGPhysicsUtil::TryKnockProp → Kick 으로 다시 밀 수 있다
// (물체 종류 질의는 응답 설정을 보지 않고 물체 종류만 본다 — 엔진 CollisionQueryFilterCallback.cpp 확인).
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGDebrisSubsystem.generated.h"

class AActor;
class UBoxComponent;
class UParticleSystem;
class UPrimitiveComponent;
class UStaticMesh;
class UStaticMeshComponent;

UENUM()
enum class EPGDebrisMotion : uint8
{
	Fly,  // 작은 소품: 튀어 날아가 구르고 멈춘다
	Tip,  // 큰 판·나무: 밀린 쪽 바닥 모서리를 축으로 넘어간다
};

UCLASS()
class PROJECTPG_API UPGDebrisSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGDebrisSubsystem* Get(const UWorld* World);

	// 원본 메시 자리에서 잔해 하나를 시작한다. 풀이 꽉 차면 가장 오래된(가능하면 안 보이는) 조각을 거둬 쓴다.
	// Source: 원본 컴포넌트(재질 복사·바닥 찾기에서 제외). 이 호출 뒤에 원본을 숨기거나 인스턴스를 지우는 건 부르는 쪽 몫.
	// Impulse: 속도 변화(cm/s). MassKg·bHeavy 는 진짜 물리로 돌 때의 무게와 "돌·콘크리트는 덜 튄다" 규칙.
	AActor* Launch(UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse, EPGDebrisMotion Motion,
		float MassKg, bool bHeavy, const UPrimitiveComponent* Source, AActor* Instigator, bool bLightweightOnly = false);

	// 이미 잔해인 상자를 다시 친다. 잔해가 아니면 false.
	bool Kick(const UPrimitiveComponent* Component, const FVector& Impulse, const AActor* Instigator);

	bool IsDebris(const UPrimitiveComponent* Component) const;

	// 받침 무너짐: 부품 하나가 사라진 자리(Removed) 바로 위에 얹혀 있던 부품이 더는 받쳐지지 않으면 떨어뜨린다.
	// 1층을 다 부쉈는데 2층이 공중에 떠 있던 문제(9/20 PIE). 한 프레임에 몇 건씩만 처리한다(부술 때 몰리지 않게).
	void QueueSupportCheck(const FBox& Removed);

	// (받침 무너짐의 "그대로 떨어지기" 는 이제 PGPhysicsUtil::TryKnockProp 의 bFallOnly 인자로 넘긴다 — 9/26, 전역 스위치 제거.
	//  20m 바닥 판을 Chaos 로 굴리면 공장 붕괴 때처럼 NarrowPhase 가 터지므로 크기와 상관없이 가벼운 흉내로 떨어뜨린다.)

	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

private:
	struct FPiece
	{
		TWeakObjectPtr<AActor> Actor;
		TWeakObjectPtr<UBoxComponent> Box;
		TWeakObjectPtr<UStaticMeshComponent> Visual;
		bool bActive = false;
		bool bResting = false;
		bool bTipping = false;
		bool bVanishing = false;
		bool bDusted = false;
		bool bHeavy = false;
		FVector Pos = FVector::ZeroVector;    // 상자 중심(월드)
		FQuat Rot = FQuat::Identity;
		FVector Vel = FVector::ZeroVector;    // cm/s
		FVector AngVel = FVector::ZeroVector; // rad/s (월드 축)
		FVector Half = FVector(10.0f);        // 상자 반크기(스케일 적용)
		FVector VisualScale = FVector::OneVector;
		FVector VisualOffset = FVector::ZeroVector;
		float MassKg = 60.0f;
		float GroundZ = 0.0f;
		FVector2D GroundSampleXY = FVector2D::ZeroVector;
		double StartTime = 0.0;
		double LastKickTime = -100.0;
		float SettleTimer = 0.0f;
		float VanishAlpha = 1.0f;
		// 넘어지기(Tip) 상태
		FVector Hinge = FVector::ZeroVector;
		FVector TiltAxis = FVector::ZeroVector;
		FVector TipStartPos = FVector::ZeroVector;
		FQuat TipStartRot = FQuat::Identity;
		float TipAngle = 0.0f;   // rad
		float TipSpeed = 0.0f;   // rad/s
		float TipGravity = 0.0f; // 넘어가는 가속(키가 클수록 느리게)
		TWeakObjectPtr<const UPrimitiveComponent> IgnoreForGround;
	};

	int32 AcquirePiece();
	bool CreatePooledActor(FPiece& Piece);
	int32 FindPiece(const UPrimitiveComponent* Component) const;
	int32 CountSimulating() const;
	void Deactivate(FPiece& Piece);
	void StartPhysics(FPiece& Piece, const FVector& Velocity, const FVector& AngularVelocity);
	void StopPhysics(FPiece& Piece);
	bool TraceFirst(const FPiece& Piece, const FVector& From, const FVector& To, FHitResult& OutHit);
	float TraceGround(const FPiece& Piece, const FVector& From);
	void TickFly(FPiece& Piece, float DeltaTime);
	void TickTip(FPiece& Piece, float DeltaTime);
	void ApplyTransform(FPiece& Piece);
	void SetVanish(FPiece& Piece, float Alpha);
	void PlayDust(const FVector& Location, float Size);
	void ProcessSupportChecks();
	bool IsSupported(const FBox& Bounds, const UPrimitiveComponent* Candidate, int32 CandidateItem) const;

	struct FSupportCheck
	{
		FBox Removed;
		int32 Retries = 0;
	};
	TArray<FSupportCheck> PendingSupportChecks;

	TArray<FPiece> Pieces;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> PoolActors; // GC 방지 + 바닥 찾기에서 잔해끼리 제외

	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> DustEffect;
	double LastDustTime = -100.0;
	int32 TracesThisFrame = 0;
};
