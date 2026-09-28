// 로봇. 몬스터를 상속받아 보스로도 쓰고, 플레이어가 F로 올라타 조종할 수도 있다.
//
// 클래스는 하나다. 누가 빙의(Possess)하느냐만 다르다.
//  - 보스: AMonsterAIController 가 빙의 → 몬스터 AI 그대로
//  - 탑승: 플레이어의 PlayerController 가 캐릭터에서 나와 이 로봇에 빙의 → 이 클래스의 Tick 이 입력을 읽음
// 컨트롤러와 폰은 상속 관계가 아니라 형제이고 Possess 로 붙는다. 그래서 몸(로봇)을 하나만 만들면 된다.
// 좌석은 OBJ-072 ~ 076 차량 요소로 만들어 둔 UPGSeatComponent 를 재사용한다.
#pragma once

#include "CoreMinimal.h"
#include "Common/PGStopTimer.h"
#include "Engine/NetSerialization.h"
#include "Interaction/Interactable.h"
#include "Interaction/PGRideable.h"
#include "Monster/PGMonsterCharacter.h"
#include "Robot/PGRobotRole.h"
#include "PGRobotCharacter.generated.h"

class UCameraComponent;
class UPGSeatComponent;
class USpringArmComponent;

UCLASS()
class PROJECTPG_API APGRobotCharacter : public APGMonsterCharacter, public IInteractable, public IPGRideable
{
	GENERATED_BODY()

public:
	APGRobotCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	// 멀티(9/27): 조종하는 사람이 바뀔 때(탑승·하차) — 서버와 그 사람 컴퓨터 모두에서 불린다. 탑승 F 가 바로 하차로 읽히지 않게.
	virtual void NotifyControllerChanged() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 큰 로봇이 걸어가며 부딪힌 소품을 밀쳐 낸다.
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	// ---- IInteractable: F 로 탑승 ----
	virtual void Interact_Implementation(APawn* Interactor) override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	// 탄 사람 화면의 문구. 멈춰 서 있을 때만 "로봇 하차".
	virtual FText GetRiderPrompt() const override;
	virtual APawn* GetRiderPawn() const override { return RiderPawn; }

	// 서버 전용. 탑승자를 숨기고 붙인 뒤 PlayerController 를 이 로봇으로 옮긴다.
	UFUNCTION(BlueprintCallable, Category = "PG|Robot")
	bool Mount(APawn* Rider);

	// 서버 전용. 좌석 옆 안전한 자리에 내려 주고 PlayerController 를 원래 캐릭터로 되돌린다.
	UFUNCTION(BlueprintCallable, Category = "PG|Robot")
	bool Dismount();

	UFUNCTION(BlueprintCallable, Category = "PG|Robot")
	bool IsRidden() const { return RiderPawn != nullptr; }

	// 탑승 중 마우스 왼쪽. 정면 부채꼴을 쓸어 몬스터에게 피해.
	UFUNCTION(BlueprintCallable, Category = "PG|Robot")
	void RobotAttack();

	// 스폰 직후(BeginPlay 전) 불러 보스/탑승용을 정한다.
	void ConfigureAsBoss(float Scale = 2.5f);

	// 보스일 때: 탑승 로봇·탱크에게 받는 피해 배율. 보스를 빨리 잡고 전함 단계로 넘어가게(9/20 사용자 결정 2배).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Robot")
	float BossHeavyDamageMultiplier = 2.0f;

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	void ConfigureAsRideable(float Scale = 1.0f);

	// 탈 수 있는 로봇인가(false = 몬스터 AI 가 붙는 보스). 바꾸는 길은 ConfigureAsBoss / ConfigureAsRideable 뿐이다.
	bool IsRideable() const { return GetRole().IsRideable(); }

	// 지금 역할(보스 / 탑승용). 역할을 고르는 곳은 여기 한 곳뿐이다 — 나머지는 전부 역할에게 맡긴다(PGRobotRole.h).
	// 역할은 상태가 없는 전략 객체라 기본 인스턴스를 그대로 쓴다(레벨·BP 에 저장될 것이 없다).
	const class UPGRobotRole& GetRole() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Robot")
	float RideWalkSpeed = 500.0f;

	// 보스 크기 배율. 건물만 한 로봇을 원해서 크게 두되, 내비메시는 작은 캡슐 기준이라 길을 못 찾으면 직진 추격으로 넘어간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Robot")
	float BossScale = 2.5f;

	// 탈것일 때의 크기 배율. 1 이면 사람 크기.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Robot")
	float RideScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Robot")
	float AttackSweepRadius = 90.0f;

	// 탑승 카메라 거리·높이 = 기본값(520 / 160) × RideScale × 이 배율. 1 이면 로봇 전체가 작게 보이고,
	// 줄일수록 가까워져 몸집이 크게 느껴진다. 카메라 거리는 여기 두 값만 바꾸면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Robot|Camera")
	float RideCameraDistanceFactor = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Robot|Camera")
	float RideCameraHeightFactor = 0.6f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Robot")
	TObjectPtr<UPGSeatComponent> Seat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Robot")
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Robot")
	TObjectPtr<UCameraComponent> Camera;

protected:
	// true 면 AI 를 붙이지 않고 F 로 탈 수 있다. false 면 몬스터 AI 가 붙는 보스.
	// protected 인 이유(2026-09-26 캡슐화 정리): 전에는 public 필드라 Configure* 함수를 거치지 않고 바꿀 수 있었고,
	//   그러면 크기·AI·카메라 설정과 어긋났다. 밖에서는 IsRideable() 로 읽기만 한다. 레벨·BP 에서 고치는 건 그대로 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Robot")
	bool bRideable = true;

	// 타고 있는 로봇이 죽으면 탑승자를 먼저 내린다. 몬스터 Die 가 컨트롤러를 떼어내는데, 그게 PlayerController 면 플레이어 캐릭터가 조종 불능으로 남았다.
	virtual void Die(AController* Killer) override;

	UFUNCTION(Server, Reliable)
	void ServerDismount();

	UFUNCTION(Server, Reliable)
	void ServerRobotAttack(FVector_NetQuantizeNormal Direction);

	void PerformRobotAttack(const FVector& Direction);
	// 멀티(9/27): 휘두르는 동작·방향을 모두의 화면에. 서버에서만 애니를 틀어 클라에선 맞기만 하고 휘두르는 게 안 보였다.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRobotAttackFx(float Yaw);
	// 탑승 로봇 한 방의 피해(작은 몹 한 방, 크리처 세 대). cpp 주석 참고.
	float RideAttackDamageFor(const APGMonsterCharacter* Target) const;
	// 탄 채 걸어가며 작은 몹 밟기.
	void TrampleSmallMonsters();
	float LastTrampleTime = -1000.0f;
	// 소품 밀쳐내기(스윕 자체는 APGMonsterCharacter::KnockAhead). 세기·가부는 역할이 정한다(보스 BossScale, 탑승 RideScale 기준).
	virtual float GetKnockRatio() const override;
	virtual bool CanKnockProps() const override;

	// 역할 클래스들은 로봇의 크기·사거리·카메라 같은 속을 직접 만진다(원래 이 클래스 안의 if 갈래였던 코드다).
	friend class UPGRobotRole;
	friend class UPGRobotBossRole;
	friend class UPGRobotRideRole;

	float LastLaunchLogTime = -1000.0f;
	FVector LastTickLocation = FVector::ZeroVector;

	// 탑승용 로봇 스킨(보스와 멀리서도 구분되게 다른 색). 몸·애니는 Visuals 칸(보스 기준)에서 바꾼다. 9/23 블루프린트 분리.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Robot|Visual")
	TSoftObjectPtr<USkeletalMesh> RideableSkinMesh;

	UPROPERTY(ReplicatedUsing = OnRep_RiderPawn, VisibleInstanceOnly, Category = "PG|Robot")
	TObjectPtr<APawn> RiderPawn;
	// 멀티(9/27): 클라에서 숨은 탑승자 충돌을 끈다(PGRide::OnRiderChangedOnClient).
	UFUNCTION()
	void OnRep_RiderPawn(APawn* OldRider);

	bool bDismountKeyWasDown = false;
	// 하차 안내는 완전히 멈추고 잠깐 뒤에 띄운다(GetRiderPrompt 가 const 라 mutable).
	mutable FPGStopTimer DismountPromptTimer;
	bool bAttackKeyWasDown = false;
	float MountedTime = -1000.0f;
};
