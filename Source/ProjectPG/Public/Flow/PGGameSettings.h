// 환경설정 값의 주인. (2026-09-22, 로비·게임 속 ESC 메뉴의 환경설정 화면이 같이 쓴다)
//
// 값은 두 곳에 나눠 저장한다.
//   1) 엔진이 이미 아는 값(화면 모드·해상도·수직 동기화·그래픽 품질·렌더링 해상도) → 엔진 UGameUserSettings.
//      적용(ApplySettings)·저장(SaveSettings)·다음 실행 때 불러오기까지 엔진이 해 준다. 직접 만들 이유가 없다.
//   2) 엔진에 자리가 없는 값(마우스 감도·밝기·볼륨·로비/게임 FPS 제한 등) → 이 클래스의 Config 변수.
//      UCLASS(Config = GameUserSettings) 라서 같은 GameUserSettings.ini 의 [/Script/ProjectPG.PGGameSettings] 칸에 들어간다.
//      세이브 파일(USaveGame)을 따로 만들지 않은 이유: 설정은 "기기마다 다른 값" 이라 엔진 설정 파일 옆에 있는 게 자연스럽고,
//      SaveConfig() 한 줄로 끝난다.
//
// 캐릭터 담당에게: 마우스 감도·조준 감도·상하 반전·시야각은 여기서 "저장만" 한다. 팀 캐릭터의 입력 코드는 우리가 못 고치므로
//   UPGGameSettings::GetMouseSensitivity() 같은 static 함수(블루프린트에서도 부를 수 있다)를 읽어서 시점 회전에 곱해 주면 된다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PGGameSettings.generated.h"

class UWorld;

// 설정 화면이 "고르는 중" 인 값 한 묶음. 적용을 누르기 전까지는 이 묶음만 바뀌고 게임에는 아무 일도 없다.
//   (블루프린트에 안 보여도 되는 값 묶음이라 USTRUCT 가 아닌 보통 구조체다.)
struct PROJECTPG_API FPGSettingsValues
{
	// 개별 그래픽 품질 칸. 순서가 곧 화면의 줄 순서다.
	enum EQualityGroup : int32
	{
		AntiAliasing,
		Shadow,
		Texture,
		Effects,
		PostProcess,
		Foliage,
		ViewDistance,
		Shading,
		QualityGroupCount
	};

	// ---- 그래픽 (엔진 UGameUserSettings 에 저장) ----
	int32 WindowMode = 1;                      // 0 전체 화면 · 1 창 전체 화면 · 2 창 모드 (엔진 EWindowMode 숫자와 같다)
	FIntPoint Resolution = FIntPoint(1920, 1080);
	float ResolutionScale = 100.0f;            // 렌더링 해상도 % (화면 크기는 그대로, 3D 를 그리는 해상도만 낮춘다)
	bool bVSync = false;
	int32 Quality[QualityGroupCount] = { 3, 3, 3, 3, 3, 3, 3, 3 }; // 0 낮음 · 1 중간 · 2 높음 · 3 최고

	// ---- 그래픽 (우리 Config) ----
	float Brightness = 1.0f;                   // 0.5 ~ 1.5, 1 이 기본
	bool bMotionBlur = true;
	int32 LobbyFpsLimit = 0;                   // 0 이면 제한 없음. 기본은 제한 없음 — 설정을 안 건드리면 원래 동작 그대로(9/22)
	int32 GameFpsLimit = 0;

	// ---- 오디오 ----
	float MasterVolume = 1.0f;                 // 0 ~ 1
	float EffectsVolume = 1.0f;                // 저장만(사운드 분류가 아직 없다 — cpp 설명 참고)
	float MusicVolume = 1.0f;
	float UiVolume = 1.0f;
	bool bMuteWhenUnfocused = true;            // 창이 뒤로 가면 소리 끄기

	// ---- 컨트롤 (저장만 — 캐릭터 담당이 읽는다) ----
	float MouseSensitivity = 1.0f;             // 0.2 ~ 3.0
	float AimSensitivity = 1.0f;
	bool bInvertMouseY = false;

	// ---- 게임플레이 ----
	float FieldOfView = 90.0f;                 // 저장만(카메라 담당이 읽는다)
	bool bShowFps = false;

	// 전체 품질 칸 값. 개별 칸이 모두 같으면 그 값, 섞여 있으면 -1(사용자 지정).
	int32 GetOverallQuality() const;
	void SetOverallQuality(int32 Level);

	// 적용을 누르지 않은 바뀐 값이 있나 보려고 비교한다(실수는 조금의 차이를 같다고 본다).
	bool IsSameAs(const FPGSettingsValues& Other) const;
};

UCLASS(Config = GameUserSettings)
class PROJECTPG_API UPGGameSettings : public UObject
{
	GENERATED_BODY()

public:
	// 값은 클래스 기본 객체(CDO) 하나에만 둔다. 엔진이 시작할 때 ini 에서 채워 준다.
	static UPGGameSettings* Get();

	// ---- 다른 코드가 읽는 값(캐릭터·카메라 담당용). 블루프린트에서도 부를 수 있다. ----
	UFUNCTION(BlueprintPure, Category = "PG|Settings")
	static float GetMouseSensitivity();

	UFUNCTION(BlueprintPure, Category = "PG|Settings")
	static float GetAimSensitivity();

	UFUNCTION(BlueprintPure, Category = "PG|Settings")
	static bool IsMouseYInverted();

	UFUNCTION(BlueprintPure, Category = "PG|Settings")
	static float GetFieldOfView();

	UFUNCTION(BlueprintPure, Category = "PG|Settings")
	static float GetMasterVolume();

	// ---- 설정 화면이 쓰는 함수 ----
	// 지금 저장된 값(엔진 값 + 우리 값)을 한 묶음으로.
	static FPGSettingsValues ReadCurrent();
	// "기본값" 버튼이 쓰는 값.
	static FPGSettingsValues MakeDefaults();
	// 고른 값을 실제로 적용하고 ini 에 저장한다. 에디터(PIE)에서는 화면 모드·해상도만 건너뛴다(에디터 창이 바뀌면 안 된다).
	// 돌려주는 값: 건너뛴 게 있으면 true(화면에 안내하려고).
	static bool ApplyAndSave(const FPGSettingsValues& Values, const UObject* WorldContext);
	// 해상도 칸의 선택지: 모니터가 지원하는 것 중 자주 쓰는 크기만. 못 읽으면 1920x1080·1600x900·1280x720.
	static TArray<FIntPoint> GetResolutionChoices();

	// ---- 레벨이 열릴 때마다(UPGPauseMenuSubsystem 이 부른다) ----
	// 엔진이 레벨을 넘어도 기억하지 않는 값(볼륨·밝기·모션 블러·로비/게임 FPS 제한·FPS 표시)을 다시 건다.
	static void ApplyRuntimeSettings(UWorld* World);
	// 에디터에서 PIE 가 끝날 때: 밝기·FPS 제한 같은 콘솔 값이 에디터 화면에 남지 않게 원래 값으로 돌린다.
	static void RestoreEditorOverrides();
	// 게임 맵(맵 생성 게임모드 AGameModePG)인가. FPS 제한을 로비용·게임용 중 무엇으로 걸지 가른다.
	static bool IsGameMap(const UWorld* World);

	// ---- 저장 칸(GameUserSettings.ini) ----
	UPROPERTY(Config)
	float MouseSensitivity = 1.0f;

	UPROPERTY(Config)
	float AimSensitivity = 1.0f;

	UPROPERTY(Config)
	bool bInvertMouseY = false;

	UPROPERTY(Config)
	float FieldOfView = 90.0f;

	UPROPERTY(Config)
	float MasterVolume = 1.0f;

	UPROPERTY(Config)
	float EffectsVolume = 1.0f;

	UPROPERTY(Config)
	float MusicVolume = 1.0f;

	UPROPERTY(Config)
	float UiVolume = 1.0f;

	UPROPERTY(Config)
	bool bMuteWhenUnfocused = true;

	UPROPERTY(Config)
	float Brightness = 1.0f;

	UPROPERTY(Config)
	bool bMotionBlur = true;

	UPROPERTY(Config)
	int32 LobbyFpsLimit = 0; // 기본 제한 없음: 레벨마다 설정을 다시 걸기 때문에 60 으로 두면 안 건드린 사람도 로비가 60 에 묶였다

	UPROPERTY(Config)
	int32 GameFpsLimit = 0;

	UPROPERTY(Config)
	bool bShowFps = false;
};
