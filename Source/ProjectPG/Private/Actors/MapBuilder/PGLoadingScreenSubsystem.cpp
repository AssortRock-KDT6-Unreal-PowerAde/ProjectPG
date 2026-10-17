#include "Actors/MapBuilder/PGLoadingScreenSubsystem.h"

#include "Actors/MapBuilder/PGLoadingScreenWidget.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"

UPGLoadingScreenSubsystem* UPGLoadingScreenSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPGLoadingScreenSubsystem>() : nullptr;
}

void UPGLoadingScreenSubsystem::Deinitialize()
{
	StopTicker();
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	RemoveWidget();
	Super::Deinitialize();
}

void UPGLoadingScreenSubsystem::ShowLoading(UWorld* World, TSoftClassPtr<UUserWidget> WidgetClass, const FText& Message)
{
	if (!IsValid(World) || bLoading)
		return;
	if (SeatedWorld.Get() == World && MapReadyWorld.Get() == World)
	{
		UE_LOG(LogTemp, Display, TEXT("PGLoading: not shown - already seated and map built in this level"));
		return;
	}
	UGameInstance* GameInstance = GetGameInstance();
	UGameViewportClient* Viewport = GameInstance ? GameInstance->GetGameViewportClient() : nullptr;
	if (!Viewport)
		return; // 화면이 없다(전용 서버 등)

	// 모양(WBP)은 DA_MapAssets 칸에서. 비었거나 부모가 다르면 창 없이 시작한다(무적은 서버가 따로 걸어서 그대로 된다).
	UClass* Class = WidgetClass.LoadSynchronous();
	if (!Class || !Class->IsChildOf(UPGLoadingScreenWidget::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("PGLoading: DA_MapAssets 'Loading Screen Class' is empty or not a PGLoadingScreenWidget (%s) - no loading screen"),
			*WidgetClass.ToString());
		return;
	}
	Widget = CreateWidget<UPGLoadingScreenWidget>(GameInstance, Class);
	if (!Widget)
		return;
	SlateRoot = Widget->TakeWidget();
	Viewport->AddViewportWidgetContent(SlateRoot.ToSharedRef(), 1000); // 게임 화면·다른 UI 보다 위
	Widget->SetRenderOpacity(1.0f);
	Widget->SetLoadingText(Message);

	bLoading = true;
	bHiding = false;
	ShownWorld = World;
	LoadingShownAt = FPlatformTime::Seconds();
	if (!WorldCleanupHandle.IsValid())
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UPGLoadingScreenSubsystem::HandleWorldCleanup);
	if (!TickHandle.IsValid())
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPGLoadingScreenSubsystem::TickLoading));
	UE_LOG(LogTemp, Display, TEXT("PGLoading: shown (%s)"), *Class->GetName());
}

void UPGLoadingScreenSubsystem::NotifySeated(UWorld* World)
{
	SeatedWorld = World;
	UE_LOG(LogTemp, Display, TEXT("PGLoading: seated by server"));
	HideIfReady();
}

void UPGLoadingScreenSubsystem::NotifyMapReady(UWorld* World)
{
	MapReadyWorld = World;
	UE_LOG(LogTemp, Display, TEXT("PGLoading: local map built"));
	HideIfReady();
}

void UPGLoadingScreenSubsystem::HideIfReady()
{
	UWorld* World = ShownWorld.Get();
	if (bLoading && World && SeatedWorld.Get() == World && MapReadyWorld.Get() == World)
		HideLoading(TEXT("seated and map built"));
}

void UPGLoadingScreenSubsystem::HideLoading(const TCHAR* Why)
{
	if (!bLoading || bHiding)
		return;
	bHiding = true;
	HideStartedAt = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Display, TEXT("PGLoading: hiding after %.1fs - %s"), HideStartedAt - LoadingShownAt, Why);
}

void UPGLoadingScreenSubsystem::RemoveWidget()
{
	if (SlateRoot.IsValid())
		if (UGameInstance* GameInstance = GetGameInstance())
			if (UGameViewportClient* Viewport = GameInstance->GetGameViewportClient())
				Viewport->RemoveViewportWidgetContent(SlateRoot.ToSharedRef());
	SlateRoot.Reset();
	Widget = nullptr;
	bLoading = false;
	bHiding = false;
	ShownWorld.Reset();
}

void UPGLoadingScreenSubsystem::StopTicker()
{
	if (TickHandle.IsValid())
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	TickHandle.Reset();
}

bool UPGLoadingScreenSubsystem::TickLoading(float DeltaTime)
{
	if (!bLoading || !Widget)
	{
		RemoveWidget();
		TickHandle.Reset();
		return false; // 끝: 이 확인을 더 안 부른다
	}
	const double Now = FPlatformTime::Seconds();
	if (!bHiding && Now - LoadingShownAt > LoadingTimeoutSeconds)
	{
		UE_LOG(LogTemp, Error, TEXT("PGLoading: no 'seated' message from the server after %.0fs - closing the loading screen anyway"),
			LoadingTimeoutSeconds);
		HideLoading(TEXT("timeout"));
	}
	if (bHiding)
	{
		const float Alpha = 1.0f - static_cast<float>((Now - HideStartedAt) / HideFadeSeconds);
		Widget->SetRenderOpacity(FMath::Clamp(Alpha, 0.0f, 1.0f));
		if (Alpha <= 0.0f)
		{
			RemoveWidget();
			TickHandle.Reset();
			return false;
		}
	}
	return true;
}

void UPGLoadingScreenSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (SeatedWorld.Get() == World)
		SeatedWorld.Reset();
	if (MapReadyWorld.Get() == World)
		MapReadyWorld.Reset();
	if (bLoading && ShownWorld.Get() == World)
	{
		StopTicker();
		RemoveWidget();
	}
}
