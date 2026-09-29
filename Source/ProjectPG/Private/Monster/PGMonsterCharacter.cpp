#include "Monster/PGMonsterCharacter.h"
#include "Common/PGSoundRouter.h"
#include "Monster/PGMonsterLookSet.h"

#include "Animation/AnimSequence.h"
#include "Common/PGPhysicsUtil.h"
#include "Flow/PGRunSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Monster/MonsterAIController.h"
#include "NavigationInvokerComponent.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGLootableComponent.h"
#include "Objects/PGObjectTypes.h"
#include "TimerManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Vehicle/PGTankPawn.h"
#include "Vehicle/PGVehiclePawn.h"

APGMonsterCharacter::APGMonsterCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	AIControllerClass = AMonsterAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	GetCharacterMovement()->MaxWalkSpeed = 350.0f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 540.0f, 0.0f);
	// 몬스터는 물리 물체를 몸으로 밀지 않는다. 주차된 차 바퀴(키네마틱 몸)에 닿으면 매 프레임 "피직스 시뮬레이션이 켜져 있어야..." 경고가
	// 메시지 로그에 쌓였다(한 판에 4,700줄). 소품은 KnockAhead 가 따로 날린다.
	GetCharacterMovement()->bEnablePhysicsInteraction = false;
	bUseControllerRotationYaw = false;

	// 무기 트레이스와 F 상호작용 모두 Visibility 채널을 쓴다. 캡슐·메시 둘 다 맞게 해 두면
	// 살아 있을 땐 캡슐에, 죽어서 캡슐을 끈 뒤엔 메시에 걸린다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	Lootable = CreateDefaultSubobject<UPGLootableComponent>(TEXT("Lootable"));

	// 내비 인보커는 플레이어에게만 둔다(반경 120m). 몬스터 40마리가 각자 40m 씩 요청하면 내비메시 재생성이 계속 돈다.
	// 플레이어 근처 몬스터는 플레이어 인보커 범위 안이라 경로를 찾고, 멀리 있는 놈은 어차피 잠복·직진이다.
	NavigationInvoker = nullptr;

	// 안 보이는 몬스터는 애니메이션 포즈 계산을 건너뛴다. 멀면 갱신 주기도 낮춘다(URO). 몬스터 수십 마리의 가장 큰 비용이 이거다.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	GetMesh()->bEnableUpdateRateOptimizations = true;

	// 생성자는 엔진이 뜨는 중에도(클래스 기본 객체) 돈다 — 에셋을 읽는 모양표 대신 코드 기본 표를 쓴다.
	FPGMonsterVisuals Preset;
	if (UPGMonsterLookSet::BuildDefaultLook(TEXT("Slime"), Preset))
		Visuals = Preset;
}

void APGMonsterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGMonsterCharacter, bDead);
	DOREPLIFETIME(APGMonsterCharacter, Health);
	DOREPLIFETIME(APGMonsterCharacter, bDormant);
	DOREPLIFETIME(APGMonsterCharacter, Faction);
	DOREPLIFETIME(APGMonsterCharacter, OnceCue); // [멀티 임시수정 2026-09-28 — 형님께 전달] 한 줄(아래 PlayOnceForAll 참고)
}

void APGMonsterCharacter::BeginPlay()
{
	Super::BeginPlay();
	HomeLocation = GetActorLocation(); // 영역(LeashRadius)의 중심. 리스폰도 원래 자리에 스폰하므로 같은 자리가 된다
	// 프리셋 손맛을 먼저 반영하고 체력을 채운다.
	if (Visuals.AttackRange > 0.0f)
		AttackRange = Visuals.AttackRange;
	// 사거리는 중심 사이 거리로 잰다. 5배 크리처는 캡슐 반지름만 4.5m 라 프리셋 사거리 3.8m 로는 영원히 못 닿아서
	// 때리지 않고 표적 위를 걸어 넘어가기만 했다. 몸집만큼 키운다. (스포너가 미리 곱해도 위 줄이 프리셋 값으로 덮어써서 여기서 해야 한다.)
	// 다만 몸집을 그대로 곱하면 5배 크리처가 19m 밖에서 휘둘러 맞혔다(팔은 그만큼 안 닿는다). 몸집이 커진 만큼의 60% 만 늘린다 → 5배 = 3.8m × 3.4 ≈ 13m.
	AttackRange *= 1.0f + (GetActorScale3D().X - 1.0f) * 0.6f;
	MaxHealth *= Visuals.HealthScale;
	AttackDamage *= Visuals.DamageScale;
	if (HasAuthority())
	{
		Health = MaxHealth;
		bDormant = bStartDormant && !Visuals.DormantIdle.IsNull();
	}
	SetVisuals(Visuals);
	if (IsValid(Lootable))
	{
		Lootable->SetLootTableId(LootTableId);
		Lootable->OnEmptied.AddDynamic(this, &APGMonsterCharacter::HandleLootEmptied);
	}
}

// ---- 보이는 것 ----

bool APGMonsterCharacter::GetPresetVisuals(FName Preset, FPGMonsterVisuals& Out)
{
	// 모양표(데이터 에셋 DA_PGMonsterLooks, 없으면 원래 코드 표와 같은 기본값)에서 찾는다(9/23 블루프린트 분리).
	const UPGMonsterLookSet* Set = UPGMonsterLookSet::GetActive();
	const FPGMonsterVisuals* Found = Set ? Set->Looks.Find(Preset) : nullptr;
	if (!Found)
		return false;
	Out = *Found;
	return true;
}

void APGMonsterCharacter::SetVisuals(const FPGMonsterVisuals& InVisuals)
{
	Visuals = InVisuals;
	GetCapsuleComponent()->SetCapsuleSize(Visuals.CapsuleRadius, Visuals.CapsuleHalfHeight);
	if (USkeletalMesh* Loaded = Visuals.Mesh.IsNull() ? nullptr : Visuals.Mesh.LoadSynchronous())
	{
		GetMesh()->SetSkeletalMeshAsset(Loaded);
		// 옷 물리(천 시뮬레이션)를 끈다. 크리처(Paragon Rampage)는 천 조각이 달려 있는데, 탑승 로봇과 싸우는 도중
		// 엔진 GPU 스킨 캐시가 천 데이터의 LOD 검사(SimData->LODIndex <= LOD)에서 멈춰 에디터가 통째로 꺼졌다(9/22 크래시).
		// 몹 수십 마리에 천을 돌릴 이유도 없다 — 멀리서는 안 보이고 프레임만 먹는다. 천 자원까지 풀어서 그 데이터 자체를 없앤다.
		if (Loaded->GetMeshClothingAssets().Num() > 0)
		{
			GetMesh()->bDisableClothSimulation = true;
			GetMesh()->ReleaseAllClothingResources();
			UE_LOG(LogPGObjects, Display, TEXT("%s: cloth simulation off for %s (%d clothing asset(s))"),
				*GetName(), *Loaded->GetName(), Loaded->GetMeshClothingAssets().Num());
		}
		// 팩마다 메시 원점이 발(0)인 것도, 몸 중심인 것도 있다(선인장이 공중에 떠 있던 원인).
		// 메시 바운드의 바닥이 캡슐 바닥에 오도록 Z 를 자동으로 맞춘다. 좌우·앞뒤 오프셋과 회전은 프리셋 값 그대로.
		const float MeshBottom = Loaded->GetBounds().GetBox().Min.Z;
		Visuals.MeshOffset.Z = -Visuals.CapsuleHalfHeight - MeshBottom;
	}
	GetMesh()->SetRelativeLocationAndRotation(Visuals.MeshOffset, Visuals.MeshRotation);
	CurrentLoop = nullptr;
	if (!bDead)
		PlayLoop(bDormant && !Visuals.DormantIdle.IsNull() ? Visuals.DormantIdle : Visuals.Idle);
}

bool APGMonsterCharacter::IsBusy() const
{
	return bAttacking || GetWorld()->GetTimeSeconds() < AnimLockUntil;
}

void APGMonsterCharacter::Wake()
{
	if (!HasAuthority() || !bDormant || bDead)
		return;
	bDormant = false;
	OnRep_Dormant();
}

void APGMonsterCharacter::Sleep()
{
	if (!HasAuthority() || bDormant || bDead || !bStartDormant || Visuals.DormantIdle.IsNull())
		return;
	bDormant = true;
	GetCharacterMovement()->StopMovementImmediately();
	CurrentLoop = nullptr; // Tick 이 DormantIdle 루프로 갈아탄다
}

void APGMonsterCharacter::OnRep_Dormant()
{
	if (bDormant || bDead)
		return;
	// 변신 모션이 끝날 때까지 다른 모션이 끼어들지 못하게 잠근다.
	AnimLockUntil = GetWorld()->GetTimeSeconds() + PlayOnce(Visuals.Wake);
}

void APGMonsterCharacter::PlayLoop(const TSoftObjectPtr<UAnimSequence>& Anim)
{
	// 매 Tick 불린다. 이미 그 루프를 돌고 있으면 로드 경로를 타지 않고 바로 나간다(몬스터 수십 마리 × 매 프레임).
	if (Anim.IsNull() || (CurrentLoop && Anim.Get() == CurrentLoop))
		return;
	UAnimSequence* Loaded = Anim.LoadSynchronous();
	if (!Loaded || Loaded == CurrentLoop)
		return;
	GetMesh()->PlayAnimation(Loaded, true);
	CurrentLoop = Loaded;
}

float APGMonsterCharacter::PlayOnce(const TSoftObjectPtr<UAnimSequence>& Anim)
{
	UAnimSequence* Loaded = Anim.IsNull() ? nullptr : Anim.LoadSynchronous();
	if (!Loaded)
		return 0.0f;
	GetMesh()->PlayAnimation(Loaded, false);
	// 한 번짜리 애니가 끝나면 Tick이 다시 서 있기/달리기 루프를 고르도록 현재 루프를 비운다.
	CurrentLoop = nullptr;
	return Loaded->GetPlayLength();
}

// [멀티 임시수정 2026-09-28 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md "몬스터 공격·맞는 동작")
// 무엇: 서버가 한 번짜리 동작을 틀 때 OnceCue 에 적어 클라도 같은 동작을 튼다. 클라는 동작 길이만큼 AnimLockUntil 을 잡는다 —
//   안 잡으면 클라 Tick 이 바로 서 있기/달리기 루프로 덮는다(클라에는 bAttacking 이 없다).
float APGMonsterCharacter::PlayOnceForAll(const TSoftObjectPtr<UAnimSequence>& Anim)
{
	const float Length = PlayOnce(Anim);
	if (HasAuthority() && Length > 0.0f)
	{
		OnceCue.Anim = Anim.Get();
		++OnceCue.Seq;
	}
	return Length;
}

void APGMonsterCharacter::OnRep_OnceCue()
{
	if (bDead || !IsValid(OnceCue.Anim) || !GetMesh())
		return;
	GetMesh()->PlayAnimation(OnceCue.Anim, false);
	CurrentLoop = nullptr;
	AnimLockUntil = GetWorld()->GetTimeSeconds() + OnceCue.Anim->GetPlayLength();
	static int32 Shown = 0; // 확인용: 처음 몇 번만 로그
	if (++Shown <= 5)
		UE_LOG(LogPGObjects, Display, TEXT("%s: once anim on this screen — %s"), *GetName(), *OnceCue.Anim->GetName());
}
// [멀티 임시수정] 끝

float APGMonsterCharacter::DistanceToTarget(const AActor* From, const AActor* Target)
{
	if (!IsValid(From) || !IsValid(Target))
		return TNumericLimits<float>::Max();
	const FVector FromLocation = From->GetActorLocation();
	if (!Target->IsA<ACharacter>())
	{
		if (const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
		{
			FVector Closest;
			const float Surface = Root->GetClosestPointOnCollision(FromLocation, Closest);
			if (Surface >= 0.0f)
				return Surface;
		}
	}
	return FVector::Dist(FromLocation, Target->GetActorLocation());
}

void APGMonsterCharacter::KnockAhead()
{
	// 방향: 움직이고 있으면 속도 방향, 벽에 막혀 멈춰 있으면 "가려는" 방향(가속 = 이동 입력).
	// 속도만 봤을 때는 거대 보스가 건물에 막혀 속도가 0 이 되는 순간 밀치기가 꺼져서, 건물 앞에서 제자리걸음만 했다.
	const FVector Velocity = GetVelocity();
	FVector Direction = Velocity.GetSafeNormal2D();
	if (Velocity.Size2D() < 100.0f)
	{
		const FVector Wanted = GetCharacterMovement()->GetCurrentAcceleration();
		if (Wanted.Size2D() < 1.0f)
			return;
		Direction = Wanted.GetSafeNormal2D();
	}
	KnockToward(Direction, 1.0f);
}

bool APGMonsterCharacter::HasClearLineTo(const AActor* Target) const
{
	if (!IsValid(Target))
		return false;
	FCollisionObjectQueryParams Walls;
	Walls.AddObjectTypesToQuery(ECC_WorldStatic);
	Walls.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGMonsterLineOfAttack), false, this);
	Params.AddIgnoredActor(Target);
	// 두 몸의 가운데끼리. 8배 보스는 가운데가 9m 위라, 발끼리 재면 낮은 담장에도 막히고 머리끼리 재면 지붕 너머도 뚫린다.
	return !GetWorld()->LineTraceTestByObjectType(GetActorLocation(), Target->GetActorLocation(), Walls, Params);
}

bool APGMonsterCharacter::SmashObstacleToward(const AActor* Target)
{
	if (!HasAuthority() || bDead || bDormant || !IsValid(Target) || !CanKnockProps() || IsBusy())
		return false;
	// 2초에 한 번. 매 프레임 부수면 벽이 한꺼번에 사라지고 잔해 예산도 한 번에 바닥난다.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastSmashTime < 2.0f)
		return false;
	LastSmashTime = Now;
	const FVector Direction = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	SetActorRotation(Direction.Rotation());
	// 공격 모션을 보여 주고 그동안은 멈춘다(IsBusy). 모션 없이 벽만 날아가면 "왜 부서졌지?"가 된다.
	const TSoftObjectPtr<UAnimSequence>& Swing = Visuals.AttackVariants.Num() > 0 ? Visuals.AttackVariants[0] : Visuals.Attack;
	AnimLockUntil = Now + FMath::Max(0.6f, PlayOnceForAll(Swing)); // [멀티 임시수정 2026-09-28 — 형님께 전달] 원래: PlayOnce(Swing)
	// 벽은 걸으며 밀치는 소품보다 크다. 공장 벽 판자까지 닿게 반경 한도를 1.6배로.
	const int32 Knocked = KnockToward(Direction, 1.6f);
	UE_LOG(LogPGObjects, Display, TEXT("%s: blocked from %s, smashed %d pieces"), *GetName(), *GetNameSafe(Target), Knocked);
	return true;
}

bool APGMonsterCharacter::KickSmallMonster(AActor* OtherActor, const FVector& Direction)
{
	APGMonsterCharacter* Other = Cast<APGMonsterCharacter>(OtherActor);
	if (!HasAuthority() || !IsValid(Other) || Other == this || Other->IsDead())
		return false;
	const float MyRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	if (Other->GetCapsuleComponent()->GetScaledCapsuleRadius() > MyRadius * 0.4f)
		return false;
	// 같은 놈을 매 프레임 차면 피해가 프레임 수만큼 들어간다. 0.5초에 한 번.
	const float Now = GetWorld()->GetTimeSeconds();
	float& Last = LastKickTime.FindOrAdd(TWeakObjectPtr<AActor>(Other), -1000.0f);
	if (Now - Last < 0.5f)
		return true;
	Last = Now;
	// 몸 크기 차이가 아주 크면(상대 반지름이 내 20% 이하: 8배 보스·17m 로봇 앞의 슬라임·선인장) 밟혀서 즉사.
	// 걷어차서 날리기만 하면 몇 번이고 다시 달려와 발에 걸렸다. 거대 보스 앞의 작은 세력은 "쓸려 나가는" 역할이다.
	if (Other->GetCapsuleComponent()->GetScaledCapsuleRadius() <= MyRadius * 0.2f)
	{
		UGameplayStatics::ApplyDamage(Other, Other->MaxHealth * 10.0f, GetController(), this, UDamageType::StaticClass());
		return true;
	}
	// 가는 방향 60% + 나에게서 멀어지는 방향 80%. 정면으로만 차면 몇 걸음 뒤 또 발 앞에 떨어져서 다시 막혔다.
	const float Ratio = GetKnockRatio();
	const FVector Away = (Other->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const FVector Horizontal = (Direction.GetSafeNormal2D() * 0.6f + Away * 0.8f).GetSafeNormal2D();
	Other->LaunchCharacter(Horizontal * FMath::Min(900.0f * Ratio, 2000.0f) + FVector(0.0f, 0.0f, FMath::Min(500.0f * Ratio, 900.0f)), true, true);
	UGameplayStatics::ApplyDamage(Other, AttackDamage * 0.5f, GetController(), this, UDamageType::StaticClass());
	return true;
}

bool APGMonsterCharacter::ShoveVehicle(AActor* OtherActor, const FVector& Direction)
{
	APGVehiclePawn* Car = Cast<APGVehiclePawn>(OtherActor);
	APGTankPawn* Tank = Cast<APGTankPawn>(OtherActor);
	if (!HasAuthority() || (!Car && !Tank) || !CanKnockProps())
		return false;
	// 크기 비교(사용자 규칙 9/20: "크기가 큰 쪽이 작은 쪽을 날린다, 개연성만 맞으면 된다").
	// 내 크기 = 캡슐 반높이(거인은 키가 크기의 대부분), 탈것 크기 = 충돌 경계 구 반지름. 차 ≈ 2.6m, 1.5배 탱크 ≈ 5.5m,
	// 5배 크리처 ≈ 4.5m, 8배 로봇 ≈ 7m → 크리처는 차만, 로봇·보스는 탱크까지 날린다.
	FVector Origin, Extent;
	OtherActor->GetActorBounds(true, Origin, Extent);
	const float TargetSize = Extent.Size();
	const float MySize = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	if (MySize < TargetSize * 1.1f)
		return false;
	const float Now = GetWorld()->GetTimeSeconds();
	float& Last = LastKickTime.FindOrAdd(TWeakObjectPtr<AActor>(OtherActor), -1000.0f);
	if (Now - Last < 0.6f)
		return true;
	Last = Now;
	// 세기: 크기 차이가 클수록 멀리. 옆으로 밀려나는 게 주고 위로는 조금만(하늘로 솟으면 개연성이 깨진다).
	const float Advantage = FMath::Clamp(MySize / TargetSize, 1.0f, 3.0f);
	const FVector Away = (OtherActor->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
	const FVector Horizontal = (Direction.GetSafeNormal2D() * 0.6f + Away * 0.8f).GetSafeNormal2D();
	const FVector Velocity = Horizontal * (700.0f * Advantage) + FVector(0.0f, 0.0f, 250.0f * Advantage);
	if (Car)
	{
		// 차는 Chaos 물리 몸: 속도 변화로 밀고, 옆으로 굴러가게 약간 돌린다.
		if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Car->GetRootComponent()); IsValid(Body) && Body->IsSimulatingPhysics())
		{
			Body->AddImpulse(Velocity, NAME_None, true);
			Body->AddAngularImpulseInDegrees(FVector::CrossProduct(FVector::UpVector, Horizontal) * 90.0f * Advantage, NAME_None, true);
		}
	}
	else
	{
		// 탱크는 코드로 움직여서 물리 몸이 없다. 탱크가 직접 밀려나는 움직임을 처리한다(APGTankPawn::Shove).
		Tank->Shove(Velocity);
	}
	// 부딪힌 만큼 조금 상한다. 차에 탄 사람도 흔들린 느낌이 나게(치명적이진 않게).
	UGameplayStatics::ApplyDamage(OtherActor, AttackDamage * 0.3f, GetController(), this, UDamageType::StaticClass());
	UE_LOG(LogPGObjects, Display, TEXT("%s shoved %s (my=%.0f target=%.0f)"), *GetName(), *OtherActor->GetName(), MySize, TargetSize);
	return true;
}

int32 APGMonsterCharacter::KnockToward(const FVector& Direction, float RadiusScale)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_KnockToward);
	const float Ratio = GetKnockRatio();
	const FVector Start = GetActorLocation();
	// 벽 부수기(RadiusScale>1)는 더 멀리, 더 높게 훑는다. 5배 크리처는 캡슐 반지름만 4.5m 라 기본 거리로는 벽에 겨우 닿는다.
	const FVector End = Start + Direction * (150.0f * Ratio + 100.0f) * RadiusScale;

	TArray<FHitResult> Hits;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectTypes.AddObjectTypesToQuery(ECC_Pawn); // 발 앞의 작은 몬스터를 막히기 전에 걷어차려고
	ObjectTypes.AddObjectTypesToQuery(ECC_Vehicle); // 발 앞의 차·탱크(나보다 작으면 밀쳐 날린다)
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGBigBodyKnock), false, this);
	// 훑는 캡슐은 적어도 내 몸 크기. 예전 크기(60×배율, 110×배율)는 8배 로봇 몸(반지름 3.6m)보다 가늘고 몸 가운데 높이만 훑어서,
	// 옆으로 걸을 때 옆구리에 걸린 통·철골을 못 잡아 끼었고, 5배 크리처는 3~4m 짜리 작은 건물을 못 보고 그 위로 올라탔다(9/19).
	const float BodyRadius = GetCapsuleComponent()->GetScaledCapsuleRadius();
	const float BodyHalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	GetWorld()->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, ObjectTypes,
		FCollisionShape::MakeCapsule(FMath::Max(60.0f * Ratio, BodyRadius * 1.1f), FMath::Max(110.0f * Ratio * RadiusScale, BodyHalfHeight)), Params);

	// 속도 변화 cm/s. 몸집에 비례하되 위쪽은 700 에서 자른다. 안 자르면 8배 보스는 위로 초속 19m, 작은 소품은 거기에 ×1.8 이 붙어
	// 30m 넘게 솟구쳤다(PIE 시작하자마자 멀리서 소품이 하늘로 날아가던 것 — 다른 세력을 쫓던 5배 크리처). 옆으로 밀려나는 게 "밀쳐낸" 느낌이다.
	const FVector Impulse = Direction * FMath::Min(1400.0f * Ratio, 3000.0f) + FVector(0.0f, 0.0f, FMath::Min(600.0f * Ratio, 700.0f));
	int32 Knocked = 0;
	for (const FHitResult& Hit : Hits)
	{
		if (KickSmallMonster(Hit.GetActor(), Direction) || ShoveVehicle(Hit.GetActor(), Direction))
			continue;
		// 내 키(캡슐 전체 높이)보다 낮은 것은 넓어도 부순다: 판때기 지붕 건물 위로 올라타지 않고, 철골 기둥·큰 벽에 끼지 않게.
		// 허리 높이까지만 허용했을 때는 17m 로봇이 공장 철골 사이에 끼었다(9/19). 큰 조각은 물리 없이 무너지는 연출이라 비용이 거의 없다.
		if (PGPhysicsUtil::TryKnockProp(Hit.GetComponent(), Hit, Impulse, this, 500.0f * Ratio * RadiusScale, 80.0f, GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f))
		{
			++Knocked;
			UE_LOG(LogPGObjects, Verbose, TEXT("%s knocked %s (%s)"), *GetName(), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()));
		}
	}
	return Knocked;
}

void APGMonsterCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bDead)
		return;
	if (HasAuthority() && !bDormant && CanKnockProps())
		KnockAhead();
	if (bAttacking)
		return;
	if (GetWorld()->GetTimeSeconds() < AnimLockUntil)
		return;
	if (bDormant)
	{
		PlayLoop(Visuals.DormantIdle.IsNull() ? Visuals.Idle : Visuals.DormantIdle);
		return;
	}
	const float Speed = GetVelocity().Size2D();
	PlayLoop(Speed > 20.0f ? Visuals.Run : Visuals.Idle);
}

// ---- 피해·사망 ----

float APGMonsterCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || bDead || Applied <= 0.0f)
		return Applied;

	Health = FMath::Max(0.0f, Health - Applied);
	UE_LOG(LogPGObjects, Display, TEXT("%s took %.0f from %s (hp %.0f/%.0f)"),
		*GetName(), Applied, *GetNameSafe(DamageCauser), Health, MaxHealth);
	// 잠복 중에 플레이어에게 맞으면 깬다. 몬스터끼리의 피해(범위 공격에 휘말림 등)로는 안 깬다 — 잠복은 플레이어만 깨운다(AI IsValidTarget 과 같은 규칙).
	if (bDormant && IsValid(EventInstigator) && EventInstigator->IsPlayerController())
		Wake();
	// 깨어 있는 몬스터는 쏜 쪽을 바로 표적으로(등 뒤에서 맞아도 돌아본다). 표적이 될 수 있는지는 AI 가 판단(영역 밖·같은 세력이면 무시).
	if (AMonsterAIController* AI = Cast<AMonsterAIController>(GetController()); IsValid(AI) && IsValid(EventInstigator))
		AI->NotifyDamagedBy(EventInstigator->GetPawn());

	if (Health <= 0.0f)
		Die(EventInstigator);
	else if (!bAttacking)
		AnimLockUntil = GetWorld()->GetTimeSeconds() + PlayOnceForAll(Visuals.Hit); // [멀티 임시수정 2026-09-28 — 형님께 전달] 원래: PlayOnce(Visuals.Hit)
	return Applied;
}

void APGMonsterCharacter::Die(AController* Killer)
{
	if (bDead)
		return;
	bDead = true;
	PGSound::PlayAll(this, FName(TEXT("Monster_Death")), this, GetActorLocation()); // 소리 신호(서버 → 모두)
	GetWorldTimerManager().ClearTimer(AttackHitTimer);
	GetWorldTimerManager().ClearTimer(AttackEndTimer);
	bAttacking = false;

	// AI를 떼고 움직임을 멈춘다. 캡슐은 꺼서 시체를 밟고 지나갈 수 있게 하고,
	// 메시는 남겨 두어 F 상호작용(루팅) 트레이스에 걸리게 한다.
	DetachFromControllerPendingDestroy();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	OnRep_Dead();

	if (IsValid(Lootable))
	{
		// 시드는 이름 해시로: 같은 몬스터 이름이면 같은 루팅. 나중에 맵 시드와 합치면 된다.
		Lootable->ActivateLoot(static_cast<int64>(GetTypeHash(GetFName())));
	}
	// 루팅을 안 해도 시체가 영원히 남지 않게. 다 털리면 HandleLootEmptied 가 더 짧게 덮어쓴다.
	//
	// CreateWeakLambda 를 쓰는 이유: 그냥 [this] 로 잡으면 이 액터가 다른 경로로 먼저
	// 파괴됐을 때(구역 붕괴가 반경 안 액터를 Destroy 한다) 타이머가 죽은 포인터로 실행된다.
	// 타이머 매니저가 자동으로 지워 주는 것은 UObject 에 묶인 것뿐이고, 날 람다는 아니다.
	if (CorpseLifetime > 0.0f)
		GetWorldTimerManager().SetTimer(CorpseTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }), CorpseLifetime, false);
	UE_LOG(LogPGObjects, Display, TEXT("%s died (killer=%s), loot table %s"),
		*GetName(), *GetNameSafe(Killer), *LootTableId.ToString());
	// 판 기록(처치 수). 보스 로봇도 여기를 거치므로 한 줄로 충분하다 — 보스 여부는 서브시스템이 가려낸다.
	UPGRunSubsystem::NotifyMonsterKilled(this, Killer);
}

void APGMonsterCharacter::OnRep_Dead()
{
	if (!bDead)
		return;
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlayOnce(Visuals.Die);
	AnimLockUntil = FLT_MAX;
}

void APGMonsterCharacter::HandleLootEmptied(UPGLootableComponent* InLootable, APawn* Pawn)
{
	if (!HasAuthority())
		return;
	GetWorldTimerManager().SetTimer(CorpseTimer,
		FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }), CorpseLifetimeAfterEmpty, false);
}

// ---- 공격 ----

bool APGMonsterCharacter::TryAttack(AActor* Target)
{
	if (!HasAuthority() || bDead || bAttacking || !IsValid(Target))
		return false;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastAttackTime < AttackInterval)
		return false;

	// 상대를 향해 돈다. 피해는 애니가 휘두르는 시점에 준다.
	FRotator Face = (Target->GetActorLocation() - GetActorLocation()).Rotation();
	Face.Pitch = 0.0f;
	Face.Roll = 0.0f;
	SetActorRotation(Face);

	LastAttackTime = Now;
	bAttacking = true;
	PendingAttackTarget = Target;
	// 모션이 여러 개면 매번 다른 것을 골라 같은 동작만 반복하는 느낌을 줄인다.
	const TSoftObjectPtr<UAnimSequence>& Chosen = Visuals.AttackVariants.Num() > 0
		? Visuals.AttackVariants[FMath::RandRange(0, Visuals.AttackVariants.Num() - 1)]
		: Visuals.Attack;
	const float Length = PlayOnceForAll(Chosen); // [멀티 임시수정 2026-09-28 — 형님께 전달] 원래: PlayOnce(Chosen)
	// 공격 모션이 안 보인다는 보고 추적용: 어떤 애니를 몇 초짜리로 틀었는지, 얼마나 떨어져서 휘둘렀는지.
	UE_LOG(LogPGObjects, Display, TEXT("%s attacks %s anim=%s len=%.2f dist=%.0f range=%.0f"), *GetName(), *GetNameSafe(Target),
		*Chosen.GetAssetName(), Length, FVector::Dist(GetActorLocation(), Target->GetActorLocation()), AttackRange);
	GetWorldTimerManager().SetTimer(AttackHitTimer, this, &APGMonsterCharacter::ApplyAttackHit, FMath::Max(AttackHitDelay, 0.01f), false);
	GetWorldTimerManager().SetTimer(AttackEndTimer, this, &APGMonsterCharacter::EndAttack, FMath::Max(Length, 0.6f), false);
	return true;
}

void APGMonsterCharacter::ApplyAttackHit()
{
	AActor* Target = PendingAttackTarget.Get();
	if (bDead || !IsValid(Target))
		return;
	// 휘두르는 사이 상대가 물러났으면 빗나간다. 여유는 사거리의 30%.
	if (DistanceToTarget(this, Target) > AttackRange * 1.3f)
		return;
	// 휘두르는 사이 벽 뒤로 숨었으면 빗나간다(벽 너머 피해 금지).
	if (!HasClearLineTo(Target))
		return;
	UGameplayStatics::ApplyDamage(Target, AttackDamage, GetController(), this, UDamageType::StaticClass());
}

void APGMonsterCharacter::EndAttack()
{
	bAttacking = false;
	PendingAttackTarget = nullptr;
}
