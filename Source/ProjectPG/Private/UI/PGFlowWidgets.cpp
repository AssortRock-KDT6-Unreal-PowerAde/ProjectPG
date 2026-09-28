#include "UI/PGFlowWidgets.h"

#include "Async/Async.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Core/UIManagerSubSystem.h"            // 팀 UI 관리자(부르기만 한다)
#include "Engine/GameInstance.h"
#include "FileMediaSource.h"
#include "Flow/PGFlowStage.h"
#include "Flow/PGRunSubsystem.h"
#include "IWebSocket.h"
#include "Kismet/KismetSystemLibrary.h"
#include "MediaPlayer.h"
#include "MediaTexture.h"
#include "Modules/ModuleManager.h"
#include "Server/AuthSubSystem.h"               // 팀 로그인(부르기·듣기만 한다)
#include "Server/WebSocketSubSystem.h"          // 팀 소켓(부르기만 한다)
#include "TimerManager.h"
#include "UI/Controller/LobbyUIFlowController.h" // 팀 로그인 흐름(부르기만 한다)
#include "UI/PGFlowStyle.h"
#include "UI/PGSettingsWidget.h"
#include "UI/PGUiFont.h"
#include "WebSocketsModule.h"

// 파일 고유 이름 공간(유니티 빌드에서 다른 cpp 상수와 안 겹치게).
namespace PGFlowWidgetsLocal
{
	// 색 값은 UI/PGFlowStyle.h 한 곳에 있다(환경설정·일시 정지 화면과 같은 색을 쓰려고). 여기서는 짧은 이름만 붙인다.
	FLinearColor FlowSrgb(uint8 R, uint8 G, uint8 B, float Alpha = 1.0f) { return PGFlowStyle::Srgb(R, G, B, Alpha); }

	const FLinearColor FlowAccent = PGFlowStyle::Accent();        // 노란 강조 #E8C547
	const FLinearColor FlowAccentHover = PGFlowStyle::AccentHover();
	const FLinearColor FlowAccentPressed = PGFlowStyle::AccentPressed();
	const FLinearColor FlowInk = PGFlowStyle::Ink();              // 노란 버튼 위 글자
	const FLinearColor FlowText = PGFlowStyle::Text();
	const FLinearColor FlowTextDim = PGFlowStyle::TextDim();
	const FLinearColor FlowTextFaint = PGFlowStyle::TextFaint();
	const FLinearColor FlowPanel = PGFlowStyle::Panel();          // 어두운 반투명 판
	const FLinearColor FlowBar = PGFlowStyle::Bar();              // 위 메뉴 줄
	const FLinearColor FlowButton = PGFlowStyle::Button();
	const FLinearColor FlowButtonHover = PGFlowStyle::ButtonHover();
	const FLinearColor FlowRowTint = PGFlowStyle::RowTint();
	const FLinearColor FlowDivider = PGFlowStyle::Divider();
	const FLinearColor FlowGood = PGFlowStyle::Good();
	const FLinearColor FlowBad = PGFlowStyle::Bad();
	const FLinearColor FlowWarn = PGFlowStyle::Warn();
	const FLinearColor FlowNeutral = PGFlowStyle::Neutral();
	const FLinearColor FlowGoodText = PGFlowStyle::GoodText();
	const FLinearColor FlowBadText = PGFlowStyle::BadText();

	// 목록은 스크롤이 되지만, 한 판에 수백 종이 나올 일은 없으니 그리는 줄 수만 막아 둔다.
	constexpr int32 FlowMaxListRows = 60;

	// 팀 UWebSocketSubSystem::ConnectToLobbyServer 에 박혀 있는 주소와 같다(그쪽 파일은 우리가 못 고친다 — 바뀌면 여기도 같이).
	const TCHAR* const FlowLobbyServerUrl = TEXT("ws://127.0.0.1:8080");
	// 서버 확인을 이만큼 기다리고 답이 없으면 꺼진 것으로 본다.
	constexpr float FlowProbeTimeoutSeconds = 3.0f;
	// 로그인 성공 뒤 로비로 넘어가기 전 잠깐(팀 쪽이 인벤토리 요청을 보낼 시간 + "성공" 문구를 읽을 시간).
	constexpr float FlowLeaveDelaySeconds = 1.2f;

	// 둥근 모서리 단색 브러시(모양은 PGFlowStyle::FlatBrush).
	FSlateBrush FlowFlatBrush(const FLinearColor& Color, float Radius) { return PGFlowStyle::FlatBrush(Color, Radius); }
}

// (using namespace 를 안 쓴다: 유니티 빌드에서는 여러 cpp 가 한 파일로 합쳐져 뒤 파일까지 이름이 새어 나간다.)

// ---- 공통 틀 ----

TSharedRef<SWidget> UPGFlowScreenWidget::RebuildWidget()
{
	// 위젯 블루프린트가 없으니 트리가 비어 있다. Slate 위젯을 만들기 직전에 한 번 짠다.
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		bCodeBuilt = true;
		CodeRoot = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("CodeRoot"));
		WidgetTree->RootWidget = CodeRoot;
		BuildContent();
	}
	else if (WidgetTree && !bDesignedBuilt)
	{
		// WBP 로 떴다. 배치는 WBP 가 들고 있고, 코드가 채워야 할 것만 채운다.
		bDesignedBuilt = true;
		BuildIntoDesigned();
	}
	return Super::RebuildWidget();
}

UPGRunSubsystem* UPGFlowScreenWidget::GetRun() const
{
	return UPGRunSubsystem::Get(this);
}

UTextBlock* UPGFlowScreenWidget::MakeText(const FText& Text, int32 Size, const FName& Typeface, const FLinearColor& Color)
{
	UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	// 한글 글꼴(Pretendard)은 PGUiFont 가 파일에서 직접 읽는다. 엔진 기본 글꼴은 한글이 네모로 나온다.
	Block->SetFont(PGUiFont::Get(Size, Typeface));
	Block->SetColorAndOpacity(FSlateColor(Color));
	Block->SetText(Text);
	// 3D 배경 위에 얹는 글자라 옅은 그림자를 깔아 밝은 하늘 앞에서도 읽히게.
	Block->SetShadowOffset(FVector2D(0.0f, 1.5f));
	Block->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f));
	return Block;
}

UBorder* UPGFlowScreenWidget::MakePanel(const FLinearColor& Color, const FMargin& InPadding, float Radius)
{
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Panel->SetBrush(PGFlowWidgetsLocal::FlowFlatBrush(Color, Radius));
	Panel->SetPadding(InPadding);
	return Panel;
}

UButton* UPGFlowScreenWidget::MakeButton(const FText& Label, EPGFlowButtonStyle Style, int32 FontSize, const FMargin& InPadding, UTextBlock** OutLabel)
{
	UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	FLinearColor Normal = PGFlowWidgetsLocal::FlowButton, Hover = PGFlowWidgetsLocal::FlowButtonHover, Pressed = PGFlowWidgetsLocal::FlowButton, TextColor = PGFlowWidgetsLocal::FlowText;
	FName Typeface = TEXT("SemiBold");
	switch (Style)
	{
	case EPGFlowButtonStyle::Primary:
		Normal = PGFlowWidgetsLocal::FlowAccent; Hover = PGFlowWidgetsLocal::FlowAccentHover; Pressed = PGFlowWidgetsLocal::FlowAccentPressed; TextColor = PGFlowWidgetsLocal::FlowInk; Typeface = TEXT("Bold");
		break;
	case EPGFlowButtonStyle::Ghost:
		Normal = FLinearColor::Transparent; Hover = PGFlowWidgetsLocal::FlowSrgb(0xFF, 0xFF, 0xFF, 0.06f); Pressed = PGFlowWidgetsLocal::FlowSrgb(0xFF, 0xFF, 0xFF, 0.10f); TextColor = PGFlowWidgetsLocal::FlowTextDim;
		break;
	default:
		break;
	}
	FButtonStyle ButtonStyle;
	ButtonStyle.SetNormal(PGFlowWidgetsLocal::FlowFlatBrush(Normal, 4.0f));
	ButtonStyle.SetHovered(PGFlowWidgetsLocal::FlowFlatBrush(Hover, 4.0f));
	ButtonStyle.SetPressed(PGFlowWidgetsLocal::FlowFlatBrush(Pressed, 4.0f));
	ButtonStyle.SetDisabled(PGFlowWidgetsLocal::FlowFlatBrush(PGFlowWidgetsLocal::FlowSrgb(0x30, 0x30, 0x30, 0.6f), 4.0f));
	ButtonStyle.SetNormalPadding(FMargin(0.0f));
	ButtonStyle.SetPressedPadding(FMargin(0.0f));
	Button->SetStyle(ButtonStyle);

	UTextBlock* Text = MakeText(Label, FontSize, Typeface, TextColor);
	if (Style == EPGFlowButtonStyle::Primary)
		Text->SetShadowColorAndOpacity(FLinearColor::Transparent); // 노란 바탕 위 검은 글자엔 그림자가 번져 보인다
	if (UButtonSlot* TextSlot = Cast<UButtonSlot>(Button->AddChild(Text)))
	{
		TextSlot->SetPadding(InPadding);
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (OutLabel)
		*OutLabel = Text;
	return Button;
}

UCanvasPanelSlot* UPGFlowScreenWidget::PlaceOnCanvas(UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment, const FVector2D& Position,
	const FVector2D& Size)
{
	UCanvasPanelSlot* CanvasSlot = CodeRoot->AddChildToCanvas(Widget);
	if (!CanvasSlot)
		return nullptr;
	CanvasSlot->SetAnchors(Anchors);
	CanvasSlot->SetAlignment(Alignment);
	CanvasSlot->SetPosition(Position);
	if (Size.IsNearlyZero())
		CanvasSlot->SetAutoSize(true);
	else
		CanvasSlot->SetSize(Size);
	return CanvasSlot;
}

UWidget* UPGFlowScreenWidget::WrapSize(UWidget* Content, float Width, float Height)
{
	USizeBox* Box = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	if (Width > 0.0f)
		Box->SetWidthOverride(Width);
	if (Height > 0.0f)
		Box->SetHeightOverride(Height);
	Box->SetContent(Content);
	return Box;
}

UTextBlock* UPGFlowScreenWidget::AddLine(UPanelWidget* Parent, const FText& Text, int32 Size, const FName& Typeface, const FLinearColor& Color,
	const FMargin& InPadding)
{
	UTextBlock* Block = MakeText(Text, Size, Typeface, Color);
	Block->SetAutoWrapText(true);
	if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Block)))
		LineSlot->SetPadding(InPadding);
	return Block;
}

UTextBlock* UPGFlowScreenWidget::AddSectionHeader(UPanelWidget* Parent, const FText& Text)
{
	return AddLine(Parent, Text, 20, TEXT("Bold"), PGFlowWidgetsLocal::FlowAccent, FMargin(0.0f, 0.0f, 0.0f, 10.0f));
}

void UPGFlowScreenWidget::AddStatRow(UPanelWidget* Parent, const FText& Label, const FText& Value, const FLinearColor& ValueColor)
{
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	// 이름은 제 폭만, 값은 남은 폭을 채우고 오른쪽 정렬 + 줄바꿈.
	// [9/23] 반대로(이름이 채우기) 두었더니 값이 긴 줄("생존율 100% (탈출 6 · 사망 0 · 시간 초과 0)")에서 값이 판 폭을 넘어 이름 위에 겹쳤다.
	if (UHorizontalBoxSlot* LabelSlot = Row->AddChildToHorizontalBox(MakeText(Label, 17, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim)))
	{
		LabelSlot->SetVerticalAlignment(VAlign_Center);
		LabelSlot->SetPadding(FMargin(0.0f, 0.0f, 16.0f, 0.0f));
	}
	UTextBlock* ValueText = MakeText(Value, 18, TEXT("SemiBold"), ValueColor);
	ValueText->SetAutoWrapText(true);
	ValueText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* ValueSlot = Row->AddChildToHorizontalBox(ValueText))
	{
		// 칸은 꽉 채우고(Fill) 글자만 오른쪽으로 붙인다. 칸 정렬을 Right 로 두면 글자가 받는 폭이 제 글자 폭뿐이라
		// "2분 25초" 같은 짧은 값까지 두 줄로 쪼개졌다(9/23 WBP 캡처).
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetHorizontalAlignment(HAlign_Fill);
		ValueSlot->SetVerticalAlignment(VAlign_Center);
	}
	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(Row)))
		RowSlot->SetPadding(FMargin(0.0f, 5.0f));
}

void UPGFlowScreenWidget::AddSpacer(UPanelWidget* Parent, float Height)
{
	USpacer* Spacer = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	Spacer->SetSize(FVector2D(1.0f, Height));
	Parent->AddChild(Spacer);
}

void UPGFlowScreenWidget::AddDivider(UPanelWidget* Parent)
{
	UBorder* Line = MakePanel(PGFlowWidgetsLocal::FlowDivider, FMargin(0.0f), 0.0f);
	if (UVerticalBoxSlot* LineSlot = Cast<UVerticalBoxSlot>(Parent->AddChild(WrapSize(Line, 0.0f, 1.0f))))
		LineSlot->SetPadding(FMargin(0.0f, 12.0f));
}

void UPGFlowScreenWidget::AddItemRow(UPanelWidget* Parent, const FPGItemStack& Stack, int32 Index)
{
	UBorder* RowBack = MakePanel(Index % 2 == 0 ? PGFlowWidgetsLocal::FlowRowTint : FLinearColor::Transparent, FMargin(12.0f, 8.0f), 3.0f);
	UHorizontalBox* Row = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UHorizontalBoxSlot* NameSlot = Row->AddChildToHorizontalBox(MakeText(FText::FromName(Stack.ItemId), 17, TEXT("Regular"), PGFlowWidgetsLocal::FlowText)))
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Row->AddChildToHorizontalBox(MakeText(FText::Format(NSLOCTEXT("PGFlow", "ItemCount", "x{0}"), FText::AsNumber(Stack.Count)), 17, TEXT("SemiBold"), PGFlowWidgetsLocal::FlowAccent));
	RowBack->SetContent(Row);
	Parent->AddChild(RowBack);
}

FText UPGFlowScreenWidget::ResultText(EPGRunResult Result)
{
	switch (Result)
	{
	case EPGRunResult::Extracted: return NSLOCTEXT("PGFlow", "ResExtracted", "탈출 성공");
	case EPGRunResult::Died: return NSLOCTEXT("PGFlow", "ResDied", "사망");
	case EPGRunResult::TimedOut: return NSLOCTEXT("PGFlow", "ResTimedOut", "시간 초과");
	case EPGRunResult::InProgress: return NSLOCTEXT("PGFlow", "ResInProgress", "진행 중");
	case EPGRunResult::Aborted: return NSLOCTEXT("PGFlow", "ResAborted", "중단");
	default: return NSLOCTEXT("PGFlow", "ResNone", "기록 없음");
	}
}

FText UPGFlowScreenWidget::ResultBandText(EPGRunResult Result)
{
	switch (Result)
	{
	case EPGRunResult::Extracted: return NSLOCTEXT("PGFlow", "BandSurvived", "생존");
	case EPGRunResult::Died: return NSLOCTEXT("PGFlow", "BandDied", "사망");
	case EPGRunResult::TimedOut: return NSLOCTEXT("PGFlow", "BandTimedOut", "시간 초과");
	case EPGRunResult::Aborted: return NSLOCTEXT("PGFlow", "BandAborted", "중단");
	default: return NSLOCTEXT("PGFlow", "BandNone", "기록 없음");
	}
}

FLinearColor UPGFlowScreenWidget::ResultColor(EPGRunResult Result)
{
	switch (Result)
	{
	case EPGRunResult::Extracted: return PGFlowWidgetsLocal::FlowGood;
	case EPGRunResult::Died: return PGFlowWidgetsLocal::FlowBad;
	case EPGRunResult::TimedOut: return PGFlowWidgetsLocal::FlowWarn;
	default: return PGFlowWidgetsLocal::FlowNeutral;
	}
}

FText UPGFlowScreenWidget::FormatDuration(float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::RoundToInt(Seconds));
	return FText::FromString(FString::Printf(TEXT("%d분 %02d초"), Whole / 60, Whole % 60));
}

FText UPGFlowScreenWidget::FormatDistance(float Centimeters)
{
	const int32 Meters = FMath::RoundToInt(Centimeters / 100.0f);
	if (Meters >= 1000)
		return FText::FromString(FString::Printf(TEXT("%.1fkm"), Meters / 1000.0f));
	return FText::FromString(FString::Printf(TEXT("%dm"), Meters));
}

// ---- 타이틀 ----
//
// 로그인 연결 방식(팀 코드는 한 줄도 안 고친다 — 부르고 듣기만 한다):
//   1) 서버 확인: 우리 쪽 짧은 소켓(ServerProbe)으로 ws://127.0.0.1:8080 에 붙어 본다. 붙으면 곧바로 끊는다.
//      팀 UWebSocketSubSystem 은 "지금 연결돼 있나" 를 밖에 알려 주는 함수가 없어서(소켓이 private) 따로 확인한다.
//   2) 붙으면: 팀 소켓도 붙게 ConnectToLobbyServer() 를 부르고(이미 붙어 있으면 그 함수가 바로 돌아온다),
//      팀 ULobbyUIFlowController::BeginSetting() 을 부른다 — 팀 로비 게임모드(AGameMode_InLobby)가 부르는 것과 같은 입구라
//      WBP_LoginWindow·WBP_LoginPopUp 이 우리 배경 위에 뜬다. 우리는 UAuthSubSystem::OnLoginStatusChanged 를 같이 듣다가
//      로그인 + 데이터 로드가 성공하면(팀 흐름과 같은 조건) 팀 창을 닫고 우리 로비(L_Lobby)로 간다.
//   3) 안 붙으면(또는 3초 안에 답이 없으면): "서버 연결 안 됨 — 오프라인으로 시작" 버튼. 로컬 창고(SaveGame)로 그대로 로비에 간다.
//   로그인 창은 게임을 켠 뒤 처음 타이틀에서만 띄운다(UPGRunSubsystem::HasShownTitleLogin). 두 번째부터는 "시작" 버튼이다.

void UPGTitleScreenWidget::BuildContent()
{
	// 배경 영상이 있으면 맨 아래 한 장. 없으면 뒤의 3D 무대가 그대로 보인다.
	TitleVideo = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TitleVideo"));
	TitleVideo->SetVisibility(ESlateVisibility::Collapsed);
	if (UCanvasPanelSlot* VideoSlot = PlaceOnCanvas(TitleVideo, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
	{
		VideoSlot->SetAutoSize(false);
		VideoSlot->SetOffsets(FMargin(0.0f));
	}

	// 아래쪽을 점점 어둡게: 띠 여러 장을 겹쳐 흉내 낸 그라데이션(이미지 에셋 없이). 로고가 불빛 앞에서도 읽힌다.
	for (int32 Band = 0; Band < 6; ++Band)
	{
		UImage* Shade = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		Shade->SetBrush(PGFlowWidgetsLocal::FlowFlatBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.12f), 0.0f));
		if (UCanvasPanelSlot* ShadeSlot = PlaceOnCanvas(Shade, FAnchors(0.0f, 1.0f, 1.0f, 1.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
		{
			// 가로로 늘인 앵커에서 Offsets 는 (왼쪽 여백, 앵커 기준 Y, 오른쪽 여백, 높이). 화면 아래에서 Height 만큼 위로 올린 띠.
			//   띠마다 높이가 달라 겹친 만큼 아래가 더 어둡다(맨 아래는 6장, 맨 위는 1장).
			const float Height = 120.0f + Band * 70.0f;
			ShadeSlot->SetAutoSize(false);
			ShadeSlot->SetOffsets(FMargin(0.0f, -Height, 0.0f, Height));
		}
	}

	// 가운데 아래: 로고 → 한 줄 소개 → 상태 → 버튼 줄.
	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("TitleStack"));
	PlaceOnCanvas(Stack, FAnchors(0.5f, 1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.0f, -56.0f));

	UHorizontalBox* Logo = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	UTextBlock* LogoLeft = MakeText(NSLOCTEXT("PGFlow", "LogoProject", "PROJECT"), 92, TEXT("Bold"), PGFlowWidgetsLocal::FlowText);
	UTextBlock* LogoRight = MakeText(NSLOCTEXT("PGFlow", "LogoPG", " PG"), 92, TEXT("Bold"), PGFlowWidgetsLocal::FlowAccent);
	for (UTextBlock* Part : { LogoLeft, LogoRight })
	{
		FSlateFontInfo Font = Part->GetFont();
		Font.LetterSpacing = 120; // 1/1000 em. 로고는 글자 사이를 조금 벌려야 제목처럼 보인다
		Part->SetFont(Font);
		Part->SetShadowOffset(FVector2D(0.0f, 3.0f));
		Part->SetShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f));
		Logo->AddChildToHorizontalBox(Part);
	}
	// 로고 밑 소개 문구는 뺐다(사용자 9/23: "별로야"). 문구가 차지하던 아래 여백만 로고에 남겨 버튼 위치는 그대로 둔다.
	if (UVerticalBoxSlot* LogoSlot = Stack->AddChildToVerticalBox(Logo))
	{
		LogoSlot->SetHorizontalAlignment(HAlign_Center);
		LogoSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 28.0f));
	}

	StatusText = MakeText(FText::GetEmpty(), 18, TEXT("SemiBold"), PGFlowWidgetsLocal::FlowTextDim);
	if (UVerticalBoxSlot* StatusSlot = Stack->AddChildToVerticalBox(StatusText))
	{
		StatusSlot->SetHorizontalAlignment(HAlign_Center);
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 16.0f));
	}

	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TitleButtons"));
	if (UVerticalBoxSlot* ButtonsSlot = Stack->AddChildToVerticalBox(Buttons))
		ButtonsSlot->SetHorizontalAlignment(HAlign_Center);

	StartButton = MakeButton(NSLOCTEXT("PGFlow", "BtnStart", "시작"), EPGFlowButtonStyle::Primary, 24, FMargin(56.0f, 14.0f));
	if (UHorizontalBoxSlot* StartSlot = Buttons->AddChildToHorizontalBox(StartButton))
		StartSlot->SetPadding(FMargin(8.0f, 0.0f));

	UTextBlock* OfflineLabelRaw = nullptr; // TObjectPtr 멤버는 UTextBlock** 로 못 넘긴다 — 날 포인터로 받아 옮긴다
	OfflineButton = MakeButton(NSLOCTEXT("PGFlow", "BtnOffline", "서버 연결 안 됨 — 오프라인으로 시작"), EPGFlowButtonStyle::Primary, 20,
		FMargin(32.0f, 14.0f), &OfflineLabelRaw);
	OfflineLabel = OfflineLabelRaw;
	if (UHorizontalBoxSlot* OfflineSlot = Buttons->AddChildToHorizontalBox(OfflineButton))
		OfflineSlot->SetPadding(FMargin(8.0f, 0.0f));

	QuitButton = MakeButton(NSLOCTEXT("PGFlow", "BtnQuit", "종료"), EPGFlowButtonStyle::Secondary, 20, FMargin(36.0f, 14.0f));
	if (UHorizontalBoxSlot* QuitSlot = Buttons->AddChildToHorizontalBox(QuitButton))
		QuitSlot->SetPadding(FMargin(8.0f, 0.0f));

	// 오른쪽 위 작은 판 번호.
	PlaceOnCanvas(MakeText(NSLOCTEXT("PGFlow", "TitleVersion", "PROTOTYPE v0.4"), 14, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextFaint),
		FAnchors(1.0f, 0.0f), FVector2D(1.0f, 0.0f), FVector2D(-32.0f, 24.0f));

	// 버튼 연결·영상·첫 상태(버튼 숨김 포함)는 NativeConstruct 가 한다 — WBP 로 뜰 때도 같은 길을 타게.
}

void UPGTitleScreenWidget::StartTitleVideo(UImage* Target)
{
	FString VideoPath;
	if (!PGFlowStage::FindTitleVideo(VideoPath))
		return;
	// 영상 → 미디어 플레이어 → 미디어 텍스처 → 이미지 브러시. 소리는 안 낸다(배경 영상이고, 소리는 MediaSoundComponent 가 따로 필요하다).
	VideoPlayer = NewObject<UMediaPlayer>(this);
	VideoPlayer->PlayOnOpen = true;
	VideoPlayer->SetLooping(true);
	VideoTexture = NewObject<UMediaTexture>(this);
	VideoTexture->AutoClear = true;
	VideoTexture->SetMediaPlayer(VideoPlayer);
	VideoTexture->UpdateResource();
	VideoSource = NewObject<UFileMediaSource>(this);
	VideoSource->SetFilePath(VideoPath);

	FSlateBrush Brush;
	Brush.SetResourceObject(VideoTexture);
	Brush.ImageSize = FVector2D(1920.0f, 1080.0f);
	Target->SetBrush(Brush);
	Target->SetVisibility(ESlateVisibility::HitTestInvisible);
	const bool bOpened = VideoPlayer->OpenSource(VideoSource);
	UE_LOG(LogTemp, Display, TEXT("PGFlow: title video %s open=%s"), *VideoPath, bOpened ? TEXT("true") : TEXT("false"));
}

void UPGTitleScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 버튼 연결은 여기 한 곳(코드 화면·WBP 공용). 빠진 버튼은 건너뛴다.
	if (StartButton) StartButton->OnClicked.AddUniqueDynamic(this, &UPGTitleScreenWidget::HandleStart);
	if (OfflineButton) OfflineButton->OnClicked.AddUniqueDynamic(this, &UPGTitleScreenWidget::HandleOffline);
	if (QuitButton) QuitButton->OnClicked.AddUniqueDynamic(this, &UPGTitleScreenWidget::HandleQuit);
	if (!OfflineLabel && OfflineButton)
		OfflineLabel = Cast<UTextBlock>(OfflineButton->GetContent());
	if (TitleVideo && !VideoPlayer)
		StartTitleVideo(TitleVideo);
	UPGRunSubsystem* Run = GetRun();
	const UWebSocketSubSystem* Socket = UWebSocketSubSystem::Get(this);
	const bool bAlreadyLoggedIn = Socket && !Socket->GetCurrentUserID().IsEmpty();
	if ((Run && Run->HasShownTitleLogin()) || bAlreadyLoggedIn)
	{
		// 이미 한 번 거쳤다(로비에서 "타이틀로" 로 돌아옴). 로그인 창을 또 띄우지 않는다 — 헤더의 HasShownTitleLogin 설명 참고.
		SetState(EPGTitleState::Ready, bAlreadyLoggedIn
			? FText::Format(NSLOCTEXT("PGFlow", "TitleLoggedIn", "{0} 로 로그인됨"), FText::FromString(Socket->GetCurrentUserID()))
			: FText::GetEmpty());
		return;
	}
	BeginServerCheck();
}

void UPGTitleScreenWidget::NativeDestruct()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ProbeTimeout);
		World->GetTimerManager().ClearTimer(LeaveTimer);
	}
	if (ServerProbe.IsValid())
	{
		// 콜백 람다는 약한 포인터로 위젯을 잡고 있어 위젯이 먼저 사라져도 안전하다. 소켓만 닫는다.
		if (ServerProbe->IsConnected())
			ServerProbe->Close();
		ServerProbe.Reset();
	}
	if (UGameInstance* GameInstance = GetGameInstance())
		if (UAuthSubSystem* Auth = GameInstance->GetSubsystem<UAuthSubSystem>())
			Auth->OnLoginStatusChanged.RemoveDynamic(this, &UPGTitleScreenWidget::HandleLoginStatus);
	if (VideoPlayer)
		VideoPlayer->Close();
	Super::NativeDestruct();
}

void UPGTitleScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	// "서버에 연결하는 중..." 의 점을 0.4초마다 하나씩(배그 타이틀의 CONNECTING... 처럼). 기다리는 중이라는 게 보여야 멈춘 걸로 안 본다.
	if (State != EPGTitleState::Checking || !StatusText)
		return;
	StatusClock += InDeltaTime;
	const int32 Dots = static_cast<int32>(StatusClock / 0.4f) % 4;
	StatusText->SetText(FText::Format(NSLOCTEXT("PGFlow", "StatusDots", "{0}{1}"), StatusBase, FText::FromString(FString::ChrN(Dots, TEXT('.')))));
}

void UPGTitleScreenWidget::SetState(EPGTitleState NewState, const FText& Status)
{
	State = NewState;
	StatusBase = Status;
	StatusClock = 0.0f;
	if (StatusText)
	{
		StatusText->SetText(Status);
		StatusText->SetColorAndOpacity(FSlateColor(NewState == EPGTitleState::Offline ? PGFlowWidgetsLocal::FlowBadText : PGFlowWidgetsLocal::FlowTextDim));
	}
	if (!StartButton || !OfflineButton)
		return;
	StartButton->SetVisibility(NewState == EPGTitleState::Ready ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	// 오프라인 버튼: 서버가 없으면 큰 노란 버튼, 로그인 창이 떠 있으면 작은 비상구(팀 창이 고장 나도 게임은 이어지게).
	const bool bShowOffline = NewState == EPGTitleState::Offline || NewState == EPGTitleState::LoginOpen;
	OfflineButton->SetVisibility(bShowOffline ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	if (OfflineLabel && NewState == EPGTitleState::LoginOpen)
		OfflineLabel->SetText(NSLOCTEXT("PGFlow", "BtnOfflineSmall", "오프라인으로 시작"));
	if (NewState == EPGTitleState::LoginOpen)
	{
		// 노란 주 버튼은 팀 로그인 창의 "로그인" 이어야 한다. 우리 쪽은 흐린 작은 버튼으로 낮춘다.
		OfflineButton->SetBackgroundColor(FLinearColor(0.35f, 0.35f, 0.35f, 0.6f));
		if (OfflineLabel)
			OfflineLabel->SetColorAndOpacity(FSlateColor(PGFlowWidgetsLocal::FlowText));
	}
}

void UPGTitleScreenWidget::BeginServerCheck()
{
	SetState(EPGTitleState::Checking, NSLOCTEXT("PGFlow", "TitleConnecting", "서버에 연결하는 중"));
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("WebSockets")))
		FModuleManager::Get().LoadModule(TEXT("WebSockets"));
	ServerProbe = FWebSocketsModule::Get().CreateWebSocket(PGFlowWidgetsLocal::FlowLobbyServerUrl, TEXT("ws"));
	if (!ServerProbe.IsValid())
	{
		FinishServerCheck(false);
		return;
	}
	// 소켓 콜백이 게임 스레드가 아닐 수도 있다고 보고, 결과 처리는 게임 스레드로 넘긴다. 위젯이 먼저 사라졌으면 약한 포인터가 막는다.
	TWeakObjectPtr<UPGTitleScreenWidget> WeakThis(this);
	ServerProbe->OnConnected().AddLambda([WeakThis]()
	{
		AsyncTask(ENamedThreads::GameThread, [WeakThis]() { if (UPGTitleScreenWidget* Self = WeakThis.Get()) Self->FinishServerCheck(true); });
	});
	ServerProbe->OnConnectionError().AddLambda([WeakThis](const FString& Error)
	{
		UE_LOG(LogTemp, Display, TEXT("PGFlow: lobby server probe failed (%s)"), *Error);
		AsyncTask(ENamedThreads::GameThread, [WeakThis]() { if (UPGTitleScreenWidget* Self = WeakThis.Get()) Self->FinishServerCheck(false); });
	});
	ServerProbe->Connect();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(ProbeTimeout, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			if (State == EPGTitleState::Checking)
			{
				UE_LOG(LogTemp, Display, TEXT("PGFlow: lobby server probe timed out after %.0fs"), PGFlowWidgetsLocal::FlowProbeTimeoutSeconds);
				FinishServerCheck(false);
			}
		}), PGFlowWidgetsLocal::FlowProbeTimeoutSeconds, false);
	}
}

void UPGTitleScreenWidget::FinishServerCheck(bool bOnline)
{
	if (State != EPGTitleState::Checking)
		return; // 연결·오류·시간 초과 중 먼저 온 하나만
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(ProbeTimeout);
	if (ServerProbe.IsValid())
	{
		// 확인만 하는 연결이라 바로 끊는다. 서버에는 아무 메시지도 보내지 않았다.
		if (ServerProbe->IsConnected())
			ServerProbe->Close();
		ServerProbe.Reset();
	}
	UE_LOG(LogTemp, Display, TEXT("PGFlow: lobby server %s"), bOnline ? TEXT("online — opening team login") : TEXT("offline"));
	if (bOnline)
		OpenTeamLogin();
	else
		SetState(EPGTitleState::Offline, NSLOCTEXT("PGFlow", "TitleOffline", "서버에 연결되지 않았습니다 · 로컬 창고로 플레이합니다"));
}

void UPGTitleScreenWidget::OpenTeamLogin()
{
	UGameInstance* GameInstance = GetGameInstance();
	ULobbyUIFlowController* Flow = ULobbyUIFlowController::Get(this);
	UUIManagerSubSystem* UiManager = UUIManagerSubSystem::Get(this);
	UAuthSubSystem* Auth = GameInstance ? GameInstance->GetSubsystem<UAuthSubSystem>() : nullptr;
	if (!Flow || !UiManager || !Auth)
	{
		SetState(EPGTitleState::Offline, NSLOCTEXT("PGFlow", "TitleNoTeamUi", "로그인 모듈을 찾지 못했습니다 · 오프라인으로 시작할 수 있습니다"));
		return;
	}
	if (UPGRunSubsystem* Run = GetRun())
		Run->MarkTitleLoginShown();
	// 팀 소켓이 게임 시작 때 서버가 꺼져 있어 실패했을 수 있다. 다시 붙게 한다(이미 붙어 있으면 그 함수가 그냥 돌아온다).
	if (UWebSocketSubSystem* Socket = UWebSocketSubSystem::Get(this))
		Socket->ConnectToLobbyServer();
	// 먼저 듣고 나서 연다: 이미 아이디가 있으면 BeginSetting 이 곧바로 로그인 요청을 보내 결과가 금방 올 수 있다.
	Auth->OnLoginStatusChanged.AddUniqueDynamic(this, &UPGTitleScreenWidget::HandleLoginStatus);
	Flow->BeginSetting();
	bTeamLoginOpened = true;

	// 팀 위젯 클래스는 BP_GameInstance 가 UIManager 에 등록한다. 등록이 안 돼 있으면 OpenUI 가 아무것도 안 띄운다 → 오프라인 길로.
	if (!UiManager->GetUI(EUIType::LoginWindow) && !UiManager->GetUI(EUIType::Login))
	{
		UE_LOG(LogTemp, Warning, TEXT("PGFlow: team login widgets did not open (UI classes not registered?) — offline start offered"));
		SetState(EPGTitleState::Offline, NSLOCTEXT("PGFlow", "TitleLoginMissing", "로그인 창을 열지 못했습니다 · 오프라인으로 시작할 수 있습니다"));
		return;
	}
	SetState(EPGTitleState::LoginOpen, NSLOCTEXT("PGFlow", "TitleLoginPrompt", "서버 연결됨 · 로그인해 주세요"));
}

void UPGTitleScreenWidget::CloseTeamLogin()
{
	if (!bTeamLoginOpened)
		return;
	bTeamLoginOpened = false;
	if (UUIManagerSubSystem* UiManager = UUIManagerSubSystem::Get(this))
		UiManager->CloseAllUI();
	// 팀 UIManager 는 창이 다 닫히면 입력을 "게임 전용 + 커서 숨김" 으로 바꾼다. 흐름 화면은 마우스가 있어야 하니 되돌린다.
	if (APlayerController* PC = GetOwningPlayer())
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
}

void UPGTitleScreenWidget::HandleLoginStatus(bool bIsLoggedIn, bool bInventoryLoaded, const FString& Message)
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: team login status loggedIn=%s data=%s (%s)"),
		bIsLoggedIn ? TEXT("true") : TEXT("false"), bInventoryLoaded ? TEXT("true") : TEXT("false"), *Message);
	// 팀 ULobbyUIFlowController::SucceedLogin 과 같은 조건(로그인 + 데이터)일 때만 성공으로 본다. 실패 안내는 팀 쪽이 메시지 창으로 띄운다.
	if (!(bIsLoggedIn && bInventoryLoaded) || State == EPGTitleState::Leaving)
		return;
	SetState(EPGTitleState::Leaving, NSLOCTEXT("PGFlow", "TitleLoginOk", "로그인 성공 · 로비로 이동합니다"));
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(LeaveTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { LeaveToLobby(); }), PGFlowWidgetsLocal::FlowLeaveDelaySeconds, false);
}

void UPGTitleScreenWidget::LeaveToLobby()
{
	CloseTeamLogin();
	if (UPGRunSubsystem* Run = GetRun())
	{
		Run->MarkTitleLoginShown();
		Run->GoToLobby();
	}
}

void UPGTitleScreenWidget::HandleStart()
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: title -> lobby (start button)"));
	LeaveToLobby();
}

void UPGTitleScreenWidget::HandleOffline()
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: title -> lobby (offline button)"));
	LeaveToLobby();
}

void UPGTitleScreenWidget::HandleQuit()
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: quit (button)"));
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}

// ---- 로비 ----

UButton* UPGLobbyScreenWidget::AddTab(UPanelWidget* Parent, const FText& Label, int32 Index)
{
	// 탭 = 글자 + 밑줄. 고른 탭만 노란 글자·노란 밑줄(배그 로비 위 메뉴와 같은 읽는 법).
	UButton* Tab = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	FButtonStyle Style;
	Style.SetNormal(PGFlowWidgetsLocal::FlowFlatBrush(FLinearColor::Transparent, 0.0f));
	Style.SetHovered(PGFlowWidgetsLocal::FlowFlatBrush(PGFlowWidgetsLocal::FlowSrgb(0xFF, 0xFF, 0xFF, 0.05f), 0.0f));
	Style.SetPressed(PGFlowWidgetsLocal::FlowFlatBrush(PGFlowWidgetsLocal::FlowSrgb(0xFF, 0xFF, 0xFF, 0.08f), 0.0f));
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f));
	Tab->SetStyle(Style);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UTextBlock* Text = MakeText(Label, 22, TEXT("Bold"), PGFlowWidgetsLocal::FlowTextDim);
	if (UVerticalBoxSlot* TextSlot = Content->AddChildToVerticalBox(Text))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetPadding(FMargin(22.0f, 18.0f, 22.0f, 14.0f));
	}
	UBorder* Underline = MakePanel(FLinearColor::Transparent, FMargin(0.0f), 0.0f);
	if (UVerticalBoxSlot* LineSlot = Content->AddChildToVerticalBox(WrapSize(Underline, 0.0f, 3.0f)))
		LineSlot->SetHorizontalAlignment(HAlign_Fill);
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Tab->AddChild(Content)))
	{
		ContentSlot->SetPadding(FMargin(0.0f));
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}
	TabLabels.SetNum(TabCount);
	TabUnderlines.SetNum(TabCount);
	TabLabels[Index] = Text;
	TabUnderlines[Index] = Underline;
	if (UHorizontalBoxSlot* TabSlot = Cast<UHorizontalBoxSlot>(Parent->AddChild(Tab)))
		TabSlot->SetVerticalAlignment(VAlign_Fill);
	return Tab;
}

void UPGLobbyScreenWidget::BuildContent()
{
	// ---- 위 메뉴 줄: 로고 | 출격 창고 기록 | ... 상태 · 타이틀로 ----
	UBorder* TopBar = MakePanel(PGFlowWidgetsLocal::FlowBar, FMargin(36.0f, 0.0f), 0.0f);
	if (UCanvasPanelSlot* BarSlot = PlaceOnCanvas(TopBar, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
	{
		BarSlot->SetAutoSize(false);
		BarSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 72.0f)); // 좌·위·우 0, 높이 72
	}
	UHorizontalBox* Bar = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TopBar"));
	TopBar->SetContent(Bar);

	UHorizontalBox* Logo = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Logo->AddChildToHorizontalBox(MakeText(NSLOCTEXT("PGFlow", "BarLogoProject", "PROJECT"), 24, TEXT("Bold"), PGFlowWidgetsLocal::FlowText));
	Logo->AddChildToHorizontalBox(MakeText(NSLOCTEXT("PGFlow", "BarLogoPG", " PG"), 24, TEXT("Bold"), PGFlowWidgetsLocal::FlowAccent));
	if (UHorizontalBoxSlot* LogoSlot = Bar->AddChildToHorizontalBox(Logo))
	{
		LogoSlot->SetVerticalAlignment(VAlign_Center);
		LogoSlot->SetPadding(FMargin(0.0f, 0.0f, 48.0f, 0.0f));
	}
	TabDeployButton = AddTab(Bar, NSLOCTEXT("PGFlow", "TabDeploy", "출격"), 0);
	TabStashButton = AddTab(Bar, NSLOCTEXT("PGFlow", "TabStash", "창고"), 1);
	TabRecordButton = AddTab(Bar, NSLOCTEXT("PGFlow", "TabRecord", "기록"), 2);

	USpacer* Fill = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	if (UHorizontalBoxSlot* FillSlot = Bar->AddChildToHorizontalBox(Fill))
		FillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	TopBarStatus = MakeText(FText::GetEmpty(), 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
	if (UHorizontalBoxSlot* StatusSlot = Bar->AddChildToHorizontalBox(TopBarStatus))
	{
		StatusSlot->SetVerticalAlignment(VAlign_Center);
		StatusSlot->SetPadding(FMargin(0.0f, 0.0f, 24.0f, 0.0f));
	}
	// 환경설정: "타이틀로" 바로 왼쪽. 9/22 사용자 "로비에 환경설정이 있으면 좋겠다".
	SettingsButton = MakeButton(NSLOCTEXT("PGFlow", "BtnSettings", "환경설정"), EPGFlowButtonStyle::Secondary, 16, FMargin(18.0f, 8.0f));
	if (UHorizontalBoxSlot* SettingsSlot = Bar->AddChildToHorizontalBox(SettingsButton))
	{
		SettingsSlot->SetVerticalAlignment(VAlign_Center);
		SettingsSlot->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	}
	TitleButton = MakeButton(NSLOCTEXT("PGFlow", "BtnTitle", "타이틀로"), EPGFlowButtonStyle::Secondary, 16, FMargin(18.0f, 8.0f));
	if (UHorizontalBoxSlot* TitleSlot = Bar->AddChildToHorizontalBox(TitleButton))
		TitleSlot->SetVerticalAlignment(VAlign_Center);

	// ---- 오른쪽 판: 탭마다 한 쪽 ----
	UBorder* SidePanel = MakePanel(PGFlowWidgetsLocal::FlowPanel, FMargin(28.0f, 26.0f));
	PlaceOnCanvas(SidePanel, FAnchors(1.0f, 0.5f), FVector2D(1.0f, 0.5f), FVector2D(-40.0f, 36.0f), FVector2D(460.0f, 760.0f));
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("Pages"));
	SidePanel->SetContent(Pages);

	DeployPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DeployPage"));
	Pages->AddChild(DeployPage);

	UVerticalBox* StashPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StashPage"));
	AddSectionHeader(StashPage, NSLOCTEXT("PGFlow", "StashTitle", "창고"));
	StashSummary = AddLine(StashPage, FText::GetEmpty(), 15, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim, FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	StashList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("StashList"));
	if (UVerticalBoxSlot* ListSlot = StashPage->AddChildToVerticalBox(StashList))
		ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Pages->AddChild(StashPage);

	UVerticalBox* RecordPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("RecordPage"));
	AddSectionHeader(RecordPage, NSLOCTEXT("PGFlow", "RecordTitle", "최근 레이드"));
	RecordList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("RecordList"));
	if (UVerticalBoxSlot* ListSlot = RecordPage->AddChildToVerticalBox(RecordList))
		ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	Pages->AddChild(RecordPage);

	// ---- 왼쪽 아래: 큰 노란 출격 버튼 ----
	UVerticalBox* DeployStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("DeployStack"));
	PlaceOnCanvas(DeployStack, FAnchors(0.0f, 1.0f), FVector2D(0.0f, 1.0f), FVector2D(56.0f, -56.0f));
	AddLine(DeployStack, NSLOCTEXT("PGFlow", "DeployHint", "절차 생성 맵 · 판마다 새 지형"), 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim, FMargin(4.0f, 0.0f, 0.0f, 10.0f));
	DeployButton = MakeButton(NSLOCTEXT("PGFlow", "BtnDeploy", "출격"), EPGFlowButtonStyle::Primary, 46, FMargin(0.0f));
	DeployStack->AddChildToVerticalBox(WrapSize(DeployButton, 380.0f, 104.0f));
	// 버튼 연결·첫 탭·목록 채우기는 NativeConstruct 가 한다 — WBP 로 뜰 때도 같은 길을 타게.
}

void UPGLobbyScreenWidget::SelectTab(int32 Index)
{
	ActiveTab = FMath::Clamp(Index, 0, TabCount - 1);
	if (Pages)
		Pages->SetActiveWidgetIndex(ActiveTab);
	for (int32 Tab = 0; Tab < TabLabels.Num(); ++Tab)
	{
		const bool bActive = Tab == ActiveTab;
		if (TabLabels[Tab])
			TabLabels[Tab]->SetColorAndOpacity(FSlateColor(bActive ? PGFlowWidgetsLocal::FlowAccent : PGFlowWidgetsLocal::FlowTextDim));
		if (TabUnderlines.IsValidIndex(Tab) && TabUnderlines[Tab])
			TabUnderlines[Tab]->SetBrush(PGFlowWidgetsLocal::FlowFlatBrush(bActive ? PGFlowWidgetsLocal::FlowAccent : FLinearColor::Transparent, 0.0f));
	}
	OnTabChanged(ActiveTab);
}

void UPGLobbyScreenWidget::HandleTabDeploy() { SelectTab(0); }
void UPGLobbyScreenWidget::HandleTabStash() { SelectTab(1); }
void UPGLobbyScreenWidget::HandleTabRecord() { SelectTab(2); }

void UPGLobbyScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 버튼 연결은 여기 한 곳. 코드로 짠 화면이든 WBP 든 같은 이름의 버튼이 이미 자리에 들어와 있다.
	// 빠진 버튼은 건너뛴다(WBP 를 만드는 중이라 아직 안 놓았을 수 있다).
	if (DeployButton) DeployButton->OnClicked.AddUniqueDynamic(this, &UPGLobbyScreenWidget::HandleDeploy);
	if (SettingsButton) SettingsButton->OnClicked.AddUniqueDynamic(this, &UPGLobbyScreenWidget::HandleOpenSettings);
	if (TitleButton) TitleButton->OnClicked.AddUniqueDynamic(this, &UPGLobbyScreenWidget::HandleBackToTitle);
	if (TabDeployButton) TabDeployButton->OnClicked.AddUniqueDynamic(this, &UPGLobbyScreenWidget::HandleTabDeploy);
	if (TabStashButton) TabStashButton->OnClicked.AddUniqueDynamic(this, &UPGLobbyScreenWidget::HandleTabStash);
	if (TabRecordButton) TabRecordButton->OnClicked.AddUniqueDynamic(this, &UPGLobbyScreenWidget::HandleTabRecord);
	// WBP 로 떴으면 탭 버튼 안에서 첫 글자(TextBlock)와 첫 밑줄(Border)을 찾아 쓴다. 코드 화면은 AddTab 이 이미 채웠다.
	// 이름을 따로 정하지 않고 "버튼 안에 있는 것" 으로 찾는 이유: 탭 세 개마다 밑줄 이름을 세 개씩 맞추게 하면 WBP 를 만질 때 틀리기 쉽다.
	if (TabLabels.Num() == 0)
	{
		const UButton* TabButtons[TabCount] = { TabDeployButton, TabStashButton, TabRecordButton };
		for (const UButton* Tab : TabButtons)
		{
			UTextBlock* Label = nullptr;
			UBorder* Underline = nullptr;
			TArray<UWidget*> Pending;
			if (Tab && Tab->GetContent())
				Pending.Add(Tab->GetContent());
			while (Pending.Num() > 0)
			{
				UWidget* Widget = Pending.Pop(EAllowShrinking::No);
				if (!Label)
					Label = Cast<UTextBlock>(Widget);
				if (!Underline)
					Underline = Cast<UBorder>(Widget);
				if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
					for (int32 Child = Panel->GetChildrenCount() - 1; Child >= 0; --Child)
						Pending.Add(Panel->GetChildAt(Child));
			}
			TabLabels.Add(Label);
			TabUnderlines.Add(Underline);
		}
	}
	SelectTab(ActiveTab);
	RefreshLists();
	// 창고가 바뀌면(콘솔 PG.Flow.StashAdd 등) 목록을 다시 그린다.
	if (UPGRunSubsystem* Run = GetRun())
		Run->OnStashChanged.AddUniqueDynamic(this, &UPGLobbyScreenWidget::RefreshLists);
}

void UPGLobbyScreenWidget::NativeDestruct()
{
	if (SettingsWindow)
	{
		SettingsWindow->OnClosed.RemoveAll(this);
		SettingsWindow->RemoveFromParent();
		SettingsWindow = nullptr;
	}
	if (UPGRunSubsystem* Run = GetRun())
		Run->OnStashChanged.RemoveDynamic(this, &UPGLobbyScreenWidget::RefreshLists);
	Super::NativeDestruct();
}

void UPGLobbyScreenWidget::RefreshLists()
{
	UPGRunSubsystem* Run = GetRun();
	if (!Run || !DeployPage || !StashList || !RecordList)
		return;
	const TArray<FPGItemStack>& Stash = Run->GetStashItems();
	const FPGPlayerStats& Stats = Run->GetStats();
	const FPGRunRecord& Last = Run->GetLastRun();

	int32 StashTotal = 0;
	for (const FPGItemStack& Stack : Stash)
		StashTotal += Stack.Count;
	if (TopBarStatus)
		TopBarStatus->SetText(FText::Format(NSLOCTEXT("PGFlow", "BarStatus", "창고 {0}종 · 출격 {1}회 · 탈출 {2}회"),
			FText::AsNumber(Stash.Num()), FText::AsNumber(Stats.TotalRuns), FText::AsNumber(Stats.Extractions)));

	// ---- 출격 쪽: 지난 레이드 + 누적 기록 ----
	DeployPage->ClearChildren();
	AddSectionHeader(DeployPage, NSLOCTEXT("PGFlow", "LastRunTitle", "지난 레이드"));
	if (!Last.IsFinished())
	{
		AddLine(DeployPage, NSLOCTEXT("PGFlow", "NoRunYet", "아직 기록이 없습니다. 왼쪽 아래 출격을 눌러 첫 판을 시작하세요."), 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
	}
	else
	{
		// 결과 띠: 결과 화면과 같은 색(초록 생존 · 빨강 사망 · 주황 시간 초과).
		UBorder* Band = MakePanel(ResultColor(Last.Result), FMargin(14.0f, 8.0f), 4.0f);
		Band->SetContent(MakeText(ResultBandText(Last.Result), 20, TEXT("Bold"), PGFlowWidgetsLocal::FlowText));
		if (UVerticalBoxSlot* BandSlot = DeployPage->AddChildToVerticalBox(Band))
			BandSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 10.0f));
		if (!Last.HowItEnded.IsEmpty())
			AddLine(DeployPage, Last.HowItEnded, 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim, FMargin(0.0f, 0.0f, 0.0f, 6.0f));
		AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowTime", "레이드 시간"), FormatDuration(Last.DurationSeconds), PGFlowWidgetsLocal::FlowText);
		AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowKills", "처치 (몬스터 / 보스)"),
			FText::Format(NSLOCTEXT("PGFlow", "RowKillsValue", "{0} / {1}"), FText::AsNumber(Last.MonsterKills), FText::AsNumber(Last.BossKills)), PGFlowWidgetsLocal::FlowText);
		AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowDragon", "드래곤"),
			Last.bDragonKilled ? NSLOCTEXT("PGFlow", "DragonDown", "격추") : NSLOCTEXT("PGFlow", "DragonNo", "-"),
			Last.bDragonKilled ? PGFlowWidgetsLocal::FlowAccent : PGFlowWidgetsLocal::FlowTextDim);
		AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowDistance", "이동 거리"), FormatDistance(Last.DistanceCm), PGFlowWidgetsLocal::FlowText);
		AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowShots", "사격"),
			FText::Format(NSLOCTEXT("PGFlow", "ShotsValue", "{0}발"), FText::AsNumber(Last.ShotsFired)), PGFlowWidgetsLocal::FlowText);
	}
	AddDivider(DeployPage);
	AddSectionHeader(DeployPage, NSLOCTEXT("PGFlow", "StatsTitle", "누적 기록"));
	const int32 Finished = Stats.Extractions + Stats.Deaths + Stats.TimeOuts;
	const int32 SurvivalPercent = Finished > 0 ? FMath::RoundToInt(100.0f * Stats.Extractions / Finished) : 0;
	AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowRuns", "출격"), FText::AsNumber(Stats.TotalRuns), PGFlowWidgetsLocal::FlowText);
	AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowSurvival", "생존율"),
		FText::Format(NSLOCTEXT("PGFlow", "SurvivalValue", "{0}%  (탈출 {1} · 사망 {2} · 시간 초과 {3})"), FText::AsNumber(SurvivalPercent),
			FText::AsNumber(Stats.Extractions), FText::AsNumber(Stats.Deaths), FText::AsNumber(Stats.TimeOuts)), PGFlowWidgetsLocal::FlowText);
	AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowTotalKills", "총 처치 (몬스터 / 보스)"),
		FText::Format(NSLOCTEXT("PGFlow", "TotalKillsValue", "{0} / {1}"), FText::AsNumber(Stats.TotalMonsterKills), FText::AsNumber(Stats.TotalBossKills)), PGFlowWidgetsLocal::FlowText);
	AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowDragons", "드래곤 격추"), FText::AsNumber(Stats.DragonKills), PGFlowWidgetsLocal::FlowText);
	AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowTotalDistance", "총 이동"), FormatDistance(Stats.TotalDistanceCm), PGFlowWidgetsLocal::FlowText);
	AddStatRow(DeployPage, NSLOCTEXT("PGFlow", "RowPlayTime", "총 플레이"), FormatDuration(Stats.TotalPlaySeconds), PGFlowWidgetsLocal::FlowText);

	// ---- 창고 쪽 ----
	StashList->ClearChildren();
	if (StashSummary)
		StashSummary->SetText(Stash.Num() == 0
			? NSLOCTEXT("PGFlow", "StashEmpty", "비어 있음 — 탈출하면 들고 나온 아이템이 여기 쌓입니다")
			: FText::Format(NSLOCTEXT("PGFlow", "StashSummary", "{0}종 · {1}개 · 탈출하면 들고 나온 아이템이 여기 쌓입니다"),
				FText::AsNumber(Stash.Num()), FText::AsNumber(StashTotal)));
	for (int32 Index = 0; Index < Stash.Num() && Index < PGFlowWidgetsLocal::FlowMaxListRows; ++Index)
		AddItemRow(StashList, Stash[Index], Index);

	// ---- 기록 쪽 ----
	RecordList->ClearChildren();
	const TArray<FPGRunRecord>& History = Run->GetRunHistory();
	if (History.Num() == 0)
		AddLine(RecordList, NSLOCTEXT("PGFlow", "RecordEmpty", "끝난 레이드가 아직 없습니다."), 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
	for (int32 Index = 0; Index < History.Num() && Index < PGFlowWidgetsLocal::FlowMaxListRows; ++Index)
	{
		const FPGRunRecord& Record = History[Index];
		UBorder* Card = MakePanel(Index % 2 == 0 ? PGFlowWidgetsLocal::FlowRowTint : FLinearColor::Transparent, FMargin(12.0f, 10.0f), 3.0f);
		UVerticalBox* CardBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const FLinearColor HeadColor = Record.Result == EPGRunResult::Extracted ? PGFlowWidgetsLocal::FlowGoodText : PGFlowWidgetsLocal::FlowBadText;
		if (UHorizontalBoxSlot* ResultSlot = Head->AddChildToHorizontalBox(MakeText(ResultText(Record.Result), 18, TEXT("Bold"), HeadColor)))
			ResultSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		Head->AddChildToHorizontalBox(MakeText(FText::AsDateTime(Record.StartedAt, EDateTimeStyle::Short, EDateTimeStyle::Short), 14, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextFaint));
		CardBox->AddChildToVerticalBox(Head);
		AddLine(CardBox, FText::Format(NSLOCTEXT("PGFlow", "RecordLine", "{0} · 처치 {1}(보스 {2}) · {3} · 사격 {4}발"),
			FormatDuration(Record.DurationSeconds), FText::AsNumber(Record.MonsterKills), FText::AsNumber(Record.BossKills),
			FormatDistance(Record.DistanceCm), FText::AsNumber(Record.ShotsFired)), 15, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
		Card->SetContent(CardBox);
		RecordList->AddChild(Card);
	}
}

void UPGLobbyScreenWidget::HandleDeploy()
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: lobby -> game (출격 button)"));
	if (UPGRunSubsystem* Run = GetRun())
		Run->StartGame();
}

void UPGLobbyScreenWidget::HandleBackToTitle()
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: lobby -> title (button)"));
	if (UPGRunSubsystem* Run = GetRun())
		Run->GoToTitle();
}

void UPGLobbyScreenWidget::HandleOpenSettings()
{
	if (SettingsWindow)
		return;
	UE_LOG(LogTemp, Display, TEXT("PGFlow: lobby -> settings (button)"));
	// 로비 화면(ZOrder 0) 위에 띄운다. 로비는 이미 마우스 커서가 보이는 입력 모드라 입력 모드는 건드리지 않는다.
	SettingsWindow = UPGSettingsWidget::Open(GetOwningPlayer(), 50);
	if (SettingsWindow)
		SettingsWindow->OnClosed.AddUObject(this, &UPGLobbyScreenWidget::HandleSettingsClosed);
}

void UPGLobbyScreenWidget::HandleSettingsClosed()
{
	SettingsWindow = nullptr;
}

// ---- 레이드 결과 ----

void UPGScoreboardScreenWidget::BuildContent()
{
	// 코드 기본 화면: 배치만 짓는다. 판마다 바뀌는 글자·색·목록은 NativeConstruct → FillFromRecord 가 채운다
	// (WBP_PGScoreboard 로 뜰 때도 같은 채우기 코드를 탄다).

	// 화면 전체를 한 번 더 어둡게. 뒤 3D 는 카메라가 이미 흐리게(피사계 심도)·채도를 빼 두었다(PGFlowStage).
	UImage* Dim = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("Dim"));
	Dim->SetBrush(PGFlowWidgetsLocal::FlowFlatBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 0.0f));
	if (UCanvasPanelSlot* DimSlot = PlaceOnCanvas(Dim, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
	{
		DimSlot->SetAutoSize(false);
		DimSlot->SetOffsets(FMargin(0.0f));
	}

	// ---- 위: 큰 제목 + 탈출구 이름 ----
	UVerticalBox* Head = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Head"));
	PlaceOnCanvas(Head, FAnchors(0.5f, 0.0f), FVector2D(0.5f, 0.0f), FVector2D(0.0f, 52.0f));
	UTextBlock* Title = MakeText(NSLOCTEXT("PGFlow", "RaidOver", "레이드 종료"), 64, TEXT("Bold"), PGFlowWidgetsLocal::FlowText);
	Title->SetShadowOffset(FVector2D(0.0f, 3.0f));
	if (UVerticalBoxSlot* TitleSlot = Head->AddChildToVerticalBox(Title))
		TitleSlot->SetHorizontalAlignment(HAlign_Center);
	SubtitleText = MakeText(FText::GetEmpty(), 22, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
	if (UVerticalBoxSlot* SubSlot = Head->AddChildToVerticalBox(SubtitleText))
	{
		SubSlot->SetHorizontalAlignment(HAlign_Center);
		SubSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
	}

	// ---- 결과 띠: 화면 가로 전체. 캐릭터 머리 위 높이(카메라가 캐릭터를 아래 2/3 에 두었다) ----
	Band = MakePanel(FLinearColor::White, FMargin(0.0f), 0.0f); // 흰 판 × 결과 색(BrushColor) = 결과 색
	Band->SetHorizontalAlignment(HAlign_Center);
	Band->SetVerticalAlignment(VAlign_Center);
	BandText = MakeText(FText::GetEmpty(), 34, TEXT("Bold"), PGFlowWidgetsLocal::FlowText);
	FSlateFontInfo BandFont = BandText->GetFont();
	BandFont.LetterSpacing = 300;
	BandText->SetFont(BandFont);
	Band->SetContent(BandText);
	if (UCanvasPanelSlot* BandSlot = PlaceOnCanvas(Band, FAnchors(0.0f, 0.0f, 1.0f, 0.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
	{
		BandSlot->SetAutoSize(false);
		BandSlot->SetOffsets(FMargin(0.0f, 212.0f, 0.0f, 62.0f)); // 위에서 212, 높이 62
	}

	// ---- 왼쪽 판: 레이드 기록 ----
	UBorder* StatsPanel = MakePanel(PGFlowWidgetsLocal::FlowPanel, FMargin(28.0f, 24.0f));
	PlaceOnCanvas(StatsPanel, FAnchors(0.0f, 0.5f), FVector2D(0.0f, 0.5f), FVector2D(64.0f, 120.0f), FVector2D(440.0f, 560.0f));
	StatsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("StatsBox"));
	StatsPanel->SetContent(StatsBox);

	// ---- 오른쪽 판: 들고 나온 / 잃은 아이템 ----
	UBorder* ItemsPanel = MakePanel(PGFlowWidgetsLocal::FlowPanel, FMargin(28.0f, 24.0f));
	PlaceOnCanvas(ItemsPanel, FAnchors(1.0f, 0.5f), FVector2D(1.0f, 0.5f), FVector2D(-64.0f, 120.0f), FVector2D(440.0f, 560.0f));
	UVerticalBox* ItemsBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ItemsBox"));
	ItemsPanel->SetContent(ItemsBox);
	ItemsHeader = AddSectionHeader(ItemsBox, FText::GetEmpty());
	ItemsNote = AddLine(ItemsBox, FText::GetEmpty(), 15, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim, FMargin(0.0f, 0.0f, 0.0f, 12.0f));
	ItemList = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("ItemList"));
	if (UVerticalBoxSlot* ListSlot = ItemsBox->AddChildToVerticalBox(ItemList))
		ListSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// ---- 아래 가운데: 다음(로비로) 하나 ----
	// 9/22 사용자: "스코어보드에서 타이틀로 가는 버튼은 필요 없다 — 로비에 '타이틀로' 가 있으니 로비로만 가면 된다."
	//   그래서 "메인 메뉴" 버튼을 뺐다. 버튼이 하나뿐이라 고민할 것 없이 누르면 되고, 노란 주 버튼 규칙(화면마다 하나)과도 맞는다.
	UHorizontalBox* Buttons = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("Buttons"));
	PlaceOnCanvas(Buttons, FAnchors(0.5f, 1.0f), FVector2D(0.5f, 1.0f), FVector2D(0.0f, -48.0f));
	NextButton = MakeButton(NSLOCTEXT("PGFlow", "BtnNext", "다음"), EPGFlowButtonStyle::Primary, 24, FMargin(0.0f));
	Buttons->AddChildToHorizontalBox(WrapSize(NextButton, 300.0f, 64.0f));
}

void UPGScoreboardScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (NextButton)
		NextButton->OnClicked.AddUniqueDynamic(this, &UPGScoreboardScreenWidget::HandleNext);
	FillFromRecord();
}

void UPGScoreboardScreenWidget::FillFromRecord()
{
	UPGRunSubsystem* Run = GetRun();
	const FPGRunRecord Record = Run ? Run->GetLastRun() : FPGRunRecord();
	const bool bFinished = Record.IsFinished();

	if (SubtitleText)
	{
		FText Subtitle = Record.HowItEnded;
		if (!bFinished)
			Subtitle = NSLOCTEXT("PGFlow", "NoRecord", "끝난 판이 없습니다. 로비에서 출격하세요.");
		else if (Subtitle.IsEmpty())
			Subtitle = ResultText(Record.Result);
		SubtitleText->SetText(Subtitle);
	}
	// 결과 띠: 색은 결과마다 다르다(초록 생존 · 빨강 사망 · 주황 시간 초과). WBP 에서 정한 띠 모양 위에 색만 바꾼다.
	if (Band)
		Band->SetBrushColor(ResultColor(Record.Result));
	if (BandText)
		BandText->SetText(ResultBandText(Record.Result));

	if (StatsBox)
	{
		StatsBox->ClearChildren();
		AddSectionHeader(StatsBox, NSLOCTEXT("PGFlow", "RaidStats", "레이드 기록"));
		if (bFinished)
		{
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbTime", "레이드 시간"), FormatDuration(Record.DurationSeconds), PGFlowWidgetsLocal::FlowText);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbMonsters", "몬스터 처치"), FText::AsNumber(Record.MonsterKills), PGFlowWidgetsLocal::FlowText);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbBosses", "보스 처치"), FText::AsNumber(Record.BossKills), Record.BossKills > 0 ? PGFlowWidgetsLocal::FlowAccent : PGFlowWidgetsLocal::FlowText);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbDragon", "드래곤"),
				Record.bDragonKilled ? NSLOCTEXT("PGFlow", "SbDragonYes", "격추!") : NSLOCTEXT("PGFlow", "SbDragonNo", "-"),
				Record.bDragonKilled ? PGFlowWidgetsLocal::FlowAccent : PGFlowWidgetsLocal::FlowTextDim);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbMove", "이동 거리"), FormatDistance(Record.DistanceCm), PGFlowWidgetsLocal::FlowText);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbShots", "사격"),
				FText::Format(NSLOCTEXT("PGFlow", "SbShotsValue", "{0}발"), FText::AsNumber(Record.ShotsFired)), PGFlowWidgetsLocal::FlowText);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbPickups", "주운 횟수"), FText::AsNumber(Record.ItemsPickedUp), PGFlowWidgetsLocal::FlowText);
			AddDivider(StatsBox);
			AddStatRow(StatsBox, NSLOCTEXT("PGFlow", "SbSeed", "맵 시드"), FText::AsNumber(Record.MapSeed), PGFlowWidgetsLocal::FlowTextFaint);
		}
		else
		{
			AddLine(StatsBox, NSLOCTEXT("PGFlow", "SbNone", "보여 드릴 기록이 없습니다."), 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
		}
	}

	const bool bKept = Record.Result == EPGRunResult::Extracted;
	int32 ItemTotal = 0;
	for (const FPGItemStack& Stack : Record.ItemsAtEnd)
		ItemTotal += Stack.Count;
	if (ItemsHeader)
		ItemsHeader->SetText(bKept ? NSLOCTEXT("PGFlow", "SbItemsKept", "들고 나온 아이템") : NSLOCTEXT("PGFlow", "SbItemsLost", "잃은 아이템"));
	if (ItemsNote)
	{
		ItemsNote->SetText(bKept
			? FText::Format(NSLOCTEXT("PGFlow", "SbKeptNote", "{0}종 {1}개 · 창고에 넣었습니다"), FText::AsNumber(Record.ItemsAtEnd.Num()), FText::AsNumber(ItemTotal))
			: FText::Format(NSLOCTEXT("PGFlow", "SbLostNote", "{0}종 {1}개 · 판 안에 두고 왔습니다"), FText::AsNumber(Record.ItemsAtEnd.Num()), FText::AsNumber(ItemTotal)));
		ItemsNote->SetColorAndOpacity(FSlateColor(bKept ? PGFlowWidgetsLocal::FlowGoodText : PGFlowWidgetsLocal::FlowBadText));
	}
	if (ItemList)
	{
		ItemList->ClearChildren();
		if (Record.ItemsAtEnd.Num() == 0)
			AddLine(ItemList, NSLOCTEXT("PGFlow", "SbNoItems", "없음"), 16, TEXT("Regular"), PGFlowWidgetsLocal::FlowTextDim);
		for (int32 Index = 0; Index < Record.ItemsAtEnd.Num() && Index < PGFlowWidgetsLocal::FlowMaxListRows; ++Index)
			AddItemRow(ItemList, Record.ItemsAtEnd[Index], Index);
	}
}

void UPGScoreboardScreenWidget::HandleNext()
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: scoreboard -> lobby (다음 button)"));
	if (UPGRunSubsystem* Run = GetRun())
		Run->GoToLobby();
}
