#include "Actors/MapBuilder/PGLoadingScreenWidget.h"

#include "Components/TextBlock.h"

void UPGLoadingScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (SplashLogo)
		SplashLogo->SetVisibility(ESlateVisibility::Collapsed);
	if (LoadingCorner)
		LoadingCorner->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UPGLoadingScreenWidget::SetLoadingText(const FText& Text)
{
	if (LoadingText)
		LoadingText->SetText(Text);
}
