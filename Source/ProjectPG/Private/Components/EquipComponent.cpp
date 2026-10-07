// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/EquipComponent.h"
#include "Components/InventoryComponent.h"
#include "Core/ItemSubSystem.h"
#include "Actor/EquipActor.h"
#include "Core/TableSubSystem.h"
#include "Core/UIManagerSubSystem.h"
#include <Server/WebSocketSubSystem.h>
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include <Server/InventorySubSystem.h>

// Sets default values for this component's properties
UEquipComponent::UEquipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}


// Called when the game starts
void UEquipComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UInventoryComponent* Inventory = GetOwnerInventoryComponent())
	{
		if (Inventory->IsServerManaged())
		{
			Inventory->OnInventoryUpdated.AddDynamic(this, &UEquipComponent::RefreshFromInventory);
			RefreshFromInventory();
			return;
		}
	}
	const APlayerState* OwnerState = Cast<APlayerState>(GetOwner());
	const APlayerController* Controller = OwnerState ? Cast<APlayerController>(OwnerState->GetOwner()) : nullptr;
	if (!Controller || !Controller->IsLocalController()) return;

	// 💡 WebSocketSubSystem 대신 UInventorySubSystem에 바인딩
	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvenSub->OnEquipReceived.RemoveDynamic(this, &UEquipComponent::SetServerEquipData);
		InvenSub->OnEquipReceived.AddDynamic(this, &UEquipComponent::SetServerEquipData);
		// Late binding 대비: 서버/서브시스템에 캐시된 장착 데이터를 즉시 재생
		InvenSub->ReplayCachedInventory();
	}
}

bool UEquipComponent::Equip(const FItemInstance& Item)
{
	return Equip(Item, nullptr);
}

bool UEquipComponent::Equip(const FItemInstance& Item, UInventoryComponent* SourceInventory)
{
	UInventoryComponent* Inventory = GetOwnerInventoryComponent();
	UInventoryComponent* Source = SourceInventory ? SourceInventory : Inventory;
	if (!Inventory || !IsValid(Source)) return false;
	const FItemInstance* Found = Source->FindItemByGuid(Item.GUID);
	if (!Found) return false;
	const FItemInstance SourceItem = *Found;
	const FItemTableRow* ItemData = Inventory->GetItemData(SourceItem.ItemID);
	if (!ItemData || ItemData->EquipSlotType == EEquipSlot::MAX) return false;
	const EEquipSlot Slot = ItemData->EquipSlotType;
	if (Inventory->IsServerManaged())
	{
		const FGuid* SlotGuid = Inventory->GetEquipSlotIDs().Find(Slot);
		return SlotGuid && Inventory->TransferItemFrom(Source, SourceItem.GUID, *SlotGuid, FIntPoint::ZeroValue, SourceItem.bIsRotated);
	}
	if (IsEquipped(SourceItem.GUID)) return true;
	FGuid TargetSlotGuid;
	for (const auto& Pair : EquipSlotGuids)
	{
		if (Pair.Value == Slot)
		{
			TargetSlotGuid = Pair.Key;
			break;
		}
	}

	if (!TargetSlotGuid.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[EquipComponent] 장착 실패: 슬롯 타입(%d)에 해당하는 유효한 TargetSlotGuid를 찾지 못했습니다!"), (int32)Slot);
		return false;
	}
	Inventory->RegisterEquipSlot(Slot, TargetSlotGuid);
	const FItemInstance* Previous = GetEquipment(Slot);
	const FGuid IgnoreGuid = Previous ? Previous->GUID : SourceItem.GUID;
	if (!Inventory->CanPlaceItemByGuid(TargetSlotGuid, SourceItem.ItemID, FIntPoint::ZeroValue, SourceItem.bIsRotated, IgnoreGuid)) return false;
	if (Previous && !UnEquip(Slot)) return false;
	if (!Inventory->TransferItemFrom(Source, SourceItem.GUID, TargetSlotGuid, FIntPoint::ZeroValue, SourceItem.bIsRotated))
	{
		UE_LOG(LogTemp, Warning, TEXT("[EquipComponent] 장착 이동 실패: GUID=%s"), *SourceItem.GUID.ToString());
		return false;
	}
	FItemInstance EquippedItem = SourceItem;
	EquippedItem.parent_inventory_guid = TargetSlotGuid;
	EquippedItem.Position = FIntPoint::ZeroValue;
	EquippedItem.bEquip = true;
	EquippedItem.type = ItemData->ItemType;
	Equipments.Add(Slot, EquippedItem);
	ApplyItemData(EquippedItem);

	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		if (!InvenSub->IsLocalOnly())
		{
			InvenSub->RequestEquipItem(SourceItem.GUID, TargetSlotGuid, true);
		}
	}

	// UI는 OnEquipmentChanged와 OnInventoryUpdated 델리게이트로 갱신됩니다.
	// 장착 변경 이벤트 전파 (UI 가 이 델리게이트 내부에서 다시 Equip을 부르지 않는지 확인 필요!)
	OnEquipmentChanged.Broadcast();
	return true;
}

bool UEquipComponent::UnEquip(const FItemInstance Item)
{
	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return false;

	const FItemTableRow* ItemData = subSystem->GetItem(Item.ItemID);
	if (!ItemData) return false;

	return UnEquip(ItemData->EquipSlotType);
}

bool UEquipComponent::UnEquip(EEquipSlot slot, bool bRestoreToInventory /*= true*/)
{
	UInventoryComponent* Inventory = GetOwnerInventoryComponent();
	const FItemInstance* Item = GetEquipment(slot);
	if (!Inventory || !Item || !bRestoreToInventory) return false;
	const FItemInstance EquippedItem = *Item;
	const FGuid Targets[] = { Inventory->GetPocketInventoryID(), Inventory->GetStashInventoryID() };
	for (const FGuid& Target : Targets)
	{
		const FIntPoint Size = Inventory->GetInventorySizeByGuid(Target);
		for (int32 Y = 0; Y < Size.Y; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				if (Inventory->CanPlaceItemByGuid(Target, EquippedItem.ItemID, FIntPoint(X, Y), EquippedItem.bIsRotated, EquippedItem.GUID))
				{
					return UnEquipTo(slot, Inventory, Target, FIntPoint(X, Y), EquippedItem.bIsRotated);
				}
			}
		}
	}
	return false;
}

bool UEquipComponent::UnEquipTo(EEquipSlot Slot, UInventoryComponent* TargetInventory, const FGuid& TargetGuid, FIntPoint Position, bool bRotated)
{
	UInventoryComponent* Inventory = GetOwnerInventoryComponent();
	const FItemInstance* Item = GetEquipment(Slot);
	if (!Inventory || !IsValid(TargetInventory) || !Item || TargetInventory->IsEquipContainer(TargetGuid)) return false;
	const FGuid ItemGuid = Item->GUID;
	if (!TargetInventory->TransferItemFrom(Inventory, ItemGuid, TargetGuid, Position, bRotated)) return false;
	if (Inventory->IsServerManaged()) return true;
	RemoveItemData(Slot);
	DestroyEquipActor(Slot);
	Equipments.Remove(Slot);
	if (UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld()))
	{
		if (!Subsystem->IsLocalOnly()) Subsystem->RequestEquipItem(ItemGuid, TargetGuid, false);
	}
	OnEquipmentChanged.Broadcast();
	return true;
}

bool UEquipComponent::Swap(EEquipSlot slot1, EEquipSlot slot2)
{
	if (const UInventoryComponent* Inventory = GetOwnerInventoryComponent())
	{
		if (Inventory->IsServerManaged()) return false;
	}
	if (!Equipments.Contains(slot1) ||
		!Equipments.Contains(slot2))
		return false;
	FItemInstance Temp = Equipments[slot1];

	Equipments[slot1] = Equipments[slot2];

	Equipments[slot2] = Temp;

	return true;
}

bool UEquipComponent::IsEquipped(const FGuid& Guid)
{
	for (const auto& Pair : Equipments)
	{
		if (Pair.Value.GUID == Guid)
			return true;
	}
	return false;
}

bool UEquipComponent::CanEquip(const FItemInstance& Item, EEquipSlot slot) const
{
	const UInventoryComponent* Inventory = GetOwnerInventoryComponent();
	const FItemTableRow* Data = Inventory ? Inventory->GetItemData(Item.ItemID) : nullptr;
	return Data && slot != EEquipSlot::MAX && Data->EquipSlotType == slot;
}

AEquipActor* UEquipComponent::GetEquipActor(EEquipSlot slot) const
{
	const TObjectPtr<AEquipActor>* Found = EquipActors.Find(slot);
	if (Found && IsValid(*Found))
	{
		return *Found;
	}
	return nullptr;
}

const FItemInstance* UEquipComponent::GetEquipment(EEquipSlot slot) const
{
	const FItemInstance* FoundEquipment = Equipments.Find(slot);
	return FoundEquipment;
}

void UEquipComponent::CopyFrom(UEquipComponent* Other)
{
	if (const UInventoryComponent* Inventory = GetOwnerInventoryComponent())
	{
		if (Inventory->IsServerManaged()) return;
	}
	if (!Other)
		return;

	for (auto& Pair : EquipActors)
	{
		if (Pair.Value)
			Pair.Value->Destroy();
	}

	EquipActors.Empty();

	Equipments.Empty();

	Equipments = Other->Equipments;

	UItemSubSystem* ItemSystem =
		UItemSubSystem::Get(GetWorld());

	if (!ItemSystem)
		return;

	for (auto& Pair : Equipments)
	{
		const FItemTableRow* Item =
			ItemSystem->GetItem(Pair.Value.ItemID);

		if (nullptr == Item) continue;

		SpawnEquipActor(
			Pair.Key,
			Item->WorldMesh);
	}
}

UInventoryComponent* UEquipComponent::GetOwnerInventoryComponent() const
{
	if (!GetOwner()) return nullptr;
	if (UInventoryComponent* Inventory = GetOwner()->FindComponentByClass<UInventoryComponent>()) return Inventory;
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		if (APlayerState* PS = PawnOwner->GetPlayerState())
		{
			return PS->GetComponentByClass<UInventoryComponent>();
		}
	}
	return nullptr;
}

void UEquipComponent::RegisterGuid(EEquipSlot slottype, FGuid guid)
{
	if (!guid.IsValid() || slottype == EEquipSlot::MAX) return;
	EquipSlotGuids.Add(guid, slottype);
	if (UInventoryComponent* Inventory = GetOwnerInventoryComponent()) Inventory->RegisterEquipSlot(slottype, guid);
}

void UEquipComponent::RefreshFromInventory()
{
	const UInventoryComponent* Inventory = GetOwnerInventoryComponent();
	if (!Inventory || !Inventory->HasInitialInventory()) return;
	Equipments.Empty();
	EquipSlotGuids.Empty();
	for (const auto& Slot : Inventory->GetEquipSlotIDs())
	{
		EquipSlotGuids.Add(Slot.Value, Slot.Key);
		const TArray<FItemInstance>& Items = Inventory->GetItems(Slot.Value);
		if (!Items.IsEmpty()) Equipments.Add(Slot.Key, Items[0]);
	}
	OnEquipmentChanged.Broadcast();
}

void UEquipComponent::SpawnEquipActor(EEquipSlot Slot, UStaticMesh* Mesh)
{

	const FItemInstance* Item = Equipments.Find(Slot);

	if (!Item)
		return;

	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return;

	const FEquipTableRow* ItemData =
		subSystem->GetEquip(Item->ItemID);

	if (!ItemData)
		return;

	AEquipActor* EquipItem =
		GetWorld()->SpawnActor<AEquipActor>(
			ItemData->EquipActorClass);
	if (!EquipItem)
		return;
	EquipItem->SetWorldMesh(Mesh);
	EquipItem->Equip(Cast<ACharacter>(GetOwner()), ItemData->SocketName);

	EquipActors.Add(Slot, EquipItem);
}

void UEquipComponent::DestroyEquipActor(EEquipSlot Slot)
{
	TObjectPtr<AEquipActor> EquipActor = EquipActors.FindRef(Slot);

	if (!IsValid(EquipActor))
		return;

	EquipActor->Unequip();
	EquipActor->Destroy();

	EquipActors.Remove(Slot);
}

void UEquipComponent::ApplyItemData(const FItemInstance& Item)
{
	if (Item.type == EItemType::Bag)
	{
		UE_LOG(LogTemp, Warning, TEXT("[EquipComponent] ApplyItemData for Bag called: ItemGUID=%s ItemID=%s"), *Item.GUID.ToString(), *Item.ItemID.ToString());
		UTableSubSystem* subsystem = UTableSubSystem::Get(GetWorld());
		if (!IsValid(subsystem)) return;

		const FItemBackpackTable* data = subsystem->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), *Item.ItemID.ToString());
		if (!data) return;

		// InventoryComponent에 가방 컨테이너 등록
		if (UInventoryComponent* InvenComp = GetOwnerInventoryComponent())
		{
			InvenComp->RegisterContainer(Item.GUID, FIntPoint(data->SlotSize.X, data->SlotSize.Y));
		}
	}
}

void UEquipComponent::RemoveItemData( EEquipSlot slot)
{
	const FItemInstance* Item = GetEquipment(slot);
	if (!Item) return;

	if (slot == EEquipSlot::BackPack || Item->type == EItemType::Bag)
	{
		// backpack 컨테이너는 item GUID에 종속되므로, 장착 해제/재장착 사이에
		// child item 상태를 보존하기 위해 즉시 해제하지 않는다.
		// 실제 제거는 아이템 자체가 소멸하거나 다른 GUID로 교체될 때 별도 처리한다.
		return;
	}
}

void UEquipComponent::SetServerEquipData(const FInventoryMapWrapper& InWrapper)
{
	if (const UInventoryComponent* Inventory = GetOwnerInventoryComponent())
	{
		if (Inventory->IsServerManaged()) return;
	}
	bHasReceivedInitialEquipSync = true;

	UE_LOG(LogTemp, Warning, TEXT("장착된 아이템이 갱신 "));
	for (const auto& Pair : Equipments) RemoveItemData(Pair.Key);
	for (const auto& Pair : EquipActors)
	{
		if (IsValid(Pair.Value)) Pair.Value->Destroy();
	}
	Equipments.Empty();
	EquipSlotGuids.Empty();
	EquipActors.Empty();

	// 1. 각 장비 슬롯 GUID 매핑 등록
	if (InWrapper.MainWeapon.IsValid()) EquipSlotGuids.Add(InWrapper.MainWeapon, EEquipSlot::MainWeapon);
	if (InWrapper.SubWeapon.IsValid())	EquipSlotGuids.Add(InWrapper.SubWeapon, EEquipSlot::SubWeapon);
	if (InWrapper.HelMet.IsValid())		EquipSlotGuids.Add(InWrapper.HelMet, EEquipSlot::HelMet);
	if (InWrapper.Cloth.IsValid())		EquipSlotGuids.Add(InWrapper.Cloth, EEquipSlot::Cloth);
	if (InWrapper.Pants.IsValid())		EquipSlotGuids.Add(InWrapper.Pants, EEquipSlot::Pants);
	if (InWrapper.Shose.IsValid())		EquipSlotGuids.Add(InWrapper.Shose, EEquipSlot::Shose);
	if (InWrapper.BackPack.IsValid())	EquipSlotGuids.Add(InWrapper.BackPack, EEquipSlot::BackPack);
	if (InWrapper.Accuracy1.IsValid())	EquipSlotGuids.Add(InWrapper.Accuracy1, EEquipSlot::Accuracy1);
	if (InWrapper.Accuracy2.IsValid())	EquipSlotGuids.Add(InWrapper.Accuracy2, EEquipSlot::Accuracy2);

	if (InWrapper.InventoryMap.Num() <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("장착된 아이템이 없습니다. "));
		OnEquipmentChanged.Broadcast();
		return;
	}

	for (const auto& Pair : InWrapper.InventoryMap)
	{
		const FGuid& guid = Pair.Key;
		const FItemArrayWrapper& wrapper = Pair.Value;

		// ★ [핵심 체크] 유효하지 않은(Zero) GUID이거나 슬롯 맵에 없는 경우 패스
		if (!guid.IsValid() || guid == FGuid(0, 0, 0, 0))
		{
			UE_LOG(LogTemp, Error, TEXT("SetServerEquipData: 유효하지 않은 장비 슬롯 GUID 감지됨!"));
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("SetServerEquipData 슬롯 확인: %s"), *guid.ToString());

		if (!EquipSlotGuids.Contains(guid)) {
			UE_LOG(LogTemp, Warning, TEXT("슬롯에 해당하는 guid를 EquipSlotGuids에서 찾을 수 없습니다: %s"), *guid.ToString());
			continue;
		}

		if (wrapper.Items.Num() > 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("장착 아이템 적용 성공: %s"), *wrapper.Items[0].ItemID.ToString());

			// 아이템 인스턴스의 parent_inventory_guid가 유실되었다면 현재 슬롯 GUID로 강제 보정
			FItemInstance TargetItem = wrapper.Items[0];
			if (!TargetItem.parent_inventory_guid.IsValid() || TargetItem.parent_inventory_guid == FGuid(0, 0, 0, 0))
			{
				TargetItem.parent_inventory_guid = guid;
			}

			TargetItem.Owner = GetOwner();
			TargetItem.bEquip = true;
			Equipments.Add(EquipSlotGuids.FindChecked(guid), TargetItem);
			ApplyItemData(TargetItem);
		}
	}
	OnEquipmentChanged.Broadcast();
}

