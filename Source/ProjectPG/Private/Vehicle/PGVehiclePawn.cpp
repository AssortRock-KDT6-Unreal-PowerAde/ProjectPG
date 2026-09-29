#include "Vehicle/PGVehiclePawn.h"
#include "Common/PGNetPoseSmoother.h"
#include "Vehicle/PGFlightKitComponent.h"
#include "Interaction/PGRideHelpers.h"
#include "Common/PGVisualSettings.h"
#include "Common/PGCameraUtil.h"

#include "Camera/CameraComponent.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Common/PGKeyPolling.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Combat/PGCreatureAccess.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGObjectTypes.h"
#include "Objects/PGVehicleComponents.h"
#include "Vehicle/PGVehicleAnimInstance.h"
#include "Common/PGPhysicsUtil.h"
#include "UObject/ConstructorHelpers.h"

// ---- 바퀴 ----

UPGVehicleWheelFront::UPGVehicleWheelFront()
{
	AxleType = EAxleType::Front;
	WheelRadius = 36.0f;
	WheelWidth = 24.0f;
	bAffectedBySteering = true;
	bAffectedByEngine = true;
	bAffectedByBrake = true;
	bAffectedByHandbrake = false;
	MaxSteerAngle = 40.0f;
	// 맵 바닥에 벽돌·턱이 많다. 서스펜션이 더 오르내려야 걸리지 않는다.
	SuspensionMaxRaise = 14.0f;
	SuspensionMaxDrop = 18.0f;
}

UPGVehicleWheelRear::UPGVehicleWheelRear()
{
	AxleType = EAxleType::Rear;
	WheelRadius = 36.0f;
	WheelWidth = 24.0f;
	bAffectedBySteering = false;
	bAffectedByEngine = true;
	bAffectedByBrake = true;
	bAffectedByHandbrake = true;
	SuspensionMaxRaise = 14.0f;
	SuspensionMaxDrop = 18.0f;
}

// ---- 차 ----

bool APGVehiclePawn::GetPresetMeshPath(FName Preset, FString& OutPath)
{
	// 설정의 차 블루프린트(없으면 C++ 기본값)의 PresetMeshes 칸에서 찾는다(9/23 블루프린트 분리 — 원래는 여기 적힌 표였다).
	const APGVehiclePawn* Defaults = Cast<APGVehiclePawn>(UPGVisualSettings::VehicleSpawnClass()->GetDefaultObject());
	const TSoftObjectPtr<USkeletalMesh>* Found = Defaults ? Defaults->PresetMeshes.Find(Preset) : nullptr;
	if (!Found || Found->IsNull())
		return false;
	OutPath = Found->ToSoftObjectPath().ToString();
	return true;
}

APGVehiclePawn::APGVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetCullDistanceSquared(FMath::Square(90000.0f)); // 멀티(9/27): 큰 탈것이 150m 밖에서 사라졌다 나타나지 않게

	// 프리셋 모델 칸 기본값(9/23 블루프린트 분리 전 GetPresetMeshPath 표 그대로).
	{
		const FString Base = TEXT("/Game/VehicleVarietyPack/Skeletons/");
		auto Car = [&Base](const TCHAR* Name) { return TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Base + FString::Printf(TEXT("%s.%s"), Name, Name))); };
		PresetMeshes.Add(TEXT("SportsCar"), Car(TEXT("SK_SportsCar")));
		PresetMeshes.Add(TEXT("Hatchback"), Car(TEXT("SK_Hatchback")));
		PresetMeshes.Add(TEXT("Pickup"), Car(TEXT("SK_Pickup")));
		PresetMeshes.Add(TEXT("SUV"), Car(TEXT("SK_SUV")));
	}
	// 기본 차는 스포츠카. 생성자에서 로드해야 Chaos 차량이 피직스 에셋을 보고 바퀴를 세팅한다.
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> DefaultMesh(
		TEXT("/Script/Engine.SkeletalMesh'/Game/VehicleVarietyPack/Skeletons/SK_SportsCar.SK_SportsCar'"));
	if (DefaultMesh.Succeeded())
		GetMesh()->SetSkeletalMeshAsset(DefaultMesh.Object);
	// F 상호작용 스윕이 Visibility 채널이라 차체가 걸리게 해 둔다.
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->SetSimulatePhysics(true);
	// 물리로 움직이는 몸은 이 플래그가 없으면 NotifyHit 이 아예 안 온다(캐릭터는 이동 스윕이라 필요 없음).
	// 차로 들이받아도 소품이 안 날아가던 원인.
	GetMesh()->SetNotifyRigidBodyCollision(true);
	// 충돌로 생긴 회전(스핀·들썩임)을 빨리 죽인다. 잔해를 밟거나 스칠 때마다 차 머리가 돌아가 방향 잡기가 어려웠다.
	// 조향은 타이어 힘이라 이 정도 감쇠로는 회전 반경이 거의 안 변한다.
	GetMesh()->SetAngularDamping(1.2f);
	// 애님 블루프린트 없이 바퀴가 돌게 하는 C++ 애님 인스턴스.
	GetMesh()->SetAnimInstanceClass(UPGVehicleAnimInstance::StaticClass());

	UChaosWheeledVehicleMovementComponent* Movement = CastChecked<UChaosWheeledVehicleMovementComponent>(GetVehicleMovementComponent());
	Movement->WheelSetups.SetNum(4);
	Movement->WheelSetups[0].WheelClass = UPGVehicleWheelFront::StaticClass();
	Movement->WheelSetups[0].BoneName = TEXT("Wheel_Front_Left");
	Movement->WheelSetups[1].WheelClass = UPGVehicleWheelFront::StaticClass();
	Movement->WheelSetups[1].BoneName = TEXT("Wheel_Front_Right");
	Movement->WheelSetups[2].WheelClass = UPGVehicleWheelRear::StaticClass();
	Movement->WheelSetups[2].BoneName = TEXT("Wheel_Rear_Left");
	Movement->WheelSetups[3].WheelClass = UPGVehicleWheelRear::StaticClass();
	Movement->WheelSetups[3].BoneName = TEXT("Wheel_Rear_Right");
	// 넓은 맵을 가로지르는 용도라 힘을 넉넉히. 미세 튜닝은 나중에 값만 바꾸면 된다.
	Movement->EngineSetup.MaxTorque = 690.0f; // 질량을 1300→1800 으로 올린 만큼(×1.38) 같이 올려 가속감은 그대로
	Movement->EngineSetup.MaxRPM = 5500.0f;
	// 토크 커브가 비어 있으면 Chaos 가 "mechanical simulation" 을 통째로 끈다(1차 PIE에서 차가 공중에 멈춰 있던 원인).
	// RPM → 토크 비율. 저속에서 힘이 붙고 고회전에서 살짝 빠지는 평범한 곡선.
	FRichCurve* Torque = Movement->EngineSetup.TorqueCurve.GetRichCurve();
	Torque->Reset();
	Torque->AddKey(0.0f, 0.8f);
	Torque->AddKey(1500.0f, 1.0f);
	Torque->AddKey(4000.0f, 1.0f);
	Torque->AddKey(5500.0f, 0.6f);
	Movement->EngineSetup.EngineIdleRPM = 900.0f;
	// 1300 이었을 때 나무·바위에 박으면 차가 공처럼 튀었다. 무거울수록 같은 충돌에서 속도 변화가 작다.
	Movement->Mass = 1800.0f;
	// 뒤집힘 방지. 관성 텐서 배율(앞, 옆, 위 축): 앞축 = 옆으로 구르기(롤), 옆축 = 앞뒤로 들리기(피치), 위축 = 좌우로 돌기(요).
	// 롤·피치만 키우면 같은 충격에도 차체가 덜 기울어 잔해를 밟거나 턱에 걸려도 안 넘어간다. 요는 1 그대로라 핸들 감각은 안 변한다.
	// 운전이 힘들던 원인이 "튕김"이 아니라 "너무 잘 뒤집힘"이었다(9/17 PIE).
	Movement->InertiaTensorScale = FVector(3.0f, 2.2f, 1.0f);
	Movement->TransmissionSetup.bUseAutomaticGears = true;
	Movement->bReverseAsBrake = true;
	// 주차 상태로 시작: 네 바퀴 전부 핸드브레이크 토크. 운전자가 없으면 입력(핸드브레이크 포함)이 0 으로 지워져서
	// 바퀴가 자유롭게 굴렀고, 캐릭터가 걸어서 밀기만 해도 1.3톤 차가 굴러갔다. 주차는 컨트롤러와 상관없이 물리에 들어간다.
	// 달릴 때 탄성(부딪힘 반응)은 그대로다 — 바퀴만 잠그는 것이라 들이받으면 여전히 튕긴다.
	Movement->SetParked(true);

	Seat = CreateDefaultSubobject<UPGSeatComponent>(TEXT("DriverSeat"));
	Seat->SetupAttachment(GetMesh());

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(GetMesh());
	// 차가 뒤집히면 스프링암도 같이 뒤집혀 카메라가 땅 밑으로 갔다. 회전을 월드 기준(절대)으로 두면
	// 차체가 어떻게 구르든 카메라는 항상 위에서 본다. 붙는 점은 차 중심 높이.
	CameraArm->SetUsingAbsoluteRotation(true);
	CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 80.0f));
	CameraArm->TargetArmLength = 700.0f;
	CameraArm->SocketOffset = FVector(0.0f, 0.0f, 160.0f);
	CameraArm->bUsePawnControlRotation = true;
	// 소품 옆을 지날 때 카메라 충돌 검사가 카메라를 확 당겨 시야가 바뀌었다. 넓은 야외 맵이라 끄고, 대신 살짝 늦게 따라가게 한다.
	CameraArm->bDoCollisionTest = false;
	CameraArm->bEnableCameraLag = true;
	CameraArm->CameraLagSpeed = 8.0f;
	CameraArm->bEnableCameraRotationLag = true;
	CameraArm->CameraRotationLagSpeed = 10.0f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;
}

void APGVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGVehiclePawn, RiderPawn);
	DOREPLIFETIME(APGVehiclePawn, BodyPaintMaterial);
	DOREPLIFETIME(APGVehiclePawn, BodyPaintSlot);
	DOREPLIFETIME(APGVehiclePawn, VehicleMeshOverride);
}

void APGVehiclePawn::SetBodyPaint(UMaterialInterface* Material, FName Slot)
{
	BodyPaintMaterial = Material;
	BodyPaintSlot = Slot;
	OnRep_BodyPaint(); // 서버(와 혼자 하는 판) 화면에도 바로 칠한다
}

void APGVehiclePawn::OnRep_BodyPaint()
{
	if (!IsValid(BodyPaintMaterial) || BodyPaintSlot.IsNone() || !GetMesh())
		return;
	GetMesh()->SetMaterialByName(BodyPaintSlot, BodyPaintMaterial);
	// 칠을 받은 차 = 변신해서 나온 차. 변신 순간 코앞에 생겨 3인칭 카메라가 걸려 확 당겨지지 않게 카메라를 통과시킨다
	//   (APGTransformNPCActor::FinishTransform 이 서버에서 하던 것 — 충돌 응답은 복제되지 않아 클라에서도 여기서 한다).
	for (UActorComponent* Component : GetComponents())
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			Primitive->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
}

void APGVehiclePawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	// 멀티(9/27): 탑승한 그 F 키가 아직 눌려 있다 — 이 컴퓨터가 조종을 넘겨받은 순간 "눌린 상태·탑승 시각" 을 새로 잡는다.
	// 전에는 Mount(서버)에서만 잡아서, 클라이언트는 탑승 F 를 곧바로 하차 F 로 읽고 ServerDismount 를 보냈다
	//   (9/27 PIE 로그: mounted → dismounted 가 같은 순간에 두 번 — "타도 운전이 안 된다").
	if (IsLocallyControlled())
	{
		bDismountKeyWasDown = true;
		MountedTime = GetWorld()->GetTimeSeconds();
	}
}

void APGVehiclePawn::OnRep_RiderPawn(APawn* OldRider)
{
	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
		Movement->SetParked(!IsRidden());
	PGRide::OnRiderChangedOnClient(RiderPawn, OldRider);
}

void APGVehiclePawn::OnRep_VehicleMeshOverride()
{
	SetVehicleMesh(VehicleMeshOverride);
}

void APGVehiclePawn::ServerUnflip_Implementation()
{
	// 뒤집혔을 때만(아무 때나 R 로 차를 들어 올리지 못하게).
	if (GetActorUpVector().Z < 0.2f)
		Unflip();
}

void APGVehiclePawn::SetVehicleMesh(USkeletalMesh* NewMesh)
{
	if (!IsValid(NewMesh))
		return;
	if (HasAuthority())
		VehicleMeshOverride = NewMesh;
	GetMesh()->SetSkeletalMeshAsset(NewMesh);
	// 메시가 바뀌면 바퀴·차체 물리도 다시 만들어야 한다.
	GetMesh()->RecreatePhysicsState();
	if (UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent())
		Movement->RecreatePhysicsState();
}

// ---- 탑승 ----

bool APGVehiclePawn::CanInteract_Implementation(APawn* Interactor) const
{
	return !IsRidden() && IsValid(Interactor) && Interactor != this;
}

FText APGVehiclePawn::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("Vehicle", "Mount", "차량 탑승");
}

FText APGVehiclePawn::GetRiderPrompt() const
{
	// 달리는 동안은 화면을 비운다. 약 5km/h 아래로 거의 멈추면 "차량 하차"(F 는 달리는 중에도 되지만, 안내는 멈췄을 때만).
	// 날고 있으면 비운다(9/28 사용자 PIE: "공중에 떠 있는데 왜 차량 하차가 보여?") — 날 때는 바퀴 움직임을 꺼 두어
	//   아래 바퀴 속도가 늘 0 이라 "멈췄다" 로 읽혔다.
	if (const UPGFlightKitComponent* Kit = FindComponentByClass<UPGFlightKitComponent>(); Kit && Kit->IsFlying())
		return FText::GetEmpty();
	const UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	const float Speed = Movement ? FMath::Abs(Movement->GetForwardSpeed()) : 0.0f;
	// 150(약 5km/h)이었을 때는 감속하는 순간마다 문구가 떴다. 완전히 멈추고 0.6초 뒤에만 띄운다.
	return DismountPromptTimer.IsSettled(Speed, GetWorld()->GetTimeSeconds())
		? NSLOCTEXT("Vehicle", "Dismount", "차량 하차") : FText::GetEmpty();
}

void APGVehiclePawn::Interact_Implementation(APawn* Interactor)
{
	Mount(Interactor);
}

bool APGVehiclePawn::Mount(APawn* Rider)
{
	if (!HasAuthority() || !IsValid(Rider) || !CanInteract_Implementation(Rider) || !IsValid(Seat))
		return false;
	APlayerController* PC = Cast<APlayerController>(Rider->GetController());
	if (!IsValid(PC) || !Seat->TryEnter(Rider))
		return false;

	RiderPawn = Rider;
	// 뒤집힌 채(스폰 때 벽에 튕기거나 굴러서) 타면 카메라가 기울어 보인다. 빙의 전에 세운다.
	// (탑승자를 붙이기 전에 세운다 — 붙이기는 차체에 딱 맞춰 붙이므로 결과는 예전 순서와 같다.)
	if (GetActorUpVector().Z < 0.2f)
		Unflip();
	// 숨기기·충돌 끄기·붙이기·빙의 — 차·탱크·로봇이 같이 쓰는 순서(Interaction/PGRideHelpers.h).
	PGRide::BoardRider(this, Rider, PC, GetMesh(), FVector(0.0f, 0.0f, 80.0f));
	// 빙의는 차체 회전을 컨트롤 회전으로 가져온다. 차가 기울어 있으면 시야가 옆으로 눕는다. 요만 남기고 살짝 내려다본다.
	PGRide::SetViewRotation(PC, FRotator(-10.0f, GetActorRotation().Yaw, 0.0f)); // 원격이면 그 사람 화면도(멀티 9/27)
	GetVehicleMovementComponent()->SetParked(false);
	ApplyDriveInput(0.0f, 0.0f, false);
	// 탑승한 그 F 키가 아직 눌려 있다. 그대로 두면 다음 Tick 이 "F 눌림"으로 보고 바로 내려 버린다(두 번 눌러야 탔던 원인).
	bDismountKeyWasDown = true;
	MountedTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogPGObjects, Display, TEXT("%s mounted by %s"), *GetName(), *GetNameSafe(Rider));
	return true;
}

void APGVehiclePawn::Unflip()
{
	if (!HasAuthority())
		return;
	const FRotator Upright(0.0f, GetActorRotation().Yaw, 0.0f);
	SetActorLocationAndRotation(GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), Upright, false, nullptr, ETeleportType::TeleportPhysics);
	NotifyServerTeleport(); // 멀티(9/28): 모는 사람 화면(자기 차 기준)에도 세운 자리를
	GetMesh()->SetPhysicsLinearVelocity(FVector::ZeroVector);
	GetMesh()->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	FlippedSeconds = 0.0f;
}

void APGVehiclePawn::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
	FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// 멀티(9/28): 모는 사람 화면도 먼저 부순다(PGPhysicsUtil::TryKnockProp 주석) — 서버를 기다리면 소품에 막혀 멈칫했다.
	if (!HasAuthority() && !IsLocallyControlled())
		return;
	// 달리다 부딪힌 작은 소품은 튕겨 낸다. 속도가 느리면(주차 중 접촉) 그냥 둔다.
	const FVector Velocity = GetVelocity();
	if (Velocity.Size() < 200.0f)
		return;
	// 5.5m 까지: 드럼통·파란 탱크·콘크리트 벽 판자까지 날린다. 12m 컨테이너와 건물만 남는다.
	const bool bKnocked = PGPhysicsUtil::TryKnockProp(OtherComp, Hit, Velocity.GetSafeNormal() * (Velocity.Size() * 1.3f + 500.0f) + FVector(0.0f, 0.0f, 450.0f), this, 550.0f);
	UE_LOG(LogPGObjects, Display, TEXT("%s hit %s (%s) speed=%.0f knocked=%s"), *GetName(), *GetNameSafe(Other), *GetNameSafe(OtherComp),
		Velocity.Size(), bKnocked ? TEXT("yes") : TEXT("no"));
}

bool APGVehiclePawn::Dismount()
{
	if (!HasAuthority() || !IsRidden() || !IsValid(Seat))
		return false;
	APlayerController* PC = Cast<APlayerController>(GetController());
	APawn* Rider = RiderPawn;
	FVector ExitLocation;
	if (!Seat->Exit(Rider, ExitLocation))
		return false;

	// 내리면 차는 서 있어야 한다. 가속을 끊고 핸드브레이크를 건다.
	// 핸드브레이크 입력은 빙의가 풀리면 지워지므로 주차로 다시 잠근다(내린 차를 사람이 밀어도 안 굴러가게).
	ApplyDriveInput(0.0f, 0.0f, true);
	GetVehicleMovementComponent()->SetParked(true);

	// 좌석 옆이 벽에 막혀 있으면(스폰된 차가 벽에 끼는 경우) 차 둘레 3.5m, 1m 높이 8방향에서 빈 자리를 찾는다.
	const FVector SafeExit = PGRide::FindExitSpot(this, Rider, ExitLocation, 350.0f, 100.0f);
	// 떼기·보이기·충돌 켜기·똑바로 세우기·시야 세우기·빙의 되돌리기(Interaction/PGRideHelpers.h).
	PGRide::ReleaseRider(this, Rider, PC, SafeExit, /*bResetControlRotation*/ true);
	RiderPawn = nullptr;
	UE_LOG(LogPGObjects, Display, TEXT("%s dismounted by %s"), *GetName(), *GetNameSafe(Rider));
	return true;
}

void APGVehiclePawn::ServerDismount_Implementation()
{
	Dismount();
}

// ---- 운전 ----

void APGVehiclePawn::ApplyDriveInput(float Throttle, float Steering, bool bHandbrake)
{
	UChaosVehicleMovementComponent* Movement = GetVehicleMovementComponent();
	if (!IsValid(Movement))
		return;
	LastThrottle = Throttle;
	Movement->SetThrottleInput(FMath::Max(Throttle, 0.0f));
	// bReverseAsBrake: 정지 상태에서 S 를 누르면 후진, 달리는 중이면 브레이크. Chaos 가 알아서 나눈다.
	Movement->SetBrakeInput(FMath::Max(-Throttle, 0.0f));
	Movement->SetSteeringInput(Steering);
	Movement->SetHandbrakeInput(bHandbrake);
}

void APGVehiclePawn::ServerSetDriveInput_Implementation(float Throttle, float Steering, bool bHandbrake)
{
	ApplyDriveInput(Throttle, Steering, bHandbrake);
}

void APGVehiclePawn::ServerSetCarPose_Implementation(FVector_NetQuantize10 Location, FRotator Rotation, FVector_NetQuantize10 LinearVelocity, FVector_NetQuantize10 AngularVelocityDeg, uint8 Epoch)
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!IsValid(Body) || !Body->IsSimulatingPhysics() || !IsRidden())
		return; // 날고 있다(물리 꺼짐)·내렸다
	if (Epoch != PoseEpoch)
		return; // 서버가 옮기기 전의 자리다(NotifyServerTeleport)
	if (FVector::DistSquared(Body->GetComponentLocation(), Location) > FMath::Square(2000.0f))
	{
		if (++RejectedPoses <= 5)
			UE_LOG(LogPGObjects, Warning, TEXT("%s: driver pose %.0fm away from the server car — ignored, sending the server spot back"), *GetName(),
				FVector::Dist(Body->GetComponentLocation(), Location) * 0.01f);
		NotifyServerTeleport(); // 서버 자리로 다시 맞춘다
		return;
	}
	// 바닥을 뚫고 내려간 자리는 받지 않는다(9/28 사용자 PIE: 날아다니는 차가 떨어지는 중에 알트탭 → 바닥 밑으로 빠짐).
	//   창이 뒤로 가면 에디터가 그 창의 프레임을 크게 떨어뜨려, 한 번에 긴 시간을 계산하는 사이 차가 얇은 바닥을 지나쳤다.
	//   모는 사람 화면이 기준이라 서버가 그 자리를 그대로 믿었다. 서버 차 자리에서 새 자리까지 사이에 땅이 끼어 있고 새 자리가 더 낮으면
	//   뚫고 내려간 것이다 — 서버 자리로 되돌린다.
	if (Location.Z < Body->GetComponentLocation().Z - 150.0f)
	{
		FHitResult Floor;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGCarPoseFloor), false, this);
		if (GetWorld()->LineTraceSingleByObjectType(Floor, Body->GetComponentLocation() + FVector(0.0f, 0.0f, 50.0f), Location,
			FCollisionObjectQueryParams(ECC_WorldStatic), Params))
		{
			if (++RejectedPoses <= 5)
				UE_LOG(LogPGObjects, Warning, TEXT("%s: driver pose went through %s (%.0f cm below the server car) — sending the server spot back"),
					*GetName(), *GetNameSafe(Floor.GetComponent()), Body->GetComponentLocation().Z - Location.Z);
			NotifyServerTeleport();
			return;
		}
	}
	Body->SetWorldLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(LinearVelocity);
	Body->SetPhysicsAngularVelocityInDegrees(AngularVelocityDeg);
}

bool APGVehiclePawn::ProbeBelowFloorPose()
{
	USkeletalMeshComponent* Body = GetMesh();
	if (!IsValid(Body))
		return false;
	const FVector Before = Body->GetComponentLocation();
	ServerSetCarPose_Implementation(Before - FVector(0.0f, 0.0f, 400.0f), Body->GetComponentRotation(), FVector::ZeroVector, FVector::ZeroVector, PoseEpoch);
	return Body->GetComponentLocation().Z < Before.Z - 300.0f;
}

void APGVehiclePawn::NotifyServerTeleport()
{
	if (!HasAuthority() || IsLocallyControlled() || !IsRidden())
		return;
	USkeletalMeshComponent* Body = GetMesh();
	if (!IsValid(Body))
		return;
	++PoseEpoch;
	ClientResetCarPose(Body->GetComponentLocation(), Body->GetComponentRotation(),
		Body->IsSimulatingPhysics() ? Body->GetPhysicsLinearVelocity() : FVector::ZeroVector, PoseEpoch);
}

void APGVehiclePawn::ClientResetCarPose_Implementation(FVector_NetQuantize10 Location, FRotator Rotation, FVector_NetQuantize10 LinearVelocity, uint8 Epoch)
{
	PoseEpoch = Epoch;
	if (USkeletalMeshComponent* Body = GetMesh())
	{
		Body->SetWorldLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		if (Body->IsSimulatingPhysics())
		{
			Body->SetPhysicsLinearVelocity(LinearVelocity);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
	}
	UE_LOG(LogPGObjects, Display, TEXT("%s: server moved the car to %s on this screen"), *GetName(), *FVector(Location).ToCompactString());
}

void APGVehiclePawn::PostNetReceivePhysicState()
{
	// 모는 사람 화면은 자기 차가 기준이다(ServerSetCarPose 주석). 서버 자리로 끌어오지 않는다. 비교 스위치 PG.NetSmooth 0 이면 예전처럼.
	if (!HasAuthority() && IsLocallyControlled() && FPGNetPoseSmoother::IsEnabled())
		return;
	Super::PostNetReceivePhysicState();
}

void APGVehiclePawn::RamMonster(AActor* Monster, const FVector& Direction, float Speed)
{
	// 서 있는 차에 몬스터가 와서 비비는 건 들이받기가 아니다. 시속 11km(300cm/s)부터.
	if (!PGCreature::IsAliveCreature(Monster) || Speed < 300.0f)
		return;
	// 스윕은 매 프레임 돈다. 같은 몬스터를 0.5초 안에 다시 치지 않는다(한 번 박았는데 피해가 수십 번 들어가는 걸 막음).
	const float Now = GetWorld()->GetTimeSeconds();
	float& LastTime = LastRamTime.FindOrAdd(TWeakObjectPtr<AActor>(Monster), -1000.0f);
	if (Now - LastTime < 0.5f)
		return;
	LastTime = Now;

	// 피해는 속도 비례: 시속 72km(2000cm/s) = 20. 작은 몬스터(A 세력 체력 70)는 서너 번이면 죽는다.
	const float Damage = Speed * 0.01f;
	// 몸 반지름 1.2m 이하만 날린다. 5배 크리처·8배 보스는 차보다 크고 무거우니 피해만 절반 들어간다.
	const bool bSmall = PGCreature::GetBodyRadius(Monster) <= 120.0f;
	if (bSmall)
		PGCreature::Launch(Monster, Direction * Speed * 0.9f + FVector(0.0f, 0.0f, FMath::Clamp(Speed * 0.35f, 250.0f, 700.0f)));
	UGameplayStatics::ApplyDamage(Monster, bSmall ? Damage : Damage * 0.5f, GetController(), this, UDamageType::StaticClass());
	UE_LOG(LogPGObjects, Display, TEXT("%s rammed %s speed=%.0f damage=%.0f launched=%s"), *GetName(), *Monster->GetName(), Speed, bSmall ? Damage : Damage * 0.5f, bSmall ? TEXT("yes") : TEXT("no"));

	// 죽었거나 사라진 항목 정리
	for (auto It = LastRamTime.CreateIterator(); It; ++It)
		if (!It.Key().IsValid())
			It.RemoveCurrent();
}

void APGVehiclePawn::KnockAhead()
{
	const FVector Velocity = GetVelocity();
	const float Speed = Velocity.Size();
	// 천천히 밀어붙일 때도 반응해야 한다. 거의 멈춰 있어도 가속 페달을 밟고 있으면 그 방향을 본다.
	if (Speed < 30.0f && FMath::IsNearlyZero(LastThrottle))
		return;
	const FVector Direction = Speed > 100.0f ? Velocity.GetSafeNormal() : GetActorForwardVector() * (LastThrottle < 0.0f ? -1.0f : 1.0f);
	const FVector Start = GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
	// 차체 반길이 + 한 프레임 이동량 정도만. 멀리서 미리 반응하면 닿기도 전에 날아가는 것처럼 보인다.
	// 범퍼 폭만큼의 납작한 상자를 차 방향으로 스윕한다. 구 하나로는 범퍼 모서리에 걸리는 소품을 놓쳤다.
	// 상자 앞면이 범퍼 + 10cm 에 오도록 끝점을 상자 두께만큼 당긴다.
	const FVector LocalExtent = GetMesh()->CalcLocalBounds().BoxExtent;
	const float HalfLength = LocalExtent.X;
	const float SweepRadius = 20.0f; // 상자 두께(진행 방향 반폭)
	// 폭은 차체 전체 + 10cm, 높이는 지붕까지. 0.9 폭·낮은 상자였을 때는 범퍼 모서리·사이드미러 높이의 소품이 스윕을 빠져나가
	// 원본(안 움직이는 정적 메시)에 차가 그대로 박았고, 그게 차가 공처럼 튀던 주원인이다.
	const FCollisionShape SweepShape = FCollisionShape::MakeBox(FVector(SweepRadius, LocalExtent.Y + 10.0f, 70.0f));
	const float Reach = HalfLength + 10.0f + Speed * 0.03f - SweepRadius;
	const FVector End = Start + Direction * FMath::Max(Reach, 10.0f);

	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn); // 몬스터 들이받기
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGVehicleKnock), false, this);
	if (IsValid(RiderPawn))
		Params.AddIgnoredActor(RiderPawn);
	GetWorld()->SweepMultiByObjectType(Hits, Start, End, GetActorQuat(), ObjectTypes, SweepShape, Params);

	// 속도에 비례해 튕긴다(속도 변화 cm/s). 천천히 밀면 살짝 밀려 나가고 그 뒤로는 차가 물리로 밀고 간다.
	// (×1.5·7m·잔해 250kg 상한으로 더 잘 날아가게 해 봤는데 9/17 PIE 에서 "그전 반응이 낫다"고 해서 되돌렸다. 운전이 힘들던 진짜 원인은 뒤집힘이었다 → 생성자의 InertiaTensorScale.)
	const FVector Impulse = Direction * (Speed * 1.2f + 80.0f) + FVector(0.0f, 0.0f, FMath::Min(Speed * 0.5f, 450.0f));
	for (const FHitResult& Hit : Hits)
	{
		if (AActor* Monster = Hit.GetActor(); PGCreature::IsCreature(Monster))
		{
			if (HasAuthority())
				RamMonster(Monster, Direction, Speed);
			continue;
		}
		// 5.5m 까지: 드럼통·파란 탱크·콘크리트 벽 판자까지 날린다. 12m 컨테이너와 건물만 남는다.
		if (PGPhysicsUtil::TryKnockProp(Hit.GetComponent(), Hit, Impulse, this, 550.0f))
			UE_LOG(LogPGObjects, Verbose, TEXT("%s knocked %s (%s) speed=%.0f reach=%.0f(half=%.0f) dist=%.0f"), *GetName(), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
				Speed, Reach + SweepRadius, HalfLength, FVector::Dist(Start, Hit.ImpactPoint));
	}
}

void APGVehiclePawn::PostNetReceiveLocationAndRotation()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
		if (UPGFlightKitComponent* Kit = FindComponentByClass<UPGFlightKitComponent>())
		{
			const FRepMovement& Rep = GetReplicatedMovement();
			if (Kit->ReceiveProxyPose(FRepMovement::RebaseOntoLocalOrigin(Rep.Location, this), Rep.Rotation))
				return;
		}
	Super::PostNetReceiveLocationAndRotation();
}

void APGVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() || IsLocallyControlled()) // 멀티(9/28): 모는 사람 화면도 앞을 훑어 먼저 부순다(몬스터 들이받기는 서버만, RamMonster)
		KnockAhead();

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) || !IsLocallyControlled())
		return;
	PGCameraUtil::KeepCameraAboveGround(this, CameraArm, 700.0f);

	const FVector2D Axes = PGKeyPolling::ReadWasd(PC);
	const float Throttle = Axes.X;
	const float Steering = Axes.Y;
	const bool bHandbrake = PC->IsInputKeyDown(EKeys::SpaceBar);

	// 입력은 이 컴퓨터의 차에 먼저 넣는다. 클라이언트도 마찬가지다(9/27 멀티) — Chaos 차는 조종하는 클라이언트가
	//   자기 입력을 매 프레임 서버로 보내(ServerUpdateState) 서버 입력을 덮어쓴다. 전에는 클라 차에 입력을 안 넣어서
	//   0 이 올라가 우리 RPC 값을 지웠고, 클라에서는 차가 안 움직였다. RPC 는 값이 바뀔 때 한 번 더 확실히 보내는 용도로 남긴다.
	ApplyDriveInput(Throttle, Steering, bHandbrake);
	if (!HasAuthority() && (Throttle != LastSentThrottle || Steering != LastSentSteering || bHandbrake != bLastSentHandbrake))
		ServerSetDriveInput(Throttle, Steering, bHandbrake);
	// 멀티(9/28): 땅에서 달리는 동안 내 차 자리를 서버에 보낸다(1초에 30번). 날 때는 비행 부품이 맡는다(물리 꺼짐).
	if (!HasAuthority() && FPGNetPoseSmoother::IsEnabled() && GetMesh() && GetMesh()->IsSimulatingPhysics()
		&& GetWorld()->GetTimeSeconds() - LastPoseSentAt >= 1.0 / 30.0)
	{
		LastPoseSentAt = GetWorld()->GetTimeSeconds();
		USkeletalMeshComponent* Body = GetMesh();
		ServerSetCarPose(Body->GetComponentLocation(), Body->GetComponentRotation(), Body->GetPhysicsLinearVelocity(), Body->GetPhysicsAngularVelocityInDegrees(), PoseEpoch);
	}
	LastSentThrottle = Throttle;
	LastSentSteering = Steering;
	bLastSentHandbrake = bHandbrake;

	PGKeyPolling::ApplyMouseLook(PC, this);

	// F: 하차. 탑승한 F 가 그대로 하차로 읽히지 않게 0.5초 뒤부터.
	if (PGKeyPolling::WasPressed(PC, EKeys::F, bDismountKeyWasDown) && GetWorld()->GetTimeSeconds() - MountedTime > 0.5f)
	{
		if (HasAuthority())
			Dismount();
		else
			ServerDismount();
	}

	// 뒤집힘 복구: R 키, 또는 거꾸로 선 채 2.5초 넘게 거의 멈춰 있으면 자동.
	const bool bUpsideDown = GetActorUpVector().Z < 0.2f;
	FlippedSeconds = (bUpsideDown && GetVelocity().Size() < 120.0f) ? FlippedSeconds + DeltaSeconds : 0.0f;
	if ((PGKeyPolling::WasPressed(PC, EKeys::R, bUnflipKeyWasDown) && bUpsideDown) || FlippedSeconds > 2.5f)
	{
		if (HasAuthority())
			Unflip();
		else
			ServerUnflip();
		FlippedSeconds = 0.0f; // 클라는 서버가 세울 때까지 매 프레임 보내지 않게
	}
}
