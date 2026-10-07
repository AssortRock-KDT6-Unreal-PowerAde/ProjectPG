// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Common/TableData.h"
#include "Server/InventorySubSystem.h" // 💡 UInventorySubSystem 연동을 위해 포함
#include <Core/ItemSubSystem.h>
#include <Core/TableSubSystem.h>
#include "Net/UnrealNetwork.h"
#include "GameMode/GameMode_InGame.h"
#include "GameMode/PlayerController_InGame.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UInventoryComponent, PrivateInventoryState, COND_OwnerOnly);
	DOREPLIFETIME(UInventoryComponent, SharedInventoryState);
}

bool UInventoryComponent::IsServerManaged() const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	if (World->GetAuthGameMode()) return World->GetAuthGameMode()->IsA<AGameMode_InGame>();
	if (const AGameStateBase* State = World->GetGameState())
	{
		if (State->GameModeClass) return State->GameModeClass->IsChildOf(AGameMode_InGame::StaticClass());
	}
	return World->GetNetMode() == NM_Client;
}

bool UInventoryComponent::CanMutateInventory() const
{
	return !IsServerManaged() || (GetOwner() && GetOwner()->HasAuthority());
}

const FItemInstance* UInventoryComponent::FindItemByGuid(const FGuid& ItemGuid) const
{
	for (const auto& Pair : ItemsMap)
	{
		for (const FItemInstance& Item : Pair.Value.Items)
		{
			if (Item.GUID == ItemGuid) return &Item;
		}
	}
	return nullptr;
}

bool UInventoryComponent::IsDescendantContainer(const FGuid& ItemGuid, const FGuid& ContainerGuid) const
{
	if (!ItemGuid.IsValid()) return false;
	FGuid Current = ContainerGuid;
	TSet<FGuid> Visited;
	while (Current.IsValid())
	{
		if (Current == ItemGuid || Visited.Contains(Current)) return true;
		Visited.Add(Current);
		const FItemInstance* Parent = FindItemByGuid(Current);
		if (!Parent) break;
		Current = Parent->parent_inventory_guid;
	}
	return false;
}

bool UInventoryComponent::RequestServerMove(UInventoryComponent* Source, const FGuid& ItemGuid, const FGuid& TargetGuid, FIntPoint Position, bool bRotated)
{
	if (!IsValid(Source) || !bHasReceivedInitialSync || !Source->HasInitialInventory()) return false;
	APlayerController_InGame* Controller = nullptr;
	const APlayerState* State = Cast<APlayerState>(GetOwner());
	if (!State) State = Cast<APlayerState>(Source->GetOwner());
	if (State) Controller = Cast<APlayerController_InGame>(State->GetOwner());
	if (!Controller) Controller = Cast<APlayerController_InGame>(GetWorld()->GetFirstPlayerController());
	const FItemInstance* Item = Source->FindItemByGuid(ItemGuid);
	if (!Controller || !Controller->IsLocalController() || !Item) return false;
	Controller->Server_MoveInventoryItem(Source, this, ItemGuid, Item->parent_inventory_guid, TargetGuid, Position, bRotated);
	return true;
}

FInventorySnapshot UInventoryComponent::MakeSnapshot() const
{
	FInventorySnapshot Snapshot;
	Snapshot.bInitialized = bHasReceivedInitialSync;
	Snapshot.PocketGuid = PocketInventoryID;
	Snapshot.StashGuid = StashInventoryID;
	TArray<FGuid> Guids;
	InventorySizeMap.GetKeys(Guids);
	for (const auto& Pair : ItemsMap) Guids.AddUnique(Pair.Key);
	Guids.Sort();
	for (const FGuid& Guid : Guids)
	{
		FInventoryContainerSnapshot& Container = Snapshot.Containers.AddDefaulted_GetRef();
		Container.Guid = Guid;
		Container.Size = GetInventorySizeByGuid(Guid);
		Container.Items = GetItems(Guid);
		for (const auto& Slot : EquipSlotID)
		{
			if (Slot.Value == Guid) Container.EquipSlot = Slot.Key;
		}
	}
	return Snapshot;
}

void UInventoryComponent::PublishInventoryState()
{
	if (!IsServerManaged() || !GetOwner()->HasAuthority() || !bHasReceivedInitialSync) return;
	FInventorySnapshot& State = GetOwner()->IsA<APlayerState>() ? PrivateInventoryState : SharedInventoryState;
	State = MakeSnapshot();
	GetOwner()->FlushNetDormancy();
	GetOwner()->ForceNetUpdate();
}

void UInventoryComponent::ApplySnapshot(const FInventorySnapshot& Snapshot)
{
	InventorySizeMap.Empty();
	ItemsMap.Empty();
	InvenGridMap.Empty();
	EquipSlotID.Empty();
	PocketInventoryID = Snapshot.PocketGuid;
	StashInventoryID = Snapshot.StashGuid;
	bHasReceivedInitialSync = Snapshot.bInitialized;
	for (const FInventoryContainerSnapshot& Container : Snapshot.Containers)
	{
		InventorySizeMap.Add(Container.Guid, Container.Size);
		FItemArrayWrapper& Wrapper = ItemsMap.Add(Container.Guid);
		Wrapper.Items = Container.Items;
		if (Container.EquipSlot != EEquipSlot::MAX) EquipSlotID.Add(Container.EquipSlot, Container.Guid);
		for (FItemInstance& Item : Wrapper.Items)
		{
			Item.Owner = GetOwner();
			Item.inventory_guid = Item.GUID;
			Item.bEquip = Container.EquipSlot != EEquipSlot::MAX;
			if (const FItemTableRow* Data = GetItemData(Item.ItemID)) Item.type = Data->ItemType;
		}
		RebuildGridMapByGuid(Container.Guid);
	}
	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::OnRep_InventoryState()
{
	const FInventorySnapshot& State = GetOwner()->IsA<APlayerState>() ? PrivateInventoryState : SharedInventoryState;
	if (State.bInitialized) ApplySnapshot(State);
}

bool UInventoryComponent::InitializeFromSnapshot(const FInventorySnapshot& Snapshot, FString* OutError)
{
	if (OutError) OutError->Reset();
	auto Reject = [OutError](const FString& Reason)
	{
		if (OutError) *OutError = Reason;
		return false;
	};
	if (!GetOwner() || !GetOwner()->HasAuthority()) return Reject(TEXT("Inventory owner has no server authority"));
	if (bHasReceivedInitialSync) return Reject(TEXT("Inventory is already initialized"));
	if (!Snapshot.bInitialized) return Reject(TEXT("Lobby snapshot is not initialized"));
	if (!Snapshot.PocketGuid.IsValid() || !Snapshot.StashGuid.IsValid() || Snapshot.PocketGuid == Snapshot.StashGuid) return Reject(TEXT("Pocket/Stash GUIDs are missing or identical"));
	if (Snapshot.Containers.IsEmpty() || Snapshot.Containers.Num() > 128) return Reject(FString::Printf(TEXT("Invalid container count: %d"), Snapshot.Containers.Num()));
	TSet<FGuid> ContainerGuids;
	TSet<EEquipSlot> Slots;
	TMap<FGuid, const FItemInstance*> Items;
	int32 TotalCells = 0;
	for (const FInventoryContainerSnapshot& Container : Snapshot.Containers)
	{
		if (!Container.Guid.IsValid() || ContainerGuids.Contains(Container.Guid)) return Reject(FString::Printf(TEXT("Invalid or duplicate container GUID: %s"), *Container.Guid.ToString()));
		ContainerGuids.Add(Container.Guid);
		if (Container.Size.X <= 0 || Container.Size.Y <= 0 || Container.Size.X > 64 || Container.Size.Y > 64) return Reject(FString::Printf(TEXT("Invalid container size: Container=%s Size=%dx%d"), *Container.Guid.ToString(), Container.Size.X, Container.Size.Y));
		TotalCells += Container.Size.X * Container.Size.Y;
		if (TotalCells > 65536) return Reject(TEXT("Snapshot exceeds total grid cell limit"));
		if (Container.EquipSlot != EEquipSlot::MAX)
		{
			if (static_cast<uint8>(Container.EquipSlot) >= static_cast<uint8>(EEquipSlot::MAX) || Slots.Contains(Container.EquipSlot) || Container.Items.Num() > 1) return Reject(FString::Printf(TEXT("Invalid equipment slot: Container=%s Slot=%d Items=%d"), *Container.Guid.ToString(), static_cast<int32>(Container.EquipSlot), Container.Items.Num()));
			if (Container.Guid == Snapshot.PocketGuid || Container.Guid == Snapshot.StashGuid) return Reject(TEXT("Equipment slot shares a root inventory GUID"));
			Slots.Add(Container.EquipSlot);
		}
		TSet<int32> Occupied;
		for (const FItemInstance& Item : Container.Items)
		{
			if (!Item.GUID.IsValid() || Items.Contains(Item.GUID) || Item.parent_inventory_guid != Container.Guid || Items.Num() >= 2048) return Reject(FString::Printf(TEXT("Invalid item identity/parent or item limit: Item=%s Parent=%s Container=%s"), *Item.GUID.ToString(), *Item.parent_inventory_guid.ToString(), *Container.Guid.ToString()));
			const FItemTableRow* Data = GetItemData(Item.ItemID);
			if (!Data) return Reject(FString::Printf(TEXT("Item table row missing on server: ItemID=%s GUID=%s"), *Item.ItemID.ToString(), *Item.GUID.ToString()));
			if (Item.StackCount < 0 || !FMath::IsFinite(Item.Durability)) return Reject(FString::Printf(TEXT("Invalid item values: GUID=%s"), *Item.GUID.ToString()));
			Items.Add(Item.GUID, &Item);
			if (Container.EquipSlot != EEquipSlot::MAX)
			{
				if (Data->EquipSlotType != Container.EquipSlot) return Reject(FString::Printf(TEXT("Equipment slot mismatch: ItemID=%s Expected=%d Actual=%d"), *Item.ItemID.ToString(), static_cast<int32>(Data->EquipSlotType), static_cast<int32>(Container.EquipSlot)));
				continue;
			}
			const FIntPoint Size = Item.bIsRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;
			if (Size.X <= 0 || Size.Y <= 0 || Item.Position.X < 0 || Item.Position.Y < 0 || Item.Position.X > Container.Size.X - Size.X || Item.Position.Y > Container.Size.Y - Size.Y) return Reject(FString::Printf(TEXT("Item out of bounds: ItemID=%s GUID=%s Container=%s Pos=%d,%d ItemSize=%dx%d ContainerSize=%dx%d"), *Item.ItemID.ToString(), *Item.GUID.ToString(), *Container.Guid.ToString(), Item.Position.X, Item.Position.Y, Size.X, Size.Y, Container.Size.X, Container.Size.Y));
			for (int32 Y = 0; Y < Size.Y; ++Y)
			{
				for (int32 X = 0; X < Size.X; ++X)
				{
					const int32 Cell = (Item.Position.Y + Y) * Container.Size.X + Item.Position.X + X;
					if (Occupied.Contains(Cell)) return Reject(FString::Printf(TEXT("Overlapping items: GUID=%s Container=%s Cell=%d"), *Item.GUID.ToString(), *Container.Guid.ToString(), Cell));
					Occupied.Add(Cell);
				}
			}
		}
	}
	if (!ContainerGuids.Contains(Snapshot.PocketGuid) || !ContainerGuids.Contains(Snapshot.StashGuid)) return Reject(TEXT("Pocket or Stash container is absent from snapshot"));
	for (const auto& Pair : Items)
	{
		TSet<FGuid> Visited;
		const FItemInstance* Current = Pair.Value;
		while (Current)
		{
			if (Visited.Contains(Current->GUID)) return Reject(FString::Printf(TEXT("Cyclic inventory hierarchy: GUID=%s"), *Current->GUID.ToString()));
			Visited.Add(Current->GUID);
			const FItemInstance* const* Parent = Items.Find(Current->parent_inventory_guid);
			Current = Parent ? *Parent : nullptr;
		}
	}
	FInventorySnapshot InitialState = Snapshot;
	if (IsServerManaged() && GetOwner()->IsA<APlayerState>())
	{
		for (uint8 Index = 0; Index < static_cast<uint8>(EEquipSlot::MAX); ++Index)
		{
			const EEquipSlot Slot = static_cast<EEquipSlot>(Index);
			if (Slots.Contains(Slot)) continue;
			FInventoryContainerSnapshot& Container = InitialState.Containers.AddDefaulted_GetRef();
			do
			{
				Container.Guid = FGuid::NewGuid();
			} while (!Container.Guid.IsValid() || ContainerGuids.Contains(Container.Guid) || Items.Contains(Container.Guid));
			ContainerGuids.Add(Container.Guid);
			Container.Size = FIntPoint(1, 1);
			Container.EquipSlot = Slot;
		}
	}
	ApplySnapshot(InitialState);
	return true;
}

void UInventoryComponent::SetPocketInventoryID(const FGuid& InGuid)
{
	if (!CanMutateInventory()) return;
	PocketInventoryID = InGuid;
	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::SetStashInventoryID(const FGuid& InGuid)
{
	if (!CanMutateInventory()) return;
	StashInventoryID = InGuid;
	OnInventoryUpdated.Broadcast();
}

bool UInventoryComponent::AddItemAt(FItemInstance NewItem, FIntPoint TargetPos)
{
	if (!CanMutateInventory()) return false;
	const FGuid TargetGuid = NewItem.parent_inventory_guid;
	if (!TargetGuid.IsValid()) return false;

	const FItemTableRow* Data = GetItemData(NewItem.ItemID);
	if (!Data) return false;


	if (!CanPlaceItemByGuid(TargetGuid, NewItem.ItemID, TargetPos, NewItem.bIsRotated, NewItem.GUID))
	{
		return false;
	}

	// GUID 보정
	if (!NewItem.GUID.IsValid())
	{
		NewItem.GUID = FGuid::NewGuid();
	}

	NewItem.Position = TargetPos;
	NewItem.inventory_guid = NewItem.GUID;
	NewItem.bEquip = IsEquipContainer(TargetGuid);

	PurgeDuplicateGuidExcept(NewItem.GUID, TargetGuid);

	const FIntPoint NewItemSize = NewItem.bIsRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;
	PurgeOverlappingItems(TargetGuid, TargetPos, NewItemSize, NewItem.GUID);

	NewItem.Owner = GetOwner();
	NewItem.type = Data->ItemType;
	ItemsMap.FindOrAdd(TargetGuid).Items.Add(NewItem);
	if (IsServerManaged() && Data->ItemType == EItemType::Bag)
	{
		if (UTableSubSystem* Tables = UTableSubSystem::Get(GetWorld()))
		{
			if (const FItemBackpackTable* Bag = Tables->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), NewItem.ItemID))
			{
				RegisterContainer(NewItem.GUID, Bag->SlotSize);
			}
		}
	}
	RebuildGridMapByGuid(TargetGuid);
	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	OnInventoryUpdated.AddDynamic(this, &UInventoryComponent::PublishInventoryState);
	if (IsServerManaged())
	{
		if (GetOwner()->HasAuthority() && !GetOwner()->IsA<APlayerState>())
		{
			bHasReceivedInitialSync = true;
			PublishInventoryState();
		}
		return;
	}
	const APlayerState* OwnerState = Cast<APlayerState>(GetOwner());
	const APlayerController* Controller = OwnerState ? Cast<APlayerController>(OwnerState->GetOwner()) : nullptr;
	if (!Controller || !Controller->IsLocalController()) return;

	// 💡 WebSocketSubSystem 대신 UInventorySubSystem에 인벤토리 수신 델리게이트 바인딩
	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvenSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryComponent::HandleInventoryReceived);
		InvenSub->OnInventoryReceived.AddDynamic(this, &UInventoryComponent::HandleInventoryReceived);
		// 수신 바인딩 이후 이미 서버에서 받은 캐시 데이터가 있다면 즉시 재전파 요청
		InvenSub->ReplayCachedInventory();
	}
}

int32 UInventoryComponent::GetColumns(const FGuid& InvenGuid) const
{
	return GetInventorySizeByGuid(InvenGuid).X;
}

int32 UInventoryComponent::GetRows(const FGuid& InvenGuid) const
{
	return GetInventorySizeByGuid(InvenGuid).Y;
}

int32 UInventoryComponent::GetGridIndex(const FGuid& InvenGuid, int32 X, int32 Y) const
{
	int32 Cols = GetColumns(InvenGuid);
	int32 Rows = GetRows(InvenGuid);

	if (Cols <= 0 || Rows <= 0 || X < 0 || X >= Cols || Y < 0 || Y >= Rows)
	{
		return -1;
	}

	return (Y * Cols) + X;
}

void UInventoryComponent::RegisterContainer(const FGuid& ContainerGUID, FIntPoint ContainerSize)
{
	if (!CanMutateInventory()) return;
	if (!ContainerGUID.IsValid())
	{
		return;
	}

	if (ContainerSize.X <= 0 ||	ContainerSize.Y <= 0)
	{
		return;
	}

	bool bSizeChanged = false;

	if (!InventorySizeMap.Contains(ContainerGUID))
	{
		InventorySizeMap.Add(
			ContainerGUID,
			ContainerSize);

		bSizeChanged = true;
	}
	else
	{
		if (InventorySizeMap[ContainerGUID] != ContainerSize)
		{
			InventorySizeMap[ContainerGUID] =
				ContainerSize;

			bSizeChanged = true;
		}
	}

	ItemsMap.FindOrAdd(ContainerGUID);

	// Grid만 다시 계산
	RebuildGridMapByGuid(ContainerGUID);

	// 크기가 바뀌었거나 처음 등록된 경우에만 알림
	if (bSizeChanged)
	{
		OnInventoryUpdated.Broadcast();
	}
}

void UInventoryComponent::RegisterEquipSlot(EEquipSlot Slot, const FGuid& ContainerGuid)
{
	if (!CanMutateInventory() || Slot == EEquipSlot::MAX || !ContainerGuid.IsValid()) return;
	EquipSlotID.Add(Slot, ContainerGuid);
	RegisterContainer(ContainerGuid, FIntPoint(1, 1));
}

bool UInventoryComponent::IsEquipContainer(const FGuid& ContainerGuid) const
{
	for (const auto& Slot : EquipSlotID)
	{
		if (Slot.Value == ContainerGuid) return true;
	}
	return false;
}

void UInventoryComponent::UnregisterContainer(const FGuid& ContainerGUID)
{
	if (!CanMutateInventory()) return;
	if (!ContainerGUID.IsValid())
		return;


	InventorySizeMap.Remove(ContainerGUID);

	// ★ 실제 아이템 데이터 유지
	// ItemsMap.Remove(ContainerGUID);

	// ★ Grid 데이터도 유지
	// InvenGridMap.Remove(ContainerGUID);

	OnInventoryUpdated.Broadcast();
}

FIntPoint UInventoryComponent::GetInventorySizeByGuid(const FGuid& InvenGuid) const
{
	if (const FIntPoint* FoundSize = InventorySizeMap.Find(InvenGuid))
	{
		return *FoundSize;
	}
	return FIntPoint::ZeroValue;
}

const FItemTableRow* UInventoryComponent::GetItemData(FName ItemID) const
{
	if (UItemSubSystem* SubSystem = UItemSubSystem::Get(GetWorld()))
	{
		return SubSystem->GetItem(ItemID);
	}
	return nullptr;
}

const FItemInstance* UInventoryComponent::GetItemInstance(FName ItemID) const
{
	for (const auto& Pair : ItemsMap)
	{
		for (const FItemInstance& Item : Pair.Value.Items)
		{
			if (Item.ItemID == ItemID)
			{
				return &Item;
			}
		}
	}
	return nullptr;
}

const TArray<FItemInstance>& UInventoryComponent::GetItems(const FGuid& InvenGuid) const
{
	static const TArray<FItemInstance> EmptyArray;
	const FItemArrayWrapper* FoundWrapper = ItemsMap.Find(InvenGuid);
	return FoundWrapper ? FoundWrapper->Items : EmptyArray;
}

bool UInventoryComponent::CanPlaceItemByGuid(const FGuid& InvenGuid, const FName& ItemID, FIntPoint TargetPos, bool bRotated, FGuid IgnoreItemGUID)
{
	if (InvenGuid == IgnoreItemGUID) return false;
	if (IsDescendantContainer(IgnoreItemGUID, InvenGuid)) return false;
	for (const auto& Slot : EquipSlotID)
	{
		if (Slot.Value != InvenGuid) continue;
		const FItemTableRow* Data = GetItemData(ItemID);
		if (!Data || Data->EquipSlotType != Slot.Key || TargetPos != FIntPoint::ZeroValue) return false;
		for (const FItemInstance& Item : GetItems(InvenGuid))
		{
			if (Item.GUID != IgnoreItemGUID) return false;
		}
		return true;
	}

	FIntPoint InvenSize = GetInventorySizeByGuid(InvenGuid);
	if (InvenSize.X <= 0 || InvenSize.Y <= 0)
	{
		if (InvenGuid.IsValid())
		{
			for (const auto& Pair : ItemsMap)
			{
				for (const FItemInstance& Candidate : Pair.Value.Items)
				{
					if (Candidate.GUID == InvenGuid)
					{
						if (UTableSubSystem* TableSub = UTableSubSystem::Get(GetWorld()))
						{
							const FItemBackpackTable* BP = TableSub->FindTableRow<FItemBackpackTable>("BackpackTable", Candidate.ItemID);
							if (BP && BP->SlotSize.X > 0 && BP->SlotSize.Y > 0)
							{
								RegisterContainer(InvenGuid, FIntPoint(BP->SlotSize.X, BP->SlotSize.Y));
								InvenSize = BP->SlotSize;
								break;
							}
						}
					}
				}
				if (InvenSize.X > 0 && InvenSize.Y > 0) break;
			}
		}

		if (InvenSize.X <= 0 || InvenSize.Y <= 0) return false;
	}

	const FItemTableRow* Data = GetItemData(ItemID);
	if (!Data) return false;

	FIntPoint ItemSize = bRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;

	if (TargetPos.X < 0 || TargetPos.Y < 0 || (TargetPos.X + ItemSize.X) > InvenSize.X || (TargetPos.Y + ItemSize.Y) > InvenSize.Y)
	{
		return false;
	}

	const FIntArrayWrapper* GridWrapper = InvenGridMap.Find(InvenGuid);
	if (!GridWrapper || GridWrapper->Grid.Num() == 0)
	{
		return true;
	}

	const TArray<FItemInstance>& ItemList = GetItems(InvenGuid);

	for (int32 x = 0; x < ItemSize.X; ++x)
	{
		for (int32 y = 0; y < ItemSize.Y; ++y)
		{
			int32 Idx = GetGridIndex(InvenGuid, TargetPos.X + x, TargetPos.Y + y);
			if (Idx != -1 && GridWrapper->Grid.IsValidIndex(Idx))
			{
				int32 OccupiedItemIdx = GridWrapper->Grid[Idx];
				if (OccupiedItemIdx >= 0 && ItemList.IsValidIndex(OccupiedItemIdx))
				{
					if (IgnoreItemGUID.IsValid() && ItemList[OccupiedItemIdx].GUID == IgnoreItemGUID)
					{
						continue;
					}
					return false;
				}
			}
		}
	}

	return true;
}

bool UInventoryComponent::AddItem(FItemInstance NewItem)
{
	if (!CanMutateInventory()) return false;
	const FGuid TargetGuid = NewItem.parent_inventory_guid;
	const FIntPoint InvenSize = GetInventorySizeByGuid(TargetGuid);

	const FItemTableRow* Data = GetItemData(NewItem.ItemID);
	if (!Data) return false;

	for (int32 y = 0; y < InvenSize.Y; ++y)
	{
		for (int32 x = 0; x < InvenSize.X; ++x)
		{
			FIntPoint TestPos(x, y);
			if (CanPlaceItemByGuid(TargetGuid, NewItem.ItemID, TestPos, NewItem.bIsRotated))
			{
				NewItem.Position = TestPos;
			if (!NewItem.GUID.IsValid())
			{
				NewItem.GUID = FGuid::NewGuid();
			}

			PurgeDuplicateGuidExcept(NewItem.GUID, TargetGuid);

			NewItem.Owner = GetOwner();
			NewItem.inventory_guid = NewItem.GUID;
			NewItem.type = Data->ItemType;
			NewItem.bEquip = IsEquipContainer(TargetGuid);
			ItemsMap.FindOrAdd(TargetGuid).Items.Add(NewItem);
			if (IsServerManaged() && Data->ItemType == EItemType::Bag)
			{
				if (UTableSubSystem* Tables = UTableSubSystem::Get(GetWorld()))
				{
					if (const FItemBackpackTable* Bag = Tables->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), NewItem.ItemID))
					{
						RegisterContainer(NewItem.GUID, Bag->SlotSize);
					}
				}
			}
				RebuildGridMapByGuid(TargetGuid);
				OnInventoryUpdated.Broadcast();
				return true;
			}
		}
	}

	return false;
}

bool UInventoryComponent::AddItemByID(FName ItemID, const FGuid& TargetInvenGuid, int32 Quantity)
{
	FItemInstance NewItem;
	NewItem.ItemID = ItemID;
	NewItem.StackCount = Quantity;
	NewItem.parent_inventory_guid = TargetInvenGuid;

	return AddItem(NewItem);
}

bool UInventoryComponent::MoveItem(const FGuid& TargetInvenGuid, FGuid ItemGUID, FIntPoint NewPos, bool bNewRotated)
{
	if (IsServerManaged())
	{
		if (!GetOwner()->HasAuthority()) return RequestServerMove(this, ItemGUID, TargetInvenGuid, NewPos, bNewRotated);
		return TransferItemFrom(this, ItemGUID, TargetInvenGuid, NewPos, bNewRotated);
	}
	FGuid SourceGuid;
	int32 ItemIndex = -1;

	for (auto& Pair : ItemsMap)
	{
		for (int32 i = 0; i < Pair.Value.Items.Num(); ++i)
		{
			if (Pair.Value.Items[i].GUID == ItemGUID)
			{
				SourceGuid = Pair.Key;
				ItemIndex = i;
				break;
			}
		}
		if (ItemIndex != -1) break;
	}

	if (ItemIndex == -1) return false;

	FItemInstance Item = ItemsMap[SourceGuid].Items[ItemIndex];
	if (!CanPlaceItemByGuid(TargetInvenGuid, Item.ItemID, NewPos, bNewRotated, ItemGUID))
	{
		return false;
	}

	const FItemTableRow* MoveItemData = GetItemData(Item.ItemID);

	// 1. 기존 위치에서 삭제
	ItemsMap[SourceGuid].Items.RemoveAt(ItemIndex);

	// 2. 값 수정
	Item.Position = NewPos;
	Item.bIsRotated = bNewRotated;
	Item.parent_inventory_guid = TargetInvenGuid;
	Item.bEquip = IsEquipContainer(TargetInvenGuid);

	// 3. 타겟 위치에 추가
	// ★ 방어: 이동 직전에 동일 GUID가 다른 컨테이너에 유령으로 남아있다면 제거
	PurgeDuplicateGuidExcept(ItemGUID, TargetInvenGuid);
	if (MoveItemData)
	{
		const FIntPoint ItemSize = Item.bIsRotated ? FIntPoint(MoveItemData->GridSize.Y, MoveItemData->GridSize.X) : MoveItemData->GridSize;
		PurgeOverlappingItems(TargetInvenGuid, NewPos, ItemSize, ItemGUID);
	}
	ItemsMap.FindOrAdd(TargetInvenGuid).Items.Add(Item);

	// 4. 그리드 재구축
	RebuildGridMapByGuid(SourceGuid);
	RebuildGridMapByGuid(TargetInvenGuid);

	// 💡 5. 서버로 이동 패킷 전송 (UInventorySubSystem을 거치도록 수정 완료)
	// 로컬/서버 분기는 InventorySubSystem::IsLocalOnly()가 단독으로 결정한다.
	// 여기서는 조건을 따지지 말고 항상 RequestMoveItem을 호출한다.
	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		if (!InvenSub->IsLocalOnly() && !IsEquipContainer(TargetInvenGuid))
		{
			InvenSub->RequestMoveItem(SourceGuid, TargetInvenGuid, ItemGUID, NewPos, bNewRotated);
		}
	}

	// 6. UI 동기화 알림
	UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent] MoveItem: Source=%s Target=%s ItemGUID=%s ItemsInSource=%d ItemsInTarget=%d"), *SourceGuid.ToString(), *TargetInvenGuid.ToString(), *ItemGUID.ToString(), ItemsMap.FindRef(SourceGuid).Items.Num(), ItemsMap.FindRef(TargetInvenGuid).Items.Num());
	OnInventoryUpdated.Broadcast();

	return true;
}

bool UInventoryComponent::RemoveItemByGUID(const FGuid& ItemGUID, FItemInstance& OutItem)
{
	if (!CanMutateInventory()) return false;
	// 방어 코드: 어떤 경로로든 동일 GUID 항목이 이 컴포넌트의 여러 컨테이너(혹은 같은 컨테이너)에
	// 중복으로 남아있을 수 있으므로, 첫 번째 항목만 제거하고 끝내지 않고 발견되는 모든 중복 항목을
	// 제거한다. 그렇지 않으면 하나만 제거된 뒤 나머지 고스트 항목이 ItemsMap에 남아 있다가,
	// 이후 다른 아이템 이동으로 OnInventoryUpdated가 브로드캐스트될 때 다시 화면에 나타난다.
	bool bFoundAny = false;
	TArray<FGuid> ContainersTouched;

	for (auto& Pair : ItemsMap)
	{
		TArray<FItemInstance>& Items = Pair.Value.Items;
		for (int32 i = Items.Num() - 1; i >= 0; --i)
		{
			if (Items[i].GUID == ItemGUID)
			{
				if (!bFoundAny)
				{
					OutItem = Items[i];
					bFoundAny = true;
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("[InventoryComponent] RemoveItemByGUID: 중복된 GUID 항목 추가 발견 및 제거. ItemGUID=%s Container=%s"), *ItemGUID.ToString(), *Pair.Key.ToString());
				}

				Items.RemoveAt(i);
				ContainersTouched.AddUnique(Pair.Key);
			}
		}
	}

	if (!bFoundAny)
	{
		return false;
	}

	for (const FGuid& Guid : ContainersTouched)
	{
		RebuildGridMapByGuid(Guid);
	}

	OnInventoryUpdated.Broadcast();
	return true;
}

bool UInventoryComponent::RemoveItemByGUIDFromContainer(const FGuid& ContainerGuid, const FGuid& ItemGUID, FItemInstance& OutItem)
{
	if (!CanMutateInventory()) return false;
	FItemArrayWrapper* Wrapper = ItemsMap.Find(ContainerGuid);
	if (!Wrapper) return false;
	UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent] RemoveItemByGUIDFromContainer start Container=%s ItemGUID=%s Count=%d"), *ContainerGuid.ToString(), *ItemGUID.ToString(), Wrapper->Items.Num());

	// 방어 코드: 동일 컨테이너 내에 동일 GUID 항목이 중복으로 남아있을 수 있으므로
	// 첫 번째만 제거하지 않고 발견되는 모든 항목을 제거한다.
	TArray<FItemInstance>& Items = Wrapper->Items;
	bool bFoundAny = false;
	for (int32 i = Items.Num() - 1; i >= 0; --i)
	{
		if (Items[i].GUID == ItemGUID)
		{
			if (!bFoundAny)
			{
				OutItem = Items[i];
				bFoundAny = true;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[InventoryComponent] RemoveItemByGUIDFromContainer: 중복된 GUID 항목 추가 발견 및 제거. Container=%s ItemGUID=%s"), *ContainerGuid.ToString(), *ItemGUID.ToString());
			}

			Items.RemoveAt(i);
		}
	}

	if (!bFoundAny)
	{
		return false;
	}

	RebuildGridMapByGuid(ContainerGuid);
	OnInventoryUpdated.Broadcast();
	return true;
}

bool UInventoryComponent::TransferItemFrom(UInventoryComponent* SourceComp, const FGuid& ItemGUID, const FGuid& TargetInvenGuid, FIntPoint NewPos, bool bNewRotated)
{
	if (IsServerManaged())
	{
		if (!IsValid(SourceComp) || SourceComp->GetWorld() != GetWorld()) return false;
		if (!GetOwner()->HasAuthority()) return RequestServerMove(SourceComp, ItemGUID, TargetInvenGuid, NewPos, bNewRotated);
		if (!SourceComp->CanMutateInventory() || !bHasReceivedInitialSync || !SourceComp->bHasReceivedInitialSync) return false;
		const FItemInstance* Found = SourceComp->FindItemByGuid(ItemGUID);
		if (!Found || !InventorySizeMap.Contains(TargetInvenGuid)) return false;
		if (SourceComp->IsDescendantContainer(ItemGUID, TargetInvenGuid)) return false;
		if (SourceComp != this && FindItemByGuid(ItemGUID)) return false;
		if (!CanPlaceItemByGuid(TargetInvenGuid, Found->ItemID, NewPos, bNewRotated, ItemGUID)) return false;

		FItemInstance Item = *Found;
		const FGuid SourceGuid = Item.parent_inventory_guid;
		TArray<FGuid> ChildContainers;
		if (SourceComp != this)
		{
			for (const auto& Pair : SourceComp->ItemsMap)
			{
				if (Pair.Key == ItemGUID || SourceComp->IsDescendantContainer(ItemGUID, Pair.Key))
				{
					if (ItemsMap.Contains(Pair.Key) || InventorySizeMap.Contains(Pair.Key)) return false;
					for (const FItemInstance& Child : Pair.Value.Items)
					{
						if (FindItemByGuid(Child.GUID)) return false;
					}
					ChildContainers.Add(Pair.Key);
				}
			}
		}

		SourceComp->ItemsMap.FindChecked(SourceGuid).Items.RemoveAll([&ItemGUID](const FItemInstance& Entry) { return Entry.GUID == ItemGUID; });
		Item.parent_inventory_guid = TargetInvenGuid;
		Item.Position = NewPos;
		Item.bIsRotated = bNewRotated;
		Item.bEquip = false;
		Item.Owner = GetOwner();
		for (const auto& Slot : EquipSlotID) Item.bEquip |= Slot.Value == TargetInvenGuid;
		ItemsMap.FindOrAdd(TargetInvenGuid).Items.Add(Item);
		for (const FGuid& Guid : ChildContainers)
		{
			ItemsMap.Add(Guid, MoveTemp(SourceComp->ItemsMap.FindChecked(Guid)));
			SourceComp->ItemsMap.Remove(Guid);
			if (const FIntPoint* Size = SourceComp->InventorySizeMap.Find(Guid)) InventorySizeMap.Add(Guid, *Size);
			SourceComp->InventorySizeMap.Remove(Guid);
			SourceComp->InvenGridMap.Remove(Guid);
			for (FItemInstance& Child : ItemsMap.FindChecked(Guid).Items) Child.Owner = GetOwner();
			RebuildGridMapByGuid(Guid);
		}
		SourceComp->RebuildGridMapByGuid(SourceGuid);
		RebuildGridMapByGuid(TargetInvenGuid);
		if (SourceComp != this) SourceComp->OnInventoryUpdated.Broadcast();
		OnInventoryUpdated.Broadcast();
		return true;
	}
	if (!SourceComp || !TargetInvenGuid.IsValid())
	{
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent] TransferItemFrom start SourceComp=%p TargetComp=%p ItemGUID=%s TargetInven=%s Pos=(%d,%d) Rot=%d"), SourceComp, this, *ItemGUID.ToString(), *TargetInvenGuid.ToString(), NewPos.X, NewPos.Y, bNewRotated ? 1 : 0);

	// 동일 컴포넌트라면 기존 MoveItem 로직을 그대로 사용
	if (SourceComp == this)
	{
		return MoveItem(TargetInvenGuid, ItemGUID, NewPos, bNewRotated);
	}

	FItemInstance Item;
	if (!SourceComp->RemoveItemByGUID(ItemGUID, Item))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent] TransferItemFrom: Item not found in source component ItemGUID=%s"), *ItemGUID.ToString());
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("[InventoryComponent] TransferItemFrom removed from source ItemGUID=%s Parent=%s Type=%d"), *Item.GUID.ToString(), *Item.parent_inventory_guid.ToString(), (int32)Item.type);

	const FGuid SourceOwnerGuid = Item.parent_inventory_guid;

	if (!CanPlaceItemByGuid(TargetInvenGuid, Item.ItemID, NewPos, bNewRotated, ItemGUID))
	{
		SourceComp->AddItemAt(Item, Item.Position);
		return false;
	}

	Item.Position = NewPos;
	Item.bIsRotated = bNewRotated;
	Item.parent_inventory_guid = TargetInvenGuid;
	Item.bEquip = IsEquipContainer(TargetInvenGuid);

	PurgeDuplicateGuidExcept(ItemGUID, TargetInvenGuid);
	if (const FItemTableRow* TransferItemData = GetItemData(Item.ItemID))
	{
		const FIntPoint ItemSize = Item.bIsRotated ? FIntPoint(TransferItemData->GridSize.Y, TransferItemData->GridSize.X) : TransferItemData->GridSize;
		PurgeOverlappingItems(TargetInvenGuid, NewPos, ItemSize, ItemGUID);
	}
	ItemsMap.FindOrAdd(TargetInvenGuid).Items.Add(Item);
	RebuildGridMapByGuid(TargetInvenGuid);
	
	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		if (!InvenSub->IsLocalOnly() && !IsEquipContainer(TargetInvenGuid))
		{
			InvenSub->RequestMoveItem(SourceOwnerGuid, TargetInvenGuid, ItemGUID, NewPos, bNewRotated);
		}
	}

	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::SetServerInventoryData(const FInventoryMapWrapper InWrapper)
{
	if (IsServerManaged()) return;
	if (InWrapper.InventorySizeMap.Num() == 0 && InWrapper.InventoryMap.Num() == 0)
	{
		return;
	}

	// 로비에서는 서버 응답이 최신 상태이며, 별도 등록된 가방 크기는 유지한다.
	for (const auto& SizePair : InWrapper.InventorySizeMap)
	{
		InventorySizeMap.Add(SizePair.Key, SizePair.Value);
	}

	ItemsMap.Empty();
	InvenGridMap.Empty();

	if (InWrapper.StashGuid.IsValid()) StashInventoryID = InWrapper.StashGuid;
	if (InWrapper.PocketGuid.IsValid()) PocketInventoryID = InWrapper.PocketGuid;

	for (const auto& SizePair : InventorySizeMap)
	{
		ItemsMap.FindOrAdd(SizePair.Key);
		RebuildGridMapByGuid(SizePair.Key);
	}

	TSet<FGuid> SeenItemGuids;
	for (const auto& Pair : InWrapper.InventoryMap)
	{
		const FGuid& TargetGuid = Pair.Key;
		FItemArrayWrapper Wrapper = Pair.Value;

		for (int32 i = Wrapper.Items.Num() - 1; i >= 0; --i)
		{
			FItemInstance& Item = Wrapper.Items[i];
			Item.Owner = GetOwner();

			if (!Item.parent_inventory_guid.IsValid() || Item.parent_inventory_guid != TargetGuid)
			{
				Item.parent_inventory_guid = TargetGuid;
			}

			if (Item.GUID.IsValid())
			{
				if (SeenItemGuids.Contains(Item.GUID))
				{
					Wrapper.Items.RemoveAt(i);
					continue;
				}
				SeenItemGuids.Add(Item.GUID);
			}
		}

		ItemsMap.FindOrAdd(TargetGuid) = Wrapper;
		RebuildGridMapByGuid(TargetGuid);
	}

	EquipSlotID.Empty();
	const FGuid SlotGuids[] = { InWrapper.MainWeapon, InWrapper.SubWeapon, InWrapper.HelMet, InWrapper.Cloth, InWrapper.Pants, InWrapper.Shose, InWrapper.BackPack, InWrapper.Accuracy1, InWrapper.Accuracy2 };
	const UEquipComponent* Equipment = GetOwner() ? GetOwner()->FindComponentByClass<UEquipComponent>() : nullptr;
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotGuids); ++Index)
	{
		if (!SlotGuids[Index].IsValid()) continue;
		const EEquipSlot Slot = static_cast<EEquipSlot>(Index);
		EquipSlotID.Add(Slot, SlotGuids[Index]);
		InventorySizeMap.Add(SlotGuids[Index], FIntPoint(1, 1));
		ItemsMap.FindOrAdd(SlotGuids[Index]);
		if (const FItemInstance* Equipped = Equipment ? Equipment->GetEquipment(Slot) : nullptr)
		{
			FItemInstance Item = *Equipped;
			Item.parent_inventory_guid = SlotGuids[Index];
			Item.Position = FIntPoint::ZeroValue;
			Item.bEquip = true;
			PurgeDuplicateGuidExcept(Item.GUID, SlotGuids[Index]);
			ItemsMap.FindOrAdd(SlotGuids[Index]).Items.Add(Item);
		}
		RebuildGridMapByGuid(SlotGuids[Index]);
	}

	OnInventoryUpdated.Broadcast();
}


void UInventoryComponent::RebuildGridMapByGuid(const FGuid& InvenGuid)
{
	if (!InventorySizeMap.Contains(InvenGuid))
	{
		return;
	}

	const FIntPoint InvenSize = InventorySizeMap[InvenGuid];
	if (InvenSize.X <= 0 || InvenSize.Y <= 0) return;

	TArray<int32>& TargetGrid = InvenGridMap.FindOrAdd(InvenGuid).Grid;
	TargetGrid.Init(-1, InvenSize.X * InvenSize.Y);

	if (!ItemsMap.Contains(InvenGuid)) return;

	const TArray<FItemInstance>& ItemList = ItemsMap[InvenGuid].Items;

	for (int32 i = 0; i < ItemList.Num(); ++i)
	{
		const FItemInstance& Item = ItemList[i];
		const FItemTableRow* Data = GetItemData(Item.ItemID);

		if (!Data) continue;

		FIntPoint Size = Item.bIsRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;
		for (int32 x = 0; x < Size.X; ++x)
		{
			for (int32 y = 0; y < Size.Y; ++y)
			{
				int32 TargetX = Item.Position.X + x;
				int32 TargetY = Item.Position.Y + y;

				int32 MapIdx = GetGridIndex(InvenGuid, TargetX, TargetY);
				if (MapIdx != -1 && TargetGrid.IsValidIndex(MapIdx))
				{
					TargetGrid[MapIdx] = i;
				}
			}
		}
	}
}

void UInventoryComponent::HandleInventoryReceived(const FInventoryMapWrapper& InventoryMapWrapper)
{
	if (IsServerManaged()) return;

	SetServerInventoryData(InventoryMapWrapper);
	bHasReceivedInitialSync = true;
}



void UInventoryComponent::PurgeOverlappingItems(const FGuid& ContainerGuid, FIntPoint TargetPos, FIntPoint ItemSize, const FGuid& IgnoreItemGUID)
{
	if (!ContainerGuid.IsValid()) return;

	FItemArrayWrapper* Wrapper = ItemsMap.Find(ContainerGuid);
	if (!Wrapper) return;

	TArray<FItemInstance>& Items = Wrapper->Items;
	bool bRemovedAny = false;

	for (int32 i = Items.Num() - 1; i >= 0; --i)
	{
		const FItemInstance& Candidate = Items[i];

		if (IgnoreItemGUID.IsValid() && Candidate.GUID == IgnoreItemGUID)
		{
			continue;
		}

		const FItemTableRow* Data = GetItemData(Candidate.ItemID);
		if (!Data) continue;

		const FIntPoint CandidateSize = Candidate.bIsRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;

		const bool bOverlap =
			TargetPos.X < Candidate.Position.X + CandidateSize.X &&
			Candidate.Position.X < TargetPos.X + ItemSize.X &&
			TargetPos.Y < Candidate.Position.Y + CandidateSize.Y &&
			Candidate.Position.Y < TargetPos.Y + ItemSize.Y;

		if (bOverlap)
		{
			UE_LOG(LogTemp, Error, TEXT("[InventoryComponent] PurgeOverlappingItems: 겹치는 유령 아이템 발견 및 제거. Container=%s GhostGUID=%s IgnoreGUID=%s TargetPos=(%d,%d)"),
				*ContainerGuid.ToString(), *Candidate.GUID.ToString(), *IgnoreItemGUID.ToString(), TargetPos.X, TargetPos.Y);

			Items.RemoveAt(i);
			bRemovedAny = true;
		}
	}

	if (bRemovedAny)
	{
		RebuildGridMapByGuid(ContainerGuid);
	}
}

void UInventoryComponent::PurgeDuplicateGuidExcept(const FGuid& ItemGUID, const FGuid& ExceptContainerGuid)
{
	if (!ItemGUID.IsValid()) return;

	TArray<FGuid> ContainersTouched;

	for (auto& Pair : ItemsMap)
	{
		TArray<FItemInstance>& Items = Pair.Value.Items;
		for (int32 i = Items.Num() - 1; i >= 0; --i)
		{
			if (Items[i].GUID == ItemGUID)
			{
				UE_LOG(LogTemp, Error, TEXT("[InventoryComponent] PurgeDuplicateGuidExcept: 중복된 GUID 항목 발견 및 제거. ItemGUID=%s Container=%s (TargetContainer=%s)"),
					*ItemGUID.ToString(), *Pair.Key.ToString(), *ExceptContainerGuid.ToString());

				Items.RemoveAt(i);
				ContainersTouched.Add(Pair.Key);
			}
		}
	}

	for (const FGuid& Guid : ContainersTouched)
	{
		RebuildGridMapByGuid(Guid);
	}

}
