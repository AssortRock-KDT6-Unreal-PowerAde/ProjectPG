// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/EquipComponent.h"
#include "Core/ItemSubSystem.h"
#include "Actor/EquipActor.h"
#include "Core/TableSubSystem.h"
#include "Core/UIManagerSubSystem.h"
// Sets default values for this component's properties
UEquipComponent::UEquipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

}


// Called when the game starts
void UEquipComponent::BeginPlay()
{
	Super::BeginPlay();
	
}

bool UEquipComponent::Equip(const FItemInstance& Item)
{
	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return false;

	const FItemTableRow* ItemData =
		subSystem->GetItem(Item.ItemID);

	if (!ItemData)
		return false;

	EEquipSlot Slot = ItemData->EquipSlot;
	
	if (Slot == EEquipSlot::MAX) return false;

	if (Equipments.Contains(Slot))
	{
		UnEquip(Slot);
	}
	Equipments.Add(Slot, Item);

	ApplyItemData(Item);

	//SpawnEquipActor(Slot, ItemData->WorldMesh);
	OnEquipmentChanged.Broadcast();
	return true;

}

bool UEquipComponent::UnEquip(const FItemInstance Item)
{
	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return false;

	const FItemTableRow* ItemData =
		subSystem->GetItem(Item.ItemID);

	EEquipSlot Slot = ItemData->EquipSlot;
	if (UnEquip(Slot))
		return true;

	return false;
}

bool UEquipComponent::UnEquip(EEquipSlot slot)
{
	if (!Equipments.Contains(slot))
		return false;

	RemoveItemData(slot);

	DestroyEquipActor(slot);

	Equipments.Remove(slot);
	OnEquipmentChanged.Broadcast();

	return true;
}

bool UEquipComponent::Swap(EEquipSlot slot1, EEquipSlot slot2)
{
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
	UItemSubSystem* ItemSystem =
		UItemSubSystem::Get(GetWorld());

	const FEquipTableRow* EquipData =
		ItemSystem->GetEquip(Item.ItemID);

	if (!EquipData)
		return false;

	return EquipData->EquipType == slot;
}

AEquipActor* UEquipComponent::GetEquipActor(EEquipSlot slot) const
{
	return nullptr;
}

const FItemInstance* UEquipComponent::GetEquipment(EEquipSlot slot) const
{
	const FItemInstance* FoundEquipment = Equipments.Find(slot);
	return FoundEquipment;
}

void UEquipComponent::CopyFrom(UEquipComponent* Other)
{
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
		UTableSubSystem* subsystem = UTableSubSystem::Get(GetWorld());
		if (IsValid(subsystem)) return;

		const FItemBackpackTable* data = subsystem->FindTableRow< FItemBackpackTable>(TEXT("BackpackTable"),*Item.ItemID.ToString());
		if (nullptr == data) return;



	}
}

void UEquipComponent::RemoveItemData( EEquipSlot slot)
{

}




