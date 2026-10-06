#include "Server/InventorySubSystem.h"

#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Core/ItemSubSystem.h"
#include "Core/TableSubSystem.h"
#include "Common/TableData.h"
#include "GameMode/CustomPlayerState.h"
#include "GameFramework/PlayerController.h"

UInventorySubSystem* UInventorySubSystem::Get(UWorld* World)
{
	if (!World) return nullptr;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		return GI->GetSubsystem<UInventorySubSystem>();
	}
	return nullptr;
}

// 시작 짐 넣기.
// ① 창고·주머니 GUID 와 크기, 장비 칸 GUID 9개를 만들어 칸 이벤트로 쏜다
//    → 인벤토리 컴포넌트(SetServerInventoryData)·장비 컴포넌트(SetServerEquipData)가 칸을 만든다.
// ② 시작 짐 표의 줄마다 아이템을 만들어 넣는다. 창고·주머니는 인벤토리 컴포넌트 AddItem(빈 자리를 왼쪽 위부터 찾음),
//    장착은 장비 컴포넌트 Equip. 가방이면 가방 칸 크기도 등록한다(가방 표 BackpackTable).
int32 UInventorySubSystem::LoadStarterInventory(APlayerController* PlayerController)
{
	ACustomPlayerState* PlayerState = PlayerController ? PlayerController->GetPlayerState<ACustomPlayerState>() : nullptr;
	if (!PlayerState || !PlayerState->InvenComp || !PlayerState->EquipComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventorySubSystem] 플레이어 상태가 아직 없어 시작 짐을 못 넣었습니다."));
		return -1;
	}
	UInventoryComponent* Inventory = PlayerState->InvenComp;
	UEquipComponent* Equipment = PlayerState->EquipComp;
	if (Inventory->GetStashInventoryID().IsValid())
		return 0;

	UItemSubSystem* Items = UItemSubSystem::Get(this);
	UTableSubSystem* Tables = UTableSubSystem::Get(this);
	if (!Items || !Tables)
		return 0;

	// ① 칸 만들기
	FInventoryMapWrapper Containers;
	Containers.StashGuid = FGuid::NewGuid();
	Containers.PocketGuid = FGuid::NewGuid();
	Containers.InventorySizeMap.Add(Containers.StashGuid, StashSize);
	Containers.InventorySizeMap.Add(Containers.PocketGuid, PocketSize);
	Containers.InventoryMap.Add(Containers.StashGuid);
	Containers.InventoryMap.Add(Containers.PocketGuid);

	FInventoryMapWrapper Slots;
	for (FGuid* SlotGuid : { &Slots.MainWeapon, &Slots.SubWeapon, &Slots.HelMet, &Slots.Cloth, &Slots.Pants,
		&Slots.Shose, &Slots.BackPack, &Slots.Accuracy1, &Slots.Accuracy2 })
	{
		*SlotGuid = FGuid::NewGuid();
	}
	OnInventoryReceived.Broadcast(Containers);
	OnEquipReceived.Broadcast(Slots);
	// 장착 아이템의 부모 = 그 장비 칸의 GUID(서버 시절과 같은 약속).
	const TMap<EEquipSlot, FGuid> SlotGuidByType = {
		{ EEquipSlot::MainWeapon, Slots.MainWeapon }, { EEquipSlot::SubWeapon, Slots.SubWeapon },
		{ EEquipSlot::HelMet, Slots.HelMet }, { EEquipSlot::Cloth, Slots.Cloth }, { EEquipSlot::Pants, Slots.Pants },
		{ EEquipSlot::Shose, Slots.Shose }, { EEquipSlot::BackPack, Slots.BackPack },
		{ EEquipSlot::Accuracy1, Slots.Accuracy1 }, { EEquipSlot::Accuracy2, Slots.Accuracy2 } };

	// ② 시작 짐 넣기
	const UDataTable* StarterTable = Tables->FindTable(TEXT("StarterInventoryTable"));
	if (!StarterTable)
	{
		UE_LOG(LogTemp, Error, TEXT("[InventorySubSystem] TableLoader 에 StarterInventoryTable 이 없습니다 - 빈 인벤토리로 시작"));
		return 0;
	}
	TArray<FStarterInventoryRow*> Rows;
	StarterTable->GetAllRows<FStarterInventoryRow>(TEXT("LoadStarterInventory"), Rows);

	int32 Placed = 0;
	TArray<FString> Failed;
	for (const FStarterInventoryRow* Row : Rows)
	{
		const FItemTableRow* ItemData = Row ? Items->GetItem(Row->ItemID) : nullptr;
		if (!ItemData)
		{
			if (Row) Failed.Add(Row->ItemID.ToString());
			continue;
		}

		FItemInstance Item;
		Item.GUID = FGuid::NewGuid();
		Item.ItemID = Row->ItemID;
		Item.StackCount = FMath::Clamp(Row->Count, 1, FMath::Max(1, ItemData->MaxStack));
		Item.type = ItemData->ItemType;
		Item.Owner = PlayerState;

		bool bOk = false;
		if (Row->Container == EStarterContainer::Equip)
		{
			Item.bEquip = true;
			Item.parent_inventory_guid = SlotGuidByType.FindRef(ItemData->EquipSlotType);
			// 가방은 칸 크기를 먼저 등록한다. 장비 컴포넌트는 "폰의 플레이어 상태" 로 등록하는데 로비엔 폰이 없어서.
			if (Item.type == EItemType::Bag)
				if (const FItemBackpackTable* Bag = Tables->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), Item.ItemID))
					Inventory->RegisterContainer(Item.GUID, Bag->SlotSize);
			bOk = Equipment->Equip(Item);
		}
		else
		{
			Item.parent_inventory_guid = Row->Container == EStarterContainer::Pocket ? Containers.PocketGuid : Containers.StashGuid;
			bOk = Inventory->AddItem(Item);
		}
		if (bOk)
			++Placed;
		else
			Failed.Add(Row->ItemID.ToString());
	}

	UE_LOG(LogTemp, Display, TEXT("[InventorySubSystem] starter inventory: rows=%d placed=%d failed=[%s]"),
		Rows.Num(), Placed, *FString::Join(Failed, TEXT(",")));
	return Placed;
}
