// 사망 통계표 전체 흐름 그림: Source/ProjectPG/Private/GameMode/README.md

#include "UI/PGDeathStatsWidget.h"
#include "GameMode/PlayerController_InGame.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

void UPGDeathStatsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (ConfirmButton) ConfirmButton->OnClicked.AddUniqueDynamic(this, &ThisClass::HandleConfirmClicked);
	// 키보드 Enter·Space 로도 확인할 수 있게 이 창이 입력을 받는다.
	SetIsFocusable(true);
}

void UPGDeathStatsWidget::ShowStats(const FPGRaidStats& Stats)
{
	auto SetText = [](UTextBlock* Block, const FString& Value)
	{
		if (Block) Block->SetText(FText::FromString(Value));
	};
	const int32 Seconds = FMath::Max(0, FMath::FloorToInt(Stats.SurvivalSeconds));
	SetText(SurvivalTimeText, FString::Printf(TEXT("%02d:%02d"), Seconds / 60, Seconds % 60));
	SetText(KillsText, FString::FromInt(Stats.Kills));
	SetText(ShotsFiredText, FString::FromInt(Stats.ShotsFired));
	SetText(HitsText, FString::FromInt(Stats.Hits));
	SetText(DamageDealtText, FString::Printf(TEXT("%.0f"), Stats.DamageDealt));
	SetText(DamageTakenText, FString::Printf(TEXT("%.0f"), Stats.DamageTaken));
	SetText(KilledByText, Stats.KilledBy.IsEmpty() ? TEXT("알 수 없음") : Stats.KilledBy);
	OnStatsShown(Stats);
}

FReply UPGDeathStatsWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Virtual_Accept)
	{
		ConfirmAndReturnToLobby();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

void UPGDeathStatsWidget::HandleConfirmClicked()
{
	ConfirmAndReturnToLobby();
}

void UPGDeathStatsWidget::ConfirmAndReturnToLobby()
{
	// 두 번 눌러도 로비 이동은 한 번만.
	if (bConfirmed) return;
	bConfirmed = true;
	if (ConfirmButton) ConfirmButton->SetIsEnabled(false);
	APlayerController_InGame* Player = Cast<APlayerController_InGame>(GetOwningPlayer());
	if (!Player)
	{
		UE_LOG(LogTemp, Error, TEXT("[DeathToLobby] 통계표의 주인이 PlayerController_InGame 이 아니라 로비로 못 보냄(%s)"), *GetNameSafe(GetOwningPlayer()));
		return;
	}
	Player->ReturnToLobbyNow();
}
