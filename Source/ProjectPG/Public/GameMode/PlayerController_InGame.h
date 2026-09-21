#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PlayerController_InGame.generated.h"

UCLASS()
class PROJECTPG_API APlayerController_InGame : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	void ToggleInventory();
	void OnRotateKey();
	void OpenCharacterWidgetInGame();
	void InteractPressed();

	// Server->Client RPC to forward inventory/websocket messages received by dedicated server
	UFUNCTION(Client, Reliable)
	void Client_ReceiveInventoryJson(const FString& MessageType, const FString& PayloadJson);
};
