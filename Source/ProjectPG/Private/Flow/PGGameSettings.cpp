#include "Flow/PGGameSettings.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "GameModes/GameModePG.h"          // 팀 게임모드(종류만 확인한다)
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"

// 파일 고유 이름 공간(유니티 빌드에서 다른 cpp 이름과 안 겹치게).
namespace PGGameSettingsLocal
{
	// 해상도 칸에 보여 줄 "자주 쓰는 크기". 모니터가 지원하는 것만 남긴다. 목록이 길면 화살표로 넘기기 힘들다.
	const FIntPoint GameSettingsCommonResolutions[] = {
		FIntPoint(3840, 2160), FIntPoint(2560, 1440), FIntPoint(1920, 1080), FIntPoint(1600, 900), FIntPoint(1280, 720),
	};

	// 밝기 1.0 은 "엔진 기본 곡선(sRGB)" 이다. 밝기를 바꾸면 r.TonemapperGamma 로 감마 값을 직접 준다(2.2 가 보통 모니터).
	//   값이 클수록 어두운 곳이 밝아진다. 3D 화면에만 걸리고 UI 글자는 그대로라 메뉴가 뿌옇게 뜨지 않는다.
	constexpr float GameSettingsBaseGamma = 2.2f;

	// 에디터에서 PIE 로 바꾼 콘솔 값의 원래 값(PIE 가 끝나면 되돌린다). 게임 실행에서는 안 쓴다.
	struct FGameSettingsEditorBackup
	{
		bool bSaved = false;
		FString TonemapperGamma;
		FString MotionBlurQuality;
		FString MaxFps;
		float UnfocusedVolume = 0.0f;
	};
	FGameSettingsEditorBackup& GameSettingsEditorBackup()
	{
		static FGameSettingsEditorBackup Backup;
		return Backup;
	}

	void SetConsoleValue(const TCHAR* Name, const FString& Value)
	{
		// SetByGameSetting: 그래픽 품질 프리셋(SetByScalability)보다 우선이라, 품질을 바꿔도 우리 값(모션 블러 끔 등)이 덮이지 않는다.
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
			Var->Set(*Value, ECVF_SetByGameSetting);
	}

	FString GetConsoleValue(const TCHAR* Name)
	{
		const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetString() : FString();
	}

	bool NearlySameSetting(float A, float B) { return FMath::IsNearlyEqual(A, B, 0.005f); }
}

// ---- 값 묶음 ----

int32 FPGSettingsValues::GetOverallQuality() const
{
	for (int32 Group = 1; Group < QualityGroupCount; ++Group)
		if (Quality[Group] != Quality[0])
			return -1;
	return Quality[0];
}

void FPGSettingsValues::SetOverallQuality(int32 Level)
{
	// 전체 품질을 고르면 개별 칸이 전부 그 값으로 바뀐다(엔진 SetOverallScalabilityLevel 과 같은 뜻).
	for (int32& Value : Quality)
		Value = FMath::Clamp(Level, 0, 3);
}

bool FPGSettingsValues::IsSameAs(const FPGSettingsValues& Other) const
{
	using PGGameSettingsLocal::NearlySameSetting;
	for (int32 Group = 0; Group < QualityGroupCount; ++Group)
		if (Quality[Group] != Other.Quality[Group])
			return false;
	return WindowMode == Other.WindowMode && Resolution == Other.Resolution && NearlySameSetting(ResolutionScale, Other.ResolutionScale)
		&& bVSync == Other.bVSync && NearlySameSetting(Brightness, Other.Brightness) && bMotionBlur == Other.bMotionBlur
		&& LobbyFpsLimit == Other.LobbyFpsLimit && GameFpsLimit == Other.GameFpsLimit
		&& NearlySameSetting(MasterVolume, Other.MasterVolume) && NearlySameSetting(EffectsVolume, Other.EffectsVolume)
		&& NearlySameSetting(MusicVolume, Other.MusicVolume) && NearlySameSetting(UiVolume, Other.UiVolume)
		&& bMuteWhenUnfocused == Other.bMuteWhenUnfocused
		&& NearlySameSetting(MouseSensitivity, Other.MouseSensitivity) && NearlySameSetting(AimSensitivity, Other.AimSensitivity)
		&& bInvertMouseY == Other.bInvertMouseY && NearlySameSetting(FieldOfView, Other.FieldOfView) && bShowFps == Other.bShowFps;
}

// ---- 읽기 ----

UPGGameSettings* UPGGameSettings::Get()
{
	return GetMutableDefault<UPGGameSettings>();
}

float UPGGameSettings::GetMouseSensitivity() { return Get()->MouseSensitivity; }
float UPGGameSettings::GetAimSensitivity() { return Get()->AimSensitivity; }
bool UPGGameSettings::IsMouseYInverted() { return Get()->bInvertMouseY; }
float UPGGameSettings::GetFieldOfView() { return Get()->FieldOfView; }
float UPGGameSettings::GetMasterVolume() { return Get()->MasterVolume; }

FPGSettingsValues UPGGameSettings::ReadCurrent()
{
	FPGSettingsValues Values;
	if (const UGameUserSettings* Engine = UGameUserSettings::GetGameUserSettings())
	{
		Values.WindowMode = FMath::Clamp(static_cast<int32>(Engine->GetFullscreenMode()), 0, 2);
		Values.Resolution = Engine->GetScreenResolution();
		if (Values.Resolution.X <= 0 || Values.Resolution.Y <= 0)
			Values.Resolution = Engine->GetDesktopResolution();
		float Normalized = 1.0f, ScaleValue = 100.0f, MinScale = 0.0f, MaxScale = 100.0f;
		Engine->GetResolutionScaleInformationEx(Normalized, ScaleValue, MinScale, MaxScale);
		// 0 이하는 "프로젝트 기본(r.ScreenPercentage.Default)" 이라는 뜻이다. 화면에는 100% 로 보여 준다.
		Values.ResolutionScale = ScaleValue > 0.0f ? FMath::Clamp(ScaleValue, 50.0f, 100.0f) : 100.0f;
		Values.bVSync = Engine->IsVSyncEnabled();
		Values.Quality[FPGSettingsValues::AntiAliasing] = Engine->GetAntiAliasingQuality();
		Values.Quality[FPGSettingsValues::Shadow] = Engine->GetShadowQuality();
		Values.Quality[FPGSettingsValues::Texture] = Engine->GetTextureQuality();
		Values.Quality[FPGSettingsValues::Effects] = Engine->GetVisualEffectQuality();
		Values.Quality[FPGSettingsValues::PostProcess] = Engine->GetPostProcessingQuality();
		Values.Quality[FPGSettingsValues::Foliage] = Engine->GetFoliageQuality();
		Values.Quality[FPGSettingsValues::ViewDistance] = Engine->GetViewDistanceQuality();
		Values.Quality[FPGSettingsValues::Shading] = Engine->GetShadingQuality();
		// 엔진은 4(시네마틱)까지 있지만 화면은 0~3 네 칸만 보인다. 4 는 "최고" 로 보여 준다.
		for (int32& Value : Values.Quality)
			Value = FMath::Clamp(Value, 0, 3);
	}
	const UPGGameSettings* Ours = Get();
	Values.Brightness = Ours->Brightness;
	Values.bMotionBlur = Ours->bMotionBlur;
	Values.LobbyFpsLimit = Ours->LobbyFpsLimit;
	Values.GameFpsLimit = Ours->GameFpsLimit;
	Values.MasterVolume = Ours->MasterVolume;
	Values.EffectsVolume = Ours->EffectsVolume;
	Values.MusicVolume = Ours->MusicVolume;
	Values.UiVolume = Ours->UiVolume;
	Values.bMuteWhenUnfocused = Ours->bMuteWhenUnfocused;
	Values.MouseSensitivity = Ours->MouseSensitivity;
	Values.AimSensitivity = Ours->AimSensitivity;
	Values.bInvertMouseY = Ours->bInvertMouseY;
	Values.FieldOfView = Ours->FieldOfView;
	Values.bShowFps = Ours->bShowFps;
	return Values;
}

FPGSettingsValues UPGGameSettings::MakeDefaults()
{
	// 우리 값은 구조체에 적어 둔 기본값을 그대로 쓴다. 화면 모드·해상도는 엔진 기본(보통 창 전체 화면 + 모니터 해상도).
	FPGSettingsValues Values;
	Values.WindowMode = FMath::Clamp(static_cast<int32>(UGameUserSettings::GetDefaultWindowMode()), 0, 2);
	Values.Resolution = UGameUserSettings::GetDefaultResolution();
	if (Values.Resolution.X <= 0 || Values.Resolution.Y <= 0)
		if (const UGameUserSettings* Engine = UGameUserSettings::GetGameUserSettings())
			Values.Resolution = Engine->GetDesktopResolution();
	return Values;
}

TArray<FIntPoint> UPGGameSettings::GetResolutionChoices()
{
	TArray<FIntPoint> Supported;
	UKismetSystemLibrary::GetSupportedFullscreenResolutions(Supported);
	TArray<FIntPoint> Choices;
	for (const FIntPoint& Common : PGGameSettingsLocal::GameSettingsCommonResolutions)
		if (Supported.Contains(Common))
			Choices.Add(Common);
	if (Choices.Num() == 0)
		Choices = { FIntPoint(1920, 1080), FIntPoint(1600, 900), FIntPoint(1280, 720) };
	// 지금 쓰는 해상도가 목록에 없으면(예: 울트라와이드) 그것도 넣는다. 안 넣으면 설정 화면을 여는 것만으로 값이 바뀐 것처럼 보인다.
	if (const UGameUserSettings* Engine = UGameUserSettings::GetGameUserSettings())
	{
		const FIntPoint Current = Engine->GetScreenResolution();
		if (Current.X > 0 && Current.Y > 0 && !Choices.Contains(Current))
			Choices.Add(Current);
	}
	Choices.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X * A.Y > B.X * B.Y; });
	return Choices;
}

bool UPGGameSettings::IsGameMap(const UWorld* World)
{
	const AGameModeBase* GameMode = World ? World->GetAuthGameMode() : nullptr;
	return GameMode && GameMode->IsA<AGameModePG>();
}

// ---- 적용 ----

bool UPGGameSettings::ApplyAndSave(const FPGSettingsValues& Values, const UObject* WorldContext)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	bool bSkippedDisplay = false;

	// 1) 엔진 값.
	if (UGameUserSettings* Engine = UGameUserSettings::GetGameUserSettings())
	{
		Engine->SetVSyncEnabled(Values.bVSync);
		Engine->SetResolutionScaleValueEx(FMath::Clamp(Values.ResolutionScale, 50.0f, 100.0f));
		Engine->SetAntiAliasingQuality(Values.Quality[FPGSettingsValues::AntiAliasing]);
		Engine->SetShadowQuality(Values.Quality[FPGSettingsValues::Shadow]);
		Engine->SetTextureQuality(Values.Quality[FPGSettingsValues::Texture]);
		Engine->SetVisualEffectQuality(Values.Quality[FPGSettingsValues::Effects]);
		Engine->SetPostProcessingQuality(Values.Quality[FPGSettingsValues::PostProcess]);
		Engine->SetFoliageQuality(Values.Quality[FPGSettingsValues::Foliage]);
		Engine->SetViewDistanceQuality(Values.Quality[FPGSettingsValues::ViewDistance]);
		Engine->SetShadingQuality(Values.Quality[FPGSettingsValues::Shading]);
		// 엔진에는 FPS 제한 칸이 하나뿐이다. 지금 있는 곳(게임 맵이냐 아니냐)에 맞는 값을 넣는다. 레벨이 바뀌면 ApplyRuntimeSettings 가 다시 고른다.
		Engine->SetFrameRateLimit(static_cast<float>(IsGameMap(World) ? Values.GameFpsLimit : Values.LobbyFpsLimit));

		if (GIsEditor)
		{
			// 에디터(PIE)에서 화면 모드·해상도를 바꾸면 에디터 창 자체가 전체 화면이 되거나 크기가 바뀐다. 그래서 여기서는 건너뛴다.
			//   값도 저장하지 않는다 — 에디터의 GameUserSettings.ini 에 전체 화면이 저장되면 다음 "독립 실행" 이 엉뚱하게 뜰 수 있다.
			bSkippedDisplay = Values.WindowMode != static_cast<int32>(Engine->GetFullscreenMode()) || Values.Resolution != Engine->GetScreenResolution();
			UE_LOG(LogTemp, Display, TEXT("PGSettings: editor — window mode/resolution not applied (%s)"), bSkippedDisplay ? TEXT("changed, skipped") : TEXT("unchanged"));
			Engine->ApplyNonResolutionSettings();
			Engine->SaveSettings();
		}
		else
		{
			Engine->SetFullscreenMode(static_cast<EWindowMode::Type>(FMath::Clamp(Values.WindowMode, 0, 2)));
			Engine->SetScreenResolution(Values.Resolution);
			// ApplySettings = 해상도 적용 + 나머지 적용 + ini 저장. ConfirmVideoMode 는 "이 화면 모드로 확정" — 안 부르면 엔진이 되돌릴 수 있다.
			Engine->ApplySettings(false);
			Engine->ConfirmVideoMode();
		}
	}

	// 2) 우리 값.
	UPGGameSettings* Ours = Get();
	Ours->Brightness = FMath::Clamp(Values.Brightness, 0.5f, 1.5f);
	Ours->bMotionBlur = Values.bMotionBlur;
	Ours->LobbyFpsLimit = FMath::Max(0, Values.LobbyFpsLimit);
	Ours->GameFpsLimit = FMath::Max(0, Values.GameFpsLimit);
	Ours->MasterVolume = FMath::Clamp(Values.MasterVolume, 0.0f, 1.0f);
	Ours->EffectsVolume = FMath::Clamp(Values.EffectsVolume, 0.0f, 1.0f);
	Ours->MusicVolume = FMath::Clamp(Values.MusicVolume, 0.0f, 1.0f);
	Ours->UiVolume = FMath::Clamp(Values.UiVolume, 0.0f, 1.0f);
	Ours->bMuteWhenUnfocused = Values.bMuteWhenUnfocused;
	Ours->MouseSensitivity = FMath::Clamp(Values.MouseSensitivity, 0.2f, 3.0f);
	Ours->AimSensitivity = FMath::Clamp(Values.AimSensitivity, 0.2f, 3.0f);
	Ours->bInvertMouseY = Values.bInvertMouseY;
	Ours->FieldOfView = FMath::Clamp(Values.FieldOfView, 70.0f, 110.0f);
	Ours->bShowFps = Values.bShowFps;
	// CDO 의 Config 변수들을 GameUserSettings.ini 의 우리 칸에 쓴다.
	Ours->SaveConfig();

	// 3) 엔진이 기억하지 않는 값은 지금 바로 건다(레벨이 바뀌면 ApplyRuntimeSettings 가 또 건다).
	ApplyRuntimeSettings(World);

	UE_LOG(LogTemp, Display, TEXT("PGSettings: applied — quality %d, vsync %s, fps lobby %d / game %d, brightness %.2f, volume %.0f%%, mouse %.2f, fov %.0f"),
		Values.GetOverallQuality(), Values.bVSync ? TEXT("on") : TEXT("off"), Values.LobbyFpsLimit, Values.GameFpsLimit,
		Values.Brightness, Values.MasterVolume * 100.0f, Values.MouseSensitivity, Values.FieldOfView);
	return bSkippedDisplay;
}

void UPGGameSettings::ApplyRuntimeSettings(UWorld* World)
{
	const UPGGameSettings* Ours = Get();
	PGGameSettingsLocal::FGameSettingsEditorBackup& Backup = PGGameSettingsLocal::GameSettingsEditorBackup();
	if (GIsEditor && !Backup.bSaved)
	{
		// 에디터에서 처음 걸기 전에 원래 값을 적어 둔다. PIE 가 끝나면 RestoreEditorOverrides 가 되돌린다.
		Backup.bSaved = true;
		Backup.TonemapperGamma = PGGameSettingsLocal::GetConsoleValue(TEXT("r.TonemapperGamma"));
		Backup.MotionBlurQuality = PGGameSettingsLocal::GetConsoleValue(TEXT("r.MotionBlurQuality"));
		Backup.MaxFps = PGGameSettingsLocal::GetConsoleValue(TEXT("t.MaxFPS"));
		Backup.UnfocusedVolume = FApp::GetUnfocusedVolumeMultiplier();
	}

	// 밝기: 1.0 이면 0(엔진 기본 곡선), 아니면 감마 = 2.2 × 밝기.
	const float Gamma = FMath::IsNearlyEqual(Ours->Brightness, 1.0f, 0.005f) ? 0.0f : PGGameSettingsLocal::GameSettingsBaseGamma * Ours->Brightness;
	PGGameSettingsLocal::SetConsoleValue(TEXT("r.TonemapperGamma"), FString::SanitizeFloat(Gamma));

	// 모션 블러: 끔 = 0. 켬 = 후처리 품질이 "최고" 면 4, 아니면 3(엔진 BaseScalability.ini 의 값과 같다 — 낮음 품질의 0 은 켬으로 올린다).
	int32 BlurQuality = 0;
	if (Ours->bMotionBlur)
	{
		const UGameUserSettings* Engine = UGameUserSettings::GetGameUserSettings();
		BlurQuality = Engine && Engine->GetPostProcessingQuality() >= 3 ? 4 : 3;
	}
	PGGameSettingsLocal::SetConsoleValue(TEXT("r.MotionBlurQuality"), FString::FromInt(BlurQuality));

	// FPS 제한: 로비·타이틀·결과 화면은 낮게(보통 60 — 메뉴에서 그래픽 카드를 쉬게), 게임 맵은 따로.
	const int32 Limit = IsGameMap(World) ? Ours->GameFpsLimit : Ours->LobbyFpsLimit;
	if (UGameUserSettings* Engine = UGameUserSettings::GetGameUserSettings())
		Engine->SetFrameRateLimit(static_cast<float>(Limit)); // ini 에는 적용 버튼 때만 저장된다. 여기선 값만 맞춰 둔다
	// 지금 바로 걸리게 콘솔 변수(t.MaxFPS)도 맞춘다. UGameUserSettings::SetFrameRateLimitCVar 는 protected 라 부를 수 없다(빌드 오류 C2248).
	PGGameSettingsLocal::SetConsoleValue(TEXT("t.MaxFPS"), FString::FromInt(Limit));

	// 전체 볼륨: 오디오 장치의 "주 볼륨" 에 곱한다. 사운드 분류(SoundClass)가 없어도 모든 소리에 걸린다.
	//   효과음·음악·UI 는 분류가 있어야 나눠 줄일 수 있는데, 프로젝트에 사운드 분류 에셋이 아직 없다 → 값만 저장한다.
	if (World)
	{
		FAudioDeviceHandle AudioDevice = World->GetAudioDevice();
		if (AudioDevice.IsValid())
			AudioDevice->SetTransientPrimaryVolume(Ours->MasterVolume);
	}

	// 창이 뒤로 가면 소리 끄기: 엔진이 창 초점을 잃을 때 이 배율을 볼륨에 곱한다(Windows 기준).
	FApp::SetUnfocusedVolumeMultiplier(Ours->bMuteWhenUnfocused ? 0.0f : 1.0f);

#if !UE_BUILD_SHIPPING
	// FPS 표시: 엔진의 "stat fps"(오른쪽 위 초록 숫자). 켜고 끄는 명령이라 지금 상태와 다를 때만 보낸다. 출시 빌드에는 이 명령이 없다.
	if (World && GEngine)
	{
		if (UGameViewportClient* Viewport = World->GetGameViewport())
			if (Viewport->IsStatEnabled(TEXT("FPS")) != Ours->bShowFps)
				GEngine->Exec(World, TEXT("stat fps"));
	}
#endif
}

void UPGGameSettings::RestoreEditorOverrides()
{
	PGGameSettingsLocal::FGameSettingsEditorBackup& Backup = PGGameSettingsLocal::GameSettingsEditorBackup();
	if (!GIsEditor || !Backup.bSaved)
		return;
	Backup.bSaved = false;
	PGGameSettingsLocal::SetConsoleValue(TEXT("r.TonemapperGamma"), Backup.TonemapperGamma);
	PGGameSettingsLocal::SetConsoleValue(TEXT("r.MotionBlurQuality"), Backup.MotionBlurQuality);
	PGGameSettingsLocal::SetConsoleValue(TEXT("t.MaxFPS"), Backup.MaxFps);
	FApp::SetUnfocusedVolumeMultiplier(Backup.UnfocusedVolume);
	// PIE 가 에디터와 같은 오디오 장치를 쓰면 줄인 볼륨이 에디터에 남는다. 에디터 쪽 장치는 1(원래 값)로 되돌린다.
	if (GEngine)
	{
		FAudioDeviceHandle MainDevice = GEngine->GetMainAudioDevice();
		if (MainDevice.IsValid())
			MainDevice->SetTransientPrimaryVolume(1.0f);
	}
	UE_LOG(LogTemp, Display, TEXT("PGSettings: editor console values restored after PIE"));
}
