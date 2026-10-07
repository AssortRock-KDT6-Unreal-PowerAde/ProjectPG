#include "UI/Plan/LobbyMenuWidget.h"

#include "Components/Button.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/Plan/OptionWidget.h"
#include "UI/Plan/PlanScreenSubSystem.h"

// 옵션 화면은 로비 메뉴(100)보다 위, 알림 창(1000)보다 아래.
static constexpr int32 PlanScreenZOrder = 500;

UButton* ULobbyMenuWidget::FindButton(const TCHAR* Name) const
{
	return Cast<UButton>(GetWidgetFromName(FName(Name)));
}

void ULobbyMenuWidget::NativeConstruct()
{
	// 형님 버튼 연결이 먼저(캐릭터·게임 시작). 그 위에 우리 것(옵션·종료)을 더한다.
	Super::NativeConstruct();

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

void ULobbyMenuWidget::HandleOption()
{
	if (UPlanScreenSubSystem* Screens = UPlanScreenSubSystem::Get(this))
		Screens->OpenScreen(OptionScreenClass, PlanScreenZOrder);
}

void ULobbyMenuWidget::HandleExit()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}
