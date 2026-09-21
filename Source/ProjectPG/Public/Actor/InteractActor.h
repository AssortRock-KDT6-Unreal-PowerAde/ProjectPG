#pragma once

#include "EngineMinimal.h"
#include "GameFramework/Actor.h"
#include "Interface/Interactable.h"
#include "InteractActor.generated.h"

class UInventoryComponent;
class UActorComponent;

UCLASS()
class PROJECTPG_API AInteractActor : public AActor,public IInteractable
{
	GENERATED_BODY()

public:
	FGuid ActorGuid;
public:
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Components")
	class UStaticMeshComponent* MeshComp;


public:
	AInteractActor();
	UFUNCTION(BlueprintCallable, Category = "Interact")
	virtual void Interact_Implementation(AActor* actor) override;

	// Inventory component that holds this actor's items
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UInventoryComponent> InventoryComp;

};
