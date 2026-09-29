// 게임 속 일시 정지 메뉴를 여닫는 담당 + 레벨이 열릴 때마다 환경설정을 다시 거는 담당. (2026-09-22)
//
// 왜 월드 서브시스템인가: 팀 게임모드(AGameModePG)·팀 캐릭터·팀 컨트롤러를 고치지 않고 ESC 를 받으려고.
//   월드마다 하나씩 자동으로 생기고 레벨과 같이 사라지니, 붙이고 떼는 코드가 따로 필요 없다.
//   키는 입력 자산을 새로 만들지 않고 매 프레임 직접 읽는다(Common/PGKeyPolling.h 와 같은 방식).
//
// 일시 정지 메뉴는 게임 맵(AGameModePG)에서만 연다. 타이틀·로비·결과 화면은 자기 버튼이 있다.
// 에디터 PIE 에서는 Esc 가 "PIE 끝내기" 로 먼저 잡혀서 게임까지 안 온다 → F10 도 같이 받는다.
//   (Esc 로 열고 싶으면: 에디터 환경설정 → 키보드 단축키 → "Stop" 검색 → Esc 를 다른 키로 바꾸면 된다.)
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGPauseMenuSubsystem.generated.h"

class UPGPauseMenuWidget;

UCLASS()
class PROJECTPG_API UPGPauseMenuSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGPauseMenuSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	bool IsMenuOpen() const { return Menu != nullptr; }
	void OpenMenu();
	// 계속하기: 메뉴를 닫고 게임을 다시 돌린다(커서 숨김 + 게임 입력).
	void CloseMenu();
	// "로비로 돌아가기" 확인 뒤: 이번 판을 "중단" 으로 끝내고 로비로 간다.
	void LeaveToLobby();
	// "게임 종료" 확인 뒤.
	void QuitGame();

private:
	// 팀 인벤토리 같은 창이 떠 있으면 ESC 는 그 창의 몫이다(겹쳐 열지 않는다).
	bool IsTeamWindowOpen() const;

	UPROPERTY(Transient)
	TObjectPtr<UPGPauseMenuWidget> Menu;

	bool bIsGameMap = false;
	bool bEscWasDown = false;
	bool bF10WasDown = false;
	bool bPausedByUs = false;
};
