#include "Actors/MapBuilder/PGLoadingScreenWidget.h"

#include "Animation/WidgetAnimation.h"

void UPGLoadingScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (SplashLogo)
		SplashLogo->SetVisibility(ESlateVisibility::Collapsed);
	if (LoadingCorner)
		LoadingCorner->SetVisibility(ESlateVisibility::HitTestInvisible);
}

float UPGLoadingScreenWidget::PlayHide()
{
	if (!HideAnim)
		return 0.0f;
	PlayAnimation(HideAnim);
	return HideAnim->GetEndTime();
}
