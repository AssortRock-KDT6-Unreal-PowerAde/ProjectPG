// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/InventoryComponent.h"
#include "Core/ItemSubSystem.h"
// Sets default values for this component's properties
UInventoryComponent::UInventoryComponent()
{

	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true); // 멀티플레이어 환경 복제 설정
	// ...
}


// Called when the game starts
void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
	GridMap.Init(-1, InventorySize.X * InventorySize.Y);
	// ...
	
}
bool UInventoryComponent::CanPlaceItem(const FName& ItemID, FIntPoint TargetPos, bool bRotated, FGuid IgnoreItemGUID)
{
	const FItemTableRow* Data = GetItemData(ItemID);
	if (!Data) return false;

	FIntPoint Size = bRotated ? FIntPoint(Data->GridSize.Y, Data->GridSize.X) : Data->GridSize;

	//인벤토리 경계 검사
	if (TargetPos.X<0 || TargetPos.Y<0 ||
		TargetPos.X + Size.X> InventorySize.X ||
		TargetPos.Y + Size.Y > InventorySize.Y)
	{
		return false;
	}
	UE_LOG(LogTemp, Warning, TEXT("ItemMove"));

	for (int32 x = 0; x < Size.X; ++x)
	{
		for (int32 y = 0; y < Size.Y;++y)
		{
			int32 MapIdx = GetGridIndex(TargetPos.X + x, TargetPos.Y + y);
			int32 OccupantIdx = GridMap[MapIdx];
			
			if (OccupantIdx != -1)
			{
				// 자기 자신 영역으로 이동하는 경우는 예외 처리
				if (IgnoreItemGUID.IsValid() && Items.IsValidIndex(OccupantIdx))
				{
					if (Items[OccupantIdx].GUID == IgnoreItemGUID)
					{
						continue;
					}				
				}
				return false; // 이미 다른 아이템이 차지함
			}
		}
	}
	return true;
}

bool UInventoryComponent::MoveItem(FGuid ItemGUID, FIntPoint NewPos, bool bNewRotated)
{
	FItemInstance* FoundItem = Items.FindByPredicate([&](const FItemInstance& Item) {
		return Item.GUID == ItemGUID;
		});

	if (!FoundItem) return false;


	if (!CanPlaceItem(FoundItem->ItemID, NewPos, bNewRotated, ItemGUID))
	{
		return false;
	}

	FoundItem->Position = NewPos;
	FoundItem->bIsRotated = bNewRotated;
	OnInventoryUpdated.Broadcast();
	RebuildGridMap();

	return true;
}

const FItemTableRow* UInventoryComponent::GetItemData(FName ItemID) const
{	
	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());

	if(false == IsValid(subSystem)) return nullptr;
	
	const FItemTableRow* item = subSystem->GetItem(ItemID);

	if (nullptr == item)
		UE_LOG(LogTemp, Warning, TEXT("ItemData %s not Find"), *ItemID.ToString());

	return item;
}



bool UInventoryComponent::AddItem(FItemInstance NewItem)
{
	const FItemTableRow* ItemData = GetItemData(NewItem.ItemID);
	if (!ItemData)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory] AddItem Failed: Invalid ItemID (%s)"), *NewItem.ItemID.ToString());
		return false;
	}
	if (!NewItem.GUID.IsValid())
	{
		NewItem.GUID = FGuid::NewGuid();
	}
	for (int32 Y = 0; Y < InventorySize.Y; ++Y)
	{
		for (int32 X = 0; X < InventorySize.X; ++X)
		{
			FIntPoint TestPos(X, Y);

			// 2-1. 회전 안 한 상태로 배치 가능한지 검사
			if (CanPlaceItem(NewItem.ItemID, TestPos, false))
			{
				NewItem.Position = TestPos;
				NewItem.bIsRotated = false;

				Items.Add(NewItem);
				RebuildGridMap(); // GridMap 재갱신
				OnInventoryUpdated.Broadcast(); // UI 및 이벤트 알림
				return true;
			}

			// 2-2. 회전한 상태로 배치 가능한지 검사
			if (CanPlaceItem(NewItem.ItemID, TestPos, true))
			{
				NewItem.Position = TestPos;
				NewItem.bIsRotated = true;

				Items.Add(NewItem);
				RebuildGridMap();
				OnInventoryUpdated.Broadcast();
				return true;
			}
		}
	}

	// 3. 인벤토리 공간이 가득 차서 배치하지 못함
	UE_LOG(LogTemp, Log, TEXT("[Inventory] AddItem Failed: Inventory Full (%s)"), *NewItem.ItemID.ToString());
	return false;
}

bool UInventoryComponent::AddItemByID(FName ItemID, int32 Quantity)
{
	if (ItemID.IsNone()) return false;

	FItemInstance NewItem;
	NewItem.ItemID = ItemID;
	NewItem.StackCount = Quantity;
	NewItem.GUID = FGuid::NewGuid();

	return AddItem(NewItem);
}
//Test용
bool UInventoryComponent::AddItemByPosition(FName ItemID, int32 Quantity, FIntPoint pos)
{
	if (ItemID.IsNone()) return false;

	const FItemTableRow* ItemData = GetItemData(ItemID);
	if (!ItemData) return false;

	FItemInstance NewItem;
	NewItem.ItemID = ItemID;
	NewItem.StackCount = Quantity;
	NewItem.GUID = FGuid::NewGuid();
	NewItem.Position = pos; // 지정한 위치 설정
	NewItem.bIsRotated = false;

	// 1. 해당 위치(pos)에 아이템을 놓을 수 있는지 범위 및 충돌 검사
	FIntPoint GridSize = NewItem.GetCurrentGridSize(ItemData);
	if (!CanPlaceItem(NewItem.ItemID, pos, NewItem.bIsRotated))
	{
		UE_LOG(LogTemp, Warning, TEXT("AddItemByPosition 실패: (%d, %d) 위치에 놓을 수 없습니다."), pos.X, pos.Y);
		return false; // 자리가 이미 차있거나 범위를 벗어남
	}

	// 2. AddItem(자동배치) 대신 직접 배열에 추가하고 델리게이트 호출
	Items.Add(NewItem);
	OnInventoryUpdated.Broadcast();

	return true;
}
void UInventoryComponent::RebuildGridMap()
{
	GridMap.Init(-1, InventorySize.X * InventorySize.Y);
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		const FItemInstance& Item = Items[i];
		const FItemTableRow* Data = GetItemData(Item.ItemID);
		if (!Data) continue;

		FIntPoint Size = Item.GetCurrentGridSize(Data);
		for (int32 x = 0;x < Size.X;++x)
		{
			for (int32 y = 0; y < Size.Y;++y)
			{
				int32 MapIdx = GetGridIndex(Item.Position.X + x, Item.Position.Y + y);
				GridMap[MapIdx] = i;
			}
		}

	}
}

