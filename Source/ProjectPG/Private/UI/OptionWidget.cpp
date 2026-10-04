#include "UI/OptionWidget.h"

#include "Components/Button.h"
#include "Components/CheckBox.h"
#include "Components/ComboBoxString.h"
#include "Core/UIManagerSubSystem.h"
#include "GameFramework/GameUserSettings.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Algo/Reverse.h"

namespace PGOption
{
	// 목록 순서 = 값. 화면 모드는 EWindowMode 순서(전체화면, 창 전체화면, 창).
	const TCHAR* WindowModes[] = { TEXT("전체 화면"), TEXT("테두리 없는 창"), TEXT("창 모드") };
	// 그래픽 품질 0~4 (언리얼 확장성 단계).
	const TCHAR* Qualities[] = { TEXT("낮음"), TEXT("보통"), TEXT("높음"), TEXT("최고"), TEXT("시네마틱") };
	// 프레임 제한. 0 = 제한 없음.
	const int32 FrameLimits[] = { 30, 60, 120, 144, 0 };
}

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
		for (const TCHAR* Label : PGOption::WindowModes)
			WindowModeCombo->AddOption(Label);
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
		for (const TCHAR* Label : PGOption::Qualities)
			QualityCombo->AddOption(Label);
		QualityCombo->SetSelectedIndex(FMath::Clamp(Settings->GetOverallScalabilityLevel(), 0, 4));
	}

	if (FrameLimitCombo)
	{
		FrameLimitCombo->ClearOptions();
		int32 Selected = UE_ARRAY_COUNT(PGOption::FrameLimits) - 1;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(PGOption::FrameLimits); ++Index)
		{
			const int32 Limit = PGOption::FrameLimits[Index];
			FrameLimitCombo->AddOption(Limit > 0 ? FString::Printf(TEXT("%d"), Limit) : FString(TEXT("제한 없음")));
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
	if (FrameLimitCombo && FrameLimitCombo->GetSelectedIndex() >= 0)
		Settings->SetFrameRateLimit(static_cast<float>(PGOption::FrameLimits[FrameLimitCombo->GetSelectedIndex()]));
	if (VSyncCheck)
		Settings->SetVSyncEnabled(VSyncCheck->IsChecked());

	Settings->ApplySettings(false);
	Settings->SaveSettings();
	UE_LOG(LogTemp, Display, TEXT("[Option] applied mode=%d res=%dx%d quality=%d fps=%.0f vsync=%d"),
		static_cast<int32>(Settings->GetFullscreenMode()), Settings->GetScreenResolution().X, Settings->GetScreenResolution().Y,
		Settings->GetOverallScalabilityLevel(), Settings->GetFrameRateLimit(), Settings->IsVSyncEnabled() ? 1 : 0);
}

void UOptionWidget::HandleBack()
{
	if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(GetWorld()))
		UI->CloseUI(EUIType::Option);
}
