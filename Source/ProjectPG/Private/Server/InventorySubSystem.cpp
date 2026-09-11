#include "Server/InventorySubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include "Dom/JsonObject.h"
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

		OnInventoryReceived.Broadcast(InventoryMapWrapper);
		OnEquipReceived.Broadcast(EquipMapWrapper);
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
}

void UInventorySubSystem::RequestGetInventory()
{
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		WS->SendJsonMessage(TEXT("GET_INVENTORY"), Payload);
	}
}

void UInventorySubSystem::RequestMoveItem(const FGuid& FromInventoryGuid, const FGuid& ToInventoryGuid, const FGuid& ItemGuid, const FIntPoint& TargetPosition, bool bIsRotated)
{
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
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		TSharedPtr<FJsonObject> PayloadObject = MakeShared<FJsonObject>();
		PayloadObject->SetStringField(TEXT("ItemGuid"), ItemGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetStringField(TEXT("TargetParentGuid"), TargetParentGuid.ToString(EGuidFormats::DigitsWithHyphens));
		PayloadObject->SetBoolField(TEXT("bIsEquipped"), bIsEquipped);

		WS->SendJsonMessage(TEXT("REQ_EQUIP_ITEM"), PayloadObject);
	}
}