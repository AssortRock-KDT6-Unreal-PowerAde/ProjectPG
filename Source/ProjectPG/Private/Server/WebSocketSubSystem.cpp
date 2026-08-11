// Fill out your copyright notice in the Description page of Project Settings.


#include "Server/WebSocketSubSystem.h"

void UWebSocketSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
}

void UWebSocketSubSystem::Deinitialize()
{
}

void UWebSocketSubSystem::ConnectToLobbyServer()
{
}

void UWebSocketSubSystem::RequestLogin(const FString& UserID)
{
}

void UWebSocketSubSystem::RequestGameStart()
{
}

void UWebSocketSubSystem::RequestCancleMatch()
{
}

void UWebSocketSubSystem::OnConnected()
{
}

void UWebSocketSubSystem::OnConnectionError(const FString& Error)
{
}

void UWebSocketSubSystem::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
}

void UWebSocketSubSystem::OnMessageReceived(const FString& MessageString)
{
}

void UWebSocketSubSystem::HandleParsedMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject)
{
}

void UWebSocketSubSystem::SendJsonMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject)
{
}
