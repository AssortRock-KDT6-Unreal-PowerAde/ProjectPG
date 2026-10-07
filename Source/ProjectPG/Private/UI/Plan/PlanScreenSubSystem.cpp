#include "UI/Plan/PlanScreenSubSystem.h"

#include "Blueprint/UserWidget.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

UPlanScreenSubSystem* UPlanScreenSubSystem::Get(const UObject* WorldContext)
{
	UGameInstance* GameInstance = WorldContext ? UGameplayStatics::GetGameInstance(WorldContext) : nullptr;
	return GameInstance ? GameInstance->GetSubsystem<UPlanScreenSubSystem>() : nullptr;
}

UUserWidget* UPlanScreenSubSystem::OpenScreen(TSubclassOf<UUserWidget> ScreenClass, int32 ZOrder)
{
	if (!ScreenClass)
		return nullptr;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
		return nullptr;

	TObjectPtr<UUserWidget>* Found = Screens.Find(ScreenClass.Get());
	UUserWidget* Screen = Found ? Found->Get() : nullptr;
	// 지난 맵에서 만든 창은 그 맵의 플레이어 것이라 쓰지 않는다.
	if (!IsValid(Screen) || Screen->GetOwningPlayer() != PC)
	{
		Screen = CreateWidget<UUserWidget>(PC, ScreenClass);
		if (!Screen)
			return nullptr;
		Screens.Add(ScreenClass.Get(), Screen);
	}
	if (!Screen->IsInViewport())
		Screen->AddToViewport(ZOrder);
	return Screen;
}

void UPlanScreenSubSystem::CloseScreen(TSubclassOf<UUserWidget> ScreenClass)
{
	if (UUserWidget* Screen = FindScreen(ScreenClass))
		Screen->RemoveFromParent();
}

UUserWidget* UPlanScreenSubSystem::FindScreen(TSubclassOf<UUserWidget> ScreenClass) const
{
	const TObjectPtr<UUserWidget>* Found = ScreenClass ? Screens.Find(ScreenClass.Get()) : nullptr;
	return Found && IsValid(Found->Get()) ? Found->Get() : nullptr;
}
