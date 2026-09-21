// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/GameMode_InLobby.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>
#include "GameMode/PlayerController_InLobby.h"
#include "GameMode/PlayerController_InGame.h"
#include <Server/InventorySubSystem.h>
#include <Server/WebSocketSubSystem.h>

AGameMode_InLobby::AGameMode_InLobby()
{
	PlayerControllerClass = APlayerController_InLobby::StaticClass();
}

void AGameMode_InLobby::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	FTimerHandle TimerHandle;
	GetWorldTimerManager().SetTimer(TimerHandle, [this]()
		{
			ULobbyUIFlowController* FlowController = ULobbyUIFlowController::Get(this);
			if (FlowController)
			{
				FlowController->BeginSetting();
			}
		}, 0.01f, false);

	// Lobby 진입 시: InGame에서 로컬 변경이 있었다면 먼저 서버로 동기화하고,
	// 이후 WebSocket 사용을 활성화해 최신 상태를 요청합니다.
	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		// 서버로 저장이 필요한 로컬 변경이 있는 경우 전송 시도
		if (InvSub->HasLocalChanges())
		{
			InvSub->ForceSaveToServer();
		}

		InvSub->SetUseWebSocket(true);
		InvSub->SetForceLocalMoves(false);
	}

	// WebSocket::Connect()는 비동기이므로, InitGame 시점에는 아직 연결이 완료되지 않았을 수 있다.
	// 실제로 연결된 뒤에 GET_INVENTORY를 요청하도록 폴링한다.
	TryRequestInventoryWhenConnected();
}

void AGameMode_InLobby::TryRequestInventoryWhenConnected()
{
	UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(this);
	UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld());

	if (!WS || !InvSub)
	{
		GetWorldTimerManager().SetTimer(InventoryRequestRetryHandle, this, &AGameMode_InLobby::TryRequestInventoryWhenConnected, 0.2f, false);
		return;
	}

	if (!WS->IsConnected())
	{
		UE_LOG(LogTemp, Log, TEXT("[GameMode_InLobby] WebSocket not connected yet, retrying inventory request..."));
		GetWorldTimerManager().SetTimer(InventoryRequestRetryHandle, this, &AGameMode_InLobby::TryRequestInventoryWhenConnected, 0.2f, false);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GameMode_InLobby] WebSocket connected, requesting inventory."));
	InvSub->RequestGetInventory();
}
