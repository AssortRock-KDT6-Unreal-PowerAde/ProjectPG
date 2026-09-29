#include "UI/PGPauseMenuWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Flow/PGPauseMenuSubsystem.h"
#include "UI/PGFlowStyle.h"
#include "UI/PGSettingsWidget.h"

// 파일 고유 이름 공간(유니티 빌드에서 다른 cpp 이름과 안 겹치게).
namespace PGPauseMenuWidgetLocal
{
	constexpr float PauseButtonWidth = 340.0f;
	constexpr float PauseButtonHeight = 58.0f;
	// 환경설정은 일시 정지 메뉴(ZOrder 80, 서브시스템이 정한다)보다 위에 뜬다.
	constexpr int32 PauseSettingsZOrder = 90;
}

void UPGPauseMenuWidget::BuildContent()
{
	// 뒤 게임 화면을 어둡게. 게임은 멈춰 있다(서브시스템이 SetGamePaused).
	UBorder* Dim = MakePanel(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), FMargin(0.0f), 0.0f);
	if (UCanvasPanelSlot* DimSlot = PlaceOnCanvas(Dim, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
	{
		DimSlot->SetAutoSize(false);
		DimSlot->SetOffsets(FMargin(0.0f));
	}

	// ---- 가운데: 제목 + 버튼 네 개 ----
	UBorder* MenuPanel = MakePanel(PGFlowStyle::PanelSolid(), FMargin(40.0f, 32.0f), 8.0f);
	PlaceOnCanvas(MenuPanel, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector);
	MenuColumn = MenuPanel;
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PauseColumn"));
	MenuPanel->SetContent(Column);
	if (UVerticalBoxSlot* TitleSlot = Column->AddChildToVerticalBox(MakeText(NSLOCTEXT("PGPause", "Title", "일시 정지"), 34, TEXT("Bold"), PGFlowStyle::Text())))
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	if (UVerticalBoxSlot* HintSlot = Column->AddChildToVerticalBox(MakeText(NSLOCTEXT("PGPause", "Hint", "Esc · F10 으로 닫기"), 14, TEXT("Regular"), PGFlowStyle::TextFaint())))
	{
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 22.0f));
	}

	// 버튼 하나 = 크기 고정 상자 + 위아래 여백. 네 개가 같은 모양이라 여기서 짠다.
	auto AddMenuButton = [this, Column](const FText& Label, EPGFlowButtonStyle Style) -> UButton*
	{
		UButton* Button = MakeButton(Label, Style, 21, FMargin(0.0f));
		if (UVerticalBoxSlot* ButtonSlot = Column->AddChildToVerticalBox(WrapSize(Button, PGPauseMenuWidgetLocal::PauseButtonWidth, PGPauseMenuWidgetLocal::PauseButtonHeight)))
			ButtonSlot->SetPadding(FMargin(0.0f, 6.0f));
		return Button;
	};
	// 노란 주 버튼은 "계속하기" 하나. 메뉴를 연 사람 대부분은 곧 게임으로 돌아간다.
	ResumeButton = AddMenuButton(NSLOCTEXT("PGPause", "BtnResume", "계속하기"), EPGFlowButtonStyle::Primary);
	SettingsButton = AddMenuButton(NSLOCTEXT("PGPause", "BtnSettings", "환경설정"), EPGFlowButtonStyle::Secondary);
	LeaveButton = AddMenuButton(NSLOCTEXT("PGPause", "BtnLobby", "로비로 돌아가기"), EPGFlowButtonStyle::Secondary);
	QuitButton = AddMenuButton(NSLOCTEXT("PGPause", "BtnQuit", "게임 종료"), EPGFlowButtonStyle::Secondary);

	// ---- 확인 창(처음엔 숨김). 메뉴 위에 겹쳐 뜬다 ----
	UBorder* ConfirmDim = MakePanel(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), FMargin(0.0f), 0.0f);
	ConfirmDim->SetHorizontalAlignment(HAlign_Center);
	ConfirmDim->SetVerticalAlignment(VAlign_Center);
	if (UCanvasPanelSlot* ConfirmSlot = PlaceOnCanvas(ConfirmDim, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
	{
		ConfirmSlot->SetAutoSize(false);
		ConfirmSlot->SetOffsets(FMargin(0.0f));
	}
	ConfirmLayer = ConfirmDim;
	UBorder* ConfirmPanel = MakePanel(PGFlowStyle::PanelSolid(), FMargin(40.0f, 30.0f), 8.0f);
	ConfirmDim->SetContent(WrapSize(ConfirmPanel, 560.0f, 0.0f));
	UVerticalBox* ConfirmBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ConfirmBox"));
	ConfirmPanel->SetContent(ConfirmBox);
	ConfirmTitle = AddLine(ConfirmBox, FText::GetEmpty(), 26, TEXT("Bold"), PGFlowStyle::Text(), FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	ConfirmBody = AddLine(ConfirmBox, FText::GetEmpty(), 17, TEXT("Regular"), PGFlowStyle::TextDim(), FMargin(0.0f, 0.0f, 0.0f, 24.0f));
	UHorizontalBox* ConfirmButtons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* RowSlot = ConfirmBox->AddChildToVerticalBox(ConfirmButtons))
		RowSlot->SetHorizontalAlignment(HAlign_Right);
	// 되돌릴 수 없는 쪽("예")을 노란 주 버튼으로 두지 않는다 — 실수로 Enter·클릭해도 안전한 "아니오" 가 먼저 눈에 띄게.
	NoButton = MakeButton(NSLOCTEXT("PGPause", "BtnNo", "아니오"), EPGFlowButtonStyle::Primary, 19, FMargin(0.0f));
	if (UHorizontalBoxSlot* NoSlot = ConfirmButtons->AddChildToHorizontalBox(WrapSize(NoButton, 150.0f, 50.0f)))
		NoSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	YesButton = MakeButton(NSLOCTEXT("PGPause", "BtnYes", "예"), EPGFlowButtonStyle::Secondary, 19, FMargin(0.0f));
	ConfirmButtons->AddChildToHorizontalBox(WrapSize(YesButton, 150.0f, 50.0f));
	// 버튼 연결·확인 창 숨기기는 NativeConstruct 가 한다 — WBP 로 뜰 때도 같은 길을 타게.
}

void UPGPauseMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 버튼 연결은 여기 한 곳(코드 화면·WBP 공용). 빠진 버튼은 건너뛴다.
	if (ResumeButton) ResumeButton->OnClicked.AddUniqueDynamic(this, &UPGPauseMenuWidget::HandleResume);
	if (SettingsButton) SettingsButton->OnClicked.AddUniqueDynamic(this, &UPGPauseMenuWidget::HandleSettings);
	if (LeaveButton) LeaveButton->OnClicked.AddUniqueDynamic(this, &UPGPauseMenuWidget::HandleLeave);
	if (QuitButton) QuitButton->OnClicked.AddUniqueDynamic(this, &UPGPauseMenuWidget::HandleQuit);
	if (NoButton) NoButton->OnClicked.AddUniqueDynamic(this, &UPGPauseMenuWidget::HandleConfirmNo);
	if (YesButton) YesButton->OnClicked.AddUniqueDynamic(this, &UPGPauseMenuWidget::HandleConfirmYes);
	if (ConfirmLayer)
		ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	SetKeyboardFocus();
}

void UPGPauseMenuWidget::NativeDestruct()
{
	// 메뉴가 닫히는데(로비로 이동 등) 환경설정이 떠 있으면 같이 닫는다. 안 닫으면 다음 레벨까지 남지는 않지만(엔진이 뗀다) 순서가 꼬인다.
	if (Settings)
	{
		Settings->OnClosed.RemoveAll(this);
		Settings->RemoveFromParent();
		Settings = nullptr;
	}
	Super::NativeDestruct();
}

FReply UPGPauseMenuWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Esc·F10: 확인 창이 떠 있으면 "아니오", 아니면 계속하기. (게임 쪽에서 여는 키와 같은 키로 닫는다.)
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F10)
	{
		if (Confirm != EPGPauseConfirm::None)
			HandleConfirmNo();
		else
			HandleResume();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPGPauseMenuWidget::HandleResume()
{
	if (UPGPauseMenuSubsystem* Pause = UPGPauseMenuSubsystem::Get(this))
		Pause->CloseMenu();
}

void UPGPauseMenuWidget::HandleSettings()
{
	Settings = UPGSettingsWidget::Open(GetOwningPlayer(), PGPauseMenuWidgetLocal::PauseSettingsZOrder);
	if (!Settings)
		return;
	// 설정이 떠 있는 동안 메뉴 판은 숨긴다(두 판이 겹쳐 보이면 어느 쪽이 눌리는지 헷갈린다).
	if (MenuColumn)
		MenuColumn->SetVisibility(ESlateVisibility::Collapsed);
	Settings->OnClosed.AddUObject(this, &UPGPauseMenuWidget::HandleSettingsClosed);
}

void UPGPauseMenuWidget::HandleSettingsClosed()
{
	Settings = nullptr;
	if (MenuColumn)
		MenuColumn->SetVisibility(ESlateVisibility::Visible);
	// 초점을 다시 가져와야 Esc 로 메뉴를 닫을 수 있다.
	SetKeyboardFocus();
}

void UPGPauseMenuWidget::HandleLeave()
{
	ShowConfirm(EPGPauseConfirm::LeaveToLobby);
}

void UPGPauseMenuWidget::HandleQuit()
{
	ShowConfirm(EPGPauseConfirm::QuitGame);
}

void UPGPauseMenuWidget::ShowConfirm(EPGPauseConfirm What)
{
	Confirm = What;
	if (!ConfirmLayer || !ConfirmTitle || !ConfirmBody)
		return;
	if (What == EPGPauseConfirm::LeaveToLobby)
	{
		ConfirmTitle->SetText(NSLOCTEXT("PGPause", "LeaveTitle", "레이드를 포기하고 로비로 갈까요?"));
		ConfirmBody->SetText(NSLOCTEXT("PGPause", "LeaveBody", "이번 판은 \"중단\" 으로 기록되고, 지금 들고 있는 아이템은 창고에 들어가지 않습니다."));
	}
	else
	{
		ConfirmTitle->SetText(NSLOCTEXT("PGPause", "QuitTitle", "게임을 종료할까요?"));
		ConfirmBody->SetText(NSLOCTEXT("PGPause", "QuitBody", "진행 중인 레이드는 기록되지 않습니다."));
	}
	ConfirmLayer->SetVisibility(ESlateVisibility::Visible);
}

void UPGPauseMenuWidget::HandleConfirmNo()
{
	Confirm = EPGPauseConfirm::None;
	if (ConfirmLayer)
		ConfirmLayer->SetVisibility(ESlateVisibility::Collapsed);
	SetKeyboardFocus();
}

void UPGPauseMenuWidget::HandleConfirmYes()
{
	const EPGPauseConfirm What = Confirm;
	Confirm = EPGPauseConfirm::None;
	UPGPauseMenuSubsystem* Pause = UPGPauseMenuSubsystem::Get(this);
	if (!Pause)
		return;
	if (What == EPGPauseConfirm::LeaveToLobby)
		Pause->LeaveToLobby();
	else if (What == EPGPauseConfirm::QuitGame)
		Pause->QuitGame();
}
