#pragma once

// 사망 통계표 전체 흐름 그림: Source/ProjectPG/Private/GameMode/README.md

#include "Blueprint/UserWidget.h"
#include "GameMode/PGRaidStatsSubsystem.h"
#include "PGDeathStatsWidget.generated.h"

class UTextBlock;
class UButton;

// 죽은 뒤 뜨는 통계표의 C++ 부모. 모양(배치·색·글꼴·연출)은 전부 WBP_DeathStats 에서 정한다.
// C++ 은 이름이 맞는 글칸에 숫자를 넣고, [확인] 버튼을 누르면 로비로 보내는 일만 한다.
// 글칸·버튼은 없어도 된다(BindWidgetOptional) → 디자이너가 칸을 빼거나 이름을 바꿔도 안 터짐.
UCLASS(Abstract)
class PROJECTPG_API UPGDeathStatsWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 서버가 보낸 기록으로 글칸을 채운다. 다 채운 뒤 OnStatsShown 을 불러 WBP 가 연출을 붙일 수 있게 한다.
	void ShowStats(const FPGRaidStats& Stats);

protected:
	// WBP 가 채울 칸: 숫자가 다 들어간 뒤 한 번(나타나는 애니메이션·소리 등).
	UFUNCTION(BlueprintImplementableEvent, Category="DeathStats")
	void OnStatsShown(const FPGRaidStats& Stats);

	// WBP 의 다른 버튼에서도 부를 수 있게 열어 둔다. 한 번만 동작한다.
	UFUNCTION(BlueprintCallable, Category="DeathStats")
	void ConfirmAndReturnToLobby();

	virtual void NativeOnInitialized() override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SurvivalTimeText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> KillsText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> ShotsFiredText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> HitsText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DamageDealtText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DamageTakenText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> KilledByText;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UButton> ConfirmButton;

private:
	bool bConfirmed = false;

	UFUNCTION()
	void HandleConfirmClicked();
};
