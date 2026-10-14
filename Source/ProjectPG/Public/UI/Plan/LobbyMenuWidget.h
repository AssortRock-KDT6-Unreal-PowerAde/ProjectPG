// 타이틀(로비) 메뉴: 형님 로비 메뉴에 기획서 동작(옵션·종료·매칭 화면·카메라)을 더한 자식.

#pragma once

#include "CoreMinimal.h"
#include "UI/LobbyWidget.h"
#include "LobbyMenuWidget.generated.h"

class UButton;
class UMatchingWidget;
class UOptionWidget;

// 로비 메뉴 자식(기획서 1.1).
// 게임에서: 로그인 뒤 타이틀 장면(마을·캐릭터) 위에 버튼 4개(캐릭터·게임 시작·옵션·종료).
//   - 캐릭터: 형님 동작 그대로(캐릭터·인벤토리 창) + 카메라가 캐릭터 앞으로 옮겨 간다.
//     창이 떠 있는 동안 메뉴 버튼은 숨기고, 창을 닫으면 메뉴 카메라로 돌아오며 버튼이 다시 보인다.
//   - 게임 시작: 형님 매칭 요청(UMatchmakingSubSystem::RequestGameStart)은 그대로 보내고,
//     형님 알림 창 대신 기획서 매칭 화면(WBP_Matching)을 띄운다.
//   - 옵션: 옵션 화면(WBP_Option). 형님 코드에서는 비어 있던 버튼.
//   - 종료: 게임을 끈다. 형님 코드에서는 비어 있던 버튼.
// 왜 자식인가: 형님 ULobbyWidget 은 그대로 두려고. 버튼은 형님 클래스 안에 숨어 있어서(private) 이름으로 찾는다.
//   "게임 시작"만은 형님 처리(알림 창 + 매칭 요청)를 떼고 우리 처리(매칭 화면 + 같은 매칭 요청)를 붙인다.
UCLASS()
class PROJECTPG_API ULobbyMenuWidget : public ULobbyWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

protected:
	// 띄울 화면 WBP. WBP_Lobby 에서 고른다.
	UPROPERTY(EditAnywhere, Category = "Plan Screens")
	TSubclassOf<UOptionWidget> OptionScreenClass;

	UPROPERTY(EditAnywhere, Category = "Plan Screens")
	TSubclassOf<UMatchingWidget> MatchingScreenClass;

	// 카메라 이름표(L_Title 의 카메라 태그)와 옮겨 가는 시간.
	UPROPERTY(EditAnywhere, Category = "Lobby Camera")
	FName MenuCameraTag = TEXT("LobbyCamera_Menu");

	UPROPERTY(EditAnywhere, Category = "Lobby Camera")
	FName CharacterCameraTag = TEXT("LobbyCamera_Character");

	UPROPERTY(EditAnywhere, Category = "Lobby Camera", meta = (ClampMin = "0"))
	float CameraBlendSeconds = 0.6f;

private:
	UFUNCTION() void HandleCharacter();
	UFUNCTION() void HandleGameStart();
	UFUNCTION() void HandleOption();
	UFUNCTION() void HandleExit();

	UButton* FindButton(const TCHAR* Name) const;

	// 캐릭터 창이 열리고 닫히는 순간을 알아채려고(형님 창 닫기 코드는 안 건드림).
	bool bCharacterWindowWasOpen = false;
	ESlateVisibility RootVisibility = ESlateVisibility::SelfHitTestInvisible;
};
