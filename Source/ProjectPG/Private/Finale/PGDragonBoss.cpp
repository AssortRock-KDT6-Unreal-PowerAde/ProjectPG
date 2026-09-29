#include "Finale/PGDragonBoss.h" // 언리얼 규칙: 클래스 이름과 같은 .cpp 는 자기 헤더를 맨 먼저 포함한다
#include "Dragon/PGDragonBossInternal.h"
#include "Dragon/PGDragonDeath.h"
#include "Dragon/PGDragonGroundCombat.h"
#include "Dragon/PGDragonAirCombat.h"
#include "Net/UnrealNetwork.h"
#include "Engine/Engine.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Misc/PackageName.h"
#include "Common/PGPlayerMessageComponent.h"
// 이 파일: 생성·흐름·상태 전환 같은 뼈대. 나머지 책임은 Dragon/ 폴더(Air, Ground, Death).


// ─────────────────────────── 처박힐 때의 카메라 흔들림 ───────────────────────────
// 위치는 cm, 회전은 도. 처음이 가장 세고(TotalSeconds 의 첫 0.15초 동안 0→1 로 들어가 툭 튀지 않게)
// 남은 시간의 제곱으로 잦아든다. 세 축의 주파수를 서로 다르게 두면 "쿵" 뒤의 잔진동처럼 읽힌다.
// 세기(거리 감쇠)는 엔진이 결과에 곱해 준다(PlayWorldCameraShake 의 안쪽·바깥 반지름).

void UPGDragonCrashShakePattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const
{
	OutInfo.Duration = FCameraShakeDuration(TotalSeconds);
	OutInfo.BlendIn = 0.0f;  // 들어가고 나가는 곡선은 아래에서 직접 만든다
	OutInfo.BlendOut = 0.0f;
}

void UPGDragonCrashShakePattern::StartShakePatternImpl(const FCameraShakePatternStartParams& /*Params*/)
{
	Elapsed = 0.0f;
}

void UPGDragonCrashShakePattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult)
{
	Elapsed += Params.DeltaTime;
	const float T = FMath::Clamp(Elapsed / FMath::Max(TotalSeconds, 0.01f), 0.0f, 1.0f);
	const float In = FMath::Clamp(Elapsed / 0.15f, 0.0f, 1.0f);
	const float Envelope = In * FMath::Square(1.0f - T);
	OutResult.Location = FVector(
		FMath::Sin(Elapsed * 55.0f) * 18.0f,
		FMath::Sin(Elapsed * 41.0f + 1.3f) * 14.0f,
		FMath::Sin(Elapsed * 63.0f + 0.7f) * 30.0f) * Envelope;
	OutResult.Rotation = FRotator(
		FMath::Sin(Elapsed * 47.0f) * 2.2f,
		FMath::Sin(Elapsed * 33.0f + 2.1f) * 0.8f,
		FMath::Sin(Elapsed * 58.0f + 0.4f) * 1.6f) * Envelope;
}

bool UPGDragonCrashShakePattern::IsFinishedImpl() const
{
	return Elapsed >= TotalSeconds;
}

// 엔진의 흔들림 클래스는 "RootShakePattern" 이라는 이름의 틀을 기본 부품으로 만든다(기본은 틀이 없다).
// 그 자리에 우리 틀을 끼운다 — 엔진의 ULegacyCameraShake 도 같은 식으로 끼운다.
UPGDragonCrashShake::UPGDragonCrashShake(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UPGDragonCrashShakePattern>(TEXT("RootShakePattern")))
{
	bSingleInstance = true; // 드래곤은 한 마리다. 겹쳐 불리면 처음부터 다시
}
APGDragonBoss::APGDragonBoss()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);

	// 맞는 몸은 캡슐 하나로 둔다. 스켈레탈 메시의 물리 에셋을 쓰면 40m 짜리 뼈 충돌이 매 프레임 갱신된다.
	Hull = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Hull"));
	Hull->InitCapsuleSize(600.0f, 900.0f);
	Hull->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Hull->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore); // 사람이 부딪혀 튕기지 않게
	SetRootComponent(Hull);

	// 몸통을 덮는 두 번째 캡슐. 크기·높이는 BeginPlay 에서 에셋을 재서 정한다.
	Torso = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Torso"));
	Torso->SetupAttachment(Hull);
	Torso->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Torso->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Mesh"));
	Mesh->SetupAttachment(Hull);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -MeshBelowOriginCm));
	Mesh->bComponentUseFixedSkelBounds = true; // 날갯짓이 커서 경계가 흔들리면 화면에서 깜빡인다
	SetNetCullDistanceSquared(FMath::Square(300000.0f));

	// 겉모습 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 에셋 그대로).
	DragonMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(DragonMeshPath));
	DragonAnimFolder.Path = DragonAnimDir;
	BreathFireEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(FireFxPath));
	BreathImpactEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(ImpactFxPath));
	BreathFlameMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/MI_MissileFlame.MI_MissileFlame")));
	CrashBlastMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(CrashBlastMeshPath));
	CrashBlastMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(CrashBlastMaterialPath));
}

void APGDragonBoss::CreateCollaborators()
{
	if (!Death)
	{
		Death = NewObject<UPGDragonDeath>(this, TEXT("Death"));
		Death->Init(this);
	}
	if (!GroundCombat)
	{
		GroundCombat = NewObject<UPGDragonGroundCombat>(this, TEXT("GroundCombat"));
		GroundCombat->Init(this);
	}
	if (!AirCombat)
	{
		AirCombat = NewObject<UPGDragonAirCombat>(this, TEXT("AirCombat"));
		AirCombat->Init(this);
	}
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGDragonAirCombat(협력 객체)가 한다.
void APGDragonBoss::BeginRise(const FVector& GroundSpot, APGBattleshipActor* InShip)
{
	if (AirCombat)
		AirCombat->BeginRise(GroundSpot, InShip);
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGDragonAirCombat(협력 객체)가 한다.
float APGDragonBoss::KeepDistanceCm(const APGBattleshipActor* Against) const
{
	if (AirCombat)
		return AirCombat->KeepDistanceCm(Against);
	return {};
}

void APGDragonBoss::PreloadAssets(TSubclassOf<APGDragonBoss> DragonClass)
{
	static TSharedPtr<FStreamableHandle> Handle; // 판이 끝날 때까지 쥐고 있어야 메모리에서 안 내려간다
	const APGDragonBoss* Defaults = DragonClass ? DragonClass->GetDefaultObject<APGDragonBoss>() : GetDefault<APGDragonBoss>();
	if (!Defaults || (Handle.IsValid() && Handle->IsActive()))
		return;
	TArray<FSoftObjectPath> Paths;
	for (const FSoftObjectPath& Path : { Defaults->DragonMesh.ToSoftObjectPath(), Defaults->BreathFireEffect.ToSoftObjectPath(),
		Defaults->BreathImpactEffect.ToSoftObjectPath(), Defaults->BreathFlameMaterial.ToSoftObjectPath(),
		Defaults->CrashBlastMesh.ToSoftObjectPath(), Defaults->CrashBlastMaterial.ToSoftObjectPath() })
		if (Path.IsValid())
			Paths.Add(Path);
	FString AnimDir = Defaults->DragonAnimFolder.Path;
	if (!AnimDir.IsEmpty() && !AnimDir.EndsWith(TEXT("/")))
		AnimDir += TEXT("/");
	static const TCHAR* AnimNames[] = { TEXT("FlyForwardAnim"), TEXT("FlyGlideAnim"), TEXT("FlyIdleAnim"), TEXT("FlyAttackAnim"), TEXT("FlyFlameAnim"),
		TEXT("TakeOffAnim"), TEXT("TakeoffAnim"), TEXT("LandAnim"), TEXT("LandingAnim"), TEXT("DieAnim"), TEXT("DeathAnim"), TEXT("GetHitAnim"),
		TEXT("HitAnim"), TEXT("RoarAnim"), TEXT("ScreamAnim"), TEXT("WalkAnim"), TEXT("AttackMouthAnim"), TEXT("AttackHandAnim"),
		TEXT("AttackFlameAnim"), TEXT("AttackTailAnim"), TEXT("AttackWingClawAnim"), TEXT("AttackFireBallAnim"), TEXT("DefendAnim") };
	int32 Anims = 0;
	for (const TCHAR* Name : AnimNames)
	{
		const FString Package = AnimDir + Name;
		if (!FPackageName::DoesPackageExist(Package))
			continue;
		Paths.Add(FSoftObjectPath(Package + TEXT(".") + Name));
		++Anims;
	}
	// 다 읽으면(화면이 있는 컴퓨터만) 드래곤 몸을 보이지 않는 채로 한 번 만들어 둔다 — 엔진이 그 재질을 그릴 준비(PSO)를 뒤에서 미리 한다.
	//   9/28 화면 그리는 시험: 동작 찾기 기다림을 없앤 뒤에도 등장 순간 85ms 프레임 1번이 남았다(처음 그릴 때 준비 비용).
	const TSoftObjectPtr<USkeletalMesh> MeshToWarm = Defaults->DragonMesh;
	Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([MeshToWarm]()
	{
		USkeletalMesh* Mesh = MeshToWarm.Get();
		if (!Mesh || !GEngine)
			return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
				continue;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			AActor* Warm = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(0.0f, 0.0f, -200000.0f)), Params);
			if (!Warm)
				continue;
			USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Warm, TEXT("DragonWarm"));
			Body->SetSkeletalMeshAsset(Mesh);
			Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Body->SetHiddenInGame(true);
			Warm->SetRootComponent(Body);
			Body->RegisterComponent(); // 등록하면서 재질 그릴 준비(PSO 미리 만들기)를 시작한다
			Warm->SetLifeSpan(20.0f);
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: warmed the dragon body for drawing (hidden, %s)"), *World->GetName());
		}
	}), FStreamableManager::AsyncLoadHighPriority);
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: preloading %d asset(s) (%d animation(s)) in the background"), Paths.Num(), Anims);
}

void APGDragonBoss::BeginPlay()
{
	Super::BeginPlay();

	// 책임별 협력 객체(검사·붕괴·그리기)를 만든다. 드래곤 액터는 이들을 순서대로 부르기만 한다(2026-09-26 한 책임 정리).
	CreateCollaborators();
	Health = MaxHealth;
	// 멀티(9/27): 전용 서버는 화면이 없어 "안 보이는 메시는 뼈를 안 움직인다" 설정에 걸린다 — 그러면 입(소켓) 위치가 기본 자세의
	//   입이라 브레스·피해가 엉뚱한 곳에서 나온다. 서버에서는 늘 뼈를 갱신한다.
	if (HasAuthority() && IsValid(Mesh))
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	// 보호 표. 이게 없으면 **솟아오를 자리의 붕괴가 나를 지운다.**
	//
	// 디렉터는 드래곤을 먼저 만들고(몸 크기를 알아야 반경을 정하므로) 그 자리를 무너뜨리는데,
	// 붕괴는 반경 안의 액터를 Destroy 한다. 나는 붕괴 중심에 서 있으니 첫 번째로 지워진다.
	// 그러면 디렉터의 `IsValid(Dragon)` 가드가 풀려 다음 상태에서 또 태어났다 — 9/20 PIE 에서
	// 드래곤이 두 번 나고 두 번째가 300m 더 높이 솟아(배가 그만큼 올라간 뒤라) 화면에서 사라졌다.
	// 붕괴 쪽은 원래 "배·드래곤은 표를 보고 건드리지 않는다" 로 되어 있었다. 표만 없었다.
	Tags.AddUnique(PGPhysicsUtil::ProtectedTag);
	// 시험용(-PGDragonAutoKill): 나타나고 20초 뒤 HP 를 0 으로 만든다. 화면 없는 시험 실행에서 끝나는 동작 순서를 로그로 보려고(9/23).
	if (HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("PGDragonAutoKill")))
	{
		FTimerHandle KillTimer;
		GetWorldTimerManager().SetTimer(KillTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: test auto-kill (-PGDragonAutoKill), state %d"), static_cast<int32>(State));
			// 첫 플레이어 몫으로 친다 — 전리품(가장 큰 피해를 준 사람) 흐름까지 시험된다.
			//   플레이어 몫이면 공중 총 배율(AirGunDamageMultiplier)이 곱해지므로 넉넉히 준다.
			UGameplayStatics::ApplyDamage(this, (Health + 1.0f) * 1000.0f, GetWorld()->GetFirstPlayerController(), nullptr, UDamageType::StaticClass());
		}), 20.0f, false);
	}
	if (USkeletalMesh* Asset = DragonMesh.IsNull() ? nullptr : DragonMesh.LoadSynchronous())
	{
		Mesh->SetSkeletalMeshAsset(Asset);
		Mesh->SetRelativeScale3D(FVector(DragonScale));
		// 이 팩 드래곤은 머리가 메시 +Y 쪽이다(뼈 확인: Head y=+127, 턱끝 y=+224 / 왼날개 x=+101). 언리얼의 "앞" 은 +X 라,
		// 돌리지 않으면 액터가 앞으로 날 때 드래곤은 옆구리를 내밀고 게걸음으로 난다(9/22 사용자: "옆으로 날아가며 전함을 따라가잖아").
		// 요 -90 이면 +Y(머리) 가 +X(앞) 로, +X(왼날개) 가 -Y(액터 왼쪽) 로 간다.
		Mesh->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
		// 몸 크기를 에셋에서 직접 잰다. 전에는 SoulEater 의 치수(578 × 1029 cm)를 코드에 박아 놓고
		// 묻는 깊이와 안전거리를 계산했는데, 모델을 바꾸는 순간 그 숫자가 전부 거짓말이 된다.
		// 재서 쓰면 어떤 드래곤을 끼워도 알아서 맞는다.
		const FBoxSphereBounds Bounds = Asset->GetBounds();
		const FVector Size = Bounds.BoxExtent * 2.0f;
		BodyHeightCm = Size.Z * DragonScale;
		// 안전거리는 "액터 원점에서 메시가 가장 멀리 뻗은 거리"여야 한다.
		//
		// 전에는 BoxExtent 만 썼는데, 그건 **피벗이 몸 한가운데에 있다**는 가정이 숨어 있다.
		// 이 팩의 드래곤은 피벗이 발밑(뒷다리 사이)이라 꼬리와 머리가 원점에서 한쪽으로 치우쳐 있고,
		// 그만큼 Extent 보다 더 멀리 나간다. 배율이 40 이라 원본에서 1cm 어긋난 것이 40cm 가 된다 —
		// 안전거리를 지켰는데도 "살짝 스치는" 양의 정체가 이것이다(9/20 계산).
		// Bounds.Origin 은 상자 중심이 피벗에서 얼마나 밀려 있는지다. |중심| + 반크기 = 가장 먼 모서리.
		const FVector Reach = Bounds.Origin.GetAbs() + Bounds.BoxExtent;
		HalfWingCm = FMath::Max(Reach.X, Reach.Y) * DragonScale;
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: %s is %.0f x %.0f x %.0f m at scale %.0f (pivot off %.0f/%.0f/%.0f m, reach %.0f m, extent-only %.0f m)"),
			*Asset->GetName(), Size.X * DragonScale * 0.01f, Size.Y * DragonScale * 0.01f,
			BodyHeightCm * 0.01f, DragonScale,
			Bounds.Origin.X * DragonScale * 0.01f, Bounds.Origin.Y * DragonScale * 0.01f, Bounds.Origin.Z * DragonScale * 0.01f,
			HalfWingCm * 0.01f, FMath::Max(Size.X, Size.Y) * 0.5f * DragonScale * 0.01f);
		Mesh->SetBoundsScale(3.0f);
		// 그림자는 켠 채로 둔다. 날개폭 410m 짜리가 땅에 드리우는 그림자가 이 싸움의 그림이다.
		// 배 안까지 그림자가 들어오던 건 드래곤 탓이 아니라 선체가 빛을 안 막아서였다(아래 갑판 지붕 참고).
		Mesh->SetCastShadow(true);

		// 입 뼈. 브레스가 여기서 나간다. 이름이 모델마다 다를 수 있어 후보를 차례로 본다.
		for (const TCHAR* Candidate : MouthBoneCandidates)
		{
			if (Mesh->DoesSocketExist(Candidate))
			{
				MouthBone = Candidate;
				break;
			}
		}
	}
	else
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGDragon: mesh missing (%s)"), *DragonMesh.ToString());
	}
	Hull->SetCapsuleSize(600.0f * DragonScale * 0.25f, 900.0f * DragonScale * 0.25f);

	// 왜 맞는 몸이 둘인가.
	//
	// Hull 은 액터 원점에 있고 반지름 60m · 반높이 90m 다. 그런데 보이는 몸은 231m 높이에 날개폭 412m 이고,
	// 메시 피벗이 발밑이라 몸통과 머리는 원점보다 한참 **위**에 있다. 즉 Hull 은 아랫도리만 덮는다.
	// 조준은 화면 가운데에서 쏘는 선 검사(ECC_Visibility)로 하는데, 메시 자체는 충돌이 없다.
	// 그래서 플레이어가 몸통·머리를 겨누면 선이 아무것도 안 맞고, 미사일이 쫓을 표적이 비어 버린다
	// (= 유도가 안 걸리고 그냥 직진한다). 보스가 화면을 꽉 채우는데 조준이 안 되는 것이 이 때문이다.
	//
	// Hull 을 키우거나 올리면 안 된다 — 액터 원점이 곧 Hull 이라, 솟아오르기·선회·착지 높이 계산이
	//   전부 "원점 기준"이다. 그래서 원점은 그대로 두고 **덮는 캡슐을 하나 더** 붙인다.
	//
	// 날개까지 덮지는 않는다. 반날개폭(206m)을 반지름으로 삼으면 미사일이 몸에서 200m 떨어진 허공에서
	//   터진다. 가로는 짧은 쪽(몸통 두께)의 절반만 쓴다 — 몸통과 머리는 확실히 맞고, 얇은 날개는 빗나간다.
	if (IsValid(Mesh) && Mesh->GetSkeletalMeshAsset())
	{
		const FBoxSphereBounds Bounds = Mesh->GetSkeletalMeshAsset()->GetBounds();
		const float TorsoRadius = FMath::Max(FMath::Min(Bounds.BoxExtent.X, Bounds.BoxExtent.Y) * 0.5f * DragonScale,
			Hull->GetScaledCapsuleRadius());
		const float TorsoHalf = FMath::Max(Bounds.BoxExtent.Z * DragonScale, TorsoRadius);
		Torso->SetCapsuleSize(TorsoRadius, TorsoHalf);
		// 메시가 원점보다 900 아래에 붙어 있고, 그 메시 안에서 상자 중심이 Origin.Z 만큼 올라가 있다.
		Torso->SetRelativeLocation(FVector(0.0f, 0.0f, Mesh->GetRelativeLocation().Z + Bounds.Origin.Z * DragonScale));
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: hit bodies — hull r%.0f h%.0f m at 0, torso r%.0f h%.0f m at %.0f m"),
			Hull->GetScaledCapsuleRadius() * 0.01f, Hull->GetScaledCapsuleHalfHeight() * 0.01f,
			TorsoRadius * 0.01f, TorsoHalf * 0.01f, Torso->GetRelativeLocation().Z * 0.01f);
	}

	FireFxAsset = BreathFireEffect.IsNull() ? nullptr : BreathFireEffect.LoadSynchronous();
	ImpactFxAsset = BreathImpactEffect.IsNull() ? nullptr : BreathImpactEffect.LoadSynchronous();
	UE_LOG(LogPGObjects, Display,
		TEXT("PGDragon: hp %.0f, fire fx=%s, impact fx=%s, mouth bone=%s, feet %.0f m below the origin"),
		MaxHealth, FireFxAsset ? TEXT("yes") : TEXT("no"), ImpactFxAsset ? TEXT("yes") : TEXT("no"),
		MouthBone.IsNone() ? TEXT("(none, guessing from the body)") : *MouthBone.ToString(), FeetOffsetCm() * 0.01f);
}

const TCHAR* APGDragonBoss::StateName(EPGDragonState InState)
{
	switch (InState)
	{
	case EPGDragonState::Rising:  return TEXT("Rising");
	case EPGDragonState::Orbit:   return TEXT("Orbit");
	case EPGDragonState::Pass:    return TEXT("Pass");
	case EPGDragonState::Landed:  return TEXT("Landed");
	case EPGDragonState::Dying:   return TEXT("Dying");
	case EPGDragonState::Breath:  return TEXT("Breath");
	case EPGDragonState::Descend: return TEXT("Descend");
	case EPGDragonState::TakeOff: return TEXT("TakeOff");
	case EPGDragonState::GroundFight: return TEXT("GroundFight");
	}
	return TEXT("?");
}

// 액터 원점에서 발바닥까지.
//
// 이게 9/21 "공중에서 앉는다" 버그의 핵심 숫자다. 메시(발바닥 = 피벗)는 원점보다 **900cm 아래**에
// 붙어 있다(생성자). 그런데 캡슐은 BeginPlay 에서 배율에 맞춰 반높이 5625cm 로 커진다. 옛 착지 코드는
// "땅 + 캡슐 반높이" 에 원점을 놓았으니, 발은 땅에서 5625 - 900 = 약 47m 위에 떠 있었다.
// 그 자리에서 착지·포효(지상 동작)를 틀었으니 허공에 앉은 것이다. 발 높이는 캡슐이 아니라 메시로 잰다.
float APGDragonBoss::FeetOffsetCm() const
{
	return IsValid(Mesh) ? -Mesh->GetRelativeLocation().Z : MeshBelowOriginCm;
}

// 드래곤을 쓰러뜨리면 가장 많이 때린 플레이어가 전리품(드래곤 비늘)을 자동으로 받는다(9/23 사용자).
// 전리품은 교환소에서 분홍 단발 가발(광선)로 바꾼다 — 가방이 차 있거나 받을 수 없는 몸이면(차·로봇에 탄 채) 발밑에 떨군다.
void APGDragonBoss::AwardTopDamageDealer()
{
	if (!HasAuthority() || TrophyItemId.IsNone())
		return;
	AController* Best = nullptr;
	float BestDamage = 0.0f;
	float Total = 0.0f;
	for (const TPair<TWeakObjectPtr<AController>, float>& Entry : DamageByController)
	{
		Total += Entry.Value;
		if (AController* Controller = Entry.Key.Get(); IsValid(Controller) && Entry.Value > BestDamage)
		{
			Best = Controller;
			BestDamage = Entry.Value;
		}
	}
	APawn* Pawn = IsValid(Best) ? Best->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: no player dealt damage (or nobody is alive) — no trophy"));
		return;
	}
	bool bGiven = UPGItemReceiverLibrary::GiveItem(Pawn, TrophyItemId, 1);
	if (!bGiven)
		bGiven = APGFloorItemActor::SpawnDrop(this, TrophyItemId, 1, FTransform(Pawn->GetActorLocation() + Pawn->GetActorForwardVector() * 150.0f)) != nullptr;
	const FString Name = Best->PlayerState ? Best->PlayerState->GetPlayerName() : Best->GetName();
	const float Share = BestDamage / FMath::Max(Total, 1.0f) * 100.0f;
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: trophy %s -> %s (%.0f damage, %.0f%% of %d player(s)) %s"),
		*TrophyItemId.ToString(), *Name, BestDamage, Share, DamageByController.Num(), bGiven ? TEXT("given") : TEXT("FAILED"));
	// 멀티(9/27): 받은 사람 화면에(서버 화면에 띄우면 전용 서버에는 화면이 없다).
	UPGPlayerMessageComponent::AnnounceTo(Best, {
		FText::Format(NSLOCTEXT("PGDragon", "Trophy", "{0} 획득 — 드래곤에게 가장 큰 피해 ({1}%)"),
			UPGWearableColorLibrary::GetItemDisplayName(TrophyItemId), FText::AsNumber(FMath::RoundToInt(Share))),
		NSLOCTEXT("PGDragon", "TrophyHint", "교환소에서 분홍 단발 가발로 바꿀 수 있습니다") });
}

float APGDragonBoss::MinAirborneZ() const
{
	return GroundDatumZ + FeetOffsetCm() + MinFlyClearanceCm;
}

FVector APGDragonBoss::GetRiseBodyCentreOffset() const
{
	const USkeletalMesh* Asset = IsValid(Mesh) ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!Asset)
		return FVector::ZeroVector;
	// 메시 상자 중심(메시 기준) → 액터 기준(메시 자리 -9m·요 -90·배율) → 솟는 자세(TickRise 의 피치 35도, 지금 요).
	const FVector InActor = Mesh->GetRelativeTransform().TransformPosition(Asset->GetBounds().Origin);
	const FVector Rotated = FRotator(35.0f, GetActorRotation().Yaw, 0.0f).RotateVector(InActor);
	return FVector(Rotated.X, Rotated.Y, 0.0f);
}

// 진짜로 발이 땅에서 떨어져 있는 상태. 공중이면 지상 동작을 막는다(PlayAnim).
// 솟는 중(Rising)은 아직 땅속·땅 위라 이륙 동작이 맞고, 내려앉은 동안(Landed)은 당연히 땅이다.
// 이륙(TakeOff)은 웅크렸다 뛰어오른 뒤부터, 추락(Dying)은 땅에 닿기 전까지만 공중이다.
bool APGDragonBoss::IsAirborne() const
{
	switch (State)
	{
	case EPGDragonState::Orbit:
	case EPGDragonState::Pass:
	case EPGDragonState::Breath:
	case EPGDragonState::Descend:
		return true;
	case EPGDragonState::TakeOff:
		return bLifted;
	case EPGDragonState::Dying:
		return !bOnGround;
	default:
		return false;
	}
}

// 공중에서는 나는 동작만 튼다.
//
// 왜 이름으로 거르나: 이 팩은 지상 동작과 공중 동작이 따로 있는데 이름이 그것을 말해 주지 않는다
//   (AttackFlameAnim 은 공중 브레스처럼 들리지만 발을 딛고 뿜는 지상 동작이다). 상태마다 옳은 이름을
//   고르는 것만으로는 부족하다 — 대체 이름 표가 못 찾은 이름을 비슷한 것으로 바꿔 주기 때문에,
//   한 번 잘못 고르면 조용히 지상 동작이 공중에서 재생된다. 마지막에 한 번 더 거른다.
// 착지 동작(Landing)은 "땅 바로 위에서 내려앉는 중(Descend)" 에만 공중에서 허용한다. 전에는 Land 로
//   시작하면 언제든 통과시켰는데, 착지 동작의 끝은 **땅에 앉은 자세**라 높은 하늘에서 틀면 그대로
//   허공에 앉는다.
// 막았을 때 경고를 남기는 이유: 그냥 무시하면 "왜 아무 동작도 안 나오지" 로 또 헤맨다.
//   모델을 바꿔도 이 줄이 뜨면 어떤 이름이 공중에서 거부됐는지 바로 보인다.
float APGDragonBoss::PlayAnim(const TCHAR* Name, bool bLoop)
{
	if (!IsValid(Mesh) || !Mesh->GetSkeletalMeshAsset())
	{
		if (!bWarnedNoMesh) // 동작마다 찍으면 로그가 넘친다. 한 번이면 원인은 보인다
		{
			bWarnedNoMesh = true;
			UE_LOG(LogPGObjects, Warning, TEXT("PGDragon: can't play %s — no mesh (further anim requests are skipped silently)"), Name);
		}
		return 0.0f;
	}
	FString AnimDir = DragonAnimFolder.Path;
	if (!AnimDir.IsEmpty() && !AnimDir.EndsWith(TEXT("/")))
		AnimDir += TEXT("/");
	UAnimSequence* Anim = LoadDragonAnim(AnimDir, Name);
	if (!Anim)
		return 0.0f; // LoadDragonAnim 이 이미 경고를 찍었다
	const FString Found = Anim->GetName();
	const bool bFlyAnim = Found.StartsWith(TEXT("Fly"));
	const bool bTakeOffAnim = Found.StartsWith(TEXT("TakeOff")) || Found.StartsWith(TEXT("Takeoff"));
	const bool bLandingAnim = Found.StartsWith(TEXT("Land"));
	// 마지막 한 방을 맞은 순간(Dying 의 Struck 단계)만은 지상용 피격·포효 동작을 공중에서 허용한다.
	//   이 드래곤에는 공중 피격 동작이 없다. 그 1초 남짓 동안 몸이 젖혀지고 흔들리고 있어서 발을 짚는 자세로는
	//   안 읽히고, "맞고 비명을 지른다" 는 그림이 훨씬 중요하다(9/21 사용자: "너무 맥없이 떨어진다").
	const bool bDeathStrike = State == EPGDragonState::Dying && DeathStep == EPGDragonDeathStep::Struck;
	const bool bAllowedInAir = bFlyAnim || bTakeOffAnim || (bLandingAnim && State == EPGDragonState::Descend) || bDeathStrike;
	if (IsAirborne() && !bAllowedInAir)
	{
		UE_LOG(LogPGObjects, Warning,
			TEXT("PGDragon: %s resolved to %s which is a ground animation — not playing it in the air (state %s)"),
			Name, *Found, StateName(State));
		return 0.0f; // 틀던 나는 동작을 그대로 둔다. 허공에서 발을 짚는 것보다 낫다.
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: anim %s -> %s (loop=%d, %.1fs, state %s, airborne=%d)"),
		Name, *Found, bLoop ? 1 : 0, Anim->GetPlayLength(), StateName(State), IsAirborne() ? 1 : 0);
	Mesh->PlayAnimation(Anim, bLoop);
	if (HasAuthority()) // 모두의 화면에 같은 동작(OnRep_AnimCue)
	{
		AnimCue.Anim = Anim;
		AnimCue.bLoop = bLoop;
		++AnimCue.Seq;
	}
	return Anim->GetPlayLength();
}

void APGDragonBoss::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGDragonBoss, AnimCue);
	DOREPLIFETIME(APGDragonBoss, BreathCue);
}

void APGDragonBoss::SetBreath(bool bOn, const FVector& From, const FVector& Dir, float Length)
{
	if (HasAuthority())
	{
		BreathCue.bOn = bOn;
		if (bOn)
		{
			BreathCue.From = From;
			BreathCue.Dir = Dir;
			BreathCue.Length = Length;
		}
	}
	if (GetNetMode() != NM_DedicatedServer && AirCombat)
		AirCombat->SetBreathFx(bOn, From, Dir, Length);
}

void APGDragonBoss::OnRep_BreathCue()
{
	if (AirCombat)
		AirCombat->SetBreathFx(BreathCue.bOn, BreathCue.From, BreathCue.Dir, BreathCue.Length);
	if (BreathCue.bOn != bBreathShownLocal) // 켜지고 꺼질 때만 한 줄(매 프레임 값이 바뀌므로)
	{
		bBreathShownLocal = BreathCue.bOn;
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: breath %s on this screen"), BreathCue.bOn ? TEXT("ON") : TEXT("off"));
	}
}

void APGDragonBoss::PlayImpactFx(const FVector& At, float Scale)
{
	if (HasAuthority())
		MulticastImpactFx(At, Scale);
}

void APGDragonBoss::MulticastImpactFx_Implementation(FVector_NetQuantize At, float Scale)
{
	if (GetNetMode() != NM_DedicatedServer && ImpactFxAsset)
		UGameplayStatics::SpawnEmitterAtLocation(this, ImpactFxAsset, At, FRotator::ZeroRotator, FVector(Scale), true, EPSCPoolMethod::AutoRelease);
}

void APGDragonBoss::MulticastCrashFx_Implementation(FVector_NetQuantize Feet, uint8 Wave)
{
	if (GetNetMode() != NM_DedicatedServer && Death)
		Death->PlayCrashFx(Feet, Wave);
}

void APGDragonBoss::OnRep_AnimCue()
{
	// 서버가 이미 "공중에서 지상 동작 금지" 같은 판단을 끝낸 동작이다 — 그대로 튼다.
	if (IsValid(Mesh) && Mesh->GetSkeletalMeshAsset() && IsValid(AnimCue.Anim))
	{
		Mesh->PlayAnimation(AnimCue.Anim, AnimCue.bLoop);
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: anim on this screen — %s%s"), *AnimCue.Anim->GetName(), AnimCue.bLoop ? TEXT(" (loop)") : TEXT(""));
	}
}

void APGDragonBoss::EnterState(EPGDragonState NewState)
{
	const EPGDragonState OldState = State;
	const float OldTimer = StateTimer;          // Landed → GroundFight 에서 진행 중인 동작의 시각을 옮겨 적는 데 쓴다
	const bool bOldGroundFlame = bGroundFlame;  // 같은 이유. 불뿜기 도중이었으면 이어서 뿜는다
	State = NewState;
	StateTimer = 0.0f;
	bAttackSwung = false;
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: state %s -> %s (hp %.0f/%.0f)"),
		StateName(OldState), StateName(NewState), Health, MaxHealth);
	// 불꽃은 브레스·땅 위 불뿜기 동안에만. 상태가 바뀌면 무조건 끈다 — 끄는 곳을 한 군데로 모아야
	// "브레스 도중 체력 50% 로 내려앉기" 같은 끼어들기에서 불이 켜진 채로 남지 않는다.
	SetBreath(false);
	bGroundFlame = false;
	bWalking = false;
	bIdling = false;
	switch (NewState)
	{
	case EPGDragonState::Rising:
		bFlapping = false;
		PlayAnim(TEXT("TakeOffAnim"), false);
		break;
	case EPGDragonState::Orbit:
		bFlapping = true;
		bGliding = false;
		LastGlideSwap = 0.0f;
		PlayAnim(TEXT("FlyForwardAnim"), true); // 반복 재생 — 도는 내내 날갯짓한다
		if (FightStartTime < 0.0 && IsValid(GetWorld()))
			FightStartTime = GetWorld()->GetTimeSeconds();
		break;
	case EPGDragonState::Pass:
		// 1초 예고 뒤에 덮친다. 예고가 없으면 피할 수가 없다(기획서 13절).
		// 예고에 ScreamAnim(포효)을 썼는데 그건 **지상 동작**이라 공중에서 발을 짚는다. 공중에 뜬 채로
		// 제자리 날갯짓(FlyIdleAnim)을 하는 것이 "덤벼들기 직전" 으로도 읽힌다.
		PlayAnim(TEXT("FlyIdleAnim"), true);
		bPassDamaged = false;
		bFlapping = false;
		PassClosest = MAX_flt; // 이번 돌진의 최소 거리를 새로 잰다
		bPassLeft = !bPassLeft;
		// 돌진 목표를 배 **너머 옆**으로 잡는다.
		//
		// 왜: 배 한가운데를 겨누면 안전거리(484m)에서 멈춰 그 자리에 떠 있게 된다. 그림이 "돌진"이 아니라
		//   "다가와서 정지"다. 배를 옆으로 스쳐 지나가는 선을 그어 두면 멈출 필요 없이 지나가고,
		//   안전거리는 선 자체가 지켜 준다(옆으로 벌린 양이 곧 안전거리다).
		if (IsValid(Ship))
		{
			const FVector ShipCentre = Ship->GetActorLocation();
			const float Keep = KeepDistanceCm(Ship);
			FVector Approach = (ShipCentre - GetActorLocation()).GetSafeNormal2D();
			if (Approach.IsNearlyZero())
				Approach = GetActorForwardVector().GetSafeNormal2D();
			// 배를 지나쳐 계속 날아가는 지점 + 옆으로 안전거리만큼. 좌우는 번갈아 — 매번 같은 쪽이면 지루하다.
			const FVector Side = FVector::CrossProduct(Approach, FVector::UpVector) * (bPassLeft ? 1.0f : -1.0f);
			PassTarget = ShipCentre + Approach * Keep * 1.2f + Side * Keep * 1.1f;
			PassTarget.Z = FMath::Max(PassTarget.Z, MinAirborneZ()); // 배가 땅 가까이 있어도 땅속으로 파고들며 돌진하지 않게(9/23)
		}
		else
		{
			PassTarget = GetActorLocation();
		}
		break;
	case EPGDragonState::Breath:
	{
		// 예고: 제자리 날갯짓하며 표적 쪽으로 몸을 돌린다. 불은 예고가 끝나고 사거리 안에 들면 나간다.
		bFlapping = false;
		bBreathing = false;
		BreathTickCount = BreathShipHits = BreathMonsterHits = BreathPlayerHits = 0;
		PlayAnim(TEXT("FlyIdleAnim"), true);
		// 불길에 닿을 수 있는 몸들을 **지금 한 번만** 모은다. 판정할 때마다 맵의 몬스터를 전부 훑으면
		// 0.2초마다 수백 개를 도는 셈이다. 2~3초짜리 브레스 동안 새로 생긴 몬스터는 못 태우지만 티가 안 난다.
		BreathCandidates.Reset();
		UWorld* World = GetWorld();
		const FVector Here = GetActorLocation();
		const float Gather = BreathRangeCm * 2.0f + HalfWingCm; // 브레스 중에 다가갈 수도 있어 넉넉히
		if (IsValid(World))
		{
			for (TActorIterator<APGMonsterCharacter> It(World); It; ++It)
				if (!It->IsDead() && FVector::DistSquared(It->GetActorLocation(), Here) < FMath::Square(Gather))
					BreathCandidates.Add(*It);
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
				if (APlayerController* PC = It->Get(); IsValid(PC) && IsValid(PC->GetPawn()))
					BreathCandidates.Add(PC->GetPawn());
		}
		// 전함 크기도 한 번만 잰다(부품이 수백 개라 매번 재면 비싸다). 전함 기준 좌표라 배가 돌아도 맞다.
		// [9/23 수정] 배 몸통(선체 부품)만 잰 상자를 쓴다. 전에는 배에 붙은 모든 부품을 쟀는데, 숨겨 둔 주포 빔(하늘에 쏘면 1500m)까지
		//   들어가 "배 길이 1537m" 가 되었고, 브레스가 그 빔 끝(자기 몸 근처)을 겨눴다.
		ShipLocalBox = IsValid(Ship) ? Ship->GetHullLocalBounds() : FBox(ForceInit);
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: breath — target %s, %d bodies gathered, ship box %s"),
			*GetNameSafe(AttackTarget.Get()), BreathCandidates.Num(),
			ShipLocalBox.IsValid ? *FString::Printf(TEXT("%.0f m long"), ShipLocalBox.GetSize().GetMax() * 0.01f) : TEXT("none"));
		break;
	}
	case EPGDragonState::Descend:
		// 내려앉기. 전에는 여기가 없었다 — 체력 50% 가 되는 순간 TakeDamage 가 드래곤을 **땅으로 순간이동**
		// 시키고 착지 동작을 틀었다. 두 가지가 틀렸다:
		//   1) 순간이동 높이가 발이 아니라 캡슐 기준이라 발이 땅에서 47m 떠 있었다(FeetOffsetCm 주석).
		//   2) 아래로 쏜 선(600m)이 땅을 못 찾으면 아예 안 움직였다 — 하늘 한가운데서 착지·포효를 틀었다.
		// 이제는 진짜로 내려온다: 땅 높이를 먼저 재고, 활공으로 내려오다가 발이 땅 가까이 오면 착지 동작,
		//   발이 땅에 닿는 순간에 Landed 로 넘어간다. 땅을 못 찾으면 내려앉지 않는다.
		bLandingAnimStarted = false;
		DescendRate = 0.0f;
		bFlapping = false;
		LastGroundProbe = 0.0f;
		{
			// 땅은 **앉을 자리**에서 잰다. 자리가 정해져 있으면(지상전·뛰어 옮기기) 그쪽, 아니면 지금 자리.
			const FVector Here = GetActorLocation();
			const FVector Probe = bDescendToSpot ? FVector(DescendSpot.X, DescendSpot.Y, Here.Z) : Here;
			if (!GroundCombat->FindGroundZ(Probe, GroundZ))
			{
				if (!bGroundFight)
				{
					UE_LOG(LogPGObjects, Display, TEXT("PGDragon: no ground under %s — not landing, back to circling"),
						*Probe.ToCompactString());
					AttacksSinceLanding = 0; // 안 그러면 다음 공격 차례에 또 내려앉으려다 또 실패한다
					bDescendToSpot = false;
					EnterState(EPGDragonState::Orbit);
					break;
				}
				// 지상전에서는 선회로 돌아가는 길이 없다. 자리는 맵 안쪽으로 잘라 두었으니(ClampToMap) 기준 높이(20)로 친다.
				GroundZ = GroundDatumZ;
				UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — no ground under %s (mountains have no collision), landing on the map floor z=%.0f"),
					*Probe.ToCompactString(), GroundDatumZ);
			}
			PlayAnim(TEXT("FlyGlideAnim"), true);
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: descending — feet are %.0f m above the ground%s"),
				(Here.Z - FeetOffsetCm() - GroundZ) * 0.01f,
				bDescendToSpot ? *FString::Printf(TEXT(", flying %.0f m to (%.0f, %.0f) m first"),
					FVector2D::Distance(FVector2D(Here.X, Here.Y), DescendSpot) * 0.01f, DescendSpot.X * 0.01f, DescendSpot.Y * 0.01f) : TEXT(""));
		}
		break;
	case EPGDragonState::Landed:
	{
		// 발이 땅에 닿았다. 착지 동작의 남은 부분이 끝나면 포효 → 지면 공격을 되풀이한다(TickLanded).
		AttacksSinceLanding = 0;
		LandedStep = 0;
		PendingGroundHit = -1.0f;
		NextLandedAction = LandingTailSeconds;
		GroundCombat->OnTouchdown();
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: landed at %s — weak point for %.0fs (damage x%.1f)"),
			*(GetActorLocation() - FVector(0.0f, 0.0f, FeetOffsetCm())).ToCompactString(), WeakPointSeconds, WeakPointMultiplier);
		break;
	}
	case EPGDragonState::TakeOff:
	{
		// 이륙 동작(웅크렸다 뛰어오른다)을 틀고, 뛰어오른 뒤부터 몸을 띄운다.
		// 전에는 땅에서 곧장 선회로 넘겨서, 선 채로 나는 동작을 틀며 미끄러지듯 떠올랐다.
		bLifted = false;
		bFlapping = false;
		AttackAnimSeconds = PlayAnim(TEXT("TakeOffAnim"), false);
		if (bHopping)
		{
			// 지상전의 뛰어 옮기기: 배 높이가 아니라 몸 높이의 절반쯤만 뜨고, 옮길 자리 쪽으로 비스듬히 간다.
			// 도착하면 TickTakeOff 가 선회가 아니라 내려앉기(Descend)로 넘긴다.
			TakeOffTo = FVector(HopSpot.X, HopSpot.Y, GetActorLocation().Z + BodyHeightCm * GroundHopHeightRatio);
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — hopping %.0f m across, %.0f m up"),
				FVector2D::Distance(FVector2D(GetActorLocation().X, GetActorLocation().Y), HopSpot) * 0.01f,
				(TakeOffTo.Z - GetActorLocation().Z) * 0.01f);
			break;
		}
		const float ShipZ = IsValid(Ship) ? Ship->GetActorLocation().Z : GetActorLocation().Z + 30000.0f;
		TakeOffTo = FVector(GetActorLocation().X, GetActorLocation().Y, FMath::Max(ShipZ + 3000.0f, GetActorLocation().Z + 10000.0f));
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: taking off, climbing %.0f m"), (TakeOffTo.Z - GetActorLocation().Z) * 0.01f);
		break;
	}
	case EPGDragonState::GroundFight:
	{
		// 지상전. 여기 오는 길은 둘: 내려앉기(Descend)에서 발이 닿았거나, 앉아 있던(Landed) 중에 전함이 추락했거나.
		bHopping = false;
		bDescendToSpot = false;
		AttackTarget = nullptr;
		LastGroundProbe = -10.0f; // 걷기(WalkToward)가 이 상태의 시계로 땅을 다시 잰다. 옛 상태의 값이 남으면 첫 몇 초를 안 잰다
		if (OldState == EPGDragonState::Landed)
		{
			// 앉아서 동작을 하던 중이면 그 동작을 잇는다. 시각은 옛 상태 시계 기준이라 새 시계(0)로 옮겨 적는다.
			NextLandedAction = FMath::Max(NextLandedAction - OldTimer, 0.0f);
			PendingGroundHit = PendingGroundHit >= 0.0f ? FMath::Max(PendingGroundHit - OldTimer, 0.0f) : -1.0f;
			bGroundFlame = bOldGroundFlame;
		}
		else
		{
			// 발이 막 닿았다. 착지 동작의 남은 부분 뒤에 포효(LandedStep 0)부터.
			LandedStep = 0;
			PendingGroundHit = -1.0f;
			NextLandedAction = LandingTailSeconds;
			GroundCombat->OnTouchdown();
		}
		if (IsValid(GetWorld()))
			LastHopWorldTime = GetWorld()->GetTimeSeconds(); // 내려앉자마자 또 뛰지 않게
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — on the ground at %s (from %s). Guns x%.1f here, x%.1f in the air; walking %.0f m/s, hop when farther than %.0f m"),
			*(GetActorLocation() - FVector(0.0f, 0.0f, FeetOffsetCm())).ToCompactString(), StateName(OldState),
			GroundGunDamageMultiplier, AirGunDamageMultiplier, GroundWalkSpeed * 0.01f, GroundHopDistanceCm * 0.01f);
		break;
	}
	case EPGDragonState::Dying:
	{
		// 죽음. 단계는 TickDying 위 주석 참고. 여기서는 첫 단계(Struck: 맞고 흔들리기)만 연다.
		//
		// 옛 두 판의 문제: (1) 체력 0 이 되는 순간 하늘에서 죽는 동작을 틀어 허공에 드러누운 채 내려왔고,
		//   (2) 그다음 판은 활공 자세로 곧게 떨어졌는데 "너무 맥없이" 떨어졌고, 땅 찾기가 실패하면
		//   원점 아래 2km 를 땅으로 쳐서 산 밑으로 꺼졌다(9/21 사용자). 산은 충돌이 없다 — 실패하면 기준 높이(20)다.
		bOnGround = false;
		FallSpeed = 0.0f;
		DeathStep = EPGDragonDeathStep::Struck;
		DeathJitter = FVector::ZeroVector;
		DeathDrift = FVector::ZeroVector;
		CrashDustWave = 0;
		bDeathOutsideMap = false;
		LastGroundProbe = -10.0f;
		const FVector Here = GetActorLocation();
		if (!GroundCombat->FindGroundZ(Here, GroundZ))
		{
			GroundZ = GroundDatumZ;
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death — no ground under %s (mountains have no collision), using the map floor z=%.0f"),
				*Here.ToCompactString(), GroundDatumZ);
		}
		const double Fought = (FightStartTime >= 0.0 && IsValid(GetWorld())) ? GetWorld()->TimeSince(FightStartTime) : 0.0;
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: down after %.0fs of fighting (average %.0f damage/s taken, max hp %.0f)"),
			Fought, Fought > 0.0 ? MaxHealth / Fought : 0.0, MaxHealth);
		if (Here.Z - FeetOffsetCm() - GroundZ < 500.0f)
		{
			// 땅에서 맞아 죽었다(내려앉은 동안). 처박힐 높이가 없으니 그 자리에서 쓰러진다.
			Death->CrashLand(false);
			break;
		}
		// 마지막 한 방이 날아온 반대쪽으로 튕겨 나간다. 어디서 맞았는지 모르면(파편 등) 뒤로.
		FVector Away = (Here - LastHitFrom).GetSafeNormal2D();
		if (Away.IsNearlyZero())
			Away = -GetActorForwardVector().GetSafeNormal2D();
		DeathDrift = Away * 3000.0f;
		PlayAnim(*DeathStruckAnimName, false); // 기본 FlyIdleAnim(공중 동작). 지상 동작을 넣어도 Struck 단계에서는 허용된다(PlayAnim 주석)
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death struck at %s — %.0f m above the ground, reeling for %.1fs before the fall"),
			*Here.ToCompactString(), (Here.Z - FeetOffsetCm() - GroundZ) * 0.01f, DeathStrikeSeconds);
		break;
	}
	}
}

void APGDragonBoss::FaceAndMove(const FVector& Target, float Speed, float DeltaSeconds)
{
	const FVector To = Target - GetActorLocation();
	const float Distance = To.Size();
	if (Distance < 1.0f)
		return; // 이미 도착. 매 프레임 올 수 있는 길이라 로그는 안 남긴다
	const FVector Dir = To / Distance;
	// 도는 속도를 제한한다. 즉시 방향을 바꾸면 40m 짜리가 미끄러지듯 움직여 무게가 사라진다.
	const FRotator Want = Dir.Rotation();
	const FRotator Next = FMath::RInterpTo(GetActorRotation(), FRotator(Want.Pitch * 0.5f, Want.Yaw, 0.0f), DeltaSeconds, 1.6f);
	SetActorLocationAndRotation(GetActorLocation() + Dir * FMath::Min(Speed * DeltaSeconds, Distance), Next);
}

void APGDragonBoss::FaceToward(const FVector& Target, float DeltaSeconds)
{
	const FVector To = (Target - GetActorLocation()).GetSafeNormal2D();
	if (To.IsNearlyZero())
		return; // 바로 위·아래. 돌 방향이 없다(매 프레임 길이라 로그 없음)
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), FRotator(0.0f, To.Rotation().Yaw, 0.0f), DeltaSeconds, 2.0f));
}

// Where 둘레의 소품·타일 조각을 날린다. 프레임당 몇 개씩만 — 한꺼번에 털면 프레임이 죽는다(전함 이륙과 같은 규칙).
void APGDragonBoss::KnockAround(const FVector& Where, float Reach)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->TimeSince(LastKnockTime) < 0.15)
		return;
	LastKnockTime = World->GetTimeSeconds();
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDragonRise), false, this);
	World->OverlapMultiByObjectType(Overlaps, Where, FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(Reach), Params);
	int32 Budget = 5;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Budget <= 0)
			break;
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (!IsValid(Component))
			continue;
		FHitResult Hit;
		Hit.Item = Overlap.ItemIndex; // 안 채우면 맵 어디에 있든 0번 인스턴스가 뜯긴다(9/20 조사)
		Hit.ImpactPoint = Component->GetComponentLocation();
		Hit.Location = Hit.ImpactPoint;
		Hit.Component = Component;
		Hit.HitObjectHandle = FActorInstanceHandle(Component->GetOwner());
		const FVector Away = (Hit.ImpactPoint - Where).GetSafeNormal2D() * 1400.0f + FVector(0.0f, 0.0f, 1800.0f);
		if (PGPhysicsUtil::TryKnockProp(Component, Hit, Away, this, 1200.0f, 250.0f))
			--Budget;
	}
}

FVector2D APGDragonBoss::ClampToMap(const FVector2D& Want, float InsetCm) const
{
	FBox2D Map;
	if (!ResolveMapBox(Map))
		Map = FallbackMapBox;
	const FVector2D Centre = Map.GetCenter();
	const FVector2D Extent = Map.GetExtent();
	const FVector2D Safe(FMath::Max(Extent.X - InsetCm, 0.0f), FMath::Max(Extent.Y - InsetCm, 0.0f));
	return FVector2D(FMath::Clamp(Want.X, Centre.X - Safe.X, Centre.X + Safe.X), FMath::Clamp(Want.Y, Centre.Y - Safe.Y, Centre.Y + Safe.Y));
}

// 타일이 깔린 범위. 디렉터(ResolveMapExtent)와 같은 방법 — 만들어진 지점들은 맵 전체에 골고루 퍼져 있어서
// 그 상자가 곧 타일 범위다(조금 안쪽이라 오히려 안전하다). 타일 배치 배열 자체는 private 이라 못 읽는다.
bool APGDragonBoss::ResolveMapBox(FBox2D& OutBox) const
{
	const IPGMapInfo* Design = UPGMapInfoSubsystem::FindMap(this);
	if (!Design || Design->GetLevelDesignPoints().IsEmpty())
		return false;
	FBox2D Box(ForceInit);
	for (const FLevelDesignPoint& Point : Design->GetLevelDesignPoints())
		Box += FVector2D(Point.WorldLocation.X, Point.WorldLocation.Y);
	OutBox = Box;
	return true;
}

void APGDragonBoss::PostNetReceiveLocationAndRotation()
{
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		const FRepMovement& Rep = GetReplicatedMovement();
		ProxySmoother.Receive(FRepMovement::RebaseOntoLocalOrigin(Rep.Location, this), Rep.Rotation, GetWorld()->GetTimeSeconds());
		return;
	}
	Super::PostNetReceiveLocationAndRotation();
}

void APGDragonBoss::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
		ProxySmoother.Step(this, GetWorld()->GetTimeSeconds(), DeltaSeconds); // 멀티(9/28): 받은 위치로 부드럽게
	// 클라이언트는 위치만 복제받는다. 매 프레임 지나는 길이라 로그는 안 남긴다(남기면 로그가 이것으로 덮인다).
	if (!HasAuthority())
	{
		if (Death)
			Death->TickCrashBlastLocal(); // 멀티(9/27): 처박힐 때 폭발 구가 이 화면에서도 부풀었다 사라지게
		return;
	}
	StateTimer += DeltaSeconds;
	// 전함이 추락하면(체력 0) 지상전. 상태 Tick 보다 먼저 본다 — 이번 틱부터 내려가기 시작하게.
	if (!bGroundFight && GroundCombat->CheckShipDown())
		return;
	switch (State)
	{
	case EPGDragonState::Rising:  AirCombat->TickRise(DeltaSeconds); break;
	case EPGDragonState::Orbit:   AirCombat->TickOrbit(DeltaSeconds); break;
	case EPGDragonState::Pass:    AirCombat->TickPass(DeltaSeconds); break;
	case EPGDragonState::Breath:  AirCombat->TickBreath(DeltaSeconds); break;
	case EPGDragonState::Descend: GroundCombat->TickDescend(DeltaSeconds); break;
	case EPGDragonState::Landed:  GroundCombat->TickLanded(DeltaSeconds); break;
	case EPGDragonState::TakeOff: GroundCombat->TickTakeOff(DeltaSeconds); break;
	case EPGDragonState::Dying:   Death->TickDying(DeltaSeconds); break;
	case EPGDragonState::GroundFight: GroundCombat->TickGroundFight(DeltaSeconds); break;
	}
	// 이 자리에서 마지막으로 거리를 지킨다. 상태별 Tick 이 어디로 옮겨 놓았든, 그리기 전에 밖으로 되돌린다.
	// 지상전에서는 안 건다 — 전함은 추락한 잔해이고, 뛰어 옮기는 몸을 494m 밖으로 밀면 맵 밖(산)으로 나간다.
	if (IsAirborne() && !bGroundFight)
	{
		KeepClearOfShip();
		// 죽으며 떨어질 때는 건드리지 않는다 — 추락 궤적·떨어지는 자리는 TickDying 이 정한다.
		if (State != EPGDragonState::Dying)
			KeepBodyOutOfHull();
		// 마지막 안전망: 공중에서는 최저 높이 밑으로 못 간다(9/23 "드래곤이 땅·산 밑을 난다").
		// 내려앉는 중(Descend)·추락(Dying)은 일부러 땅으로 가는 것이라 뺀다. 이륙(TakeOff)도 땅에서 올라가는 중이라 뺀다 —
		//   걸면 발을 떼는 순간 9m → 44m 로 순간이동했다(9/23 시험).
		if (State != EPGDragonState::Dying && State != EPGDragonState::Descend && State != EPGDragonState::TakeOff)
		{
			const FVector Here = GetActorLocation();
			if (Here.Z < MinAirborneZ())
			{
				SetActorLocation(FVector(Here.X, Here.Y, MinAirborneZ()));
				if (!bFloorClampLogged)
				{
					bFloorClampLogged = true;
					UE_LOG(LogPGObjects, Display, TEXT("PGDragon: held above the floor — origin was %.0f m, lifted to %.0f m (state %s)"),
						Here.Z * 0.01f, MinAirborneZ() * 0.01f, StateName(State));
				}
			}
		}
	}
}

// 보이는 몸이 선체를 파고들지 않게 하는 두 번째 안전망(9/22 사용자: "드래곤이 전함을 뚫고 지나간다").
//
// 왜 KeepClearOfShip 만으로는 모자라나: 그쪽은 "액터 원점 ↔ 배 중심" 거리만 본다. 그런데 이 드래곤의 액터 원점은
//   발밑 근처이고, 보이는 몸통은 그보다 한참 위에 있다(배율 25 에서 약 200m). 원점은 안전거리 밖인데 날개와 몸통은
//   배 안에 들어가 있을 수 있었다. 그래서 여기서는 "메시 상자(보이는 몸)" 와 "선체 상자" 를 직접 맞대 본다.
//
// 왜 블루프린트에 보이지 않는 충돌 상자를 달아서 막지 않나(사용자 질문): 드래곤은 물리로 움직이지 않고
//   코드가 SetActorLocation 으로 자리를 정한다(sweep 없이). 그래서 충돌체가 있어도 그냥 통과한다 — 막는 것은 이 코드뿐이다.
//
// 어떻게: 드래곤 메시 상자의 여덟 꼭짓점을 배 기준 좌표로 옮겨 상자를 만들고, 선체 상자(여유 15m)와 겹치는지 본다.
//   겹치면 세 축(배의 앞뒤·좌우·위아래) 중 가장 조금 밀면 빠지는 쪽으로 민다. 선회·돌진·브레스 목표는 그대로 두고
//   이번 틱 자리만 고치므로, 상태 기계("먼저 배를 노린다" 포함)는 그대로 돌고 배 바깥에서 돌며 공격하게 된다.
void APGDragonBoss::KeepBodyOutOfHull()
{
	constexpr float DragonHullMarginCm = 1500.0f;
	const USkeletalMesh* Asset = IsValid(Mesh) ? Mesh->GetSkeletalMeshAsset() : nullptr;
	if (!IsValid(Ship) || !Asset)
		return;
	UWorld* World = GetWorld();
	// 선체 상자: 처음 한 번 + 5초마다 다시 잰다. 전함 부품은 스폰 1초 뒤에 붙어서, 너무 일찍 재면 작은 상자가 나온다.
	if (!HullAvoidBox.IsValid || (IsValid(World) && World->TimeSince(HullBoxMeasuredAt) > 5.0))
	{
		// [9/23 수정] 선체 부품만 잰 상자. 전에는 배의 모든 부품을 재서 숨긴 주포 빔(1500m)까지 들어갔고, 그 거대한 상자에서
		//   빠져나오려고 드래곤이 아래로 200m 넘게 밀려 땅·산 밑을 날았다(9/23 로그: 기준점 Z -225m, "ship box 1537 m long").
		HullAvoidBox = Ship->GetHullLocalBounds();
		HullBoxMeasuredAt = IsValid(World) ? World->GetTimeSeconds() : 0.0;
	}
	if (!HullAvoidBox.IsValid)
		return;

	// 보이는 몸 = 메시 에셋의 상자(쉬는 자세)를 지금 메시 자리·방향·배율로 옮긴 것. 컴포넌트 경계는 깜빡임 방지로 3배 부풀려 둬서 못 쓴다.
	const FTransform& ShipTransform = Ship->GetActorTransform();
	const FTransform& MeshTransform = Mesh->GetComponentTransform();
	const FBoxSphereBounds MeshBounds = Asset->GetBounds();
	const FBox MeshBox(MeshBounds.Origin - MeshBounds.BoxExtent, MeshBounds.Origin + MeshBounds.BoxExtent);
	FBox BodyBox(ForceInit);
	for (int32 Corner = 0; Corner < 8; ++Corner)
	{
		const FVector Local((Corner & 1) ? MeshBox.Max.X : MeshBox.Min.X, (Corner & 2) ? MeshBox.Max.Y : MeshBox.Min.Y, (Corner & 4) ? MeshBox.Max.Z : MeshBox.Min.Z);
		BodyBox += ShipTransform.InverseTransformPosition(MeshTransform.TransformPosition(Local));
	}
	const FBox HullZone = HullAvoidBox.ExpandBy(DragonHullMarginCm / FMath::Max(ShipTransform.GetScale3D().GetMax(), KINDA_SMALL_NUMBER));
	if (!HullZone.Intersect(BodyBox))
	{
		bHullAvoidActive = false;
		return;
	}
	// 축마다 "몸을 어느 쪽으로 얼마나 밀면 빠지나" 를 재고 가장 짧은 것을 고른다. 방향은 몸 가운데가 선체 가운데의 어느 쪽에 있나로 정한다.
	const FVector BodyCentre = BodyBox.GetCenter();
	const FVector HullCentre = HullZone.GetCenter();
	FVector Push = FVector::ZeroVector;
	float Best = TNumericLimits<float>::Max();
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const bool bPositive = BodyCentre[Axis] >= HullCentre[Axis];
		const float Need = bPositive ? static_cast<float>(HullZone.Max[Axis] - BodyBox.Min[Axis]) : static_cast<float>(BodyBox.Max[Axis] - HullZone.Min[Axis]);
		// 아래로 밀면 최저 높이 밑으로 가는 경우는 고르지 않는다 — 곧바로 최저 높이로 다시 올려져 선체 안에 끼인다(9/23).
		if (Axis == 2 && !bPositive && GetActorLocation().Z - Need < MinAirborneZ())
			continue;
		if (Need < Best)
		{
			Best = Need;
			Push = FVector::ZeroVector;
			Push[Axis] = bPositive ? Need : -Need;
		}
	}
	SetActorLocation(GetActorLocation() + ShipTransform.TransformVector(Push));
	// 한 번 붙을 때 한 번만 남긴다(빠져나갔다가 다시 붙으면 또 찍는다). 매 틱 찍으면 로그가 이것으로 덮인다.
	if (!bHullAvoidActive)
	{
		bHullAvoidActive = true;
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: body touched the ship hull (+%.0f m margin) — pushed out %.0f m (state %s)"),
			DragonHullMarginCm * 0.01f, ShipTransform.TransformVector(Push).Size() * 0.01f, StateName(State));
	}
}

// 안전거리 안으로 들어와 있으면 배 중심에서 바깥으로 밀어낸다.
//
// 왜 상태별 Tick 이 아니라 여기서 하나: 전에는 돌진(TickPass)에서만 걸었다. 그런데 **선회 중에도**
//   거리가 깨진다 — 선회는 목표점만 안전거리 위에 두고 그쪽으로 날아갈 뿐이라, 가는 도중이나
//   목표가 움직이는 동안에는 안쪽에 들어가 있을 수 있다. 실제로 사용자가 "함선 내부로 날개가 뚫고
//   들어와 날갯짓한다" 고 했다(9/21). 상태를 하나 늘릴 때마다 안전망을 다시 거는 대신 여기 한 곳에 둔다.
//
// 배가 움직이는 것도 여기서 흡수된다. 전함은 이제 조종할 수 있어서(18m/s) 플레이어가 드래곤 쪽으로
//   몰고 갈 수 있다. 드래곤이 가만히 있어도 배가 다가오면 거리가 깨지는데, 매 틱 재서 밀어내면
//   "배가 밀고 들어오면 드래곤이 물러난다" 가 된다. 드래곤만 피하게 하는 것이라 배 조종감은 안 바뀐다.
//
// 그리기 전에 되돌리므로 한 프레임도 겹쳐 보이지 않는다(Tick 은 렌더보다 먼저다).
void APGDragonBoss::KeepClearOfShip()
{
	if (!IsValid(Ship))
		return; // 배가 없으면 지킬 거리도 없다(매 프레임 길이라 로그 없음)
	const FVector Centre = Ship->GetActorLocation();
	const float Keep = KeepDistanceCm(Ship);
	FVector Offset = GetActorLocation() - Centre;
	const float Now = Offset.Size();
	if (Now >= Keep)
		return;
	if (Now <= 1.0f)
	{
		// 정확히 겹쳐 있으면 밀 방향이 없다. 아무 방향으로나 빼낸다.
		Offset = FVector(1.0f, 0.0f, 0.0f);
		SetActorLocation(Centre + Offset * Keep);
		return;
	}
	SetActorLocation(Centre + Offset / Now * Keep);
	// 얼마나 파고들었는지 남긴다. 매 틱 찍으면 로그가 넘치므로 1초에 한 번만.
	UWorld* World = GetWorld();
	if (IsValid(World) && World->TimeSince(LastClampLogTime) > 1.0)
	{
		LastClampLogTime = World->GetTimeSeconds();
		UE_LOG(LogPGObjects, Display,
			TEXT("PGDragon: pushed back out — was %.0f m from the ship, keep is %.0f m (state %s)"),
			Now * 0.01f, Keep * 0.01f, StateName(State));
	}
}

float APGDragonBoss::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (State == EPGDragonState::Dying)
		return 0.0f; // 이미 떨어지는 중. 파편이 계속 맞으니 로그는 안 남긴다
	float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	if (State == EPGDragonState::Landed)
		Applied *= WeakPointMultiplier; // 약점: 내려앉아 있는 동안
	// 지상 무기(총·차·탱크) 배율. 9/21 사용자: "지면에서 총 맞으면 추가 딜, 공중에서는 안 아프게".
	//
	// 무엇을 지상 무기로 보나: 피해 원인(DamageCauser)이 전함이 아닌 것. 전함 주포는 원인이 전함 자신이고,
	//   미사일도 전함이 쏘므로(PGMissileSubsystem 의 Shooter = 전함) 원인이 전함이다. 나머지 중 원인이 폰이거나
	//   (플레이어 캐릭터의 총 = PGWeaponComponent 의 ApplyPointDamage, 차·탱크의 들이받기·포) 지시자가 플레이어
	//   컨트롤러면 지상 무기다. 그 밖(잔해·폭발 소품 등)은 배율 없이 그대로 — 소품이 x6 으로 들어가면 안 된다.
	// 주포의 "5발에 격추" 는 여기 안 걸린다(원인이 전함).
	const bool bShipWeapon = IsValid(DamageCauser) && (DamageCauser == Ship.Get() || DamageCauser->IsA<APGBattleshipActor>());
	const bool bGroundWeapon = !bShipWeapon
		&& (Cast<APawn>(DamageCauser) != nullptr || (IsValid(EventInstigator) && EventInstigator->IsPlayerController()));
	if (bGroundWeapon)
	{
		const bool bOnTheGround = GroundCombat->IsOnGroundForGuns();
		const float Multiplier = bOnTheGround ? GroundGunDamageMultiplier : AirGunDamageMultiplier;
		const float Before = Applied;
		Applied *= Multiplier;
		// 총알은 초당 예닐곱 발이라 매번 찍으면 로그가 넘친다. 0.5초에 한 번.
		if (UWorld* World = GetWorld(); IsValid(World) && World->TimeSince(LastGunLogTime) > 0.5)
		{
			LastGunLogTime = World->GetTimeSeconds();
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: gun hit x%.1f (%s) — %.0f -> %.0f from %s, state %s, hp %.0f/%.0f"),
				Multiplier, bOnTheGround ? TEXT("on the ground") : TEXT("in the air"), Before, Applied,
				*GetNameSafe(DamageCauser), StateName(State), FMath::Max(0.0f, Health - Applied), MaxHealth);
		}
	}
	// 누가 얼마나 때렸나(플레이어별). 죽을 때 가장 많이 때린 사람이 전리품을 받는다(9/23). 전함 주포·미사일은 원인이 배지만
	//   지시자가 쏜 사람의 컨트롤러라 그 사람 몫으로 들어간다. 지시자가 없으면 원인 액터의 주인 컨트롤러로 본다.
	{
		AController* Credit = IsValid(EventInstigator) ? EventInstigator : (IsValid(DamageCauser) ? DamageCauser->GetInstigatorController() : nullptr);
		if (IsValid(Credit) && Credit->IsPlayerController())
			DamageByController.FindOrAdd(Credit) += FMath::Min(Applied, Health);
	}
	Health = FMath::Max(0.0f, Health - Applied);
	// 체력 흐름을 2초에 한 번 남긴다. 주포·미사일이 실제로 얼마나 들어가는지 봐야 MaxHealth 를 맞출 수 있다.
	if (UWorld* World = GetWorld(); IsValid(World) && World->TimeSince(LastHealthLogTime) > 2.0)
	{
		LastHealthLogTime = World->GetTimeSeconds();
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: hp %.0f/%.0f (%.0f%%) — last hit %.0f from %s, state %s"),
			Health, MaxHealth, Health / FMath::Max(MaxHealth, 1.0f) * 100.0f, Applied, *GetNameSafe(DamageCauser), StateName(State));
	}
	if (Health <= 0.0f)
	{
		// 마지막 한 방이 어디서 왔나 — 죽는 연출이 그 반대쪽으로 튕긴다. 폭발 피해는 원인 액터가 미사일·배라
		//   대체로 배 쪽이고, 그것도 없으면 배 자리로 친다.
		LastHitFrom = IsValid(DamageCauser) ? DamageCauser->GetActorLocation()
			: (IsValid(Ship) ? Ship->GetActorLocation() : GetActorLocation() + GetActorForwardVector() * 1000.0f);
		EnterState(EPGDragonState::Dying);
		// 판 기록(드래곤 격추). 스코어보드가 "드래곤 격추" 를 따로 보여 준다.
		UPGRunSubsystem::NotifyDragonKilled(this, EventInstigator);
		AwardTopDamageDealer();
		return Applied;
	}
	if (!bUsedWeakPoint && !bGroundFight && Health <= MaxHealth * 0.5f)
	{
		// 절반이 깎이면 한 번은 차례와 상관없이 내려앉는다 — 이때가 반격할 기회다.
		// 이미 내려앉는 중이거나 앉아 있으면 그것으로 친다. 솟는 중·이륙 중에는 다음 번에. 지상전이면 이미 땅이라 뜻이 없다.
		if (State == EPGDragonState::Landed || State == EPGDragonState::Descend)
		{
			bUsedWeakPoint = true;
		}
		else if (State == EPGDragonState::Orbit || State == EPGDragonState::Pass || State == EPGDragonState::Breath)
		{
			bUsedWeakPoint = true;
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: half health — going down now"));
			EnterState(EPGDragonState::Descend);
		}
	}
	return Applied;
}
