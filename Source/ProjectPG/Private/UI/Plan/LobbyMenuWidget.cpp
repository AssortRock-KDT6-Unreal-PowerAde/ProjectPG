#include "UI/Plan/LobbyMenuWidget.h"

#include "Components/Button.h"
#include "Core/UIManagerSubSystem.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Server/MatchmakingSubSystem.h"
#include "UI/Plan/MatchingWidget.h"
#include "UI/Plan/OptionWidget.h"
#include "UI/Plan/PlanScreenSubSystem.h"
#include "UI/Plan/TitleStageSubSystem.h"

// 옵션·매칭 화면은 로비 메뉴(100)보다 위, 알림 창(1000)보다 아래.
static constexpr int32 PlanScreenZOrder = 500;

UButton* ULobbyMenuWidget::FindButton(const TCHAR* Name) const
{
	return Cast<UButton>(GetWidgetFromName(FName(Name)));
}

void ULobbyMenuWidget::NativeConstruct()
{
	// 형님 버튼 연결이 먼저(캐릭터·게임 시작). 그 위에 우리 것을 더한다.
	Super::NativeConstruct();

	if (UButton* Button = FindButton(TEXT("CharacterBtn")))
	{
		Button->OnClicked.RemoveDynamic(this, &ULobbyMenuWidget::HandleCharacter);
		Button->OnClicked.AddDynamic(this, &ULobbyMenuWidget::HandleCharacter);
	}

	if (UButton* Button = FindButton(TEXT("GameStartBtn")))
	{
		// 형님 처리(알림 창 + 매칭 요청)를 떼고, 매칭 화면 + 같은 매칭 요청으로 바꾼다.
		Button->OnClicked.RemoveDynamic(this, &ULobbyWidget::OnClickedGameStartButton);
		Button->OnClicked.RemoveDynamic(this, &ULobbyMenuWidget::HandleGameStart);
		Button->OnClicked.AddDynamic(this, &ULobbyMenuWidget::HandleGameStart);
	}
	if (UButton* Button = FindButton(TEXT("OptionBtn")))
	{
		Button->OnClicked.RemoveDynamic(this, &ULobbyMenuWidget::HandleOption);
		Button->OnClicked.AddDynamic(this, &ULobbyMenuWidget::HandleOption);
	}
	if (UButton* Button = FindButton(TEXT("ExitBtn")))
	{
		Button->OnClicked.RemoveDynamic(this, &ULobbyMenuWidget::HandleExit);
		Button->OnClicked.AddDynamic(this, &ULobbyMenuWidget::HandleExit);
	}
}

void ULobbyMenuWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(GetWorld());
	const UUserWidget* CharacterWindow = UI ? UI->GetUI(EUIType::Character) : nullptr;
	const bool bOpen = CharacterWindow && CharacterWindow->IsInViewport() && CharacterWindow->IsVisible();
	if (bOpen == bCharacterWindowWasOpen)
		return;
	bCharacterWindowWasOpen = bOpen;
	// 캐릭터 창과 메뉴 버튼이 겹쳐 보이지 않게, 창이 떠 있는 동안 메뉴 버튼(맨 바깥 판)을 숨긴다.
	// 이 위젯 자체는 화면에 남겨 둬야 계속 창 닫힘을 알아챌 수 있어서, 안쪽 판만 숨긴다.
	if (UWidget* Root = GetRootWidget())
	{
		if (bOpen)
			RootVisibility = Root->GetVisibility();
		Root->SetVisibility(bOpen ? ESlateVisibility::Collapsed : RootVisibility);
	}
	if (!bOpen)
	{
		if (UTitleStageSubSystem* Stage = UTitleStageSubSystem::Get(this))
			Stage->FocusCamera(MenuCameraTag, CameraBlendSeconds);
	}
}

// 창은 형님 처리(OnClickedCharacterButton)가 연다. 여기서는 카메라만 캐릭터 앞으로.
void ULobbyMenuWidget::HandleCharacter()
{
	if (UTitleStageSubSystem* Stage = UTitleStageSubSystem::Get(this))
		Stage->FocusCamera(CharacterCameraTag, CameraBlendSeconds);
}

void ULobbyMenuWidget::HandleGameStart()
{
	if (UPlanScreenSubSystem* Screens = UPlanScreenSubSystem::Get(this))
		Screens->OpenScreen(MatchingScreenClass, PlanScreenZOrder);
	// 매칭 요청은 형님 것 그대로.
	if (UMatchmakingSubSystem* Match = UMatchmakingSubSystem::Get(GetWorld()))
		Match->RequestGameStart();
}

void ULobbyMenuWidget::HandleOption()
{
	if (UPlanScreenSubSystem* Screens = UPlanScreenSubSystem::Get(this))
		Screens->OpenScreen(OptionScreenClass, PlanScreenZOrder);
}

void ULobbyMenuWidget::HandleExit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
