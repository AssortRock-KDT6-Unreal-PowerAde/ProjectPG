// 타이틀 인트로 ② 구출: 전함·드래곤(게임 후반의 볼거리)을 미리 보여 주지 않는 쪽(사용자 9/22: "나오면 스포일러라 재미없다").
//
// 시간표(초):
//   0.0  왼쪽 멀리서 선인장 몹이 달려오고, 주인공이 소총을 들고 오른쪽에서 뛰어 들어온다.
//   1.2  멈춰 서서 허리 사격 세 발(1.35 · 1.65 · 1.95) — 세 번째에 몹이 뒤로 튕겨 쓰러진다.
//   2.2  뒤(화면 오른쪽 멀리)에서 두 마리가 더 달려온다. 주인공이 돌아서는데 탄창이 비어 장전(3.5) — 그사이 코앞까지 온다(위기).
//        카메라가 낮게 붙고 화각이 좁아지며 화면이 살짝 기운다.
//   4.1  두 마리가 덤벼드는 순간 — 하늘에서 날으는 변신 차가 내리꽂으며 빔 두 방(4.55, 4.85). 몹이 날아가 쓰러진다.
//   5.3  차가 주인공 머리 위를 지나 크게 돌아서 7.7 주인공 옆에 내려앉는다(부스터는 착지 전에 접힌다).
//   8.1  차가 여고생으로 돌아온다(게임의 "차 → 여고생" 역변신 그대로).
//   9.9  주인공이 카메라 쪽으로 돌아서고, 카메라가 낮게 밀고 들어와 평소 타이틀 구도에서 멈춘다(12초).
// 12.0 둘이 나란히 선 그림 → 0.4초에 까맣게 → 그사이 몹·차·여고생·소총을 치움 → 0.6초에 걷히면 평소 타이틀(주인공 혼자).
//      여고생은 인트로에만 나온다(9/22 사용자). 건너뛰어도 같은 평소 타이틀로 간다.
//
// 주인공 소총 동작을 어떻게 입히나:
//   소총 동작(PG/Animations/Sequences 의 AS_*_Rifle_*)은 UE5 마네킹 뼈대(SK_Mannequin)용이고 주인공은 Quantum 뼈대다. 그대로는 못 튼다.
//   그런데 두 뼈대는 뼈 이름이 똑같다(root·pelvis·spine_01~05·neck_01~02·clavicle·upperarm·hand_r… 에셋에서 확인).
//   그래서 보이지 않는 마네킹(SKM_Manny)에 소총 동작을 틀고, 주인공 몸은 그 포즈를 "뼈 이름으로" 따라가게 한다(SetLeaderPoseComponent —
//   게임의 옷·장비가 몸을 따라가는 것과 같은 기능). 리타기터 에셋을 새로 만들 필요가 없다.
//   마네킹 메시·동작이 없는 PC 에서는 Quantum 달리기·대기로 돌고, 몹은 같은 시각에 쓰러진다.
#include "Flow/PGTitleIntro.h"
#include "Flow/PGFlowStageSet.h"
#include "Flow/PGTitleIntroSet.h"

#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Monster/PGMonsterCharacter.h"
#include "Objects/PGTransformNPCActor.h"
#include "Particles/ParticleSystem.h"
#include "Vehicle/PGFlightKitComponent.h"

namespace PGIntroRescueLocal
{
	constexpr float IntroRescueDuration = 12.0f;

	// 주인공은 무대(DA_PGFlowStage)가 세운 캐릭터를 빌린다. 동작·마네킹·총·총구 불꽃·차 에셋은 데이터 에셋 DA_PGTitleIntro
	//   (없으면 원래 코드 에셋)에 있다(9/23 블루프린트 분리). 아래는 에셋이 아니라 "어떻게 드나" 값이라 코드에 둔다.
	// 총 들기 값: 게임의 소총 AK(UPGWeaponComponent 의 Rifle_AK)와 같다(요 -90 으로 총구를 앞으로, 0.8배, 손에서 앞 2·위 3cm).
	const FRotator IntroRifleRotation(0.0f, -90.0f, 0.0f);
	constexpr float IntroRifleScale = 0.8f;
	const FVector IntroRifleHold(2.0f, 0.0f, 3.0f);
	const FName IntroRifleHandSocket(TEXT("hand_r"));

	// 여고생(차)이 내려앉는 자리. 주인공(원점) 오른쪽 뒤 — 타이틀 구도에서 주인공 오른쪽에 나란히 보인다.
	//   차 몸(길이 약 4.5m, 폭 1.9m)이 주인공과 25cm 이상 떨어지고, 무대의 바위(120,-210)·돌무더기(-200,-380)를 밟지 않는 자리.
	const FVector IntroGirlSpot(-170.0f, -195.0f, 0.0f);
	// 여고생 메시는 +X 를 본다(가져오기 스크립트가 T 포즈 팔 방향으로 확인). 요 0 = 카메라(+X) 쪽을 본다. 차도 같은 방향으로 내려앉는다.
	constexpr float IntroGirlYaw = 0.0f;

	// 주인공 자리(무대 원점 기준). 오른쪽(-Y) 화면 밖에서 뛰어 들어와 원점 가까이에서 멈춰 쏜다.
	const FVector IntroHeroStart(0.0f, -420.0f, 0.0f);
	const FVector IntroHeroStop(0.0f, -40.0f, 0.0f);
	constexpr float IntroHeroStopAt = 1.2f;
	const float IntroShotTimes[] = { 1.35f, 1.65f, 1.95f };
	constexpr float IntroReloadAt = 3.5f;
	constexpr float IntroTracerSeconds = 0.06f;

	constexpr float IntroBoosterRetractAt = 6.9f;
	constexpr float IntroCarLandAt = 7.7f;
	constexpr float IntroGirlAt = 8.1f;     // 부스터가 다 접힌 뒤(접기 1.17초) 여고생으로 바꾼다 — 부스터가 튀어 사라지지 않게
	constexpr float IntroBeamSeconds = 0.14f;

	float EaseOut(float A) { return 1.0f - FMath::Square(1.0f - FMath::Clamp(A, 0.0f, 1.0f)); }
}

APGTitleIntroRescue::APGTitleIntroRescue()
{
	using namespace PGIntroRescueLocal;
	// 차가 날아오는 길. 오른쪽 하늘 멀리서 내리꽂아(빔) → 주인공 머리 위를 지나 → 왼쪽에서 크게 돌아 → +X 로 들어와 내려앉는다.
	CarPath = {
		{ 3.3f, FVector(-1800.0f, -4200.0f, 2600.0f) },
		{ 4.4f, FVector(-700.0f, -1700.0f, 900.0f) },
		{ 5.3f, FVector(-150.0f, 300.0f, 520.0f) },
		{ 6.2f, FVector(-650.0f, 950.0f, 430.0f) },
		{ 7.0f, FVector(-1000.0f, -120.0f, 230.0f) },
		{ IntroCarLandAt, IntroGirlSpot },
	};

	// 카메라 컷(위치, 보는 점, 화각, 기울기). 무대 카메라는 -X 를 보고, 화면 왼쪽 = +Y · 오른쪽 = -Y 다.
	// 0초: 앞에서 넓게. 왼쪽 화면 밖에서 몹이, 오른쪽 화면 밖에서 주인공이 들어온다.
	CamKeys.Add({ 0.0f, FVector(640.0f, 60.0f, 150.0f), FVector(0.0f, 60.0f, 95.0f), 58.0f, 0.0f });
	// 1.6~2.4초: 쏘는 주인공(오른쪽)과 쓰러지는 몹(왼쪽)이 한 화면에 들어오게 가운데를 둘 사이로 옮긴다.
	CamKeys.Add({ 1.6f, FVector(600.0f, 150.0f, 130.0f), FVector(0.0f, 150.0f, 100.0f), 55.0f, 0.0f });
	CamKeys.Add({ 2.4f, FVector(520.0f, 80.0f, 120.0f), FVector(-40.0f, 120.0f, 100.0f), 55.0f, 0.0f });
	// 3.3~4.3초: 위기. 주인공 왼쪽 뒤로 낮게 붙어 달려오는 두 마리를 그의 어깨 너머로 본다. 화각을 좁히고 화면을 살짝 비튼다.
	CamKeys.Add({ 3.3f, FVector(330.0f, 190.0f, 75.0f), FVector(-40.0f, -260.0f, 110.0f), 50.0f, 0.0f });
	CamKeys.Add({ 4.3f, FVector(270.0f, 150.0f, 60.0f), FVector(-40.0f, -220.0f, 125.0f), 40.0f, 7.0f });
	// 4.9초: 빔이 떨어진 쪽(하늘)으로 고개를 든다.
	CamKeys.Add({ 4.9f, FVector(300.0f, 200.0f, 75.0f), FVector(-250.0f, -500.0f, 330.0f), 50.0f, 3.0f });
	// 5.5~7.1초: 뒤로 물러나며 머리 위를 지나 도는 차를 따라간다(보는 점 = 그 시각의 차 자리). 가까이 두면 차가 화면을 스쳐 지나가 버린다.
	CamKeys.Add({ 5.5f, FVector(650.0f, 250.0f, 170.0f), EvalPath(CarPath, 5.5f), 58.0f, 0.0f });
	CamKeys.Add({ 6.3f, FVector(750.0f, 100.0f, 200.0f), EvalPath(CarPath, 6.3f), 58.0f, 0.0f });
	CamKeys.Add({ 7.1f, FVector(750.0f, -60.0f, 180.0f), FMath::Lerp(EvalPath(CarPath, 7.1f), IntroGirlSpot, 0.4f), 55.0f, 0.0f });
	// 7.9초: 내려앉은 차를 한 발 떨어져서(풀 덤불이 앞을 가리지 않게 조금 높이) → 8.9초 여고생으로 돌아오는 것을 가까이.
	CamKeys.Add({ 7.9f, FVector(620.0f, -120.0f, 150.0f), FVector(-170.0f, -195.0f, 60.0f), 50.0f, 0.0f });
	CamKeys.Add({ 8.9f, FVector(480.0f, -200.0f, 135.0f), FVector(-170.0f, -195.0f, 100.0f), 45.0f, 0.0f });
	// 10.2초 → 12초: 둘을 같이 담으며 낮게 밀고 들어와 평소 타이틀 구도(뼈대가 자동으로 붙인다)에서 멈춘다.
	CamKeys.Add({ 10.2f, FVector(400.0f, -90.0f, 100.0f), FVector(-70.0f, -90.0f, 90.0f), 52.0f, 0.0f });
	ShotTimes = { 0.8f, 1.4f, 1.97f, 2.4f, 3.6f, 4.3f, 4.6f, 5.6f, 6.8f, 7.8f, 8.8f, 10.3f, 11.6f, 12.2f };
}

float APGTitleIntroRescue::GetDuration() const
{
	return PGIntroRescueLocal::IntroRescueDuration;
}

void APGTitleIntroRescue::BeginShots()
{
	using namespace PGIntroRescueLocal;
	FindStageHero();
	SetupRifleHero();

	// 몹 셋: 게임의 A 세력(작은 몹 떼) 프리셋을 그대로 쓴다. AI·루팅이 붙은 진짜 몬스터가 아니라 겉모습(메시·애니)만 빌린 인형이다 —
	//   진짜 몬스터를 세우면 AI 가 제멋대로 움직여 시간표를 못 지킨다.
	struct FMonsterPlan
	{
		const TCHAR* Preset;
		FVector From, To;
		float RunStart, RunEnd, AttackAt, HitAt;
		FVector FlyTo;
		float FlyHeight, Spin;
	};
	const FMonsterPlan Plans[] = {
		// 첫 놈: 왼쪽 멀리서 달려오다 세 번째 총알(1.95초)에 뒤로 튕겨 쓰러진다. 떨어지는 자리는 타이틀 화면 밖.
		{ TEXT("Cactus"), FVector(-60.0f, 950.0f, 0.0f), FVector(-30.0f, 330.0f, 0.0f), 0.2f, 1.95f, -1.0f, IntroShotTimes[2],
			FVector(-120.0f, 560.0f, 0.0f), 45.0f, 30.0f },
		// 둘째·셋째: 주인공 등 뒤(오른쪽 멀리)에서 덮친다. 덤벼드는 순간 차 빔에 맞는다.
		{ TEXT("Beholder"), FVector(-380.0f, -1350.0f, 0.0f), FVector(-70.0f, -195.0f, 0.0f), 2.2f, 4.2f, 4.05f, 4.55f,
			FVector(-380.0f, -950.0f, 0.0f), 250.0f, -260.0f },
		{ TEXT("ChestMonster"), FVector(260.0f, -1250.0f, 0.0f), FVector(75.0f, -180.0f, 0.0f), 2.35f, 4.25f, 4.12f, 4.85f,
			FVector(360.0f, -900.0f, 0.0f), 230.0f, 300.0f },
	};
	for (const FMonsterPlan& Plan : Plans)
	{
		FPGIntroMonster Monster;
		Monster.From = Plan.From;
		Monster.To = Plan.To;
		Monster.RunStart = Plan.RunStart;
		Monster.RunEnd = Plan.RunEnd;
		Monster.AttackAt = Plan.AttackAt;
		Monster.HitAt = Plan.HitAt;
		Monster.FlyTo = Plan.FlyTo;
		Monster.FlyHeight = Plan.FlyHeight;
		Monster.FlySpinYaw = Plan.Spin;
		SpawnIntroMonster(Plan.Preset, Monster);
	}
	SpawnCar();
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: rescue cast ready — hero %s, rifle anims %s, %d monster(s), car %s"),
		Hero.IsValid() ? TEXT("found") : TEXT("MISSING"), IsValid(RifleDriver) ? TEXT("on (mannequin leader pose)") : TEXT("off (Quantum run/idle)"),
		Monsters.Num(), IsValid(Car) ? TEXT("ok") : TEXT("MISSING"));
}

void APGTitleIntroRescue::FindStageHero()
{
	using namespace PGIntroRescueLocal;
	// 주인공: 무대가 세운 캐릭터를 찾아 빌린다. 새로 만들지 않는 이유 — 끝에 "진짜 무대 주인공" 과 바꿔치기하면 한 프레임 튄다.
	//   메시 이름은 무대 에셋(DA_PGFlowStage)의 주인공 칸에서 읽는다 — 무대 주인공을 바꿔도 인트로가 따라간다.
	const FString HeroMeshName = UPGFlowStageSet::GetActive()->HeroMesh.GetAssetName();
	for (TActorIterator<ASkeletalMeshActor> It(GetWorld()); It; ++It)
	{
		const USkeletalMeshComponent* Body = It->GetSkeletalMeshComponent();
		if (Body && Body->GetSkeletalMeshAsset() && Body->GetSkeletalMeshAsset()->GetName() == HeroMeshName)
		{
			Hero = *It;
			HeroHome = It->GetActorTransform();
			break;
		}
	}
	const UPGTitleIntroSet* Set = UPGTitleIntroSet::GetActive();
	HeroIdle = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->HeroIdle));
	HeroRun = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->HeroRun));
}

bool APGTitleIntroRescue::SpawnIntroMonster(const TCHAR* Preset, const FPGIntroMonster& Plan)
{
	FPGMonsterVisuals Visuals;
	if (!APGMonsterCharacter::GetPresetVisuals(Preset, Visuals))
		return false;
	USkeletalMesh* Mesh = Visuals.Mesh.IsNull() ? nullptr : LoadIntroAsset<USkeletalMesh>(*Visuals.Mesh.ToSoftObjectPath().ToString());
	if (!Mesh)
		return false;
	FPGIntroMonster Monster = Plan;
	Monster.MeshYaw = Visuals.MeshRotation.Yaw;
	Monster.Run = Visuals.Run.IsNull() ? nullptr : LoadIntroAsset<UAnimSequence>(*Visuals.Run.ToSoftObjectPath().ToString());
	Monster.Attack = Visuals.Attack.IsNull() ? nullptr : LoadIntroAsset<UAnimSequence>(*Visuals.Attack.ToSoftObjectPath().ToString());
	Monster.Die = Visuals.Die.IsNull() ? nullptr : LoadIntroAsset<UAnimSequence>(*Visuals.Die.ToSoftObjectPath().ToString());
	const float Facing = (Plan.To - Plan.From).Rotation().Yaw;
	ASkeletalMeshActor* Actor = SpawnSkeletal(Mesh, FTransform(FRotator(0.0f, Facing + Monster.MeshYaw, 0.0f), Plan.From));
	if (!Actor)
		return false;
	Actor->GetSkeletalMeshComponent()->SetBoundsScale(2.0f); // 쓰러지는 동작이 경계 밖으로 나가 깜빡이지 않게
	Monster.Actor = Actor;
	SpawnedActors.Add(Actor);
	Monsters.Add(Monster);
	return true;
}

void APGTitleIntroRescue::SetupRifleHero()
{
	using namespace PGIntroRescueLocal;
	ASkeletalMeshActor* HeroActor = Hero.Get();
	const UPGTitleIntroSet* Set = UPGTitleIntroSet::GetActive();
	USkeletalMesh* Manny = LoadIntroAsset<USkeletalMesh>(*IntroPath(Set->RifleMannequin));
	RifleJog = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->RifleJog));
	RifleIdle = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->RifleIdle));
	RifleFire = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->RifleFire));
	RifleReload = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->RifleReload));
	if (!HeroActor || !Manny || !RifleIdle || !RifleFire)
	{
		UE_LOG(LogTemp, Warning, TEXT("PGTitleIntro: rifle animations unavailable (hero %d, manny %d, idle %d, fire %d) — Quantum run/idle instead"),
			HeroActor != nullptr, Manny != nullptr, RifleIdle != nullptr, RifleFire != nullptr);
		return;
	}
	// 보이지 않는 마네킹. 주인공에 붙여 같이 다니게 한다(포즈는 뼈 이름으로 복사되니 자리는 상관없지만, 경계·컬링이 엉뚱한 데서 계산되지 않게).
	RifleDriver = SpawnSkeletal(Manny, HeroActor->GetActorTransform());
	if (!IsValid(RifleDriver))
		return;
	RifleDriver->AttachToActor(HeroActor, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	RifleDriver->SetActorHiddenInGame(true);
	USkeletalMeshComponent* HeroBody = HeroActor->GetSkeletalMeshComponent();
	// 주인공 몸이 마네킹 포즈를 따라간다. 두 뼈대의 뼈 이름이 같아서 리타기터 없이 맞는다(파일 머리 설명).
	HeroBody->SetLeaderPoseComponent(RifleDriver->GetSkeletalMeshComponent(), true);

	// 소총: 게임의 AK 와 같은 메시·들기 값. 매 틱 오른손 자리에 "몸 방향 기준" 으로 놓는다(UPGWeaponComponent 의 몸 기준 들기와 같은 방식 —
	//   Quantum 뼈대에는 총 소켓(weapon_r)이 없고, 손목 방향을 따르면 사격 동작 중 총구가 흔들려 엉뚱한 데를 겨눈다).
	if (UStaticMesh* RifleMesh = LoadIntroAsset<UStaticMesh>(*IntroPath(Set->RifleMesh)))
	{
		Rifle = NewObject<UStaticMeshComponent>(this, TEXT("IntroRifle"));
		Rifle->SetStaticMesh(RifleMesh);
		Rifle->SetMobility(EComponentMobility::Movable);
		Rifle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Rifle->SetCanEverAffectNavigation(false);
		Rifle->RegisterComponent();
		Rifle->SetWorldScale3D(FVector(IntroRifleScale));
		// 총구 = 메시에서 총신 방향(원본 +Y) 끝, 높이·옆은 가운데. AK 원본은 총구가 +Y 를 본다(무기 컴포넌트 주석).
		const FBox Box = RifleMesh->GetBoundingBox();
		RifleMuzzleLocal = FVector(Box.GetCenter().X, Box.Max.Y, Box.GetCenter().Z + Box.GetExtent().Z * 0.35f);
	}
	Tracer = AddBeamComponent(this);
}

// ---- 매 틱 ----

void APGTitleIntroRescue::TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation)
{
	TickHero(T);
	for (FPGIntroMonster& Monster : Monsters)
		TickMonster(Monster, T);
	TickCar(T, DeltaSeconds);
	TickShieldFade(DeltaSeconds);

	// 흔들림: 첫 몹 쓰러짐(1.95), 빔 두 방(4.55, 4.85), 착지(7.7). 맞는 순간 세게, 금방 잦아든다.
	float Kick = 0.0f;
	for (const float At : { 1.95f, 4.55f, 4.85f, 7.7f })
		if (T >= At)
			Kick += FMath::Exp(-(T - At) * 7.0f) * (At == 4.55f || At == 4.85f ? 1.0f : 0.5f);
	OutShakeLocation = FVector(FMath::Sin(T * 61.0f), FMath::Sin(T * 47.0f + 1.0f), FMath::Sin(T * 53.0f + 2.0f)) * 6.0f * Kick;
	OutShakeRotation = FRotator(FMath::Sin(T * 43.0f) * 0.7f, FMath::Sin(T * 37.0f + 0.5f) * 0.4f, FMath::Sin(T * 51.0f + 1.5f) * 0.6f) * Kick;
}

void APGTitleIntroRescue::PlayHero(UAnimSequence* Anim, bool bLoop)
{
	ASkeletalMeshActor* HeroActor = Hero.Get();
	if (!HeroActor || !Anim || HeroPlaying == Anim)
		return;
	HeroPlaying = Anim;
	HeroActor->GetSkeletalMeshComponent()->PlayAnimation(Anim, bLoop);
}

void APGTitleIntroRescue::PlayRifle(UAnimSequence* Anim, bool bLoop, float T)
{
	if (!IsValid(RifleDriver) || !Anim)
		return;
	RiflePlaying = Anim;
	RifleDriver->GetSkeletalMeshComponent()->PlayAnimation(Anim, bLoop);
	RifleAnimEnd = bLoop ? TNumericLimits<float>::Max() : T + Anim->GetPlayLength();
}

void APGTitleIntroRescue::SetHeroPose(const FVector& Location, float FacingYaw)
{
	// 이 메시는 +Y 를 보고 있어서 액터 요 = 바라볼 방향 - 90(무대가 -90 으로 카메라를 보게 세우는 것과 같은 규칙).
	HeroFacing = FacingYaw;
	if (ASkeletalMeshActor* HeroActor = Hero.Get())
		HeroActor->SetActorLocationAndRotation(HeroHome.GetLocation() + Location, FRotator(0.0f, FacingYaw - 90.0f, 0.0f));
}

void APGTitleIntroRescue::TickHero(float T)
{
	using namespace PGIntroRescueLocal;
	if (!Hero.IsValid())
		return;
	// 자리: 오른쪽 화면 밖에서 원점 가까이까지 뛰어 들어와(0~1.2초) 멈춘다. 끝에(9.9~10.5초) 원점으로 반 걸음.
	FVector Location = FMath::Lerp(IntroHeroStart, IntroHeroStop, FMath::Clamp(T / IntroHeroStopAt, 0.0f, 1.0f));
	// 바라보는 방향: 왼쪽(+Y, 첫 몹) → 2.9~3.5초 카메라 쪽을 지나 오른쪽(-Y, 뒤에서 오는 두 마리)으로 → 9.9~10.5초 카메라(+X).
	float Facing = 90.0f;
	if (T >= 2.9f)
		Facing = FMath::Lerp(90.0f, -90.0f, FMath::SmoothStep(2.9f, 3.5f, T));
	if (T >= 9.9f)
	{
		const float A = FMath::SmoothStep(9.9f, 10.5f, T);
		Facing = FMath::Lerp(-90.0f, 0.0f, A);
		Location = FMath::Lerp(IntroHeroStop, FVector::ZeroVector, A);
	}
	SetHeroPose(Location, Facing);

	if (!IsValid(RifleDriver))
	{
		// 소총 동작이 없는 PC: Quantum 달리기 → 대기. 몹은 같은 시각에 쓰러진다.
		PlayHero(T < IntroHeroStopAt ? HeroRun : HeroIdle, true);
		return;
	}
	if (T < IntroHeroStopAt)
	{
		if (RiflePlaying != RifleJog)
			PlayRifle(RifleJog ? RifleJog : RifleIdle, true, T);
	}
	else
	{
		// 세 발. 쏠 때마다 사격 동작을 처음부터 다시 튼다(반동이 한 발씩 보인다).
		while (ShotsFired < static_cast<int32>(UE_ARRAY_COUNT(IntroShotTimes)) && T >= IntroShotTimes[ShotsFired])
		{
			PlayRifle(RifleFire, false, T);
			FireRifle(T);
			++ShotsFired;
		}
		// 뒤에서 두 마리 — 돌아서며 장전. 이 동작 동안은 못 쏜다(그래서 위기다).
		if (!bReloadStarted && T >= IntroReloadAt)
		{
			bReloadStarted = true;
			PlayRifle(RifleReload, false, T);
		}
		if (RiflePlaying == RifleJog || (T >= RifleAnimEnd && RiflePlaying != RifleIdle))
			PlayRifle(RifleIdle, true, T);
	}
	UpdateRifle();
	if (TracerHideAt >= 0.0f && T >= TracerHideAt && IsValid(Tracer))
	{
		Tracer->SetVisibility(false);
		TracerHideAt = -1.0f;
	}
}

void APGTitleIntroRescue::UpdateRifle()
{
	using namespace PGIntroRescueLocal;
	ASkeletalMeshActor* HeroActor = Hero.Get();
	if (!IsValid(Rifle) || !HeroActor)
		return;
	const USkeletalMeshComponent* Body = HeroActor->GetSkeletalMeshComponent();
	if (!Body->DoesSocketExist(IntroRifleHandSocket))
		return;
	// 위치 = 오른손 + 몸 방향으로 돌린 보정, 방향 = 몸 정면 + 총 메시 회전(게임 무기 컴포넌트의 몸 기준 들기와 같은 식).
	const FRotator BodyYaw(0.0f, HeroFacing, 0.0f);
	const FVector Hand = Body->GetSocketLocation(IntroRifleHandSocket);
	Rifle->SetWorldLocationAndRotation(Hand + BodyYaw.RotateVector(IntroRifleHold), BodyYaw.Quaternion() * IntroRifleRotation.Quaternion());
}

void APGTitleIntroRescue::FireRifle(float T, int32 TargetIndex)
{
	using namespace PGIntroRescueLocal;
	if (!IsValid(Rifle) || !Monsters.IsValidIndex(TargetIndex))
		return;
	UpdateRifle(); // 이번 틱의 손 자리에서 쏜다
	const FVector Muzzle = Rifle->GetComponentTransform().TransformPosition(RifleMuzzleLocal);
	if (UParticleSystem* Flash = LoadIntroAsset<UParticleSystem>(*IntroPath(UPGTitleIntroSet::GetActive()->MuzzleFlash)))
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Flash, Muzzle, Rifle->GetComponentRotation(), FVector(1.0f), true);
	// 총알 궤적: 가는 빛줄기를 아주 잠깐. 겨눈 몹 가슴께로.
	if (const ASkeletalMeshActor* Target = Monsters[TargetIndex].Actor.Get())
	{
		ShowBeam(Tracer, Muzzle, Target->GetActorLocation() + FVector(0.0f, 0.0f, 90.0f), 0.08f);
		TracerHideAt = T + IntroTracerSeconds;
	}
}

void APGTitleIntroRescue::TickMonster(FPGIntroMonster& Monster, float T)
{
	ASkeletalMeshActor* Actor = Monster.Actor.Get();
	if (!Actor || Monster.bGone)
		return;
	USkeletalMeshComponent* Body = Actor->GetSkeletalMeshComponent();
	const float Facing = (Monster.To - Monster.From).Rotation().Yaw;
	if (!Monster.bHit && T >= Monster.HitAt)
	{
		Monster.bHit = true;
		if (Monster.Die)
			Body->PlayAnimation(Monster.Die, false);
		// 소총에 맞아 쓰러질 때는 작은 "맞음" 효과만. 예전에는 보스가 땅을 내리찍는 흙먼지 폭발(P_Rampage_Rock_HitWorld)을 써서
		// 소총 세 발에 땅이 터진 것처럼 보였다(9/22 사용자: "바주카포 쏜 것도 아니고"). 흙먼지 폭발은 변신차 빔 장면에만 둔다.
		SpawnHitSpark(Actor->GetActorLocation() + FVector(0.0f, 0.0f, 80.0f), 0.35f);
	}
	if (!Monster.bHit)
	{
		const float A = FMath::Clamp((T - Monster.RunStart) / FMath::Max(Monster.RunEnd - Monster.RunStart, 0.01f), 0.0f, 1.0f);
		if (Monster.AttackAt >= 0.0f && T >= Monster.AttackAt)
		{
			if (!Monster.bAttacked && Monster.Attack)
				Body->PlayAnimation(Monster.Attack, false);
			Monster.bAttacked = true;
		}
		else if (!Monster.bRunning && Monster.Run)
		{
			Body->PlayAnimation(Monster.Run, true);
			Monster.bRunning = true;
		}
		Actor->SetActorLocationAndRotation(FMath::Lerp(Monster.From, Monster.To, A), FRotator(0.0f, Facing + Monster.MeshYaw, 0.0f));
		return;
	}
	// 맞은 뒤: 0.8초 동안 포물선으로 날아가 떨어지며 돈다 → 2.4초 쓰러져 있다가 → 1.2초에 걸쳐 땅속으로 가라앉아 사라진다.
	//   타이틀 구도에 시체가 남지 않게(떨어지는 자리도 타이틀 화면 밖이다).
	const float Since = T - Monster.HitAt;
	const float Fly = FMath::Clamp(Since / 0.8f, 0.0f, 1.0f);
	FVector Location = FMath::Lerp(Monster.To, Monster.FlyTo, PGIntroRescueLocal::EaseOut(Fly))
		+ FVector(0.0f, 0.0f, Monster.FlyHeight * 4.0f * Fly * (1.0f - Fly));
	const float Sink = FMath::Clamp((Since - 2.4f) / 1.2f, 0.0f, 1.0f);
	Location.Z -= 180.0f * Sink;
	Actor->SetActorLocationAndRotation(Location, FRotator(0.0f, Facing + Monster.MeshYaw + Monster.FlySpinYaw * PGIntroRescueLocal::EaseOut(Fly), 0.0f));
	if (Sink >= 1.0f)
	{
		Actor->SetActorHiddenInGame(true);
		Monster.bGone = true;
	}
}

void APGTitleIntroRescue::SpawnCar()
{
	using namespace PGIntroRescueLocal;
	USkeletalMesh* CarMesh = LoadIntroAsset<USkeletalMesh>(*IntroPath(UPGTitleIntroSet::GetActive()->CarMesh));
	if (!CarMesh)
		return;
	// 물리 차(APGVehiclePawn)가 아니라 겉모습만. 바퀴 물리는 공중 곡예를 시간표대로 못 따라온다.
	Car = SpawnSkeletal(CarMesh, FTransform(FRotator::ZeroRotator, EvalPath(CarPath, 0.0f)));
	if (!IsValid(Car))
		return;
	SpawnedActors.Add(Car);
	USkeletalMeshComponent* Body = Car->GetSkeletalMeshComponent();
	Body->SetBoundsScale(2.0f);
	// 변신 여고생의 차 색(남색 + 빨간 줄). 값은 여고생 액터의 기본값을 그대로 읽는다 — 색을 바꾸면 여기도 같이 바뀐다.
	const APGTransformNPCActor* GirlDefaults = Cast<APGTransformNPCActor>(APGTransformNPCActor::GetSpawnClass()->GetDefaultObject());
	if (UMaterialInterface* Navy = GirlDefaults->VehicleBodyMaterial.IsNull() ? nullptr
		: LoadIntroAsset<UMaterialInterface>(*GirlDefaults->VehicleBodyMaterial.ToSoftObjectPath().ToString()))
		Body->SetMaterialByName(GirlDefaults->VehicleBodySlot, Navy);

	// 로켓 부스터: 게임의 비행 키트(UPGFlightKitComponent)와 같은 메시·애니·불꽃. 값도 그 기본값에서 읽는다.
	const UPGFlightKitComponent* Kit = UPGFlightKitComponent::GetSpawnClass()->GetDefaultObject<UPGFlightKitComponent>();
	USkeletalMesh* BoosterMesh = Kit->BoosterMesh.IsNull() ? nullptr : LoadIntroAsset<USkeletalMesh>(*Kit->BoosterMesh.ToSoftObjectPath().ToString());
	if (BoosterMesh)
	{
		// 부스터 키트는 차와 같은 공간으로 만들어져 차 메시 원점에 그대로 붙이면 제자리다(비행 키트 주석).
		Booster = NewObject<USkeletalMeshComponent>(Car, TEXT("IntroBooster"));
		Booster->SetSkeletalMeshAsset(BoosterMesh);
		Booster->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Booster->SetupAttachment(Body);
		Booster->bComponentUseFixedSkelBounds = true;
		Booster->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Booster->RegisterComponent();
		// 날아오는 동안은 펼친 자세: 펼치기 애니의 마지막 프레임에 세워 둔다.
		if (UAnimSequence* Deploy = Kit->DeployAnim.IsNull() ? nullptr : LoadIntroAsset<UAnimSequence>(*Kit->DeployAnim.ToSoftObjectPath().ToString()))
		{
			Booster->PlayAnimation(Deploy, false);
			Booster->SetPosition(Deploy->GetPlayLength(), false);
		}
		BoosterRetract = Kit->RetractAnim.IsNull() ? nullptr : LoadIntroAsset<UAnimSequence>(*Kit->RetractAnim.ToSoftObjectPath().ToString());
		if (UStaticMesh* FlameMesh = Kit->FlameMesh.IsNull() ? nullptr : LoadIntroAsset<UStaticMesh>(*Kit->FlameMesh.ToSoftObjectPath().ToString()))
		{
			for (const TCHAR* Socket : { TEXT("ThrustL"), TEXT("ThrustR") })
			{
				UStaticMeshComponent* Flame = NewObject<UStaticMeshComponent>(Car);
				Flame->SetStaticMesh(FlameMesh);
				Flame->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Flame->SetCastShadow(false);
				Flame->SetupAttachment(Booster, Socket);
				// 노즐 뼈가 요 -90 으로 누워 있어 +90 돌려야 불꽃이 뒤(-X)로 나간다(비행 키트와 같은 보정).
				Flame->SetRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
				Flame->RegisterComponent();
				Flames.Add(Flame);
			}
		}
	}
	CarBeam = AddBeamComponent(Car);
}

void APGTitleIntroRescue::TickCar(float T, float DeltaSeconds)
{
	using namespace PGIntroRescueLocal;
	if (!IsValid(Car))
		return;
	// 3.3초 전에는 하늘 멀리(첫 점)에서 기다린다 — 화면 밖이다.
	const FVector Location = EvalPath(CarPath, T);
	FRotator Rotation = PathVelocity(CarPath, T).Rotation();
	Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -20.0f, 20.0f);
	if (bCarYawValid && DeltaSeconds > KINDA_SMALL_NUMBER)
	{
		const float YawRate = FMath::FindDeltaAngleDegrees(LastCarYaw, Rotation.Yaw) / DeltaSeconds;
		CarRoll = FMath::FInterpTo(CarRoll, FMath::Clamp(YawRate * 0.35f, -30.0f, 30.0f), DeltaSeconds, 4.0f);
	}
	LastCarYaw = Rotation.Yaw;
	bCarYawValid = DeltaSeconds > 0.0f;
	Rotation.Roll = CarRoll;
	// 내려앉기 직전(7.0~7.7초)에는 여고생이 설 방향으로 곧게 편다. 차 조각의 쉬는 자세 = 이 자리·방향의 차라서 바꿔치기가 안 보인다.
	const float Settle = FMath::SmoothStep(7.0f, IntroCarLandAt, T);
	Rotation = FMath::Lerp(Rotation, FRotator(0.0f, IntroGirlYaw, 0.0f), Settle);
	if (T >= IntroCarLandAt)
		Rotation = FRotator(0.0f, IntroGirlYaw, 0.0f);
	Car->SetActorLocationAndRotation(Location, Rotation);

	// 불꽃: 부스터를 접기 전까지 켜 두고 길이를 살짝 떨게 한다(비행 키트와 같은 떨림).
	for (UStaticMeshComponent* Flame : Flames)
	{
		if (!IsValid(Flame))
			continue;
		const bool bOn = T < IntroBoosterRetractAt;
		Flame->SetVisibility(bOn);
		if (bOn)
			Flame->SetRelativeScale3D(FVector(FMath::FRandRange(0.8f, 1.2f), 1.0f, 1.0f));
	}
	if (!bBoosterRetracted && T >= IntroBoosterRetractAt && IsValid(Booster))
	{
		bBoosterRetracted = true;
		if (BoosterRetract)
			Booster->PlayAnimation(BoosterRetract, false);
	}

	// 빔 두 방: 차 앞 끝에서 몹 가슴께로. 게임의 날으는 차 빔과 같은 메시.
	const float BeamTimes[] = { 4.55f, 4.85f };
	const int32 Targets[] = { 1, 2 };
	for (int32 Index = BeamsFired; Index < 2; ++Index)
	{
		if (T < BeamTimes[Index])
			break;
		BeamsFired = Index + 1;
		if (!Monsters.IsValidIndex(Targets[Index]))
			continue;
		const ASkeletalMeshActor* Target = Monsters[Targets[Index]].Actor.Get();
		if (!Target)
			continue;
		const float HalfLength = static_cast<float>(Car->GetSkeletalMeshComponent()->CalcLocalBounds().BoxExtent.X);
		const FVector Muzzle = Car->GetActorLocation() + Car->GetActorForwardVector() * (HalfLength + 30.0f) + FVector(0.0f, 0.0f, 25.0f);
		ShowBeam(CarBeam, Muzzle, Target->GetActorLocation() + FVector(0.0f, 0.0f, 70.0f), 2.0f);
		BeamHideAt = T + IntroBeamSeconds;
	}
	if (BeamHideAt >= 0.0f && T >= BeamHideAt && IsValid(CarBeam))
	{
		CarBeam->SetVisibility(false);
		BeamHideAt = -1.0f;
	}

	// 착지 흙먼지 폭발은 뺐다. 보스 땅찍기 효과(P_Rampage_Rock_HitWorld)라 차가 살짝 내려앉는데 땅이 터지는 것처럼 보였다
	// (9/22 사용자: "차가 바닥에 내려오는데 깜짝 놀랐네 — 몹에서 터지던 걸 착지로 옮긴 것뿐"). 착지는 부스터가 접히는 것만으로 충분하다.
	// 부스터가 다 접히면 차를 치우고 같은 자리·방향에 "차에서 돌아오는 여고생" 을 세운다.
	if (T >= IntroGirlAt && !IsValid(Girl))
	{
		Car->Destroy();
		Car = nullptr;
		SpawnGirl(true);
	}
}

void APGTitleIntroRescue::SpawnGirl(bool bRevert)
{
	using namespace PGIntroRescueLocal;
	UWorld* World = GetWorld();
	if (!World)
		return;
	// 게임의 변신 여고생 액터를 그대로 쓴다: 역변신(차 조각이 분해되며 여고생이 나타남)·키 보정·대기 동작이 이미 다 들어 있다.
	//   이미 연료를 받은 상태로 세운다 — 안 그러면 주변에 연료통을 새로 놓는다(EnsureFuelNearby).
	const FTransform Transform(FRotator(0.0f, IntroGirlYaw, 0.0f), IntroGirlSpot);
	APGTransformNPCActor* NewGirl = World->SpawnActorDeferred<APGTransformNPCActor>(APGTransformNPCActor::GetSpawnClass(), Transform, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!NewGirl)
		return;
	NewGirl->SetAlreadyFueled(true);
	NewGirl->SetPlayRevertIntro(bRevert);
	NewGirl->FinishSpawning(Transform);
	Girl = NewGirl;
	bShieldHandled = false;
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: school girl %s at %s"), bRevert ? TEXT("turning back from the car") : TEXT("placed"), *IntroGirlSpot.ToCompactString());
}

void APGTitleIntroRescue::TickShieldFade(float DeltaSeconds)
{
	// 여고생 액터는 사람으로 돌아오면(또는 세운 지 1초 뒤) 스스로 방어막 반구를 띄운다(게임에서 몹·차로부터 지키는 표시).
	//   타이틀에는 지킬 것이 없고, 반구가 옆의 주인공까지 덮는다. 그래서 떠오르는 순간 한 번 밝게 번쩍이고 1.2초에 걸쳐 걷는다.
	if (!IsValid(Girl))
		return;
	if (!bShieldHandled && IsValid(Girl->Shield))
	{
		bShieldHandled = true;
		if (bShieldInstantRemove)
		{
			Girl->Shield->DestroyComponent();
			Girl->Shield = nullptr;
			return;
		}
		ShieldMaterial = Cast<UMaterialInstanceDynamic>(Girl->Shield->GetMaterial(0));
		ShieldFade = 0.0f;
	}
	if (ShieldFade < 0.0f)
		return;
	ShieldFade += DeltaSeconds;
	const float A = FMath::Clamp(ShieldFade / 1.2f, 0.0f, 1.0f);
	if (IsValid(ShieldMaterial))
		ShieldMaterial->SetScalarParameterValue(TEXT("Brightness"), FMath::Lerp(0.35f, 0.0f, A));
	if (A >= 1.0f)
	{
		if (IsValid(Girl->Shield))
			Girl->Shield->DestroyComponent();
		Girl->Shield = nullptr;
		ShieldMaterial = nullptr;
		ShieldFade = -1.0f;
	}
}

void APGTitleIntroRescue::CleanupShots(bool bSkipped)
{
	// 인트로에만 나오는 것(몹·차·여고생·소총·마네킹)을 전부 치우고 무대를 인트로 전 모습(주인공 혼자, 맨손 대기)으로 돌린다.
	//   여고생은 인트로에만 나온다(9/22 사용자) — 끝나면 까만 화면 뒤에서(GetEndFadeOutSeconds) 치우고, 건너뛰어도 같은 모습으로 간다.
	for (AActor* Actor : SpawnedActors)
		if (IsValid(Actor))
			Actor->Destroy();
	SpawnedActors.Reset();
	Monsters.Reset();
	Car = nullptr;
	if (IsValid(Girl))
		Girl->Destroy();
	Girl = nullptr;
	ShieldMaterial = nullptr;
	ShieldFade = -1.0f;
	if (IsValid(Tracer))
		Tracer->DestroyComponent();
	Tracer = nullptr;
	if (IsValid(Rifle))
		Rifle->DestroyComponent();
	Rifle = nullptr;
	if (ASkeletalMeshActor* HeroActor = Hero.Get())
	{
		HeroActor->SetActorTransform(HeroHome);
		HeroFacing = HeroHome.Rotator().Yaw + 90.0f;
		// 마네킹 포즈 따라가기를 끊고 무대 원래 동작(Quantum 대기)으로. 끊지 않으면 마네킹이 사라진 뒤 몸이 기준 자세로 굳는다.
		USkeletalMeshComponent* Body = HeroActor->GetSkeletalMeshComponent();
		Body->SetLeaderPoseComponent(nullptr);
		if (HeroIdle)
			Body->PlayAnimation(HeroIdle, true);
		HeroPlaying = HeroIdle;
	}
	if (IsValid(RifleDriver))
		RifleDriver->Destroy();
	RifleDriver = nullptr;
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: rescue cast cleared (%s) — the title stage is back to the hero alone"),
		bSkipped ? TEXT("skipped") : TEXT("behind the fade"));
}

void APGTitleIntroRescue::TickAfterFinish(float DeltaSeconds)
{
}