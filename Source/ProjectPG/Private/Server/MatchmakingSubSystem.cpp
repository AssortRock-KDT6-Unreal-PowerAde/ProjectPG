#include "Server/MatchmakingSubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include "Dom/JsonObject.h"
#include "Kismet/GameplayStatics.h"

UMatchmakingSubSystem* UMatchmakingSubSystem::Get(UWorld* World)
{
	if (!World) return nullptr;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		return GI->GetSubsystem<UMatchmakingSubSystem>();
	}
	return nullptr;
}

void UMatchmakingSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UMatchmakingSubSystem::Deinitialize()
{
	Super::Deinitialize();
}

void UMatchmakingSubSystem::HandleMatchMessage(const FString& MessageType, TSharedPtr<FJsonObject> PayloadObject)
{
	if (!PayloadObject.IsValid()) return;

	UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld());

	if (MessageType == TEXT("WAITING_FOR_MATCH"))
	{
		FString Message = PayloadObject->GetStringField(TEXT("message"));
		int32 CurrentCount = PayloadObject->GetIntegerField(TEXT("currentQueueCount"));
		int32 TargetCount = PayloadObject->GetIntegerField(TEXT("targetCount"));

		FString StatusText = FString::Printf(TEXT("%s (%d/%d)"), *Message, CurrentCount, TargetCount);
		OnMatchStatusChanged.Broadcast(TEXT("WAITING"), StatusText);
	}
	else if (MessageType == TEXT("SERVER_STARTING"))
	{
		FString Message = PayloadObject->GetStringField(TEXT("message"));
		OnMatchStatusChanged.Broadcast(TEXT("SERVER_STARTING"), Message);
	}
	else if (MessageType == TEXT("JOIN_SERVER"))
	{
		FString IP = PayloadObject->GetStringField(TEXT("ip"));
		int32 Port = PayloadObject->GetIntegerField(TEXT("port"));
		FString CurrentUserId = WS ? WS->GetCurrentUserID() : TEXT("");

		FString ConnectURL = FString::Printf(TEXT("%s:%d?UserId=%s"), *IP, Port, *CurrentUserId);

		UWorld* World = GetWorld();
		if (World)
		{
			APlayerController* PC = World->GetFirstPlayerController();
			if (PC)
			{
				PC->ClientTravel(ConnectURL, ETravelType::TRAVEL_Absolute);
			}
			else
			{
				UGameplayStatics::OpenLevel(World, FName(*ConnectURL));
			}
		}
	}
	else if (MessageType == TEXT("MATCH_CANCELLED"))
	{
		FString Message = PayloadObject->GetStringField(TEXT("message"));
		OnMatchStatusChanged.Broadcast(TEXT("Match_CANCELLED"), Message);
	}
}

void UMatchmakingSubSystem::RequestGameStart()
{
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		WS->SendJsonMessage(TEXT("GameStart"), Payload);
	}
}

void UMatchmakingSubSystem::RequestCancleMatch()
{
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		WS->SendJsonMessage(TEXT("CancelMatch"), Payload);
		OnMatchStatusChanged.Broadcast(TEXT("CancelMatch"), TEXT("매칭 취소중..."));

	}
}