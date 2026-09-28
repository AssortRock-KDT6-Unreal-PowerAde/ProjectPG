#include "UI/PGOverlayWidgets.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CircularThrobber.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Styling/CoreStyle.h"

namespace PGOverlayWidgetsLocal
{
	// 예전 Slate 와 같은 엔진 기본 글꼴(한글은 엔진 글꼴의 보조 글꼴이 그린다).
	UTextBlock* MakeOverlayText(UWidgetTree* Tree, const FName& Name, int32 Size, const FLinearColor& Color)
	{
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), Name);
		Text->SetFont(FCoreStyle::GetDefaultFontStyle("Bold", Size));
		Text->SetColorAndOpacity(FSlateColor(Color));
		return Text;
	}
}

// ---- 로딩·시작 로고 ----

TSharedRef<SWidget> UPGLoadingScreenWidget::RebuildWidget()
{
	// WBP 가 없으면(트리가 비었으면) 예전 Slate(UPGLoadingScreenSubsystem::EnsureWidget)와 같은 모양을 짓는다.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		using namespace PGOverlayWidgetsLocal;
		UOverlay* LayerRoot = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("LoadingRoot"));
		WidgetTree->RootWidget = LayerRoot;

		Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Backdrop"));
		Backdrop->SetBrushColor(FLinearColor::Black);
		if (UOverlaySlot* BackSlot = LayerRoot->AddChildToOverlay(Backdrop))
		{
			BackSlot->SetHorizontalAlignment(HAlign_Fill);
			BackSlot->SetVerticalAlignment(VAlign_Fill);
		}

		// 시작 로고: 가운데 크게. 글자 두 덩이(PROJECT 흰색 + PG 노랑) — 타이틀 화면 로고와 같은 색.
		UHorizontalBox* Logo = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SplashLogo"));
		SplashLogo = Logo;
		if (UOverlaySlot* LogoSlot = LayerRoot->AddChildToOverlay(Logo))
		{
			LogoSlot->SetHorizontalAlignment(HAlign_Center);
			LogoSlot->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* LogoA = MakeOverlayText(WidgetTree, TEXT("SplashLogoA"), 96, FLinearColor(0.95f, 0.95f, 0.95f));
		LogoA->SetText(NSLOCTEXT("PGLoading", "LogoA", "PROJECT"));
		if (UHorizontalBoxSlot* ASlot = Logo->AddChildToHorizontalBox(LogoA))
			ASlot->SetPadding(FMargin(0.0f, 0.0f, 28.0f, 0.0f));
		UTextBlock* LogoB = MakeOverlayText(WidgetTree, TEXT("SplashLogoB"), 96, FLinearColor(0.91f, 0.77f, 0.28f));
		LogoB->SetText(NSLOCTEXT("PGLoading", "LogoB", "PG"));
		Logo->AddChildToHorizontalBox(LogoB);

		// 필드 진입 로딩: 오른쪽 아래 글자 + 도는 표시.
		UHorizontalBox* Corner = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("LoadingCorner"));
		LoadingCorner = Corner;
		if (UOverlaySlot* CornerSlot = LayerRoot->AddChildToOverlay(Corner))
		{
			CornerSlot->SetHorizontalAlignment(HAlign_Right);
			CornerSlot->SetVerticalAlignment(VAlign_Bottom);
			CornerSlot->SetPadding(FMargin(0.0f, 0.0f, 64.0f, 56.0f));
		}
		LoadingText = MakeOverlayText(WidgetTree, TEXT("LoadingText"), 22, FLinearColor(0.85f, 0.85f, 0.85f));
		if (UHorizontalBoxSlot* TextSlot = Corner->AddChildToHorizontalBox(LoadingText))
		{
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));
		}
		UCircularThrobber* Throbber = WidgetTree->ConstructWidget<UCircularThrobber>(UCircularThrobber::StaticClass(), TEXT("LoadingThrobber"));
		Throbber->SetRadius(14.0f);
		if (UHorizontalBoxSlot* ThrobberSlot = Corner->AddChildToHorizontalBox(Throbber))
			ThrobberSlot->SetVerticalAlignment(VAlign_Center);
	}
	// 둘 다 처음엔 숨김 — 서브시스템이 로고·로딩 중 필요한 쪽만 켠다(WBP 로 떠도 같다).
	if (SplashLogo)
		SplashLogo->SetVisibility(ESlateVisibility::Collapsed);
	if (LoadingCorner)
		LoadingCorner->SetVisibility(ESlateVisibility::Collapsed);
	return Super::RebuildWidget();
}

void UPGLoadingScreenWidget::SetLoadingText(const FText& Text)
{
	if (LoadingText)
		LoadingText->SetText(Text);
}

void UPGLoadingScreenWidget::ShowLoadingCorner(bool bShow)
{
	if (LoadingCorner)
		LoadingCorner->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UPGLoadingScreenWidget::ShowSplashLogo(bool bShow)
{
	if (SplashLogo)
		SplashLogo->SetVisibility(bShow ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UPGLoadingScreenWidget::SetSplashOpacity(float Opacity)
{
	if (SplashLogo)
		SplashLogo->SetRenderOpacity(Opacity);
}

// ---- 안내·카운트다운 한 줄 ----

UPGAnnounceLineWidget::UPGAnnounceLineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TopFill = 0.26f;
	FontSize = 34;
	LineColor = FLinearColor::White;
}

UPGCountdownLineWidget::UPGCountdownLineWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	TopFill = 0.58f;
	FontSize = 44;
	LineColor = FLinearColor(0.55f, 1.0f, 0.55f, 1.0f);
}

TSharedRef<SWidget> UPGScreenLineWidget::RebuildWidget()
{
	// WBP 가 없으면 예전 Slate(UPGAnnounceSubsystem)와 같은 모양: 위 빈칸(TopFill) + 가운데 글자 + 아래 빈칸.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Column = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("LineColumn"));
		WidgetTree->RootWidget = Column;
		if (UVerticalBoxSlot* TopSlot = Column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("TopSpace"))))
		{
			FSlateChildSize TopSize(ESlateSizeRule::Fill);
			TopSize.Value = TopFill;
			TopSlot->SetSize(TopSize);
		}
		LineText = PGOverlayWidgetsLocal::MakeOverlayText(WidgetTree, TEXT("LineText"), FontSize, LineColor);
		LineText->SetShadowOffset(FVector2D(2.0f, 2.0f));
		LineText->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.85f));
		LineText->SetJustification(ETextJustify::Center);
		if (UVerticalBoxSlot* TextSlot = Column->AddChildToVerticalBox(LineText))
			TextSlot->SetHorizontalAlignment(HAlign_Center);
		if (UVerticalBoxSlot* BottomSlot = Column->AddChildToVerticalBox(WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass(), TEXT("BottomSpace"))))
		{
			FSlateChildSize BottomSize(ESlateSizeRule::Fill);
			BottomSize.Value = 1.0f - TopFill;
			BottomSlot->SetSize(BottomSize);
		}
	}
	return Super::RebuildWidget();
}

void UPGScreenLineWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::HitTestInvisible); // 마우스·클릭을 가로채지 않는다
}

void UPGScreenLineWidget::SetLine(const FText& Text)
{
	if (LineText)
		LineText->SetText(Text);
}

void UPGScreenLineWidget::SetLineOpacity(float Opacity)
{
	if (LineText)
		LineText->SetRenderOpacity(Opacity);
}
