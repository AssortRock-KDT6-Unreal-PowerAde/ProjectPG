// 매칭 화면(기획서 1.3.1): "매칭중..." 글자, 취소 버튼, 진행 막대.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Server/SessionSubSystem.h"
#include "MatchingWidget.generated.h"

class UButton;
class UProgressBar;
class UTextBlock;

// 매칭 화면.
// C++ 은 동작만: 매칭 담당(USessionSubSystem)의 상태를 글자로, 진행률을 막대로 보여 주고, 취소 버튼을 전달한다.
// 모양(배치·색·글꼴)은 WBP_Matching 에서. 칸 이름이 같으면 붙는다(없으면 그 부분만 안 보임).
UCLASS()
class PROJECTPG_API UMatchingWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> MatchProgress;

private:
	UFUNCTION()
	void HandleStateChanged(EMatchingState State, const FText& Message);

	UFUNCTION()
	void HandleCancelClicked();

	// 실패·취소 글자를 잠깐 보여 준 뒤 창을 닫는다.
	void CloseSoon();

	FTimerHandle CloseTimer;
};
