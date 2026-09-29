// 탈 수 있는 탱크(셔먼). 차(APGVehiclePawn)와 달리 Chaos 바퀴 물리를 쓰지 않고 코드로 움직인다.
//
// 왜 물리 차량이 아닌가: 궤도 차량은 Chaos 바퀴 구성으로 흉내 내기 어렵고(제자리 회전, 수십 톤 무게), 이 탱크의 목적은
//   "크리처를 끌고 다니며 쏘기 + 나무·바위를 밀고 지나가기"다. 그래서 움직임을 직접 정한다:
//   - W/S 전진·후진(가속·감속), A/D 제자리 회전.
//   - 차체 네 귀퉁이 아래로 땅을 재서 높이·기울기를 맞춘다(경사·언덕).
//   - 가는 방향 앞을 스윕해서 나무·바위·소품을 PGPhysicsUtil::TryKnockProp 으로 밀어 버린다. 건물처럼 큰 것은 막힌다.
//   - 포탑은 카메라가 보는 점을 향해 돌고(초당 TurretTurnRate), 포신은 그 점을 향해 들린다. 좌클릭 = 주포.
//   - 주포: 포구에서 포신 방향으로 선을 쏴서 맞은 곳에 폭발(범위 피해 + 소품 날리기 + 작은 몬스터 띄우기).
// 탑승·하차·카메라는 차와 같다(IInteractable F 탑승, IPGRideable 하차 문구, 탑승자를 숨겨 붙이고 Possess).
// 메시는 셔먼 팩을 차체·포탑·포신 셋으로 나눈 것(/Game/PG/Vehicles/Tank). 메시가 없어도 상자 몸으로 움직이기는 한다.
// 소리·이펙트는 나중에 Wwise 와 함께 붙인다(9/18 결정). 지금은 PG.Tank.DebugShots 로 탄도를 선으로 볼 수 있다.
#pragma once

#include "CoreMinimal.h"
#include "Common/PGStopTimer.h"
#include "Common/PGNetPoseSmoother.h"
#include "GameFramework/Pawn.h"
#include "Interaction/Interactable.h"
#include "Interaction/PGRideable.h"
#include "PGTankPawn.generated.h"

class UBoxComponent;
class UCameraComponent;
class UPGSeatComponent;
class USpringArmComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class PROJECTPG_API APGTankPawn : public APawn, public IInteractable, public IPGRideable
{
	GENERATED_BODY()

public:
	APGTankPawn();

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	// 멀티(9/27): 조종하는 사람이 바뀔 때(탑승·하차) — 서버와 그 사람 컴퓨터 모두에서 불린다. 탑승 F 가 바로 하차로 읽히지 않게.
	virtual void NotifyControllerChanged() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	// 캐릭터·보스가 탱크 위를 발판으로 삼지 않게(차와 같은 이유).
	virtual bool CanBeBaseForCharacter(APawn* Pawn) const override { return false; }

	// ---- IInteractable: F 로 탑승 ----
	virtual void Interact_Implementation(APawn* Interactor) override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	// ---- IPGRideable: 멈췄을 때만 "탱크 하차" ----
	virtual FText GetRiderPrompt() const override;
	virtual APawn* GetRiderPawn() const override { return RiderPawn; }

	UFUNCTION(BlueprintCallable, Category = "PG|Tank")
	bool Mount(APawn* Rider);

	UFUNCTION(BlueprintCallable, Category = "PG|Tank")
	bool Dismount();

	UFUNCTION(BlueprintCallable, Category = "PG|Tank")
	bool IsRidden() const { return RiderPawn != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "PG|Tank")
	bool IsWrecked() const { return bWrecked; }

	UFUNCTION(BlueprintCallable, Category = "PG|Tank")
	float GetHealth() const { return Health; }

	// 서버 전용. 주포 한 발(쿨다운이면 false). 조준은 지금 포신 방향 그대로.
	UFUNCTION(BlueprintCallable, Category = "PG|Tank")
	bool FireMainGun();

	// 서버 전용. 더 큰 몸(8배 로봇·보스)에게 밀쳐졌을 때: 이 속도(cm/s)로 옆으로 밀려나고 살짝 떴다가 떨어진다.
	// 탱크는 물리 몸이 없어서(코드로 움직임) 밀려나는 것도 TickDrive 에서 직접 처리한다.
	void Shove(const FVector& Velocity);

	// 부품은 읽기만 연다(2026-09-26 캡슐화 정리). 전에는 부품 포인터가 public 이라 밖에서 바꿔 끼울 수 있었다 —
	// 전함(APGBattleshipActor)은 처음부터 protected 였는데 같은 사람이 쓴 탱크만 규칙이 달랐다.
	UBoxComponent* GetBody() const { return Body; }
	UStaticMeshComponent* GetHullMesh() const { return HullMesh; }
	UStaticMeshComponent* GetTurretMesh() const { return TurretMesh; }
	UStaticMeshComponent* GetGunMesh() const { return GunMesh; }

	// ---- 모양 (메시 세 부품. 피벗: 차체 = 바닥 중앙, 포탑 = 회전축, 포신 = 고각축. 앞 = +X) ----
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	TSoftObjectPtr<UStaticMesh> HullAsset;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	TSoftObjectPtr<UStaticMesh> TurretAsset;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	TSoftObjectPtr<UStaticMesh> GunAsset;
	// 차체 피벗 기준 포탑 피벗, 포탑 피벗 기준 포신 피벗, 포신 피벗 기준 포구(전부 배율 1 의 cm).
	// 값은 셔먼 팩 조립 맵(M4_Combined)의 부품 위치(Tools/make_tank_parts.py 가 옮겨 구울 때 잰 것).
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	FVector TurretOffset = FVector(7.59f, 0.0f, 186.49f);
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	FVector GunOffset = FVector(104.74f, 0.0f, 44.78f);
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	FVector MuzzleOffset = FVector(329.7f, 0.0f, 0.0f);
	// 몸집 배율. 실제 셔먼(약 6m)보다 크게 — 5배 크리처·8배 보스와 싸워도 장난감처럼 안 보이게. 1.3 은 PIE 에서 작아 보여 1.5(약 9m).
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh", meta = (ClampMin = "0.5"))
	float TankScale = 1.5f;
	// 메시가 없을 때 쓰는 몸 크기(배율 1 의 반 크기, cm). 메시가 있으면 차체 메시 경계로 바뀐다.
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Mesh")
	FVector FallbackHalfExtent = FVector(300.0f, 150.0f, 140.0f);

	// ---- 주행 ----
	// 약 50km/h. 실제 셔먼(40km/h)보다 빠르게 — 크리처를 끌고 다니며(카이팅) 쏘려면 크리처 달리기보다 빨라야 한다.
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Drive")
	float MaxForwardSpeed = 1400.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Drive")
	float MaxReverseSpeed = 700.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Drive")
	float Acceleration = 700.0f;
	// 초당 회전(도). 제자리 회전 포함.
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Drive")
	float TurnRate = 55.0f;
	// 이 높이(배율 1 의 cm)까지의 턱·단(시설 바닥 슬래브, 연석, 낮은 계단)은 궤도로 타고 넘는다.
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Drive")
	float MaxStepHeight = 120.0f;
	// 이 크기(경계 구 반지름, cm)까지의 소품·나무·바위를 밀어 버린다. 건물 덩어리는 이보다 커서 막힌다.
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Drive")
	float KnockMaxRadius = 900.0f;

	// ---- 포 ----
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float TurretTurnRate = 70.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float GunMinPitch = -8.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float GunMaxPitch = 22.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float FireInterval = 1.6f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float Range = 25000.0f;
	// 맞은 대상에게 직접 + 폭발 범위. 8배 보스(체력 약 2560)는 여섯~일곱 발, B 크리처는 두세 발.
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float DirectDamage = 250.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float BlastDamage = 220.0f;
	UPROPERTY(EditAnywhere, Category = "PG|Tank|Gun")
	float BlastRadius = 650.0f;

	UPROPERTY(EditAnywhere, Category = "PG|Tank")
	float MaxHealth = 4000.0f;

protected:
	// ---- 부품(밖에서는 위 Get* 으로 읽기만) ----
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<UBoxComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<UStaticMeshComponent> HullMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<UStaticMeshComponent> TurretMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<UStaticMeshComponent> GunMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<UPGSeatComponent> Seat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Tank")
	TObjectPtr<UCameraComponent> Camera;

	UFUNCTION(Server, Unreliable)
	void ServerSetTankInput(float InThrottle, float InTurn, FVector_NetQuantize InAimPoint);

	UFUNCTION(Server, Reliable)
	void ServerFire();

	UFUNCTION(Server, Reliable)
	void ServerDismount();

	// 발사 연출(모든 화면). 이펙트·소리가 붙기 전까지는 디버그 선.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastShotFx(FVector_NetQuantize Muzzle, FVector_NetQuantize Impact, bool bExploded);

	UFUNCTION()
	void OnRep_Aim();

	// ---- 멀티(9/27): 운전자 화면의 탱크 위치 ----
	// 엔진의 위치 복제는 "조종하는 본인"에게는 보내지 않는다(그 사람이 스스로 움직인다고 보기 때문). 그런데 탱크는 서버가 코드로
	//   움직여서, 운전자 화면에서는 탱크가 제자리에 있었다(다른 사람 화면에서는 움직임). 운전자에게만 위치를 따로 보낸다.
	UPROPERTY(ReplicatedUsing = OnRep_DriverPose)
	FVector_NetQuantize10 DriverPoseLocation;
	UPROPERTY(ReplicatedUsing = OnRep_DriverPose)
	FRotator DriverPoseRotation = FRotator::ZeroRotator;
	UFUNCTION()
	void OnRep_DriverPose();
	// 멀티(9/28): 받은 위치로 순간이동하지 않고 매 프레임 부드럽게 따라간다(PGNetPoseSmoother.h). 운전자 화면 / 구경하는 사람 화면.
	FPGNetPoseSmoother DriverSmoother;
	FPGNetPoseSmoother ProxySmoother;
	FVector PendingCorrection = FVector::ZeroVector; // 모는 사람 화면(미리 계산): 아직 흘려 넣지 않은 서버와의 차이
	virtual void PostNetReceiveLocationAndRotation() override;

	void ApplyAssets();
	void TickDrive(float DeltaSeconds);
	void TickAim(float DeltaSeconds);
	void KnockAhead(const FVector& Direction, float Speed);
	void Explode(const FVector& Location, AActor* DirectHit);
	void RamMonster(AActor* Monster, const FVector& Direction, float Speed);
	void Wreck();
	FVector GetMuzzleLocation() const;
	bool TraceGround(const FVector& Local2D, FVector& OutGround) const;

	UPROPERTY(ReplicatedUsing = OnRep_RiderPawn, VisibleInstanceOnly, Category = "PG|Tank")
	TObjectPtr<APawn> RiderPawn;
	// 멀티(9/27): 클라에서 숨은 탑승자 충돌을 끈다(PGRide::OnRiderChangedOnClient).
	UFUNCTION()
	void OnRep_RiderPawn(APawn* OldRider);

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|Tank")
	float Health = 0.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|Tank")
	bool bWrecked = false;

	// 포탑 요(차체 기준)·포신 피치. 서버가 정하고 복제한다.
	UPROPERTY(ReplicatedUsing = OnRep_Aim, VisibleInstanceOnly, Category = "PG|Tank")
	float TurretYaw = 0.0f;
	UPROPERTY(ReplicatedUsing = OnRep_Aim, VisibleInstanceOnly, Category = "PG|Tank")
	float GunPitch = 0.0f;

	// 서버가 받은 입력
	float Throttle = 0.0f;
	float Turn = 0.0f;
	FVector AimPoint = FVector::ZeroVector;
	bool bHasAim = false;

	// 멀티(9/27): 복제 — 운전자 화면의 "탱크 하차" 안내가 이 속도를 본다(서버만 알아서 클라에선 늘 0 → 달리는 중에도 떴다).
	UPROPERTY(Replicated)
	float CurrentSpeed = 0.0f;
	float VerticalSpeed = 0.0f;
	// 밀쳐진 속도(수평)와 그때 생긴 헛도는 회전(도/초). 땅에 닿아 있으면 빠르게 줄어든다.
	FVector ShoveVelocity = FVector::ZeroVector;
	float ShoveSpin = 0.0f;
	float LastFireTime = -1000.0f;
	float MountedTime = -1000.0f;
	bool bDismountKeyWasDown = false;
	// 하차 안내는 완전히 멈추고 잠깐 뒤에 띄운다(GetRiderPrompt 가 const 라 mutable).
	mutable FPGStopTimer DismountPromptTimer;
	bool bFireKeyWasDown = false;
	// 차체 반 크기(배율 적용). 몸 상자의 높이 = 전체 높이 - 바닥 여유(작은 턱·돌부리는 넘는다).
	FVector HalfExtent = FVector::ZeroVector;
	float GroundClearance = 0.0f;
	TMap<TWeakObjectPtr<AActor>, float> LastRamTime;
};
