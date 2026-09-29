#include "Vehicle/PGTankPawn.h"
#include "Interaction/PGRideHelpers.h"
#include "Common/PGCameraUtil.h"

#include "Camera/CameraComponent.h"
#include "Common/PGKeyPolling.h"
#include "Common/PGPhysicsUtil.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Combat/PGCreatureAccess.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerState.h"
#include "Objects/PGObjectTypes.h"
#include "Flow/PGRunSubsystem.h"
#include "Objects/PGVehicleComponents.h"
#include "Perception/AISense_Hearing.h"

namespace
{
	// 이펙트가 붙기 전까지 탄도·폭발 범위를 선으로 그린다. 끄려면 PG.Tank.DebugShots 0.
	TAutoConsoleVariable<int32> CVarTankDebugShots(TEXT("PG.Tank.DebugShots"), 1, TEXT("Draw tank shell traces and blast radius (until real FX are hooked up)."));
}

APGTankPawn::APGTankPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetNetCullDistanceSquared(FMath::Square(90000.0f)); // 멀티(9/27): 큰 탈것이 150m 밖에서 사라졌다 나타나지 않게
	SetReplicatingMovement(true);
	AutoPossessAI = EAutoPossessAI::Disabled;

	// 몸 = 상자 하나. 움직임은 이 상자를 스윕해서 막히는지 본다(메시는 보이기만).
	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetBoxExtent(FallbackHalfExtent * TankScale);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_Vehicle);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	// 날아간 잔해(PhysicsBody)는 무시하고 깔고 지나간다. 막히면 잔해 하나에 탱크가 멈춰 섰다.
	Body->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->CanCharacterStepUpOn = ECB_No;
	// 움직이는 큰 상자가 내비메시를 매 프레임 다시 굽지 않게.
	Body->SetCanEverAffectNavigation(false);

	auto MakeMesh = [this](const TCHAR* Name, USceneComponent* Parent)
	{
		UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Mesh->SetupAttachment(Parent);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetCanEverAffectNavigation(false);
		return Mesh;
	};
	HullMesh = MakeMesh(TEXT("Hull"), Body);
	TurretMesh = MakeMesh(TEXT("Turret"), HullMesh);
	GunMesh = MakeMesh(TEXT("Gun"), TurretMesh);

	Seat = CreateDefaultSubobject<UPGSeatComponent>(TEXT("DriverSeat"));
	Seat->SetupAttachment(Body);

	// 카메라: 차와 같지만 더 멀리·높이. 포탑 너머로 조준점을 봐야 한다.
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Body);
	CameraArm->SetUsingAbsoluteRotation(true);
	CameraArm->TargetArmLength = 1300.0f;
	CameraArm->SocketOffset = FVector(0.0f, 0.0f, 380.0f);
	CameraArm->bUsePawnControlRotation = true;
	CameraArm->bDoCollisionTest = false;
	CameraArm->bEnableCameraLag = true;
	CameraArm->CameraLagSpeed = 8.0f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	HullAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Vehicles/Tank/SM_PGV_TankHull.SM_PGV_TankHull")));
	TurretAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Vehicles/Tank/SM_PGV_TankTurret.SM_PGV_TankTurret")));
	GunAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Vehicles/Tank/SM_PGV_TankGun.SM_PGV_TankGun")));
}

void APGTankPawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGTankPawn, RiderPawn);
	DOREPLIFETIME(APGTankPawn, Health);
	DOREPLIFETIME(APGTankPawn, bWrecked);
	DOREPLIFETIME(APGTankPawn, TurretYaw);
	DOREPLIFETIME(APGTankPawn, GunPitch);
	DOREPLIFETIME(APGTankPawn, CurrentSpeed);
	DOREPLIFETIME_CONDITION(APGTankPawn, DriverPoseLocation, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION(APGTankPawn, DriverPoseRotation, COND_AutonomousOnly);
}

void APGTankPawn::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyAssets();
}

void APGTankPawn::BeginPlay()
{
	Super::BeginPlay();
	ApplyAssets();
	Health = MaxHealth;
}

void APGTankPawn::ApplyAssets()
{
	auto Load = [](UStaticMeshComponent* Component, const TSoftObjectPtr<UStaticMesh>& Asset)
	{
		UStaticMesh* Mesh = Asset.IsNull() ? nullptr : Asset.LoadSynchronous();
		Component->SetStaticMesh(Mesh);
		return Mesh;
	};
	UStaticMesh* Hull = Load(HullMesh, HullAsset);
	Load(TurretMesh, TurretAsset);
	Load(GunMesh, GunAsset);

	// 몸 크기 = 차체 메시 경계 × 배율. 바닥 여유(몸 상자 밑면 ~ 땅)는 작은 턱·돌부리를 넘으려고 둔다.
	FVector Full = FallbackHalfExtent * 2.0f;
	if (Hull)
		Full = Hull->GetBoundingBox().GetSize();
	Full *= TankScale;
	GroundClearance = FMath::Min(45.0f * TankScale, Full.Z * 0.2f);
	HalfExtent = FVector(Full.X * 0.5f, Full.Y * 0.5f, (Full.Z - GroundClearance) * 0.5f);
	Body->SetBoxExtent(HalfExtent);

	// 차체 피벗은 바닥 중앙 → 몸 상자 중심에서 (바닥 여유 + 상자 반 높이)만큼 내려간 곳.
	HullMesh->SetRelativeScale3D(FVector(TankScale));
	HullMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -(GroundClearance + HalfExtent.Z)));
	// 자식의 상대 위치는 부모(차체) 공간 = 배율 1 의 cm 로 적으면 된다.
	TurretMesh->SetRelativeLocation(TurretOffset);
	GunMesh->SetRelativeLocation(GunOffset);
	OnRep_Aim();

	Seat->SetRelativeLocation(FVector(0.0f, -HalfExtent.Y - 150.0f, -HalfExtent.Z));
	CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, HalfExtent.Z));
}

// ---- 탑승 ----

bool APGTankPawn::CanInteract_Implementation(APawn* Interactor) const
{
	return !bWrecked && !IsRidden() && IsValid(Interactor) && Interactor != this;
}

FText APGTankPawn::GetInteractionPrompt_Implementation() const
{
	return bWrecked ? NSLOCTEXT("Tank", "Wrecked", "부서진 탱크") : NSLOCTEXT("Tank", "Mount", "탱크 탑승");
}

FText APGTankPawn::GetRiderPrompt() const
{
	// 완전히 멈추고 0.6초 뒤에만 띄운다(감속 중 깜빡이지 않게).
	return DismountPromptTimer.IsSettled(FMath::Abs(CurrentSpeed), GetWorld()->GetTimeSeconds())
		? NSLOCTEXT("Tank", "Dismount", "탱크 하차") : FText::GetEmpty();
}

void APGTankPawn::Interact_Implementation(APawn* Interactor)
{
	Mount(Interactor);
}

bool APGTankPawn::Mount(APawn* Rider)
{
	if (!HasAuthority() || !IsValid(Rider) || !CanInteract_Implementation(Rider) || !IsValid(Seat))
		return false;
	APlayerController* PC = Cast<APlayerController>(Rider->GetController());
	if (!IsValid(PC) || !Seat->TryEnter(Rider))
		return false;

	RiderPawn = Rider;
	// 숨기기·충돌 끄기·붙이기·빙의 — 차·탱크·로봇이 같이 쓰는 순서(Interaction/PGRideHelpers.h). 탱크는 몸통 한가운데에 붙인다.
	PGRide::BoardRider(this, Rider, PC, Body, FVector::ZeroVector);
	PGRide::SetViewRotation(PC, FRotator(-8.0f, GetActorRotation().Yaw, 0.0f)); // 원격이면 그 사람 화면도(멀티 9/27)
	// 탑승한 그 F 가 바로 하차로 읽히지 않게(차와 같은 이유).
	bDismountKeyWasDown = true;
	MountedTime = GetWorld()->GetTimeSeconds();
	UE_LOG(LogPGObjects, Display, TEXT("%s mounted by %s"), *GetName(), *GetNameSafe(Rider));
	return true;
}

bool APGTankPawn::Dismount()
{
	if (!HasAuthority() || !IsRidden() || !IsValid(Seat))
		return false;
	APlayerController* PC = Cast<APlayerController>(GetController());
	APawn* Rider = RiderPawn;
	FVector ExitLocation;
	if (!Seat->Exit(Rider, ExitLocation))
		return false;

	Throttle = 0.0f;
	Turn = 0.0f;
	CurrentSpeed = 0.0f;
	// 옆이 막혀 있으면 탱크 둘레(차체 반폭 + 1.5m) 8방향에서 빈 자리를 찾는다. 탱크는 몸통 높이에서 찾는다(띄우지 않음).
	const FVector SafeExit = PGRide::FindExitSpot(this, Rider, ExitLocation, FMath::Max(HalfExtent.X, HalfExtent.Y) + 150.0f, 0.0f);
	PGRide::ReleaseRider(this, Rider, PC, SafeExit, /*bResetControlRotation*/ true);
	RiderPawn = nullptr;
	UE_LOG(LogPGObjects, Display, TEXT("%s dismounted by %s"), *GetName(), *GetNameSafe(Rider));
	return true;
}

void APGTankPawn::ServerDismount_Implementation()
{
	Dismount();
}

// ---- 입력 ----

void APGTankPawn::ServerSetTankInput_Implementation(float InThrottle, float InTurn, FVector_NetQuantize InAimPoint)
{
	Throttle = FMath::Clamp(InThrottle, -1.0f, 1.0f);
	Turn = FMath::Clamp(InTurn, -1.0f, 1.0f);
	AimPoint = InAimPoint;
	bHasAim = true;
}

void APGTankPawn::ServerFire_Implementation()
{
	FireMainGun();
}

void APGTankPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 멀티(9/28): 클라이언트 화면의 탱크.
	//   구경하는 사람: 서버 위치를 부드럽게 따라간다(ProxySmoother).
	//   모는 사람: 따라가기만 하면 "입력 → 서버 → 내 화면" 만큼 늦게 반응한다(날아다니는 차에서 확인: 156ms). 그래서 같은 주행 계산
	//     (TickDrive)을 이 화면에서 내 입력으로 바로 돌리고(아래 입력 읽기 뒤), 서버와의 차이는 여러 프레임에 나눠 흘려 넣는다(OnRep_DriverPose).
	//   비교 스위치 PG.NetSmooth: 0 = 바로 옮김, 1 = 미리 계산(기본), 2 = 따라가기.
	if (GetLocalRole() == ROLE_AutonomousProxy && FPGNetPoseSmoother::Mode() == 2)
		DriverSmoother.Step(this, GetWorld()->GetTimeSeconds(), DeltaSeconds);
	else if (GetLocalRole() == ROLE_SimulatedProxy)
		ProxySmoother.Step(this, GetWorld()->GetTimeSeconds(), DeltaSeconds);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (IsValid(PC) && IsLocallyControlled())
	{
		PGKeyPolling::ApplyMouseLook(PC, this);
		PGCameraUtil::KeepCameraAboveGround(this, CameraArm, 1300.0f);
		const FVector2D Axes = PGKeyPolling::ReadWasd(PC);
		// 조준점: 화면 가운데(카메라)가 보는 곳. 포탑·포신이 그 점을 향해 돈다 — 조준점과 탄착이 맞는다.
		FVector ViewLocation;
		FRotator ViewRotation;
		PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector ViewEnd = ViewLocation + ViewRotation.Vector() * Range;
		FHitResult ViewHit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankAim), false, this);
		if (IsValid(RiderPawn))
			Params.AddIgnoredActor(RiderPawn);
		const FVector Aim = GetWorld()->LineTraceSingleByChannel(ViewHit, ViewLocation, ViewEnd, ECC_Visibility, Params) ? ViewHit.ImpactPoint : ViewEnd;
		if (HasAuthority())
			ServerSetTankInput_Implementation(Axes.X, Axes.Y, Aim);
		else
		{
			ServerSetTankInput(Axes.X, Axes.Y, Aim);
			Throttle = FMath::Clamp(Axes.X, -1.0f, 1.0f); // 미리 계산용으로 이 화면에도(멀티 9/28)
			Turn = FMath::Clamp(Axes.Y, -1.0f, 1.0f);
		}

		// 좌클릭을 누르고 있으면 장전될 때마다 쏜다.
		if (PC->IsInputKeyDown(EKeys::LeftMouseButton) && GetWorld()->GetTimeSeconds() - LastFireTime >= FireInterval)
		{
			if (HasAuthority())
				FireMainGun();
			else
			{
				LastFireTime = GetWorld()->GetTimeSeconds(); // 클라가 RPC 를 매 프레임 보내지 않게
				ServerFire();
			}
		}

		if (PGKeyPolling::WasPressed(PC, EKeys::F, bDismountKeyWasDown) && GetWorld()->GetTimeSeconds() - MountedTime > 0.5f)
		{
			if (HasAuthority())
				Dismount();
			else
				ServerDismount();
		}
	}

	// 모는 사람 화면: 주행을 미리 계산하고, 서버와의 차이를 조금씩 흘려 넣는다(위 주석).
	if (!HasAuthority() && GetLocalRole() == ROLE_AutonomousProxy && FPGNetPoseSmoother::Mode() == 1)
	{
		TickDrive(DeltaSeconds);
		const FVector Step = PendingCorrection * (1.0f - FMath::Exp(-8.0f * DeltaSeconds));
		PendingCorrection -= Step;
		AddActorWorldOffset(Step, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (HasAuthority())
	{
		TickDrive(DeltaSeconds);
		TickAim(DeltaSeconds);
		// 운전자 화면용 위치(OnRep_DriverPose). 사람이 몰 때만 — 값이 바뀔 때만 실제로 보내진다.
		if (IsPlayerControlled())
		{
			DriverPoseLocation = GetActorLocation();
			DriverPoseRotation = GetActorRotation();
		}
	}
}

void APGTankPawn::OnRep_RiderPawn(APawn* OldRider)
{
	PGRide::OnRiderChangedOnClient(RiderPawn, OldRider);
}

void APGTankPawn::OnRep_DriverPose()
{
	// 운전하는 클라이언트: 서버가 정한 탱크 위치를 따라간다(카메라·조준도 이 위치에서 나간다).
	// 멀티(9/28): 바로 옮기면 위치가 올 때마다 뚝뚝 끊겼다 — 받아 두고 Tick 에서 부드럽게(DriverSmoother).
	if (GetLocalRole() != ROLE_AutonomousProxy)
		return;
	DriverSmoother.Receive(DriverPoseLocation, DriverPoseRotation, GetWorld()->GetTimeSeconds());
	if (FPGNetPoseSmoother::Mode() != 1)
	{
		if (!FPGNetPoseSmoother::IsEnabled()) // 0: 예전처럼 바로 옮김. 2: Tick 의 따라가기가 맡는다
			SetActorLocationAndRotation(DriverPoseLocation, DriverPoseRotation, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	// 미리 계산 중: 서버 위치는 망 한쪽 길이만큼 과거라 서버 속도로 그만큼 당겨 비교하고, 차이는 Tick 이 흘려 넣는다. 8m 넘으면 바로 옮긴다.
	float OneWaySeconds = 0.03f;
	if (const APlayerState* State = GetPlayerState())
		OneWaySeconds = FMath::Clamp(State->ExactPing * 0.0005f, 0.0f, 0.3f);
	const FVector ServerNow = FVector(DriverPoseLocation) + DriverSmoother.Velocity * OneWaySeconds;
	const FVector Error = ServerNow - GetActorLocation();
	if (Error.SizeSquared() > FMath::Square(800.0f))
	{
		SetActorLocationAndRotation(ServerNow, DriverPoseRotation, false, nullptr, ETeleportType::TeleportPhysics);
		PendingCorrection = FVector::ZeroVector;
		return;
	}
	PendingCorrection = Error;
	FRotator Now = GetActorRotation();
	Now.Yaw = FRotator::NormalizeAxis(Now.Yaw + FMath::FindDeltaAngleDegrees(Now.Yaw, DriverPoseRotation.Yaw) * 0.1f);
	SetActorRotation(Now);
}

void APGTankPawn::PostNetReceiveLocationAndRotation()
{
	// 구경하는 사람 화면: 엔진은 받은 위치로 바로 옮긴다(탱크는 물리가 아니라 보간이 없다). 받아 두고 Tick 에서 부드럽게.
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		const FRepMovement& Rep = GetReplicatedMovement();
		ProxySmoother.Receive(FRepMovement::RebaseOntoLocalOrigin(Rep.Location, this), Rep.Rotation, GetWorld()->GetTimeSeconds());
		return;
	}
	Super::PostNetReceiveLocationAndRotation();
}

// ---- 주행 ----

bool APGTankPawn::TraceGround(const FVector& Local2D, FVector& OutGround) const
{
	const FRotator Yaw(0.0f, GetActorRotation().Yaw, 0.0f);
	const FVector Point = GetActorLocation() + Yaw.RotateVector(FVector(Local2D.X, Local2D.Y, 0.0f));
	FCollisionObjectQueryParams Ground;
	Ground.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankGround), false, this);
	if (IsValid(RiderPawn))
		Params.AddIgnoredActor(RiderPawn);
	// 여러 개를 받아 "탈것을 막지 않는 것"은 건너뛴다. 왜: 길찾기용 보이지 않는 바닥판(NavigationFloor)은 탈것을 무시하게 해 뒀는데,
	// 물체 종류(WorldStatic)로만 찾으면 그 판도 잡혀서 움푹 파인 지형 위를 떠서 달렸다.
	TArray<FHitResult> Hits;
	GetWorld()->LineTraceMultiByObjectType(Hits, Point + FVector(0.0f, 0.0f, HalfExtent.Z + 250.0f), Point - FVector(0.0f, 0.0f, 1500.0f), Ground, Params);
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (IsValid(Component) && Component->GetCollisionResponseToChannel(ECC_Vehicle) != ECR_Block)
			continue;
		OutGround = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void APGTankPawn::Shove(const FVector& Velocity)
{
	if (!HasAuthority() || bWrecked)
		return;
	ShoveVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	VerticalSpeed = FMath::Max(VerticalSpeed, Velocity.Z);
	ShoveSpin = FMath::FRandRange(-60.0f, 60.0f);
	CurrentSpeed *= 0.3f; // 들이받히면 달리던 힘이 대부분 꺾인다
}

void APGTankPawn::TickDrive(float DeltaSeconds)
{
	if (bWrecked || !IsRidden())
	{
		Throttle = 0.0f;
		Turn = 0.0f;
	}

	// 속도: 반대로 밟거나 떼면 가속의 두 배로 줄인다(무거운 궤도의 제동 느낌).
	const float TargetSpeed = Throttle >= 0.0f ? Throttle * MaxForwardSpeed : Throttle * MaxReverseSpeed;
	const bool bBraking = FMath::Abs(TargetSpeed) < FMath::Abs(CurrentSpeed) || TargetSpeed * CurrentSpeed < 0.0f;
	CurrentSpeed = FMath::FInterpConstantTo(CurrentSpeed, TargetSpeed, DeltaSeconds, bBraking ? Acceleration * 2.0f : Acceleration);
	const float Yaw = GetActorRotation().Yaw + (Turn * TurnRate + ShoveSpin) * DeltaSeconds;

	// 땅: 차체 네 귀퉁이 아래를 재서 높이와 기울기(앞뒤·좌우)를 맞춘다.
	const float Fx = HalfExtent.X * 0.8f;
	const float Fy = HalfExtent.Y * 0.8f;
	FVector FL, FR, BL, BR;
	bool bFL = TraceGround(FVector(Fx, -Fy, 0.0f), FL);
	bool bFR = TraceGround(FVector(Fx, Fy, 0.0f), FR);
	bool bBL = TraceGround(FVector(-Fx, -Fy, 0.0f), BL);
	bool bBR = TraceGround(FVector(-Fx, Fy, 0.0f), BR);
	FRotator Rotation = GetActorRotation();
	FVector Location = GetActorLocation();
	int32 Contacts = bFL + bFR + bBL + bBR;
	// 한두 귀퉁이만 땅에 닿아 있으면(맵 가장자리·절벽 끝에 걸침) 떨어지지 않고 닿은 곳 높이로 선다.
	//   전에는 셋 미만이면 바로 자유 낙하라, 가장자리에서 앞쪽이 허공에 걸리는 순간 땅 밑으로 빠졌다(9/28 사용자 PIE).
	if (Contacts > 0 && Contacts < 3)
	{
		float SumZ = 0.0f;
		for (const TPair<bool, FVector*>& Corner : { TPair<bool, FVector*>(bFL, &FL), TPair<bool, FVector*>(bFR, &FR), TPair<bool, FVector*>(bBL, &BL), TPair<bool, FVector*>(bBR, &BR) })
			if (Corner.Key)
				SumZ += Corner.Value->Z;
		const float AvgZ = SumZ / Contacts;
		const FRotator Flat(0.0f, GetActorRotation().Yaw, 0.0f);
		const FVector Base = GetActorLocation();
		auto Fill = [&](bool bHit, FVector& Corner, float LocalX, float LocalY)
		{
			if (bHit)
				return;
			Corner = Base + Flat.RotateVector(FVector(LocalX, LocalY, 0.0f));
			Corner.Z = AvgZ;
		};
		Fill(bFL, FL, Fx, -Fy);
		Fill(bFR, FR, Fx, Fy);
		Fill(bBL, BL, -Fx, -Fy);
		Fill(bBR, BR, -Fx, Fy);
		bFL = bFR = bBL = bBR = true; // 아래 "빠진 귀퉁이 채우기" 가 다시 덮지 않게
		Contacts = 4;
	}
	if (Contacts >= 3)
	{
		// 빠진 귀퉁이는 대각선 반대쪽 두 점으로 채운다(바위 모서리 위에 한 귀퉁이만 떠 있을 때).
		if (!bFL) FL = FR + BL - BR;
		if (!bFR) FR = FL + BR - BL;
		if (!bBL) BL = FL + BR - FR;
		if (!bBR) BR = FR + BL - FL;
		const float FrontZ = (FL.Z + FR.Z) * 0.5f;
		const float BackZ = (BL.Z + BR.Z) * 0.5f;
		const float LeftZ = (FL.Z + BL.Z) * 0.5f;
		const float RightZ = (FR.Z + BR.Z) * 0.5f;
		const float TargetPitch = FMath::RadiansToDegrees(FMath::Atan2(FrontZ - BackZ, Fx * 2.0f));
		// 롤 +는 오른쪽이 내려가는 쪽이다. 오른쪽 땅이 높으면 음수.
		const float TargetRoll = -FMath::RadiansToDegrees(FMath::Atan2(RightZ - LeftZ, Fy * 2.0f));
		Rotation.Pitch = FMath::FInterpTo(Rotation.Pitch, TargetPitch, DeltaSeconds, 6.0f);
		Rotation.Roll = FMath::FInterpTo(Rotation.Roll, TargetRoll, DeltaSeconds, 6.0f);
		const float TargetZ = (FL.Z + FR.Z + BL.Z + BR.Z) * 0.25f + GroundClearance + HalfExtent.Z;
		// 밀쳐져 위로 뜬 순간(VerticalSpeed > 0)도 공중으로 친다. 안 그러면 아래 "오르막 따라가기"가 바로 땅에 붙였다.
		if (Location.Z > TargetZ + 20.0f || VerticalSpeed > 0.0f)
		{
			// 턱에서 떨어지는 중: 중력으로 내려간다(순간이동처럼 붙지 않게).
			VerticalSpeed -= 980.0f * DeltaSeconds;
			Location.Z = FMath::Max(Location.Z + VerticalSpeed * DeltaSeconds, TargetZ);
		}
		else
		{
			// 오르막은 부드럽게 따라 올라간다.
			VerticalSpeed = 0.0f;
			Location.Z = FMath::FInterpTo(Location.Z, TargetZ, DeltaSeconds, 12.0f);
		}
	}
	else if (HasAuthority())
	{
		// 땅이 없다(다리 밖·절벽): 떨어진다.
		VerticalSpeed -= 980.0f * DeltaSeconds;
		Location.Z += VerticalSpeed * DeltaSeconds;
	}
	else
	{
		// 멀티(9/28): 모는 사람 화면의 미리 계산에서는 떨어뜨리지 않는다. 맵 가장자리 밖에서는 서버가 경계로 되미는데
		//   이 화면만 땅 밑으로 떨어져 카메라가 땅 속으로 들어갔다(사용자 PIE). 높이는 서버를 따른다(OnRep_DriverPose 차이 흘려 넣기).
		VerticalSpeed = 0.0f;
	}
	Rotation.Yaw = Yaw;
	SetActorLocationAndRotation(Location, Rotation, false, nullptr, ETeleportType::None);

	// 밀쳐진 움직임: 옆으로 미끄러지며(건물에 막히면 멈춤), 땅에 닿아 있으면 궤도 마찰로 빠르게 멈춘다.
	if (!ShoveVelocity.IsNearlyZero(20.0f))
	{
		FHitResult ShoveHit;
		AddActorWorldOffset(ShoveVelocity * DeltaSeconds, true, &ShoveHit);
		if (ShoveHit.bBlockingHit)
			ShoveVelocity = FVector::VectorPlaneProject(ShoveVelocity, ShoveHit.ImpactNormal) * 0.3f;
		const bool bAirborne = VerticalSpeed > 0.0f || Contacts < 3;
		ShoveVelocity *= FMath::Exp((bAirborne ? -0.5f : -3.0f) * DeltaSeconds);
		ShoveSpin *= FMath::Exp(-3.0f * DeltaSeconds);
	}
	else
	{
		ShoveVelocity = FVector::ZeroVector;
		ShoveSpin = 0.0f;
	}

	if (FMath::Abs(CurrentSpeed) < 1.0f)
		return;
	// 앞(뒤)을 먼저 밀어 버리고, 그다음 몸을 옮긴다. 옮기다 막히면 한 번 더 밀어 보고, 그래도 막히면 벽을 따라 미끄러진다.
	const FVector Forward = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Direction = Forward * FMath::Sign(CurrentSpeed);
	KnockAhead(Direction, FMath::Abs(CurrentSpeed));
	const FVector Delta = Forward * CurrentSpeed * DeltaSeconds;
	FHitResult Hit;
	AddActorWorldOffset(Delta, true, &Hit);
	if (!Hit.bBlockingHit)
		return;
	if (AActor* Monster = Hit.GetActor(); PGCreature::IsCreature(Monster))
	{
		RamMonster(Monster, Direction, FMath::Abs(CurrentSpeed));
		return;
	}
	const FVector Impulse = Direction * (FMath::Abs(CurrentSpeed) * 1.1f + 400.0f) + FVector(0.0f, 0.0f, 250.0f);
	// 탱크 키보다 낮은 넓은 판(평평한 1층 건물·단상)은 넓어도 밀어 버린다(거인과 같은 LowHeight 규칙). 9/20 PIE: 넓은 판때기에 막혀 짜증.
	if (PGPhysicsUtil::TryKnockProp(Hit.GetComponent(), Hit, Impulse, this, KnockMaxRadius, 200.0f, (HalfExtent.Z + GroundClearance) * 2.0f))
		return;
	// 턱 오르기: 막힌 면이 벽처럼 서 있고 그 높이가 MaxStepHeight 안이면, 몸을 턱 높이만큼 들어 올린 채 다시 앞으로 가 본다.
	// 캐릭터 계단 오르기와 같은 순서(올리기 → 앞으로 → 다음 프레임 땅 따라 내려앉기). 앞으로 못 가면 원래 자리로 되돌린다.
	// 없을 때는 시설 바닥 슬래브(약 1m) 앞에서 벽처럼 막혔다(9/18 PIE).
	const float GroundUnder = GetActorLocation().Z - HalfExtent.Z - GroundClearance;
	const float Ledge = Hit.ImpactPoint.Z - GroundUnder;
	if (FMath::Abs(Hit.ImpactNormal.Z) < 0.7f && Ledge > 0.0f && Ledge <= MaxStepHeight * TankScale)
	{
		const FVector Before = GetActorLocation();
		AddActorWorldOffset(FVector(0.0f, 0.0f, Ledge + 15.0f), true);
		FHitResult Forward2;
		AddActorWorldOffset(Delta * (1.0f - Hit.Time) + Direction * 20.0f, true, &Forward2);
		if (!Forward2.bBlockingHit || Forward2.Time > 0.2f)
		{
			VerticalSpeed = 0.0f;
			return;
		}
		SetActorLocation(Before);
	}
	FVector Slide = FVector::VectorPlaneProject(Delta * (1.0f - Hit.Time), Hit.ImpactNormal);
	Slide.Z = 0.0f;
	AddActorWorldOffset(Slide, true);
	// 건물에 정면으로 박으면 속도가 빠르게 죽는다(벽에 대고 계속 가속만 쌓이지 않게).
	CurrentSpeed *= FMath::Clamp(1.0f - FMath::Abs(FVector::DotProduct(Hit.ImpactNormal, Direction)) * 0.5f, 0.0f, 1.0f);
}

void APGTankPawn::KnockAhead(const FVector& Direction, float Speed)
{
	// 차체 폭·높이만 한 얇은 상자를 가는 방향으로 스윕. 닿기 조금 전에 밀어 버려 탱크가 멈칫하지 않게 한다.
	const FVector Start = GetActorLocation();
	const float Reach = HalfExtent.X + 60.0f + Speed * 0.05f;
	const FCollisionShape Shape = FCollisionShape::MakeBox(FVector(30.0f, HalfExtent.Y + 20.0f, HalfExtent.Z));
	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankKnock), false, this);
	if (IsValid(RiderPawn))
		Params.AddIgnoredActor(RiderPawn);
	GetWorld()->SweepMultiByObjectType(Hits, Start, Start + Direction * Reach, FRotator(0.0f, GetActorRotation().Yaw, 0.0f).Quaternion(), ObjectTypes, Shape, Params);

	// 나무는 속도 변화 400 이상이어야 꺾인다(PGPhysicsUtil). 탱크는 천천히 밀어도 넘어가게 기본 400 을 더한다.
	const FVector Impulse = Direction * (Speed * 1.1f + 400.0f) + FVector(0.0f, 0.0f, FMath::Min(Speed * 0.3f, 400.0f));
	for (const FHitResult& Hit : Hits)
	{
		if (AActor* Monster = Hit.GetActor(); PGCreature::IsCreature(Monster))
		{
			RamMonster(Monster, Direction, Speed);
			continue;
		}
		PGPhysicsUtil::TryKnockProp(Hit.GetComponent(), Hit, Impulse, this, KnockMaxRadius, 200.0f, (HalfExtent.Z + GroundClearance) * 2.0f);
	}
}

void APGTankPawn::RamMonster(AActor* Monster, const FVector& Direction, float Speed)
{
	if (!HasAuthority())
		return; // 피해·밀치기는 서버만(모는 사람 화면의 미리 계산도 여기를 지난다 — 멀티 9/28)
	if (!PGCreature::IsAliveCreature(Monster))
		return;
	const bool bSmall = PGCreature::GetBodyRadius(Monster) <= 150.0f;
	// 작은 몬스터는 속도와 상관없이 깔린다(즉사). 예전에는 느리게(200 미만) 가면 아무 일도 없이 슬라임 하나에 탱크가 막혀 섰다.
	// 죽으면 캡슐 충돌이 꺼지므로(APGMonsterCharacter::Die) 다음 프레임부터 그대로 지나간다.
	if (bSmall && Speed >= 30.0f)
	{
		UGameplayStatics::ApplyDamage(Monster, PGCreature::GetMaxHealth(Monster) * 10.0f, GetController(), this, UDamageType::StaticClass());
		return;
	}
	if (Speed < 200.0f)
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	float& LastTime = LastRamTime.FindOrAdd(TWeakObjectPtr<AActor>(Monster), -1000.0f);
	if (Now - LastTime < 0.5f)
		return;
	LastTime = Now;
	// 탱크는 무겁다: 차(속도×0.01)의 세 배. 50km/h 로 들이받으면 42.
	// 여기까지 오는 건 큰 몬스터(크리처 등)뿐이다. 날리지 않고 피해만 준다.
	const float Damage = Speed * 0.03f;
	UGameplayStatics::ApplyDamage(Monster, Damage, GetController(), this, UDamageType::StaticClass());
}

// ---- 포 ----

void APGTankPawn::OnRep_Aim()
{
	TurretMesh->SetRelativeRotation(FRotator(0.0f, TurretYaw, 0.0f));
	GunMesh->SetRelativeRotation(FRotator(GunPitch, 0.0f, 0.0f));
}

void APGTankPawn::TickAim(float DeltaSeconds)
{
	if (bHasAim && !bWrecked && IsRidden())
	{
		// 포탑: 조준점 방향(월드 요) - 차체 요 = 포탑이 돌아야 할 각. 초당 TurretTurnRate 만큼만 돈다.
		const FVector Pivot = TurretMesh->GetComponentLocation();
		const float WantYaw = FRotator::NormalizeAxis((AimPoint - Pivot).Rotation().Yaw - GetActorRotation().Yaw);
		const float DeltaYaw = FMath::Clamp(FRotator::NormalizeAxis(WantYaw - TurretYaw), -TurretTurnRate * DeltaSeconds, TurretTurnRate * DeltaSeconds);
		TurretYaw = FRotator::NormalizeAxis(TurretYaw + DeltaYaw);
		// 포신: 포탑 공간에서 본 조준점의 높이각.
		const FVector Local = TurretMesh->GetComponentTransform().InverseTransformVectorNoScale(AimPoint - GunMesh->GetComponentLocation());
		const float WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(Local.Z, FVector2D(Local.X, Local.Y).Size())), GunMinPitch, GunMaxPitch);
		GunPitch = FMath::FInterpConstantTo(GunPitch, WantPitch, DeltaSeconds, 40.0f);
	}
	OnRep_Aim();
}

FVector APGTankPawn::GetMuzzleLocation() const
{
	return GunMesh->GetComponentTransform().TransformPosition(MuzzleOffset);
}

bool APGTankPawn::FireMainGun()
{
	UWorld* World = GetWorld();
	// 간격 검사에 20% 여유(멀티 9/27): 클라가 자기 시계로 맞춰 보낸 발사가 네트워크 흔들림으로 조금 일찍 도착하면 씹혔다.
	if (!HasAuthority() || bWrecked || !World || World->GetTimeSeconds() - LastFireTime < FireInterval * 0.8f)
		return false;
	LastFireTime = World->GetTimeSeconds();
	// 판 기록(사격 수). 플레이어가 탄 탱크일 때만 센다.
	UPGRunSubsystem::NotifyShotFired(this);

	const FVector Muzzle = GetMuzzleLocation();
	const FVector Direction = GunMesh->GetForwardVector();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankShell), false, this);
	if (IsValid(RiderPawn))
		Params.AddIgnoredActor(RiderPawn);
	const bool bHit = World->LineTraceSingleByChannel(Hit, Muzzle, Muzzle + Direction * Range, ECC_Visibility, Params);
	const FVector Impact = bHit ? Hit.ImpactPoint : Muzzle + Direction * Range;
	// 포 소리는 크다: 몬스터 청각(45m)에 두 군데로 알린다 — 쏜 곳과 떨어진 곳.
	UAISense_Hearing::ReportNoiseEvent(World, Muzzle, 1.0f, this, 0.0f, TEXT("TankGun"));
	if (bHit)
	{
		Explode(Impact, Hit.GetActor());
		UAISense_Hearing::ReportNoiseEvent(World, Impact, 1.0f, this, 0.0f, TEXT("TankShell"));
	}
	MulticastShotFx(Muzzle, Impact, bHit);
	UE_LOG(LogPGObjects, Display, TEXT("%s fired: hit=%s at %s"), *GetName(), *GetNameSafe(Hit.GetActor()), *Impact.ToCompactString());
	return true;
}

void APGTankPawn::Explode(const FVector& Location, AActor* DirectHit)
{
	UWorld* World = GetWorld();
	AController* ShooterController = GetController();
	TArray<AActor*> Ignore = { this };
	if (IsValid(RiderPawn))
		Ignore.Add(RiderPawn);
	if (IsValid(DirectHit) && DirectHit != this)
		UGameplayStatics::ApplyDamage(DirectHit, DirectDamage, ShooterController, this, UDamageType::StaticClass());
	// 범위 피해: 가운데 30% 는 전부, 가장자리는 20% 까지 줄어든다. 벽 뒤(보이지 않는 곳)는 안 맞는다.
	UGameplayStatics::ApplyRadialDamageWithFalloff(World, BlastDamage, BlastDamage * 0.2f, Location, BlastRadius * 0.3f, BlastRadius, 1.0f,
		UDamageType::StaticClass(), Ignore, this, ShooterController, ECC_Visibility);

	// 폭발 둘레의 소품·나무를 바깥으로 날리고, 작은 몬스터는 띄운다.
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankBlast), false, this);
	World->OverlapMultiByObjectType(Overlaps, Location, FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(BlastRadius * 0.6f), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!IsValid(Component))
			continue;
		const FVector Outward = (Component->GetComponentLocation() - Location).GetSafeNormal2D();
		if (AActor* Monster = Overlap.GetActor(); PGCreature::IsCreature(Monster))
		{
			if (PGCreature::IsAliveCreature(Monster) && PGCreature::GetBodyRadius(Monster) <= 150.0f)
				PGCreature::Launch(Monster, Outward * 900.0f + FVector(0.0f, 0.0f, 700.0f));
			continue;
		}
		// 인스턴스 메시(PCG 나무·바위)는 몇 번째 인스턴스인지가 Item 에 있어야 그 하나만 떼어 낸다.
		FHitResult Fake;
		Fake.Component = Component;
		Fake.Item = Overlap.ItemIndex;
		Fake.ImpactPoint = Location;
		Fake.Location = Location;
		PGPhysicsUtil::TryKnockProp(Component, Fake, Outward * 1000.0f + FVector(0.0f, 0.0f, 500.0f), this, 600.0f, 100.0f);
	}
}

void APGTankPawn::MulticastShotFx_Implementation(FVector_NetQuantize Muzzle, FVector_NetQuantize Impact, bool bExploded)
{
	if (CVarTankDebugShots.GetValueOnGameThread() == 0)
		return;
	DrawDebugLine(GetWorld(), Muzzle, Impact, FColor::Yellow, false, 0.3f, 0, 6.0f);
	if (bExploded)
		DrawDebugSphere(GetWorld(), Impact, BlastRadius, 16, FColor::Orange, false, 0.5f, 0, 3.0f);
}

// ---- 체력 ----

float APGTankPawn::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || bWrecked || Applied <= 0.0f || DamageCauser == this)
		return Applied;
	Health = FMath::Max(0.0f, Health - Applied);
	UE_LOG(LogPGObjects, Display, TEXT("%s took %.0f from %s (hp %.0f/%.0f)"), *GetName(), Applied, *GetNameSafe(DamageCauser), Health, MaxHealth);
	if (Health <= 0.0f)
		Wreck();
	return Applied;
}

void APGTankPawn::Wreck()
{
	// 부서지면 타고 있던 사람을 내려 준다(탱크 안에서 같이 죽지 않게 — 탑승자는 숨겨져 피해를 안 받는다).
	if (IsRidden())
		Dismount();
	bWrecked = true;
	CurrentSpeed = 0.0f;
	UE_LOG(LogPGObjects, Display, TEXT("%s wrecked"), *GetName());
}

void APGTankPawn::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	// 멀티(9/27): 탑승 F 가 아직 눌려 있다. 이 컴퓨터가 조종을 넘겨받은 순간 "눌린 상태·탑승 시각" 을 새로 잡는다.
	// 전에는 Mount(서버)에서만 잡아 클라이언트는 탑승 F 를 곧바로 하차로 읽었다(차와 같은 문제, APGVehiclePawn 참고).
	if (IsLocallyControlled())
	{
		bDismountKeyWasDown = true;
		MountedTime = GetWorld()->GetTimeSeconds();
	}
}
