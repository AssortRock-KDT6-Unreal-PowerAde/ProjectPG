#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Common/GameData.h"
#include "PlayerController_InGame.generated.h"

class UInventoryComponent;

UCLASS()
class PROJECTPG_API APlayerController_InGame : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	void ToggleInventory();
	void OnRotateKey();
	void OpenCharacterWidgetInGame();
	void InteractPressed();

	UFUNCTION(Server, Reliable)
	void Server_InitializeInventory(const FInventorySnapshot& Snapshot);

	UFUNCTION(Client, Reliable)
	void Client_InitialInventoryResult(bool bSuccess, const FString& Reason);

	UFUNCTION(Server, Reliable)
	void Server_MoveInventoryItem(UInventoryComponent* Source, UInventoryComponent* Target, FGuid ItemGuid, FGuid SourceGuid, FGuid TargetGuid, FIntPoint Position, bool bRotated);

	UFUNCTION(Client, Reliable)
	void Client_InventoryRequestResult(bool bSuccess);

	// Server->Client RPC to forward inventory/websocket messages received by dedicated server
	UFUNCTION(Client, Reliable)
	void Client_ReceiveInventoryJson(const FString& MessageType, const FString& PayloadJson);

private:
	FTimerHandle InitialInventoryTimer;
	bool bInitialInventorySubmitted = false;
	bool bInitialInventoryRequested = false;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	float InventoryInteractionDistance = 400.f;

	void TryInitializeInventory();
	void SubmitInitialInventory(const FInventorySnapshot& Snapshot);
	bool CanAccessInventory(const UInventoryComponent* Inventory) const;

	UFUNCTION()
	void HandleInitialInventory(const FInventoryMapWrapper& Inventory);
};
