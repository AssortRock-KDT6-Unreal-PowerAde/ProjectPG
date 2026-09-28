// 흐름 화면 세 개(타이틀·로비·레이드 결과)의 위젯. (2026-09-22, 임시 상자 화면을 배그 로비·타르코프 결과 화면 느낌으로 교체)
//
// 위젯 블루프린트 없이 C++ 로 트리를 짠다(PGInteractionPromptWidget 과 같은 이유: 에셋이면 팀 저장소로 옮길 때 같이 옮겨야 하고
//   머지 충돌이 난다). 뒤에 보이는 3D 무대(풀밭·나무·차·캐릭터)는 이 위젯이 아니라 게임모드가 짓는다(Flow/PGFlowStage.h).
//   그래서 UI 담당이 진짜 위젯을 만들면 게임모드(PGFlowGameModes.h)의 ScreenWidgetClass 만 바꾸면 되고, 배경은 그대로 남는다.
//
// 모양 규칙(세 화면 공통): 어두운 반투명 판 + 노란 강조(#E8C547) + 넉넉한 여백. 위치는 전부 화면 가장자리 기준(앵커)이라
//   1920x1080 에서 맞춘 배치가 다른 해상도에서도 DPI 배율로 같이 커지고 작아진다.
//
// 데이터는 전부 UPGRunSubsystem 에서 읽는다(GetLastRun / GetStashItems / GetStats / GetRunHistory, OnStashChanged).
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Flow/PGRunTypes.h"
#include "PGFlowWidgets.generated.h"

class IWebSocket;
class UBorder;
class UButton;
class UCanvasPanel;
class UCanvasPanelSlot;
class UFileMediaSource;
class UImage;
class UMediaPlayer;
class UMediaTexture;
class UPanelWidget;
class UPGRunSubsystem;
class UPGSettingsWidget;
class UScrollBox;
class UTextBlock;
class UVerticalBox;
class UWidgetSwitcher;

// 버튼 모양 세 가지. 노란 주 버튼은 화면마다 하나만(눈이 먼저 가야 할 곳).
enum class EPGFlowButtonStyle : uint8
{
	Primary,   // 노란 바탕 + 검은 글자
	Secondary, // 어두운 반투명 바탕 + 흰 글자
	Ghost,     // 바탕 없음(메뉴 탭·작은 링크)
};

// 공통: 화면 전체를 덮는 캔버스 하나. 자식은 BuildContent 에서 캔버스에 판을 붙인다.
UCLASS(Abstract)
class PROJECTPG_API UPGFlowScreenWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	// 코드 기본 화면을 짠다(WBP 로 뜨면 부르지 않는다 — WBP 가 이미 배치를 들고 있다).
	virtual void BuildContent() {}
	// WBP 로 떴을 때 한 번 부른다. WBP 가 준 빈 상자에 코드가 내용을 채워야 하는 화면(환경설정의 설정 줄)이 쓴다.
	virtual void BuildIntoDesigned() {}

	// 지금 화면이 코드로 지어졌나(WBP 가 아니라).
	bool IsCodeBuilt() const { return bCodeBuilt; }

	UPGRunSubsystem* GetRun() const;

	// ---- 조립 도우미 ----
	UTextBlock* MakeText(const FText& Text, int32 Size, const FName& Typeface, const FLinearColor& Color);
	UBorder* MakePanel(const FLinearColor& Color, const FMargin& Padding, float Radius = 6.0f);
	UButton* MakeButton(const FText& Label, EPGFlowButtonStyle Style, int32 FontSize, const FMargin& Padding, UTextBlock** OutLabel = nullptr);
	// Size 가 0 이면 내용 크기에 맞춘다.
	UCanvasPanelSlot* PlaceOnCanvas(UWidget* Widget, const FAnchors& Anchors, const FVector2D& Alignment, const FVector2D& Position,
		const FVector2D& Size = FVector2D::ZeroVector);
	UWidget* WrapSize(UWidget* Content, float Width, float Height);
	UTextBlock* AddLine(UPanelWidget* Parent, const FText& Text, int32 Size, const FName& Typeface, const FLinearColor& Color,
		const FMargin& Padding = FMargin(0.0f, 2.0f));
	UTextBlock* AddSectionHeader(UPanelWidget* Parent, const FText& Text);
	// "레이드 시간 ........ 3분 12초" 한 줄. 이름은 왼쪽 흐리게, 값은 오른쪽 굵게.
	void AddStatRow(UPanelWidget* Parent, const FText& Label, const FText& Value, const FLinearColor& ValueColor);
	void AddSpacer(UPanelWidget* Parent, float Height);
	void AddDivider(UPanelWidget* Parent);
	// 아이템 목록 한 줄(이름 · 개수). 짝수 줄은 살짝 밝게 깔아 줄이 눈으로 따라가진다.
	void AddItemRow(UPanelWidget* Parent, const FPGItemStack& Stack, int32 Index);

	static FText ResultText(EPGRunResult Result);
	// 결과 띠에 쓰는 한 단어(생존·사망·시간 초과).
	static FText ResultBandText(EPGRunResult Result);
	static FLinearColor ResultColor(EPGRunResult Result);
	static FText FormatDuration(float Seconds);
	static FText FormatDistance(float Centimeters);

	// 코드 기본 화면의 바탕 캔버스. 이름을 Root 로 두면 WBP 의 루트 위젯 "Root" 와 이름이 겹쳐 WBP 컴파일이 경고를 낸다(9/23).
	UPROPERTY(Transient)
	TObjectPtr<UCanvasPanel> CodeRoot;

	bool bCodeBuilt = false;
	bool bDesignedBuilt = false;
};

// 타이틀 화면의 진행 상태.
enum class EPGTitleState : uint8
{
	Checking,  // 서버에 붙어 보는 중
	LoginOpen, // 팀 로그인 창이 떠 있다
	Offline,   // 서버 없음 → 오프라인 버튼
	Ready,     // 이미 로그인 창을 한 번 거쳤다 → 시작 버튼
	Leaving,   // 로비로 넘어가는 중
};

// 타이틀: 불타는 폐차 앞의 캐릭터 + 아래쪽 "PROJECT PG" 로고 + 서버 연결 상태.
//   서버가 켜져 있으면 팀 로그인 창(WBP_LoginWindow·WBP_LoginPopUp)을 우리 배경 위에 띄우고, 로그인에 성공하면 우리 로비로 간다.
//   서버가 꺼져 있으면 "서버 연결 안 됨 — 오프라인으로 시작" 버튼으로 그냥 로비로 간다.
UCLASS()
class PROJECTPG_API UPGTitleScreenWidget : public UPGFlowScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleStart();

	UFUNCTION()
	void HandleOffline();

	UFUNCTION()
	void HandleQuit();

	// 팀 UAuthSubSystem::OnLoginStatusChanged 를 같이 듣는다(팀 ULobbyUIFlowController 도 듣는다 — 우리는 "성공하면 로비로" 만 덧붙인다).
	UFUNCTION()
	void HandleLoginStatus(bool bIsLoggedIn, bool bInventoryLoaded, const FString& Message);

	void SetState(EPGTitleState NewState, const FText& Status);
	void BeginServerCheck();
	void FinishServerCheck(bool bOnline);
	void OpenTeamLogin();
	void CloseTeamLogin();
	void LeaveToLobby();
	void StartTitleVideo(UImage* Target);

	// ---- WBP 자리 (9/23 블루프린트 분리, WBP_PGTitle) ----
	// 서버 확인·로그인 흐름은 C++ 에 그대로 두고, 로고·버튼·글자 배치만 WBP 가 맡는다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Title", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Title", meta = (BindWidgetOptional))
	TObjectPtr<UButton> StartButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Title", meta = (BindWidgetOptional))
	TObjectPtr<UButton> OfflineButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Title", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitButton;

	// 배경 영상을 그릴 전체 화면 이미지(영상 파일이 있을 때만 보인다).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Title", meta = (BindWidgetOptional))
	TObjectPtr<UImage> TitleVideo;

	// 오프라인 버튼 안의 글자. WBP 에서는 버튼 안 첫 글자를 찾아 쓴다.
	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> OfflineLabel;

	// 배경 영상(Content/Movies/PG_TitleLoop.mp4 가 있을 때만).
	UPROPERTY(Transient)
	TObjectPtr<UMediaPlayer> VideoPlayer;

	UPROPERTY(Transient)
	TObjectPtr<UMediaTexture> VideoTexture;

	UPROPERTY(Transient)
	TObjectPtr<UFileMediaSource> VideoSource;

	// 서버가 살아 있나 한 번 붙어 보는 소켓. 팀 소켓(UWebSocketSubSystem)은 연결 상태를 밖에 알려 주는 함수가 없어서 따로 본다.
	TSharedPtr<IWebSocket> ServerProbe;
	FTimerHandle ProbeTimeout;
	FTimerHandle LeaveTimer;
	EPGTitleState State = EPGTitleState::Checking;
	bool bTeamLoginOpened = false;
	float StatusClock = 0.0f;
	FText StatusBase;
};

// 로비: 위 메뉴 줄(출격·창고·기록 | 환경설정·타이틀로), 오른쪽 판(선택한 메뉴 내용), 왼쪽 아래 큰 노란 "출격" 버튼. 가운데는 3D 캐릭터.
//   환경설정은 탭이 아니라 버튼이다: 설정 항목이 많아(그래픽·오디오·컨트롤·게임플레이) 오른쪽 좁은 판에 안 들어가고,
//   게임 속 ESC 메뉴에서도 같은 화면을 띄워야 해서 따로 떠 있는 큰 창(UPGSettingsWidget)으로 만들었다.
UCLASS()
class PROJECTPG_API UPGLobbyScreenWidget : public UPGFlowScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UFUNCTION()
	void RefreshLists();

	UFUNCTION()
	void HandleDeploy();

	UFUNCTION()
	void HandleBackToTitle();

	UFUNCTION()
	void HandleOpenSettings();

	void HandleSettingsClosed();

	UFUNCTION()
	void HandleTabDeploy();

	UFUNCTION()
	void HandleTabStash();

	UFUNCTION()
	void HandleTabRecord();

	void SelectTab(int32 Index);
	UButton* AddTab(UPanelWidget* Parent, const FText& Label, int32 Index);

	// 탭이 바뀔 때 WBP 가 받는 알림. 밑줄·아이콘 같은 탭 꾸밈은 WBP 그래프에서 한다(글자 색은 C++ 가 이미 바꾼다).
	UFUNCTION(BlueprintImplementableEvent, Category = "PG|Lobby")
	void OnTabChanged(int32 TabIndex);

	static constexpr int32 TabCount = 3;

	// ---- WBP 자리 (9/23 블루프린트 분리) ----
	// WBP_PGLobby 에서 같은 이름으로 위젯을 놓으면 자동으로 여기에 연결된다(BindWidgetOptional).
	// WBP 없이 C++ 기본 화면이 뜰 때는 BuildContent 가 같은 자리를 코드로 채운다 — 그래서 아래 동작 코드는 하나로 충분하다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> DeployButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TitleButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TabDeployButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TabStashButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UButton> TabRecordButton;

	// 탭 세 쪽(0 출격 · 1 창고 · 2 기록) 을 넘기는 스위처.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UWidgetSwitcher> Pages;

	// 지난 레이드·누적 기록 줄을 C++ 가 채우는 세로 상자.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> DeployPage;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> StashList;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StashSummary;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> RecordList;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Lobby", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> TopBarStatus;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextBlock>> TabLabels;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UBorder>> TabUnderlines;

	// 떠 있는 환경설정 창(없으면 null). 두 번 눌러 두 장 뜨지 않게 들고 있는다.
	UPROPERTY(Transient)
	TObjectPtr<UPGSettingsWidget> SettingsWindow;

	int32 ActiveTab = 0;
};

// 레이드 결과(타르코프 "레이드 종료" 느낌): 흐린 배경의 캐릭터, 큰 제목, 탈출구 이름, 생존/사망 띠, 기록 판, 아이템 판, 다음(로비로).
//   타이틀로 바로 가는 버튼은 없다 — 로비 위 메뉴 줄에 "타이틀로" 가 있다(9/22 사용자 결정).
UCLASS()
class PROJECTPG_API UPGScoreboardScreenWidget : public UPGFlowScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void NativeConstruct() override;

	UFUNCTION()
	void HandleNext();

	// 지난 판 기록으로 글자·색·목록을 채운다. 배치(WBP 든 코드든)는 건드리지 않는다.
	void FillFromRecord();

	// ---- WBP 자리 (9/23 블루프린트 분리, WBP_PGScoreboard) ----
	// 판마다 바뀌는 것만 C++ 가 채운다: 부제(탈출구 이름), 결과 띠 색·글자, 기록 줄, 아이템 제목·요약·목록.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> SubtitleText;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Band;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> BandText;

	// "레이드 기록" 제목과 줄들을 C++ 가 통째로 채우는 세로 상자.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> StatsBox;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemsHeader;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ItemsNote;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UScrollBox> ItemList;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Scoreboard", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NextButton;
};
