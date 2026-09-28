#include "Flow/PGTitleIntro.h"

#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Flow/PGFlowStage.h"
#include "Flow/PGTitleIntroSet.h"
#include "Flow/PGLoadingScreenSubsystem.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "GenericPlatform/ICursor.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Particles/ParticleSystem.h"
#include "Styling/CoreStyle.h"
#include "UnrealClient.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"

// 아무 키·마우스 클릭을 "건너뛰기" 로 받는다.
// 왜 Slate 입력 전처리기인가: 인트로 동안에는 화면 위젯이 없고(타이틀 위젯은 끝난 뒤에 뜬다) 컨트롤러 입력 모드도 UI 겸용이라,
//   게임 입력 바인딩으로 받으면 어느 쪽이 먼저 먹는지에 따라 가끔 놓친다. 전처리기는 모든 입력을 가장 먼저 본다.
//   입력을 먹지는 않는다(false) — 콘솔 키(`)도 그대로 콘솔을 연다.
class FPGTitleIntroSkipInput : public IInputProcessor
{
public:
	bool bPressed = false;

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& InKeyEvent) override
	{
		if (!InKeyEvent.IsRepeat())
			bPressed = true;
		return false;
	}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		bPressed = true;
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("PGTitleIntroSkip"); }
};

// 유니티 빌드에서 다른 cpp 의 이름과 겹치지 않게 파일 고유 이름 공간을 쓴다.
namespace PGTitleIntroLocal
{
	// "이 게임에서 이미 봤다" 표시. 게임 인스턴스 단위로 본다 — 에디터 PIE 는 누를 때마다 인스턴스가 새로 생기므로 매번 다시 나오고,
	//   한 판 안에서 로비 → 타이틀로 돌아오면 같은 인스턴스라 안 나온다. 약한 포인터라 인스턴스가 사라지면 저절로 비워진다.
	TWeakObjectPtr<UGameInstance> IntroPlayedFor;

	// 콘솔 PG.Flow.Intro 로 잠깐 바꾼 값(프로젝트 설정보다 먼저). 에디터를 끄면 사라진다.
	bool bHasConsoleVariant = false;
	EPGTitleIntroVariant ConsoleVariant = EPGTitleIntroVariant::Rescue;

	// 위아래 검은 띠 한 쪽이 화면 높이에서 차지하는 비율. 가운데 80% 가 영화 화면비(약 2.2:1)가 된다.
	constexpr float IntroLetterboxFraction = 0.1f;
	// 타이틀 카메라가 보는 점까지의 거리(PGFlowStage 의 타이틀 컷: 430cm 앞의 캐릭터). 마지막 컷의 "보는 점" 을 만들 때 쓴다.
	constexpr float IntroTitleLookDistance = 430.0f;
	// 인트로 동안의 움직임 흐림 세기(엔진 기본 0.5).
	constexpr float IntroMotionBlur = 0.15f;

	// 빔·흙먼지·총알 맞은 효과 에셋은 데이터 에셋 DA_PGTitleIntro(없으면 원래 코드 에셋)에 있다(9/23 블루프린트 분리).

	bool ParseVariant(const FString& Name, EPGTitleIntroVariant& Out)
	{
		const UEnum* Enum = StaticEnum<EPGTitleIntroVariant>();
		const int64 Value = Enum ? Enum->GetValueByNameString(Name) : INDEX_NONE;
		if (Value == INDEX_NONE)
			return false;
		Out = static_cast<EPGTitleIntroVariant>(Value);
		return true;
	}

	FString VariantName(EPGTitleIntroVariant Variant)
	{
		const UEnum* Enum = StaticEnum<EPGTitleIntroVariant>();
		return Enum ? Enum->GetNameStringByValue(static_cast<int64>(Variant)) : TEXT("?");
	}

	// PG.Flow.Intro [ShipDragon|Rescue|None] — 인트로 종류를 바꾸고 "봤다" 표시를 지운다. 다음에 타이틀을 열면(PG.Flow.Title) 그 인트로가 나온다.
	FAutoConsoleCommandWithWorldAndArgs IntroCommand(
		TEXT("PG.Flow.Intro"),
		TEXT("PG.Flow.Intro [ShipDragon|Rescue|None] - pick the title intro and re-arm it (then PG.Flow.Title)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (Args.Num() > 0)
			{
				EPGTitleIntroVariant Parsed;
				if (!ParseVariant(Args[0], Parsed))
				{
					UE_LOG(LogTemp, Warning, TEXT("PGTitleIntro: unknown intro '%s' (ShipDragon, Rescue, None)"), *Args[0]);
					return;
				}
				bHasConsoleVariant = true;
				ConsoleVariant = Parsed;
			}
			IntroPlayedFor.Reset();
			UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: intro = %s, re-armed — PG.Flow.Title to watch it"),
				*VariantName(APGTitleIntro::GetActiveVariant()));
		}));
}

// ---- 시작 ----

APGTitleIntro::APGTitleIntro()
{
	// 스스로 틱하지 않는다. 타이틀 게임모드가 Animate 다음에 Advance 를 부른다(헤더 설명).
	PrimaryActorTick.bCanEverTick = false;
	SetCanBeDamaged(false);
}

EPGTitleIntroVariant APGTitleIntro::GetActiveVariant()
{
	using namespace PGTitleIntroLocal;
	FString FromCommandLine;
	EPGTitleIntroVariant Parsed;
	if (FParse::Value(FCommandLine::Get(), TEXT("PGIntro="), FromCommandLine) && ParseVariant(FromCommandLine, Parsed))
		return Parsed;
	if (bHasConsoleVariant)
		return ConsoleVariant;
	return UPGFlowSettings::Get().TitleIntroVariant;
}

APGTitleIntro* APGTitleIntro::StartOnce(UWorld* World, ACameraActor* StageCamera)
{
	using namespace PGTitleIntroLocal;
	// 화면이 없으면(헤드리스 스모크 테스트·전용 서버) 볼 사람도 없다.
	if (!IsValid(World) || !IsValid(StageCamera) || !World->GetGameViewport())
		return nullptr;
	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance || IntroPlayedFor.Get() == GameInstance)
		return nullptr;
	// 조건이 안 맞아 못 틀어도 "봤다" 로 친다. 나중에 로비에서 돌아왔을 때 뜬금없이 인트로가 나오면 안 된다.
	IntroPlayedFor = GameInstance;

	const EPGTitleIntroVariant Variant = GetActiveVariant();
	if (Variant == EPGTitleIntroVariant::None)
	{
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: intro is None — straight to the title"));
		return nullptr;
	}
	// 타이틀 배경 영상이 깔리면 3D 무대가 없다(PGFlowStage::Build). 움직일 무대가 없으니 인트로도 없다.
	FString VideoPath;
	if (PGFlowStage::FindTitleVideo(VideoPath))
		return nullptr;

	UClass* IntroClass = APGTitleIntroAmbush::StaticClass();
	if (Variant == EPGTitleIntroVariant::ShipDragon)
		IntroClass = APGTitleIntroShipDragon::StaticClass();
	else if (Variant == EPGTitleIntroVariant::Rescue)
		IntroClass = APGTitleIntroRescue::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APGTitleIntro* Intro = World->SpawnActor<APGTitleIntro>(IntroClass, FTransform::Identity, Params);
	if (!Intro)
		return nullptr;
	Intro->Begin(StageCamera);
	return Intro;
}

void APGTitleIntro::Begin(ACameraActor* StageCamera)
{
	Camera = StageCamera;
	if (UCameraComponent* Lens = StageCamera->GetCameraComponent())
	{
		TitleFov = Lens->FieldOfView;
		// 인트로 카메라는 빨리 돌고 내려온다. 기본 움직임 흐림이면 전함·드래곤이 번져서 알아볼 수 없었다(9/22 시험 촬영). 끝나면 되돌린다.
		bSavedBlurOverride = Lens->PostProcessSettings.bOverride_MotionBlurAmount;
		SavedBlurAmount = Lens->PostProcessSettings.MotionBlurAmount;
		Lens->PostProcessSettings.bOverride_MotionBlurAmount = true;
		Lens->PostProcessSettings.MotionBlurAmount = PGTitleIntroLocal::IntroMotionBlur;
	}
	bTestShots = FParse::Param(FCommandLine::Get(), TEXT("PGIntroShots"));
	FParse::Value(FCommandLine::Get(), TEXT("PGIntroSkipAt="), TestSkipAt);
	// 무거운 에셋 읽기·스폰은 로고가 떠 있는 지금 한다. 화면이 까매서 멈칫해도 안 보인다.
	BeginShots();
	BuildLetterbox();
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: %s intro ready (%.1fs, %d camera keys) — waiting for the splash logo"),
		GetVariantName(), GetDuration(), CamKeys.Num());
	Advance(0.0f); // 로고가 걷히는 순간 보일 첫 컷에 카메라를 미리 둔다
}

// ---- 매 틱 ----

void APGTitleIntro::Advance(float WorldDeltaSeconds)
{
	using namespace PGTitleIntroLocal;
	// 인트로 시간표는 "실제 시간" 으로 간다. 습격 인트로는 세상 시간을 느리게(SetGlobalTimeDilation) 하는데,
	//   게임모드가 넘겨주는 틱 시간은 그만큼 줄어 있다. 그대로 쓰면 느려지는 구간의 길이가 들쭉날쭉해지고, 느린 동안 카메라까지 느려진다.
	//   그래서 느린 비율로 나눠 실제 시간으로 되돌린다(느리게 할 것은 각 인트로가 장면 시각으로 따로 계산한다).
	const AWorldSettings* Settings = GetWorldSettings();
	const float Dilation = Settings ? FMath::Max(Settings->GetEffectiveTimeDilation(), 0.0001f) : 1.0f;
	const float DeltaSeconds = WorldDeltaSeconds / Dilation;
	if (Phase == EPGTitleIntroPhase::Done)
	{
		TickAfterFinish(DeltaSeconds);
		TickTestShots(DeltaSeconds);
		return;
	}
	ACameraActor* Cam = Camera.Get();
	if (!Cam)
	{
		Finish(true);
		return;
	}
	// 방금 PGFlowStage::Animate 가 카메라를 "평소 타이틀 자리" 에 놓았다. 그 자리가 인트로의 마지막 컷이다.
	//   그래서 인트로가 끝나는 프레임과 평소 타이틀의 첫 프레임이 정확히 같은 그림이 된다(튀지 않는다).
	FPGIntroCamKey TitleKey;
	TitleKey.Time = GetDuration();
	TitleKey.Location = Cam->GetActorLocation();
	TitleKey.LookAt = TitleKey.Location + Cam->GetActorForwardVector() * IntroTitleLookDistance;
	TitleKey.Fov = TitleFov;

	FVector ShakeLocation = FVector::ZeroVector;
	FRotator ShakeRotation = FRotator::ZeroRotator;
	if (Phase == EPGTitleIntroPhase::WaitingForSplash)
	{
		const UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(this);
		if (Loading && Loading->IsSplashLogoShowing())
		{
			// 로고가 떠 있는 동안은 첫 컷에 멈춰 둔다. 검은 바탕이 걷히며 이 그림이 드러난다.
			TickShots(0.0f, 0.0f, ShakeLocation, ShakeRotation);
			ApplyCamera(EvaluateCamera(0.0f, TitleKey), FVector::ZeroVector, FRotator::ZeroRotator);
			SetLetterboxOpacity(1.0f);
			return;
		}
		Phase = EPGTitleIntroPhase::Playing;
		RegisterSkipInput();
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: %s intro playing (any key or click skips)"), GetVariantName());
	}

	Clock += DeltaSeconds;
	const bool bKeySkip = SkipInput.IsValid() && SkipInput->bPressed;
	const bool bTestSkip = TestSkipAt >= 0.0f && Clock >= TestSkipAt;
	if (bKeySkip || bTestSkip)
	{
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: skipped at %.1fs (%s)"), Clock, bKeySkip ? TEXT("input") : TEXT("-PGIntroSkipAt"));
		Finish(true);
		TickTestShots(0.0f);
		return;
	}
	const float Duration = GetDuration();
	const float T = FMath::Min(Clock, Duration);
	TickShots(T, DeltaSeconds, ShakeLocation, ShakeRotation);
	ApplyCamera(EvaluateCamera(T, TitleKey), ShakeLocation, ShakeRotation);
	// 검은 띠는 끝나기 1.2초 전부터 0.2초 전까지 걷힌다. 타이틀 위젯이 나타날 때는 이미 없다.
	//   뚝 끊어 끝내는 인트로는 끝까지 둔다(끊기는 순간까지 영화 화면비여야 한다). 까만 화면 뒤에서 치운다.
	SetLetterboxOpacity(KeepLetterboxToEnd() ? 1.0f : 1.0f - FMath::SmoothStep(Duration - 1.2f, Duration - 0.2f, T));
	TickTestShots(DeltaSeconds);
	if (Clock < Duration)
		return;
	// 끝. 종류에 따라 화면을 까맣게 덮었다가, 까만 동안 인트로용 액터를 치우고(CleanupShots) 다시 걷는다.
	//   구출 인트로: 여고생은 인트로에만 나온다(9/22 사용자). 보이는 채로 사라지면 튀어 보이니 까만 동안 바꿔친다.
	//   습격 인트로: 페이드 없이 뚝 끊는다(0초에 까맣게) → 0.4초 버틴다 → 치우고 밝아진다.
	const float FadeOut = GetEndFadeOutSeconds();
	const float Hold = GetEndHoldSeconds();
	if (FadeOut <= 0.0f && Hold <= 0.0f)
	{
		Finish(false);
		return;
	}
	if (EndFadeClock < 0.0f)
	{
		EndFadeClock = 0.0f;
		// 페이드 시간이 0 이면 처음부터 끝까지 까만 "페이드" 를 건다 = 그 프레임부터 바로 까맣다(시간 0 을 주면 엔진이 알파를 안 바꾼다).
		if (FadeOut > 0.0f)
			StartCameraFade(0.0f, 1.0f, FadeOut, true);
		else
			StartCameraFade(1.0f, 1.0f, 0.01f, true);
	}
	else
	{
		EndFadeClock += DeltaSeconds;
	}
	if (EndFadeClock >= FadeOut + Hold)
		Finish(false);
}

void APGTitleIntro::StartCameraFade(float From, float To, float Seconds, bool bHold)
{
	// 카메라 매니저의 페이드는 3D 화면만 덮는다. 뒤이어 뜨는 타이틀 위젯은 덮지 않아서 위젯의 서서히 나타나기와 겹쳐도 된다.
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (PC && PC->PlayerCameraManager)
		PC->PlayerCameraManager->StartCameraFade(From, To, Seconds, FLinearColor::Black, false, bHold);
}

void APGTitleIntro::Finish(bool bSkipped)
{
	if (Phase == EPGTitleIntroPhase::Done)
		return;
	Phase = EPGTitleIntroPhase::Done;
	UnregisterSkipInput();
	RemoveLetterbox();
	CleanupShots(bSkipped);
	// 까맣게 덮어 둔 상태였으면(끝 페이드 도중에 끝났거나 건너뜀) 걷는다. 안 걷으면 타이틀이 까만 채로 남는다.
	if (EndFadeClock >= 0.0f)
		StartCameraFade(1.0f, 0.0f, FMath::Max(GetEndFadeInSeconds(), 0.05f), false);
	// 화각을 타이틀 값으로. 위치·방향은 다음 틱부터 PGFlowStage::Animate 가 다시 맡는다(건너뛴 프레임에도 이미 그 자리다).
	if (ACameraActor* Cam = Camera.Get())
		if (UCameraComponent* Lens = Cam->GetCameraComponent())
		{
			Lens->SetFieldOfView(TitleFov);
			Lens->PostProcessSettings.bOverride_MotionBlurAmount = bSavedBlurOverride;
			Lens->PostProcessSettings.MotionBlurAmount = SavedBlurAmount;
		}
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: %s intro %s at %.1fs — title UI next"), GetVariantName(),
		bSkipped ? TEXT("skipped") : TEXT("finished"), Clock);
}

void APGTitleIntro::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 레벨이 바뀌거나 게임이 꺼지면 화면 위젯과 입력 전처리기를 떼어야 한다. 둘 다 월드 밖(Slate)에 붙어 있어서 액터와 같이 안 사라진다.
	UnregisterSkipInput();
	RemoveLetterbox();
	Super::EndPlay(EndPlayReason);
}

// ---- 카메라 ----

FPGIntroCamKey APGTitleIntro::EvaluateCamera(float T, const FPGIntroCamKey& TitleKey) const
{
	TArray<FPGIntroCamKey> Keys = CamKeys;
	if (EndsOnTitleShot() || Keys.Num() == 0)
		Keys.Add(TitleKey);
	if (Keys.Num() == 1 || T <= Keys[0].Time)
		return Keys[0];
	int32 Index = 0;
	while (Index < Keys.Num() - 2 && T >= Keys[Index + 1].Time)
		++Index;
	const FPGIntroCamKey& K0 = Keys[FMath::Max(Index - 1, 0)];
	const FPGIntroCamKey& K1 = Keys[Index];
	const FPGIntroCamKey& K2 = Keys[Index + 1];
	const FPGIntroCamKey& K3 = Keys[FMath::Min(Index + 2, Keys.Num() - 1)];
	const float U = FMath::Clamp((T - K1.Time) / FMath::Max(K2.Time - K1.Time, KINDA_SMALL_NUMBER), 0.0f, 1.0f);

	FPGIntroCamKey Out;
	Out.Time = T;
	Out.Location = CatmullRom(K0.Location, K1.Location, K2.Location, K3.Location, U);
	Out.LookAt = CatmullRom(K0.LookAt, K1.LookAt, K2.LookAt, K3.LookAt, U);
	// 화각·기울기는 곡선이 넘쳐 흔들릴 이유가 없어 부드러운 직선 보간으로 충분하다.
	const float Ease = FMath::SmoothStep(0.0f, 1.0f, U);
	Out.Fov = FMath::Lerp(K1.Fov, K2.Fov, Ease);
	Out.Roll = FMath::Lerp(K1.Roll, K2.Roll, Ease);
	return Out;
}

void APGTitleIntro::ApplyCamera(const FPGIntroCamKey& Pose, const FVector& ShakeLocation, const FRotator& ShakeRotation)
{
	ACameraActor* Cam = Camera.Get();
	if (!Cam)
		return;
	FRotator Rotation = (Pose.LookAt - Pose.Location).Rotation();
	Rotation.Roll = Pose.Roll;
	Cam->SetActorLocationAndRotation(Pose.Location + ShakeLocation, Rotation + ShakeRotation);
	if (UCameraComponent* Lens = Cam->GetCameraComponent())
		Lens->SetFieldOfView(Pose.Fov);
}

FVector APGTitleIntro::CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float U)
{
	const float U2 = U * U;
	const float U3 = U2 * U;
	return 0.5f * ((2.0f * P1) + (P2 - P0) * U + (2.0f * P0 - 5.0f * P1 + 4.0f * P2 - P3) * U2 + (3.0f * P1 - P0 - 3.0f * P2 + P3) * U3);
}

FVector APGTitleIntro::EvalPath(const TArray<FPGIntroPathKey>& Keys, float T)
{
	if (Keys.Num() == 0)
		return FVector::ZeroVector;
	if (Keys.Num() == 1 || T <= Keys[0].Time)
		return Keys[0].Location;
	if (T >= Keys.Last().Time)
		return Keys.Last().Location;
	int32 Index = 0;
	while (Index < Keys.Num() - 2 && T >= Keys[Index + 1].Time)
		++Index;
	const FVector& P0 = Keys[FMath::Max(Index - 1, 0)].Location;
	const FVector& P1 = Keys[Index].Location;
	const FVector& P2 = Keys[Index + 1].Location;
	const FVector& P3 = Keys[FMath::Min(Index + 2, Keys.Num() - 1)].Location;
	const float U = (T - Keys[Index].Time) / FMath::Max(Keys[Index + 1].Time - Keys[Index].Time, KINDA_SMALL_NUMBER);
	return CatmullRom(P0, P1, P2, P3, U);
}

FVector APGTitleIntro::PathVelocity(const TArray<FPGIntroPathKey>& Keys, float T)
{
	// 끝점에서 멈춘 뒤에도 방향이 0 이 되지 않게, 길 안쪽으로 당겨서 잰다.
	if (Keys.Num() < 2)
		return FVector::ForwardVector;
	const float Clamped = FMath::Clamp(T, Keys[0].Time + 0.05f, Keys.Last().Time - 0.05f);
	return (EvalPath(Keys, Clamped + 0.05f) - EvalPath(Keys, Clamped - 0.05f)) / 0.1f;
}

// ---- 위아래 검은 띠 ----

void APGTitleIntro::BuildLetterbox()
{
	using namespace PGTitleIntroLocal;
	UGameViewportClient* Viewport = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
	if (!Viewport || Letterbox.IsValid())
		return;
	const FSlateBrush* White = FCoreStyle::Get().GetBrush("WhiteBrush");
	Letterbox = SNew(SVerticalBox)
		.Visibility(EVisibility::HitTestInvisible)
		+ SVerticalBox::Slot().FillHeight(IntroLetterboxFraction)
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor::Black)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f - 2.0f * IntroLetterboxFraction)
		[
			SNew(SSpacer)
		]
		+ SVerticalBox::Slot().FillHeight(IntroLetterboxFraction)
		[
			SNew(SBorder).BorderImage(White).BorderBackgroundColor(FLinearColor::Black)
		];
	// 500: 게임 화면·타이틀 위젯(0)보다 위, 시작 로고의 검은 바탕(1000)보다 아래. 로고가 떠 있을 때는 로고가 덮는다.
	Viewport->AddViewportWidgetContent(Letterbox.ToSharedRef(), 500);
}

void APGTitleIntro::SetLetterboxOpacity(float Opacity)
{
	if (Letterbox.IsValid())
		Letterbox->SetRenderOpacity(FMath::Clamp(Opacity, 0.0f, 1.0f));
}

void APGTitleIntro::RemoveLetterbox()
{
	if (!Letterbox.IsValid())
		return;
	if (UWorld* World = GetWorld())
		if (UGameViewportClient* Viewport = World->GetGameViewport())
			Viewport->RemoveViewportWidgetContent(Letterbox.ToSharedRef());
	Letterbox.Reset();
}

// ---- 건너뛰기 ----

void APGTitleIntro::RegisterSkipInput()
{
	if (SkipInput.IsValid() || !FSlateApplication::IsInitialized())
		return;
	SkipInput = MakeShared<FPGTitleIntroSkipInput>();
	FSlateApplication::Get().RegisterInputPreProcessor(SkipInput);
}

void APGTitleIntro::UnregisterSkipInput()
{
	if (!SkipInput.IsValid())
		return;
	if (FSlateApplication::IsInitialized())
		FSlateApplication::Get().UnregisterInputPreProcessor(SkipInput);
	SkipInput.Reset();
}

// ---- 같이 쓰는 도구 ----

UObject* APGTitleIntro::LoadIntroObject(const TCHAR* ObjectPath, UClass* Class)
{
	// 데이터 에셋에서 칸을 비웠으면 조용히 뺀다(일부러 뺀 것이다).
	if (!ObjectPath || !*ObjectPath)
		return nullptr;
	const FString Package = FPackageName::ObjectPathToPackageName(FString(ObjectPath));
	if (!FPackageName::DoesPackageExist(Package))
	{
		UE_LOG(LogTemp, Warning, TEXT("PGTitleIntro: %s not on disk — skipped"), ObjectPath);
		return nullptr;
	}
	UObject* Asset = LoadObject<UObject>(nullptr, ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
	if (!Asset || (Class && !Asset->IsA(Class)))
	{
		UE_LOG(LogTemp, Warning, TEXT("PGTitleIntro: %s is not a %s — skipped"), ObjectPath, Class ? *Class->GetName() : TEXT("?"));
		return nullptr;
	}
	return Asset;
}

ASkeletalMeshActor* APGTitleIntro::SpawnSkeletal(USkeletalMesh* Mesh, const FTransform& Transform)
{
	UWorld* World = GetWorld();
	if (!Mesh || !World)
		return nullptr;
	ASkeletalMeshActor* Actor = World->SpawnActorDeferred<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), Transform, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Actor)
		return nullptr;
	USkeletalMeshComponent* Body = Actor->GetSkeletalMeshComponent();
	Body->SetSkeletalMeshAsset(Mesh);
	// 애님 블루프린트 없이 애니 하나씩 튼다(무대의 주인공과 같은 방식). 게임 쪽 ABP 는 폰·이동 속도를 읽어서 여기서 못 쓴다.
	Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 화면 밖에 있던 몸이 들어오는 순간 한 프레임 기준 자세로 튀지 않게 늘 포즈를 갱신한다(몇 개뿐이라 비용이 작다).
	Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Actor->FinishSpawning(Transform);
	return Actor;
}

UStaticMeshComponent* APGTitleIntro::AddBeamComponent(AActor* BeamOwner)
{
	using namespace PGTitleIntroLocal;
	UStaticMesh* Mesh = LoadIntroAsset<UStaticMesh>(*IntroPath(UPGTitleIntroSet::GetActive()->BeamMesh));
	if (!Mesh || !IsValid(BeamOwner))
		return nullptr;
	UStaticMeshComponent* Beam = NewObject<UStaticMeshComponent>(BeamOwner);
	Beam->SetStaticMesh(Mesh);
	Beam->SetMobility(EComponentMobility::Movable);
	Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Beam->SetCastShadow(false);
	Beam->SetCanEverAffectNavigation(false);
	Beam->RegisterComponent();
	Beam->SetVisibility(false);
	return Beam;
}

void APGTitleIntro::ShowBeam(UStaticMeshComponent* Beam, const FVector& From, const FVector& To, float Thickness)
{
	if (!IsValid(Beam))
		return;
	// 빔 메시는 피벗에서 +X 로 100cm(게임의 가발 광선·날으는 차 빔과 같은 메시). X 배율 = 거리/100.
	Beam->SetWorldLocationAndRotation(From, (To - From).Rotation());
	Beam->SetWorldScale3D(FVector(FVector::Dist(From, To) / 100.0f, Thickness, Thickness));
	Beam->SetVisibility(true);
}

void APGTitleIntro::SpawnDust(const FVector& Location, float Scale)
{
	using namespace PGTitleIntroLocal;
	if (UParticleSystem* Dust = LoadIntroAsset<UParticleSystem>(*IntroPath(UPGTitleIntroSet::GetActive()->DustEffect)))
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Dust, Location, FRotator::ZeroRotator, FVector(Scale), true);
}

// 총알에 맞은 작은 효과. 총알 전용 효과가 프로젝트에 없어서, 보스 팩의 "캐릭터가 맞았을 때" 효과를 작게 줄여 쓴다.
void APGTitleIntro::SpawnHitSpark(const FVector& Location, float Scale)
{
	if (UParticleSystem* Hit = LoadIntroAsset<UParticleSystem>(*IntroPath(UPGTitleIntroSet::GetActive()->HitEffect)))
		UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Hit, Location, FRotator::ZeroRotator, FVector(Scale), true);
}

// ---- 시험용 스크린샷(-PGIntroShots) ----
// 화면 없는 PC 에서도 인트로 구도를 눈으로 확인하려고 둔다. 정해 둔 시각마다 한 장, 끝난 뒤 타이틀 위젯이 뜬 그림 한 장을 찍고 게임을 끈다.

void APGTitleIntro::TickTestShots(float DeltaSeconds)
{
	if (!bTestShots || NextShot < 0)
		return;
	auto Request = [this](const FString& Suffix)
	{
		const FString File = FPaths::ScreenShotDir() / FString::Printf(TEXT("PGIntro_%s_%s.png"), GetVariantName(), *Suffix);
		FScreenshotRequest::RequestScreenshot(File, true, false);
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: screenshot %s at intro %.2fs"), *File, Clock);
	};
	if (Phase != EPGTitleIntroPhase::Done)
	{
		if (NextShot < ShotTimes.Num() && Clock >= ShotTimes[NextShot])
		{
			Request(FString::Printf(TEXT("%02d_%04.1fs"), NextShot, ShotTimes[NextShot]));
			++NextShot;
		}
		return;
	}
	AfterFinishClock += DeltaSeconds;
	if (!bFinalShotTaken && AfterFinishClock >= 1.6f)
	{
		bFinalShotTaken = true;
		Request(TEXT("99_title"));
	}
	else if (bFinalShotTaken && AfterFinishClock >= 2.6f)
	{
		NextShot = -1;
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: test shots done — quitting"));
		FPlatformMisc::RequestExit(false);
	}
}
