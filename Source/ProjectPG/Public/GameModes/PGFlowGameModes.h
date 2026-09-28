// 흐름 화면(타이틀·로비·스코어보드)용 게임모드와 플레이어 컨트롤러. (2026-09-22)
//
// 세 화면은 하는 일이 같다: 폰 없이(관전 폰) 마우스를 켜고 화면 위젯 하나를 띄운다. 그래서 바탕 클래스 하나에
//   "어느 화면인가" 와 "어떤 위젯을 띄우나" 만 다르게 둔다.
//
// UI 담당이 진짜 위젯을 만들면: 이 게임모드를 부모로 BP 를 만들어 ScreenWidgetClass 만 바꾸거나, 레벨의 게임모드 오버라이드로 그 BP 를
//   지정하면 된다. 위젯이 뜬 뒤 OnFlowScreenReady(BP 이벤트)가 불리므로 거기서 데이터를 채워도 된다.
//   데이터는 전부 UPGRunSubsystem(GetLastRun / GetStashItems / GetStats)에서 읽는다.
//
// 화면 뒤 3D 무대(풀밭·나무·차·캐릭터·빛·카메라)는 위젯과 따로 게임모드가 짓는다(Flow/PGFlowStage.h). 그래서 UI 담당이
//   위젯만 바꿔 끼워도 뒤 배경은 그대로 남는다. 레벨에 직접 무대를 꾸미면 bBuildStage 를 끈다.
//
// 게임 맵의 게임모드(팀 AGameModePG)는 건드리지 않는다. 판 기록은 PGRunSubsystem 이 스스로 시작한다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Flow/PGFlowStage.h"
#include "Flow/PGRunSubsystem.h"
#include "PGFlowGameModes.generated.h"

class UUserWidget;
class APGTitleIntro;

// 마우스 커서 + UI 입력. 흐름 화면에는 조종할 폰이 없다.
//
// 화면 연출(3D 무대·인트로·화면 위젯·로딩 화면)은 이 컨트롤러가 **자기 컴퓨터에서** 한다(2026-09-27 멀티 정리).
// 왜: 예전에는 게임모드가 했다. 게임모드는 언리얼 규칙상 서버에만 있어서, 전용 서버(Play As Client)로 돌리면
//   화면 없는 서버가 위젯을 띄우려다 거절당하고("로컬 플레이어 컨트롤러만 위젯에…") 접속한 사람은 까만 화면만 봤다.
// 어느 화면인지·어떤 위젯인지는 게임모드의 설정 칸 그대로다. 클라이언트에는 게임모드가 없으므로 복제되는
//   게임 상태(GameState)가 알려 주는 게임모드 종류의 기본값(CDO)에서 읽는다.
UCLASS()
class PROJECTPG_API APGFlowPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	APGFlowPlayerController();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UUserWidget* GetScreenWidget() const { return ScreenWidget; }

protected:
	// 이 화면의 설정(서버·혼자 할 때는 진짜 게임모드, 클라이언트는 게임모드 종류의 기본값). 아직 모르면 nullptr.
	const class APGFlowGameModeBase* GetFlowConfig() const;
	// 설정을 알게 되면 연출을 시작한다(클라이언트는 GameState 가 복제될 때까지 잠깐 기다린다).
	void StartPresentation();
	void CreateScreenWidget();
	// 무대를 짓고 카메라를 화면으로 잡는다. 한 번만.
	void BuildStage();
	void TakeFlowShotIfRequested(TSubclassOf<UUserWidget> WidgetClass);

	EPGFlowScreen Screen = EPGFlowScreen::Title;
	bool bPresentationStarted = false;
	// 지은 무대 중 매 틱 움직일 것(카메라·불빛). 액터는 레벨이 들고 있어 여기는 약한 포인터뿐이다(UPROPERTY 불필요).
	FPGFlowStageHandles Stage;
	bool bStageBuilt = false;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> ScreenWidget;

	// 첫 타이틀에서만 도는 인트로(Flow/PGTitleIntro.h). 도는 동안은 화면 위젯을 미루고, 끝나면 위젯을 서서히 띄운다.
	UPROPERTY(Transient)
	TObjectPtr<APGTitleIntro> TitleIntro;

	// 인트로 뒤 위젯이 나타나는 중이면 0 이상(경과 초). 아니면 -1.
	float WidgetFadeAge = -1.0f;

	FTimerHandle RetryTimer;
};

// 흐름 화면의 설정(어느 화면·어떤 위젯·무대를 지을지)과 BP 이벤트. 연출 자체는 APGFlowPlayerController 가 각자 컴퓨터에서 한다.
UCLASS(Abstract)
class PROJECTPG_API APGFlowGameModeBase : public AGameModeBase
{
	GENERATED_BODY()

public:
	APGFlowGameModeBase();

	// 화면에 띄울 위젯. 기본은 코드로 짠 임시 위젯(UI/PGFlowWidgets.h). UI 담당 위젯으로 바꾸는 자리.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PG|Flow")
	TSubclassOf<UUserWidget> ScreenWidgetClass;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Flow")
	EPGFlowScreen Screen = EPGFlowScreen::Title;

	// 화면 뒤 3D 무대를 코드로 지을지. 레벨에 직접 배치한 무대를 쓰려면 끈다(그때는 레벨의 카메라도 직접 정해야 한다).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PG|Flow")
	bool bBuildStage = true;

	// 이 컴퓨터(로컬 플레이어)의 화면 위젯. 연출은 APGFlowPlayerController 가 하므로 거기서 읽는다.
	UFUNCTION(BlueprintCallable, Category = "PG|Flow")
	UUserWidget* GetScreenWidget() const;

	// 위젯이 뷰포트에 올라간 직후. BP 에서 데이터를 채우거나 애니메이션을 틀 자리.
	// (게임모드가 있는 컴퓨터 — 혼자 할 때·서버 — 에서만 불린다. 클라이언트에는 게임모드가 없다.)
	UFUNCTION(BlueprintImplementableEvent, Category = "PG|Flow")
	void OnFlowScreenReady(UUserWidget* Widget);
};

UCLASS()
class PROJECTPG_API APGTitleGameMode : public APGFlowGameModeBase
{
	GENERATED_BODY()

public:
	APGTitleGameMode();
};

// 로비: 창고(판 밖 아이템) 확인, 지난 판 결과, 출격. 팀의 AGameMode_InLobby(로그인·매칭 서버 흐름)와는 별개다 —
//   그쪽은 서버가 있어야 돌고, 이쪽은 로컬 SaveGame 창고로 판 사이를 잇는다.
UCLASS()
class PROJECTPG_API APGLobbyGameMode : public APGFlowGameModeBase
{
	GENERATED_BODY()

public:
	APGLobbyGameMode();
};

UCLASS()
class PROJECTPG_API APGScoreboardGameMode : public APGFlowGameModeBase
{
	GENERATED_BODY()

public:
	APGScoreboardGameMode();
};
