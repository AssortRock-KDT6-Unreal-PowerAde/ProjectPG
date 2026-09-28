// 변신 스포츠카의 로켓 부스터 비행. 마우스 오른쪽을 누르고 있으면 뒤쪽 부스터가 펼쳐지고 불꽃을 뿜으며 난다. (2026-09-20)
//
// 왜 따로 컴포넌트인가: 변신 여고생이 된 그 차(APGTransformNPCActor 가 만든 차)에만 붙인다. 다른 차·팩 원본은 그대로.
// 겉모습은 모델링 세션의 부스터 키트(스켈레탈 SK_CarBoosterKit — 차와 같은 공간이라 차 메시 원점에 그대로 붙인다,
//   펼치기/접기 애니, 노즐 소켓 ThrustL/R, 불꽃 메시 SM_BoosterFlame).
// 비행 방식(차는 Chaos 바퀴 물리 몸):
//  - 오른쪽 누르는 동안: 위로 오른다(중력보다 센 위쪽 가속). 누르고 있으면 계속 오른다.
//  - W 누르는 동안: 앞으로 간다. 이때는 위쪽 가속이 중력보다 살짝 약해 조금씩 내려간다 — 다시 오르려면 오른쪽을 누른다.
//    (사용자 9/20: "오른쪽 마우스 누르면 계속 위로, W 누르는 동안은 아주 조금씩 고도가 낮아지게")
//  - 연료: 한 번 넣으면 무제한이다. 공중에서 연료가 떨어져 추락하는 현실감은 이 게임에 필요 없다(사용자 결정).
//  - 공중에서는 차체를 수평으로 세우는 회전 가속(안 그러면 노즐 힘에 코가 들려 뒤집힌다 — 모델링 README 권고) + A/D 로 좌우 선회.
//  - 너무 높이(땅 위 MaxAltitude) 오르지 않게 위쪽 가속을 줄인다.
// 서버가 힘을 준다. 클라이언트는 누름 상태만 서버로 보낸다(탱크·차 입력과 같은 방식).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/PGNetPoseSmoother.h"
#include "PGFlightKitComponent.generated.h"

class UAnimSequence;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UCanvas;
struct FHitResult;

UCLASS(Blueprintable, ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGFlightKitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGFlightKitComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	TSoftObjectPtr<USkeletalMesh> BoosterMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	TSoftObjectPtr<UAnimSequence> DeployAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	TSoftObjectPtr<UAnimSequence> RetractAnim;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	TSoftObjectPtr<UStaticMesh> FlameMesh;

	// 위쪽 가속(중력의 배수). 1 이면 떠 있기만 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float LiftG = 1.35f;
	// 앞쪽 가속 cm/s²
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float ForwardAccel = 2600.0f;

	// 아케이드 비행 값: 누르는 대로 바로 움직인다(관성은 조금만).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float ArcadeSpeed = 3600.0f;      // 앞으로 36m/s
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float ArcadeClimbSpeed = 1500.0f; // 위로 15m/s. 더 빠르면 탱탱볼처럼 튀어 오른다(9/20 사용자)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float ArcadeSinkSpeed = 350.0f;   // W 만 누르면 초당 3.5m 씩 천천히 내려간다

	// W 로 낮게 날 때 바닥 위 이 높이(cm) 밑으로는 가라앉지 않는다(바닥이 오르막이면 따라 오른다). 0 이면 끈다.
	// 왜(9/23): W 만 누르면 계속 가라앉다가 바닥 1.2m 안에 들면 "착륙"으로 쳐서 물리로 떨어졌다.
	//   배 안 바닥은 앞쪽이 5m 쯤 높아(보이지 않는 바닥 상자가 2도 기울어 있다) 낮게 앞으로 날면 매번 바닥에 걸려 멈춘 것처럼 보였다.
	//   착륙 판정(1.2m)보다 높게 떠 있으니 W 를 누르는 동안은 안 내려앉고, W 를 떼면 예전처럼 내려앉는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight", meta = (ClampMin = "0.0"))
	float SkimHeightCm = 250.0f;

	// 공중에 **가만히** 있을 때(오른쪽도 W 도 안 누를 때)는 중력처럼 점점 빨리 떨어진다.
	// 사용자 요구(9/20): "확 추락하는 거 아니야. 공중에 가만히 있을 때만 점점 천천히 추락하는데,
	//   추락하는 속도가 빨라지는 거야." 그래서 시작은 ArcadeSinkSpeed(350) 그대로이고,
	//   가만히 있은 시간에 비례해 목표 침하 속도만 키운다. W 로 나아가는 중에는 적용하지 않는다.
	// 1초 버틸 때마다 침하 속도에 이만큼(cm/s)을 더한다. 1초 뒤 770, 2초 뒤 1190, 3초 뒤 1610.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float HoverFallAccel = 420.0f;
	// 아무리 오래 떨어져도 이 속도를 넘지 않는다. 너무 빠르면 한 프레임에 지면 판정(120cm)을 지나칠 수 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float HoverFallMaxSpeed = 2500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float ArcadeTurnRate = 70.0f;     // A/D 초당 70도
	// 속도가 목표까지 붙는 빠르기. 낮을수록 무겁다 — 누르는 즉시 최고 속도가 되면 가볍고 미끄럽게 느껴진다.
	// 1.8 → 1.35 (사용자 9/20: "운전 잘 되면서 살짝 더 무겁게"). 최고 속도는 그대로 두고 붙는 시간만
	// 0.55초 → 0.75초로 늘렸다. 방향 전환(ArcadeTurnRate)은 건드리지 않았다 — 그것까지 늦추면 둔해진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float ArcadeSmooth = 1.35f;
	// 격납고 입구 근처에서 갑판 높이로 끌어 주는 거리(cm)와 세기. 437m 짜리 배의 좁은 입구를
	// 손으로 정확히 맞춰 들어가는 건 사실상 불가능해서, 가까이 가면 살짝 도와준다(9/20 사용자: "들어갈 수가 없다").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float DockAssistRange = 9000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float DockAssistStrength = 1.6f;
	// 연료를 쓰게 할지. 기본은 무제한(사용자 결정 9/20) — 켜면 MaxFuelSeconds 만큼만 난다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	bool bLimitedFuel = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float MaxFuelSeconds = 20.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float FuelRegen = 1.0f;

	// W 로 나아갈 때의 위쪽 가속(중력 대비). 1 보다 조금 작아서 천천히 내려간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float GlideG = 0.94f;
	// 땅 위 이 높이(cm)를 넘으면 위쪽 가속을 줄여 더 안 오른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float MaxAltitude = 60000.0f; // 600m: 전함이 이륙해 더 높이 올라가도 쫓아갈 수 있게(9/20)
	// 부스터가 다 펼쳐지는 시간(초). 이 뒤부터 추진한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float DeploySeconds = 1.17f; // 펼치기 애니 길이(가져오기 로그: 1.167초 = 35프레임, 9/20 재작업본)

	// ---- 좌클릭 빔 ----
	// 왜: 몹이 차에 달라붙어 짜증난다는 사용자 요구(9/20).
	// 피해만 주면 "떼어냈다"가 안 읽혀서 맞은 몹을 차 반대쪽으로 밀기도 한다.
	// 전함 주포도 좌클릭이지만 이 차를 직접 몰고 있을 때만 쓰이므로 겹치지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float FireInterval = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float BeamRange = 12000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float BeamDamage = 18.0f;
	// 몇 발에 몹(보스 로봇 포함)이 쓰러지나. 0 이면 예전처럼 BeamDamage 고정 피해. 9/22 사용자: "8 대면 잡을 수 있게".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight", meta = (ClampMin = "0"))
	int32 BeamHitsToKill = 8;
	// 맞은 몹을 차 반대쪽으로 미는 속도(cm/s). 떼어내는 것이 목적이라 피해보다 이쪽이 중요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	float BeamPush = 1100.0f;
	// 십자 조준선을 그릴지. 차를 몰고 있는 동안에만 나온다.
	// 진짜 조준 UI 는 팀원이 만드는 중이라 이것은 임시 표식이다(사용자 9/20: "어딜 쏘는지 모르겠네").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight")
	bool bShowAimMarker = true;

	UFUNCTION(BlueprintCallable, Category = "PG|Flight")
	float GetFuel() const { return Fuel; }

protected:
	// 멀티(9/27): Reliable — 바뀔 때만 보내는데 한 번 잃으면 서버가 옛 입력(계속 상승·계속 선회)을 붙들고 있었다.
	//   InViewYaw: 조종하는 사람 시선. 컨트롤 회전은 서버로 안 오므로 아케이드 비행이 시선을 따라 돌려면 같이 보내야 한다.
	UFUNCTION(Server, Reliable)
	void ServerSetThrust(bool bInThrust, bool bInForward, float InYaw, float InViewYaw);
	float RemoteViewYaw = 0.0f;
	bool bHasRemoteViewYaw = false;

	// ---- 멀티(9/27): 부스터·불꽃·비행 상태·운전자 화면 위치 ----
	// 부스터 펼침·불꽃·빔·아케이드 비행(물리 끄기)이 서버에서만 일어나서, 클라에서는 부스터가 안 보이고 불꽃도 없었고,
	//   운전자 화면의 차는 물리로 땅에 붙어 있었다(서버의 차만 날았다). 상태를 복제해 각자 같은 모습을 만든다.
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION()
	void OnRep_Deployed();
	UFUNCTION()
	void OnRep_Arcade();
	UFUNCTION()
	void OnRep_DriverPose();
	// 부스터 펼치기/접기 애니(서버·클라 공통).
	void PlayDeployVisual();
	// 아케이드 비행의 물리 끄기/켜기(클라이언트 사본: 물리로 서버와 싸우지 않게).
	void ApplyArcadePhysicsOnClient();
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBeamFx(FVector_NetQuantize Muzzle, FVector_NetQuantize Impact);
	UPROPERTY(Replicated)
	bool bFlameOn = false;
	UPROPERTY(ReplicatedUsing = OnRep_DriverPose)
	FVector_NetQuantize10 DriverPoseLocation;
	UPROPERTY(ReplicatedUsing = OnRep_DriverPose)
	FRotator DriverPoseRotation = FRotator::ZeroRotator;
	// 멀티(9/28): 서버의 비행 속도도 같이 보낸다 — 받은 간격으로 속도를 짐작하면 위치가 불규칙하게 올 때(에디터에서 서버+창 둘) 짐작이 흔들려
	//   맞추기가 차를 앞뒤로 당겼다("날다가 조금씩 멈칫").
	UPROPERTY(Replicated)
	FVector_NetQuantize10 DriverPoseVelocity;
	// 멀티(9/28): 날아다니는 차를 받은 위치로 순간이동하지 않고 부드럽게 따라간다(PGNetPoseSmoother.h). 모는 본인 / 구경하는 사람.
	FPGNetPoseSmoother DriverSmoother;
	FPGNetPoseSmoother ProxySmoother;
	int32 DriverHardCorrections = 0; // 모는 사람 화면이 서버와 8m 넘게 어긋나 바로 옮긴 수(시험 확인용)
	float DriverMaxErrorCm = 0.0f;   // 모는 사람 화면과 서버의 가장 큰 차이(시험 확인용)
	FVector PendingCorrection = FVector::ZeroVector; // 아직 흘려 넣지 않은 서버와의 차이
public:
	int32 GetDriverHardCorrections() const { return DriverHardCorrections; }
	// 코드로 날고 있나(물리·바퀴 움직임이 꺼져 있다). 모든 컴퓨터에서 같다(bArcade 복제).
	bool IsFlying() const { return bArcade; }
	float GetDriverMaxErrorCm() const { return DriverMaxErrorCm; }
	// 구경하는 사람 화면: 차(APGVehiclePawn::PostNetReceiveLocationAndRotation)가 받은 위치를 넘긴다. 날고 있으면 받아 두고 true.
	bool ReceiveProxyPose(const FVector& Location, const FRotator& Rotation);
protected:

	void EnsureVisuals();
	void SetDeployed(bool bDeploy);
	void ApplyFlightForces(float DeltaTime);
	// 아케이드 비행: 물리를 끄고 코드로 직접 움직인다(탱크·전함과 같은 방식).
	void EnterArcade();
	void ExitArcade();
	// 전함 갑판에 내려앉았으면 배에 붙인다. 안 붙이면 배가 움직일 때 물리 차가 갑판을 뚫고 빠진다
	// (9/20 PIE: "내부로 들어가면 갑자기 차가 밑으로 빠져"). 다시 타면 뗀다.
	void DockToShipIfLanded();
	void UndockFromShip();
	void TickArcade(float DeltaTime);
	// 날아가다 부딪힌 소품을 부순다. 부쉈으면 true.
	bool SmashProps(const FHitResult& BlockingHit);
	// 좌클릭 빔: 달라붙은 몹을 떼어내는 용도다.
	void EnsureBeam();
	void FireBeam(const FVector& AimPoint);
	UFUNCTION(Server, Reliable)
	void ServerFireBeam(FVector AimPoint);
	// 화면 가운데가 닿는 곳. 빔은 이 지점을 향해 나간다.
	FVector ComputeAimPoint(const APlayerController* PC) const;
	// 화면 가운데 고정 십자선. 3D 표식을 맞는 지점에 그렸더니 벽에 반쯤 파묻혔다(9/20 사용자).
	void DrawCrosshair(UCanvas* Canvas, APlayerController* PC);
	FDelegateHandle CrosshairHandle;
	bool IsAirborne(float& OutHeight) const;
	USkeletalMeshComponent* GetCarMesh() const;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Booster;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Flames;

	bool bThrust = false;   // 오른쪽 버튼: 위로
	bool bForward = false;  // W: 앞으로(고도는 조금씩 내려간다)
	float YawInput = 0.0f;
	UPROPERTY(ReplicatedUsing = OnRep_Deployed)
	bool bDeployed = false;
	double DeployedAt = 0.0;
	double LastThrustAt = -100.0;
	double LastFlightLogTime = -100.0;
	float Fuel = 0.0f;
	// 공중에서는 물리를 끈다. 바퀴 차량을 공중에서 힘으로 밀면 "어디 쏠리듯 제멋대로" 간다(9/20 PIE).
	UPROPERTY(ReplicatedUsing = OnRep_Arcade)
	bool bArcade = false;
	bool bDocked = false;   // 배 갑판에 붙어 있나
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BeamMesh;
	double LastFireTime = -100.0;
	double BeamShownAt = -100.0;
	// 몬스터별 마지막 들이받기 시각. 스윕은 매 프레임 도는데 그때마다 피해를 주면
	// 한 번 스친 것으로 수십 번 맞는다(APGVehiclePawn::LastRamTime 과 같은 방식).
	TMap<TWeakObjectPtr<AActor>, float> LastRamTime;
	// 큰 몹에 마지막으로 닿은 시각. 붙어 있는 동안은 새 충돌로 안 친다(SmashProps).
	TMap<TWeakObjectPtr<AActor>, float> LastContactTime;
	double LastKnockTime = -100.0; // 겹침 조회를 마지막으로 돌린 시각(매 프레임 볼 이유가 없다)
	FVector ArcadeVelocity = FVector::ZeroVector;
	float ArcadeYaw = 0.0f;
	float HoverFallSeconds = 0.0f; // 가만히 떠 있은 시간(입력이 들어오면 0 으로)
	float LastSinkSpeed = 0.0f;  // 로그용: 지금 목표 침하 속도
	bool bDockAssisting = false; // 로그용: 격납고 도움이 고도를 잡고 있나
	FString LastArcadeBlocker;   // 로그용: 아케이드 이동을 막은 것("안 뜬다" 원인 찾기)

	bool bSentThrust = false;
	bool bSentForward = false;
	float SentYaw = 0.0f;
	float SentViewYaw = 0.0f;

public:
	// 빔 모양(가발 광선과 같은 메시를 쓴다). 9/23 블루프린트 분리 — BP_PGFlightKit 에서 바꾼다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Flight|Visual")
	TSoftObjectPtr<UStaticMesh> BeamMeshAsset;

	// 붙일 클래스: 설정(ProjectPG Visuals > Flight Kit Class)에 블루프린트가 있으면 그것, 없으면 C++.
	static UClass* GetSpawnClass();
};
