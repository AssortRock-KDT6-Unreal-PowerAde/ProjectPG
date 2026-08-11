// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"

#include "WebSocketSubSystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnLoginStatusChanged, bool, bIsLoggedIn, bool, bInventoryLoaded, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchStatusChanged, const FString&, StatusType, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCreateIDStatusChanged, bool, bSuccess, const FString&, UserId, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryReceived, const TArray<FItemInstance>&, Items);

UCLASS()
class PROJECTPG_API UWebSocketSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
private:
	bool bHasAttemptConnection = false;
	TSharedPtr<IWebSocket> WebSocket;
	FString CurrentUserId;
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void ConnectToLobbyServer();

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void RequestLogin(const FString& UserID);

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void RequestGameStart();

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void RequestCancleMatch();

public:
	// 💡 UI에서 바인딩할 이벤트 델리게이트
	UPROPERTY(BlueprintAssignable, Category = "Lobby WebSocket|Events")
	FOnLoginStatusChanged OnLoginStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Lobby WebSocket|Events")
	FOnMatchStatusChanged OnMatchStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Lobby WebSocket|Events")
	FOnCreateIDStatusChanged OnCreateIDStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
	FOnInventoryReceived OnInventoryReceived;
private:
	

	void OnConnected();
	void OnConnectionError(const FString& Error);
	void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnMessageReceived(const FString& MessageString);

	void HandleParsedMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);

	// 패킷 전송용 헬퍼 함수
	void SendJsonMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);
};
