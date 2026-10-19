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
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
	RemoveWidget();
	Super::Deinitialize();
}

void UPGLoadingScreenSubsystem::ShowLoading(UWorld* World, TSoftClassPtr<UUserWidget> WidgetClass)
{
	if (!IsValid(World) || Widget)
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
	ShownWorld = World;
	bHiding = false;
	LoadingShownAt = FPlatformTime::Seconds();
	if (!WorldCleanupHandle.IsValid())
		WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UPGLoadingScreenSubsystem::HandleWorldCleanup);

	// 끝내 "섰다" 가 안 오면(서버 무적 시간 제한이 지나도) 그냥 닫는다. 영영 검은 화면보다 낫다.
	const float WaitLimit = Widget->WaitLimitSeconds;
	Schedule(WaitLimitHandle, WaitLimit, [this, WaitLimit]()
	{
		UE_LOG(LogTemp, Error, TEXT("PGLoading: still not seated/map built after %.0fs (seated=%d, map built=%d) - closing the loading screen anyway"),
			WaitLimit, SeatedWorld.IsValid() ? 1 : 0, MapReadyWorld.IsValid() ? 1 : 0);
		HideLoading(TEXT("wait limit"));
	});
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
	if (Widget && World && SeatedWorld.Get() == World && MapReadyWorld.Get() == World)
		HideLoading(TEXT("seated and map built"));
}

void UPGLoadingScreenSubsystem::HideLoading(const TCHAR* Why)
{
	if (!Widget || bHiding)
		return;
	bHiding = true;
	UE_LOG(LogTemp, Display, TEXT("PGLoading: hiding after %.1fs - %s"), FPlatformTime::Seconds() - LoadingShownAt, Why);
	const float AnimSeconds = Widget->PlayHide();
	if (AnimSeconds <= 0.0f)
		RemoveWidget();
	else
		Schedule(RemoveHandle, AnimSeconds, [this]() { RemoveWidget(); });
}

void UPGLoadingScreenSubsystem::RemoveWidget()
{
	CancelSchedules();
	if (SlateRoot.IsValid())
		if (UGameInstance* GameInstance = GetGameInstance())
			if (UGameViewportClient* Viewport = GameInstance->GetGameViewportClient())
				Viewport->RemoveViewportWidgetContent(SlateRoot.ToSharedRef());
	SlateRoot.Reset();
	Widget = nullptr;
	bHiding = false;
	ShownWorld.Reset();
}

void UPGLoadingScreenSubsystem::Schedule(FTSTicker::FDelegateHandle& Handle, float Delay, TFunction<void()> Action)
{
	if (Handle.IsValid())
		FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	TWeakObjectPtr<UPGLoadingScreenSubsystem> WeakThis(this);
	Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis, &Handle, Action = MoveTemp(Action)](float)
		{
			if (WeakThis.IsValid())
			{
				Handle.Reset();
				Action();
			}
			return false; // 한 번만
		}), Delay);
}

void UPGLoadingScreenSubsystem::CancelSchedules()
{
	for (FTSTicker::FDelegateHandle* Handle : { &WaitLimitHandle, &RemoveHandle })
	{
		if (Handle->IsValid())
			FTSTicker::GetCoreTicker().RemoveTicker(*Handle);
		Handle->Reset();
	}
}

void UPGLoadingScreenSubsystem::HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (SeatedWorld.Get() == World)
		SeatedWorld.Reset();
	if (MapReadyWorld.Get() == World)
		MapReadyWorld.Reset();
	if (Widget && ShownWorld.Get() == World)
		RemoveWidget();
}
