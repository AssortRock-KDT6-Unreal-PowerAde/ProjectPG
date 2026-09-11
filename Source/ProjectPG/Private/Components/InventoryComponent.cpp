// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/InventoryComponent.h"
#include "Common/TableData.h"
#include "Server/InventorySubSystem.h" // 💡 UInventorySubSystem 연동을 위해 포함
#include <Core/ItemSubSystem.h>
#include <Core/TableSubSystem.h>

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

	// 배치 가능 여부 검사
	if (!CanPlaceItemByGuid(TargetGuid, NewItem.ItemID, TargetPos, NewItem.bIsRotated))
	{
		return false;
	}

	// GUID 보정
	if (!NewItem.GUID.IsValid())
	{
		NewItem.GUID = FGuid::NewGuid();
	}

	NewItem.Position = TargetPos;

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
	if (!ContainerGUID.IsValid() || ContainerSize.X <= 0 || ContainerSize.Y <= 0) return;

	if (InventorySizeMap.Contains(ContainerGUID) && InventorySizeMap[ContainerGUID] == ContainerSize)
	{
		return;
	}

	InventorySizeMap.FindOrAdd(ContainerGUID) = ContainerSize;
	ItemsMap.FindOrAdd(ContainerGUID);
	RebuildGridMapByGuid(ContainerGUID);

	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::UnregisterContainer(const FGuid& ContainerGUID)
{
	if (!ContainerGUID.IsValid()) return;

	InventorySizeMap.Remove(ContainerGUID);
	ItemsMap.Remove(ContainerGUID);
	InvenGridMap.Remove(ContainerGUID);

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

	// 1. 기존 위치에서 삭제
	ItemsMap[SourceGuid].Items.RemoveAt(ItemIndex);

	// 2. 값 수정
	Item.Position = NewPos;
	Item.bIsRotated = bNewRotated;
	Item.parent_inventory_guid = TargetInvenGuid;

	// 3. 타겟 위치에 추가
	ItemsMap.FindOrAdd(TargetInvenGuid).Items.Add(Item);

	// 4. 그리드 재구축
	RebuildGridMapByGuid(SourceGuid);
	RebuildGridMapByGuid(TargetInvenGuid);

	// 💡 5. 서버로 이동 패킷 전송 (UInventorySubSystem을 거치도록 수정 완료)
	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvenSub->RequestMoveItem(SourceGuid, TargetInvenGuid, ItemGUID, NewPos, bNewRotated);
	}

	// 6. UI 동기화 알림
	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::SetServerInventoryData(const FInventoryMapWrapper InWrapper)
{
	if (InWrapper.InventorySizeMap.Num() == 0 && InWrapper.InventoryMap.Num() == 0)
	{
		return;
	}

	if (InWrapper.InventorySizeMap.Num() > 0)
	{
		InventorySizeMap = InWrapper.InventorySizeMap;
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
	SetServerInventoryData(InventoryMapWrapper);
}