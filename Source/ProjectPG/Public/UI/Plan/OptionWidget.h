// 옵션(설정) 화면.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "OptionWidget.generated.h"

class UButton;
class UCheckBox;
class UComboBoxString;

// 옵션 화면.
// 게임에서: 로비(타이틀) 메뉴의 "옵션" → 화면 모드·해상도·그래픽 품질·프레임 제한·수직 동기화를 바꾸고 "적용".
// 값은 언리얼 기본 설정 저장소(UGameUserSettings → Saved/Config/.../GameUserSettings.ini)에 저장돼 다음 실행에도 남는다.
// C++ 은 동작만(목록 채우기·적용·저장). 모양은 WBP_Option. 칸 이름이 같으면 붙는다.
// 누가 여나: 로비 메뉴(ULobbyMenuWidget)가 UPlanScreenSubSystem 으로 연다(형님 UI 관리자 목록은 안 건드림).
UCLASS()
class PROJECTPG_API UOptionWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;

	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> WindowModeCombo;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> ResolutionCombo;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> QualityCombo;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UComboBoxString> FrameLimitCombo;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UCheckBox> VSyncCheck;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> ApplyButton;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UButton> BackButton;

	// 목록 글자·값. WBP_Option 에서 고친다. 화면 모드 순서 = EWindowMode(전체 화면, 테두리 없는 창, 창 모드),
	// 품질 순서 = 언리얼 확장성 단계 0~4, 프레임 제한 0 = 제한 없음.
	UPROPERTY(EditAnywhere, Category = "Option")
	TArray<FText> WindowModeLabels = { INVTEXT("전체 화면"), INVTEXT("테두리 없는 창"), INVTEXT("창 모드") };
	UPROPERTY(EditAnywhere, Category = "Option")
	TArray<FText> QualityLabels = { INVTEXT("낮음"), INVTEXT("보통"), INVTEXT("높음"), INVTEXT("최고"), INVTEXT("시네마틱") };
	UPROPERTY(EditAnywhere, Category = "Option")
	TArray<int32> FrameLimits = { 30, 60, 120, 144, 0 };
	UPROPERTY(EditAnywhere, Category = "Option")
	FText NoFrameLimitLabel = INVTEXT("제한 없음");

private:
	// 지금 저장된 값으로 목록을 채우고 고른다(창을 열 때마다).
	void LoadFromSettings();

	UFUNCTION() void HandleApply();
	UFUNCTION() void HandleBack();

	// 목록 글자 ↔ 값. 글자는 화면에 보이는 그대로(한국어).
	TArray<FIntPoint> Resolutions;
};
