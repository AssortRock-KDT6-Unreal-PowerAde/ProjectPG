#include "Flow/PGLoadingScreenSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "Flow/PGFlowSettings.h"
#include "UI/PGOverlayWidgets.h"
#include "UObject/UObjectGlobals.h"

UPGLoadingScreenSubsystem* UPGLoadingScreenSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPGLoadingScreenSubsystem>() : nullptr;
}

void UPGLoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPGLoadingScreenSubsystem::Tick));
	PostLoadHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UPGLoadingScreenSubsystem::HandlePostLoadMap);
}

void UPGLoadingScreenSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
	RemoveWidget();
	Super::Deinitialize();
}

void UPGLoadingScreenSubsystem::EnsureWidget()
{
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!Viewport)
		return; // 화면이 없다(헤드리스·전용 서버)
	if (!Widget)
	{
		// 설정(ProjectPG Flow > Screens > Loading Screen Class)에 WBP 가 있으면 그것, 없으면 코드 기본 모양(예전 Slate 와 같다).
		Widget = CreateWidget<UPGLoadingScreenWidget>(GameInstance,
			UPGFlowSettings::ResolveWidgetClass(UPGFlowSettings::Get().LoadingScreenClass, UPGLoadingScreenWidget::StaticClass()));
		if (!Widget)
			return;
		SlateRoot = Widget->TakeWidget();
		UE_LOG(LogTemp, Display, TEXT("PGLoading: widget %s"), *Widget->GetClass()->GetName());
	}
	// 레벨이 바뀌면 엔진이 떼어 낸다 — 그때마다 다시 붙인다. 이미 붙어 있으면 한 번 떼고 붙여 두 번 붙지 않게.
	Viewport->RemoveViewportWidgetContent(SlateRoot.ToSharedRef());
	Viewport->AddViewportWidgetContent(SlateRoot.ToSharedRef(), 1000); // 게임 화면·안내 글자(50)보다 위
}

void UPGLoadingScreenSubsystem::RemoveWidget()
{
	if (!SlateRoot.IsValid())
		return;
	if (UGameInstance* GameInstance = GetGameInstance())
		if (UGameViewportClient* Viewport = GameInstance->GetGameViewportClient())
			Viewport->RemoveViewportWidgetContent(SlateRoot.ToSharedRef());
	SlateRoot.Reset();
	Widget = nullptr;
}

void UPGLoadingScreenSubsystem::ShowLoading(const FText& Message)
{
	bLoading = true;
	bHiding = false;
	LoadingShownAt = FPlatformTime::Seconds();
	EnsureWidget();
	if (!Widget)
		return;
	Widget->SetRenderOpacity(1.0f);
	Widget->SetLoadingText(Message);
	Widget->ShowLoadingCorner(true);
	Widget->ShowSplashLogo(false);
	UE_LOG(LogTemp, Display, TEXT("PGLoading: shown — %s"), *Message.ToString());
}

void UPGLoadingScreenSubsystem::HideLoading(const TCHAR* Why)
{
	if (!bLoading || bHiding)
		return;
	bHiding = true;
	HideStartedAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("PGLoading: hiding after %.1fs — %s"), HideStartedAt - LoadingShownAt, Why);
}

void UPGLoadingScreenSubsystem::AnnounceWhenClear(const FText& Line)
{
	if (Line.IsEmpty())
		return;
	PendingAnnounce = Line;
	// PIE 에서 게임 맵으로 바로 시작하면 로딩이 안 떠 있다 — 그때는 바로 띄운다.
	if (!bLoading)
		FlushPendingAnnounce();
}

void UPGLoadingScreenSubsystem::FlushPendingAnnounce()
{
	if (PendingAnnounce.IsEmpty())
		return;
	UGameInstance* GameInstance = GetGameInstance();
	UWorld* World = GameInstance ? GameInstance->GetWorld() : nullptr;
	if (UPGAnnounceSubsystem* Announcer = World ? UPGAnnounceSubsystem::Get(World) : nullptr)
		Announcer->Announce({ PendingAnnounce });
	UE_LOG(LogTemp, Display, TEXT("PGLoading: start message \"%s\" %s"), *PendingAnnounce.ToString(), World ? TEXT("sent") : TEXT("dropped (no world)"));
	PendingAnnounce = FText::GetEmpty();
}

bool UPGLoadingScreenSubsystem::PlaySplashOnce()
{
	if (bSplashPlayed)
		return false;
	bSplashPlayed = true;
	EnsureWidget();
	if (!Widget)
		return false;
	bSplashActive = true;
	SplashStartedAt = FPlatformTime::Seconds();
	Widget->SetRenderOpacity(1.0f);
	Widget->ShowSplashLogo(true);
	Widget->SetSplashOpacity(0.0f);
	Widget->ShowLoadingCorner(false);
	UE_LOG(LogTemp, Display, TEXT("PGLoading: splash logo"));
	return true;
}

void UPGLoadingScreenSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	// 로딩·로고를 보여 주는 중이면 새 레벨 위에 다시 붙인다(엔진이 레벨을 바꾸며 뗐다).
	if (bLoading || bSplashActive)
		EnsureWidget();
}

bool UPGLoadingScreenSubsystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds();
	if (bSplashActive && Widget)
	{
		// 로고: 검은 바탕은 그대로, 글자만 부드럽게 나타났다 사라진다. 끝나면 바탕도 걷어 타이틀을 보여 준다.
		const float Age = static_cast<float>(Now - SplashStartedAt);
		float LogoAlpha = 1.0f;
		if (Age < SplashFadeIn)
			LogoAlpha = Age / SplashFadeIn;
		else if (Age > SplashFadeIn + SplashHold)
			LogoAlpha = 1.0f - (Age - SplashFadeIn - SplashHold) / SplashFadeOut;
		Widget->SetSplashOpacity(FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(LogoAlpha, 0.0f, 1.0f)));
		const float Total = SplashFadeIn + SplashHold + SplashFadeOut;
		if (Age >= Total)
		{
			// 바탕을 0.6초에 걷는다. 로딩이 같이 떠 있으면 바탕은 로딩 몫이라 남긴다.
			const float Lift = (Age - Total) / 0.6f;
			if (!bLoading)
				Widget->SetRenderOpacity(1.0f - FMath::Clamp(Lift, 0.0f, 1.0f));
			if (Lift >= 1.0f)
			{
				bSplashActive = false;
				Widget->ShowSplashLogo(false);
				if (!bLoading)
					RemoveWidget();
			}
		}
	}
	if (bLoading && Widget)
	{
		if (!bHiding && Now - LoadingShownAt > LoadingTimeoutSeconds)
			HideLoading(TEXT("timeout"));
		if (bHiding)
		{
			const float Alpha = 1.0f - static_cast<float>((Now - HideStartedAt) / HideFadeSeconds);
			Widget->SetRenderOpacity(FMath::Clamp(Alpha, 0.0f, 1.0f));
			if (Alpha <= 0.0f)
			{
				bLoading = false;
				bHiding = false;
				if (!bSplashActive)
					RemoveWidget();
				FlushPendingAnnounce();
			}
		}
	}
	return true;
}
