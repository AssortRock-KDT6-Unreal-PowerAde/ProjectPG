#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "InteractActor.generated.h"

class UInventoryComponent;
class UActorComponent;

UCLASS()
class PROJECTPG_API AInteractActor : public AActor
{
	GENERATED_BODY()

public:
	AInteractActor();

	// Inventory component that holds this actor's items
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UInventoryComponent> InventoryComp;

	// Called by player when interacting with this actor
	UFUNCTION(BlueprintCallable, Category = "Interact")
	void Interact(APlayerController* InteractingController);
};
