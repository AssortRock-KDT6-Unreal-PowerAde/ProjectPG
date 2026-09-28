// 게임 속 일시 정지 메뉴(ESC / F10). 계속하기 · 환경설정 · 로비로 돌아가기 · 게임 종료. (2026-09-22)
//
// 띄우고 닫는 것(게임 멈춤·마우스 커서·입력 모드)은 UPGPauseMenuSubsystem 이 한다. 이 위젯은 버튼과 확인 창만 그린다.
// "로비로 돌아가기" 와 "게임 종료" 는 되돌릴 수 없어서 한 번 더 묻는다(타르코프에서 확인 없이 눌려 전리품을 잃었다는 불만이 있었다 —
//   Docs/SettingsResearch_2026-09-22.md).
#pragma once

#include "CoreMinimal.h"
#include "UI/PGFlowWidgets.h"
#include "PGPauseMenuWidget.generated.h"

class UPGSettingsWidget;

// 확인 창이 지금 무엇을 묻고 있나.
enum class EPGPauseConfirm : uint8
{
	None,
	LeaveToLobby,
	QuitGame,
};

UCLASS()
class PROJECTPG_API UPGPauseMenuWidget : public UPGFlowScreenWidget
{
	GENERATED_BODY()

protected:
	virtual void BuildContent() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UFUNCTION()
	void HandleResume();

	UFUNCTION()
	void HandleSettings();

	UFUNCTION()
	void HandleLeave();

	UFUNCTION()
	void HandleQuit();

	UFUNCTION()
	void HandleConfirmYes();

	UFUNCTION()
	void HandleConfirmNo();

	void ShowConfirm(EPGPauseConfirm What);
	void HandleSettingsClosed();

	// ---- WBP 자리 (9/23 블루프린트 분리, WBP_PGPauseMenu) ----
	// 같은 이름으로 놓으면 연결된다. 없으면 코드 화면(BuildContent)이 같은 자리를 채운다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> ResumeButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> SettingsButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> LeaveButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> QuitButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> YesButton;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UButton> NoButton;

	// 메뉴 판(환경설정이 떠 있는 동안 숨긴다).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> MenuColumn;

	// 확인 창 전체(처음엔 숨김).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> ConfirmLayer;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ConfirmTitle;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Pause", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> ConfirmBody;

	UPROPERTY(Transient)
	TObjectPtr<UPGSettingsWidget> Settings;

	EPGPauseConfirm Confirm = EPGPauseConfirm::None;
};
