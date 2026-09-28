// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/InventoryComponent.h"
#include "Common/TableData.h"
#include "Server/InventorySubSystem.h" // 💡 UInventorySubSystem 연동을 위해 포함
#include <Core/ItemSubSystem.h>
#include <Core/TableSubSystem.h>
#include "Net/UnrealNetwork.h"
UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UInventoryComponent::AddItemAt(FItemInstance NewItem, FIntPoint TargetPos)
{
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

	PurgeDuplicateGuidExcept(NewItem.GUID, TargetGuid);

	const FIntPoint NewItemSize = NewItem.bIsRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;
	PurgeOverlappingItems(TargetGuid, TargetPos, NewItemSize, NewItem.GUID);

	ItemsMap.FindOrAdd(TargetGuid).Items.Add(NewItem);	
	RebuildGridMapByGuid(TargetGuid);
	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

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

void UInventoryComponent::UnregisterContainer(const FGuid& ContainerGUID)
{
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

			ItemsMap.FindOrAdd(TargetGuid).Items.Add(NewItem);
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
		if (!InvenSub->IsLocalOnly())
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
		if (!InvenSub->IsLocalOnly())
		{
			InvenSub->RequestMoveItem(SourceOwnerGuid, TargetInvenGuid, ItemGUID, NewPos, bNewRotated);
		}
	}

	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::SetServerInventoryData(const FInventoryMapWrapper InWrapper)
{
	if (InWrapper.InventorySizeMap.Num() == 0 && InWrapper.InventoryMap.Num() == 0)
	{
		return;
	}

	// Merge server-provided inventory size map with local registrations.
	// Do not blindly overwrite locally-registered container sizes (e.g., actor-local containers)
	if (InWrapper.InventorySizeMap.Num() > 0)
	{
		for (const auto& SizePair : InWrapper.InventorySizeMap)
		{
			const FGuid& Guid = SizePair.Key;
			const FIntPoint& ServerSize = SizePair.Value;

			if (!InventorySizeMap.Contains(Guid))
			{
				InventorySizeMap.FindOrAdd(Guid, ServerSize);
				UE_LOG(LogTemp, Warning, TEXT("SetServerInventoryData: Added server container size GUID=%s Size=(%d,%d)"), *Guid.ToString(), ServerSize.X, ServerSize.Y);
			}
			else
			{
				const FIntPoint LocalSize = InventorySizeMap[Guid];
				if (LocalSize != ServerSize)
				{
					// Keep local size and report conflict for investigation
					UE_LOG(LogTemp, Warning, TEXT("SetServerInventoryData: Size conflict GUID=%s Local=(%d,%d) Server=(%d,%d) - keeping Local"), *Guid.ToString(), LocalSize.X, LocalSize.Y, ServerSize.X, ServerSize.Y);
				}
			}
		}
	}


	for (const auto& Pair : InWrapper.InventoryMap)
	{
		ItemsMap.Remove(Pair.Key);
		InvenGridMap.Remove(Pair.Key);
	}

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

	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		if (bHasReceivedInitialSync && InvenSub->IsLocalOnly())
		{
			UE_LOG(LogTemp, Log, TEXT("[InventoryComponent] HandleInventoryReceived: Ignored stale replay/snapshot while LocalOnly and already synced once."));
			return;
		}
	}

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
