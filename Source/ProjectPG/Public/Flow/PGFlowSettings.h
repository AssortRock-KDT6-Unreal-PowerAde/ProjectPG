// 게임 흐름 설정 (프로젝트 설정 > Game > ProjectPG Flow). 레벨 경로와 판 규칙을 코드 밖에서 바꾼다.
//
// 레벨이 비어 있거나 없으면 엔진의 빈 맵(/Engine/Maps/Entry)에 게임모드만 얹어서 연다 — .umap 을 안 만들어도 흐름이 돈다.
//   (오브젝트 스모크 테스트가 같은 방법으로 Entry 맵을 쓴다.) 에디터에서 진짜 레벨을 만들면 여기 경로만 넣으면 된다.
//
// 시작 맵(DefaultEngine.ini 의 GameDefaultMap)은 건드리지 않는다: 헤드리스 스모크 테스트와 PIE 습관이 그 설정을 믿고 있다.
//   타이틀부터 시작하고 싶으면 사용자가 GameDefaultMap 을 타이틀 레벨로 바꾸거나 콘솔에서 PG.Flow.Title 을 친다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PGFlowSettings.generated.h"

class UUserWidget;

class UWorld;

// 시작 로고와 타이틀 화면 사이에 트는 짧은 인트로(Flow/PGTitleIntro.h). 두 가지를 만들어 두고 고른다(2026-09-22 사용자).
// ini 에는 이름(ShipDragon/Rescue/None)으로 저장된다 — 순서를 바꿔도 설정이 엉뚱한 값으로 바뀌지 않는다.
UENUM()
enum class EPGTitleIntroVariant : uint8
{
	None,       // 인트로 없이 바로 타이틀
	ShipDragon, // 하늘을 가르는 전함과 뒤쫓는 드래곤 → 캐릭터로 내려온다
	Rescue,     // 주인공이 몹과 싸우다 위기 → 날으는 변신 차가 구해 주고 여고생으로 돌아와 나란히 선다
	Ambush,     // 소총으로 몹을 잡다가 뒤에서 두 마리가 덮치는 순간 느려지고, 뚝 끊겨 타이틀로(예고편식)
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ProjectPG Flow"))
class PROJECTPG_API UPGFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPGFlowSettings();

	static const UPGFlowSettings& Get() { return *GetDefault<UPGFlowSettings>(); }

	// ---- 레벨 ----
	// 비어 있으면 /Engine/Maps/Entry + 해당 게임모드. 제안 경로: /Game/PG/Level/Flow/L_Title, L_Lobby, L_Scoreboard.
	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> TitleLevel;

	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> LobbyLevel;

	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> ScoreboardLevel;

	// 출격하면 여는 게임 맵. 기본은 지금 쓰는 절차 생성 시험 맵.
	UPROPERTY(Config, EditAnywhere, Category = "Levels", meta = (AllowedClasses = "/Script/Engine.World"))
	TSoftObjectPtr<UWorld> GameLevel;

	// ---- 화면 위젯(WBP) ----
	// 눈에 보이는 배치는 WBP 가, 버튼 동작·데이터는 C++ 부모가 맡는다(9/23 블루프린트 분리 결정, Docs/BlueprintMigrationPlan_2026-09-23.md).
	// 비어 있거나 못 읽으면 C++ 가 코드로 짠 기본 화면을 띄운다 — WBP 를 옮기는 도중에도 게임이 안 멈추게.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> TitleScreenClass;

	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> LobbyScreenClass;

	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> ScoreboardScreenClass;

	// 게임 속 ESC 메뉴. 부모가 PGPauseMenuWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> PauseMenuClass;

	// 환경설정 창(로비·ESC 메뉴 공용). 부모가 PGSettingsWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> SettingsWindowClass;

	// 게임 속 화면 가운데 상호작용 안내("[F] 줍기"). 부모가 PGInteractionPromptWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> InteractionPromptClass;

	// 검은 로딩·시작 로고. 부모가 PGLoadingScreenWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> LoadingScreenClass;

	// 화면 위쪽 안내 한 줄(보스 처치·탈출 안내 등). 부모가 PGAnnounceLineWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> AnnounceLineClass;

	// 탈출 카운트다운 한 줄("탈출까지 3"). 부모가 PGCountdownLineWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> CountdownLineClass;

	// 화면 가운데 조준점 + 가발 광선 고리(9/23). 부모가 PGCrosshairWidget 인 WBP 여야 한다.
	UPROPERTY(Config, EditAnywhere, Category = "Screens")
	TSoftClassPtr<UUserWidget> CrosshairClass;

	// 설정에 적힌 WBP 를 읽어 돌려준다. 비었거나, 못 읽었거나, Fallback 의 자식이 아니면 Fallback(코드 화면)을 돌려준다.
	static UClass* ResolveWidgetClass(const TSoftClassPtr<UUserWidget>& Designed, UClass* Fallback);

	// ---- 타이틀 인트로 ----
	// 게임을 켜고 처음 타이틀이 열릴 때 한 번 트는 인트로. 콘솔 PG.Flow.Intro <이름> 으로 잠깐 바꿔 볼 수 있다.
	UPROPERTY(Config, EditAnywhere, Category = "Title")
	EPGTitleIntroVariant TitleIntroVariant = EPGTitleIntroVariant::Ambush;

	// ---- 판 규칙 ----
	// 판 제한 시간(초). 0 이면 없음. 넘기면 결과 = 시간 초과, 아이템은 사망과 같이 잃는다.
	UPROPERTY(Config, EditAnywhere, Category = "Run", meta = (ClampMin = "0.0"))
	float RunTimeLimitSeconds = 0.0f;

	// 판이 끝난 뒤 스코어보드로 자동으로 넘어갈지. 끄면 결과만 기록하고 그 자리에 남는다(PIE 로 다른 것을 시험할 때).
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	bool bAutoTravelAfterRun = true;

	// 탈출·사망 연출을 볼 시간을 주고 나서 스코어보드로 간다(초).
	UPROPERTY(Config, EditAnywhere, Category = "Run", meta = (ClampMin = "0.0"))
	float ResultScreenDelaySeconds = 3.0f;

	// 게임 맵에 들어가 로딩 화면이 걷히면 한 번 띄우는 안내 문구(안내 줄 WBP_PGAnnounceLine 으로 뜬다). 비우면 안 띄운다. (9/23)
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	FText RaidStartMessage = NSLOCTEXT("PGFlow", "RaidStart", "게임이 시작되었습니다");

	// 출격할 때 창고 아이템을 전부 주머니에 넣어 가지고 나갈지. 켜면 사망 시 창고째 잃는 타르코프식이 된다.
	// 기본은 끔: 로비에서 창고를 "확인" 하는 것이 먼저이고, 무엇을 가져갈지 고르는 UI 는 아직 없다.
	UPROPERTY(Config, EditAnywhere, Category = "Run")
	bool bCarryStashIntoRun = false;

	// 창고 저장 파일 슬롯 이름(Saved/SaveGames/<이름>.sav).
	UPROPERTY(Config, EditAnywhere, Category = "Stash")
	FString StashSlotName = TEXT("PGStash");

	// 지난 판 기록을 몇 개까지 남기나.
	UPROPERTY(Config, EditAnywhere, Category = "Stash", meta = (ClampMin = "1", ClampMax = "100"))
	int32 MaxRunHistory = 10;
};
