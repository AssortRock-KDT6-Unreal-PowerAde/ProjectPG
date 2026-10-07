// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameMode_InLobby.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API AGameMode_InLobby : public AGameModeBase
{
	GENERATED_BODY()
public:
	AGameMode_InLobby();
	void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage);

private:
	// WebSocket이 실제로 연결될 때까지 폴링한 뒤 인벤토리를 요청한다.
	// (WebSocket::Connect()는 비동기이므로 InitGame 시점에는 아직 연결되어있지 않을 수 있다)
	FTimerHandle InventoryRequestRetryHandle;
	void TryRequestInventoryWhenConnected();
};
