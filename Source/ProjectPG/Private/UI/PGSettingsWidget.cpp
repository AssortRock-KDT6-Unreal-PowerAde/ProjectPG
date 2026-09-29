#include "UI/PGSettingsWidget.h"

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
#include "Components/Slider.h"
#include "Components/Spacer.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Flow/PGFlowSettings.h"
#include "UI/PGFlowStyle.h"

// 파일 고유 이름 공간(유니티 빌드에서 다른 cpp 이름과 안 겹치게).
namespace PGSettingsWidgetLocal
{
	// 1920x1080 기준 크기. 화면이 작아지면 DPI 배율로 같이 줄어든다(다른 흐름 화면과 같은 규칙).
	const FVector2D SettingsPanelSize(1180.0f, 820.0f);
	constexpr float SettingsControlWidth = 440.0f;

	// FPS 제한 선택지. 0 은 제한 없음. 144 는 요즘 흔한 게이밍 모니터 주사율이라 넣었다.
	const int32 SettingsFpsChoices[] = { 30, 60, 120, 144, 0 };

	// 팀 캐릭터 입력 자산. 키 목록을 "읽기만" 한다(고치지 않는다).
	const TCHAR* const SettingsPlayerInputContext = TEXT("/Game/PG/Input/IMC_Player.IMC_Player");

	// 입력 동작 이름 → 화면에 보일 우리말. 목록에 없는 동작은 동작의 설명(ActionDescription)이나 이름을 그대로 쓴다.
	FText SettingsActionName(const UInputAction* Action)
	{
		if (!Action)
			return FText::GetEmpty();
		static const TMap<FString, FText> Names = {
			{ TEXT("IA_Move"), NSLOCTEXT("PGSettings", "ActMove", "이동") },
			{ TEXT("IA_Look_Mouse"), NSLOCTEXT("PGSettings", "ActLook", "둘러보기") },
			{ TEXT("IA_Ability_Jump"), NSLOCTEXT("PGSettings", "ActJump", "점프") },
			{ TEXT("IA_Ability_Sprint"), NSLOCTEXT("PGSettings", "ActSprint", "달리기") },
			{ TEXT("IA_Crouch"), NSLOCTEXT("PGSettings", "ActCrouch", "앉기") },
			{ TEXT("IA_Interaction"), NSLOCTEXT("PGSettings", "ActInteract", "상호작용") },
			{ TEXT("IA_Weapon_Fire"), NSLOCTEXT("PGSettings", "ActFire", "사격") },
			{ TEXT("IA_Weapon_Reload"), NSLOCTEXT("PGSettings", "ActReload", "재장전") },
			{ TEXT("IA_Weapon_Scope"), NSLOCTEXT("PGSettings", "ActScope", "조준") },
		};
		if (const FText* Found = Names.Find(Action->GetName()))
			return *Found;
		if (!Action->ActionDescription.IsEmpty())
			return Action->ActionDescription;
		return FText::FromString(Action->GetName());
	}

	FText SettingsQualityName(int32 Level)
	{
		switch (Level)
		{
		case 0: return NSLOCTEXT("PGSettings", "QLow", "낮음");
		case 1: return NSLOCTEXT("PGSettings", "QMid", "중간");
		case 2: return NSLOCTEXT("PGSettings", "QHigh", "높음");
		case 3: return NSLOCTEXT("PGSettings", "QEpic", "최고");
		default: return NSLOCTEXT("PGSettings", "QCustom", "사용자 지정");
		}
	}

	TArray<FText> SettingsQualityOptions()
	{
		return { SettingsQualityName(0), SettingsQualityName(1), SettingsQualityName(2), SettingsQualityName(3) };
	}

	TArray<FText> SettingsFpsOptions()
	{
		TArray<FText> Options;
		for (const int32 Fps : SettingsFpsChoices)
			Options.Add(Fps > 0 ? FText::Format(NSLOCTEXT("PGSettings", "FpsValue", "{0} FPS"), FText::AsNumber(Fps)) : NSLOCTEXT("PGSettings", "FpsNone", "제한 없음"));
		return Options;
	}

	int32 SettingsFpsIndex(int32 Fps)
	{
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(SettingsFpsChoices)); ++Index)
			if (SettingsFpsChoices[Index] == Fps)
				return Index;
		return 1; // 목록에 없는 값(ini 를 손으로 고친 경우)은 60 으로 보여 준다
	}

	FText SettingsPercent(float Value) { return FText::Format(NSLOCTEXT("PGSettings", "Percent", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value * 100.0f))); }

	FText SettingsDecimal(float Value)
	{
		FNumberFormattingOptions Format;
		Format.MinimumFractionalDigits = 2;
		Format.MaximumFractionalDigits = 2;
		return FText::AsNumber(Value, &Format);
	}
}

// ---- 중계 객체 ----

void UPGSettingsRowRelay::HandleClicked()
{
	if (UPGSettingsWidget* Widget = Owner.Get())
		Widget->StepRow(RowIndex, Direction);
}

void UPGSettingsRowRelay::HandleSliderChanged(float Value)
{
	if (UPGSettingsWidget* Widget = Owner.Get())
		Widget->SlideRow(RowIndex, Value);
}

// ---- 띄우기·닫기 ----

UPGSettingsWidget* UPGSettingsWidget::Open(APlayerController* Owner, int32 ZOrder)
{
	if (!Owner)
		return nullptr;
	// 설정(ProjectPG Flow > Screens > Settings Window Class)에 WBP 가 있으면 그것을 띄운다(9/23 블루프린트 분리).
	UPGSettingsWidget* Widget = CreateWidget<UPGSettingsWidget>(Owner, UPGFlowSettings::ResolveWidgetClass(UPGFlowSettings::Get().SettingsWindowClass, UPGSettingsWidget::StaticClass()));
	if (!Widget)
		return nullptr;
	// 초점을 받을 수 있어야 Esc 를 직접 받는다. 이 값은 Slate 위젯이 만들어지기 전(화면에 붙이기 전)에만 바꿀 수 있다.
	Widget->SetIsFocusable(true);
	Widget->AddToViewport(ZOrder);
	Widget->SetKeyboardFocus();
	UE_LOG(LogTemp, Display, TEXT("PGSettings: settings screen opened (%s)"), *Widget->GetClass()->GetName());
	return Widget;
}

void UPGSettingsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetKeyboardFocus();
}

FReply UPGSettingsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	// Esc = 뒤로 가기(적용 안 한 값은 버린다). F10 도 같다 — 에디터 PIE 에서는 Esc 가 PIE 를 끝내 버려서 F10 으로 연다(일시 정지 메뉴 설명 참고).
	if (InKeyEvent.GetKey() == EKeys::Escape || InKeyEvent.GetKey() == EKeys::F10)
	{
		HandleBack();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPGSettingsWidget::CloseSelf()
{
	if (bClosing)
		return;
	bClosing = true;
	RemoveFromParent();
	OnClosed.Broadcast();
}

// ---- 화면 짜기 ----

void UPGSettingsWidget::BuildContent()
{
	Saved = UPGGameSettings::ReadCurrent();
	Pending = Saved;
	ResolutionChoices = UPGGameSettings::GetResolutionChoices();
	if (!ResolutionChoices.Contains(Pending.Resolution))
		ResolutionChoices.Add(Pending.Resolution);

	// 창 틀: WBP(WBP_PGSettings)가 SettingsBody 를 주면 그 안에 채우고, 없으면 여기서 판을 짓는다.
	UVerticalBox* Body = SettingsBody;
	if (!Body)
	{
		// 뒤 화면(로비·게임)을 어둡게 덮는다. 이 판이 마우스를 다 받아서 뒤 버튼이 눌리지 않는다.
		UBorder* Dim = MakePanel(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f), FMargin(0.0f), 0.0f);
		if (UCanvasPanelSlot* DimSlot = PlaceOnCanvas(Dim, FAnchors(0.0f, 0.0f, 1.0f, 1.0f), FVector2D::ZeroVector, FVector2D::ZeroVector))
		{
			DimSlot->SetAutoSize(false);
			DimSlot->SetOffsets(FMargin(0.0f));
		}

		UBorder* MenuPanel = MakePanel(PGFlowStyle::PanelSolid(), FMargin(36.0f, 26.0f, 36.0f, 24.0f), 8.0f);
		PlaceOnCanvas(MenuPanel, FAnchors(0.5f, 0.5f), FVector2D(0.5f, 0.5f), FVector2D::ZeroVector, PGSettingsWidgetLocal::SettingsPanelSize);
		Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("SettingsBody"));
		MenuPanel->SetContent(Body);
	}
	Body->ClearChildren(); // WBP 가 준 상자에 미리 보기용 위젯이 들어 있어도 지우고 우리가 채운다

	// ---- 머리: 제목 + 탭 ----
	UHorizontalBox* Head = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Body->AddChildToVerticalBox(Head);
	if (UHorizontalBoxSlot* TitleSlot = Head->AddChildToHorizontalBox(MakeText(NSLOCTEXT("PGSettings", "Title", "환경설정"), 30, TEXT("Bold"), PGFlowStyle::Text())))
	{
		TitleSlot->SetVerticalAlignment(VAlign_Center);
		TitleSlot->SetPadding(FMargin(0.0f, 0.0f, 40.0f, 0.0f));
	}
	AddTopTab(Head, NSLOCTEXT("PGSettings", "TabGraphics", "그래픽"), TabLabels, TabLines, 22)->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleTabGraphics);
	AddTopTab(Head, NSLOCTEXT("PGSettings", "TabAudio", "오디오"), TabLabels, TabLines, 22)->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleTabAudio);
	AddTopTab(Head, NSLOCTEXT("PGSettings", "TabControls", "컨트롤"), TabLabels, TabLines, 22)->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleTabControls);
	AddTopTab(Head, NSLOCTEXT("PGSettings", "TabGameplay", "게임플레이"), TabLabels, TabLines, 22)->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleTabGameplay);
	USpacer* HeadFill = WidgetTree->ConstructWidget<USpacer>(USpacer::StaticClass());
	if (UHorizontalBoxSlot* FillSlot = Head->AddChildToHorizontalBox(HeadFill))
		FillSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
	if (UHorizontalBoxSlot* HintSlot = Head->AddChildToHorizontalBox(MakeText(NSLOCTEXT("PGSettings", "EscHint", "Esc  뒤로 가기"), 14, TEXT("Regular"), PGFlowStyle::TextFaint())))
		HintSlot->SetVerticalAlignment(VAlign_Center);
	AddDivider(Body);

	// ---- 가운데: 탭마다 한 쪽 ----
	Pages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("SettingsPages"));
	if (UVerticalBoxSlot* PagesSlot = Body->AddChildToVerticalBox(Pages))
		PagesSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	const FText EditorOnlyNote = GIsEditor
		? NSLOCTEXT("PGSettings", "NoteEditorDisplay", "에디터(PIE)에서는 적용되지 않습니다 · 패키징한 게임에서 적용됩니다")
		: FText::GetEmpty();
	const FText StoredForCharacter = NSLOCTEXT("PGSettings", "NoteStoredCharacter", "저장만 됩니다 · 캐릭터 조작에 연결할 예정입니다");

	// 그래픽: 안에 기본·고급 두 쪽.
	UVerticalBox* GraphicsPage = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GraphicsPage"));
	Pages->AddChild(GraphicsPage);
	UHorizontalBox* SubTabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	if (UVerticalBoxSlot* SubSlot = GraphicsPage->AddChildToVerticalBox(SubTabs))
		SubSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
	AddTopTab(SubTabs, NSLOCTEXT("PGSettings", "SubBasic", "기본"), SubTabLabels, SubTabLines, 17)->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleGraphicsBasic);
	AddTopTab(SubTabs, NSLOCTEXT("PGSettings", "SubAdvanced", "고급"), SubTabLabels, SubTabLines, 17)->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleGraphicsAdvanced);
	GraphicsPages = WidgetTree->ConstructWidget<UWidgetSwitcher>(UWidgetSwitcher::StaticClass(), TEXT("GraphicsPages"));
	if (UVerticalBoxSlot* GraphicsSlot = GraphicsPage->AddChildToVerticalBox(GraphicsPages))
		GraphicsSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));

	// -- 그래픽 기본 --
	UVerticalBox* Basic = MakePage(GraphicsPages);
	AddChoiceRow(Basic, NSLOCTEXT("PGSettings", "RowWindowMode", "디스플레이 모드"), EditorOnlyNote,
		{ NSLOCTEXT("PGSettings", "WmFullscreen", "전체 화면"), NSLOCTEXT("PGSettings", "WmBorderless", "창 전체 화면"), NSLOCTEXT("PGSettings", "WmWindowed", "창 모드") },
		[this]() { return Pending.WindowMode; }, [this](int32 Index) { Pending.WindowMode = Index; });
	TArray<FText> ResolutionNames;
	for (const FIntPoint& Size : ResolutionChoices)
		ResolutionNames.Add(FText::FromString(FString::Printf(TEXT("%d x %d"), Size.X, Size.Y)));
	AddChoiceRow(Basic, NSLOCTEXT("PGSettings", "RowResolution", "해상도"), EditorOnlyNote, ResolutionNames,
		[this]() { return FMath::Max(0, ResolutionChoices.IndexOfByKey(Pending.Resolution)); },
		[this](int32 Index) { if (ResolutionChoices.IsValidIndex(Index)) Pending.Resolution = ResolutionChoices[Index]; });
	AddSliderRow(Basic, NSLOCTEXT("PGSettings", "RowScale", "렌더링 해상도"),
		NSLOCTEXT("PGSettings", "NoteScale", "낮추면 화면 크기는 그대로 두고 3D 만 낮은 해상도로 그려 빨라집니다"),
		50.0f, 100.0f, 1.0f, &Pending.ResolutionScale,
		[](float Value) { return FText::Format(NSLOCTEXT("PGSettings", "ScaleValue", "{0}%"), FText::AsNumber(FMath::RoundToInt(Value))); });
	AddToggleRow(Basic, NSLOCTEXT("PGSettings", "RowVSync", "수직 동기화"),
		NSLOCTEXT("PGSettings", "NoteVSync", "화면 찢어짐을 막습니다 · 켜면 입력이 조금 늦어질 수 있습니다"), &Pending.bVSync);
	AddChoiceRow(Basic, NSLOCTEXT("PGSettings", "RowLobbyFps", "로비 FPS 제한"), NSLOCTEXT("PGSettings", "NoteLobbyFps", "타이틀·로비·결과 화면"),
		PGSettingsWidgetLocal::SettingsFpsOptions(),
		[this]() { return PGSettingsWidgetLocal::SettingsFpsIndex(Pending.LobbyFpsLimit); },
		[this](int32 Index) { Pending.LobbyFpsLimit = PGSettingsWidgetLocal::SettingsFpsChoices[Index]; });
	AddChoiceRow(Basic, NSLOCTEXT("PGSettings", "RowGameFps", "인게임 FPS 제한"), NSLOCTEXT("PGSettings", "NoteGameFps", "레이드 맵"),
		PGSettingsWidgetLocal::SettingsFpsOptions(),
		[this]() { return PGSettingsWidgetLocal::SettingsFpsIndex(Pending.GameFpsLimit); },
		[this](int32 Index) { Pending.GameFpsLimit = PGSettingsWidgetLocal::SettingsFpsChoices[Index]; });
	AddSliderRow(Basic, NSLOCTEXT("PGSettings", "RowBrightness", "밝기"), NSLOCTEXT("PGSettings", "NoteBrightness", "3D 화면에만 적용됩니다 · 메뉴 글자는 그대로입니다"),
		0.5f, 1.5f, 0.01f, &Pending.Brightness,
		[](float Value) { return FText::AsNumber(FMath::RoundToInt(Value * 100.0f)); });
	AddBrightnessPreview(Basic);

	// -- 그래픽 고급 --
	UVerticalBox* Advanced = MakePage(GraphicsPages);
	TArray<FText> OverallOptions = PGSettingsWidgetLocal::SettingsQualityOptions();
	OverallOptions.Add(PGSettingsWidgetLocal::SettingsQualityName(-1));
	// 전체 품질: 앞 네 칸만 고를 수 있고, 다섯째 "사용자 지정" 은 개별 칸이 섞였을 때 보여 주기만 한다.
	AddChoiceRow(Advanced, NSLOCTEXT("PGSettings", "RowOverall", "전체 품질"), NSLOCTEXT("PGSettings", "NoteOverall", "고르면 아래 항목이 모두 같은 단계로 바뀝니다"),
		OverallOptions,
		[this]() { const int32 Level = Pending.GetOverallQuality(); return Level < 0 ? 4 : Level; },
		[this](int32 Index) { Pending.SetOverallQuality(Index); }, 4);
	struct FQualityRowInfo { FPGSettingsValues::EQualityGroup Group; FText Label; FText Note; };
	const FQualityRowInfo QualityRows[] = {
		{ FPGSettingsValues::AntiAliasing, NSLOCTEXT("PGSettings", "RowAA", "안티에일리어싱"), NSLOCTEXT("PGSettings", "NoteAA", "물체 가장자리의 계단 무늬를 다듬습니다") },
		{ FPGSettingsValues::Shadow, NSLOCTEXT("PGSettings", "RowShadow", "그림자"), FText::GetEmpty() },
		{ FPGSettingsValues::Texture, NSLOCTEXT("PGSettings", "RowTexture", "텍스처"), NSLOCTEXT("PGSettings", "NoteTexture", "표면 그림의 선명도 · 그래픽 메모리를 많이 씁니다") },
		{ FPGSettingsValues::Effects, NSLOCTEXT("PGSettings", "RowEffects", "효과"), NSLOCTEXT("PGSettings", "NoteEffects", "불·연기·폭발") },
		{ FPGSettingsValues::PostProcess, NSLOCTEXT("PGSettings", "RowPost", "후처리"), NSLOCTEXT("PGSettings", "NotePost", "빛 번짐·색 보정 같은 화면 전체 효과") },
		{ FPGSettingsValues::Foliage, NSLOCTEXT("PGSettings", "RowFoliage", "수풀"), NSLOCTEXT("PGSettings", "NoteFoliage", "풀·덤불 양") },
		{ FPGSettingsValues::ViewDistance, NSLOCTEXT("PGSettings", "RowViewDistance", "시야 거리"), NSLOCTEXT("PGSettings", "NoteViewDistance", "먼 물체를 얼마나 멀리까지 자세히 그릴지 정합니다") },
		{ FPGSettingsValues::Shading, NSLOCTEXT("PGSettings", "RowShading", "셰이딩"), NSLOCTEXT("PGSettings", "NoteShading", "재질의 빛 반사 계산") },
	};
	for (const FQualityRowInfo& Info : QualityRows)
	{
		const int32 Group = Info.Group;
		AddChoiceRow(Advanced, Info.Label, Info.Note, PGSettingsWidgetLocal::SettingsQualityOptions(),
			[this, Group]() { return Pending.Quality[Group]; }, [this, Group](int32 Index) { Pending.Quality[Group] = Index; });
	}
	AddToggleRow(Advanced, NSLOCTEXT("PGSettings", "RowMotionBlur", "모션 블러"), NSLOCTEXT("PGSettings", "NoteMotionBlur", "빠르게 돌 때 화면이 흐려지는 효과"), &Pending.bMotionBlur);

	// -- 오디오 --
	UVerticalBox* Audio = MakePage(Pages);
	AddSliderRow(Audio, NSLOCTEXT("PGSettings", "RowMaster", "전체 볼륨"), FText::GetEmpty(), 0.0f, 1.0f, 0.01f, &Pending.MasterVolume,
		[](float Value) { return PGSettingsWidgetLocal::SettingsPercent(Value); });
	// 효과음·음악·UI: 소리를 종류별로 줄이려면 사운드 분류(SoundClass) 에셋과 소리마다의 연결이 필요한데 프로젝트에 아직 없다.
	//   그래서 값만 저장한다. 분류가 생기면 PGGameSettings.cpp 의 ApplyRuntimeSettings 에서 사운드 믹스로 걸면 된다.
	const FText StoredAudio = NSLOCTEXT("PGSettings", "NoteStoredAudio", "저장만 됩니다 · 소리 분류가 생기면 연결됩니다");
	AddSliderRow(Audio, NSLOCTEXT("PGSettings", "RowEffectsVol", "효과음"), StoredAudio, 0.0f, 1.0f, 0.01f, &Pending.EffectsVolume,
		[](float Value) { return PGSettingsWidgetLocal::SettingsPercent(Value); });
	AddSliderRow(Audio, NSLOCTEXT("PGSettings", "RowMusicVol", "음악"), StoredAudio, 0.0f, 1.0f, 0.01f, &Pending.MusicVolume,
		[](float Value) { return PGSettingsWidgetLocal::SettingsPercent(Value); });
	AddSliderRow(Audio, NSLOCTEXT("PGSettings", "RowUiVol", "UI"), StoredAudio, 0.0f, 1.0f, 0.01f, &Pending.UiVolume,
		[](float Value) { return PGSettingsWidgetLocal::SettingsPercent(Value); });
	AddToggleRow(Audio, NSLOCTEXT("PGSettings", "RowUnfocused", "창이 뒤에 있을 때 소리 끄기"), NSLOCTEXT("PGSettings", "NoteUnfocused", "다른 프로그램을 보고 있을 때"), &Pending.bMuteWhenUnfocused);

	// -- 컨트롤 --
	UVerticalBox* Controls = MakePage(Pages);
	AddSliderRow(Controls, NSLOCTEXT("PGSettings", "RowMouse", "마우스 감도"), StoredForCharacter, 0.2f, 3.0f, 0.05f, &Pending.MouseSensitivity,
		[](float Value) { return PGSettingsWidgetLocal::SettingsDecimal(Value); });
	AddSliderRow(Controls, NSLOCTEXT("PGSettings", "RowAim", "조준 감도"), StoredForCharacter, 0.2f, 3.0f, 0.05f, &Pending.AimSensitivity,
		[](float Value) { return PGSettingsWidgetLocal::SettingsDecimal(Value); });
	AddToggleRow(Controls, NSLOCTEXT("PGSettings", "RowInvert", "마우스 상하 반전"), StoredForCharacter, &Pending.bInvertMouseY);
	AddKeyList(Controls);

	// -- 게임플레이 --
	UVerticalBox* Gameplay = MakePage(Pages);
	AddSliderRow(Gameplay, NSLOCTEXT("PGSettings", "RowFov", "시야각 (FOV)"), NSLOCTEXT("PGSettings", "NoteFov", "저장만 됩니다 · 카메라에 연결할 예정입니다"),
		70.0f, 110.0f, 1.0f, &Pending.FieldOfView,
		[](float Value) { return FText::Format(NSLOCTEXT("PGSettings", "FovValue", "{0}°"), FText::AsNumber(FMath::RoundToInt(Value))); });
	AddToggleRow(Gameplay, NSLOCTEXT("PGSettings", "RowShowFps", "FPS 표시"), NSLOCTEXT("PGSettings", "NoteShowFps", "화면 오른쪽 위에 초당 프레임 수"), &Pending.bShowFps);

	// ---- 아래: 기본값 | 안내 | 뒤로 가기 · 적용 ----
	AddDivider(Body);
	UHorizontalBox* Foot = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SettingsFoot"));
	Body->AddChildToVerticalBox(Foot);
	UButton* DefaultsButton = MakeButton(NSLOCTEXT("PGSettings", "BtnDefaults", "기본값"), EPGFlowButtonStyle::Secondary, 18, FMargin(0.0f));
	DefaultsButton->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleDefaults);
	Foot->AddChildToHorizontalBox(WrapSize(DefaultsButton, 150.0f, 52.0f));
	StatusText = MakeText(FText::GetEmpty(), 16, TEXT("Regular"), PGFlowStyle::TextDim());
	if (UHorizontalBoxSlot* StatusSlot = Foot->AddChildToHorizontalBox(StatusText))
	{
		StatusSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		StatusSlot->SetVerticalAlignment(VAlign_Center);
		StatusSlot->SetPadding(FMargin(20.0f, 0.0f));
	}
	UButton* BackButton = MakeButton(NSLOCTEXT("PGSettings", "BtnBack", "뒤로 가기"), EPGFlowButtonStyle::Secondary, 18, FMargin(0.0f));
	BackButton->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleBack);
	if (UHorizontalBoxSlot* BackSlot = Foot->AddChildToHorizontalBox(WrapSize(BackButton, 170.0f, 52.0f)))
		BackSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
	UButton* ApplyButton = MakeButton(NSLOCTEXT("PGSettings", "BtnApply", "적용"), EPGFlowButtonStyle::Primary, 20, FMargin(0.0f));
	ApplyButton->OnClicked.AddDynamic(this, &UPGSettingsWidget::HandleApply);
	Foot->AddChildToHorizontalBox(WrapSize(ApplyButton, 190.0f, 52.0f));

	SelectTab(0);
	SelectGraphicsPage(0);
	RefreshRows();
}

UVerticalBox* UPGSettingsWidget::MakePage(UPanelWidget* Into)
{
	UScrollBox* Scroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass());
	UVerticalBox* List = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	Scroll->AddChild(List);
	Into->AddChild(Scroll);
	return List;
}

void UPGSettingsWidget::AddSectionTitle(UPanelWidget* Page, const FText& Title)
{
	AddLine(Page, Title, 18, TEXT("Bold"), PGFlowStyle::Accent(), FMargin(4.0f, 18.0f, 0.0f, 6.0f));
}

UButton* UPGSettingsWidget::AddTopTab(UPanelWidget* Parent, const FText& Label, TArray<TObjectPtr<UTextBlock>>& Labels, TArray<TObjectPtr<UBorder>>& Lines, int32 FontSize)
{
	// 로비 위 메뉴와 같은 탭 모양: 글자 + 밑줄, 고른 탭만 노랗게.
	UButton* Tab = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
	FButtonStyle Style;
	Style.SetNormal(PGFlowStyle::FlatBrush(FLinearColor::Transparent, 0.0f));
	Style.SetHovered(PGFlowStyle::FlatBrush(PGFlowStyle::Srgb(0xFF, 0xFF, 0xFF, 0.05f), 0.0f));
	Style.SetPressed(PGFlowStyle::FlatBrush(PGFlowStyle::Srgb(0xFF, 0xFF, 0xFF, 0.08f), 0.0f));
	Style.SetNormalPadding(FMargin(0.0f));
	Style.SetPressedPadding(FMargin(0.0f));
	Tab->SetStyle(Style);

	UVerticalBox* Content = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	UTextBlock* Text = MakeText(Label, FontSize, TEXT("Bold"), PGFlowStyle::TextDim());
	if (UVerticalBoxSlot* TextSlot = Content->AddChildToVerticalBox(Text))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetPadding(FMargin(18.0f, 10.0f, 18.0f, 8.0f));
	}
	UBorder* Underline = MakePanel(FLinearColor::Transparent, FMargin(0.0f), 0.0f);
	Content->AddChildToVerticalBox(WrapSize(Underline, 0.0f, 3.0f));
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Tab->AddChild(Content)))
	{
		ContentSlot->SetPadding(FMargin(0.0f));
		ContentSlot->SetHorizontalAlignment(HAlign_Fill);
		ContentSlot->SetVerticalAlignment(VAlign_Fill);
	}
	Labels.Add(Text);
	Lines.Add(Underline);
	if (UHorizontalBoxSlot* TabSlot = Cast<UHorizontalBoxSlot>(Parent->AddChild(Tab)))
		TabSlot->SetVerticalAlignment(VAlign_Center);
	return Tab;
}

UButton* UPGSettingsWidget::MakeArrowButton(const FText& Glyph, int32 RowIndex, int32 Direction)
{
	UButton* Arrow = MakeButton(Glyph, EPGFlowButtonStyle::Secondary, 18, FMargin(0.0f));
	UPGSettingsRowRelay* Relay = NewObject<UPGSettingsRowRelay>(this);
	Relay->Owner = this;
	Relay->RowIndex = RowIndex;
	Relay->Direction = Direction;
	Relays.Add(Relay); // UPROPERTY 배열이 잡고 있어야 가비지 수거에 안 지워진다(버튼의 델리게이트는 약하게만 잡는다)
	Arrow->OnClicked.AddDynamic(Relay, &UPGSettingsRowRelay::HandleClicked);
	return Arrow;
}

namespace PGSettingsWidgetLocal
{
	// 줄 왼쪽: 이름 + (있으면) 흐린 설명. 줄마다 같은 모양이라 한 곳에서 짠다.
	void SettingsFillRowLabel(UVerticalBox* LabelBox, UTextBlock* Label, UTextBlock* Note)
	{
		LabelBox->AddChildToVerticalBox(Label);
		if (Note)
			if (UVerticalBoxSlot* NoteSlot = LabelBox->AddChildToVerticalBox(Note))
				NoteSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	}
}

int32 UPGSettingsWidget::AddChoiceRow(UPanelWidget* Page, const FText& Label, const FText& Note, const TArray<FText>& Options,
	TFunction<int32()> GetIndex, TFunction<void(int32)> SetIndex, int32 SelectableCount)
{
	const int32 RowIndex = Rows.Num();
	FPGSettingsRow& Row = Rows.AddDefaulted_GetRef();
	Row.Options = Options;
	Row.SelectableCount = SelectableCount > 0 ? FMath::Min(SelectableCount, Options.Num()) : Options.Num();
	Row.GetIndex = MoveTemp(GetIndex);
	Row.SetIndex = MoveTemp(SetIndex);

	UBorder* Back = MakePanel(RowIndex % 2 == 0 ? PGFlowStyle::RowTint() : FLinearColor::Transparent, FMargin(16.0f, 9.0f), 4.0f);
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Back->SetContent(Line);

	UVerticalBox* LabelBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	PGSettingsWidgetLocal::SettingsFillRowLabel(LabelBox, MakeText(Label, 18, TEXT("SemiBold"), PGFlowStyle::Text()),
		Note.IsEmpty() ? nullptr : MakeText(Note, 13, TEXT("Regular"), PGFlowStyle::TextFaint()));
	if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(LabelBox))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	// 오른쪽: [<]  값  [>]  — 드롭다운 대신 화살표로 넘긴다. 엔진 드롭다운(ComboBox)은 기본 글꼴이 한글을 네모로 그리고, 모양도 다른 버튼과 달라진다.
	UHorizontalBox* Picker = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Picker->AddChildToHorizontalBox(WrapSize(MakeArrowButton(NSLOCTEXT("PGSettings", "ArrowLeft", "<"), RowIndex, -1), 44.0f, 38.0f));
	UTextBlock* Value = MakeText(FText::GetEmpty(), 18, TEXT("SemiBold"), PGFlowStyle::Accent());
	Value->SetJustification(ETextJustify::Center);
	if (UHorizontalBoxSlot* ValueSlot = Picker->AddChildToHorizontalBox(Value))
	{
		ValueSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ValueSlot->SetVerticalAlignment(VAlign_Center);
		ValueSlot->SetHorizontalAlignment(HAlign_Center);
	}
	Picker->AddChildToHorizontalBox(WrapSize(MakeArrowButton(NSLOCTEXT("PGSettings", "ArrowRight", ">"), RowIndex, 1), 44.0f, 38.0f));
	if (UHorizontalBoxSlot* PickerSlot = Line->AddChildToHorizontalBox(WrapSize(Picker, PGSettingsWidgetLocal::SettingsControlWidth, 0.0f)))
		PickerSlot->SetVerticalAlignment(VAlign_Center);
	Row.ValueText = Value;

	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Page->AddChild(Back)))
		RowSlot->SetPadding(FMargin(0.0f, 1.0f));
	return RowIndex;
}

int32 UPGSettingsWidget::AddToggleRow(UPanelWidget* Page, const FText& Label, const FText& Note, bool* Value)
{
	return AddChoiceRow(Page, Label, Note, { NSLOCTEXT("PGSettings", "Off", "끔"), NSLOCTEXT("PGSettings", "On", "켬") },
		[Value]() { return *Value ? 1 : 0; }, [Value](int32 Index) { *Value = Index == 1; });
}

int32 UPGSettingsWidget::AddSliderRow(UPanelWidget* Page, const FText& Label, const FText& Note, float Min, float Max, float Step, float* Value,
	TFunction<FText(float)> Format)
{
	const int32 RowIndex = Rows.Num();
	FPGSettingsRow& Row = Rows.AddDefaulted_GetRef();
	Row.bSlider = true;
	Row.Min = Min;
	Row.Max = Max;
	Row.Step = Step;
	Row.GetValue = [Value]() { return *Value; };
	Row.SetValue = [Value](float NewValue) { *Value = NewValue; };
	Row.Format = MoveTemp(Format);

	UBorder* Back = MakePanel(RowIndex % 2 == 0 ? PGFlowStyle::RowTint() : FLinearColor::Transparent, FMargin(16.0f, 9.0f), 4.0f);
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Back->SetContent(Line);

	UVerticalBox* LabelBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	PGSettingsWidgetLocal::SettingsFillRowLabel(LabelBox, MakeText(Label, 18, TEXT("SemiBold"), PGFlowStyle::Text()),
		Note.IsEmpty() ? nullptr : MakeText(Note, 13, TEXT("Regular"), PGFlowStyle::TextFaint()));
	if (UHorizontalBoxSlot* LabelSlot = Line->AddChildToHorizontalBox(LabelBox))
	{
		LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		LabelSlot->SetVerticalAlignment(VAlign_Center);
	}

	UHorizontalBox* Control = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	USlider* Slider = WidgetTree->ConstructWidget<USlider>(USlider::StaticClass());
	Slider->SetMinValue(Min);
	Slider->SetMaxValue(Max);
	Slider->SetStepSize(Step);
	Slider->SetSliderBarColor(PGFlowStyle::Srgb(0x5A, 0x5C, 0x63));
	Slider->SetSliderHandleColor(PGFlowStyle::Accent());
	UPGSettingsRowRelay* Relay = NewObject<UPGSettingsRowRelay>(this);
	Relay->Owner = this;
	Relay->RowIndex = RowIndex;
	Relays.Add(Relay);
	Slider->OnValueChanged.AddDynamic(Relay, &UPGSettingsRowRelay::HandleSliderChanged);
	if (UHorizontalBoxSlot* SliderSlot = Control->AddChildToHorizontalBox(Slider))
	{
		SliderSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		SliderSlot->SetVerticalAlignment(VAlign_Center);
		SliderSlot->SetPadding(FMargin(4.0f, 0.0f, 14.0f, 0.0f));
	}
	UTextBlock* ValueText = MakeText(FText::GetEmpty(), 18, TEXT("SemiBold"), PGFlowStyle::Accent());
	ValueText->SetJustification(ETextJustify::Right);
	if (UHorizontalBoxSlot* ValueSlot = Control->AddChildToHorizontalBox(WrapSize(ValueText, 76.0f, 0.0f)))
		ValueSlot->SetVerticalAlignment(VAlign_Center);
	if (UHorizontalBoxSlot* ControlSlot = Line->AddChildToHorizontalBox(WrapSize(Control, PGSettingsWidgetLocal::SettingsControlWidth, 38.0f)))
		ControlSlot->SetVerticalAlignment(VAlign_Center);
	Row.Slider = Slider;
	Row.ValueText = ValueText;

	if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Page->AddChild(Back)))
		RowSlot->SetPadding(FMargin(0.0f, 1.0f));
	return RowIndex;
}

void UPGSettingsWidget::AddBrightnessPreview(UPanelWidget* Page)
{
	// 배그의 밝기 미리보기 그림 대신: 검정 → 흰색 열 칸. 밝기를 움직이면 칸 색이 같이 바뀐다.
	//   주의: 메뉴(UI)에는 밝기가 안 걸리므로 이 칸들은 "적용하면 3D 가 이렇게 된다" 를 계산해서 칠한 흉내다(RefreshBrightnessPreview).
	UBorder* Back = MakePanel(FLinearColor::Transparent, FMargin(16.0f, 4.0f, 16.0f, 10.0f), 0.0f);
	UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	Back->SetContent(Line);
	if (UHorizontalBoxSlot* HintSlot = Line->AddChildToHorizontalBox(MakeText(
		NSLOCTEXT("PGSettings", "BrightnessHint", "미리보기 · 왼쪽 두 번째 칸이 겨우 보이면 알맞습니다"), 14, TEXT("Regular"), PGFlowStyle::TextDim())))
	{
		HintSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		HintSlot->SetVerticalAlignment(VAlign_Center);
	}
	UHorizontalBox* Swatches = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	constexpr int32 SwatchCount = 10;
	for (int32 Index = 0; Index < SwatchCount; ++Index)
	{
		UImage* Swatch = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		BrightnessSwatches.Add(Swatch);
		if (UHorizontalBoxSlot* SwatchSlot = Swatches->AddChildToHorizontalBox(WrapSize(Swatch, 40.0f, 30.0f)))
			SwatchSlot->SetPadding(FMargin(Index == 0 ? 0.0f : 4.0f, 0.0f, 0.0f, 0.0f));
	}
	Line->AddChildToHorizontalBox(WrapSize(Swatches, PGSettingsWidgetLocal::SettingsControlWidth, 0.0f));
	Page->AddChild(Back);
}

void UPGSettingsWidget::AddKeyList(UPanelWidget* Page)
{
	AddSectionTitle(Page, NSLOCTEXT("PGSettings", "KeysTitle", "키 설정"));
	// 키를 바꾸는 기능은 넣지 않았다. 엔진의 "플레이어가 키를 바꾸는 기능"(Enhanced Input 사용자 설정)은
	//   입력 자산(IMC_Player)의 키마다 "플레이어가 바꿀 수 있음" 을 켜야 쓸 수 있는데, 그 자산은 팀 캐릭터 담당 것이라 우리가 못 고친다.
	//   그래서 지금은 자산에 적힌 키를 읽어서 보여 주기만 한다.
	AddLine(Page, NSLOCTEXT("PGSettings", "KeysNote", "보기만 됩니다 · 키 바꾸기는 입력 자산에서 허용해야 쓸 수 있습니다"),
		14, TEXT("Regular"), PGFlowStyle::TextFaint(), FMargin(4.0f, 0.0f, 0.0f, 8.0f));

	// 동작별로 키를 모은다(이동은 W·A·S·D 네 줄이 한 동작). 자산에 적힌 순서를 지킨다.
	TArray<TPair<FText, TArray<FString>>> Groups;
	TArray<const UInputAction*> GroupActions;
	if (const UInputMappingContext* Context = LoadObject<UInputMappingContext>(nullptr, PGSettingsWidgetLocal::SettingsPlayerInputContext))
	{
		for (const FEnhancedActionKeyMapping& Mapping : Context->GetMappings())
		{
			const UInputAction* Action = Mapping.Action;
			if (!Action || !Mapping.Key.IsValid())
				continue;
			int32 GroupIndex = GroupActions.IndexOfByKey(Action);
			if (GroupIndex == INDEX_NONE)
			{
				GroupIndex = GroupActions.Add(Action);
				Groups.Add(TPair<FText, TArray<FString>>(PGSettingsWidgetLocal::SettingsActionName(Action), TArray<FString>()));
			}
			Groups[GroupIndex].Value.AddUnique(Mapping.Key.GetDisplayName(false).ToString());
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PGSettings: key list — %s not found"), PGSettingsWidgetLocal::SettingsPlayerInputContext);
	}
	// 우리 쪽 키(일시 정지 메뉴)도 같이 보여 준다.
	Groups.Add(TPair<FText, TArray<FString>>(NSLOCTEXT("PGSettings", "ActPause", "일시 정지 메뉴"), TArray<FString>{ TEXT("Esc"), TEXT("F10") }));

	for (int32 Index = 0; Index < Groups.Num(); ++Index)
	{
		UBorder* Back = MakePanel(Index % 2 == 0 ? PGFlowStyle::RowTint() : FLinearColor::Transparent, FMargin(16.0f, 8.0f), 4.0f);
		UHorizontalBox* Line = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Back->SetContent(Line);
		if (UHorizontalBoxSlot* NameSlot = Line->AddChildToHorizontalBox(MakeText(Groups[Index].Key, 17, TEXT("Regular"), PGFlowStyle::Text())))
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		UTextBlock* Keys = MakeText(FText::FromString(FString::Join(Groups[Index].Value, TEXT("  ·  "))), 17, TEXT("SemiBold"), PGFlowStyle::Accent());
		Keys->SetJustification(ETextJustify::Center);
		Line->AddChildToHorizontalBox(WrapSize(Keys, PGSettingsWidgetLocal::SettingsControlWidth, 0.0f));
		if (UVerticalBoxSlot* RowSlot = Cast<UVerticalBoxSlot>(Page->AddChild(Back)))
			RowSlot->SetPadding(FMargin(0.0f, 1.0f));
	}
}

// ---- 값 바꾸기 ----

void UPGSettingsWidget::StepRow(int32 RowIndex, int32 Direction)
{
	if (!Rows.IsValidIndex(RowIndex) || Rows[RowIndex].bSlider)
		return;
	FPGSettingsRow& Row = Rows[RowIndex];
	if (Row.SelectableCount <= 0 || !Row.GetIndex || !Row.SetIndex)
		return;
	int32 Current = Row.GetIndex();
	// "사용자 지정" 처럼 고를 수 없는 칸에 있으면, 오른쪽은 첫 칸·왼쪽은 마지막 칸부터 시작한다.
	if (Current >= Row.SelectableCount)
		Current = Direction > 0 ? -1 : Row.SelectableCount;
	// 끝에서 멈춘다(돌아가지 않는다). "낮음" 에서 왼쪽을 눌렀는데 "최고" 가 되면 놀란다.
	const int32 Next = FMath::Clamp(Current + Direction, 0, Row.SelectableCount - 1);
	Row.SetIndex(Next);
	RefreshRows();
}

void UPGSettingsWidget::SlideRow(int32 RowIndex, float Value)
{
	if (!Rows.IsValidIndex(RowIndex) || !Rows[RowIndex].bSlider)
		return;
	FPGSettingsRow& Row = Rows[RowIndex];
	const float Snapped = FMath::Clamp(Row.Step > 0.0f ? FMath::GridSnap(Value, Row.Step) : Value, Row.Min, Row.Max);
	Row.SetValue(Snapped);
	// 슬라이더를 끄는 중에는 그 줄 글자만 바꾼다(슬라이더 값을 다시 넣으면 손잡이가 떨린다).
	if (UTextBlock* Text = Row.ValueText.Get())
		Text->SetText(Row.Format ? Row.Format(Snapped) : FText::AsNumber(Snapped));
	RefreshBrightnessPreview();
	RefreshStatus();
}

void UPGSettingsWidget::RefreshRows()
{
	for (FPGSettingsRow& Row : Rows)
	{
		if (Row.bSlider)
		{
			const float Value = Row.GetValue ? Row.GetValue() : 0.0f;
			if (USlider* Slider = Row.Slider.Get())
				Slider->SetValue(Value); // 코드로 넣는 값은 OnValueChanged 를 부르지 않는다(사용자가 끌 때만 부른다)
			if (UTextBlock* Text = Row.ValueText.Get())
				Text->SetText(Row.Format ? Row.Format(Value) : FText::AsNumber(Value));
		}
		else if (UTextBlock* Text = Row.ValueText.Get())
		{
			const int32 Index = Row.GetIndex ? Row.GetIndex() : 0;
			Text->SetText(Row.Options.IsValidIndex(Index) ? Row.Options[Index] : FText::GetEmpty());
		}
	}
	RefreshBrightnessPreview();
	RefreshStatus();
}

void UPGSettingsWidget::RefreshBrightnessPreview()
{
	// 밝기 B 를 적용하면 3D 의 감마가 2.2 → 2.2×B 가 된다. 원래 화면에서 v 로 보이던 밝기는 v^(1/B) 로 보이게 된다.
	const float Brightness = FMath::Clamp(Pending.Brightness, 0.5f, 1.5f);
	const int32 Count = BrightnessSwatches.Num();
	for (int32 Index = 0; Index < Count; ++Index)
	{
		UImage* Swatch = BrightnessSwatches[Index];
		if (!Swatch)
			continue;
		const float Level = Count > 1 ? static_cast<float>(Index) / (Count - 1) : 0.0f;
		const float Shown = FMath::Pow(Level, 1.0f / Brightness);
		const uint8 Byte = static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(Shown * 255.0f), 0, 255));
		Swatch->SetBrush(PGFlowStyle::FlatBrush(PGFlowStyle::Srgb(Byte, Byte, Byte), 2.0f));
	}
}

void UPGSettingsWidget::RefreshStatus()
{
	if (!StatusText)
		return;
	if (!Pending.IsSameAs(Saved))
	{
		StatusText->SetText(NSLOCTEXT("PGSettings", "StatusDirty", "바뀐 값이 있습니다 · 적용을 눌러야 게임에 반영되고 저장됩니다"));
		StatusText->SetColorAndOpacity(FSlateColor(PGFlowStyle::Accent()));
	}
	else
	{
		StatusText->SetText(LastApplyNote);
		StatusText->SetColorAndOpacity(FSlateColor(PGFlowStyle::TextDim()));
	}
}

// ---- 버튼 ----

void UPGSettingsWidget::HandleApply()
{
	const bool bSkippedDisplay = UPGGameSettings::ApplyAndSave(Pending, this);
	Saved = Pending;
	LastApplyNote = bSkippedDisplay
		? NSLOCTEXT("PGSettings", "StatusAppliedEditor", "적용했습니다 · 에디터에서는 디스플레이 모드·해상도를 건너뜁니다")
		: NSLOCTEXT("PGSettings", "StatusApplied", "적용했습니다");
	RefreshStatus();
}

void UPGSettingsWidget::HandleBack()
{
	if (!Pending.IsSameAs(Saved))
		UE_LOG(LogTemp, Display, TEXT("PGSettings: closed without applying — pending changes discarded"));
	CloseSelf();
}

void UPGSettingsWidget::HandleDefaults()
{
	// 모든 탭을 기본값으로 "고른다". 게임에 걸리는 건 적용을 눌렀을 때다(다른 값과 같은 규칙).
	Pending = UPGGameSettings::MakeDefaults();
	if (!ResolutionChoices.Contains(Pending.Resolution) && ResolutionChoices.Num() > 0)
	{
		// 모니터 해상도가 선택지에 없으면(드문 크기) 목록에서 모니터보다 크지 않은 가장 큰 것을 고른다(목록은 큰 것부터다).
		FIntPoint Pick = ResolutionChoices.Last();
		for (const FIntPoint& Size : ResolutionChoices)
			if (Size.X <= Pending.Resolution.X && Size.Y <= Pending.Resolution.Y) { Pick = Size; break; }
		Pending.Resolution = Pick;
	}
	RefreshRows();
}

void UPGSettingsWidget::SelectTab(int32 Index)
{
	if (Pages)
		Pages->SetActiveWidgetIndex(Index);
	for (int32 Tab = 0; Tab < TabLabels.Num(); ++Tab)
	{
		const bool bActive = Tab == Index;
		if (TabLabels[Tab])
			TabLabels[Tab]->SetColorAndOpacity(FSlateColor(bActive ? PGFlowStyle::Accent() : PGFlowStyle::TextDim()));
		if (TabLines.IsValidIndex(Tab) && TabLines[Tab])
			TabLines[Tab]->SetBrush(PGFlowStyle::FlatBrush(bActive ? PGFlowStyle::Accent() : FLinearColor::Transparent, 0.0f));
	}
}

void UPGSettingsWidget::SelectGraphicsPage(int32 Index)
{
	if (GraphicsPages)
		GraphicsPages->SetActiveWidgetIndex(Index);
	for (int32 Tab = 0; Tab < SubTabLabels.Num(); ++Tab)
	{
		const bool bActive = Tab == Index;
		if (SubTabLabels[Tab])
			SubTabLabels[Tab]->SetColorAndOpacity(FSlateColor(bActive ? PGFlowStyle::Text() : PGFlowStyle::TextFaint()));
		if (SubTabLines.IsValidIndex(Tab) && SubTabLines[Tab])
			SubTabLines[Tab]->SetBrush(PGFlowStyle::FlatBrush(bActive ? PGFlowStyle::Text() : FLinearColor::Transparent, 0.0f));
	}
}

void UPGSettingsWidget::HandleTabGraphics() { SelectTab(0); }
void UPGSettingsWidget::HandleTabAudio() { SelectTab(1); }
void UPGSettingsWidget::HandleTabControls() { SelectTab(2); }
void UPGSettingsWidget::HandleTabGameplay() { SelectTab(3); }
void UPGSettingsWidget::HandleGraphicsBasic() { SelectGraphicsPage(0); }
void UPGSettingsWidget::HandleGraphicsAdvanced() { SelectGraphicsPage(1); }
