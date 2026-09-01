// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/InventoryComponent.h"
#include "Common/TableData.h"
#include "Server/WebSocketSubSystem.h"
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

	// 서버 통신은 호출자(Equip/Inventory UI)가 담당하도록 하며, 로컬 상태만 즉시 반영
	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// WebSocketSubsystem 가져오기 및 서버 수신 델리게이트 바인딩
	if (UWebSocketSubSystem* Subsystem = UWebSocketSubSystem::Get(GetWorld()))
	{
		Subsystem->OnInventoryReceived.RemoveDynamic(this, &UInventoryComponent::HandleInventoryReceived);

		Subsystem->OnInventoryReceived.AddDynamic(this, &UInventoryComponent::HandleInventoryReceived);
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

	// X, Y 좌표가 실제 인벤토리 범위 내에 있는지 엄격 검사 (다음 줄 오염 방지)
	if (Cols <= 0 || Rows <= 0 || X < 0 || X >= Cols || Y < 0 || Y >= Rows)
	{
		return -1;
	}

	return (Y * Cols) + X;
}

void UInventoryComponent::RegisterContainer(const FGuid& ContainerGUID, FIntPoint ContainerSize)
{
	if (!ContainerGUID.IsValid() || ContainerSize.X <= 0 || ContainerSize.Y <= 0) return;

	// 💡 [추가된 방어 코드] 이미 동일한 가방이 같은 크기로 등록되어 있다면 중복 브로드캐스트를 막고 리턴합니다.
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
		// 방어: 대상 컨테이너의 크기가 등록되어 있지 않다면
		// 해당 GUID가 실제로 백팩 아이템의 GUID인지 검사하여 테이블에서 크기를 자동 등록 시도
		if (InvenGuid.IsValid())
		{
			// Search for an item instance whose GUID matches the container GUID
			for (const auto& Pair : ItemsMap)
			{
				for (const FItemInstance& Candidate : Pair.Value.Items)
				{
					if (Candidate.GUID == InvenGuid)
					{
						// Found potential backpack item; lookup backpack table
						if (UTableSubSystem* TableSub = UTableSubSystem::Get(GetWorld()))
						{
							const FItemBackpackTable* BP = TableSub->FindTableRow<FItemBackpackTable>("BackpackTable", Candidate.ItemID);
							if (BP && BP->SlotSize.X > 0 && BP->SlotSize.Y > 0)
							{
								RegisterContainer(InvenGuid, FIntPoint(BP->SlotSize.X, BP->SlotSize.Y));
								InvenSize = BP->SlotSize;
								UE_LOG(LogTemp, Warning, TEXT("[CanPlaceItemByGuid] Auto-registered container %s size=(%d,%d) based on item %s"), *InvenGuid.ToString(), BP->SlotSize.X, BP->SlotSize.Y, *Candidate.ItemID.ToString());
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

	// 1. 경계 영역 검사
	if (TargetPos.X < 0 || TargetPos.Y < 0 || (TargetPos.X + ItemSize.X) > InvenSize.X || (TargetPos.Y + ItemSize.Y) > InvenSize.Y)
	{
		return false;
	}

	const FIntArrayWrapper* GridWrapper = InvenGridMap.Find(InvenGuid);
	

	// 해당 인벤토리 그리드가 비어있다면 배치가 가능한 상태
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
			if (Idx != -1 && GridWrapper->Grid.IsValidIndex(Idx))
			{
				int32 OccupiedItemIdx = GridWrapper->Grid[Idx];
				if (OccupiedItemIdx >= 0 && ItemList.IsValidIndex(OccupiedItemIdx))
				{
					// 동일 인벤토리 내 이동 시 자기 자신 영역 무시
					if (IgnoreItemGUID.IsValid() && ItemList[OccupiedItemIdx].GUID == IgnoreItemGUID)
					{
						continue;
					}
					return false; // 다른 아이템과 충돌
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

	UE_LOG(LogTemp, Warning, TEXT("[MoveItem] ThisComp=%p Called MoveItem Item=%s ToGuid=%s Pos=(%d,%d) Rot=%d"), this, *ItemGUID.ToString(), *TargetInvenGuid.ToString(), NewPos.X, NewPos.Y, bNewRotated);

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
	// 배치 가능 검사
	FItemInstance Item = ItemsMap[SourceGuid].Items[ItemIndex];
	if (!CanPlaceItemByGuid(TargetInvenGuid, Item.ItemID, NewPos, bNewRotated, ItemGUID))
	{
		return false;
	}

	// 1. 기존 위치에서 삭제
	ItemsMap[SourceGuid].Items.RemoveAt(ItemIndex);

	// 2. 값 수정 (parent_inventory_guid 필수 변경)
	Item.Position = NewPos;
	Item.bIsRotated = bNewRotated;
	Item.parent_inventory_guid = TargetInvenGuid; // ★ GUID 갱신

	// 3. 타겟 위치에 추가
	ItemsMap.FindOrAdd(TargetInvenGuid).Items.Add(Item);

	// 4. 소스/타겟 그리드 지도 재구축
	RebuildGridMapByGuid(SourceGuid);
	RebuildGridMapByGuid(TargetInvenGuid);

	// 5. 서버 패킷 전송
	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->RequestMoveItem(SourceGuid, TargetInvenGuid, ItemGUID, NewPos, bNewRotated);
	}

	// 6. UI 동기화 델리게이트 알림 -> UI가 다시 그려지면서 RenderItems()가 호출됨
	OnInventoryUpdated.Broadcast();
	return true;
}

void UInventoryComponent::SetServerInventoryData(const FInventoryMapWrapper InWrapper)
{
	// ★ 수정: 크기 데이터(InventorySizeMap)마저 완전히 없다면 잘못된 데이터로 판단하여 리턴합니다.
		// (아이템이 0개인 빈 인벤토리도 정상 데이터이므로 처리를 진행해야 합니다)
	if (InWrapper.InventorySizeMap.Num() == 0 && InWrapper.InventoryMap.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SetServerInventoryData] 수신된 인벤토리 크기 및 아이템 정보가 모두 비어있습니다."));
		return;
	}

	// 1. 크기 데이터가 존재할 때 크기 맵 갱신
	if (InWrapper.InventorySizeMap.Num() > 0)
	{
		InventorySizeMap = InWrapper.InventorySizeMap;
	}

	// 2. 기존 메모리 초기화
	ItemsMap.Empty();
	InvenGridMap.Empty();

	if (InWrapper.StashGuid.IsValid()) StashInventoryID = InWrapper.StashGuid;
	if (InWrapper.PocketGuid.IsValid()) PocketInventoryID = InWrapper.PocketGuid;
	// 3. 등록된 모든 컨테이너 GUID에 대해 빈 그리드 배열 생성
	for (const auto& SizePair : InventorySizeMap)
	{
		// 빈 아이템 배열 등록 (아이템이 없어도 Key 등록)
		ItemsMap.FindOrAdd(SizePair.Key);
		RebuildGridMapByGuid(SizePair.Key);
	}

	// 4. 수신된 아이템 복사 및 그리드 매핑
	TSet<FGuid> SeenItemGuids;
	for (const auto& Pair : InWrapper.InventoryMap)
	{
		const FGuid& TargetGuid = Pair.Key;
		FItemArrayWrapper Wrapper = Pair.Value; // 복사해서 수정 후 집어넣음

		// 역순으로 순회하여 중복 제거할 때 안전하게 RemoveAt를 사용할 수 있도록 함
		for (int32 i = Wrapper.Items.Num() - 1; i >= 0; --i)
		{
			FItemInstance& Item = Wrapper.Items[i];

			// Owner 주입
			Item.Owner = GetOwner();

			// 보정: 서버 데이터가 잘못된 parent_inventory_guid를 보냈다면 Pair.Key(TargetGuid)를 우선 사용
			if (!Item.parent_inventory_guid.IsValid() || Item.parent_inventory_guid != TargetGuid)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[SetServerInventoryData] Correcting item %s parent_guid from %s to %s"), *Item.GUID.ToString(), *Item.parent_inventory_guid.ToString(), *TargetGuid.ToString());
				Item.parent_inventory_guid = TargetGuid;
			}

			// 중복 GUID 검사: 이미 다른 컨테이너에서 본 GUID라면 해당 항목을 건너뜀
			if (Item.GUID.IsValid())
			{
				if (SeenItemGuids.Contains(Item.GUID))
				{
					UE_LOG(LogTemp, Warning, TEXT("[SetServerInventoryData] Skipping duplicate item %s for container %s (already assigned)"), *Item.GUID.ToString(), *TargetGuid.ToString());
					Wrapper.Items.RemoveAt(i);
					continue;
				}
				SeenItemGuids.Add(Item.GUID);
			}
		}

		ItemsMap.FindOrAdd(TargetGuid) = Wrapper;
		RebuildGridMapByGuid(TargetGuid);
	}

	// 5. UI 갱신 알림
	// 디버그: 모든 컨테이너와 포함된 아이템 GUID 출력
	for (const auto& Pair : ItemsMap)
	{
		const FGuid& Guid = Pair.Key;
		const TArray<FItemInstance>& List = Pair.Value.Items;
		UE_LOG(LogTemp, Warning, TEXT("[SetServerInventoryData] Container %s has %d items"), *Guid.ToString(), List.Num());
		for (const FItemInstance& It : List)
		{
			UE_LOG(LogTemp, Warning, TEXT("  - Item %s parent=%s pos=(%d,%d)"), *It.GUID.ToString(), *It.parent_inventory_guid.ToString(), It.Position.X, It.Position.Y);
		}
	}

	OnInventoryUpdated.Broadcast();
}

void UInventoryComponent::RebuildGridMapByGuid(const FGuid& InvenGuid)
{
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

				int32 MapIdx = GetGridIndex(InvenGuid, TargetX, TargetY);
				if (MapIdx != -1 && TargetGrid.IsValidIndex(MapIdx))
				{
					TargetGrid[MapIdx] = i;
				}
			}
		}
	}
}

void UInventoryComponent::HandleInventoryReceived(const FInventoryMapWrapper InventoryMapWrapper)
{
	// SetServerInventoryData 하나만 호출하면 내부에서 그리드 생성과 Broadcast가 모두 완료됩니다.
	SetServerInventoryData(InventoryMapWrapper);
}

