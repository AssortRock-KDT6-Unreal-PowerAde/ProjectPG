// Fill out your copyright notice in the Description page of Project Settings.

#include "Server/WebSocketSubSystem.h"
#include "WebSocketsModule.h"
#include "Json.h"
#include "JsonUtilities.h"
#include "Common/GameData.h"
#include "Kismet/GameplayStatics.h"
#include "Core/ItemSubSystem.h"

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
	// 이미 연결 시도를 진행 중
	if (bHasAttemptConnection)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WebSocket Subsystem] Initialize 중복 호출 방지됨."));
		return;
	}
	bHasAttemptConnection = true;
	ConnectToLobbyServer();
}

void UWebSocketSubSystem::Deinitialize()
{
	if (!WebSocket.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓이 연결되어있지 않습니다."));
		return;
	}
	else
	{
		if (WebSocket.IsValid())
		{
			WebSocket->OnConnected().RemoveAll(this);
			WebSocket->OnConnectionError().RemoveAll(this);
			WebSocket->OnClosed().RemoveAll(this);
			WebSocket->OnMessage().RemoveAll(this);
		}
		// 소켓 연결이 열려있다면 Close() 호출로 명시적 종료
		if (WebSocket->IsConnected())
		{
			WebSocket->Close();
		}
	}
	Super::Deinitialize();
}

void UWebSocketSubSystem::ConnectToLobbyServer()
{
	if (WebSocket.IsValid() && WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Warning, TEXT("[WebSocket SubSystem] 이미 로비 서버에 연결되어 있습니다."));
		return;
	}

	if (!FModuleManager::Get().IsModuleLoaded("WebSockets"))
	{
		FModuleManager::Get().LoadModule("WebSockets");
	}
	FString serverURL = TEXT("ws://127.0.0.1:8080");
	FString ServerProtocol = TEXT("ws");
	WebSocket = FWebSocketsModule::Get().CreateWebSocket(serverURL, ServerProtocol);

	if (!WebSocket.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓 객체 생성에 실패했습니다."));
		return;
	}
	WebSocket->OnConnected().AddUObject(this, &UWebSocketSubSystem::OnConnected);
	WebSocket->OnConnectionError().AddUObject(this, &UWebSocketSubSystem::OnConnectionError);
	WebSocket->OnClosed().AddUObject(this, &UWebSocketSubSystem::OnClosed);
	WebSocket->OnMessage().AddUObject(this, &UWebSocketSubSystem::OnMessageReceived);

	WebSocket->Connect();
}

void UWebSocketSubSystem::RequestLogin(const FString& UserID)
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓이 연결되어있지 않습니다."));
		return;
	}

	CurrentUserId = UserID;

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("userId"), UserID);

	SendJsonMessage(TEXT("LOGIN"), Payload);
}

void UWebSocketSubSystem::RequestGameStart()
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓이 연결되어있지 않습니다."));
		return;
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	SendJsonMessage(TEXT("GameStart"), Payload);
}

void UWebSocketSubSystem::RequestCancleMatch()
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected()) return;

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	SendJsonMessage(TEXT("CancelMatch"), Payload);
}

void UWebSocketSubSystem::RequestCreateID(const FString& UserId)
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓 연결되어 있지 않습니다."));
		return;
	}
	CurrentUserId = UserId;

	FGuid NewPocketGUID = FGuid::NewGuid();
	FGuid NewStashGUID = FGuid::NewGuid();

	// 언리얼 GUID를 표준 문자열 형태(DigitsWithHyphens)로 변환
	FString PocketGuidStr = NewPocketGUID.ToString(EGuidFormats::DigitsWithHyphens);
	FString StashGuidStr = NewStashGUID.ToString(EGuidFormats::DigitsWithHyphens);

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("userId"), UserId);
	Payload->SetStringField(TEXT("pocketGuid"), PocketGuidStr);
	Payload->SetStringField(TEXT("stashGuid"), StashGuidStr);

	UE_LOG(LogTemp, Log, TEXT("[RequestCreateID] 전송 데이터 - userId: %s | pocketGuid: %s | stashGuid: %s"),
		*UserId, *PocketGuidStr, *StashGuidStr);

	SendJsonMessage(TEXT("Create_ID"), Payload);
}

void UWebSocketSubSystem::RequestGetInventory()
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓이 연결되어있지 않습니다."));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("데이터 불러오는중..."));

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	SendJsonMessage(TEXT("GET_INVENTORY"), Payload);
}

void UWebSocketSubSystem::OnConnected()
{
	UE_LOG(LogTemp, Log, TEXT("[WebSocket Subsystem] 로비 서버 연결 성공"));
}

void UWebSocketSubSystem::OnConnectionError(const FString& Error)
{
	UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 연결 에러: %s"), *Error);
}

void UWebSocketSubSystem::OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean)
{
	UE_LOG(LogTemp, Warning, TEXT("[WebSocket Subsystem] 로비 서버와 연결이 종료되었습니다. Reason: %s"), *Reason);
}

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

	// 0-1. 계정 생성 성공
	if (UpperType == TEXT("CREATE_ACCOUNT_SUCCESS") || UpperType == TEXT("CREATE_ID_SUCCESS"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("계정 생성 성공");

		UE_LOG(LogTemp, Log, TEXT("[Create ID Status] 성공: %s"), *Message);
		OnCreateIDStatusChanged.Broadcast(true, CurrentUserId, Message);
	}
	// 0-2. 계정 생성 실패
	else if (UpperType == TEXT("CREATE_ACCOUNT_FAIL") || UpperType == TEXT("CREATE_ID_FAILURE"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("계정 생성 실패");

		UE_LOG(LogTemp, Error, TEXT("[Create ID Status] 실패: %s"), *Message);
		OnCreateIDStatusChanged.Broadcast(false, TEXT(""), Message);
	}
	// 1-1. 로그인 성공
	else if (UpperType == TEXT("LOGIN_SUCCESS"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("로그인 성공");

		UE_LOG(LogTemp, Log, TEXT("[Login Status] 성공: %s"), *Message);
		OnLoginStatusChanged.Broadcast(true, true, Message);
	}
	// 1-2. 로그인 실패
	else if (UpperType == TEXT("LOGIN_FAIL") || UpperType == TEXT("LOGIN_FAILURE"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("로그인 실패");

		UE_LOG(LogTemp, Error, TEXT("[Login Status] 실패: %s"), *Message);
		OnLoginStatusChanged.Broadcast(false, false, Message);
	}
	// 2. GameStart 후 매칭 대기 중
	else if (UpperType == TEXT("WAITING_FOR_MATCH"))
	{
		FString Message = PayloadObject->GetStringField(TEXT("message"));
		int32 CurrentCount = PayloadObject->GetIntegerField(TEXT("currentQueueCount"));
		int32 TargetCount = PayloadObject->GetIntegerField(TEXT("targetCount"));

		FString StatusText = FString::Printf(TEXT("%s (%d/%d)"), *Message, CurrentCount, TargetCount);
		UE_LOG(LogTemp, Log, TEXT("[Matchmaking]: %s"), *StatusText);

		OnMatchStatusChanged.Broadcast(TEXT("WAITING"), StatusText);
	}
	// 3. 매칭 완료 후 서버 부팅 대기 중
	else if (UpperType == TEXT("SERVER_STARTING"))
	{
		FString Message = PayloadObject->GetStringField(TEXT("message"));
		UE_LOG(LogTemp, Log, TEXT("[Matchmaking]: %s"), *Message);

		OnMatchStatusChanged.Broadcast(TEXT("SERVER_STARTING"), Message);
	}
	// 4. 데디케이트 서버 구동 완료 -> 인게임으로 이동
	else if (UpperType == TEXT("JOIN_SERVER"))
	{
		FString IP = PayloadObject->GetStringField(TEXT("ip"));
		int32 Port = PayloadObject->GetIntegerField(TEXT("port"));

		FString ConnectURL = FString::Printf(TEXT("%s:%d?UserId=%s"), *IP, Port, *CurrentUserId);
		UE_LOG(LogTemp, Log, TEXT("[Matchmaking] 데디케이트 서버로 이동 -> %s"), *ConnectURL);

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
	// 5. 인벤토리 데이터 파싱
	else if (UpperType == TEXT("INVENTORY_DATA"))
	{
		// KEY를 FGuid로 사용하는 맵으로 수정
		TMap<FGuid, FItemArrayWrapper> InventoryItems;

		UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
		if (subSystem == nullptr)
		{
			UE_LOG(LogTemp, Error, TEXT("[Inventory] ItemSubSystem을 찾을 수 없습니다."));
			return;
		}

		// payload 내부의 "items" 배열 추출
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray;
		if (PayloadObject->TryGetArrayField(TEXT("items"), ItemsArray))
		{
			for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
			{
				TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
				if (!ItemObject.IsValid()) continue;

				FItemInstance Item;

				// 아이템 자체 GUID 파싱
				FString GuidString = ItemObject->GetStringField(TEXT("guid"));
				FGuid::Parse(GuidString, Item.GUID);

				// 부모 인벤토리 GUID 파싱 (parent_inventory_guid)
				FString ParentGuidString = ItemObject->GetStringField(TEXT("parent_inventory_guid"));
				FGuid::Parse(ParentGuidString, Item.parent_inventory_guid);

				// ItemID, StackCount, Durability, Position 파싱
				Item.ItemID = FName(*ItemObject->GetStringField(TEXT("item_id")));
				Item.StackCount = ItemObject->GetIntegerField(TEXT("stack_count"));
				Item.Durability = ItemObject->GetNumberField(TEXT("current_durability"));
				Item.Position.X = ItemObject->GetIntegerField(TEXT("pos_x"));
				Item.Position.Y = ItemObject->GetIntegerField(TEXT("pos_y"));

				// ItemData 테이블 정보 연결
				const FItemTableRow* ItemInstance = subSystem->GetItem(Item.ItemID);
				if (ItemInstance != nullptr)
				{
					Item.type = ItemInstance->ItemType;
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Inventory] 유효하지 않은 ItemID: %s"), *Item.ItemID.ToString());
				}

				// 내부 인벤토리 GUID가 있는 경우 (가방/상자 아이템 등)
				if (ItemObject->HasField(TEXT("inventory_guid")))
				{
					FString InvenGuidStr = ItemObject->GetStringField(TEXT("inventory_guid"));
					FGuid::Parse(InvenGuidStr, Item.inventory_guid);
				}

				// 파싱된 아이템을 부모 인벤토리 GUID 맵에 추가
				InventoryItems.FindOrAdd(Item.parent_inventory_guid).Items.Add(Item);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Inventory] 인벤토리 데이터 수신 완료: 총 %d개 가방/보관함 분류됨"), InventoryItems.Num());

		FInventoryMapWrapper InventoryMapWrapper;
		InventoryMapWrapper.InventoryMap = InventoryItems;

		OnInventoryReceived.Broadcast(InventoryMapWrapper);
	}
	// 6. 매칭 취소
	else if (UpperType == TEXT("MATCH_CANCELLED"))
	{
		FString Message = PayloadObject->GetStringField(TEXT("message"));
		OnMatchStatusChanged.Broadcast(TEXT("CANCELLED"), Message);
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
		UE_LOG(LogTemp, Log, TEXT("[WebSocket Send] %s"), *OutputString);
		WebSocket->Send(OutputString);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] JSON 직렬화 실패"));
	}
}