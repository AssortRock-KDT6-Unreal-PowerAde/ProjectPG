#include "UI/Plan/OptionWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Algo/Reverse.h"

void UOptionWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ApplyButton)
	{
		ApplyButton->OnClicked.RemoveDynamic(this, &UOptionWidget::HandleApply);
		ApplyButton->OnClicked.AddDynamic(this, &UOptionWidget::HandleApply);
	}
	if (BackButton)
	{
		BackButton->OnClicked.RemoveDynamic(this, &UOptionWidget::HandleBack);
		BackButton->OnClicked.AddDynamic(this, &UOptionWidget::HandleBack);
	}
	LoadFromSettings();
}

void UOptionWidget::LoadFromSettings()
{
	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings)
		return;

	if (WindowModeCombo)
	{
		WindowModeCombo->ClearOptions();
		for (const FText& Label : WindowModeLabels)
			WindowModeCombo->AddOption(Label.ToString());
		WindowModeCombo->SetSelectedIndex(static_cast<int32>(Settings->GetFullscreenMode()));
	}

	if (ResolutionCombo)
	{
		// 이 모니터가 지원하는 해상도 목록(큰 것부터).
		ResolutionCombo->ClearOptions();
		Resolutions.Reset();
		UKismetSystemLibrary::GetSupportedFullscreenResolutions(Resolutions);
		Algo::Reverse(Resolutions);
		const FIntPoint Current = Settings->GetScreenResolution();
		int32 Selected = 0;
		for (int32 Index = 0; Index < Resolutions.Num(); ++Index)
		{
			ResolutionCombo->AddOption(FString::Printf(TEXT("%d x %d"), Resolutions[Index].X, Resolutions[Index].Y));
			if (Resolutions[Index] == Current)
				Selected = Index;
		}
		ResolutionCombo->SetSelectedIndex(Selected);
	}

	if (QualityCombo)
	{
		QualityCombo->ClearOptions();
		for (const FText& Label : QualityLabels)
			QualityCombo->AddOption(Label.ToString());
		QualityCombo->SetSelectedIndex(FMath::Clamp(Settings->GetOverallScalabilityLevel(), 0, QualityLabels.Num() - 1));
	}

	if (FrameLimitCombo)
	{
		FrameLimitCombo->ClearOptions();
		int32 Selected = FrameLimits.Num() - 1;
		for (int32 Index = 0; Index < FrameLimits.Num(); ++Index)
		{
			const int32 Limit = FrameLimits[Index];
			FrameLimitCombo->AddOption(Limit > 0 ? FString::Printf(TEXT("%d"), Limit) : NoFrameLimitLabel.ToString());
			if (FMath::IsNearlyEqual(Settings->GetFrameRateLimit(), static_cast<float>(Limit)))
				Selected = Index;
		}
		FrameLimitCombo->SetSelectedIndex(Selected);
	}

	if (VSyncCheck)
		VSyncCheck->SetIsChecked(Settings->IsVSyncEnabled());
}

// 적용: 고른 값을 설정에 넣고, 화면에 바로 반영하고, 파일에 저장한다.
void UOptionWidget::HandleApply()
{
	UGameUserSettings* Settings = UGameUserSettings::GetGameUserSettings();
	if (!Settings)
		return;

	if (WindowModeCombo && WindowModeCombo->GetSelectedIndex() >= 0)
		Settings->SetFullscreenMode(static_cast<EWindowMode::Type>(WindowModeCombo->GetSelectedIndex()));
	if (ResolutionCombo && Resolutions.IsValidIndex(ResolutionCombo->GetSelectedIndex()))
		Settings->SetScreenResolution(Resolutions[ResolutionCombo->GetSelectedIndex()]);
	if (QualityCombo && QualityCombo->GetSelectedIndex() >= 0)
		Settings->SetOverallScalabilityLevel(QualityCombo->GetSelectedIndex());
	if (FrameLimitCombo && FrameLimits.IsValidIndex(FrameLimitCombo->GetSelectedIndex()))
		Settings->SetFrameRateLimit(static_cast<float>(FrameLimits[FrameLimitCombo->GetSelectedIndex()]));
	if (VSyncCheck)
		Settings->SetVSyncEnabled(VSyncCheck->IsChecked());

	Settings->ApplySettings(false);
	Settings->SaveSettings();
	UE_LOG(LogTemp, Display, TEXT("[Option] applied mode=%d res=%dx%d quality=%d fps=%.0f vsync=%d"),
		static_cast<int32>(Settings->GetFullscreenMode()), Settings->GetScreenResolution().X, Settings->GetScreenResolution().Y,
		Settings->GetOverallScalabilityLevel(), Settings->GetFrameRateLimit(), Settings->IsVSyncEnabled() ? 1 : 0);
}

// 뒤로: 창만 뗀다(로비 메뉴가 뒤에 그대로 있다). 창은 UPlanScreenSubSystem 이 들고 있다가 다음에 다시 쓴다.
void UOptionWidget::HandleBack()
{
	RemoveFromParent();
}
