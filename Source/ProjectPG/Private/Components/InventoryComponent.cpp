// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/InventoryComponent.h"
#include "Common/TableData.h"
#include "Server/WebSocketSubSystem.h"
#include <Core/ItemSubSystem.h>

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// 💡 DataComponent가 수신받아 SetServerInventoryData를 호출하므로 
	// BeginPlay의 Direct 웹소켓 바인딩은 중복 방지를 위해 제거합니다.
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
	if (Cols <= 0) return -1;
	return (Y * Cols) + X;
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
	const FIntPoint InvenSize = GetInventorySizeByGuid(InvenGuid);
	if (InvenSize.X <= 0 || InvenSize.Y <= 0) return false;

	const FItemTableRow* Data = GetItemData(ItemID);
	if (!Data) return false;

	FIntPoint ItemSize = bRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;

	// 1. 경계 영역 검사
	if (TargetPos.X < 0 || TargetPos.Y < 0 || (TargetPos.X + ItemSize.X) > InvenSize.X || (TargetPos.Y + ItemSize.Y) > InvenSize.Y)
	{
		return false;
	}

	const FIntArrayWrapper* GridWrapper = InvenGridMap.Find(InvenGuid);

	// 💡 [수정 포인트] 해당 인벤토리 그리드가 아직 없거나 비어있는 경우:
	// 경계 검사(1번)를 통과했고 아이템이 전혀 없는 인벤토리라면 배치가 가능한 상태입니다!
	if (!GridWrapper || GridWrapper->Grid.Num() == 0)
	{
		return true;
	}

	const TArray<FItemInstance>& ItemList = GetItems(InvenGuid);

	// 2. 타일 충돌 검사
	for (int32 x = 0; x < ItemSize.X; ++x)
	{
		for (int32 y = 0; y < ItemSize.Y; ++y)
		{
			int32 Idx = GetGridIndex(InvenGuid, TargetPos.X + x, TargetPos.Y + y);
			if (GridWrapper->Grid.IsValidIndex(Idx))
			{
				int32 OccupiedItemIdx = GridWrapper->Grid[Idx];
				if (OccupiedItemIdx >= 0 && ItemList.IsValidIndex(OccupiedItemIdx))
				{
					// 동일 인벤토리 내 이동 시 자기 자신 영역 제외
					if (IgnoreItemGUID.IsValid() && ItemList[OccupiedItemIdx].GUID == IgnoreItemGUID)
					{
						continue;
					}
					return false; // 이미 다른 아이템이 차지하고 있음 (빨간색)
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

	// 빈 공간 자동 탐색
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

	// 해당 GUID를 가진 아이템 검색
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

	// 배치 검사
	if (!CanPlaceItemByGuid(TargetInvenGuid, Item.ItemID, NewPos, bNewRotated, ItemGUID))
	{
		return false;
	}

	// 기존 위치에서 제거
	ItemsMap[SourceGuid].Items.RemoveAt(ItemIndex);
	RebuildGridMapByGuid(SourceGuid);

	// 새 위치 업데이트 및 추가
	Item.Position = NewPos;
	Item.bIsRotated = bNewRotated;
	Item.parent_inventory_guid = TargetInvenGuid;

	ItemsMap.FindOrAdd(TargetInvenGuid).Items.Add(Item);
	RebuildGridMapByGuid(TargetInvenGuid);

	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::AllocateItemDataByGuid(const TMap<FGuid, FItemArrayWrapper>& ItemData)
{
	ItemsMap.Empty();

	for (const auto& Pair : ItemData)
	{
		const FGuid& TargetGuid = Pair.Key;
		FItemArrayWrapper Wrapper = Pair.Value;

		for (FItemInstance& Item : Wrapper.Items)
		{
			if (!Item.GUID.IsValid())
			{
				Item.GUID = FGuid::NewGuid();
			}
			Item.parent_inventory_guid = TargetGuid;
		}

		ItemsMap.Add(TargetGuid, Wrapper);
		RebuildGridMapByGuid(TargetGuid);
	}

	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::SetServerInventoryData(const FInventoryMapWrapper InWrapper)
{
	ItemsMap.Empty();

	StashInventoryID = InWrapper.StashGuid;
	PocketInventoryID = InWrapper.PocketGuid;
	InventorySizeMap = InWrapper.InventorySizeMap;

	// 💡 아이템 목록에 없는 인벤토리(비어있는 Pocket 등)도 그리드를 미리 0(-1)으로 초기화
	for (const auto& SizePair : InventorySizeMap)
	{
		RebuildGridMapByGuid(SizePair.Key);
	}

	for (const auto& Pair : InWrapper.InventoryMap)
	{
		const FGuid& TargetGuid = Pair.Key;
		const FItemArrayWrapper& Wrapper = Pair.Value;

		ItemsMap.Add(TargetGuid, Wrapper);
		RebuildGridMapByGuid(TargetGuid);
	}

	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::RebuildGridMapByGuid(const FGuid& InvenGuid)
{
	// SizeMap 존재 여부 안전 검사
	if (!InventorySizeMap.Contains(InvenGuid))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RebuildGridMap] InventorySizeMap에서 GUID(%s)를 찾을 수 없습니다."), *InvenGuid.ToString());
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

				// 💡 2D 그리드 범위 내 위치인지 엄격 검사하여 다음 줄 오염 방지
				if (TargetX >= 0 && TargetX < InvenSize.X && TargetY >= 0 && TargetY < InvenSize.Y)
				{
					int32 MapIdx = GetGridIndex(InvenGuid, TargetX, TargetY);
					if (TargetGrid.IsValidIndex(MapIdx))
					{
						TargetGrid[MapIdx] = i;
					}
				}
			}
		}
	}
}