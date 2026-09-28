#include "Robot/PGRobotCharacter.h"
#include "Robot/PGRobotRole.h"
#include "Interaction/PGRideHelpers.h"
#include "Vehicle/PGTankPawn.h"
#include "Common/PGCameraUtil.h"
#include "Finale/PGFinaleDirector.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Common/PGKeyPolling.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGObjectTypes.h"
#include "Objects/PGVehicleComponents.h"
#include "Common/PGPhysicsUtil.h"

// 공격 판정 그리기(로봇 스윕 캡슐, 무기 트레이스 선). 기본 꺼짐. 콘솔 PG.DebugCombat 1 로 켠다.
TAutoConsoleVariable<int32> CVarPGDebugCombat(TEXT("PG.DebugCombat"), 0, TEXT("Draw attack sweeps and weapon traces (robot capsule, weapon line)."));

APGRobotCharacter::APGRobotCharacter()
{
	// 보스인지 탑승용인지는 BeginPlay 에서 정하므로 자동 빙의는 끈다.
	AutoPossessAI = EAutoPossessAI::Disabled;
	// 멀티(9/27): 17m 보스가 150m(기본 복제 거리) 밖에서 사라졌다 나타나지 않게.
	SetNetCullDistanceSquared(FMath::Square(90000.0f));

	MaxHealth = 500.0f;
	AttackDamage = 40.0f;
	AttackRange = 260.0f;
	AttackInterval = 1.0f;
	AttackHitDelay = 0.35f;
	LootTableId = TEXT("LT_CorpseC");

	GetCharacterMovement()->MaxWalkSpeed = 420.0f;
	// 밟고 있던 물체(차량·날아가는 소품 대리 액터)의 속도를 떠날 때 물려받지 않는다.
	// 8배 보스가 물리로 흔들리는 차 위에 올라섰다 내려오면 그 순간의 튀는 속도를 그대로 받아 하늘로 날아갈 수 있다.
	GetCharacterMovement()->bImpartBaseVelocityX = false;
	GetCharacterMovement()->bImpartBaseVelocityY = false;
	GetCharacterMovement()->bImpartBaseVelocityZ = false;
	GetCharacterMovement()->bImpartBaseAngularVelocity = false;

	Seat = CreateDefaultSubobject<UPGSeatComponent>(TEXT("DriverSeat"));
	Seat->SetupAttachment(RootComponent);

	// 탑승 시 3인칭. 로봇 뒤 위에서 본다.
	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(RootComponent);
	CameraArm->TargetArmLength = 520.0f;
	CameraArm->SocketOffset = FVector(0.0f, 0.0f, 160.0f);
	CameraArm->bUsePawnControlRotation = true;
	// 카메라가 벽·지붕·날아가는 잔해에 닿을 때마다 로봇 쪽으로 확 당겨져서(스프링암 충돌 검사) 시야가 계속 줌인·줌아웃됐다.
	// 탱크·차와 같이 충돌 검사를 끄고, 살짝 늦게 따라오게 해 흔들림을 줄인다. 17m 로봇이라 벽에 카메라가 묻히는 일은 드물다.
	CameraArm->bDoCollisionTest = false;
	CameraArm->bEnableCameraLag = true;
	CameraArm->CameraLagSpeed = 8.0f;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	// 스켈레톤 본 이름이 마네킹과 같아(hand_r, pelvis, spine_01) 마네킹 기준 오프셋을 쓴다.
	const FString Pack = TEXT("/Game/SciFi_ToiletMech");
	const FString A = Pack + TEXT("/Animation/Anim_SciFi_ToiletMech_");
	auto Anim = [](const FString& Path) { return TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Path)); };
	FPGMonsterVisuals V;
	V.Mesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Pack + TEXT("/Mesh/SK_SciFi_ToiletMech_Skin1.SK_SciFi_ToiletMech_Skin1")));
	V.Idle = Anim(A + TEXT("Idle2.Anim_SciFi_ToiletMech_Idle2"));
	V.Run = Anim(A + TEXT("Run.Anim_SciFi_ToiletMech_Run"));
	V.Attack = Anim(A + TEXT("Attack1.Anim_SciFi_ToiletMech_Attack1"));
	V.Hit = Anim(A + TEXT("GetHit_F.Anim_SciFi_ToiletMech_GetHit_F"));
	V.Die = Anim(A + TEXT("Death1.Anim_SciFi_ToiletMech_Death1"));
	V.AttackVariants = {
		Anim(A + TEXT("Attack1.Anim_SciFi_ToiletMech_Attack1")),
		Anim(A + TEXT("Attack2.Anim_SciFi_ToiletMech_Attack2")),
		Anim(A + TEXT("Attack3.Anim_SciFi_ToiletMech_Attack3")),
		Anim(A + TEXT("Attack4.Anim_SciFi_ToiletMech_Attack4")),
		Anim(A + TEXT("Attack5.Anim_SciFi_ToiletMech_Attack5")) };
	// 보스는 변기 모양으로 앉아 있다가(Idle1_Toilet) 가까이 오면 변신(TransitionIdle1ToIdle3)하고 쫓아온다.
	V.DormantIdle = Anim(A + TEXT("Idle1_Toilet.Anim_SciFi_ToiletMech_Idle1_Toilet"));
	V.Wake = Anim(A + TEXT("TransitionIdle1ToIdle3.Anim_SciFi_ToiletMech_TransitionIdle1ToIdle3"));
	// 캡슐이 내비 에이전트보다 크면 벽 옆 경로에서 몸이 걸린다. 다리가 가늘어 45면 충분하다.
	V.CapsuleRadius = 45.0f;
	V.CapsuleHalfHeight = 110.0f;
	V.MeshOffset = FVector(0.0f, 0.0f, -110.0f);
	V.MeshRotation = FRotator(0.0f, -90.0f, 0.0f);
	Visuals = V;
	// 탑승용 스킨 칸 기본값(9/23 블루프린트 분리 전 ConfigureAsRideable 에 적혀 있던 노란 Skin3).
	RideableSkinMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Pack + TEXT("/Mesh/SK_SciFi_ToiletMech_Skin3.SK_SciFi_ToiletMech_Skin3")));
}

// 역할을 고르는 유일한 곳. bRideable 은 레벨·BP 에서 고치는 설정 칸이고, 그 값에 맞는 역할(상태 없는 기본 인스턴스)을 돌려준다.
const UPGRobotRole& APGRobotCharacter::GetRole() const
{
	return bRideable ? static_cast<const UPGRobotRole&>(*GetDefault<UPGRobotRideRole>())
		: static_cast<const UPGRobotRole&>(*GetDefault<UPGRobotBossRole>());
}

float APGRobotCharacter::GetKnockRatio() const
{
	return GetRole().GetKnockRatio(*this);
}

bool APGRobotCharacter::CanKnockProps() const
{
	return GetRole().CanKnockProps(*this);
}

void APGRobotCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGRobotCharacter, RiderPawn);
	// 멀티(9/27): 스포너가 세우기 전에 정하는 역할·크기(ConfigureAs*). 복제하지 않으면 클라는 기본값(탈것, 배율 1)으로
	//   "탈것 역할" 을 돌려, 보스가 1배 크기로 땅에 박힌 채 "로봇 탑승" 이 뜨고 탈것 로봇은 스킨·크기가 틀렸다.
	//   세우기 전에 정해지므로 처음 한 번이면 클라 BeginPlay(역할 갖추기) 때 이미 와 있다.
	DOREPLIFETIME_CONDITION(APGRobotCharacter, bRideable, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(APGRobotCharacter, BossScale, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(APGRobotCharacter, RideScale, COND_InitialOnly);
}

void APGRobotCharacter::ConfigureAsBoss(float Scale)
{
	bRideable = false;
	BossScale = FMath::Clamp(Scale, 1.0f, 12.0f);
	MaxHealth = 800.0f * BossScale / 2.5f;
	bStartDormant = true;
	WakeRange = 1500.0f;
}

void APGRobotCharacter::ConfigureAsRideable(float Scale)
{
	bRideable = true;
	RideScale = FMath::Clamp(Scale, 1.0f, 12.0f);
	// 탑승용도 변기로 잠복해 있다가 플레이어가 가까이 오면 변신한다(보스와 같은 연출). 멀어지면 다시 변기.
	bStartDormant = true;
	WakeRange = 1500.0f + 200.0f * RideScale;
	// 탑승용은 노란 스킨(Skin3). 멀리서도 보스(Skin1)와 구분되게. 스폰이 Deferred 라 BeginPlay 전에 바꿔도 반영된다.
	if (!RideableSkinMesh.IsNull())
		Visuals.Mesh = RideableSkinMesh;
}

void APGRobotCharacter::BeginPlay()
{
	// 탈것 로봇의 노란 스킨: 서버는 ConfigureAsRideable 에서 넣었다. 클라는 복제된 bRideable 로 같은 스킨을 고른다(부모 BeginPlay 가 입히기 전에).
	if (!HasAuthority() && bRideable && !RideableSkinMesh.IsNull())
		Visuals.Mesh = RideableSkinMesh;
	Super::BeginPlay();
	// 역할에 맞게 크기·걸음·카메라·AI 를 갖춘다(보스: 키우고 AI 붙임 / 탑승용: 배율만큼 키움). PGRobotRole.cpp
	GetRole().OnBeginPlay(*this);
	// 멀티(9/28): 몸 그림(메시)의 자리를 "기준" 으로 다시 기억시킨다. 엔진은 캐릭터가 처음 만들어질 때의 메시 자리를 기억해 두고
	//   구경하는 사람 화면에서 매 프레임 그 자리로 되돌린다(움직임 부드럽게 하기). 모양(SetVisuals)이 BeginPlay 에서 메시를 옮기므로
	//   옛 자리로 돌아가, 8배로 키운 로봇은 몇 m 떠 있거나 파묻혀 보였다(9/28 사용자 PIE: 변신 전후 로봇 위치가 이상하다).
	static IConsoleVariable* CacheSwitch = IConsoleManager::Get().FindConsoleVariable(TEXT("PG.NetSmooth")); // 0 = 예전처럼(비교 시험)
	if (USkeletalMeshComponent* Body = GetMesh(); Body && (!CacheSwitch || CacheSwitch->GetInt() != 0))
		CacheInitialMeshOffset(Body->GetRelativeLocation(), Body->GetRelativeRotation());
}

// ---- 탑승 ----

bool APGRobotCharacter::CanInteract_Implementation(APawn* Interactor) const
{
	// 변기 상태거나 변신 모션 중이면 아직 못 탄다. 가까이 서 있으면 곧 깨어난다.
	return GetRole().CanBeMounted() && !IsDead() && !IsRidden() && !IsDormant() && !IsBusy() && IsValid(Interactor) && Interactor != this;
}

FText APGRobotCharacter::GetInteractionPrompt_Implementation() const
{
	return NSLOCTEXT("Robot", "Mount", "로봇 탑승");
}

FText APGRobotCharacter::GetRiderPrompt() const
{
	// 걷는 중에는 비운다. 멈춰 서면 내릴 수 있다는 걸 알려 준다(F 는 언제든 된다).
	// 완전히 멈추고 0.6초 뒤에만 띄운다(걸음 사이 속도가 잠깐 0 이 될 때 깜빡이지 않게).
	return DismountPromptTimer.IsSettled(GetVelocity().Size2D(), GetWorld()->GetTimeSeconds())
		? NSLOCTEXT("Robot", "Dismount", "로봇 하차") : FText::GetEmpty();
}

void APGRobotCharacter::Interact_Implementation(APawn* Interactor)
{
	Mount(Interactor);
}

bool APGRobotCharacter::Mount(APawn* Rider)
{
	if (!HasAuthority() || !IsValid(Rider) || !CanInteract_Implementation(Rider) || !IsValid(Seat))
		return false;
	APlayerController* PC = Cast<APlayerController>(Rider->GetController());
	if (!IsValid(PC))
		return false;
	if (!Seat->TryEnter(Rider))
		return false;

	RiderPawn = Rider;

	// 숨기기·충돌 끄기·붙이기·빙의 — 차·탱크·로봇이 같이 쓰는 순서(Interaction/PGRideHelpers.h).
	PGRide::BoardRider(this, Rider, PC, GetRootComponent(), FVector(0.0f, 0.0f, 60.0f));
	GetCharacterMovement()->MaxWalkSpeed = RideWalkSpeed;
	// 탑승한 F·클릭이 아직 눌려 있다. 다음 Tick 에서 바로 내리거나 휘두르지 않게 "이미 눌린 상태"로 시작한다.
	bDismountKeyWasDown = true;
	bAttackKeyWasDown = true;
	MountedTime = GetWorld()->GetTimeSeconds();

	UE_LOG(LogPGObjects, Display, TEXT("%s mounted by %s"), *GetName(), *GetNameSafe(Rider));
	return true;
}

bool APGRobotCharacter::Dismount()
{
	if (!HasAuthority() || !IsRidden() || !IsValid(Seat))
		return false;
	APlayerController* PC = Cast<APlayerController>(GetController());
	APawn* Rider = RiderPawn;
	FVector ExitLocation;
	if (!Seat->Exit(Rider, ExitLocation))
		return false;

	// 로봇은 내린 자리에서 바로 선다(빈 자리 찾기 없음, 좌석 자리 + 1m). 시야는 탑승자 것을 그대로 둔다.
	GetCharacterMovement()->StopMovementImmediately();
	PGRide::ReleaseRider(this, Rider, PC, PGRide::FindExitSpot(this, Rider, ExitLocation, 0.0f, 0.0f), /*bResetControlRotation*/ false);
	RiderPawn = nullptr;
	UE_LOG(LogPGObjects, Display, TEXT("%s dismounted by %s"), *GetName(), *GetNameSafe(Rider));
	return true;
}

void APGRobotCharacter::ServerDismount_Implementation()
{
	Dismount();
}

void APGRobotCharacter::Die(AController* Killer)
{
	if (IsDead())
		return;
	if (IsRidden())
	{
		UE_LOG(LogPGObjects, Display, TEXT("%s died while ridden by %s, dismounting first"), *GetName(), *GetNameSafe(RiderPawn));
		Dismount();
	}
	Super::Die(Killer);
	// 역할별 죽음 처리 — 보스면 피날레를 연다(PGRobotRole.cpp).
	GetRole().OnDied(*this);
}

// ---- 탑승 중 조작 ----

void APGRobotCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (IsDead())
		return;
	// 사람이 탔을 때 고개를 들어도 카메라가 땅 밑으로 안 들어가게(PGCameraUtil.h).
	if (IsRidden())
	{
		PGCameraUtil::KeepCameraAboveGround(this, CameraArm, 520.0f * RideScale * RideCameraDistanceFactor);
		GetRole().TickRidden(*this); // 탑승용: 작은 몹 밟기
	}
	// 소품 밀쳐내기는 APGMonsterCharacter::Tick 이 한다(CanKnockProps / GetKnockRatio 를 이 클래스가 덮어씀).

	// 로봇이 하늘로 날아간 현상 추적. 속도로 나는 것(launched)과 속도 없이 위치만 튀는 것(displaced = 겹침 해소로 밀려남)을 구분해 남긴다.
	if (HasAuthority())
	{
		const FVector Location = GetActorLocation();
		const float Jump = LastTickLocation.IsZero() ? 0.0f : (Location - LastTickLocation).Z;
		const bool bLaunched = GetVelocity().Z > 1500.0f;
		const bool bDisplaced = Jump > 150.0f && GetVelocity().Z < 500.0f;
		if ((bLaunched || bDisplaced) && GetWorld()->GetTimeSeconds() - LastLaunchLogTime > 1.0f)
		{
			LastLaunchLogTime = GetWorld()->GetTimeSeconds();
			const UCharacterMovementComponent* Move = GetCharacterMovement();
			UE_LOG(LogPGObjects, Warning, TEXT("%s %s: jump=%.0f vel=%s mode=%d base=%s floor=%s at %s"), *GetName(), bLaunched ? TEXT("launched") : TEXT("displaced"),
				Jump, *GetVelocity().ToCompactString(), static_cast<int32>(Move->MovementMode),
				*GetNameSafe(Move->GetMovementBase() ? Move->GetMovementBase()->GetOwner() : nullptr),
				*GetNameSafe(Move->CurrentFloor.HitResult.GetActor()), *Location.ToCompactString());
		}
		LastTickLocation = Location;
	}

	// 빈 로봇일 때 역할별 처리(탑승용: 가까이 오는 사람에 맞춰 깨고 자기 — AI 가 없어서 직접 본다).
	if (!IsRidden())
		GetRole().TickUnridden(*this);

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!IsValid(PC) || !IsLocallyControlled())
		return;
	SetActorTickInterval(0.0f); // 타는 동안은 매 프레임(입력)

	// 키를 직접 읽는 임시 입력(PGKeyPolling). 공격 모션 중엔 발을 안 뗀다.
	if (!IsAttacking())
		PGKeyPolling::ApplyWasdMovement(PC, this);
	PGKeyPolling::ApplyMouseLook(PC, this);

	// F: 하차. 탑승한 F 가 그대로 하차로 읽히지 않게 0.5초 뒤부터.
	if (PGKeyPolling::WasPressed(PC, EKeys::F, bDismountKeyWasDown) && GetWorld()->GetTimeSeconds() - MountedTime > 0.5f)
	{
		if (HasAuthority())
			Dismount();
		else
			ServerDismount();
	}
	if (PGKeyPolling::WasPressed(PC, EKeys::LeftMouseButton, bAttackKeyWasDown))
		RobotAttack();
}

void APGRobotCharacter::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved,
	FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
	// 멀티(9/28): 탄 사람 화면도 소품은 먼저 부순다(기다리면 막혀 멈칫). 몬스터 걷어차기·차 밀치기는 서버만.
	if ((!HasAuthority() && !IsLocallyControlled()) || IsDead())
		return;
	// 보스와 사람이 탄 로봇만 소품을 날린다. 빈 탑승 로봇은 서 있기만 한다.
	if (!CanKnockProps())
		return;
	// 몸에 걸린 게 작은 몬스터면 걷어찬다(스윕보다 먼저 몸이 닿은 경우).
	if (HasAuthority() && (KickSmallMonster(Other, GetActorForwardVector()) || ShoveVehicle(Other, GetActorForwardVector())))
		return;
	// 속도 변화 cm/s. 위쪽은 700 에서 자른다(APGMonsterCharacter::KnockAhead 와 같은 규칙 — 소품이 하늘로 솟구치지 않게).
	const FVector Push = GetActorForwardVector() * FMath::Min(1400.0f * GetKnockRatio(), 3000.0f) + FVector(0.0f, 0.0f, FMath::Min(600.0f * GetKnockRatio(), 700.0f));
	// 몸이 클수록 큰 것도 날린다. 8배면 컨테이너까지.
	PGPhysicsUtil::TryKnockProp(OtherComp, Hit, Push, this, 500.0f * GetKnockRatio(), 80.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f);
}

float APGRobotCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// 역할별 받는 피해 조정(보스: 사람이 탄 로봇·탱크에게 더 아프다). PGRobotRole.cpp
	DamageAmount = GetRole().ModifyIncomingDamage(*this, DamageAmount, DamageCauser);
	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

void APGRobotCharacter::RobotAttack()
{
	const FVector Direction = FRotator(0.0f, GetControlRotation().Yaw, 0.0f).Vector();
	if (HasAuthority())
		PerformRobotAttack(Direction);
	else
	{
		SetActorRotation(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f)); // 조종하는 사람 화면에서 바로 돌린다(서버 회전은 본인에게 안 온다)
		ServerRobotAttack(Direction);
	}
}

void APGRobotCharacter::MulticastRobotAttackFx_Implementation(float Yaw)
{
	if (HasAuthority())
		return; // 서버는 PerformRobotAttack 에서 이미 틀었다
	if (GetLocalRole() == ROLE_SimulatedProxy)
		SetActorRotation(FRotator(0.0f, Yaw, 0.0f));
	const float Length = PlayOnceForAll(Visuals.Attack); // 멀티(9/28): 클라도 같은 공격 동작(몬스터 임시수정의 PlayOnceForAll)
	AnimLockUntil = GetWorld()->GetTimeSeconds() + FMath::Max(Length, 0.6f); // 걷기·서기 애니가 바로 덮지 않게
}

void APGRobotCharacter::ServerRobotAttack_Implementation(FVector_NetQuantizeNormal Direction)
{
	PerformRobotAttack(Direction);
}

// 한 방의 피해는 역할이 정한다(탑승용: 작은 몹 한 방·크리처 세 대 / 그 밖: 기본 공격력). PGRobotRole.cpp
float APGRobotCharacter::RideAttackDamageFor(const APGMonsterCharacter* Target) const
{
	return GetRole().AttackDamageFor(*this, Target);
}

// 탄 로봇이 걸어가며 작은 몹을 밟으면 쓰러뜨린다(0.2초마다, 발밑 둘레). 크리처는 밟히지 않는다 — 로봇보다 크다.
void APGRobotCharacter::TrampleSmallMonsters()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastTrampleTime < 0.2f || GetVelocity().Size2D() < 150.0f)
		return;
	LastTrampleTime = Now;
	const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius() + 80.0f;
	const FVector Feet = GetActorLocation() - FVector(0.0f, 0.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 0.6f);
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRobotTrample), false, this);
	GetWorld()->OverlapMultiByChannel(Overlaps, Feet, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Radius), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		APGMonsterCharacter* Monster = Cast<APGMonsterCharacter>(Overlap.GetActor());
		if (!IsValid(Monster) || Monster == this || Monster->IsDead() || Monster->IsA<APGRobotCharacter>() || Monster->GetSimpleCollisionRadius() > 120.0f)
			continue;
		const FVector Away = (Monster->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		Monster->LaunchCharacter(Away * 900.0f + FVector(0.0f, 0.0f, 450.0f), true, true);
		UGameplayStatics::ApplyDamage(Monster, Monster->GetMaxHealth(), GetController(), this, UDamageType::StaticClass());
		UE_LOG(LogPGObjects, Display, TEXT("%s trampled %s"), *GetName(), *Monster->GetName());
	}
}

void APGRobotCharacter::PerformRobotAttack(const FVector& Direction)
{
	if (!HasAuthority() || IsDead() || IsAttacking())
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastAttackTime < AttackInterval)
		return;
	LastAttackTime = Now;

	// 몬스터 TryAttack 과 달리 표적이 없어도 휘두른다. 정면으로 굵은 스윕을 쏴서 걸린 몬스터 전부에게 피해.
	SetActorRotation(FRotator(0.0f, Direction.Rotation().Yaw, 0.0f));
	bAttacking = true;
	const float Length = PlayOnceForAll(Visuals.Attack); // 멀티(9/28): 클라도 같은 공격 동작(몬스터 임시수정의 PlayOnceForAll)
	MulticastRobotAttackFx(Direction.Rotation().Yaw);
	GetWorldTimerManager().SetTimer(AttackEndTimer, this, &APGRobotCharacter::EndAttack, FMath::Max(Length, 0.6f), false);

	const FVector Start = GetActorLocation();
	const FVector End = Start + Direction * AttackRange;
	TArray<FHitResult> Hits;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRobotAttack), false, this);
	if (IsValid(RiderPawn))
		Params.AddIgnoredActor(RiderPawn);
	GetWorld()->SweepMultiByChannel(Hits, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(AttackSweepRadius), Params);

	TSet<AActor*> Damaged;
	for (const FHitResult& Hit : Hits)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor) || Damaged.Contains(HitActor))
			continue;
		if (!HitActor->IsA<APGMonsterCharacter>())
			continue;
		Damaged.Add(HitActor);
		UGameplayStatics::ApplyDamage(HitActor, RideAttackDamageFor(Cast<APGMonsterCharacter>(HitActor)), GetController(), this, UDamageType::StaticClass());
	}
	if (CVarPGDebugCombat.GetValueOnGameThread() != 0)
		DrawDebugCapsule(GetWorld(), (Start + End) * 0.5f, AttackRange * 0.5f, AttackSweepRadius,
			FRotationMatrix::MakeFromZ(Direction).ToQuat(), Damaged.Num() > 0 ? FColor::Red : FColor::Orange, false, 0.8f);
	UE_LOG(LogPGObjects, Display, TEXT("%s robot attack hit=%d"), *GetName(), Damaged.Num());
}

void APGRobotCharacter::OnRep_RiderPawn(APawn* OldRider)
{
	PGRide::OnRiderChangedOnClient(RiderPawn, OldRider);
}

void APGRobotCharacter::NotifyControllerChanged()
{
	Super::NotifyControllerChanged();
	// 멀티(9/27): 사람이 탄 로봇의 걸음 속도는 조종하는 컴퓨터(클라)에서도 같아야 한다 — Mount 는 서버에서만 넣어서
	//   클라는 보통 속도로 예측하고 서버가 계속 되돌렸다. 서버는 타는 동안 매 프레임 돈다(0.2초 간격이면 소품을 못 치운다).
	if (IsPlayerControlled())
	{
		GetCharacterMovement()->MaxWalkSpeed = RideWalkSpeed;
		if (HasAuthority())
			SetActorTickInterval(0.0f);
	}
	// 멀티(9/27): 탑승 F 가 아직 눌려 있다. 이 컴퓨터가 조종을 넘겨받은 순간 "눌린 상태·탑승 시각" 을 새로 잡는다.
	// 전에는 Mount(서버)에서만 잡아 클라이언트는 탑승 F 를 곧바로 하차로 읽었다(차와 같은 문제, APGVehiclePawn 참고).
	if (IsLocallyControlled())
	{
		bDismountKeyWasDown = true;
		MountedTime = GetWorld()->GetTimeSeconds();
	}
}
