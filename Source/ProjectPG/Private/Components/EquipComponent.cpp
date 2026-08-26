// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/EquipComponent.h"
#include "Components/InventoryComponent.h"
#include "Core/ItemSubSystem.h"
#include "Actor/EquipActor.h"
#include "Core/TableSubSystem.h"
#include "Core/UIManagerSubSystem.h"
#include <Server/WebSocketSubSystem.h>
#include "GameFramework/PlayerState.h"

// Sets default values for this component's properties
UEquipComponent::UEquipComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

}


// Called when the game starts
void UEquipComponent::BeginPlay()
{
	Super::BeginPlay();

	// WebSocketSubsystem 가져오기 및 서버 수신 델리게이트 바인딩
	if (UWebSocketSubSystem* Subsystem = UWebSocketSubSystem::Get(GetWorld()))
	{
		Subsystem->OnEquipRecived.RemoveDynamic(this, &UEquipComponent::SetServerEquipData);

		Subsystem->OnEquipRecived.AddDynamic(this, &UEquipComponent::SetServerEquipData);
	}
}

bool UEquipComponent::Equip(const FItemInstance& Item)
{
	if (IsEquipped(Item.GUID))
	{
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("[EquipComponent] 장착 시도: %s"), *Item.ItemID.ToString());

	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return false;

	const FItemTableRow* ItemData = subSystem->GetItem(Item.ItemID);
	if (!ItemData) return false;

	EEquipSlot Slot = ItemData->EquipSlotType;
	if (Slot == EEquipSlot::MAX) return false;

	// 기존 장착 아이템이 있다면 해제 (무한 루프 방지를 위해 UnEquip 내부 Broadcast 억제 필요)
	if (Equipments.Contains(Slot))
	{
		UnEquip(Slot);
	}

	// 장착 맵에 추가
	Equipments.Add(Slot, Item);

	// 가방 등 추가 데이터 적용
	ApplyItemData(Item);

	FGuid TargetSlotGuid;
	for (const auto& Pair : EquipSlotGuids)
	{
		if (Pair.Value == Slot)
		{
			TargetSlotGuid = Pair.Key;
			break;
		}
	}

	// TargetSlotGuid must be valid
	if (!TargetSlotGuid.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[EquipComponent] 장착 실패: 슬롯 타입(%d)에 해당하는 유효한 TargetSlotGuid를 찾지 못했습니다!"), (int32)Slot);
		return false;
	}
	// 웹소켓 서버로 장착 패킷 전송
	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{

		WebSocketSub->RequestEquipItem(Item.GUID, TargetSlotGuid, true);
	}

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

bool UEquipComponent::UnEquip(EEquipSlot slot)
{
	if (!Equipments.Contains(slot))
		return false;

	FItemInstance RemovedItem = Equipments[slot];

	// 1. 데이터 및 액터 해제 작업을 먼저 진행
	RemoveItemData(slot);
	DestroyEquipActor(slot);

	// 2. 맵에서 완전 제거
	Equipments.Remove(slot);

	// 3. 웹소켓 서버로 해제 패킷 전송
	//if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	//{
	//	WebSocketSub->RequestEquipItem(RemovedItem.GUID, RemovedItem.parent_inventory_guid, false);
	//}

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
	if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
	{
		if (APlayerState* PS = PawnOwner->GetPlayerState())
		{
			return PS->GetComponentByClass<UInventoryComponent>();
		}
	}
	return nullptr;
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
		if (!IsValid(subsystem)) return;

		const FItemBackpackTable* data = subsystem->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), *Item.ItemID.ToString());
		if (!data) return;

		// InventoryComponent에 가방 컨테이너 등록
		if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
		{
			if (APlayerState* PS = PawnOwner->GetPlayerState())
			{
				if (UInventoryComponent* InvenComp = PS->GetComponentByClass<UInventoryComponent>())
				{
					InvenComp->RegisterContainer(Item.GUID, FIntPoint(data->SlotSize.X, data->SlotSize.Y));
				}
			}
		}
	}
}

void UEquipComponent::RemoveItemData( EEquipSlot slot)
{
	const FItemInstance* Item = GetEquipment(slot);
	if (!Item) return;

	if (slot == EEquipSlot::BackPack || Item->type == EItemType::Bag)
	{
		if (APawn* PawnOwner = Cast<APawn>(GetOwner()))
		{
			if (APlayerState* PS = PawnOwner->GetPlayerState())
			{
				if (UInventoryComponent* InvenComp = PS->GetComponentByClass<UInventoryComponent>())
				{
					InvenComp->UnregisterContainer(Item->GUID);
				}
			}
		}
	}
}

void UEquipComponent::SetServerEquipData(const FInventoryMapWrapper InWrapper)
{
	if (!InWrapper.BackPack.IsValid() && !InWrapper.MainWeapon.IsValid() /* ...다른 슬롯들도 체크... */)
	{
		UE_LOG(LogTemp, Error, TEXT("[SetServerEquipData] 유효하지 않은 맵퍼가 들어와서 무시합니다."));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("장착된 아이템이 갱신 "));
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

			Equip(TargetItem);
		}
	}
}

