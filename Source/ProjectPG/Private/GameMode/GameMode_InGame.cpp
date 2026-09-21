#include "GameMode/GameMode_InGame.h"
#include "Server/InventorySubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include "Kismet/GameplayStatics.h"
#include "GameMode/PlayerController_InGame.h"
#include "Server/AuthSubSystem.h"
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
		InvSub->SetForceLocalMoves(false);

		// Bind temporary handler to detect first inventory reception
		InvSub->OnInventoryReceived.RemoveDynamic(this, &AGameMode_InGame::OnInventoryReceivedOnce);
		InvSub->OnInventoryReceived.AddDynamic(this, &AGameMode_InGame::OnInventoryReceivedOnce);
	}

	// WebSocket::Connect()는 비동기이므로, InitGame 시점에는 아직 연결이 완료되지 않았을 수 있다.
	// 실제로 연결된 뒤에 GET_INVENTORY를 요청하도록 폴링한다.
	TryRequestInventoryWhenConnected();
}

void AGameMode_InGame::TryRequestInventoryWhenConnected()
{
	UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(this);
	UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld());

	if (!WS || !InvSub)
	{
		GetWorldTimerManager().SetTimer(InventoryRequestRetryHandle, this, &AGameMode_InGame::TryRequestInventoryWhenConnected, 0.2f, false);
		return;
	}

	if (!WS->IsConnected())
	{
		UE_LOG(LogTemp, Log, TEXT("[GameMode_InGame] WebSocket not connected yet, retrying inventory request..."));
		GetWorldTimerManager().SetTimer(InventoryRequestRetryHandle, this, &AGameMode_InGame::TryRequestInventoryWhenConnected, 0.2f, false);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GameMode_InGame] WebSocket connected, requesting inventory."));
	InvSub->RequestGetInventory();
}

void AGameMode_InGame::OnInventoryReceivedOnce(const FInventoryMapWrapper& InventoryMap)
{
	if (bHasInitializedInventory) return;
	bHasInitializedInventory = true;

	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		// After the first successful receipt, switch to local-only inventory handling
		InvSub->SetUseWebSocket(false);
		// Ensure subsequent moves/equip are handled locally in InGame
		InvSub->SetForceLocalMoves(true);

		// Unbind this handler
		InvSub->OnInventoryReceived.RemoveDynamic(this, &AGameMode_InGame::OnInventoryReceivedOnce);
	}
}
