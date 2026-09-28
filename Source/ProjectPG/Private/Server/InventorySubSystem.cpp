#include "Server/InventorySubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include "Dom/JsonObject.h"
#include "Json.h"
#include "Core/ItemSubSystem.h"

UInventorySubSystem* UInventorySubSystem::Get(UWorld* World)
{
	if (!World) return nullptr;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		return GI->GetSubsystem<UInventorySubSystem>();
	}
	return nullptr;
}

void UInventorySubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UInventorySubSystem::Deinitialize()
{
	Super::Deinitialize();
}

void UInventorySubSystem::HandleInventoryMessage(const FString& MessageType, TSharedPtr<FJsonObject> PayloadObject)
{
	UE_LOG(LogTemp, Warning, TEXT("[InventorySubSystem] HandleInventoryMessage: Type=%s PayloadValid=%s"), *MessageType, PayloadObject.IsValid() ? TEXT("true") : TEXT("false"));
	if (!PayloadObject.IsValid()) return;

	if (MessageType == TEXT("INVENTORY_DATA"))
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

		auto ParseEquipSlot = [&](const TCHAR* FieldName, FGuid& OutGuid) {
			if (PayloadObject->TryGetStringField(FieldName, TempGuidStr) && !TempGuidStr.IsEmpty())
			{
				FGuid::Parse(TempGuidStr, OutGuid);
			}
			};
		ParseEquipSlot(TEXT("MainWeaponGuid"), InventoryMapWrapper.MainWeapon);
		ParseEquipSlot(TEXT("SubWeaponGuid"), InventoryMapWrapper.SubWeapon);
		ParseEquipSlot(TEXT("HelMetGuid"), InventoryMapWrapper.HelMet);
		ParseEquipSlot(TEXT("ClothGuid"), InventoryMapWrapper.Cloth);
		ParseEquipSlot(TEXT("PantsGuid"), InventoryMapWrapper.Pants);
		ParseEquipSlot(TEXT("ShoseGuid"), InventoryMapWrapper.Shose);
		ParseEquipSlot(TEXT("BackPackGuid"), InventoryMapWrapper.BackPack);
		ParseEquipSlot(TEXT("Accuracy1Guid"), InventoryMapWrapper.Accuracy1);
		ParseEquipSlot(TEXT("Accuracy2Guid"), InventoryMapWrapper.Accuracy2);

		EquipMapWrapper.MainWeapon = InventoryMapWrapper.MainWeapon;
		EquipMapWrapper.SubWeapon = InventoryMapWrapper.SubWeapon;
		EquipMapWrapper.HelMet = InventoryMapWrapper.HelMet;
		EquipMapWrapper.Cloth = InventoryMapWrapper.Cloth;
		EquipMapWrapper.Pants = InventoryMapWrapper.Pants;
		EquipMapWrapper.Shose = InventoryMapWrapper.Shose;
		EquipMapWrapper.BackPack = InventoryMapWrapper.BackPack;
		EquipMapWrapper.Accuracy1 = InventoryMapWrapper.Accuracy1;
		EquipMapWrapper.Accuracy2 = InventoryMapWrapper.Accuracy2;

		TMap<FGuid, FItemArrayWrapper> InventoryItems;
		TMap<FGuid, FItemArrayWrapper> EquipItems;

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
						int32 Cols = 0, Rows = 0;
						InvenObj->TryGetNumberField(TEXT("max_cols"), Cols);
						if (Cols == 0) InvenObj->TryGetNumberField(TEXT("cols"), Cols);
						InvenObj->TryGetNumberField(TEXT("max_rows"), Rows);
						if (Rows == 0) InvenObj->TryGetNumberField(TEXT("rows"), Rows);

						if (Cols > 0 && Rows > 0)
						{
							InventoryMapWrapper.InventorySizeMap.Add(InvenGuid, FIntPoint(Cols, Rows));
						}
						InventoryItems.FindOrAdd(InvenGuid);
					}
				}
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* ItemsArray;
		if (PayloadObject->TryGetArrayField(TEXT("items"), ItemsArray))
		{
			for (const TSharedPtr<FJsonValue>& ItemValue : *ItemsArray)
			{
				TSharedPtr<FJsonObject> ItemObject = ItemValue->AsObject();
				if (!ItemObject.IsValid()) continue;

				FItemInstance Item;
				FString ItemGuidStr;
				if (ItemObject->TryGetStringField(TEXT("guid"), ItemGuidStr))
				{
					FGuid::Parse(ItemGuidStr, Item.GUID);
				}

				FString ParentGuidStr;
				if (ItemObject->TryGetStringField(TEXT("parent_inventory_guid"), ParentGuidStr) && !ParentGuidStr.IsEmpty())
				{
					FGuid::Parse(ParentGuidStr, Item.parent_inventory_guid);
				}

				FString ItemIdStr;
				if (ItemObject->TryGetStringField(TEXT("item_id"), ItemIdStr))
				{
					Item.ItemID = FName(*ItemIdStr);
				}

				ItemObject->TryGetNumberField(TEXT("stack_count"), Item.StackCount);
				double TempDurability = 0.0;
				if (ItemObject->TryGetNumberField(TEXT("current_durability"), TempDurability))
				{
					Item.Durability = TempDurability;
				}

				ItemObject->TryGetNumberField(TEXT("pos_x"), Item.Position.X);
				ItemObject->TryGetNumberField(TEXT("pos_y"), Item.Position.Y);

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

				if (!Item.bEquip && Item.parent_inventory_guid.IsValid())
				{
					InventoryItems.FindOrAdd(Item.parent_inventory_guid).Items.Add(Item);
				}
				else if (Item.bEquip)
				{
					EquipItems.FindOrAdd(Item.parent_inventory_guid).Items.Add(Item);
				}
			}
		}

		InventoryMapWrapper.InventoryMap = InventoryItems;
		EquipMapWrapper.InventoryMap = EquipItems;

		// 캐시에 저장하고 브로드캐스트
		CachedInventory = InventoryMapWrapper;
		bHasCachedInventory = true;

		// 장착 데이터도 캐시로 저장 (EquipMapWrapper의 InventoryMap 사용)
		CachedEquip = EquipMapWrapper;
		bHasCachedEquip = true;

		// Broadcast equip first so EquipComponents can register containers (backpacks) before inventory UI rebuild
		OnEquipReceived.Broadcast(EquipMapWrapper);
		OnInventoryReceived.Broadcast(CachedInventory);

		// Initial inventory applied, stop waiting flag
		if (bWaitingForInitialInventory)
		{
			bWaitingForInitialInventory = false;
			UE_LOG(LogTemp, Log, TEXT("InventorySubSystem: Received initial INVENTORY_DATA, bWaitingForInitialInventory=false"));
		}
	}
	else if (MessageType == TEXT("RES_MOVE_ITEM"))
	{
		bool bSuccess = PayloadObject->GetBoolField(TEXT("success"));
		if (bSuccess)
		{
			RequestGetInventory();
		}
		else
		{
			RequestGetInventory();
		}
	}

	// 추가: 장착/해제 응답 처리 예시(서버에서 장착 응답 오는 경우에 대비)
	else if (MessageType == TEXT("RES_EQUIP_ITEM"))
	{
		bool bSuccess = false;
		if (PayloadObject->TryGetBoolField(TEXT("success"), bSuccess))
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventorySubSystem] RES_EQUIP_ITEM received success=%d"), bSuccess ? 1 : 0);
			// 성공 시 서버에서 최신 인벤토리/장착 데이터를 요청
			RequestGetInventory();
		}
	}
}

void UInventorySubSystem::ReplayCachedInventory()
{
	if (!bHasCachedInventory && !bHasCachedEquip) return;

	UE_LOG(LogTemp, Log, TEXT("InventorySubSystem::ReplayCachedInventory called. HasInventory=%d HasEquip=%d"), bHasCachedInventory ? 1 : 0, bHasCachedEquip ? 1 : 0);

	// Ensure equip handlers run before inventory handlers to allow container registration
	if (bHasCachedEquip)
	{
		OnEquipReceived.Broadcast(CachedEquip);
	}
	if (bHasCachedInventory)
	{
		OnInventoryReceived.Broadcast(CachedInventory);
	}
}


void UInventorySubSystem::RequestGetInventory()
{
	// WebSocket 사용이 비활성화된 경우 로컬 캐시가 존재하면 캐시를 바로 전파
	if (!bUseWebSocket)
	{
		ReplayCachedInventory();
		return;
	}
	// Mark that we are waiting for the server's initial inventory response. This prevents
	// UI or component code from prematurely sending moves to the server while the initial
	// server inventory is being delivered and applied locally.
	bWaitingForInitialInventory = true;

	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		WS->SendJsonMessage(TEXT("GET_INVENTORY"), Payload);
	}
}

void UInventorySubSystem::RequestMoveItem(const FGuid& FromInventoryGuid, const FGuid& ToInventoryGuid, const FGuid& ItemGuid, const FIntPoint& TargetPosition, bool bIsRotated)
{
	// 단일 정책 지점(IsLocalOnly)을 사용해 로컬/서버 분기를 결정한다.
	// 호출부(UI, InventoryComponent, EquipComponent)는 이 조건을 직접 검사하지 않고
	// 항상 RequestMoveItem을 호출하면 된다.
	if (IsLocalOnly())
	{
		UE_LOG(LogTemp, Warning, TEXT("InventorySubSystem: Local move ignored for cache transport Item=%s From=%s To=%s (local state already updated by component)"), *ItemGuid.ToString(), *FromInventoryGuid.ToString(), *ToInventoryGuid.ToString());
		bHasLocalChanges = true;
		return;
	}

	// Default: forward to WebSocket
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();
		PayloadObject->SetStringField(TEXT("FromInventoryGuid"), FromInventoryGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetStringField(TEXT("ToInventoryGuid"), ToInventoryGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetStringField(TEXT("ItemGuid"), ItemGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetNumberField(TEXT("TargetX"), TargetPosition.X);
		PayloadObject->SetNumberField(TEXT("TargetY"), TargetPosition.Y);
		PayloadObject->SetBoolField(TEXT("bIsRotated"), bIsRotated);

		WS->SendJsonMessage(TEXT("REQ_MOVE_ITEM"), PayloadObject);
	}
}

void UInventorySubSystem::RequestEquipItem(const FGuid& ItemGuid, const FGuid& TargetParentGuid, bool bIsEquipped)
{
	// 단일 정책 지점(IsLocalOnly)을 사용해 로컬/서버 분기를 결정한다. (RequestMoveItem과 동일 조건)
	if (IsLocalOnly())
	{
		UE_LOG(LogTemp, Warning, TEXT("InventorySubSystem: Local equip state handled by component Item=%s Target=%s (cache not mutated)"), *ItemGuid.ToString(), *TargetParentGuid.ToString());
		bHasLocalChanges = true;
		return;
	}

	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();
		PayloadObject->SetStringField(TEXT("ItemGuid"), ItemGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetStringField(TEXT("TargetParentGuid"), TargetParentGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetBoolField(TEXT("bIsEquipped"), bIsEquipped);

		bool bSent = false;
		if (WS)
		{
			bSent = WS->SendPayload(TEXT("REQ_EQUIP_ITEM"), PayloadObject);
		}
		UE_LOG(LogTemp, Warning, TEXT("[InventorySubSystem] RequestEquipItem: ItemGUID=%s Target=%s bIsEquipped=%d WebSocketAvailable=%s Sent=%s"), *ItemGuid.ToString(), *TargetParentGuid.ToString(), bIsEquipped ? 1 : 0, WS ? TEXT("true") : TEXT("false"), bSent ? TEXT("true") : TEXT("false"));
	}
}

void UInventorySubSystem::SetUseWebSocket(bool bUse)
{
	bUseWebSocket = bUse;
	UE_LOG(LogTemp, Log, TEXT("InventorySubSystem: SetUseWebSocket => %s"), bUse ? TEXT("true") : TEXT("false"));
}

void UInventorySubSystem::SetForceLocalMoves(bool bForce)
{
	bForceLocalMoves = bForce;
	UE_LOG(LogTemp, Log, TEXT("InventorySubSystem: SetForceLocalMoves => %s"), bForce ? TEXT("true") : TEXT("false"));
}

void UInventorySubSystem::ForceSaveToServer()
{
	if (!bHasCachedInventory)
	{
		UE_LOG(LogTemp, Warning, TEXT("ForceSaveToServer: no cached inventory to save."));
		return;
	}

	if (!bHasLocalChanges)
	{
		UE_LOG(LogTemp, Log, TEXT("ForceSaveToServer: no local changes to save."));
		return;
	}

	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();

		if (CachedInventory.StashGuid.IsValid()) Payload->SetStringField(TEXT("stashGuid"), CachedInventory.StashGuid.ToString(EGuidFormats::DigitsWithHyphens));
		if (CachedInventory.PocketGuid.IsValid()) Payload->SetStringField(TEXT("pocketGuid"), CachedInventory.PocketGuid.ToString(EGuidFormats::DigitsWithHyphens));

		auto SetEquipField = [&](const TCHAR* Name, const FGuid& Guid)
		{
			if (Guid.IsValid()) Payload->SetStringField(Name, Guid.ToString(EGuidFormats::DigitsWithHyphens));
		};
		SetEquipField(TEXT("MainWeaponGuid"), CachedInventory.MainWeapon);
		SetEquipField(TEXT("SubWeaponGuid"), CachedInventory.SubWeapon);
		SetEquipField(TEXT("HelMetGuid"), CachedInventory.HelMet);
		SetEquipField(TEXT("ClothGuid"), CachedInventory.Cloth);
		SetEquipField(TEXT("PantsGuid"), CachedInventory.Pants);
		SetEquipField(TEXT("ShoseGuid"), CachedInventory.Shose);
		SetEquipField(TEXT("BackPackGuid"), CachedInventory.BackPack);
		SetEquipField(TEXT("Accuracy1Guid"), CachedInventory.Accuracy1);
		SetEquipField(TEXT("Accuracy2Guid"), CachedInventory.Accuracy2);

		TArray<TSharedPtr<FJsonValue>> InventoriesArray;
		for (const auto& Pair : CachedInventory.InventorySizeMap)
		{
			TSharedPtr<FJsonObject> InvenObj = MakeShared<FJsonObject>();
			InvenObj->SetStringField(TEXT("inventory_id"), Pair.Key.ToString(EGuidFormats::DigitsWithHyphens));
			InvenObj->SetNumberField(TEXT("cols"), Pair.Value.X);
			InvenObj->SetNumberField(TEXT("rows"), Pair.Value.Y);
			InventoriesArray.Add(MakeShared<FJsonValueObject>(InvenObj));
		}
		Payload->SetArrayField(TEXT("inventories"), InventoriesArray);

		TArray<TSharedPtr<FJsonValue>> ItemsArray;
		for (const auto& Pair : CachedInventory.InventoryMap)
		{
			for (const FItemInstance& Item : Pair.Value.Items)
			{
				TSharedPtr<FJsonObject> ItemObj = MakeShared<FJsonObject>();
				if (Item.GUID.IsValid()) ItemObj->SetStringField(TEXT("guid"), Item.GUID.ToString(EGuidFormats::DigitsWithHyphens));
				if (Item.parent_inventory_guid.IsValid()) ItemObj->SetStringField(TEXT("parent_inventory_guid"), Item.parent_inventory_guid.ToString(EGuidFormats::DigitsWithHyphens));
				ItemObj->SetStringField(TEXT("item_id"), Item.ItemID.ToString());
				ItemObj->SetNumberField(TEXT("stack_count"), Item.StackCount);
				ItemObj->SetNumberField(TEXT("current_durability"), Item.Durability);
				ItemObj->SetNumberField(TEXT("pos_x"), Item.Position.X);
				ItemObj->SetNumberField(TEXT("pos_y"), Item.Position.Y);
				ItemObj->SetNumberField(TEXT("is_equipped"), Item.bEquip ? 1 : 0);
				ItemObj->SetBoolField(TEXT("bIsRotated"), Item.bIsRotated);

				ItemsArray.Add(MakeShared<FJsonValueObject>(ItemObj));
			}
		}
		Payload->SetArrayField(TEXT("items"), ItemsArray);

		if (WS->SendPayload(TEXT("SYNC_INVENTORY"), Payload))
		{
			UE_LOG(LogTemp, Log, TEXT("ForceSaveToServer: SYNC_INVENTORY sent."));
			bHasLocalChanges = false;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("ForceSaveToServer: WebSocket not connected. SYNC_INVENTORY not sent."));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ForceSaveToServer: WebSocket subsystem not available."));
	}
}