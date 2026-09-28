// 탈 수 있는 자동차. Vehicle Variety Pack 의 블루프린트는 UE4 PhysX 차량(/Script/PhysXVehicles.WheeledVehicle)
// 기반이라 5.6 에서 열리지 않는다. 메시·스켈레톤·피직스 에셋만 쓰고 폰은 여기서 Chaos 차량으로 새로 만든다.
//
// 탑승·하차는 로봇과 같다: 탑승자를 숨겨 붙이고 PlayerController 가 이 폰으로 Possess. 좌석은 UPGSeatComponent.
// 입력은 검증용 캐릭터와 같은 키 폴링(W/S 가속·후진, A/D 조향, Space 핸드브레이크, F 하차).
#pragma once

#include "CoreMinimal.h"
#include "Common/PGStopTimer.h"
#include "ChaosVehicleWheel.h"
#include "Interaction/Interactable.h"
#include "Interaction/PGRideable.h"
#include "WheeledVehiclePawn.h"
#include "Engine/NetSerialization.h"
#include "PGVehiclePawn.generated.h"

class UCameraComponent;
class UPGSeatComponent;
class USkeletalMesh;
class USpringArmComponent;

// 앞바퀴: 조향. 뒷바퀴: 핸드브레이크. 네 바퀴 모두 구동(AWD)으로 두어 잔디·경사에서도 잘 나가게 한다.
UCLASS()
class PROJECTPG_API UPGVehicleWheelFront : public UChaosVehicleWheel
{
	GENERATED_BODY()
public:
	UPGVehicleWheelFront();
};

UCLASS()
class PROJECTPG_API UPGVehicleWheelRear : public UChaosVehicleWheel
{
	GENERATED_BODY()
public:
	UPGVehicleWheelRear();
};

UCLASS()
class PROJECTPG_API APGVehiclePawn : public AWheeledVehiclePawn, public IInteractable, public IPGRideable
{
	GENERATED_BODY()

public:
	APGVehiclePawn();

	virtual void Tick(float DeltaSeconds) override;
	// 멀티(9/28): 날고 있으면(물리 꺼짐) 받은 위치를 비행 부품이 부드럽게 따라가게 넘긴다. 땅에서는 엔진 그대로.
	virtual void PostNetReceiveLocationAndRotation() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// 캐릭터(특히 8배 보스)가 물리로 흔들리는 차 지붕을 발판으로 삼지 못하게 한다. false 면 올라서는 대신 옆으로 미끄러져 내려온다.
	virtual bool CanBeBaseForCharacter(APawn* Pawn) const override { return false; }
	virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
		FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	// 뒤집힌 채 멈춰 있으면 바로 세운다(R 키, 또는 자동).
	UFUNCTION(BlueprintCallable, Category = "PG|Vehicle")
	void Unflip();

	// ---- IInteractable: F 로 탑승 ----
	virtual void Interact_Implementation(APawn* Interactor) override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation() const override;

	// ---- IPGRideable: 탄 사람 화면의 문구. 거의 멈췄을 때만 "차량 하차" ----
	virtual FText GetRiderPrompt() const override;
	virtual APawn* GetRiderPawn() const override { return RiderPawn; }

	UFUNCTION(BlueprintCallable, Category = "PG|Vehicle")
	bool Mount(APawn* Rider);

	UFUNCTION(BlueprintCallable, Category = "PG|Vehicle")
	bool Dismount();

	UFUNCTION(BlueprintCallable, Category = "PG|Vehicle")
	bool IsRidden() const { return RiderPawn != nullptr; }

	// 스폰 직후(FinishSpawning 전) 다른 차 메시로 바꾼다. 팩의 네 대는 같은 스켈레톤·휠 본을 쓴다.
	UFUNCTION(BlueprintCallable, Category = "PG|Vehicle")
	void SetVehicleMesh(USkeletalMesh* NewMesh);

	// 차체 한 슬롯의 색을 바꾼다(변신차 남색). 서버에서 부르면 클라이언트 차에도 같은 색이 칠해진다(복제).
	void SetBodyPaint(UMaterialInterface* Material, FName Slot);

	// 멀티(9/27): 조종하는 사람이 바뀔 때(탑승·하차) — 서버와 그 사람 컴퓨터 모두에서 불린다.
	virtual void NotifyControllerChanged() override;

	// 프리셋 이름(SportsCar, Hatchback, Pickup, SUV) → 메시 경로. 설정의 차 블루프린트(없으면 C++)의 PresetMeshes 칸에서 읽는다.
	static bool GetPresetMeshPath(FName Preset, FString& OutPath);

	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGVehicle 에서 바꾼다) ----
	// 프리셋 이름 → 차 모델. 팩의 네 대는 같은 스켈레톤·휠 본을 써서 바꿔 끼워도 바퀴가 맞는다. 기본값 = 원래 코드 표.
	// 기본 차(프리셋 없음) 모델은 Mesh 컴포넌트의 Skeletal Mesh 에서 바꾼다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Vehicle|Visual")
	TMap<FName, TSoftObjectPtr<USkeletalMesh>> PresetMeshes;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Vehicle")
	TObjectPtr<UPGSeatComponent> Seat;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Vehicle")
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Vehicle")
	TObjectPtr<UCameraComponent> Camera;

protected:
	UFUNCTION(Server, Reliable)
	void ServerDismount();

	UFUNCTION(Server, Reliable)
	void ServerSetDriveInput(float Throttle, float Steering, bool bHandbrake);

	// ---- 멀티(9/28): 땅에서 달리는 차는 모는 사람 화면이 기준 ----
	// 왜: 모는 사람 컴퓨터와 서버가 각자 차 물리를 돌리면 결과가 조금씩 달라지고, 그때마다 엔진이 모는 사람 차를 서버 자리로
	//   끌어와 "툭" 튀었다(시험: 4초에 한 번 40~190cm). 그래서 모는 사람이 자기 차의 자리·속도를 1초에 30번 보내고
	//   서버는 그대로 맞춘다. 모는 사람 화면에는 서버 자리를 덮어쓰지 않는다(PostNetReceivePhysicState). 구경하는 사람은 그대로 서버를 본다.
	//   믿지 못할 값(서버 자리에서 20m 넘게 떨어진 자리)은 받지 않는다. 날고 있을 때는 비행 부품이 맡는다.
	UFUNCTION(Server, Unreliable)
	void ServerSetCarPose(FVector_NetQuantize10 Location, FRotator Rotation, FVector_NetQuantize10 LinearVelocity, FVector_NetQuantize10 AngularVelocityDeg, uint8 Epoch);
	// 서버가 직접 차를 옮겼을 때(경계 되밀기·뒤집힌 차 세우기 등) 모는 사람 화면에도 그 자리를 보낸다. 번호(Epoch)가 바뀌기 전에
	//   보낸 옛 자리는 서버가 버린다 — 안 그러면 모는 사람이 옛 자리를 계속 보내 서버 차를 되돌렸다(9/28 시험: 311m 어긋남).
	UFUNCTION(Client, Reliable)
	void ClientResetCarPose(FVector_NetQuantize10 Location, FRotator Rotation, FVector_NetQuantize10 LinearVelocity, uint8 Epoch);
	uint8 PoseEpoch = 0;
public:
	// 서버: 차를 코드로 옮긴 뒤 부른다(모는 사람이 원격이면 그 화면에도 옮긴다).
	void NotifyServerTeleport();
	// 시험(PG.CarPoseProbe): 모는 사람이 "바닥 4m 아래" 자리를 보낸 것처럼 서버 쪽 받기를 돌려 본다. 받아들였으면 true.
	bool ProbeBelowFloorPose();
protected:
	virtual void PostNetReceivePhysicState() override;
	double LastPoseSentAt = 0.0;
	int32 RejectedPoses = 0;

	void ApplyDriveInput(float Throttle, float Steering, bool bHandbrake);
	// 진행 방향 앞을 매 프레임 스윕해 소품을 날린다. 물리 몸의 NotifyHit 은 오지 않을 때가 있어 이쪽을 주 경로로 쓴다.
	void KnockAhead();

	UPROPERTY(ReplicatedUsing = OnRep_RiderPawn, VisibleInstanceOnly, Category = "PG|Vehicle")
	TObjectPtr<APawn> RiderPawn;
	// 멀티(9/27): 클라이언트 차의 주차 브레이크·탑승자 충돌을 서버와 맞춘다. 주차(SetParked)는 복제되지 않아
	//   클라 차는 바퀴가 잠긴 채 서버 보정과 싸웠다(덜컹거림·바퀴 안 돎).
	UFUNCTION()
	void OnRep_RiderPawn(APawn* OldRider);

	// 멀티(9/27): 서버가 스폰 때 고른 차 모양(픽업·SUV…). 복제하지 않으면 클라에선 모두 기본 차였다.
	UPROPERTY(ReplicatedUsing = OnRep_VehicleMeshOverride)
	TObjectPtr<USkeletalMesh> VehicleMeshOverride;
	UFUNCTION()
	void OnRep_VehicleMeshOverride();

	// 멀티(9/27): R 키·자동 뒤집기 복구를 클라이언트도(Unflip 은 서버 전용).
	UFUNCTION(Server, Reliable)
	void ServerUnflip();

	// 차체 덧칠(SetBodyPaint). 머티리얼 칠은 복제되지 않아 클라 차는 원래 색이었다(9/27 PIE: "내가 알던 변신카가 아니다").
	UPROPERTY(ReplicatedUsing = OnRep_BodyPaint)
	TObjectPtr<UMaterialInterface> BodyPaintMaterial;
	UPROPERTY(Replicated)
	FName BodyPaintSlot;
	UFUNCTION()
	void OnRep_BodyPaint();

	bool bDismountKeyWasDown = false;
	// 하차 안내는 완전히 멈추고 잠깐 뒤에 띄운다(GetRiderPrompt 가 const 라 mutable).
	mutable FPGStopTimer DismountPromptTimer;
	bool bUnflipKeyWasDown = false;
	float LastThrottle = 0.0f;

	// 달리다 몬스터를 들이받으면 속도 비례 피해 + 작은 몬스터는 날려 보낸다. KnockAhead 스윕에서 부른다.
	void RamMonster(AActor* Monster, const FVector& Direction, float Speed);
	TMap<TWeakObjectPtr<AActor>, float> LastRamTime;
	float MountedTime = -1000.0f;
	float FlippedSeconds = 0.0f;
	float LastSentThrottle = 0.0f;
	float LastSentSteering = 0.0f;
	bool bLastSentHandbrake = false;
};
