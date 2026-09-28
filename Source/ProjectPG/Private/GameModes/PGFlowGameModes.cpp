#include "GameModes/PGFlowGameModes.h"
#include "Containers/Ticker.h"
#include "Flow/PGFlowSettings.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "Flow/PGPauseMenuSubsystem.h"
#include "UI/PGSettingsWidget.h"
#include "Flow/PGLoadingScreenSubsystem.h"
#include "Flow/PGTitleIntro.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/SpectatorPawn.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "TimerManager.h"
#include "UI/PGFlowWidgets.h"

namespace PGFlowGameModesLocal
{
	const TCHAR* ScreenName(EPGFlowScreen Screen)
	{
		switch (Screen)
		{
		case EPGFlowScreen::Title: return TEXT("Title");
		case EPGFlowScreen::Lobby: return TEXT("Lobby");
		case EPGFlowScreen::Game: return TEXT("Game");
		case EPGFlowScreen::Scoreboard: return TEXT("Scoreboard");
		}
		return TEXT("?");
	}
}

// ---- 플레이어 컨트롤러 (화면 연출은 각자 컴퓨터에서) ----

APGFlowPlayerController::APGFlowPlayerController()
{
	// 타이틀 카메라 흐름·불빛 깜빡임·인트로를 매 틱 돌린다. 계산은 사인 몇 개라 공짜에 가깝다.
	PrimaryActorTick.bCanEverTick = true;
}

void APGFlowPlayerController::BeginPlay()
{
	Super::BeginPlay();
	// 전용 서버에 있는 "남의 컨트롤러" 는 화면이 없다. 화면은 자기 컴퓨터의 컨트롤러만 만든다.
	if (!IsLocalController())
		return;
	// GameAndUI: 버튼을 누르면서도 ` 콘솔이 열려야 한다(PG.Flow.* 명령으로 흐름을 시험한다). UIOnly 면 콘솔 키가 안 먹는다.
	bShowMouseCursor = true;
	FInputModeGameAndUI Mode;
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	Mode.SetHideCursorDuringCapture(false);
	SetInputMode(Mode);
	StartPresentation();
}

const APGFlowGameModeBase* APGFlowPlayerController::GetFlowConfig() const
{
	const UWorld* World = GetWorld();
	if (!World)
		return nullptr;
	// 혼자 할 때·서버: 진짜 게임모드(레벨에서 바꾼 값까지 그대로).
	if (const APGFlowGameModeBase* GameMode = World->GetAuthGameMode<APGFlowGameModeBase>())
		return GameMode;
	// 클라이언트: 게임모드는 없지만 GameState 가 게임모드 종류를 복제해 준다 → 그 종류의 기본값을 읽는다.
	const AGameStateBase* GameState = World->GetGameState();
	if (GameState && GameState->GameModeClass && GameState->GameModeClass->IsChildOf(APGFlowGameModeBase::StaticClass()))
		return GameState->GameModeClass->GetDefaultObject<APGFlowGameModeBase>();
	return nullptr;
}

void APGFlowPlayerController::StartPresentation()
{
	if (bPresentationStarted)
		return;
	const APGFlowGameModeBase* Config = GetFlowConfig();
	if (!Config)
	{
		// 클라이언트는 GameState 가 막 복제되는 중일 수 있다. 잠깐 뒤에 다시.
		GetWorldTimerManager().SetTimer(RetryTimer, this, &APGFlowPlayerController::StartPresentation, 0.1f, false);
		return;
	}
	bPresentationStarted = true;
	Screen = Config->Screen;
	UE_LOG(LogTemp, Display, TEXT("PGFlow: %s screen begun on %s (widget class %s, %s)"),
		PGFlowGameModesLocal::ScreenName(Screen), *GetWorld()->GetMapName(), *GetNameSafe(Config->ScreenWidgetClass.Get()),
		GetWorld()->GetAuthGameMode() ? TEXT("game mode here") : TEXT("client - config from game mode class"));
	// 게임을 켜고 처음 타이틀이 뜰 때만 검은 화면에 PROJECT PG 로고를 한 번 보여 준다(9/22 사용자). 타이틀은 그 뒤에서 이미 준비된다.
	if (Screen == EPGFlowScreen::Title)
		if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(this))
			Loading->PlaySplashOnce();
	// 게임 맵에서 돌아올 때 로딩 화면이 남아 있으면 걷는다(탈출·사망으로 판이 끝나 로비·결과로 넘어온 경우).
	if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(this); Loading && Loading->IsLoadingShown())
		Loading->HideLoading(TEXT("flow screen opened"));
	CreateScreenWidget();
}

void APGFlowPlayerController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!IsLocalController())
		return;
	if (bStageBuilt)
		PGFlowStage::Animate(Stage, Screen, GetWorld()->GetTimeSeconds());
	// 인트로는 Animate 다음에 돈다: Animate 가 놓은 "평소 타이틀 카메라 자리" 를 인트로가 마지막 컷으로 읽고 그 위에 덮어쓴다.
	if (IsValid(TitleIntro))
	{
		TitleIntro->Advance(DeltaSeconds);
		if (TitleIntro->IsFinished() && !IsValid(ScreenWidget))
		{
			CreateScreenWidget();
			if (IsValid(ScreenWidget))
			{
				ScreenWidget->SetRenderOpacity(0.0f);
				WidgetFadeAge = 0.0f;
			}
		}
	}
	// 인트로 뒤 타이틀 위젯이 0.8초에 걸쳐 나타난다(갑자기 뜨면 인트로의 여운이 끊긴다).
	if (WidgetFadeAge >= 0.0f && IsValid(ScreenWidget))
	{
		WidgetFadeAge += DeltaSeconds;
		ScreenWidget->SetRenderOpacity(FMath::Clamp(WidgetFadeAge / 0.8f, 0.0f, 1.0f));
		if (WidgetFadeAge >= 0.8f)
			WidgetFadeAge = -1.0f;
	}
}

void APGFlowPlayerController::BuildStage()
{
	const APGFlowGameModeBase* Config = GetFlowConfig();
	if (bStageBuilt || !Config || !Config->bBuildStage)
		return;
	bStageBuilt = true;
	// 무대는 이 컴퓨터에만 짓는다(복제하지 않는 연출용 액터). 서버·다른 사람 화면과 상관없다.
	Stage = PGFlowStage::Build(GetWorld(), Screen);
	if (ACameraActor* Camera = Stage.Camera.Get())
	{
		// 자동 카메라 관리를 끈다. 켜 두면 관전 폰을 빙의할 때마다 시점이 폰으로 되돌아가 무대 카메라가 풀린다.
		bAutoManageActiveCameraTarget = false;
		SetViewTarget(Camera);
	}
}

void APGFlowPlayerController::CreateScreenWidget()
{
	UWorld* World = GetWorld();
	const APGFlowGameModeBase* Config = GetFlowConfig();
	if (!IsValid(World) || IsValid(ScreenWidget) || !Config)
		return;
	// 무대는 위젯과 상관없이 짓는다 — UI 담당 위젯으로 바꿔도, 위젯 클래스가 비어 있어도 뒤 배경은 나온다.
	BuildStage();
	// 게임을 켜고 처음 여는 타이틀이면 인트로부터(9/22 사용자: 시작 로고 → 인트로 → 타이틀). 인트로가 끝나면 Tick 이 이 함수를 다시 부른다.
	//   위젯을 미루는 이유: 위젯이 뜨면 서버 확인·로그인 창이 바로 시작되는데, 인트로 위에 그게 겹치면 안 된다.
	if (Screen == EPGFlowScreen::Title && !IsValid(TitleIntro))
		TitleIntro = APGTitleIntro::StartOnce(World, Stage.Camera.Get());
	if (IsValid(TitleIntro) && !TitleIntro->IsFinished())
		return;
	// 설정(ProjectPG Flow > Screens)에 WBP 가 있으면 그것을, 없으면 코드로 짠 기본 화면(ScreenWidgetClass)을 띄운다.
	TSubclassOf<UUserWidget> WidgetClass = Config->ScreenWidgetClass;
	const UPGFlowSettings& Settings = UPGFlowSettings::Get();
	const TSoftClassPtr<UUserWidget>& Designed = Screen == EPGFlowScreen::Title ? Settings.TitleScreenClass
		: Screen == EPGFlowScreen::Lobby ? Settings.LobbyScreenClass
		: Settings.ScoreboardScreenClass;
	if (!Designed.IsNull())
	{
		if (UClass* Loaded = Designed.LoadSynchronous())
			WidgetClass = Loaded;
		else
			UE_LOG(LogTemp, Warning, TEXT("PGFlow: screen WBP %s not found — using the code-built screen"), *Designed.ToString());
	}
	if (!WidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("PGFlow: %s has no ScreenWidgetClass — nothing to show"), *GetNameSafe(Config));
		return;
	}
	ScreenWidget = CreateWidget<UUserWidget>(this, WidgetClass);
	if (!IsValid(ScreenWidget))
		return;
	ScreenWidget->AddToViewport();
	UE_LOG(LogTemp, Display, TEXT("PGFlow: %s widget %s on screen"), PGFlowGameModesLocal::ScreenName(Screen), *ScreenWidget->GetName());
	TakeFlowShotIfRequested(WidgetClass);
	// BP 이벤트는 게임모드가 있는 컴퓨터에서만(혼자 할 때·서버). 클라이언트의 설정은 기본값(CDO)이라 이벤트를 부르지 않는다.
	if (APGFlowGameModeBase* GameMode = World->GetAuthGameMode<APGFlowGameModeBase>())
		GameMode->OnFlowScreenReady(ScreenWidget);
}

void APGFlowPlayerController::TakeFlowShotIfRequested(TSubclassOf<UUserWidget> WidgetClass)
{
	UWorld* World = GetWorld();
	// 시험용(-PGFlowShot): 화면이 뜨고 2초 뒤 한 장 찍고 끈다. WBP 로 옮기기 전·후 그림을 나란히 비교하려고 둔다(9/23 블루프린트 분리).
	//   -PGFlowShotOverlay=Pause|Settings 를 같이 주면 그 위에 뜨는 창(ESC 메뉴·환경설정)까지 열어 놓고 찍는다.
	//   타이머가 아니라 코어 티커를 쓰는 이유: ESC 메뉴는 게임을 멈춰(SetGamePaused) 월드 타이머가 멈춘다.
	if (!FParse::Param(FCommandLine::Get(), TEXT("PGFlowShot")))
		return;
	FString Overlay;
	FParse::Value(FCommandLine::Get(), TEXT("PGFlowShotOverlay="), Overlay);
	const FString Tag = FString::Printf(TEXT("PGFlow_%s_%s%s"), PGFlowGameModesLocal::ScreenName(Screen), *WidgetClass->GetName(),
		Overlay.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("_%s"), *Overlay));
	if (Overlay.Equals(TEXT("Pause"), ESearchCase::IgnoreCase))
	{
		if (UPGPauseMenuSubsystem* Pause = UPGPauseMenuSubsystem::Get(World))
			Pause->OpenMenu();
	}
	else if (Overlay.Equals(TEXT("Settings"), ESearchCase::IgnoreCase))
	{
		UPGSettingsWidget::Open(this, 50);
	}
	else if (Overlay.Equals(TEXT("Loading"), ESearchCase::IgnoreCase))
	{
		if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(World))
			Loading->ShowLoading(NSLOCTEXT("PGFlow", "ShotLoading", "필드에 진입 중"));
	}
	else if (Overlay.Equals(TEXT("Splash"), ESearchCase::IgnoreCase))
	{
		// 시작 로고(1.2초 페이드인 → 1.4초 머묾). 2초 캡처 때 글자가 다 보인다.
		if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(World))
			Loading->PlaySplashOnce();
	}
	else if (Overlay.Equals(TEXT("Announce"), ESearchCase::IgnoreCase))
	{
		// 안내 줄(1.8초에 걸쳐 나타난다 — 2초 캡처 때는 거의 다 보인다) + 카운트다운 한 줄.
		if (UPGAnnounceSubsystem* Announce = UPGAnnounceSubsystem::Get(World))
		{
			Announce->Announce({ NSLOCTEXT("PGFlow", "ShotAnnounce", "드래곤을 쓰러뜨렸습니다 · 탈출구로 가세요") });
			Announce->SetCountdown(NSLOCTEXT("PGFlow", "ShotCountdown", "탈출까지 3"));
		}
	}
	double Clock = 0.0;
	bool bShotTaken = false;
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[Tag, Clock, bShotTaken](float Delta) mutable
		{
			Clock += Delta;
			if (!bShotTaken && Clock >= 2.0)
			{
				bShotTaken = true;
				FScreenshotRequest::RequestScreenshot(FPaths::ScreenShotDir() / Tag + TEXT(".png"), true, false);
				UE_LOG(LogTemp, Display, TEXT("PGFlow: screenshot %s"), *Tag);
			}
			if (Clock >= 3.0)
			{
				FPlatformMisc::RequestExit(false);
				return false;
			}
			return true;
		}), 0.0f);
}

// ---- 게임모드 바탕 (설정만) ----

APGFlowGameModeBase::APGFlowGameModeBase()
{
	// 관전 폰: 움직이는 캐릭터가 없어야 한다. 레벨에 PlayerStart 가 없어도(빈 Entry 맵) 원점에 생긴다.
	DefaultPawnClass = ASpectatorPawn::StaticClass();
	PlayerControllerClass = APGFlowPlayerController::StaticClass();
}

UUserWidget* APGFlowGameModeBase::GetScreenWidget() const
{
	if (const UWorld* World = GetWorld())
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			if (const APGFlowPlayerController* PC = Cast<APGFlowPlayerController>(It->Get()); PC && PC->IsLocalController())
				return PC->GetScreenWidget();
	return nullptr;
}

// ---- 화면별 ----

APGTitleGameMode::APGTitleGameMode()
{
	Screen = EPGFlowScreen::Title;
	ScreenWidgetClass = UPGTitleScreenWidget::StaticClass();
}

APGLobbyGameMode::APGLobbyGameMode()
{
	Screen = EPGFlowScreen::Lobby;
	ScreenWidgetClass = UPGLobbyScreenWidget::StaticClass();
}

APGScoreboardGameMode::APGScoreboardGameMode()
{
	Screen = EPGFlowScreen::Scoreboard;
	ScreenWidgetClass = UPGScoreboardScreenWidget::StaticClass();
}
