#include "Server/WebSocketSubSystem.h"
#include "WebSocketsModule.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Kismet/GameplayStatics.h"

#include "Server/AuthSubSystem.h"
#include "Server/MatchmakingSubSystem.h"
#include "Server/InventorySubSystem.h"
// Include InGame player controller to call client RPC
#include "GameMode/PlayerController_InGame.h"

UWebSocketSubSystem* UWebSocketSubSystem::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;
	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;
	return inst->GetSubsystem<UWebSocketSubSystem>();
}

void UWebSocketSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (IsRunningDedicatedServer())
	{
		UE_LOG(LogTemp, Log, TEXT("[WebSocket Subsystem] Dedicated Server Detected. Skipping Lobby Connect."));
		return;
	}

	if (bHasAttemptConnection) return;
	bHasAttemptConnection = true;
	ConnectToLobbyServer();
}

void UWebSocketSubSystem::Deinitialize()
{
	if (WebSocket.IsValid())
	{
		WebSocket->OnConnected().RemoveAll(this);
		WebSocket->OnConnectionError().RemoveAll(this);
		WebSocket->OnClosed().RemoveAll(this);
		WebSocket->OnMessage().RemoveAll(this);

		if (WebSocket->IsConnected())
		{
			WebSocket->Close();
		}
		WebSocket.Reset();
	}
	Super::Deinitialize();
}

void UWebSocketSubSystem::ConnectToLobbyServer()
{
	if (WebSocket.IsValid() && WebSocket->IsConnected()) return;

	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModule("WebSockets");
	}

	FString serverURL = TEXT("ws://127.0.0.1:8080");
	WebSocket = FWebSocketsModule::Get().CreateWebSocket(serverURL, TEXT("ws"));

	if (!WebSocket.IsValid()) return;

	WebSocket->OnConnected().AddUObject(this, &UWebSocketSubSystem::OnConnected);
	WebSocket->OnConnectionError().AddUObject(this, &UWebSocketSubSystem::OnConnectionError);
	WebSocket->OnClosed().AddUObject(this, &UWebSocketSubSystem::OnClosed);
	WebSocket->OnMessage().AddUObject(this, &UWebSocketSubSystem::OnMessageReceived);

	WebSocket->Connect();
}

void UWebSocketSubSystem::OnConnected() { UE_LOG(LogTemp, Log, TEXT("[WebSocket Subsystem] 로비 서버 연결 성공")); }
void UWebSocketSubSystem::OnConnectionError(const FString& Error) { UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 연결 에러: %s"), *Error); }
void UWebSocketSubSystem::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean) { UE_LOG(LogTemp, Warning, TEXT("[WebSocket Subsystem] 연결 종료: %s"), *Reason); }

void UWebSocketSubSystem::OnMessageReceived(const FString& MessageString)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(MessageString);

	if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
	{
		FString MessageType = JsonObject->GetStringField(TEXT("type"));
		TSharedPtr<FJsonObject> Payload = JsonObject->GetObjectField(TEXT("payload"));

		HandleParsedMessage(MessageType, Payload);
	}
}

void UWebSocketSubSystem::HandleParsedMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject)
{
	if (!PayloadObject.IsValid()) return;

	FString UpperType = Type.ToUpper();
	OnWebSocketMessageReceived.Broadcast(UpperType, PayloadObject);

	UGameInstance* GI = GetGameInstance();
	if (!GI) return;

	// 1. Auth 관련 메시지 위임
	if (UpperType.Contains(TEXT("LOGIN")) || UpperType.Contains(TEXT("ACCOUNT")) || UpperType.Contains(TEXT("CREATE_ID")) || UpperType.Contains(TEXT("SIGN")))
	{
		if (UAuthSubSystem* AuthSub = GI->GetSubsystem<UAuthSubSystem>())
		{
			AuthSub->HandleAuthMessage(UpperType, PayloadObject);
		}
		return;
	}

	// 2. Matchmaking 관련 메시지 위임
	if (UpperType.Contains(TEXT("MATCH")) || UpperType.Contains(TEXT("WAITING")) || UpperType.Contains(TEXT("SERVER")) ||
		UpperType.Contains(TEXT("JOIN")) || UpperType.Contains(TEXT("GAMESTART")) || UpperType.Contains(TEXT("CANCEL")))
	{
		if (UMatchmakingSubSystem* MatchSub = GI->GetSubsystem<UMatchmakingSubSystem>())
		{
			MatchSub->HandleMatchMessage(UpperType, PayloadObject);
		}
		return;
	}

	// 3. Inventory 관련 메시지 위임
	if (UpperType.Contains(TEXT("INVENTORY")) || UpperType.Contains(TEXT("ITEM")) || UpperType.Contains(TEXT("POCKET")) || UpperType.Contains(TEXT("STASH")))
	{
		if (UInventorySubSystem* InvSub = GI->GetSubsystem<UInventorySubSystem>())
		{
			InvSub->HandleInventoryMessage(UpperType, PayloadObject);
		}

		// Additionally, if running as dedicated server, forward the message to owning player's client(s)
		if (IsRunningDedicatedServer())
		{
			// Serialize payload back to JSON string
			FString PayloadJson;
			TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&PayloadJson);
			if (FJsonSerializer::Serialize(PayloadObject.ToSharedRef(), Writer))
			{
				// Iterate all player controllers and call client RPC
				UWorld* World = GetWorld();
				if (World)
				{
					for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
					{
						if (APlayerController* PC = It->Get())
						{
							// Only forward to InGame player controllers
							if (APlayerController_InGame* InGamePC = Cast<APlayerController_InGame>(PC))
							{
								InGamePC->Client_ReceiveInventoryJson(UpperType, PayloadJson);
							}
						}
					}
				}
			}
		}
		return;
	}
}

void UWebSocketSubSystem::SendJsonMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject)
{
	TSharedPtr<FJsonObject> RootObject = MakeShared<FJsonObject>();
	RootObject->SetStringField(TEXT("type"), Type);
	RootObject->SetObjectField(TEXT("payload"), PayloadObject);

	FString OutputString;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
	if (FJsonSerializer::Serialize(RootObject.ToSharedRef(), Writer))
	{
		if (!WebSocket.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("UWebSocketSubSystem::SendJsonMessage - WebSocket is not valid, cannot send Type=%s"), *Type);
			return;
		}

		if (!WebSocket->IsConnected())
		{
			UE_LOG(LogTemp, Warning, TEXT("UWebSocketSubSystem::SendJsonMessage - WebSocket is not connected, cannot send Type=%s"), *Type);
			return;
		}

		WebSocket->Send(OutputString);
	}
}

bool UWebSocketSubSystem::SendPayload(const FString& Type, TSharedPtr<FJsonObject> PayloadObject)
{
	bool bConnected = WebSocket.IsValid() && WebSocket->IsConnected();
	UE_LOG(LogTemp, Warning, TEXT("[WebSocketSubSystem] SendPayload: Type=%s Connected=%s"), *Type, bConnected ? TEXT("true") : TEXT("false"));
	if (!bConnected) return false;
	SendJsonMessage(Type, PayloadObject);
	UE_LOG(LogTemp, Warning, TEXT("[WebSocketSubSystem] SendPayload: Type=%s Sent"), *Type);
	return true;
}