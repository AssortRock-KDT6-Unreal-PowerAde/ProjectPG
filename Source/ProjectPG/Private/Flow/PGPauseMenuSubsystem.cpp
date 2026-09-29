#include "Flow/PGPauseMenuSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Common/PGKeyPolling.h"
#include "Core/UIManagerSubSystem.h"            // 팀 UI 관리자(창이 떠 있나 보기만 한다)
#include "Engine/World.h"
#include "Flow/PGFlowSettings.h"
#include "Flow/PGGameSettings.h"
#include "Flow/PGLoadingScreenSubsystem.h"
#include "Flow/PGRunSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/PGPauseMenuWidget.h"

// 파일 고유 이름 공간(유니티 빌드에서 다른 cpp 이름과 안 겹치게).
namespace PGPauseMenuSubsystemLocal
{
	// 게임 HUD·안내 글자보다 위, 환경설정(90)보다 아래.
	constexpr int32 PauseMenuZOrder = 80;
}

UPGPauseMenuSubsystem* UPGPauseMenuSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UPGPauseMenuSubsystem>() : nullptr;
}

bool UPGPauseMenuSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 게임 월드(독립 실행·PIE)에만. 에디터 편집 화면이나 에셋 미리보기 월드에는 만들지 않는다.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE) && Super::ShouldCreateSubsystem(Outer);
}

TStatId UPGPauseMenuSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPGPauseMenuSubsystem, STATGROUP_Tickables);
}

void UPGPauseMenuSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	bIsGameMap = UPGGameSettings::IsGameMap(&InWorld);
	// 레벨이 바뀌면 오디오 장치 볼륨·FPS 제한 같은 값이 새 레벨 기준으로 다시 맞춰져야 한다(로비 60, 게임 맵은 따로 등).
	//   흐름 레벨(타이틀·로비·결과)과 게임 맵 모두 여기를 지나므로 한 곳에서 건다.
	UPGGameSettings::ApplyRuntimeSettings(&InWorld);
}

void UPGPauseMenuSubsystem::Deinitialize()
{
	if (Menu)
	{
		Menu->RemoveFromParent();
		Menu = nullptr;
	}
	// 에디터 PIE 가 끝날 때(또는 PIE 안에서 레벨을 넘을 때) 밝기·FPS 제한 콘솔 값을 에디터 원래 값으로 돌린다.
	//   레벨을 넘은 경우엔 다음 레벨의 OnWorldBeginPlay 가 곧바로 다시 건다.
	if (const UWorld* World = GetWorld())
		if (World->WorldType == EWorldType::PIE)
			UPGGameSettings::RestoreEditorOverrides();
	Super::Deinitialize();
}

bool UPGPauseMenuSubsystem::IsTeamWindowOpen() const
{
	const UUIManagerSubSystem* UiManager = UUIManagerSubSystem::Get(this);
	if (!UiManager)
		return false;
	// 팀 UIManager 는 창을 닫을 때 지우지 않고 숨긴다(CloseUI → Collapsed). 그래서 "화면에 붙어 있고 보이는가" 로 본다.
	for (const EUIType Type : { EUIType::Inventory, EUIType::EquipMent, EUIType::ItemContext, EUIType::BackPackPopup,
		EUIType::Character, EUIType::MessagePopup, EUIType::Quest })
	{
		const UUserWidget* Window = UiManager->GetUI(Type);
		if (Window && Window->IsInViewport() && Window->IsVisible())
			return true;
	}
	return false;
}

void UPGPauseMenuSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!bIsGameMap)
		return;
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->IsLocalController())
		return;
	// 키 상태는 매 프레임 읽어 둔다(눌린 "순간" 을 알려면 지난 프레임 상태가 필요하다).
	const bool bEsc = PGKeyPolling::WasPressed(PC, EKeys::Escape, bEscWasDown);
	const bool bF10 = PGKeyPolling::WasPressed(PC, EKeys::F10, bF10WasDown);
	// 메뉴가 열려 있을 때는 입력이 UI 로만 가서 여기엔 키가 안 온다. 닫는 키는 메뉴 위젯이 직접 받는다.
	if (IsMenuOpen() || !(bEsc || bF10))
		return;
	// 필드 진입 로딩(검은 화면) 중에는 열지 않는다. 로딩 뒤에서 게임이 멈추면 시작 지점 배치가 늦어진다.
	if (const UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(World))
		if (Loading->IsLoadingShown())
			return;
	if (IsTeamWindowOpen())
		return;
	OpenMenu();
}

void UPGPauseMenuSubsystem::OpenMenu()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || Menu)
		return;
	// 설정(ProjectPG Flow > Screens > Pause Menu Class)에 WBP 가 있으면 그것을 띄운다(9/23 블루프린트 분리).
	Menu = CreateWidget<UPGPauseMenuWidget>(PC, UPGFlowSettings::ResolveWidgetClass(UPGFlowSettings::Get().PauseMenuClass, UPGPauseMenuWidget::StaticClass()));
	if (!Menu)
		return;
	Menu->SetIsFocusable(true); // Esc 를 위젯이 직접 받으려면 초점을 받을 수 있어야 한다(화면에 붙이기 전에만 바꿀 수 있다)
	Menu->AddToViewport(PGPauseMenuSubsystemLocal::PauseMenuZOrder);

	// 누르고 있던 이동 키를 놓은 것으로 친다. 안 하면 메뉴를 닫은 뒤 캐릭터가 혼자 걸어간다.
	PC->FlushPressedKeys();
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(Menu->TakeWidget());
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(Mode);
	PC->SetShowMouseCursor(true);

	// 혼자 하는 게임일 때만 진짜로 멈춘다. 여럿이 하는 판(서버)에서 한 사람이 세계를 멈추면 안 된다 — 타르코프·배그도 ESC 로 안 멈춘다.
	if (World->GetNetMode() == NM_Standalone)
		bPausedByUs = UGameplayStatics::SetGamePaused(World, true);
	UE_LOG(LogTemp, Display, TEXT("PGPause: menu opened (paused=%s, %s)"), bPausedByUs ? TEXT("true") : TEXT("false"), *Menu->GetClass()->GetName());
}

void UPGPauseMenuSubsystem::CloseMenu()
{
	UWorld* World = GetWorld();
	if (Menu)
	{
		Menu->RemoveFromParent();
		Menu = nullptr;
	}
	if (bPausedByUs && World)
		UGameplayStatics::SetGamePaused(World, false);
	bPausedByUs = false;
	// 게임 조작으로 되돌린다: 커서 숨김 + 게임 입력만(팀 UIManager 가 창을 다 닫았을 때 하는 것과 같다).
	if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
	}
	// 닫을 때 누른 키가 아직 눌려 있어도 곧바로 다시 열리지 않게, "이미 눌려 있던 키" 로 적어 둔다.
	bEscWasDown = true;
	bF10WasDown = true;
	UE_LOG(LogTemp, Display, TEXT("PGPause: menu closed"));
}

void UPGPauseMenuSubsystem::LeaveToLobby()
{
	UWorld* World = GetWorld();
	if (Menu)
	{
		Menu->RemoveFromParent();
		Menu = nullptr;
	}
	if (bPausedByUs && World)
		UGameplayStatics::SetGamePaused(World, false);
	bPausedByUs = false;

	UPGRunSubsystem* Run = UPGRunSubsystem::Get(this);
	if (!Run)
		return;
	// 판을 "중단" 으로 끝낸다(공개 함수 FinishRun). 중단은 창고·통계에 안 들어가고 "지난 레이드" 에만 남는다(PGRunSubsystem 규칙).
	//   FinishRun 은 설정에 따라 몇 초 뒤 결과 화면으로 가는 타이머를 거는데, 바로 다음 줄의 GoToLobby 가 레벨을 열면서 그 타이머를 지운다.
	if (Run->IsRunActive())
		Run->FinishRun(EPGRunResult::Aborted, NSLOCTEXT("PGPause", "HowAbandoned", "레이드 포기 (메뉴에서 로비로)"));
	UE_LOG(LogTemp, Display, TEXT("PGPause: leaving to lobby (raid abandoned)"));
	Run->GoToLobby();
}

void UPGPauseMenuSubsystem::QuitGame()
{
	UWorld* World = GetWorld();
	UE_LOG(LogTemp, Display, TEXT("PGPause: quit game (menu)"));
	if (bPausedByUs && World)
		UGameplayStatics::SetGamePaused(World, false);
	bPausedByUs = false;
	UKismetSystemLibrary::QuitGame(World, World ? World->GetFirstPlayerController() : nullptr, EQuitPreference::Quit, false);
}
