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

	UE_LOG(LogTemp, Warning,
		TEXT("========== NETWORK VERSION =========="));

	//UE_LOG(LogTemp, Warning,
	//	TEXT("NetworkVersion: %u"),
	//	FNetworkVersion::GetLocalNetworkVersion());

	uint32 CurrentVersion = 0;

	// 오버라이드 델리게이트에 바인딩된 함수가 존재한다면 그 값을 가져오고, 아니면 기본 버전을 가져옵니다.
	if (FNetworkVersion::GetLocalNetworkVersionOverride.IsBound())
	{
		// 오버라이드 함수를 직접 호출하여 현재 적용된 커스텀 버전 값을 확인합니다.
		CurrentVersion = FNetworkVersion::GetLocalNetworkVersionOverride.Execute();
	}
	else
	{
		CurrentVersion = FNetworkVersion::GetLocalNetworkVersion();
	}

	UE_LOG(LogTemp, Warning,
		TEXT("NetworkVersion: %u"),
		CurrentVersion);
	UE_LOG(LogTemp, Warning,
		TEXT("IsDedicatedServer: %s"),
		IsRunningDedicatedServer() ? TEXT("TRUE") : TEXT("FALSE"));

	UE_LOG(LogTemp, Warning,
		TEXT("====================================="));

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
	// 1. GUID 생성 (기존 포켓, 창고 + 나머지 장비 슬롯)
	FGuid NewPocketGUID = FGuid::NewGuid();
	FGuid NewStashGUID	= FGuid::NewGuid();
	FGuid NewMainWeapon = FGuid::NewGuid();
	FGuid NewSubWeapon	= FGuid::NewGuid();
	FGuid NewHelMet		= FGuid::NewGuid();
	FGuid NewCloth		= FGuid::NewGuid();
	FGuid NewPants		= FGuid::NewGuid();
	FGuid NewShose		= FGuid::NewGuid();
	FGuid NewBackPack	= FGuid::NewGuid();
	FGuid NewAccuracy1	= FGuid::NewGuid();
	FGuid NewAccuracy2	= FGuid::NewGuid();

	// 2. 언리얼 GUID를 표준 문자열 형태(DigitsWithHyphens)로 변환
	FString PocketGuidStr = NewPocketGUID.ToString(EGuidFormats::DigitsWithHyphens);
	FString StashGuidStr = NewStashGUID.ToString(EGuidFormats::DigitsWithHyphens);
	FString MainWeaponStr = NewMainWeapon.ToString(EGuidFormats::DigitsWithHyphens);
	FString SubWeaponStr = NewSubWeapon.ToString(EGuidFormats::DigitsWithHyphens);
	FString HelMetStr = NewHelMet.ToString(EGuidFormats::DigitsWithHyphens);
	FString ClothStr = NewCloth.ToString(EGuidFormats::DigitsWithHyphens);
	FString PantsStr = NewPants.ToString(EGuidFormats::DigitsWithHyphens);
	FString ShoseStr = NewShose.ToString(EGuidFormats::DigitsWithHyphens);
	FString BackPackStr = NewBackPack.ToString(EGuidFormats::DigitsWithHyphens);
	FString Accuracy1Str = NewAccuracy1.ToString(EGuidFormats::DigitsWithHyphens);
	FString Accuracy2Str = NewAccuracy2.ToString(EGuidFormats::DigitsWithHyphens);

	// 3. JSON Payload 객체 생성 및 데이터 세팅
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("userId"), UserId);
	Payload->SetStringField(TEXT("pocketGuid"), PocketGuidStr);
	Payload->SetStringField(TEXT("stashGuid"), StashGuidStr);

	// Node.js 서버 카멜케이스(camelCase) 변수명에 맞춰 JSON 필드 추가
	Payload->SetStringField(TEXT("MainWeapon"), MainWeaponStr);
	Payload->SetStringField(TEXT("SubWeapon"), SubWeaponStr);
	Payload->SetStringField(TEXT("HelMet"), HelMetStr);
	Payload->SetStringField(TEXT("Cloth"), ClothStr);
	Payload->SetStringField(TEXT("Pants"), PantsStr);
	Payload->SetStringField(TEXT("Shose"), ShoseStr);
	Payload->SetStringField(TEXT("BackPack"), BackPackStr);
	Payload->SetStringField(TEXT("Accuracy1"), Accuracy1Str);
	Payload->SetStringField(TEXT("Accuracy2"), Accuracy2Str);

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
		FInventoryMapWrapper EquipMapWrapper;
		FString TempGuidStr;
		if (PayloadObject->TryGetStringField(TEXT("stashGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.StashGuid);
		}
		if (PayloadObject->TryGetStringField(TEXT("pocketGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.PocketGuid);
		}

		if (PayloadObject->TryGetStringField(TEXT("MainWeaponGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.MainWeapon);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.MainWeapon);
		}
		if (PayloadObject->TryGetStringField(TEXT("SubWeaponGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.SubWeapon);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.SubWeapon);
		}
		if (PayloadObject->TryGetStringField(TEXT("HelMetGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.HelMet);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.HelMet);
		}
		if (PayloadObject->TryGetStringField(TEXT("ClothGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.Cloth);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.Cloth);
		}
		if (PayloadObject->TryGetStringField(TEXT("PantsGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.Pants);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.Pants);
		}
		if (PayloadObject->TryGetStringField(TEXT("ShoseGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.Shose);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.Shose);
		}
		if (PayloadObject->TryGetStringField(TEXT("BackPackGuid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.BackPack);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.BackPack);
		}
		if (PayloadObject->TryGetStringField(TEXT("Accuracy1Guid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.Accuracy1);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.Accuracy1);
		}
		if (PayloadObject->TryGetStringField(TEXT("Accuracy2Guid"), TempGuidStr) && !TempGuidStr.IsEmpty())
		{
			FGuid::Parse(TempGuidStr, InventoryMapWrapper.Accuracy2);
			FGuid::Parse(TempGuidStr, EquipMapWrapper.Accuracy2);
		}

		TMap<FGuid, FItemArrayWrapper> InventoryItems;
		TMap<FGuid, FItemArrayWrapper> EquipItems;

		// 1) 인벤토리 컨테이너 크기 파싱 및 빈 InventoryMap Key 사전 생성
		const TArray<TSharedPtr<FJsonValue>>* InventoriesArray;
		if (PayloadObject->TryGetArrayField(TEXT("inventories"), InventoriesArray))
		{
			for (const TSharedPtr<FJsonValue>& InvenValue : *InventoriesArray)
			{
				TSharedPtr<FJsonObject> InvenObj = InvenValue->AsObject();
				if (!InvenObj.IsValid()) continue;

				FGuid InvenGuid;
				FString GuidStr;
				if (InvenObj->TryGetStringField(TEXT("inventory_id"), GuidStr) || InvenObj->TryGetStringField(TEXT("guid"), GuidStr))
				{
					if (FGuid::Parse(GuidStr, InvenGuid))
					{
						int32 Cols = 0;
						int32 Rows = 0;

						if (!InvenObj->TryGetNumberField(TEXT("max_cols"), Cols))
						{
							InvenObj->TryGetNumberField(TEXT("cols"), Cols);
						}

						if (!InvenObj->TryGetNumberField(TEXT("max_rows"), Rows))
						{
							InvenObj->TryGetNumberField(TEXT("rows"), Rows);
						}

						if (Cols > 0 && Rows > 0)
						{
							InventoryMapWrapper.InventorySizeMap.Add(InvenGuid, FIntPoint(Cols, Rows));
						}

						InventoryItems.FindOrAdd(InvenGuid);
					}
				}
			}
		}

		// 2) 아이템 배열 파싱
		const TArray<TSharedPtr<FJsonValue>>* ItemsArray;

		if (PayloadObject->TryGetArrayField(TEXT("items"), ItemsArray))
		{
			for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
			{
				TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
				if (!ItemObject.IsValid()) continue;

				FItemInstance Item;

				// GUID 파싱
				FString ItemGuidStr;
				if (ItemObject->TryGetStringField(TEXT("guid"), ItemGuidStr))
				{
					FGuid::Parse(ItemGuidStr, Item.GUID);
				}

				// ★ Parent GUID 파싱 (Null 사용 시 Warning 방지)
				FString ParentGuidStr;
				if (ItemObject->TryGetStringField(TEXT("parent_inventory_guid"), ParentGuidStr) && !ParentGuidStr.IsEmpty())
				{
					FGuid::Parse(ParentGuidStr, Item.parent_inventory_guid);
				}

				// ItemID 파싱
				FString ItemIdStr;
				if (ItemObject->TryGetStringField(TEXT("item_id"), ItemIdStr))
				{
					Item.ItemID = FName(*ItemIdStr);
				}

				// 수량 및 내구도 파싱
				ItemObject->TryGetNumberField(TEXT("stack_count"), Item.StackCount);

				double TempDurability = 0.0;
				if (ItemObject->TryGetNumberField(TEXT("current_durability"), TempDurability))
				{
					Item.Durability = TempDurability;
				}

				ItemObject->TryGetNumberField(TEXT("pos_x"), Item.Position.X);
				ItemObject->TryGetNumberField(TEXT("pos_y"), Item.Position.Y);

				// is_equipped 파싱
				int32 IsEquippedInt = 0;
				if (ItemObject->TryGetNumberField(TEXT("is_equipped"), IsEquippedInt))
				{
					Item.bEquip = (IsEquippedInt == 1);
						
				}	
				if (ItemObject->HasField(TEXT("bIsRotated")))
				{
					Item.bIsRotated = ItemObject->GetBoolField(TEXT("bIsRotated"));
				}

				if (UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld()))
				{
					const FItemTableRow* ItemInstance = subSystem->GetItem(Item.ItemID);
					if (ItemInstance != nullptr)
					{
						Item.type = ItemInstance->ItemType;
					}
				}

				// 중복 제거: 같은 GUID가 다른 컨테이너에 이미 들어있다면 제거
				for (auto& Pair : InventoryItems)
				{
					TArray<FItemInstance>& ListRef = Pair.Value.Items;
					for (int32 idx = ListRef.Num() - 1; idx >= 0; --idx)
					{
						if (ListRef[idx].GUID == Item.GUID)
						{
							UE_LOG(LogTemp, Warning, TEXT("[WebSocket] Removing duplicate item %s from container %s"), *Item.GUID.ToString(), *Pair.Key.ToString());
							ListRef.RemoveAt(idx);
						}
					}
				}
				for (auto& Pair : EquipItems)
				{
					TArray<FItemInstance>& ListRef = Pair.Value.Items;
					for (int32 idx = ListRef.Num() - 1; idx >= 0; --idx)
					{
						if (ListRef[idx].GUID == Item.GUID)
						{
							UE_LOG(LogTemp, Warning, TEXT("[WebSocket] Removing duplicate equip item %s from container %s"), *Item.GUID.ToString(), *Pair.Key.ToString());
							ListRef.RemoveAt(idx);
						}
					}
				}

				// ★ 장착 상태가 아니고 부모 인벤토리 GUID가 유효할 때만 슬롯 리스트에 등록
				if (!Item.bEquip && Item.parent_inventory_guid.IsValid())
				{
					InventoryItems.FindOrAdd(Item.parent_inventory_guid).Items.Add(Item);
				}
				else if (Item.bEquip)
				{
					EquipItems.FindOrAdd(Item.parent_inventory_guid).Items.Add(Item);
					UE_LOG(LogTemp, Warning, TEXT("장착 guid 저장: %s"), *Item.parent_inventory_guid.ToString());
				}
			}
		}

		InventoryMapWrapper.InventoryMap = InventoryItems;
		EquipMapWrapper.InventoryMap = EquipItems;
		OnInventoryReceived.Broadcast(InventoryMapWrapper);
		OnEquipRecived.Broadcast(EquipMapWrapper);
	}
	// 6. 아이템 이동 응답 (RES_MOVE_ITEM)
	else if (UpperType == TEXT("RES_MOVE_ITEM"))
	{
		bool bSuccess = PayloadObject->GetBoolField(TEXT("success"));

		if (bSuccess)
		{
			UE_LOG(LogTemp, Log, TEXT("[WebSocket Subsystem] 아이템 이동 성공 - 최신 인벤토리 재요청"));

			// ★ [수정] 이동 성공 시 서버로부터 최신 인벤토리 데이터를 다시 받아와 UI/메모리 동기화
			RequestGetInventory();
		}
		else
		{
			FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("이동 실패");
			UE_LOG(LogTemp, Error, TEXT("[WebSocket Subsystem] 아이템 이동 실패: %s"), *Message);

			// ★ [수정] 실패 시에도 기존 위치로 UI 원복을 위해 인벤토리 재요청
			RequestGetInventory();
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

void UWebSocketSubSystem::RequestEquipItem(const FGuid& ItemGuid, const FGuid& TargetParentGuid, bool bIsEquipped) 
{
	if (!WebSocket.IsValid() || !WebSocket->IsConnected()) return;
	UE_LOG(LogTemp, Warning, TEXT("server %s"), *TargetParentGuid.ToString());
	TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();
	PayloadObject->SetStringField(TEXT("ItemGuid"), ItemGuid.ToString(EGuidFormats::DigitsWithHyphens));
	PayloadObject->SetStringField(TEXT("TargetParentGuid"), TargetParentGuid.ToString(EGuidFormats::DigitsWithHyphens));
	PayloadObject->SetBoolField(TEXT("bIsEquipped"), bIsEquipped);

	SendJsonMessage(TEXT("REQ_EQUIP_ITEM"), PayloadObject);
}