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

void UWebSocketSubSystem::RequestMoveItem(const FGuid& FromInventoryGuid, const FGuid& ToInventoryGuid, const FGuid& ItemGuid, const FIntPoint& TargetPosition, bool bIsRotated)
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected())
	{
		UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 웹소켓이 연결되어있지 않습니다."));
		return;
	}

	TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();

	// Node.js 서버 핸들러(handleMoveItem) 파라미터 구조와 대소문자 일치
	PayloadObject->SetStringField(TEXT("FromInventoryGuid"), FromInventoryGuid.ToString(EGuidFormats::DigitsWithHyphens));
	PayloadObject->SetStringField(TEXT("ToInventoryGuid"), ToInventoryGuid.ToString(EGuidFormats::DigitsWithHyphens));
	PayloadObject->SetStringField(TEXT("ItemGuid"), ItemGuid.ToString(EGuidFormats::DigitsWithHyphens));
	PayloadObject->SetNumberField(TEXT("TargetX"), TargetPosition.X);
	PayloadObject->SetNumberField(TEXT("TargetY"), TargetPosition.Y);
	PayloadObject->SetBoolField(TEXT("bIsRotated"), bIsRotated);

	SendJsonMessage(TEXT("REQ_MOVE_ITEM"), PayloadObject);
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
	else if (UpperType == TEXT("INVENTORY_DATA"))
	{
		FInventoryMapWrapper InventoryMapWrapper;

		// 1) 최상위 인벤토리 GUID (Stash / Pocket)
		if (PayloadObject->HasField(TEXT("stashGuid")))
		{
			FGuid::Parse(PayloadObject->GetStringField(TEXT("stashGuid")), InventoryMapWrapper.StashGuid);
		}
		if (PayloadObject->HasField(TEXT("pocketGuid")))
		{
			FGuid::Parse(PayloadObject->GetStringField(TEXT("pocketGuid")), InventoryMapWrapper.PocketGuid);
		}

		// 2) DB(inventorycontainer) 기반 크기 데이터 파싱
		const TArray<TSharedPtr<FJsonValue>>* InventoriesArray;
		if (PayloadObject->TryGetArrayField(TEXT("inventories"), InventoriesArray))
		{
			for (const TSharedPtr<FJsonValue>& InvenValue : *InventoriesArray)
			{
				TSharedPtr<FJsonObject> InvenObj = InvenValue->AsObject();
				if (!InvenObj.IsValid()) continue;

				FGuid InvenGuid;
				// inventory_id 또는 guid 필드
				FString GuidStr = InvenObj->HasField(TEXT("inventory_id")) ? InvenObj->GetStringField(TEXT("inventory_id")) : InvenObj->GetStringField(TEXT("guid"));

				if (FGuid::Parse(GuidStr, InvenGuid))
				{
					// DB 필드명(max_cols, max_rows)과 JSON Key 파싱 유연화
					int32 Cols = InvenObj->HasField(TEXT("max_cols")) ? InvenObj->GetIntegerField(TEXT("max_cols")) : InvenObj->GetIntegerField(TEXT("cols"));
					int32 Rows = InvenObj->HasField(TEXT("max_rows")) ? InvenObj->GetIntegerField(TEXT("max_rows")) : InvenObj->GetIntegerField(TEXT("rows"));

					InventoryMapWrapper.InventorySizeMap.Add(InvenGuid, FIntPoint(Cols, Rows));
				}
			}
		}

		TMap<FGuid, FItemArrayWrapper> InventoryItems;
		UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
		if (subSystem == nullptr) return;

		const TArray<TSharedPtr<FJsonValue>>* ItemsArray;
		if (PayloadObject->TryGetArrayField(TEXT("items"), ItemsArray))
		{
			for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
			{
				TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
				if (!ItemObject.IsValid()) continue;

				FItemInstance Item;
				FGuid::Parse(ItemObject->GetStringField(TEXT("guid")), Item.GUID);
				FGuid::Parse(ItemObject->GetStringField(TEXT("parent_inventory_guid")), Item.parent_inventory_guid);

				Item.ItemID = FName(*ItemObject->GetStringField(TEXT("item_id")));
				Item.StackCount = ItemObject->GetIntegerField(TEXT("stack_count"));
				Item.Durability = ItemObject->GetNumberField(TEXT("current_durability"));
				Item.Position.X = ItemObject->GetIntegerField(TEXT("pos_x"));
				Item.Position.Y = ItemObject->GetIntegerField(TEXT("pos_y"));

				// 회전 상태 파싱 (bool/int 유연 파싱)
				if (ItemObject->HasField(TEXT("bIsRotated")))
				{
					Item.bIsRotated = ItemObject->GetBoolField(TEXT("bIsRotated"));
				}
				else if (ItemObject->HasField(TEXT("is_rotate")))
				{
					Item.bIsRotated = ItemObject->GetIntegerField(TEXT("is_rotate")) == 1;
				}

				const FItemTableRow* ItemInstance = subSystem->GetItem(Item.ItemID);
				if (ItemInstance != nullptr)
				{
					Item.type = ItemInstance->ItemType;
				}

				InventoryItems.FindOrAdd(Item.parent_inventory_guid).Items.Add(Item);
			}
		}

		InventoryMapWrapper.InventoryMap = InventoryItems;

		UE_LOG(LogTemp, Log, TEXT("[Inventory] DB 수신 완료 - StashGUID(%s), PocketGUID(%s), 크기정보 %d개"),
			*InventoryMapWrapper.StashGuid.ToString(),
			*InventoryMapWrapper.PocketGuid.ToString(),
			InventoryMapWrapper.InventorySizeMap.Num());

		OnInventoryReceived.Broadcast(InventoryMapWrapper);
	}
	// 6. 아이템 이동 응답 (RES_MOVE_ITEM)
	else if (UpperType == TEXT("RES_MOVE_ITEM"))
	{
		bool bSuccess = PayloadObject->GetBoolField(TEXT("success"));

		if (bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("[WebSocket Subsystem] 아이템 이동 성공"));
			// Node.js 서버에서 RES_MOVE_ITEM 직후 INVENTORY_DATA 패킷을 연속으로 보내줄 경우 
			// 위 5번 분기(INVENTORY_DATA)에서 자동으로 최신 UI가 동기화됩니다.
		}
		else
		{
			FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("이동 실패");
			UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 아이템 이동 실패: %s"), *Message);
		}
	}
	// 7. 매칭 취소
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