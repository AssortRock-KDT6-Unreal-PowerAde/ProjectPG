#include "GameMode/GameMode_InGame.h"
#include "Server/InventorySubSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/PlayerController_InGame.h"

AGameMode_InGame::AGameMode_InGame()
{
	// Use the InGame player controller class by default
	PlayerControllerClass = APlayerController_InGame::StaticClass();
}

void AGameMode_InGame::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("GameMode Chagne : InGame"));
	// Ensure WebSocket remains connected (WebSocketSubSystem auto-connects in Initialize).
	// We want to request inventory once and then switch InventorySubSystem to local mode.
	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		// Ensure websocket usage is enabled to retrieve server data first
		InvSub->SetUseWebSocket(true);

		// Bind temporary handler to detect first inventory reception
		InvSub->OnInventoryReceived.RemoveDynamic(this, &AGameMode_InGame::OnInventoryReceivedOnce);
		InvSub->OnInventoryReceived.AddDynamic(this, &AGameMode_InGame::OnInventoryReceivedOnce);

		// Request inventory from server
		InvSub->RequestGetInventory();
	}
}

void AGameMode_InGame::OnInventoryReceivedOnce(const FInventoryMapWrapper& InventoryMap)
{
	if (bHasInitializedInventory) return;
	bHasInitializedInventory = true;

	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		// After the first successful receipt, switch to local-only inventory handling
		InvSub->SetUseWebSocket(false);

		// Unbind this handler
		InvSub->OnInventoryReceived.RemoveDynamic(this, &AGameMode_InGame::OnInventoryReceivedOnce);
	}
}
