// 타이틀(로비) 메뉴: 형님 로비 메뉴에 기획서 동작(옵션·종료·매칭 화면·카메라)을 더한 자식.

#pragma once

#include "CoreMinimal.h"
#include "UI/LobbyWidget.h"
#include "LobbyMenuWidget.generated.h"

class UButton;
class UOptionWidget;

// 로비 메뉴 자식(기획서 1.1).
// 게임에서: 로그인 뒤 타이틀 장면(마을·캐릭터) 위에 버튼 4개(캐릭터·게임 시작·옵션·종료).
//   - 캐릭터·게임 시작: 형님 동작 그대로.
//   - 옵션: 옵션 화면(WBP_Option). 형님 코드에서는 비어 있던 버튼.
//   - 종료: 게임을 끈다. 형님 코드에서는 비어 있던 버튼.
// 왜 자식인가: 형님 ULobbyWidget 은 그대로 두려고. 버튼은 형님 클래스 안에 숨어 있어서(private) 이름으로 찾는다.
UCLASS()
class PROJECTPG_API ULobbyMenuWidget : public ULobbyWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

protected:
	// 띄울 화면 WBP. WBP_Lobby 에서 고른다.
	UPROPERTY(EditAnywhere, Category = "Plan Screens")
	TSubclassOf<UOptionWidget> OptionScreenClass;

private:
	UFUNCTION() void HandleOption();
	UFUNCTION() void HandleExit();

	UButton* FindButton(const TCHAR* Name) const;
};
