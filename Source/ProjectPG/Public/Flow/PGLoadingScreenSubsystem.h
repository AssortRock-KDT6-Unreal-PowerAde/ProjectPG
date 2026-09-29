// 검은 화면 두 가지: 시작 로고(타이틀 전에 한 번)와 필드 진입 로딩(출격 → 시작 지점에 설 때까지). (2026-09-22)
//
// 왜 게임 인스턴스 서브시스템인가: 레벨이 바뀌어도 살아 있어야 한다. 로비에서 출격을 누르면 로비 레벨이 내려가고 게임 맵이 올라오는데,
//   화면을 가리는 주인이 그 사이에 사라지면 안 된다. 월드 서브시스템·게임모드는 레벨과 같이 죽는다.
// 왜 Slate 인가(UMG 위젯 애셋이 아니라): 안내 글자(UPGAnnounceSubsystem)와 같은 이유 — 애셋 없이 코드만으로 뜨고 한글 글꼴이 기본으로 붙는다.
//   UI 담당이 로딩 위젯을 만들면 Show/Hide 를 부르는 쪽은 그대로 두고 그리는 부분만 바꾸면 된다.
// 주의: 엔진은 레벨을 바꿀 때 화면 위젯을 전부 뗀다(UEngine::LoadMap → RemoveAllViewportWidgets). 그래서 새 레벨이 올라오면 다시 붙인다.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PGLoadingScreenSubsystem.generated.h"

class SWidget;
class UPGLoadingScreenWidget;

UCLASS()
class PROJECTPG_API UPGLoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPGLoadingScreenSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 필드 진입 로딩을 띄운다. 곧바로 완전히 검게(페이드 없이) — 출격을 누른 순간부터 로비가 안 보여야 한다.
	void ShowLoading(const FText& Message);
	// 로딩을 걷는다(0.6초 페이드아웃). 시작 지점에 플레이어를 세운 쪽(WarZoneFootprintPreview)이 부른다.
	void HideLoading(const TCHAR* Why);
	bool IsLoadingShown() const { return bLoading; }
	// 로딩 화면이 다 걷힌 뒤 안내 줄로 한 번 띄운다(로딩이 안 떠 있으면 바로). 로딩에 가려 문구가 안 보이는 일이 없게(9/23).
	void AnnounceWhenClear(const FText& Line);

	// 시작 로고(검은 화면 가운데 PROJECT PG, 페이드인 → 머묾 → 페이드아웃). 게임을 켠 뒤 한 번만. 이미 봤으면 false.
	bool PlaySplashOnce();
	// 로고 글자가 아직 떠 있나(검은 바탕이 걷히는 마지막 0.6초는 뺀다). 타이틀 인트로가 이게 끝나기를 기다렸다 시작한다.
	bool IsSplashLogoShowing() const
	{
		return bSplashActive && FPlatformTime::Seconds() - SplashStartedAt < SplashFadeIn + SplashHold + SplashFadeOut;
	}

private:
	bool Tick(float DeltaTime);
	void EnsureWidget();
	void RemoveWidget();
	void HandlePostLoadMap(UWorld* LoadedWorld);

	// 그리는 위젯(9/23: Slate → UMG 부모 UPGLoadingScreenWidget, 모양은 WBP_PGLoadingScreen).
	// 게임 인스턴스 소유라 레벨이 바뀌어도 살아 있다. 화면에는 Slate 조각(SlateRoot)으로 붙인다 — 예전과 같은 층(1000)·같은 방식.
	UPROPERTY(Transient)
	TObjectPtr<UPGLoadingScreenWidget> Widget;
	TSharedPtr<SWidget> SlateRoot;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle PostLoadHandle;

	bool bLoading = false;
	bool bHiding = false;
	FText PendingAnnounce; // AnnounceWhenClear 가 맡긴 문구(로딩이 걷히면 띄우고 비운다)
	void FlushPendingAnnounce();
	double LoadingShownAt = 0.0;
	double HideStartedAt = 0.0;
	float LoadingTimeoutSeconds = 90.0f; // 무슨 일이 있어도 이 뒤에는 걷는다(영영 검은 화면보다 낫다)
	float HideFadeSeconds = 0.6f;

	bool bSplashPlayed = false;
	bool bSplashActive = false;
	double SplashStartedAt = 0.0;
	float SplashFadeIn = 1.2f;
	float SplashHold = 1.4f;
	float SplashFadeOut = 1.0f;
};
