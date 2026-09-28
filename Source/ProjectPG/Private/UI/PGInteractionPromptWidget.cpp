#include "UI/PGInteractionPromptWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Camera/PlayerCameraManager.h"
#include "Flow/PGFlowSettings.h"
#include "GameFramework/PlayerController.h"
#include "Objects/PGInteractionComponent.h"
#include "UI/PGUiFont.h"

namespace
{
	const FLinearColor TextWhite(0.95f, 0.95f, 0.95f, 1.0f);
	const FLinearColor TextDim(0.72f, 0.72f, 0.72f, 0.9f);
	const FLinearColor Accent(1.0f, 0.78f, 0.22f, 1.0f);     // 배그식 노란 강조
	const FLinearColor Panel(0.02f, 0.02f, 0.02f, 0.55f);
}

UPGInteractionPromptWidget* UPGInteractionPromptWidget::Create(APlayerController* Owner)
{
	if (!Owner)
		return nullptr;
	UPGInteractionPromptWidget* Widget = CreateWidget<UPGInteractionPromptWidget>(Owner,
		UPGFlowSettings::ResolveWidgetClass(UPGFlowSettings::Get().InteractionPromptClass, UPGInteractionPromptWidget::StaticClass()));
	UE_LOG(LogTemp, Display, TEXT("PGInteractionPrompt: created (%s)"), Widget ? *Widget->GetClass()->GetName() : TEXT("null"));
	return Widget;
}

TSharedRef<SWidget> UPGInteractionPromptWidget::RebuildWidget()
{
	// 위젯 블루프린트가 없으니 트리가 비어 있다. Slate 위젯을 만들기 직전에 한 번 짠다.
	if (WidgetTree && !WidgetTree->RootWidget)
		BuildTree();
	return Super::RebuildWidget();
}

UTextBlock* UPGInteractionPromptWidget::MakeText(const FName& Name, int32 Size, const FName& Typeface, const FLinearColor& Color)
{
	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
	Text->SetFont(PGUiFont::Get(Size, Typeface));
	Text->SetColorAndOpacity(FSlateColor(Color));
	// 밝은 하늘·눈밭 위에서도 읽히게 얇은 그림자.
	Text->SetShadowOffset(FVector2D(1.0f, 1.0f));
	Text->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.75f));
	return Text;
}

void UPGInteractionPromptWidget::BuildTree()
{
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Root"));
	WidgetTree->RootWidget = Root;
	SetVisibility(ESlateVisibility::HitTestInvisible); // 마우스 입력을 가로채지 않는다

	// 조준점. 3인칭에서는 "지금 무엇을 보고 있나"가 이게 없으면 안 보인다.
	UTextBlock* Dot = MakeText(TEXT("Crosshair"), 14, TEXT("Bold"), FLinearColor(1.0f, 1.0f, 1.0f, 0.85f));
	Dot->SetText(FText::FromString(TEXT("•")));
	if (UCanvasPanelSlot* DotSlot = Root->AddChildToCanvas(Dot))
	{
		DotSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		DotSlot->SetAlignment(FVector2D(0.5f, 0.5f));
		DotSlot->SetAutoSize(true);
		DotSlot->SetPosition(FVector2D::ZeroVector);
	}

	// 화면 가운데에서 살짝 아래. 조준점을 가리지 않고 시선을 많이 안 옮겨도 되는 자리.
	UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Column"));
	if (UCanvasPanelSlot* ColumnSlot = Root->AddChildToCanvas(Column))
	{
		ColumnSlot->SetAnchors(FAnchors(0.5f, 0.5f));
		ColumnSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		ColumnSlot->SetAutoSize(true);
		ColumnSlot->SetPosition(FVector2D(0.0f, 70.0f));
	}

	// [F] 문구
	PromptRow = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PromptRow"));
	PromptRow->SetBrushColor(Panel);
	PromptRow->SetPadding(FMargin(14.0f, 7.0f));
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("PromptBox"));
	PromptRow->SetContent(Row);

	UBorder* KeyCap = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("KeyCap"));
	KeyCap->SetBrushColor(Accent);
	KeyCap->SetPadding(FMargin(9.0f, 1.0f));
	UTextBlock* KeyText = MakeText(TEXT("KeyText"), 17, TEXT("Bold"), FLinearColor(0.05f, 0.05f, 0.05f, 1.0f));
	KeyText->SetShadowColorAndOpacity(FLinearColor::Transparent);
	KeyText->SetText(FText::FromString(TEXT("F")));
	KeyCap->SetContent(KeyText);
	if (UHorizontalBoxSlot* KeySlot = Row->AddChildToHorizontalBox(KeyCap))
	{
		KeySlot->SetVerticalAlignment(VAlign_Center);
		KeySlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}
	PromptText = MakeText(TEXT("PromptText"), 19, TEXT("SemiBold"), TextWhite);
	if (UHorizontalBoxSlot* TextSlot = Row->AddChildToHorizontalBox(PromptText))
		TextSlot->SetVerticalAlignment(VAlign_Center);
	if (UVerticalBoxSlot* PromptSlot = Column->AddChildToVerticalBox(PromptRow))
		PromptSlot->SetHorizontalAlignment(HAlign_Center);

	// 유지형(금고 등) 게이지. 누르고 있는 동안만 보인다.
	HoldBar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(), TEXT("HoldBar"));
	HoldBar->SetFillColorAndOpacity(Accent);
	HoldBar->SetPercent(0.0f);
	HoldBar->SetVisibility(ESlateVisibility::Collapsed);
	if (UVerticalBoxSlot* BarSlot = Column->AddChildToVerticalBox(HoldBar))
	{
		BarSlot->SetHorizontalAlignment(HAlign_Fill);
		BarSlot->SetPadding(FMargin(0.0f, 3.0f, 0.0f, 0.0f));
	}

	// 겹친 대상 목록. 후보가 둘 이상일 때만 보인다.
	ListBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ListBox"));
	if (UVerticalBoxSlot* ListSlot = Column->AddChildToVerticalBox(ListBox))
	{
		ListSlot->SetHorizontalAlignment(HAlign_Center);
		ListSlot->SetPadding(FMargin(0.0f, 8.0f, 0.0f, 0.0f));
	}
	WheelHint = MakeText(TEXT("WheelHint"), 12, TEXT("Regular"), TextDim);
	WheelHint->SetText(NSLOCTEXT("InteractionPrompt", "WheelHint", "마우스 휠로 대상 바꾸기"));
	if (UVerticalBoxSlot* HintSlot = Column->AddChildToVerticalBox(WheelHint))
	{
		HintSlot->SetHorizontalAlignment(HAlign_Center);
		HintSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	}

	// 처음 숨기기는 NativeConstruct 가 한다 — WBP 로 떠도 같은 길을 타게.
}

void UPGInteractionPromptWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible); // 마우스 입력을 가로채지 않는다
	// 대상이 생기기 전에는 문구·게이지·목록을 숨긴다(코드 화면·WBP 공용).
	if (PromptRow) PromptRow->SetVisibility(ESlateVisibility::Collapsed);
	if (HoldBar) HoldBar->SetVisibility(ESlateVisibility::Collapsed);
	if (ListBox) ListBox->SetVisibility(ESlateVisibility::Collapsed);
	if (WheelHint) WheelHint->SetVisibility(ESlateVisibility::Collapsed);
}

void UPGInteractionPromptWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 화면이 검게 덮여 있는 동안(시작 로딩 페이드 등)은 조준점·문구를 숨긴다.
	// 왜: UMG 는 카메라 페이드보다 위에 그려져서, 로딩 중 까만 화면 한가운데 흰 점만 떠 보였다.
	// 로딩 완료 신호를 따로 듣지 않고 "지금 화면이 얼마나 어두운가"만 본다 — 페이드를 쓰는 다른 연출에도 그대로 맞는다.
	const APlayerController* PC = GetOwningPlayer();
	const float Fade = (PC && PC->PlayerCameraManager && PC->PlayerCameraManager->bEnableFading) ? PC->PlayerCameraManager->FadeAmount : 0.0f;
	const ESlateVisibility Wanted = Fade > 0.5f ? ESlateVisibility::Hidden : ESlateVisibility::HitTestInvisible;
	if (GetVisibility() != Wanted)
		SetVisibility(Wanted);
}

void UPGInteractionPromptWidget::BindTo(UPGInteractionComponent* InInteraction)
{
	if (IsValid(Interaction))
	{
		Interaction->OnTargetChanged.RemoveDynamic(this, &UPGInteractionPromptWidget::HandleTargetChanged);
		Interaction->OnCandidatesChanged.RemoveDynamic(this, &UPGInteractionPromptWidget::HandleCandidatesChanged);
		Interaction->OnHoldProgress.RemoveDynamic(this, &UPGInteractionPromptWidget::HandleHoldProgress);
	}
	Interaction = InInteraction;
	if (!IsValid(Interaction))
		return;
	Interaction->OnTargetChanged.AddDynamic(this, &UPGInteractionPromptWidget::HandleTargetChanged);
	Interaction->OnCandidatesChanged.AddDynamic(this, &UPGInteractionPromptWidget::HandleCandidatesChanged);
	Interaction->OnHoldProgress.AddDynamic(this, &UPGInteractionPromptWidget::HandleHoldProgress);
	HandleTargetChanged(Interaction->GetCurrentTarget(), Interaction->GetCurrentPrompt());
	RefreshList();
}

void UPGInteractionPromptWidget::NativeDestruct()
{
	BindTo(nullptr);
	Super::NativeDestruct();
}

void UPGInteractionPromptWidget::HandleTargetChanged(AActor* Target, const FText& Prompt)
{
	if (!PromptRow || !PromptText)
		return;
	const bool bShow = IsValid(Target) && !Prompt.IsEmpty();
	PromptRow->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	PromptText->SetText(Prompt);
	RefreshList(); // 선택 강조가 같이 바뀐다
}

void UPGInteractionPromptWidget::HandleCandidatesChanged(const TArray<AActor*>& Candidates, int32 SelectedIndex)
{
	RefreshList();
}

void UPGInteractionPromptWidget::RefreshList()
{
	if (!ListBox || !WheelHint)
		return;
	ListBox->ClearChildren();
	const int32 Count = IsValid(Interaction) ? Interaction->GetCandidates().Num() : 0;
	const bool bShow = Count > 1;
	ListBox->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	WheelHint->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	if (!bShow)
		return;

	const int32 Selected = Interaction->GetSelectedIndex();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const bool bSelected = Index == Selected;
		UTextBlock* Line = MakeText(NAME_None, bSelected ? 16 : 14, bSelected ? TEXT("SemiBold") : TEXT("Regular"), bSelected ? Accent : TextDim);
		Line->SetText(FText::Format(NSLOCTEXT("InteractionPrompt", "ListLine", "{0}  {1}"),
			FText::FromString(bSelected ? TEXT("▶") : TEXT("  ")), Interaction->GetCandidatePrompt(Index)));
		if (UVerticalBoxSlot* LineSlot = ListBox->AddChildToVerticalBox(Line))
		{
			LineSlot->SetHorizontalAlignment(HAlign_Left);
			LineSlot->SetPadding(FMargin(0.0f, 1.0f));
		}
	}
}

void UPGInteractionPromptWidget::HandleHoldProgress(float Progress01)
{
	if (!HoldBar)
		return;
	HoldBar->SetPercent(Progress01);
	HoldBar->SetVisibility(Progress01 > 0.0f ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}
