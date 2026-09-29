// 타이틀 인트로 ③ 습격(기본값): 소총으로 몹을 잡다가, 등 뒤에서 두 마리가 덮치는 순간 시간이 느려지고 — 뚝 끊겨 까만 화면 → 타이틀.
//   사용자 9/22: "여고생 빼고, 총 쏴서 몬스터 상대하다가 뒤에서 두 마리 덮칠 때 살짝 슬로우 되고 걍 타이틀로 넘어가는, 영화·게임 참고해서 멋진 연출로".
//
// 참고한 연출(예고편·게임 공개 영상에서 흔히 쓰는 두 기법):
//   - 속도 램프(speed ramp): 한 장면 안에서 재생 속도를 정상 → 슬로모션으로 끌어내려 "결정적인 순간" 을 늘여 보여 준다.
//       https://www.filmeditingpro.com/speed-ramping-guide-i-video-editing-quick-tip/
//   - 스매시 컷(smash cut): 긴장이 가장 높은 순간에 예고 없이 뚝 끊어 전혀 다른 그림(여기서는 까만 화면 → 타이틀)으로 넘어간다.
//       https://www.masterclass.com/articles/how-to-use-a-smash-cut-transition-when-editing-a-film
//   게임 공개 예고편들이 "괴물이 덤벼드는 순간 → 느려짐 → 까만 화면 → 로고" 로 끝나는 것과 같은 짜임이다.
//   소리가 없으므로 "소리가 뚝 끊기는 순간" 대신 화면을 페이드 없이 한 프레임에 까맣게 한다.
//
// 시간표(실제 초):
//   0.0  왼쪽에서 몹 두 마리가 달려오고, 주인공이 소총을 들고 오른쪽에서 뛰어 들어온다.
//   1.35·1.65  첫 놈에게 두 발 → 쓰러짐.   2.2·2.5  둘째에게 두 발 → 쓰러짐. (맞을 때는 작은 불꽃만)
//   3.0  장전. 3.4~5.1 카메라가 천천히 밀고 들어간다(긴장). 심장 뛰듯 약한 흔들림이 점점 커진다.
//   5.3  등 뒤(불타는 폐차·덤불 뒤)에 숨어 있던 두 마리가 뛰어오른다. 카메라가 그의 어깨 너머 뒤쪽으로 확 돌고(휩), 초점이 주인공 → 몹으로 넘어간다(랙 포커스).
//   5.35~5.85  세상 시간이 0.2배까지 느려진다. 색이 빠지고 가장자리가 어두워진다. 카메라는 실제 시간으로 아주 천천히 다가간다.
//   8.0  몹이 공중에서 덮치기 직전 — 뚝 끊고 까만 화면 0.4초 → 까만 동안 전부 치우고 → 0.8초에 걸쳐 평소 타이틀(주인공 혼자)이 밝아진다.
//
// 왜 사람·몹 자리는 "장면 시각" 으로 움직이나: 세상 시간을 느리게 하면(SetGlobalTimeDilation) 애니·이펙트는 엔진이 알아서 느려지지만,
//   이 코드가 SetActorLocation 으로 옮기는 자리는 안 느려진다. 그래서 느린 비율을 곱해 흐르는 장면 시각(SceneClock)으로 자리를 정한다.
//   카메라는 실제 시각으로 움직여서, 세상이 멈춘 듯한 동안에도 카메라만 천천히 다가가는 그림이 된다.
#include "Flow/PGTitleIntro.h"

#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"

namespace PGIntroAmbushLocal
{
	constexpr float AmbushDuration = 8.0f;        // 이 실제 시각에 뚝 끊는다

	// 주인공 자리(무대 원점 기준): 오른쪽 화면 밖에서 뛰어 들어와 원점 가까이에서 멈춰 쏜다(구출 인트로와 같은 동선).
	const FVector AmbushHeroStart(0.0f, -420.0f, 0.0f);
	const FVector AmbushHeroStop(0.0f, -40.0f, 0.0f);
	constexpr float AmbushHeroStopAt = 1.2f;
	struct FAmbushShot { float Time; int32 Target; };
	const FAmbushShot AmbushShots[] = { { 1.35f, 0 }, { 1.65f, 0 }, { 2.2f, 1 }, { 2.5f, 1 } };
	constexpr float AmbushReloadAt = 3.0f;
	constexpr float AmbushTurnAt = 5.35f;         // 등 뒤 소리에 돌아서기 시작(느린 동안이라 반쯤만 돈다)
	constexpr float AmbushTracerSeconds = 0.06f;

	// 속도 램프(실제 시각): 5.35 → 5.85 초 동안 1배 → 0.2배.
	constexpr float AmbushRampStart = 5.35f;
	constexpr float AmbushRampEnd = 5.85f;
	constexpr float AmbushSlowSpeed = 0.2f;
	// 뛰어오른 몹이 떨어질 때까지 걸리는 장면 시간. 끊는 순간(장면 약 6.0초)에 4분의 3쯤 — 주인공 머리 위로 내리꽂는 중이다.
	constexpr float AmbushLeapSeconds = 1.0f;

	// 심장 박동 흔들림: 0.85초마다 "쿵-쿵" 두 번. 긴장 구간부터 서서히 세진다.
	constexpr float AmbushBeatPeriod = 0.85f;

	float AmbushSpeedAt(float T)
	{
		if (T >= AmbushDuration)
			return 1.0f; // 끊은 뒤(까만 화면)에는 정상 속도
		return FMath::Lerp(1.0f, AmbushSlowSpeed, FMath::SmoothStep(AmbushRampStart, AmbushRampEnd, T));
	}

	float AmbushHeartbeat(float T)
	{
		const float Phase = FMath::Fmod(T, AmbushBeatPeriod);
		return FMath::Exp(-FMath::Square(Phase / 0.05f)) + 0.6f * FMath::Exp(-FMath::Square((Phase - 0.2f) / 0.05f));
	}
}

APGTitleIntroAmbush::APGTitleIntroAmbush()
{
	using namespace PGIntroAmbushLocal;
	// 구출 인트로의 생성자가 채운 컷·차 길을 비우고 이 인트로 것으로 채운다.
	CamKeys.Reset();
	CarPath.Reset();
	// 카메라 컷(실제 시각). 무대 카메라는 -X 를 보고, 화면 왼쪽 = +Y · 오른쪽 = -Y 다.
	// 0초: 앞에서 넓게. 왼쪽 화면 밖에서 몹이, 오른쪽 화면 밖에서 주인공이 들어온다.
	CamKeys.Add({ 0.0f, FVector(640.0f, 60.0f, 150.0f), FVector(0.0f, 120.0f, 95.0f), 58.0f, 0.0f });
	// 1.6~2.6초: 쏘는 주인공과 쓰러지는 두 마리를 한 화면에.
	CamKeys.Add({ 1.6f, FVector(600.0f, 200.0f, 130.0f), FVector(0.0f, 200.0f, 100.0f), 55.0f, 0.0f });
	CamKeys.Add({ 2.6f, FVector(520.0f, 160.0f, 130.0f), FVector(-60.0f, 120.0f, 110.0f), 52.0f, 0.0f });
	// 3.4~5.1초: 긴장. 주인공 앞(그가 보는 +Y 쪽)에서 얼굴을 잡고 천천히 밀고 들어간다. 그의 등 뒤(-Y)가 화면 뒤쪽에 비어 있다.
	CamKeys.Add({ 3.4f, FVector(330.0f, 330.0f, 150.0f), FVector(0.0f, -40.0f, 150.0f), 45.0f, 0.0f });
	CamKeys.Add({ 5.1f, FVector(210.0f, 270.0f, 155.0f), FVector(0.0f, -40.0f, 150.0f), 40.0f, 0.0f });
	// 5.5초: 휩 — 0.4초 만에 그의 어깨 너머 뒤쪽(폐차·덤불에서 튀어나온 두 마리)으로 확 돈다. 주인공은 화면 오른쪽 앞에 걸린다.
	CamKeys.Add({ 5.5f, FVector(190.0f, 265.0f, 160.0f), FVector(-400.0f, -300.0f, 190.0f), 38.0f, 0.0f });
	// 8초: 느린 동안 아주 천천히 다가가며 좁아지고 살짝 기운다. 이 컷에서 끊는다(평소 타이틀 구도로 흘러가지 않는다).
	CamKeys.Add({ AmbushDuration, FVector(160.0f, 240.0f, 158.0f), FVector(-300.0f, -250.0f, 185.0f), 32.0f, 3.0f });
	ShotTimes = { 0.8f, 1.5f, 2.3f, 2.6f, 3.6f, 4.6f, 5.2f, 5.45f, 5.7f, 6.2f, 6.8f, 7.4f, 7.9f, 8.2f };
}

float APGTitleIntroAmbush::GetDuration() const
{
	return PGIntroAmbushLocal::AmbushDuration;
}

void APGTitleIntroAmbush::BeginShots()
{
	using namespace PGIntroAmbushLocal;
	FindStageHero();
	SetupRifleHero();

	// 몹 넷: 앞의 둘은 총에 맞아 쓰러지고(구출 인트로의 TickMonster 그대로), 뒤의 둘은 등 뒤에서 뛰어올라 덮친다(TickLeaper).
	auto Plan = [](const FVector& From, const FVector& To, float RunStart, float RunEnd, float HitAt, const FVector& FlyTo, float FlyHeight, float Spin)
	{
		FPGIntroMonster M;
		M.From = From;
		M.To = To;
		M.RunStart = RunStart;
		M.RunEnd = RunEnd;
		M.HitAt = HitAt;
		M.FlyTo = FlyTo;
		M.FlyHeight = FlyHeight;
		M.FlySpinYaw = Spin;
		return M;
	};
	// 쓰러지는 둘: 왼쪽 멀리서 달려오다 두 번째 총알에 뒤로 튕겨 넘어진다(소총이라 작게 튕긴다). 떨어지는 자리는 타이틀 화면 밖.
	SpawnIntroMonster(TEXT("Cactus"), Plan(FVector(-60.0f, 950.0f, 0.0f), FVector(-30.0f, 330.0f, 0.0f), 0.2f, AmbushShots[1].Time,
		AmbushShots[1].Time, FVector(-100.0f, 480.0f, 0.0f), 40.0f, 25.0f));
	SpawnIntroMonster(TEXT("Slime"), Plan(FVector(-320.0f, 1050.0f, 0.0f), FVector(-230.0f, 420.0f, 0.0f), 0.6f, AmbushShots[3].Time,
		AmbushShots[3].Time, FVector(-300.0f, 560.0f, 0.0f), 35.0f, -30.0f));
	FirstLeaper = Monsters.Num();
	// 덮치는 둘: 불타는 폐차(-650,-300) 뒤와 덤불 나무(-1100,-700) 뒤에 숨어 있다가 5.3초에 튀어나와 뛰어오른다.
	//   처음엔 등 뒤 멀리서 달려오게 했는데, 긴장 장면(주인공 얼굴 컷)의 배경에 다가오는 게 다 보여서 "휩으로 드러내기" 가 안 됐다(9/22 시험 촬영).
	//   From = To = 숨은 자리(달리지 않는다), FlyTo = 떨어질 자리(주인공 바로 옆), FlyHeight = 뛰는 높이. HitAt 은 쓰지 않는다(아주 먼 시각).
	SpawnIntroMonster(TEXT("ChestMonster"), Plan(FVector(-900.0f, -450.0f, 0.0f), FVector(-900.0f, -450.0f, 0.0f), 5.3f, 5.3f,
		1.0e6f, FVector(-20.0f, -110.0f, 0.0f), 180.0f, 0.0f));
	SpawnIntroMonster(TEXT("Beholder"), Plan(FVector(-1150.0f, -760.0f, 0.0f), FVector(-1150.0f, -760.0f, 0.0f), 5.35f, 5.35f,
		1.0e6f, FVector(40.0f, -90.0f, 0.0f), 200.0f, 0.0f));

	// 화면 설정 원본을 적어 둔다. 느린 순간에 색·가장자리·초점을 바꿨다가 끝·건너뛰기 때 이 값으로 되돌린다.
	if (ACameraActor* Cam = Camera.Get())
		if (const UCameraComponent* Lens = Cam->GetCameraComponent())
			SavedPost = Lens->PostProcessSettings;
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: ambush cast ready — hero %s, rifle anims %s, %d monster(s) (%d leaper(s))"),
		Hero.IsValid() ? TEXT("found") : TEXT("MISSING"), IsValid(RifleDriver) ? TEXT("on") : TEXT("off"), Monsters.Num(), Monsters.Num() - FirstLeaper);
}

void APGTitleIntroAmbush::TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation)
{
	using namespace PGIntroAmbushLocal;
	// 속도 램프: 이번 틱에 흐를 장면 시간 = 실제 시간 × 속도. 세상 시간도 같은 비율로 느리게 해서 애니·이펙트가 같이 느려지게 한다.
	const float Speed = AmbushSpeedAt(T);
	SceneClock += DeltaSeconds * Speed;
	if (UWorld* World = GetWorld())
	{
		if (Speed < 0.999f)
		{
			UGameplayStatics::SetGlobalTimeDilation(World, Speed);
			bTimeDilated = true;
		}
		else if (bTimeDilated)
		{
			// 끊는 순간(까만 화면)부터는 정상 속도. 0.4초 까만 화면·밝아지는 시간이 느려지면 안 된다.
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
			bTimeDilated = false;
		}
	}
	const float S = SceneClock;
	TickAmbushHero(S);
	for (int32 Index = 0; Index < Monsters.Num(); ++Index)
	{
		if (Index < FirstLeaper)
			TickMonster(Monsters[Index], S);
		else
			TickLeaper(Monsters[Index], S);
	}

	// 느린 순간의 화면: 색이 빠지고, 가장자리가 어두워지고, 초점이 주인공에서 덮치는 몹으로 넘어간다.
	const float Slow = T < AmbushDuration ? FMath::SmoothStep(AmbushRampStart - 0.05f, AmbushRampEnd + 0.15f, T) : 0.0f;
	ApplySlowMoLook(Slow, T);

	// 흔들림: 총 맞힐 때 짧게 + 긴장 구간부터 심장 박동(실제 시각 기준 — 느린 동안에도 박동은 제 빠르기로 뛴다. 긴장만 늘어난다).
	float Kick = 0.0f;
	for (const FAmbushShot& Shot : AmbushShots)
		if (S >= Shot.Time)
			Kick += FMath::Exp(-(S - Shot.Time) * 9.0f) * 0.35f;
	const float Tension = T < AmbushDuration ? FMath::SmoothStep(3.2f, 5.0f, T) * (1.0f + Slow) : 0.0f;
	const float Beat = AmbushHeartbeat(T) * Tension;
	OutShakeLocation = FVector(FMath::Sin(T * 61.0f), FMath::Sin(T * 47.0f + 1.0f), FMath::Sin(T * 53.0f + 2.0f)) * 5.0f * Kick
		+ FVector(0.0f, 0.0f, -2.0f * Beat);
	OutShakeRotation = FRotator(FMath::Sin(T * 43.0f) * 0.5f, FMath::Sin(T * 37.0f + 0.5f) * 0.3f, 0.0f) * Kick
		+ FRotator(-0.35f * Beat, 0.0f, 0.15f * Beat);
}

void APGTitleIntroAmbush::TickAmbushHero(float S)
{
	using namespace PGIntroAmbushLocal;
	if (!Hero.IsValid())
		return;
	// 자리: 뛰어 들어와(0~1.2초) 멈춘다.
	const FVector Location = FMath::Lerp(AmbushHeroStart, AmbushHeroStop, FMath::Clamp(S / AmbushHeroStopAt, 0.0f, 1.0f));
	// 방향: 왼쪽(+Y, 달려오는 몹)을 보다가, 긴장 구간에는 좌우로 살피고, 등 뒤 소리에 돌아선다(느린 동안이라 반쯤에서 끊긴다).
	float Facing = 90.0f;
	if (S >= 3.2f && S < AmbushTurnAt)
		Facing += 12.0f * FMath::Sin((S - 3.2f) * 2.2f) * FMath::SmoothStep(3.2f, 3.8f, S);
	if (S >= AmbushTurnAt)
		Facing = FMath::Lerp(90.0f, -70.0f, FMath::SmoothStep(AmbushTurnAt, AmbushTurnAt + 0.8f, S));
	SetHeroPose(Location, Facing);

	if (!IsValid(RifleDriver))
	{
		// 소총 동작이 없는 PC: Quantum 달리기 → 대기. 몹은 같은 시각에 쓰러진다.
		PlayHero(S < AmbushHeroStopAt ? HeroRun : HeroIdle, true);
		return;
	}
	if (S < AmbushHeroStopAt)
	{
		if (RiflePlaying != RifleJog)
			PlayRifle(RifleJog ? RifleJog : RifleIdle, true, S);
	}
	else
	{
		// 두 발씩 두 마리. 쏠 때마다 사격 동작을 처음부터 다시 튼다(반동이 한 발씩 보인다).
		while (ShotsFired < static_cast<int32>(UE_ARRAY_COUNT(AmbushShots)) && S >= AmbushShots[ShotsFired].Time)
		{
			PlayRifle(RifleFire, false, S);
			FireRifle(S, AmbushShots[ShotsFired].Target);
			TracerHideAt = S + AmbushTracerSeconds;
			++ShotsFired;
		}
		// 다 잡았다고 한숨 돌리며 장전 — 이 틈에 뒤에서 온다.
		if (!bReloadStarted && S >= AmbushReloadAt)
		{
			bReloadStarted = true;
			PlayRifle(RifleReload, false, S);
		}
		if (RiflePlaying == RifleJog || (S >= RifleAnimEnd && RiflePlaying != RifleIdle))
			PlayRifle(RifleIdle, true, S);
	}
	UpdateRifle();
	if (TracerHideAt >= 0.0f && S >= TracerHideAt && IsValid(Tracer))
	{
		Tracer->SetVisibility(false);
		TracerHideAt = -1.0f;
	}
}

void APGTitleIntroAmbush::TickLeaper(FPGIntroMonster& Monster, float S)
{
	using namespace PGIntroAmbushLocal;
	ASkeletalMeshActor* Actor = Monster.Actor.Get();
	if (!Actor)
		return;
	USkeletalMeshComponent* Body = Actor->GetSkeletalMeshComponent();
	// 뛰기 전에는 숨겨 둔다. 폐차·덤불 뒤에 세워 두었지만 색이 튀는 몹이라(분홍 눈알) 가장자리가 삐져나와 보였다(9/22 시험 촬영).
	//   뛰는 순간 나타나는 것은 가림막 바로 뒤라 "튀어나온" 것으로 읽힌다.
	Actor->SetActorHiddenInGame(S < Monster.RunEnd);
	if (S < Monster.RunEnd)
	{
		// 뛰기 전: 숨은 자리에서 기다린다(From = To 라 제자리).
		if (!Monster.bRunning && Monster.Run)
		{
			Body->PlayAnimation(Monster.Run, true);
			Monster.bRunning = true;
		}
		const float A = FMath::Clamp((S - Monster.RunStart) / FMath::Max(Monster.RunEnd - Monster.RunStart, 0.01f), 0.0f, 1.0f);
		const float Facing = (Monster.To - Monster.From).Rotation().Yaw;
		Actor->SetActorLocationAndRotation(FMath::Lerp(Monster.From, Monster.To, A), FRotator(0.0f, Facing + Monster.MeshYaw, 0.0f));
		return;
	}
	// 뛰어오른 뒤: 공격 동작을 한 번 틀고, 포물선으로 주인공 쪽에 내리꽂는다. 느린 동안이라 공중에서 거의 멈춰 보인다.
	if (!Monster.bAttacked)
	{
		Monster.bAttacked = true;
		if (Monster.Attack)
			Body->PlayAnimation(Monster.Attack, false);
	}
	const float L = FMath::Clamp((S - Monster.RunEnd) / AmbushLeapSeconds, 0.0f, 1.0f);
	const FVector Location = FMath::Lerp(Monster.To, Monster.FlyTo, L) + FVector(0.0f, 0.0f, Monster.FlyHeight * 4.0f * L * (1.0f - L));
	const float Facing = (Monster.FlyTo - Monster.To).Rotation().Yaw;
	// 앞으로 살짝 숙인다(덮치는 자세). 메시 앞 방향이 +Y 인 팩이라 피치 대신 롤 축이 앞뒤다 — 그래서 요만 돌리고 기울기는 안 준다.
	Actor->SetActorLocationAndRotation(Location, FRotator(0.0f, Facing + Monster.MeshYaw, 0.0f));
}

void APGTitleIntroAmbush::ApplySlowMoLook(float Amount, float T)
{
	ACameraActor* Cam = Camera.Get();
	UCameraComponent* Lens = Cam ? Cam->GetCameraComponent() : nullptr;
	if (!Lens || (Amount <= 0.0f && !bLookChanged))
		return;
	bLookChanged = true;
	FPostProcessSettings& Post = Lens->PostProcessSettings;
	// 색 빼기: 느린 순간만 따로 떼어 보이게(예고편에서 흔한 "시간이 멎은" 색감).
	Post.bOverride_ColorSaturation = true;
	const float Saturation = FMath::Lerp(1.0f, 0.45f, Amount);
	Post.ColorSaturation = FVector4(Saturation, Saturation, Saturation, 1.0f);
	// 가장자리 어둡게 + 색 번짐: 시야가 좁아지는 느낌.
	Post.bOverride_VignetteIntensity = true;
	Post.VignetteIntensity = FMath::Lerp(SavedPost.bOverride_VignetteIntensity ? SavedPost.VignetteIntensity : 0.4f, 1.15f, Amount);
	Post.bOverride_SceneFringeIntensity = true;
	Post.SceneFringeIntensity = FMath::Lerp(0.0f, 2.5f, Amount);
	// 랙 포커스: 휩 직후 초점이 주인공(약 3m) → 덮치는 몹(약 6.5m)으로 넘어간다. 느리기 전에는 초점 효과를 안 건다.
	Post.bOverride_DepthOfFieldFocalDistance = Amount > 0.0f;
	Post.bOverride_DepthOfFieldFstop = Amount > 0.0f;
	Post.DepthOfFieldFocalDistance = FMath::Lerp(320.0f, 650.0f, Amount);
	Post.DepthOfFieldFstop = 2.0f;
}

void APGTitleIntroAmbush::RestoreTimeAndLook()
{
	if (bTimeDilated)
	{
		if (UWorld* World = GetWorld())
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
		bTimeDilated = false;
	}
	if (bLookChanged)
	{
		if (ACameraActor* Cam = Camera.Get())
			if (UCameraComponent* Lens = Cam->GetCameraComponent())
				Lens->PostProcessSettings = SavedPost;
		bLookChanged = false;
	}
}

void APGTitleIntroAmbush::CleanupShots(bool bSkipped)
{
	// 세상 시간·화면 색을 먼저 되돌리고, 사람·몹·총은 구출 인트로의 뒷정리가 치운다(주인공 혼자 맨손 대기로).
	RestoreTimeAndLook();
	if (const AWorldSettings* Settings = GetWorldSettings())
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: ambush %s — world time dilation back to %.2f, screen look restored"),
			bSkipped ? TEXT("skipped") : TEXT("cut"), Settings->TimeDilation);
	Super::CleanupShots(bSkipped);
}

void APGTitleIntroAmbush::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 느린 도중에 레벨이 바뀌면(콘솔 PG.Flow.Lobby 등) 세상 시간이 0.2배로 남는다 — 레벨이 내려갈 때도 되돌린다.
	RestoreTimeAndLook();
	Super::EndPlay(EndPlayReason);
}