// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/GameMode_InLobby.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>
#include "GameMode/PlayerController_InLobby.h"
#include "GameMode/PlayerController_InGame.h"

AGameMode_InLobby::AGameMode_InLobby()
{
	PlayerControllerClass = APlayerController_InLobby::StaticClass();
}
#include <Server/InventorySubSystem.h>

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
		InvSub->RequestGetInventory();
	}
}
