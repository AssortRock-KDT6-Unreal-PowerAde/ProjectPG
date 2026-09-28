// 환경설정 화면. 로비의 "환경설정" 버튼과 게임 속 일시 정지 메뉴(ESC/F10)가 같은 위젯을 띄운다. (2026-09-22)
//
// 구성(배그 설정 화면을 본떴다 — Docs/SettingsResearch_2026-09-22.md):
//   위 탭: 그래픽(기본·고급) / 오디오 / 컨트롤 / 게임플레이.  아래 버튼: 기본값 / 뒤로 가기 / 적용.
//   값을 바꿔도 "적용" 을 누르기 전까지는 게임에 아무 일도 없다(슬라이더도 같다). 그래야 규칙이 하나라 헷갈리지 않고,
//   해상도처럼 바로 걸면 화면이 깜빡이는 값과 볼륨처럼 바로 걸어도 되는 값을 사용자가 구분할 필요가 없다.
//   뒤로 가기(또는 Esc)는 적용 안 한 값을 버린다.
//
// 값 저장·적용은 전부 UPGGameSettings(Flow/PGGameSettings.h)가 한다. 이 위젯은 "고르는 중" 값(Pending)만 들고 있다.
#pragma once

#include "CoreMinimal.h"
#include "Flow/PGGameSettings.h"
#include "UI/PGFlowWidgets.h"
#include "PGSettingsWidget.generated.h"

class UImage;
class USlider;
class UPGSettingsWidget;

// 줄 하나의 버튼·슬라이더 이벤트를 위젯으로 넘기는 작은 중계 객체.
//   왜 필요한가: UButton::OnClicked 는 매개변수 없는 UFUNCTION 만 받는다. 줄이 30개 가까이라 줄마다 함수를 만들 수는 없으니,
//   "몇 번째 줄의 어느 쪽 화살표인가" 를 이 객체가 들고 있다가 위젯의 StepRow(줄, 방향)를 부른다.
UCLASS()
class PROJECTPG_API UPGSettingsRowRelay : public UObject
{
	GENERATED_BODY()

public:
	TWeakObjectPtr<UPGSettingsWidget> Owner;
	int32 RowIndex = INDEX_NONE;
	int32 Direction = 0;

	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleSliderChanged(float Value);
};

// 설정 한 줄. 화살표로 고르는 줄(Choice)과 슬라이더 줄(Slider) 두 가지.
struct FPGSettingsRow
{
	bool bSlider = false;
	// Choice: 보이는 선택지와 지금 몇 번째인가(Pending 에서 읽고 쓴다).
	TArray<FText> Options;
	int32 SelectableCount = 0;      // 화살표로 고를 수 있는 앞쪽 선택지 수(뒤쪽은 "사용자 지정" 처럼 보여 주기만 하는 칸)
	TFunction<int32()> GetIndex;
	TFunction<void(int32)> SetIndex;
	// Slider: 범위와 값 읽기·쓰기, 오른쪽에 보일 글자.
	float Min = 0.0f;
	float Max = 1.0f;
	float Step = 0.01f;
	TFunction<float()> GetValue;
	TFunction<void(float)> SetValue;
	TFunction<FText(float)> Format;
	// 화면 쪽(위젯 트리가 들고 있으니 약한 포인터로만 본다).
	TWeakObjectPtr<UTextBlock> ValueText;
	TWeakObjectPtr<USlider> Slider;
};

// 설정 화면이 닫혔다(블루프린트에서 들을 일은 없어서 보통 델리게이트).
DECLARE_MULTICAST_DELEGATE(FPGOnSettingsClosed);

UCLASS()
class PROJECTPG_API UPGSettingsWidget : public UPGFlowScreenWidget
{
	GENERATED_BODY()

public:
	// 화면에 띄우고 키보드 초점을 준다(Esc 로 닫으려면 초점이 있어야 한다). ZOrder 는 부른 화면보다 위로.
	static UPGSettingsWidget* Open(APlayerController* Owner, int32 ZOrder);

	// 닫힐 때 부른 쪽(로비·일시 정지 메뉴)이 할 일을 건다. 적용 안 한 값은 이미 버려진 뒤다.
	FPGOnSettingsClosed OnClosed;

	void StepRow(int32 RowIndex, int32 Direction);
	void SlideRow(int32 RowIndex, float Value);

protected:
	virtual void BuildContent() override;
	// WBP 로 떠도 설정 줄은 코드가 만든다(줄 30개가 값에서 나온다).
	virtual void BuildIntoDesigned() override { BuildContent(); }
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleApply();

	UFUNCTION()
	void HandleBack();

	UFUNCTION()
	void HandleDefaults();

	UFUNCTION()
	void HandleTabGraphics();

	UFUNCTION()
	void HandleTabAudio();

	UFUNCTION()
	void HandleTabControls();

	UFUNCTION()
	void HandleTabGameplay();

	UFUNCTION()
	void HandleGraphicsBasic();

	UFUNCTION()
	void HandleGraphicsAdvanced();

	void SelectTab(int32 Index);
	void SelectGraphicsPage(int32 Index);
	void CloseSelf();

	// ---- 줄 만들기 ----
	// 탭 한 쪽 = 스크롤 상자 + 그 안의 세로 줄 상자. 줄은 세로 상자에 붙인다.
	UVerticalBox* MakePage(UPanelWidget* Into);
	void AddSectionTitle(UPanelWidget* Page, const FText& Title);
	// Note 는 이름 아래 흐린 작은 글자(예: "저장만 — 캐릭터 담당이 읽는다"). 비우면 안 보인다.
	int32 AddChoiceRow(UPanelWidget* Page, const FText& Label, const FText& Note, const TArray<FText>& Options,
		TFunction<int32()> GetIndex, TFunction<void(int32)> SetIndex, int32 SelectableCount = -1);
	int32 AddToggleRow(UPanelWidget* Page, const FText& Label, const FText& Note, bool* Value);
	int32 AddSliderRow(UPanelWidget* Page, const FText& Label, const FText& Note, float Min, float Max, float Step, float* Value,
		TFunction<FText(float)> Format);
	void AddBrightnessPreview(UPanelWidget* Page);
	void AddKeyList(UPanelWidget* Page);
	UButton* AddTopTab(UPanelWidget* Parent, const FText& Label, TArray<TObjectPtr<UTextBlock>>& Labels, TArray<TObjectPtr<UBorder>>& Lines, int32 FontSize);
	UButton* MakeArrowButton(const FText& Glyph, int32 RowIndex, int32 Direction);

	// 값이 바뀐 뒤 모든 줄의 글자·슬라이더와 아래 안내 글을 다시 맞춘다.
	void RefreshRows();
	void RefreshBrightnessPreview();
	void RefreshStatus();

	FPGSettingsValues Pending; // 고르는 중
	FPGSettingsValues Saved;   // 마지막으로 적용된 값(바뀐 게 있나 비교용)
	TArray<FIntPoint> ResolutionChoices;
	TArray<FPGSettingsRow> Rows;
	FText LastApplyNote;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPGSettingsRowRelay>> Relays;

	// ---- WBP 자리 (9/23 블루프린트 분리, WBP_PGSettings) ----
	// 설정 줄이 30개 가까이 되고 전부 코드가 값에서 만들어 내므로, WBP 가 맡는 것은 "창 틀" 이다:
	//   뒤를 덮는 어두운 판, 가운데 창(크기·색·모서리), 그 안의 빈 세로 상자(SettingsBody).
	//   제목·탭·설정 줄·아래 버튼은 C++ 이 SettingsBody 안에 지금처럼 채운다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Settings", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> SettingsBody;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> Pages;

	UPROPERTY(Transient)
	TObjectPtr<UWidgetSwitcher> GraphicsPages;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TabLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TabLines;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> SubTabLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> SubTabLines;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UImage>> BrightnessSwatches;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> StatusText;

	bool bClosing = false;
};
