// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/GameMode_InLobby.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>

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
}
