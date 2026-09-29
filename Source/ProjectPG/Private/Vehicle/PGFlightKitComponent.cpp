#include "Vehicle/PGFlightKitComponent.h"

#include "Common/PGVisualSettings.h"
#include "Animation/AnimSequence.h"
#include "Common/PGKeyPolling.h"
#include "Common/PGPhysicsUtil.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PawnMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Combat/PGCreatureAccess.h"
#include "Finale/PGBattleshipActor.h"
#include "Finale/PGFinaleDirector.h"
#include "Objects/PGObjectTypes.h"
#include "Flow/PGRunSubsystem.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/PlayerState.h"

namespace
{
	const TCHAR* const BoosterDir = TEXT("/Game/PG/Characters/SchoolGirl/Booster/");
	FSoftObjectPath BoosterPath(const TCHAR* Name) { return FSoftObjectPath(FString::Printf(TEXT("%s%s.%s"), BoosterDir, Name, Name)); }
}

UClass* UPGFlightKitComponent::GetSpawnClass()
{
	const TSoftClassPtr<UActorComponent>& Designed = UPGVisualSettings::Get().FlightKitClass;
	if (!Designed.IsNull())
	{
		UClass* Loaded = Designed.LoadSynchronous();
		if (Loaded && Loaded->IsChildOf(UPGFlightKitComponent::StaticClass()))
			return Loaded;
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: flight kit %s not usable — using the C++ class"), *Designed.ToString());
	}
	return UPGFlightKitComponent::StaticClass();
}

UPGFlightKitComponent::UPGFlightKitComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
	BoosterMesh = TSoftObjectPtr<USkeletalMesh>(BoosterPath(TEXT("SK_CarBoosterKit")));
	DeployAnim = TSoftObjectPtr<UAnimSequence>(BoosterPath(TEXT("A_CarBooster_Deploy")));
	RetractAnim = TSoftObjectPtr<UAnimSequence>(BoosterPath(TEXT("A_CarBooster_Retract")));
	FlameMesh = TSoftObjectPtr<UStaticMesh>(BoosterPath(TEXT("SM_BoosterFlame")));
	BeamMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Characters/Quantum/Wig/SM_WigBeam.SM_WigBeam")));
}

void UPGFlightKitComponent::BeginPlay()
{
	Super::BeginPlay();
	Fuel = MaxFuelSeconds;
	// 십자 조준선은 화면(캔버스)에 그려야 하므로 월드 그리기가 아니라 디버그 그리기 서비스에 건다.
	// 진짜 조준 UI 는 팀원이 만드는 중이라 이것은 임시다.
	CrosshairHandle = UDebugDrawService::Register(TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &UPGFlightKitComponent::DrawCrosshair));
}

void UPGFlightKitComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 안 빼면 사라진 차의 조준선이 계속 그려진다(델리게이트가 죽은 객체를 붙들고 있다).
	if (CrosshairHandle.IsValid())
	{
		UDebugDrawService::Unregister(CrosshairHandle);
		CrosshairHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

USkeletalMeshComponent* UPGFlightKitComponent::GetCarMesh() const
{
	// 차 몸 = 루트 스켈레탈 메시(바퀴 물리 몸). FindComponentByClass 는 붙여 둔 부스터 메시를 잡을 수 있어 루트로 찾는다.
	return GetOwner() ? Cast<USkeletalMeshComponent>(GetOwner()->GetRootComponent()) : nullptr;
}

void UPGFlightKitComponent::EnsureVisuals()
{
	if (IsValid(Booster))
		return;
	USkeletalMeshComponent* Car = GetCarMesh();
	USkeletalMesh* Mesh = BoosterMesh.LoadSynchronous();
	if (!Car || !Mesh)
		return;
	// 부스터 키트는 차와 같은 공간(원점·방향·cm)으로 만들어져서 차 메시에 (0,0,0) 으로 붙이면 제자리다.
	Booster = NewObject<USkeletalMeshComponent>(GetOwner(), TEXT("BoosterKit"));
	Booster->SetSkeletalMeshAsset(Mesh);
	Booster->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Booster->SetupAttachment(Car);
	Booster->RegisterComponent();
	Booster->bComponentUseFixedSkelBounds = true; // 접힌 상태에서 시작해도 화면에서 잘리지 않게
	// 기본 자세 = 접기 애니의 마지막 프레임. 재작업본(9/20)은 접으면 로켓이 차체 안으로 진짜 들어가서
	// 컴포넌트를 숨길 필요가 없다 — 대신 가만두면 펼친 자세로 서 있으므로 접은 자세를 한 번 재생해 둔다.
	if (UAnimSequence* Folded = RetractAnim.LoadSynchronous())
	{
		Booster->PlayAnimation(Folded, false);
		Booster->SetPosition(Folded->GetPlayLength(), false);
	}
	// 불꽃: 노즐 뼈(ThrustL/ThrustR)에 붙인다. 불꽃 메시는 피벗(노즐)에서 -X 로 100cm 뻗는다(X 배율 = 길이).
	if (UStaticMesh* Flame = FlameMesh.LoadSynchronous())
	{
		for (const TCHAR* Socket : { TEXT("ThrustL"), TEXT("ThrustR") })
		{
			UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(GetOwner());
			Component->SetStaticMesh(Flame);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Component->SetCastShadow(false);
			Component->SetupAttachment(Booster, Socket);
			// 노즐 뼈가 요 -90도로 누워 있어(가져오기 로그: ThrustL/R rot Y-90) 그대로 붙이면 불꽃이 옆으로 나간다. +90 돌려 뒤(-X)로.
			Component->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
			Component->RegisterComponent();
			Component->SetVisibility(false);
			Flames.Add(Component);
		}
	}
}

void UPGFlightKitComponent::SetDeployed(bool bDeploy)
{
	if (bDeployed == bDeploy)
		return;
	bDeployed = bDeploy;
	PlayDeployVisual();
	DeployedAt = GetWorld()->GetTimeSeconds();
}

void UPGFlightKitComponent::PlayDeployVisual()
{
	EnsureVisuals();
	if (IsValid(Booster))
	{
		if (UAnimSequence* Anim = (bDeployed ? DeployAnim : RetractAnim).LoadSynchronous())
			Booster->PlayAnimation(Anim, false);
	}
}

void UPGFlightKitComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGFlightKitComponent, bDeployed);
	DOREPLIFETIME(UPGFlightKitComponent, bArcade);
	DOREPLIFETIME(UPGFlightKitComponent, bFlameOn);
	DOREPLIFETIME_CONDITION(UPGFlightKitComponent, DriverPoseLocation, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION(UPGFlightKitComponent, DriverPoseRotation, COND_AutonomousOnly);
	DOREPLIFETIME_CONDITION(UPGFlightKitComponent, DriverPoseVelocity, COND_AutonomousOnly);
}

void UPGFlightKitComponent::OnRep_Deployed()
{
	PlayDeployVisual();
}

void UPGFlightKitComponent::OnRep_Arcade()
{
	ApplyArcadePhysicsOnClient();
}

void UPGFlightKitComponent::ApplyArcadePhysicsOnClient()
{
	// 서버의 EnterArcade/ExitArcade 와 같은 물리 끄기·켜기. 속도 이어받기 같은 계산은 서버 몫이라 여기서는 하지 않는다.
	if (GetOwner() && GetOwner()->HasAuthority())
		return;
	if (USkeletalMeshComponent* Car = GetCarMesh())
	{
		// 미리 계산(모는 사람 화면)의 출발 상태: 서버의 EnterArcade 와 같게 지금 속도·기수에서 시작한다(멀티 9/28).
		if (bArcade)
		{
			ArcadeVelocity = Car->IsSimulatingPhysics() ? Car->GetPhysicsLinearVelocity() : FVector::ZeroVector;
			ArcadeYaw = FRotator::NormalizeAxis(GetOwner()->GetActorRotation().Yaw);
			HoverFallSeconds = 0.0f;
		}
		Car->SetSimulatePhysics(!bArcade);
	}
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
			Movement->SetActive(!bArcade);
}

void UPGFlightKitComponent::OnRep_DriverPose()
{
	// 모는 사람 화면: 서버가 코드로 옮긴 차 위치(엔진은 조종하는 본인에게 위치를 안 보낸다 — 탱크와 같은 이유).
	// 멀티(9/28): 바로 옮기면 위치가 올 때마다 "버버벅" 끊겼다 — 받아 두고 TickComponent 에서 부드럽게(DriverSmoother).
	// 9/28 두 번째: 모는 사람 화면은 스스로 미리 계산하므로(TickComponent), 여기서는 서버와의 차이만 조금씩 맞춘다.
	//   서버 위치는 망 한쪽 길이만큼 과거라, 서버 속도(연속으로 받은 위치로 어림)로 그만큼 앞으로 당겨 비교한다.
	//   8m 넘게 어긋나면(서버가 막혔다·순간이동) 바로 옮긴다. 비교 스위치 PG.NetSmooth 0 이면 예전처럼 바로 옮긴다.
	AActor* Owner = GetOwner();
	if (!bArcade || !Owner || Owner->GetLocalRole() != ROLE_AutonomousProxy)
		return;
	DriverSmoother.Receive(DriverPoseLocation, DriverPoseRotation, GetWorld()->GetTimeSeconds());
	if (FPGNetPoseSmoother::Mode() != 1)
	{
		if (!FPGNetPoseSmoother::IsEnabled()) // 0: 예전처럼 바로 옮김. 2: Tick 의 따라가기가 맡는다
			Owner->SetActorLocationAndRotation(DriverPoseLocation, DriverPoseRotation, false, nullptr, ETeleportType::TeleportPhysics);
		return;
	}
	float OneWaySeconds = 0.03f;
	if (const APawn* Pawn = Cast<APawn>(Owner))
		if (const APlayerState* State = Pawn->GetPlayerState())
			OneWaySeconds = FMath::Clamp(State->ExactPing * 0.0005f, 0.0f, 0.3f); // ExactPing = 왕복 ms
	const FVector ServerNow = FVector(DriverPoseLocation) + FVector(DriverPoseVelocity) * OneWaySeconds;
	const FVector Error = ServerNow - Owner->GetActorLocation();
	DriverMaxErrorCm = FMath::Max(DriverMaxErrorCm, static_cast<float>(Error.Size()));
	if (Error.SizeSquared() > FMath::Square(800.0f))
	{
		Owner->SetActorLocation(ServerNow, false, nullptr, ETeleportType::TeleportPhysics);
		ArcadeYaw = FRotator::NormalizeAxis(DriverPoseRotation.Yaw);
		PendingCorrection = FVector::ZeroVector;
		++DriverHardCorrections;
		return;
	}
	// 1.5m 안쪽 차이는 맞추지 않는다(내 계산을 믿는다) — 같은 입력·같은 계산이라 작은 차이는 망 타이밍 흔들림일 뿐이고,
	//   그걸 매번 맞추면 차가 앞뒤로 당겨져 "조금씩 멈칫" 했다(9/28 사용자 PIE). 넘친 만큼만 흘려 넣는다.
	constexpr float DeadZoneCm = 150.0f;
	const float ErrorSize = Error.Size();
	PendingCorrection = ErrorSize > DeadZoneCm ? Error * ((ErrorSize - DeadZoneCm) / ErrorSize) : FVector::ZeroVector;
	const float YawGap = FMath::FindDeltaAngleDegrees(ArcadeYaw, DriverPoseRotation.Yaw);
	if (FMath::Abs(YawGap) > 3.0f) // 기수도 3도 안쪽 차이는 두고 넘친 만큼만
		ArcadeYaw = FRotator::NormalizeAxis(ArcadeYaw + (YawGap - FMath::Sign(YawGap) * 3.0f) * 0.1f);
}

bool UPGFlightKitComponent::ReceiveProxyPose(const FVector& Location, const FRotator& Rotation)
{
	if (!bArcade)
	{
		ProxySmoother.Reset(); // 땅에서는 차 물리 복제(엔진)가 맡는다
		return false;
	}
	ProxySmoother.Receive(Location, Rotation, GetWorld()->GetTimeSeconds());
	return true;
}

void UPGFlightKitComponent::MulticastBeamFx_Implementation(FVector_NetQuantize Muzzle, FVector_NetQuantize Impact)
{
	// 빔 그림: 포구에서 탄착점까지 늘린다. TickComponent 가 0.12초 뒤 끈다(서버·클라 모두).
	EnsureBeam();
	if (!IsValid(BeamMesh) || !GetWorld())
		return;
	const FVector Direction = (FVector(Impact) - FVector(Muzzle)).GetSafeNormal();
	const float Distance = FMath::Max(50.0f, static_cast<float>(FVector::Dist(Muzzle, Impact)));
	BeamMesh->SetWorldLocation(Muzzle);
	BeamMesh->SetWorldRotation(Direction.Rotation());
	BeamMesh->SetWorldScale3D(FVector(Distance / 100.0f, 1.2f, 1.2f));
	BeamMesh->SetVisibility(true);
	BeamShownAt = GetWorld()->GetTimeSeconds();
}

bool UPGFlightKitComponent::IsAirborne(float& OutHeight) const
{
	const AActor* Owner = GetOwner();
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFlightGround), false, Owner);
	const FVector Start = Owner->GetActorLocation();
	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0f, 0.0f, 20000.0f), ECC_Visibility, Params))
	{
		OutHeight = Start.Z - Hit.ImpactPoint.Z;
		return OutHeight > 120.0f;
	}
	// 땅을 못 찾았다(호수 위·맵 밖). 터무니없이 큰 값을 돌려주면 "이미 최고 고도"로 쳐서 상승력이 0 이 된다
	// — 9/20 PIE: 오른쪽을 눌러도 안 올라오고 떨어지기만 했다. 적당한 값으로 둔다.
	OutHeight = 3000.0f;
	return true;
}

void UPGFlightKitComponent::ServerSetThrust_Implementation(bool bInThrust, bool bInForward, float InYaw, float InViewYaw)
{
	bThrust = bInThrust;
	bForward = bInForward;
	YawInput = FMath::Clamp(InYaw, -1.0f, 1.0f);
	RemoteViewYaw = FRotator::NormalizeAxis(InViewYaw);
	bHasRemoteViewYaw = true;
}

void UPGFlightKitComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
		return;
	// 멀티(9/28): 클라이언트 화면의 날아다니는 차.
	//   구경하는 사람: 서버 위치를 부드럽게 따라간다(ProxySmoother).
	//   모는 사람: 서버를 따라가기만 하면 "입력 → 서버 → 내 화면" 만큼 늦게 반응해 어색했다(9/28 사용자 PIE).
	//     그래서 같은 비행 계산(TickArcade)을 이 화면에서 내 입력으로 바로 돌리고(아래 입력 읽기 뒤), 서버 위치는 차이만 조금씩 맞춘다(OnRep_DriverPose).
	//   날지 않으면 잊는다(다음 비행에 옛 위치로 끌려가지 않게).
	if (!Pawn->HasAuthority())
	{
		if (!bArcade)
		{
			DriverSmoother.Reset();
			ProxySmoother.Reset();
		}
		else if (Pawn->GetLocalRole() != ROLE_AutonomousProxy)
			ProxySmoother.Step(Pawn, GetWorld()->GetTimeSeconds(), DeltaTime);
		else if (FPGNetPoseSmoother::Mode() == 2)
			DriverSmoother.Step(Pawn, GetWorld()->GetTimeSeconds(), DeltaTime); // 비교용: 첫 시도(따라가기만)
	}

	// 입력: 탄 사람 화면에서 오른쪽 버튼·A/D 를 읽는다. 서버면 바로 쓰고, 클라이언트면 바뀔 때만 보낸다.
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()); PC && Pawn->IsLocallyControlled())
	{
		const bool bHeld = PGKeyPolling::IsDown(PC, EKeys::RightMouseButton);
		const FVector2D Wasd = PGKeyPolling::ReadWasd(PC);
		const bool bAhead = Wasd.X > 0.1f; // W
		const float Yaw = Wasd.Y;
		// 이 화면의 입력은 이 화면에서도 바로 쓴다(서버면 원래대로, 클라이언트면 비행 미리 계산용 — 멀티 9/28).
		bThrust = bHeld;
		bForward = bAhead;
		YawInput = Yaw;
		if (!Pawn->HasAuthority())
		{
			// 시선은 2도 넘게 바뀌었을 때만 같이 보낸다(마우스를 움직일 때마다 보내지 않게).
			const float ViewYaw = PC->GetControlRotation().Yaw;
			if (bHeld != bSentThrust || bAhead != bSentForward || !FMath::IsNearlyEqual(Yaw, SentYaw)
				|| FMath::Abs(FMath::FindDeltaAngleDegrees(ViewYaw, SentViewYaw)) > 0.5f) // 9/28: 2도 → 0.5도. 서버가 다른 쪽으로 날면 모는 사람 화면(미리 계산)과 어긋난다
			{
				ServerSetThrust(bHeld, bAhead, Yaw, ViewYaw);
				bSentThrust = bHeld;
				bSentForward = bAhead;
				SentYaw = Yaw;
				SentViewYaw = ViewYaw;
			}
		}

		// 좌클릭: 앞으로 빔. 달라붙은 몹을 떼어낸다.
		// 여기는 "이 차를 지금 직접 몰고 있는 사람" 의 화면에서만 도는 자리다(PC 가 이 폰을 조종 중).
		// 그래서 전함 조종석의 좌클릭 주포와 절대 겹치지 않는다 — 함교에 앉아 있으면 조종 중인 폰은 차가 아니라 사람이다.
		// 지상 주행이든 아케이드 비행이든 똑같이 쏜다(bArcade 를 보지 않는다).
		// 십자 조준선은 DrawCrosshair 가 화면에 그린다(여기서는 아무것도 안 그린다).
		// 겨누는 지점은 쏠 때만 계산한다 — 십자가 화면 고정이라 미리 알 필요가 없다.
		if (PGKeyPolling::IsDown(PC, EKeys::LeftMouseButton) && GetWorld()->GetTimeSeconds() - LastFireTime >= FireInterval)
		{
			const FVector Aim = ComputeAimPoint(PC);
			if (Pawn->HasAuthority())
			{
				FireBeam(Aim);
			}
			else
			{
				LastFireTime = GetWorld()->GetTimeSeconds(); // 클라가 매 프레임 RPC 를 보내지 않게
				ServerFireBeam(Aim);
			}
		}
	}
	else if (Pawn->HasAuthority() && !Pawn->GetController())
	{
		bThrust = false; // 내린 차는 날지 않는다
		bForward = false;
	}

	// 빔은 0.12초만 보인다. 껐다 켜는 것만으로 "쐈다"가 읽힌다(전함 주포와 같은 방식).
	if (BeamShownAt > 0.0 && IsValid(BeamMesh) && GetWorld()->TimeSince(BeamShownAt) > 0.12)
	{
		BeamMesh->SetVisibility(false);
		BeamShownAt = -100.0;
	}
	// 모는 사람 화면: 비행을 미리 계산한다(위 주석). 착지·갑판 붙기는 서버가 정한다(TickArcade 안에서 서버만).
	if (!Pawn->HasAuthority() && bArcade && Pawn->GetLocalRole() == ROLE_AutonomousProxy && FPGNetPoseSmoother::Mode() == 1)
	{
		TickArcade(DeltaTime);
		// 서버와의 차이(OnRep_DriverPose 가 잰 것)를 한 번에 옮기지 않고 프레임마다 조금씩 흘려 넣는다 — 한 번에 옮기면 그만큼 툭 튄다.
		const FVector Step = PendingCorrection * (1.0f - FMath::Exp(-4.0f * DeltaTime)); // 8 → 4(9/28): 더 천천히 맞춘다
		PendingCorrection -= Step;
		Pawn->AddActorWorldOffset(Step, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (!Pawn->HasAuthority())
	{
		// 클라이언트: 불꽃은 서버가 알려 준 상태로(길이 떨림은 각자).
		for (UStaticMeshComponent* Flame : Flames)
			if (IsValid(Flame))
			{
				Flame->SetVisibility(bFlameOn);
				if (bFlameOn)
					Flame->SetRelativeScale3D(FVector(FMath::FRandRange(0.8f, 1.2f), 1.0f, 1.0f));
			}
		return;
	}
	// 갑판에 붙여 둔 차에 사람이 타면 바로 떼어 준다. 붙어 있는 동안은 물리가 꺼져 있어서
	// 그대로 두면 운전이 안 된다(9/20 PIE: "자동차가 우주선에 붙어버리는데?").
	if (bDocked && Pawn->GetController())
		UndockFromShip();
	const double Now = GetWorld()->GetTimeSeconds();
	float Height = 0.0f;
	const bool bAirborne = IsAirborne(Height); // Height 는 여기선 안 쓴다(ApplyFlightForces 가 따로 잰다)
	// 공중에서 W 만 눌러도 계속 난다. 안 그러면 오른쪽 버튼을 놓는 순간 떨어져서 앞으로 갈 수가 없다.
	const bool bWantThrust = bThrust || (bForward && bAirborne);
	if (bWantThrust && (!bLimitedFuel || Fuel > 0.0f))
	{
		SetDeployed(true);
		LastThrustAt = Now;
	}
	else if (bDeployed && !bAirborne && Now - LastThrustAt > 1.5f)
	{
		SetDeployed(false); // 착지하고 잠깐 지나면 접는다
	}
	const bool bFiring = bWantThrust && (!bLimitedFuel || Fuel > 0.0f) && bDeployed && Now - DeployedAt >= DeploySeconds;
	bFlameOn = bFiring; // 클라이언트 불꽃(복제)
	for (UStaticMeshComponent* Flame : Flames)
	{
		if (!IsValid(Flame))
			continue;
		Flame->SetVisibility(bFiring);
		if (bFiring)
			Flame->SetRelativeScale3D(FVector(FMath::FRandRange(0.8f, 1.2f), 1.0f, 1.0f)); // 불꽃 길이가 살짝 떨린다
	}
	// 무엇이 눌렸고 실제로 어떤 상태인지 1초에 한 번 남긴다. "안 나간다"가 입력 문제인지,
	// 부스터가 아직 안 펴진 것인지, 힘이 모자란 것인지 이 줄로 구분한다.
	if (GetWorld()->TimeSince(LastFlightLogTime) > 1.0)
	{
		LastFlightLogTime = GetWorld()->GetTimeSeconds();
		USkeletalMeshComponent* Car = GetCarMesh();
		const FVector Velocity = Car ? Car->GetPhysicsLinearVelocity() : FVector::ZeroVector;
		UE_LOG(LogPGObjects, Display,
			TEXT("PGFlight: up=%d fwd=%d firing=%d deployed=%d air=%d sim=%d awake=%d height=%.0fm speed=%.0f/%.0f km/h")
			TEXT(" | arcade=%d hover=%.1fs sink=%.0f dock=%d blocker=%s"),
			bThrust ? 1 : 0, bForward ? 1 : 0, bFiring ? 1 : 0, bDeployed ? 1 : 0, bAirborne ? 1 : 0,
			(Car && Car->IsSimulatingPhysics()) ? 1 : 0, (Car && Car->IsAnyRigidBodyAwake()) ? 1 : 0,
			Height * 0.01f, Velocity.Size2D() * 0.036f, Velocity.Z * 0.036f,
			bArcade ? 1 : 0, HoverFallSeconds, LastSinkSpeed, bDockAssisting ? 1 : 0, *LastArcadeBlocker);
			LastArcadeBlocker.Reset();
	}
	if (bFiring)
	{
		if (bLimitedFuel)
			Fuel = FMath::Max(0.0f, Fuel - DeltaTime);
		// 부스터가 펴지면 물리를 끄고 코드로 난다. 물리로 밀던 시절의 "제멋대로 쏠림"이 여기서 사라진다.
		EnterArcade();
	}
	if (bArcade)
	{
		TickArcade(DeltaTime);
		// 모는 사람 화면용 위치(OnRep_DriverPose). 사람이 몰 때만.
		if (Pawn->IsPlayerControlled())
		{
			DriverPoseLocation = GetOwner()->GetActorLocation();
			DriverPoseRotation = GetOwner()->GetActorRotation();
			DriverPoseVelocity = ArcadeVelocity;
		}
	}
	else if (!bAirborne)
	{
		Fuel = FMath::Min(MaxFuelSeconds, Fuel + FuelRegen * DeltaTime);
	}
	if (!bArcade && (bFiring || bAirborne))
	{
		// 공중에서는 차체를 수평으로 세운다(노즐 힘이 무게중심보다 낮아 코가 들린다) + A/D 선회.
		USkeletalMeshComponent* Car = GetCarMesh();
		if (Car && Car->IsSimulatingPhysics())
		{
			const FVector Up = Car->GetUpVector();
			const FVector LevelAxis = FVector::CrossProduct(Up, FVector::UpVector);
			const FVector AngVel = Car->GetPhysicsAngularVelocityInRadians();
			const FVector Correct = LevelAxis * 6.0f - FVector(AngVel.X, AngVel.Y, 0.0f) * 2.5f;
			const FVector Turn = FVector(0.0f, 0.0f, YawInput * 2.2f - AngVel.Z * 1.5f);
			Car->AddTorqueInRadians((Correct + Turn), NAME_None, true);
		}
	}
}

// ---- 아케이드 비행 ----
//
// 왜 물리를 끄나: 이 차는 Chaos 바퀴 차량이다. 공중에서 힘(AddForce)으로 밀면 바퀴·서스펜션이 없는 상태라
//   무게중심과 토크가 제멋대로 작용해 "어디 쏠리듯" 날아가고, 조용하면 Chaos 가 몸을 재워 조작이 아예 안 먹는다
//   (9/20 PIE 에서 둘 다 겪었다). 전함·탱크·미사일과 같은 방식 — 물리를 끄고 코드로 옮긴다 — 이 훨씬 다루기 쉽다.
// 조작: 오른쪽 = 위로, W = 보는 쪽으로 앞으로(천천히 하강), A/D = 좌우 선회. 땅에 닿으면 물리를 되돌린다.

// 갑판은 "움직이는 액터에 붙은 상자"다. 그 위에 물리 차를 그냥 올려 두면 배가 움직일 때
// 접촉이 끊겨 갑판을 뚫고 빠진다. 그래서 내려앉으면 아예 배에 붙여 버린다 — 배와 한 몸이 되어 같이 간다.
void UPGFlightKitComponent::DockToShipIfLanded()
{
	if (bDocked)
		return;
	AActor* Owner = GetOwner();
	const APGFinaleDirector* Director = APGFinaleDirector::Get(GetWorld());
	APGBattleshipActor* Ship = Director ? Director->GetShip() : nullptr;
	if (!IsValid(Owner) || !IsValid(Ship))
		return;
	// 갑판 높이 근처, 선체 안쪽이어야 한다.
	const FVector Local = Ship->GetActorTransform().InverseTransformPosition(Owner->GetActorLocation());
	const FBox& Bounds = Ship->GetHullLocalBounds();
	const float DeckZ = Ship->GetDeckWorldZ() - Ship->GetActorLocation().Z;
	if (!Bounds.IsValid || Local.X < Bounds.Min.X || Local.X > Bounds.Max.X
		|| FMath::Abs(Local.Y) > Bounds.GetSize().Y * 0.3f
		|| Local.Z < DeckZ - 200.0f || Local.Z > DeckZ + 1200.0f)
		return;
	if (USkeletalMeshComponent* Car = GetCarMesh())
		Car->SetSimulatePhysics(false);
	Owner->AttachToActor(Ship, FAttachmentTransformRules::KeepWorldTransform);
	bDocked = true;
	UE_LOG(LogPGObjects, Display, TEXT("PGFlight: docked on the deck"));
}

void UPGFlightKitComponent::UndockFromShip()
{
	if (!bDocked)
		return;
	bDocked = false;
	if (AActor* Owner = GetOwner())
		Owner->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	if (USkeletalMeshComponent* Car = GetCarMesh())
		Car->SetSimulatePhysics(true);
	UE_LOG(LogPGObjects, Display, TEXT("PGFlight: undocked"));
}

void UPGFlightKitComponent::EnterArcade()
{
	if (bArcade)
		return;
	UndockFromShip(); // 붙어 있었으면 떼고 난다
	USkeletalMeshComponent* Car = GetCarMesh();
	if (!Car)
		return;
	ArcadeVelocity = Car->GetPhysicsLinearVelocity();
	ArcadeYaw = FRotator::NormalizeAxis(GetOwner()->GetActorRotation().Yaw);
	Car->SetSimulatePhysics(false);
	// 바퀴 움직임 컴포넌트도 멈춘다. 안 그러면 착지 판정·서스펜션이 계속 끼어든다.
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
			Movement->SetActive(false);
	bArcade = true;
	HoverFallSeconds = 0.0f; // 새로 날기 시작할 때는 떨어지던 기세를 물려받지 않는다
	UE_LOG(LogPGObjects, Display, TEXT("PGFlight: arcade flight on"));
}

void UPGFlightKitComponent::ExitArcade()
{
	if (!bArcade)
		return;
	bArcade = false;
	USkeletalMeshComponent* Car = GetCarMesh();
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
		if (UPawnMovementComponent* Movement = Pawn->GetMovementComponent())
			Movement->SetActive(true);
	if (Car)
	{
		Car->SetSimulatePhysics(true);
		Car->SetPhysicsLinearVelocity(FVector(ArcadeVelocity.X, ArcadeVelocity.Y, FMath::Min(ArcadeVelocity.Z, 0.0f)));
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGFlight: arcade flight off (landed)"));
}

void UPGFlightKitComponent::TickArcade(float DeltaTime)
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
		return;
	// 선회: A/D 로 기수를 돌리고, 시선 쪽으로 천천히 따라 돈다.
	//
	// 각도는 반드시 -180~180 으로 접어서 다뤄야 한다. 전에는 FInterpTo 로 그냥 보간했는데,
	// 그건 각도를 모르는 일반 실수 보간이라 기수가 179도이고 시선이 -179도이면(실제로는 2도 차이)
	// 0도를 지나 **먼 쪽으로 358도를 돌았다.** 차가 올라가거나 앞으로 갈 때마다 한 바퀴씩 돌던 것이
	// 이것이다(9/20 PIE). 게다가 A/D 입력이 위에서 한 번, 아래에서 또 한 번 더해져 선회가 2배로 먹었다.
	ArcadeYaw = FRotator::NormalizeAxis(ArcadeYaw + YawInput * ArcadeTurnRate * DeltaTime);
	if (const APawn* Pawn = Cast<APawn>(Owner))
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		{
			// 시선: 이 컴퓨터 사람이면 컨트롤 회전, 원격 클라이언트면 그 사람이 보낸 시선(멀티 9/27 — 서버의 컨트롤 회전은 탑승 순간에 멈춰 있다).
			const float ViewYaw = (PC->IsLocalController() || !bHasRemoteViewYaw) ? PC->GetControlRotation().Yaw : RemoteViewYaw;
			// 두 각도의 "짧은 쪽" 차이. 이 값은 늘 -180~180 이라 먼 쪽으로 돌 수가 없다.
			const float Shortest = FMath::FindDeltaAngleDegrees(ArcadeYaw, ViewYaw);
			ArcadeYaw = FRotator::NormalizeAxis(ArcadeYaw + Shortest * FMath::Clamp(DeltaTime * 3.0f, 0.0f, 1.0f));
		}
	// 앞 방향: 기수 쪽.
	const FVector Forward = FRotator(0.0f, ArcadeYaw, 0.0f).Vector();

	// 가만히 떠 있은 시간. 오른쪽이든 W 든 하나라도 누르면 0 으로 되돌린다 —
	// 안 그러면 잠깐 눌렀다 뗐을 때 이미 빠르게 떨어지는 상태에서 이어진다.
	HoverFallSeconds = (!bThrust && !bForward) ? HoverFallSeconds + DeltaTime : 0.0f;

	FVector Want = FVector::ZeroVector;
	if (bForward)
		Want += Forward * ArcadeSpeed;
	if (bThrust)
	{
		Want.Z += ArcadeClimbSpeed;
		LastSinkSpeed = -ArcadeClimbSpeed;
	}
	else if (bForward)
	{
		LastSinkSpeed = ArcadeSinkSpeed; // W 로 나아가는 동안은 전처럼 일정하게 조금씩 내려간다
		// 바닥이 SkimHeightCm 보다 가까우면 가라앉지 않고 그 높이로 떠오른다(헤더 주석). 오르막 바닥도 따라 오른다.
		float GroundHeight = 0.0f;
		IsAirborne(GroundHeight);
		if (SkimHeightCm > 0.0f && GroundHeight < SkimHeightCm)
			LastSinkSpeed = -FMath::Min((SkimHeightCm - GroundHeight) * 3.0f, ArcadeClimbSpeed);
		Want.Z -= LastSinkSpeed;
	}
	else
	{
		// 가만히 떠 있으면 중력처럼 점점 빨라진다. 시작은 전과 같은 350cm/s 라 "확 떨어지지" 않고,
		// 버티는 시간이 길어질수록 빨라진다(사용자 9/20). 상한을 두는 이유는 헤더에 적었다.
		LastSinkSpeed = FMath::Min(ArcadeSinkSpeed + HoverFallAccel * HoverFallSeconds, HoverFallMaxSpeed);
		Want.Z -= LastSinkSpeed;
	}
	// 격납고 입구 도움: 입구 가까이에서는 갑판 높이로 부드럽게 끌어 준다.
	// 배 껍데기는 충돌이 없어서 옆구리로 들어가면 아무 데나 통과해 버리고, 진짜 입구는 뒤쪽 한 곳뿐이다.
	//
	// **들어가려고 몰고 있을 때만** 돕는다(9/21 수정). 전에는 조건 없이 돌았고, 그래서 입력을 놓고
	//   가만히 있어도 이 보조가 Want.Z 를 갑판 높이로 끌어당겨 차가 공중에 멈춰 섰다
	//   (사용자 9/21: "왜 가만히 있는데도 안 내려가냐"). 게다가 그 높이가 안정점이라 — 내려가면 위로,
	//   올라가면 아래로 당긴다 — 스스로 빠져나올 수가 없었다. 가속 추락은 정상 동작하고 있었고
	//   이 보조가 덮어쓴 것이었다. 보조는 좁은 입구로 **날아 들어가는 것**을 돕자고 넣은 것이라 조작 중에만 필요하다.
	bDockAssisting = false;
	if (bThrust || bForward)
	{
		const APGFinaleDirector* Director = APGFinaleDirector::Get(GetWorld());
		if (const APGBattleshipActor* Ship = Director ? Director->GetShip() : nullptr;
			IsValid(Ship) && Ship->HasArrived() && Ship->bBuildCodeDeck) // 코드 갑판이 꺼져 있으면(바닥을 블루프린트로 깐 배) 안 돕는다 — 끌어당기는 높이가 예전 코드 갑판이라 허공으로 잡아당겼다(9/22 로그 dock=1, "보이지 않는 것에 막혀 떨어진다")
		{
			const FVector Entrance = Ship->GetHangarEntranceWorld();
			const float Distance = FVector::Dist(Owner->GetActorLocation(), Entrance);
			if (Distance < DockAssistRange)
			{
				const float Pull = (1.0f - Distance / DockAssistRange) * DockAssistStrength;
				const float WantZ = (Entrance.Z - Owner->GetActorLocation().Z) * Pull;
				Want.Z = FMath::Lerp(Want.Z, WantZ, FMath::Clamp(Pull, 0.0f, 0.8f));
				bDockAssisting = true;
			}
		}
	}
	ArcadeVelocity = FMath::VInterpTo(ArcadeVelocity, Want, DeltaTime, ArcadeSmooth);

	// 이동: 막히면 거기서 멈춘다(전함 갑판·벽에 박히지 않게 쓸어 간다).
	const FVector Delta = ArcadeVelocity * DeltaTime;
	// 기울기: 가는 쪽으로 살짝 코를 숙이고, 도는 쪽으로 기운다.
	// 부호: 9/21 사용자 "왼쪽으로 가는데 오른쪽으로 기운다" — 예전 -18 은 반대였다. 도는 쪽 날개가 내려가게 +.
	const float Bank = FMath::Clamp(YawInput * 18.0f, -18.0f, 18.0f);
	const float Pitch = FMath::Clamp(-ArcadeVelocity.Z * 0.004f, -12.0f, 12.0f);
	Owner->SetActorRotation(FRotator(Pitch, ArcadeYaw, Bank));
	FHitResult Hit;
	Owner->AddActorWorldOffset(Delta, true, &Hit);
	if (Hit.bBlockingHit)
		LastArcadeBlocker = FString::Printf(TEXT("%s/%s%s"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), Hit.bStartPenetrating ? TEXT(" (started inside)") : TEXT(""));
	// 이륙 순간 바퀴가 땅에 살짝 묻힌 채 시작하면 쓸기가 "처음부터 겹침" 으로 막혀 제자리에 붙었다
	//   (9/28 시험: 네 번 중 한 번 서버에서 속도 0, 막은 것 = 땅). 오르려는 중이면 위로만 빼 준다 — 겹침에서 나오면 다음 프레임부터 정상.
	// 9/28 두 번째: 묻힌 게 아니라 "땅에 딱 닿은 채" 시작해도 막혔다(막은 것 = 땅, 속도 0) — 모는 사람 기준 차는 이륙 직전 자리가
	//   땅에 딱 붙어 오는 일이 잦다. 오르려는 중에 바닥(위를 보는 면)에 막히면 똑같이 위로 먼저 뺀다.
	if (Hit.bBlockingHit && (Hit.bStartPenetrating || Hit.ImpactNormal.Z > 0.7f) && (bThrust || Delta.Z > 0.0f))
	{
		Owner->AddActorWorldOffset(FVector(0.0f, 0.0f, FMath::Max(Delta.Z, 30.0f)), false);
		return;
	}
	// 부딪힌 것이 부술 만한 물건이면 부순다(땅에서 달리는 차와 같게).
	const bool bBroke = SmashProps(Hit);
	if (Hit.bBlockingHit)
		// 부딪히면 속도를 죽인다(튕겨 날아가지 않게). 단, 부수고 지나갔는데 차가 멈춰 서면 이상하다.
		ArcadeVelocity *= bBroke ? 0.85f : 0.3f;

	// 착지: 땅에 가까운데 내려가는 중이면 물리를 돌려준다.
	float Height = 0.0f;
	const bool bAir = IsAirborne(Height);
	if (!bAir && ArcadeVelocity.Z <= 0.0f && !bThrust && Owner->HasAuthority())
	{
		ExitArcade();
		DockToShipIfLanded(); // 전함 갑판 위였으면 배에 붙는다
	}
}

void UPGFlightKitComponent::ApplyFlightForces(float DeltaTime)
{
	USkeletalMeshComponent* Car = GetCarMesh();
	if (!Car || !Car->IsSimulatingPhysics())
		return;
	float Height = 0.0f;
	IsAirborne(Height);
	// 위쪽 가속: 오른쪽 버튼이면 오르고(LiftG), W 만이면 중력보다 살짝 약해(GlideG) 천천히 내려간다.
	// 높이가 MaxAltitude 에 가까워질수록 오르는 힘을 중력만큼으로 줄인다(그 높이에서 떠 있기).
	const float Ceiling = FMath::Clamp((MaxAltitude - Height) / 800.0f, 0.0f, 1.0f);
	const float WantG = bThrust ? (1.0f + (LiftG - 1.0f) * Ceiling) : GlideG;
	const float Lift = 980.0f * WantG;
	// 앞으로 가는 힘은 W 를 누를 때만. 오른쪽만 누르면 제자리에서 수직으로 오른다(전함 차고로 올라갈 때 필요).
	// 방향은 "차 메시가 보는 쪽"이 아니라 "플레이어가 보는 쪽"이다 — 차 리그의 앞축이 +X 가 아닐 수 있고,
	// 공중에서 차체가 기울면 2D 로 눌렀을 때 거의 0 이 되어 아예 안 나갔다(9/20 PIE: "W 눌러도 앞으로 안 가진다").
	// 보는 쪽으로 나는 편이 조작도 쉽다(사용자: "공중 자동차는 조작 쉽게").
	FVector Forward = FVector(Car->GetForwardVector().X, Car->GetForwardVector().Y, 0.0f).GetSafeNormal();
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
		if (const APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
			Forward = FRotator(0.0f, PC->GetControlRotation().Yaw, 0.0f).Vector();
	if (Forward.IsNearlyZero())
		Forward = FVector::ForwardVector;
	const FVector Push = bForward ? Forward * ForwardAccel : FVector::ZeroVector;
	// 떨어지는 중에 오른쪽을 누르면 빨리 회복해야 한다. 내려가는 속도에 비례해 위로 더 민다
	// (안 그러면 중력만 겨우 상쇄해서 "떨어지던 속도 그대로" 계속 내려간다).
	float Recover = 0.0f;
	if (bThrust)
	{
		const float DownSpeed = FMath::Max(0.0f, -Car->GetPhysicsLinearVelocity().Z);
		Recover = FMath::Min(DownSpeed * 2.5f, 4000.0f);
	}
	// 멀리 나가 한동안 조용하면 Chaos 가 이 몸을 재운다. 자는 몸에는 힘을 줘도 아무 일이 없다
	// (9/20 PIE: "일정 범위 밖으로 나가면 컨트롤이 안 된다"). 힘을 주기 전에 깨운다.
	Car->WakeAllRigidBodies();
	Car->AddForce(FVector(0.0f, 0.0f, Lift + Recover) + Push, NAME_None, true);

	// 너무 빨리 솟지 않게 위쪽 속도 제한.
	FVector Velocity = Car->GetPhysicsLinearVelocity();
	if (Velocity.Z > 1500.0f)
	{
		Velocity.Z = 1500.0f;
		Car->SetPhysicsLinearVelocity(Velocity);
	}
}

// 날아가다 부딪힌 소품을 부순다.
//
// 왜 따로 필요한가: 땅에서 달릴 때는 APGVehiclePawn::KnockAhead 가 차 앞을 스윕해 부순다. 그런데 비행은
//   물리를 끄고 코드로 옮기는 아케이드 방식이라(EnterArcade) 그 경로를 아예 안 탄다. 그래서 날아서 들이받으면
//   속도만 줄고 맞은 물건은 멀쩡했다(사용자 9/20: "다른 차량들처럼 부술 수 있게").
// 왜 두 갈래인가:
//   1) 막는 것(드럼통·벽 판자·컨테이너)은 이동 스윕이 그대로 잡는다. 그 Hit 을 손대지 않고 그대로 넘긴다 —
//      HISM 인스턴스 번호(Hit.Item)가 채워져 있어야 한다. 비우면 맵 어디에 있든 0번 인스턴스가 뜯긴다.
//   2) 나무는 차·폰과 아예 안 부딪히게 돼 있어서(PGPhysicsUtil::IsTreeMesh) 스윕에 걸리지 않는다.
//      그래서 차 몸 크기만 한 구를 겹쳐 보고 그 안에 든 것을 친다(드래곤 KnockAround 와 같은 방식).
// 한도 550cm 는 땅에서 달리는 차와 같은 값이다 — 사용자 요구가 "다른 차량들처럼" 이므로 같아야 한다.
//   12m 컨테이너와 건물은 그대로 남고, 지면은 PGTerrain 태그가 붙어 있어 TryKnockProp 이 알아서 거부한다
//   (로봇이 지나간 자리마다 바닥이 꺼지던 사고 때문에 일부러 넣은 방어다 — 건드리지 않는다).
bool UPGFlightKitComponent::SmashProps(const FHitResult& BlockingHit)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(Owner) || !IsValid(World))
		return false;
	const float Speed = static_cast<float>(ArcadeVelocity.Size());

	// 몬스터를 먼저 본다. 속도 검사보다 **앞**이어야 한다.
	//
	// 왜(9/20 진단): 비행 중 차는 물리를 끈 몸을 스윕으로 옮기는데, 차 메시(Vehicle 프로필)와 몬스터
	//   캡슐(Pawn 프로필)이 서로 Block 이다. 그런데 소품 부수기(TryKnockProp)는 폰이 가진 컴포넌트를
	//   거부하므로 몬스터를 치면 늘 "못 부숨" 이 되고, 부르는 쪽이 속도를 매 프레임 0.3배로 깎는다.
	//   그래서 한 번 몹에 닿으면 속도가 200cm/s 를 다시 넘길 수가 없어 차가 그 자리에 박힌다.
	//   사용자가 말한 "몹들이 자꾸 달라붙는다" 가 이것이다 — 몹이 붙는 게 아니라 차가 몹에 걸리는 것이다.
	//   (몬스터를 차에 붙이는 코드는 프로젝트 어디에도 없다. 어태치가 아니라 충돌 문제다.)
	// 그래서 속도가 느려도 여기까지는 와야 한다. 안 그러면 위 악순환을 빠져나갈 길이 없다.
	if (AActor* Monster = BlockingHit.GetActor(); PGCreature::IsAliveCreature(Monster))
	{
		// 1.2m 이하만 날아간다. 5배 크리처·8배 보스는 차보다 크고 무거우니 진짜로 막히는 게 맞다.
		// 기준값은 땅에서 달리는 차(APGVehiclePawn::RamMonster)와 같게 맞췄다.
		const bool bSmall = PGCreature::GetBodyRadius(Monster) <= 120.0f;
		const float Now = GetWorld()->GetTimeSeconds();
		float& LastTime = LastRamTime.FindOrAdd(TWeakObjectPtr<AActor>(Monster), -1000.0f);
		// 큰 몹은 "닿아 있는 동안" 을 한 번의 충돌로 본다. 마지막으로 닿은 뒤 1초 넘게 떨어졌다 다시 부딪혀야 새 충돌.
		// 왜: 차가 로봇에 걸려 멈춰 있는 동안 0.5초마다 새로 들이받은 것으로 쳐서 로봇이 계속 깎였다
		//   (9/22 PIE: 30초 동안 7씩 60번, 500 → 78). 속도도 실제로 멈춘 속도가 아니라 조종 입력으로 다시 채워지는 값이었다.
		float& LastTouch = LastContactTime.FindOrAdd(TWeakObjectPtr<AActor>(Monster), -1000.0f);
		const bool bStillTouching = !bSmall && Now - LastTouch < 1.0f;
		LastTouch = Now;
		if (!bSmall)
		{
			// 막힌 쪽으로 가는 속도는 버리고 표면을 따라 미끄러지게 + 살짝 밀어낸다. 안 그러면 공중에서 로봇에 박혀 멈춘다.
			const FVector Normal = BlockingHit.ImpactNormal.GetSafeNormal();
			if (!Normal.IsNearlyZero())
				ArcadeVelocity = FVector::VectorPlaneProject(ArcadeVelocity, Normal) + Normal * 300.0f;
		}
		if (!bStillTouching && Now - LastTime >= 0.5f)
		{
			LastTime = Now;
			const FVector Push = Speed > 1.0f ? ArcadeVelocity.GetSafeNormal() : Owner->GetActorForwardVector();
			if (bSmall)
				PGCreature::Launch(Monster, Push * FMath::Max(Speed * 0.9f, 700.0f)
					+ FVector(0.0f, 0.0f, FMath::Clamp(Speed * 0.35f, 250.0f, 700.0f)));
			// 피해도 땅에서 달리는 차와 같은 식(속도 × 0.01, 큰 몹은 절반).
			const float Damage = FMath::Max(Speed * 0.01f, 5.0f);
			APawn* Driver = Cast<APawn>(Owner);
			UGameplayStatics::ApplyDamage(Monster, bSmall ? Damage : Damage * 0.5f,
				Driver ? Driver->GetController() : nullptr, Owner, UDamageType::StaticClass());
			UE_LOG(LogPGObjects, Display, TEXT("PGFlight: shoved %s off (speed=%.0f damage=%.0f pushed=%s)"),
				*Monster->GetName(), Speed, bSmall ? Damage : Damage * 0.5f, bSmall ? TEXT("yes") : TEXT("no"));
		}
		// 죽었거나 사라진 항목 정리
		for (auto It = LastRamTime.CreateIterator(); It; ++It)
			if (!It.Key().IsValid())
				It.RemoveCurrent();
		for (auto It = LastContactTime.CreateIterator(); It; ++It)
			if (!It.Key().IsValid())
				It.RemoveCurrent();
		// 작은 몹은 밀어내고 그대로 지나간다(속도를 죽이지 않는다 = 걸리지 않는다).
		// 큰 크리처는 벽과 같다 — 여기서 false 를 돌려주면 부르는 쪽이 속도를 0.3배로 죽인다.
		return bSmall;
	}

	// 살살 붙이는 정도로는 소품을 안 부순다. 멈춰 떠 있는데 옆 물건이 터지면 이상하다.
	if (Speed < 200.0f)
		return false;
	// 충격량은 땅에서 달리는 차와 같은 식(APGVehiclePawn::KnockAhead): 속도에 비례하고 위로도 조금 띄운다.
	// 느리게 스치면 살짝, ArcadeSpeed(3600)로 박으면 크게.
	const FVector Direction = ArcadeVelocity.GetSafeNormal();
	const FVector Impulse = Direction * (Speed * 1.2f + 80.0f) + FVector(0.0f, 0.0f, FMath::Min(Speed * 0.5f, 450.0f));

	bool bBroke = false;
	if (BlockingHit.bBlockingHit)
		bBroke = PGPhysicsUtil::TryKnockProp(BlockingHit.GetComponent(), BlockingHit, Impulse, Owner, 550.0f);

	// 안 막는 것(나무)까지 잡으려면 겹침을 따로 본다. 매 프레임 볼 이유는 없다.
	if (World->TimeSince(LastKnockTime) < 0.1)
		return bBroke;
	LastKnockTime = World->GetTimeSeconds();
	// 차 몸 크기만큼만 본다. 넓게 잡으면 옆을 스쳐 지나가기만 해도 터진다.
	FVector Origin = FVector::ZeroVector;
	FVector Extent = FVector::ZeroVector;
	Owner->GetActorBounds(true, Origin, Extent);
	const float Reach = FMath::Clamp(static_cast<float>(Extent.Size2D()), 150.0f, 350.0f);
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFlightSmash), false, Owner);
	World->OverlapMultiByObjectType(Overlaps, Origin, FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(Reach), Params);
	// 한 프레임에 실제로 뜯기는 개수는 PG.Knock.PerFrame(기본 4)이 막고 있다. 혼자 다 쓰지 않게 둘까지만.
	int32 Budget = 2;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Budget <= 0)
			break;
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!IsValid(Component) || Component == BlockingHit.GetComponent())
			continue;
		FHitResult Hit;
		Hit.Item = Overlap.ItemIndex; // 안 채우면 맵 어디에 있든 0번 인스턴스가 뜯긴다(9/20 조사)
		Hit.ImpactPoint = Component->GetComponentLocation();
		Hit.Location = Hit.ImpactPoint;
		Hit.Component = Component;
		Hit.HitObjectHandle = FActorInstanceHandle(Component->GetOwner());
		if (PGPhysicsUtil::TryKnockProp(Component, Hit, Impulse, Owner, 550.0f))
		{
			--Budget;
			bBroke = true;
			UE_LOG(LogPGObjects, Verbose, TEXT("PGFlight: smashed %s (%s) at %.0f km/h"),
				*GetNameSafe(Component->GetOwner()), *Component->GetName(), Speed * 0.036f);
		}
	}
	return bBroke;
}

// ---- 좌클릭 빔 ----
//
// 왜 넣었나: 사용자 요구(9/20) "몹들 자꾸 달라붙는 게 짜증나서". 그래서 이 공격의 목적은 잡는 것이 아니라
//   떼어내는 것이다. 피해와 밀어내기를 같이 준다 — 피해만 주면 몹이 그 자리에 붙어 있어서 떼어낸 느낌이 없다.
// 왜 전용 발사체를 안 만드나: 전함 주포(APGBattleshipActor::FireCannon)와 탱크 주포가 이미 같은 방식을 쓴다.
//   가발 광선 메시(SM_WigBeam: 피벗에서 +X 로 100cm, X 배율 = 거리/100)를 잠깐 보였다 끄고, 맞히는 건 선 검사다.
//   새로 만들 것이 없고, 차가 여고생이 변신한 것이라 "앞으로 쏘는 광선"이 세계관에도 맞는다.
// 좌클릭이 겹치지 않는 이유는 입력을 읽는 자리에 적어 두었다(TickComponent).

void UPGFlightKitComponent::EnsureBeam()
{
	if (IsValid(BeamMesh))
		return;
	USkeletalMeshComponent* Car = GetCarMesh();
	UStaticMesh* Mesh = BeamMeshAsset.IsNull() ? nullptr : BeamMeshAsset.LoadSynchronous();
	if (!Car || !Mesh)
		return;
	BeamMesh = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("FlightBeam"));
	BeamMesh->SetStaticMesh(Mesh);
	BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BeamMesh->SetCastShadow(false);
	BeamMesh->SetCanEverAffectNavigation(false);
	// 차에 붙여 둔다. 0.12초 동안 차가 움직여도 빔이 제자리에 남지 않는다.
	BeamMesh->SetupAttachment(Car);
	BeamMesh->RegisterComponent();
	BeamMesh->SetVisibility(false);
}

void UPGFlightKitComponent::ServerFireBeam_Implementation(FVector AimPoint)
{
	FireBeam(AimPoint);
}

void UPGFlightKitComponent::FireBeam(const FVector& AimPoint)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	USkeletalMeshComponent* Car = GetCarMesh();
	if (!IsValid(Pawn) || !IsValid(World) || !Car || World->GetTimeSeconds() - LastFireTime < FireInterval * 0.8f) // 20% 여유(멀티 9/27, 탱크와 같은 이유)
		return;
	LastFireTime = World->GetTimeSeconds();
	// 판 기록(사격 수). 플레이어가 모는 날으는 차일 때만 센다.
	UPGRunSubsystem::NotifyShotFired(Pawn);
	EnsureBeam();

	// 포구는 차 앞 끝 바깥. 차체 안에서 쏘면 제 몸에 맞는다.
	const FVector Forward = Pawn->GetActorForwardVector();
	const float HalfLength = static_cast<float>(Car->CalcLocalBounds().BoxExtent.X);
	const FVector Muzzle = Pawn->GetActorLocation() + Forward * (HalfLength + 30.0f) + FVector(0.0f, 0.0f, 25.0f);
	const FVector Direction = (AimPoint - Muzzle).GetSafeNormal();

	// 선이 아니라 반지름 40cm 구를 쓸어 보낸다. 차에 달라붙어 비비는 몹은 몸이 화면 가운데에서
	// 조금씩 벗어나 있어서, 선 한 줄로는 계속 빗나간다(떼어내는 게 목적인데 안 맞으면 의미가 없다).
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFlightBeam), false, Pawn);
	const bool bHit = World->SweepSingleByChannel(Hit, Muzzle, Muzzle + Direction * BeamRange, FQuat::Identity,
		ECC_Visibility, FCollisionShape::MakeSphere(40.0f), Params);
	const FVector Impact = bHit ? Hit.ImpactPoint : Muzzle + Direction * BeamRange;

	if (AActor* Monster = Hit.GetActor(); PGCreature::IsAliveCreature(Monster))
	{
		// 미는 방향은 "빔이 나간 쪽"이 아니라 "차에서 멀어지는 쪽"이다. 떼어내는 것이 목적이라
		// 차를 기준으로 밀어야 한다 — 빔 방향으로 밀면 비스듬히 맞았을 때 차 옆으로 돌아 들어온다.
		const FVector Away = (Monster->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D();
		// 큰 몬스터(크리처·보스)는 차보다 무거워 안 날아간다. 들이받기(APGVehiclePawn::RamMonster)와 같은 기준 1.2m.
		const bool bSmall = PGCreature::GetBodyRadius(Monster) <= 120.0f;
		if (bSmall)
			PGCreature::Launch(Monster, Away * BeamPush + FVector(0.0f, 0.0f, 400.0f));
		// 피해는 "몹 최대 체력의 1/BeamHitsToKill" — 몇 발에 잡히는지가 몹 크기와 상관없이 같다.
		// 왜(9/22 사용자: "몇 대를 때려야 해. 보스가 죽지가 않잖아. 8 대면 잡을 수 있게"): 전에는 고정 피해(큰 몹은 절반)라
		//   체력이 수십 배인 보스 로봇은 수십 발을 맞아야 했다. 포트폴리오 시연용으로 한 판이 끝까지 가야 한다.
		//   0.1% 를 더 얹는 이유: 나눗셈 오차로 여덟 번째에 체력 0.0001 이 남아 안 죽는 일을 막는다.
		const float Damage = BeamHitsToKill > 0 ? PGCreature::GetMaxHealth(Monster) / BeamHitsToKill * 1.001f : (bSmall ? BeamDamage : BeamDamage * 0.5f);
		UGameplayStatics::ApplyDamage(Monster, Damage, Pawn->GetController(), Pawn, UDamageType::StaticClass());
		UE_LOG(LogPGObjects, Display, TEXT("PGFlight: beam hit %s (damage=%.0f = max hp %.0f / %d, hp left %.0f, pushed=%s)"),
			*Monster->GetName(), Damage, PGCreature::GetMaxHealth(Monster), BeamHitsToKill, PGCreature::GetHealth(Monster), bSmall ? TEXT("yes") : TEXT("no"));
	}
	else if (bHit)
	{
		// 몹이 아니면 소품이라도 부순다. 날아가며 부수는 것과 같은 경로라 한도(550cm)도 같다.
		PGPhysicsUtil::TryKnockProp(Hit.GetComponent(), Hit, Direction * 900.0f + FVector(0.0f, 0.0f, 300.0f), Pawn, 550.0f);
	}

	// 빔 그림은 모두의 화면에(멀티 9/27 — 서버에서만 켜서 아무도 못 봤다).
	MulticastBeamFx(Muzzle, Impact);
}

// 화면 가운데가 실제로 닿는 곳. 조준점과 빔이 서로 다른 데를 보면 조준점이 거짓말이 되므로 한 곳에서 계산한다.
FVector UPGFlightKitComponent::ComputeAimPoint(const APlayerController* PC) const
{
	const UWorld* World = GetWorld();
	if (!IsValid(PC) || !IsValid(World))
		return FVector::ZeroVector;
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector ViewEnd = ViewLocation + ViewRotation.Vector() * BeamRange;
	FHitResult ViewHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFlightAim), false, GetOwner());
	return World->LineTraceSingleByChannel(ViewHit, ViewLocation, ViewEnd, ECC_Visibility, Params)
		? ViewHit.ImpactPoint : ViewEnd;
}


// 화면 가운데 고정 십자 조준선.
//
// 왜 화면에 그리나: 처음에는 빔이 닿는 지점에 3D 구를 그렸는데, 깊이 검사를 받는 물체라
//   벽을 겨누면 표면에 반쯤 파묻혀 보였다(9/20 사용자: "조준선은 왜 벽에 파묻히냐"). 화면에 그리면
//   깊이와 무관하게 늘 같은 자리·같은 크기로 보인다. 애초에 필요한 것도 "어디를 겨누나" 하나뿐이었다.
// 왜 디버그 그리기 서비스인가: 이건 컴포넌트라 AHUD::DrawHUD 를 쓸 수 없다. 진짜 조준 UI 는
//   팀원이 만드는 중이므로 HUD 클래스를 새로 만들지 않고 임시로 캔버스에 직접 그린다.
// 차를 몰고 있는 사람 화면에만 나온다 — 걸어 다닐 때 떠 있으면 팀원이 진짜 조준점을 붙일 때 겹친다.
void UPGFlightKitComponent::DrawCrosshair(UCanvas* Canvas, APlayerController* PC)
{
	if (!bShowAimMarker || !IsValid(Canvas))
		return;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	// 이 차를 지금 직접 몰고 있을 때만. 남의 화면이나 걸어 다닐 때는 안 그린다.
	if (!IsValid(Pawn) || !Pawn->IsLocallyControlled() || !Pawn->GetController())
		return;

	const float CenterX = Canvas->SizeX * 0.5f;
	const float CenterY = Canvas->SizeY * 0.5f;
	// 화면 높이의 1.2%. 해상도가 달라져도 보이는 크기가 같다.
	const float Arm = FMath::Max(6.0f, Canvas->SizeY * 0.012f);
	const float Gap = Arm * 0.35f; // 가운데를 비운다 — 겨누는 점이 선에 가려지면 조준이 어렵다
	const FLinearColor Color(0.35f, 0.85f, 1.0f, 0.9f);
	const float Thickness = 2.0f;
	Canvas->K2_DrawLine(FVector2D(CenterX - Arm, CenterY), FVector2D(CenterX - Gap, CenterY), Thickness, Color);
	Canvas->K2_DrawLine(FVector2D(CenterX + Gap, CenterY), FVector2D(CenterX + Arm, CenterY), Thickness, Color);
	Canvas->K2_DrawLine(FVector2D(CenterX, CenterY - Arm), FVector2D(CenterX, CenterY - Gap), Thickness, Color);
	Canvas->K2_DrawLine(FVector2D(CenterX, CenterY + Gap), FVector2D(CenterX, CenterY + Arm), Thickness, Color);
}
