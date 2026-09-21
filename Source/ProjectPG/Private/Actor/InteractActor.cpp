#include "Actor/InteractActor.h"
#include "Components/InventoryComponent.h"
#include "GameFramework/PlayerController.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/InventoryWindow.h"

AInteractActor::AInteractActor()
{
	PrimaryActorTick.bCanEverTick = false;
	InventoryComp = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComp"));
}

void AInteractActor::Interact(APlayerController* InteractingController)
{
	if (!InteractingController) return;

	if (UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(InteractingController))
	{
		UUserWidget* CharacterWidget = UISub->OpenUI(EUIType::Character);
		if (UInventoryWindow* Win = Cast<UInventoryWindow>(CharacterWidget))
		{
			// Bind this actor's inventory to the main inventory area
			Win->InitWidgetForActor(InventoryComp);
			// Force a refresh to ensure main inventory grid is created immediately
			Win->RefreshAllGrids();
			// Ensure the pocket/backpack widgets are not auto-created
		}
	}
}
