// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/InventoryGridWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/ItemWidget.h"
#include "UI/SlotWidget.h"
#include "UI/ItemDragDropOperation.h"

#include "Core/ItemSubSystem.h"
#include "Components/UniformGridSlot.h"
#include "Components/InventoryComponent.h"
#include "Components/UniformGridPanel.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "Components/EquipComponent.h"

#include "Server/WebSocketSubSystem.h"
#include <Core/TableSubSystem.h>
#include <UI/InventoryWindow.h>
#include <UI/EquipSlot.h>
#include <Core/UIManagerSubSystem.h>

void UInventoryGridWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UInventoryGridWidget::RefreshGrid(UInventoryComponent* InComp, const FGuid& InvenGuid)
{
	if (!IsValid(InComp) || !InvenGuid.IsValid()) return;

	InventoryGUID = InvenGuid;
	BindInventoryComponent(InComp);
}

void UInventoryGridWidget::BindInventoryComponent(UInventoryComponent* InComp)
{
	if (TargetInventoryComp)
	{
		// 반드시 기존 바인딩을 제거하여 이중 호출을 막음
		TargetInventoryComp->OnInventoryUpdated.RemoveAll(this);
	}

	TargetInventoryComp = InComp;

	if (TargetInventoryComp)
	{
		TargetInventoryComp->OnInventoryUpdated.AddDynamic(this, &UInventoryGridWidget::RefreshGridUI);
		UE_LOG(LogTemp, Warning, TEXT("[BindInventoryComponent] GridWidget=%s this=%p BoundToComp=%p"), *GetName(), this, TargetInventoryComp);
	}
}

// DB의 Columns, Rows 수치로 빈 배경 슬롯 격자 생성
void UInventoryGridWidget::CreateBackGroundGrid(int32 Columns, int32 Rows)
{
	if (!BackGroundGrid || !SlotWidgetClass || !InventorySizeBox) return;
	if (Columns <= 0 || Rows <= 0) return;

	BackGroundGrid->ClearChildren();
	BackGroundGrid->SetSlotPadding(FMargin(0.0f));

	const float CalculatedWidth = Columns * TileSize;
	const float CalculatedHeight = Rows * TileSize;

	// 1. SizeBox 크기 고정
	InventorySizeBox->SetWidthOverride(CalculatedWidth);
	InventorySizeBox->SetHeightOverride(CalculatedHeight);

	// 2. Row x Col 만큼 타일 슬롯 생성 및 추가
	for (int32 r = 0; r < Rows; ++r)
	{
		for (int32 c = 0; c < Columns; ++c)
		{
			USlotWidget* SlotWidget = CreateWidget<USlotWidget>(this, SlotWidgetClass);
			if (!SlotWidget) continue;

			// AddChildToUniformGrid takes (Content, Column, Row) so pass column first then row
			UUniformGridSlot* GridSlot = BackGroundGrid->AddChildToUniformGrid(SlotWidget, c, r);
			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
		}
	}
}

void UInventoryGridWidget::RefreshGridUI()
{
	if (!TargetInventoryComp) {
		UE_LOG(LogTemp, Warning, TEXT("TargetInventoryComp none"));
		return;
	}
	// 1. GUID가 지정되지 않은 경우 기본 창고(Stash) GUID 가져오기 시도
	if (!InventoryGUID.IsValid())
	{
		InventoryGUID = TargetInventoryComp->GetStashInventoryID();
	}

	if (!InventoryGUID.IsValid()) {
		UE_LOG(LogTemp, Warning, TEXT("InventoryGuid none"));
		return;
	}
	int32 GridColumns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 GridRows = TargetInventoryComp->GetRows(InventoryGUID);
	UE_LOG(LogTemp, Warning, TEXT("슬롯생성 %d %d"),GridColumns, GridRows);

	// 2. 크기가 0이면 가방(Backpack) 장착 데이터 자동 복구 시도
	if (GridColumns <= 0 || GridRows <= 0)
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC && PC->GetPawn())
		{
			//가방 장착시 
			if (UEquipComponent* EquipComp = PC->GetPawn()->FindComponentByClass<UEquipComponent>())
			{
				const FItemInstance* BackpackItem = EquipComp->GetEquipment(EEquipSlot::BackPack);
				if (BackpackItem && BackpackItem->GUID == InventoryGUID)
				{
					if (UTableSubSystem* TableSub = UTableSubSystem::Get(GetWorld()))
					{
						const FItemBackpackTable* Data = TableSub->FindTableRow<FItemBackpackTable>("BackpackTable", BackpackItem->ItemID);
						if (Data && Data->SlotSize.X > 0 && Data->SlotSize.Y > 0)
						{
							// 테이블 정보로 가방 컨테이너 크기 즉시 등록
							TargetInventoryComp->RegisterContainer(InventoryGUID, FIntPoint(Data->SlotSize.X, Data->SlotSize.Y));
							GridColumns = Data->SlotSize.X;
							GridRows = Data->SlotSize.Y;
						}
					}
				}
			}
		}
	}

	// 3. 서버 데이터 동기화 대기 중인 경우 처리 중단 (패킷 수신 후 OnInventoryUpdated 델리게이트로 재호출됨)
	if (GridColumns <= 0 || GridRows <= 0)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[RefreshGridUI] 인벤토리 정보 동기화 대기 중... (GUID: %s)"), *InventoryGUID.ToString());
		return;
	}
	if (GridColumns == 0 && GridRows == 0) {
		FIntPoint size = TargetInventoryComp->GetInventorySizeByGuid(InventoryGUID);
		GridColumns = size.X;
		GridRows = size.Y;

	}
	// 4. 정상 크기 수신 확인 후 배경 슬롯 및 아이템 렌더링
	CreateBackGroundGrid(GridColumns, GridRows);
	RenderItems();
}
void UInventoryGridWidget::RenderItems()
{
	if (!ItemCanvas || !ItemWidgetClass || !TargetInventoryComp || !InventoryGUID.IsValid()) return;

	ItemCanvas->ClearChildren();

	UItemSubSystem* ItemSubSystem = UItemSubSystem::Get(GetWorld());
	if (!ItemSubSystem) return;

	const TArray<FItemInstance>& ItemList = TargetInventoryComp->GetItems(InventoryGUID);

	// 진단 로그: 렌더링 대상 아이템 수 및 인벤토리 GUID
	UE_LOG(LogTemp, Warning, TEXT("[RenderItems] InventoryGUID=%s ItemCount=%d"), *InventoryGUID.ToString(), ItemList.Num());

	for (const FItemInstance& Item : ItemList)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RenderItems] Item GUID=%s Pos=(%d,%d) parent_guid=%s"), *Item.GUID.ToString(), Item.Position.X, Item.Position.Y, *Item.parent_inventory_guid.ToString());
		// 방어 코드: Item의 parent_inventory_guid가 현재 그리드의 InventoryGUID와 다르면 렌더링하지 않음
		if (Item.parent_inventory_guid.IsValid() && Item.parent_inventory_guid != InventoryGUID)
		{
			UE_LOG(LogTemp, Warning, TEXT("[RenderItems] Skipping item %s because parent_guid %s != InventoryGUID %s"), *Item.GUID.ToString(), *Item.parent_inventory_guid.ToString(), *InventoryGUID.ToString());
			continue;
		}
		const FItemTableRow* ItemData = ItemSubSystem->GetItem(Item.ItemID);
		if (!ItemData) continue;

		UItemWidget* ItemWidget = CreateWidget<UItemWidget>(this, ItemWidgetClass);
		if (!ItemWidget) continue;

		// ★ 핵심: 현재 InventoryGridWidget의 InventoryGUID를 정확히 넘겨주어 위젯의 OwnerInventoryGUID 갱신!
		ItemWidget->InitWidget(Item, *ItemData, InventoryGUID, TileSize);

		UCanvasPanelSlot* CanvasSlot = ItemCanvas->AddChildToCanvas(ItemWidget);
		if (CanvasSlot)
		{
			FIntPoint GridSize = Item.GetCurrentGridSize(ItemData);
			FVector2D PositionPixel = FVector2D(Item.Position.X * TileSize, Item.Position.Y * TileSize);
			FVector2D SizePixel = FVector2D(GridSize.X * TileSize, GridSize.Y * TileSize);

			CanvasSlot->SetPosition(PositionPixel);
			CanvasSlot->SetSize(SizePixel);
			CanvasSlot->SetAutoSize(false);
		}
	}
}
USlotWidget* UInventoryGridWidget::GetSlotWidgetAt(int32 TileX, int32 TileY)
{
	if (!BackGroundGrid || !TargetInventoryComp || !InventoryGUID.IsValid()) return nullptr;

	int32 Columns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 Rows = TargetInventoryComp->GetRows(InventoryGUID);

	if (TileX < 0 || TileX >= Columns || TileY < 0 || TileY >= Rows)
	{
		return nullptr;
	}

	const int32 ChildCount = BackGroundGrid->GetChildrenCount();
	for (int32 i = 0; i < ChildCount; ++i)
	{
		UWidget* Child = BackGroundGrid->GetChildAt(i);
		if (!Child) continue;

		if (UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Child->Slot))
		{
			if (GridSlot->GetColumn() == TileX && GridSlot->GetRow() == TileY)
			{
				return Cast<USlotWidget>(Child);
			}
		}
	}

	return nullptr;
}

bool UInventoryGridWidget::CanPlaceItemAt(const FItemInstance& ItemToPlace, FIntPoint TargetTile, FIntPoint GridSize)
{
	if (!TargetInventoryComp || !InventoryGUID.IsValid()) return false;

	// 디버그 로그로 드롭 좌표 확인 (필요 시 주석 해제)
	// UE_LOG(LogTemp, Log, TEXT("[CanPlaceItemAt] TargetTile: (%d, %d), GUID: %s"), TargetTile.X, TargetTile.Y, *ItemToPlace.GUID.ToString());

	return TargetInventoryComp->CanPlaceItemByGuid(
		InventoryGUID,
		ItemToPlace.ItemID,
		TargetTile,
		ItemToPlace.bIsRotated,
		ItemToPlace.GUID // ★ 자기 자신 GUID 전달하여 본인 영역 충돌 무시
	);
}

const FItemInstance* UInventoryGridWidget::GetItemAtCell(int32 TileX, int32 TileY)
{
	if (!TargetInventoryComp || !InventoryGUID.IsValid()) return nullptr;

	int32 Columns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 Rows = TargetInventoryComp->GetRows(InventoryGUID);

	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (!subSystem) return nullptr;

	if (TileX < 0 || TileX >= Columns || TileY < 0 || TileY >= Rows)
	{
		return nullptr;
	}

	const TArray<FItemInstance>& Items = TargetInventoryComp->GetItems(InventoryGUID);

	for (const FItemInstance& Item : Items)
	{
		const FItemTableRow* ItemData = subSystem->GetItem(Item.ItemID);
		if (!ItemData) continue;

		FIntPoint GridSize = Item.GetCurrentGridSize(ItemData);

		int32 StartX = Item.Position.X;
		int32 StartY = Item.Position.Y;
		int32 EndX = StartX + GridSize.X;
		int32 EndY = StartY + GridSize.Y;

		if (TileX >= StartX && TileX < EndX && TileY >= StartY && TileY < EndY)
		{
			return &Item;
		}
	}

	return nullptr;
}

void UInventoryGridWidget::ClearSlotHighlights()
{
	for (USlotWidget* SlotWidget : HighlightedSlots)
	{
		if (SlotWidget)
		{
			SlotWidget->SetHighlightState(EBorderHighlightState::None);
		}
	}
	HighlightedSlots.Empty();
}

FReply UInventoryGridWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::R)
	{
		if (UItemDragDropOperation* DragOp = Cast<UItemDragDropOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent()))
		{
			// 회전 상태 반전
			DragOp->bCurrentRotated = !DragOp->bCurrentRotated;

			// DragVisual 위젯 크기 및 회전 UI 업데이트
			if (UItemWidget* DragVisual = Cast<UItemWidget>(DragOp->DefaultDragVisual))
			{
				DragVisual->ItemInstance.bIsRotated = DragOp->bCurrentRotated;
				DragVisual->RefreshWidget(); // 크기 갱신
			}
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}

bool UInventoryGridWidget::NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	ClearSlotHighlights();

	UItemDragDropOperation* ItemDragOp = Cast<UItemDragDropOperation>(InOperation);
	if (!ItemDragOp || !BackGroundGrid || !InventoryGUID.IsValid()) return false;

	// 1. 드롭할 대상 타일 좌표 계산
	FGeometry GridGeometry = BackGroundGrid->GetCachedGeometry();
	FVector2D LocalMousePos = GridGeometry.AbsoluteToLocal(InDragDropEvent.GetScreenSpacePosition());
	FIntPoint TargetTile = MouseToTilePosition(LocalMousePos, ItemDragOp->DragOffset);

	// 디버그 출력: 드롭 직전 산출된 좌표 확인
	UE_LOG(LogTemp, Log, TEXT("[NativeOnDrop] ScreenPos:(%s) LocalMouse:(%s) DragOffset:(%s) ComputedTarget:(%d,%d) GUID:%s"),
		*InDragDropEvent.GetScreenSpacePosition().ToString(), *LocalMousePos.ToString(), *ItemDragOp->DragOffset.ToString(), TargetTile.X, TargetTile.Y, *ItemDragOp->DraggedItem.GUID.ToString());

	// 2. 드래그 중인 아이템 객체 준비 (현재 회전값 반영)
	FItemInstance TempInstance = ItemDragOp->DraggedItem;
	TempInstance.bIsRotated = ItemDragOp->bCurrentRotated;

	UItemWidget* DragVisual = Cast<UItemWidget>(ItemDragOp->DefaultDragVisual);
	if (!DragVisual) return false;

	const FItemTableRow* ItemData = DragVisual->GetCachedItemData();
	if (!ItemData) return false;

	FIntPoint GridSize = TempInstance.GetCurrentGridSize(ItemData);

	// 3. 만약 드래그 출처가 장착 슬롯이라면(장비 해제) 특수 처리
	if (ItemDragOp->bFromEquip)
	{
		// 장착에서 해제되어 인벤토리로 들어오는 흐름
		// 드래그 오프셋이 EquipSlot 기준으로 계산되어 있을 수 있으므로
		// 여러 후보 위치를 계산하여 배치 가능한 첫 위치를 선택하도록 안전하게 처리합니다.
		TArray<FIntPoint> CandidateTiles;

		// 1) 드래그에 포함된 DragOffset을 사용한 기본 계산
		CandidateTiles.Add(MouseToTilePosition(LocalMousePos, ItemDragOp->DragOffset));

		// 2) 아이템 크기 중심을 오프셋으로 사용한 계산(중앙 기준)
		CandidateTiles.Add(MouseToTilePosition(LocalMousePos, FVector2D((GridSize.X * TileSize) * 0.5f, (GridSize.Y * TileSize) * 0.5f)));

		// 3) 마우스 좌표 그대로(DragOffset 0) 시도
		CandidateTiles.Add(MouseToTilePosition(LocalMousePos, FVector2D::ZeroVector));

		FIntPoint ChosenTile = FIntPoint(-1, -1);
		for (const FIntPoint& Cand : CandidateTiles)
		{
			if (CanPlaceItemAt(TempInstance, Cand, GridSize))
			{
				ChosenTile = Cand;
				break;
			}
		}

		bool bCanPlace = (ChosenTile.X != -1 && ChosenTile.Y != -1);
		// 디버그: 후보 타일 및 선택 결과 출력
		FString CandList;
		for (const FIntPoint& C : CandidateTiles)
		{
			CandList += FString::Printf(TEXT("(%d,%d) "), C.X, C.Y);
		}
		UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid::Unequip] Candidates=%s Chosen=(%d,%d)"), *CandList, ChosenTile.X, ChosenTile.Y);
		if (!bCanPlace)
		{
			if (ItemDragOp->WidgetReference)
			{
				ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
			}
			UE_LOG(LogTemp, Warning, TEXT("[NativeOnDrop] (Unequip) 배치 불가능한 위치입니다. Candidates tested: %d | Drop 취소"), CandidateTiles.Num());
			return false;
		}

		if (!TargetInventoryComp)
		{
			return false;
		}

		// 1) 먼저 장착에서 해제 요청 (로컬 적용)
		// 우선 드래그 출처가 EquipSlot 위젯이면 해당 위젯의 RequestUnEquip을 호출해
		// EquipComponent 인스턴스를 정확히 사용하도록 합니다.
		if (ItemDragOp->WidgetReference)
		{
			if (UEquipSlot* SrcSlot = Cast<UEquipSlot>(ItemDragOp->WidgetReference))
			{
				SrcSlot->RequestUnEquip();
			}
		}

		// 2) 인벤토리에 아이템 추가 (명시 위치)
		// 서버에 이동 명령을 보내기 위해 기존 소스 GUID를 보존
		FGuid SourceGuid = ItemDragOp->DraggedItem.parent_inventory_guid;
		TempInstance.parent_inventory_guid = InventoryGUID;
		bool bAdded = TargetInventoryComp->AddItemAt(TempInstance, ChosenTile);

		if (ItemDragOp->WidgetReference)
		{
			ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
		}

		// 3) 서버에 장착 해제 패킷 전송
		if (bAdded)
		{
			if (UWebSocketSubSystem* Web = UWebSocketSubSystem::Get(GetWorld()))
			{
				// 1) 장착 해제 알림
				Web->RequestEquipItem(TempInstance.GUID, InventoryGUID, false);

				// 2) 선택한 위치로 아이템 이동 요청 (서버에 구체적 좌표 전달)
				Web->RequestMoveItem(SourceGuid, InventoryGUID, TempInstance.GUID, ChosenTile, TempInstance.bIsRotated);
			}
			UE_LOG(LogTemp, Warning, TEXT("[InventoryGrid::Unequip] Sent RequestMoveItem From=%s To=%s Item=%s Pos=(%d,%d)"), *SourceGuid.ToString(), *InventoryGUID.ToString(), *TempInstance.GUID.ToString(), ChosenTile.X, ChosenTile.Y);
		}

		// 소스 EquipSlot UI 정리: 드래그 시작 소스가 EquipSlot 위젯이면 Clear 호출
		if (ItemDragOp->WidgetReference)
		{
			if (UEquipSlot* SrcSlot = Cast<UEquipSlot>(ItemDragOp->WidgetReference))
			{
				SrcSlot->Clear();
			}
		}

		// BackPack의 경우에는 장착 해제 시 백팩 UI 제거
		// UUIManagerSubSystem을 통해 활성화된 InventoryWindow를 찾아 처리
		UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(GetWorld());
		if (UISub)
		{
			if (UUserWidget* InventoryUI = UISub->GetUI(EUIType::Inventory))
			{
				if (UInventoryWindow* ParentInvenWindow = Cast<UInventoryWindow>(InventoryUI))
				{
					UItemSubSystem* ItemSub = UItemSubSystem::Get(GetWorld());
					if (ItemSub)
					{
						const FItemTableRow* DragData = ItemSub->GetItem(TempInstance.ItemID);
						if (DragData && DragData->EquipSlotType == EEquipSlot::BackPack)
						{
							ParentInvenWindow->SetupBackPackInventoryWidget(nullptr);
						}
					}
				}
			}
		}

		return bAdded;
	}

	// 4. 일반적인 인벤토리 간 이동 처리 (기존 구현 유지)
	bool bCanPlace = CanPlaceItemAt(TempInstance, TargetTile, GridSize);

	// 배치가 불가능한 지역이면 드롭 취소 및 원복
	if (!bCanPlace)
	{
		if (ItemDragOp->WidgetReference)
		{
			ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
		}
		UE_LOG(LogTemp, Warning, TEXT("[NativeOnDrop] 배치 불가능한 위치입니다. TargetTile:(%d, %d) Drop 취소"), TargetTile.X, TargetTile.Y);
		return false;
	}

	// 4. 배치 가능 시 이동 처리 (MoveItem 선처리 호출)
	if (ItemDragOp->WidgetReference)
	{
		ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
	}

	if (TargetInventoryComp)
	{
		// MoveItem 내부에서 로컬 선처리 및 WebSocketSub->RequestMoveItem()이 수행됩니다.
		return TargetInventoryComp->MoveItem(
			InventoryGUID,
			ItemDragOp->DraggedItem.GUID,
			TargetTile,
			ItemDragOp->bCurrentRotated
		);
	}

	return false;
}
bool UInventoryGridWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

	if (!TargetInventoryComp || !BackGroundGrid || !InventoryGUID.IsValid()) return false;

	UItemDragDropOperation* ItemDragOp = Cast<UItemDragDropOperation>(InOperation);
	if (!ItemDragOp) return false;

	UItemWidget* DragVisual = Cast<UItemWidget>(ItemDragOp->DefaultDragVisual);
	if (!DragVisual) return false;

	ClearSlotHighlights();

	FGeometry GridGeometry = BackGroundGrid->GetCachedGeometry();
	FVector2D LocalMousePos = GridGeometry.AbsoluteToLocal(InDragDropEvent.GetScreenSpacePosition());

	const FItemTableRow* ItemData = DragVisual->GetCachedItemData();
	if (!ItemData) return false;

	// 현재 회전값 반영된 객체로 검사
	FItemInstance TempInstance = ItemDragOp->DraggedItem;
	TempInstance.bIsRotated = ItemDragOp->bCurrentRotated;

	FIntPoint GridSize = TempInstance.GetCurrentGridSize(ItemData);
	FIntPoint TargetTile = MouseToTilePosition(LocalMousePos, ItemDragOp->DragOffset);

	int32 Columns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 Rows = TargetInventoryComp->GetRows(InventoryGUID);

	// 배치 검사 실행
	bool bCanPlace = CanPlaceItemAt(TempInstance, TargetTile, GridSize);

	UE_LOG(LogTemp, Warning, TEXT("[NativeOnDrop] CanPlaceCheck GUID=%s Columns=%d Rows=%d GridSize=(%d,%d) Target=(%d,%d) Result=%d"), *InventoryGUID.ToString(), Columns, Rows, GridSize.X, GridSize.Y, TargetTile.X, TargetTile.Y, bCanPlace);
	EBorderHighlightState HighlightState = bCanPlace ? EBorderHighlightState::Valid : EBorderHighlightState::Invalid;

	for (int32 x = 0; x < GridSize.X; ++x)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			int32 CheckX = TargetTile.X + x;
			int32 CheckY = TargetTile.Y + y;

			if (CheckX >= 0 && CheckX < Columns && CheckY >= 0 && CheckY < Rows)
			{
				if (USlotWidget* SlotWidget = GetSlotWidgetAt(CheckX, CheckY))
				{
					SlotWidget->SetHighlightState(HighlightState);
					HighlightedSlots.Add(SlotWidget);
				}
			}
		}
	}

	return true;
}

void UInventoryGridWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	ClearSlotHighlights();
}

FIntPoint UInventoryGridWidget::MouseToTilePosition(const FVector2D& LocalMousePos, const FVector2D& DragOffset)
{
	if (TileSize <= 0.0f) return FIntPoint(-1, -1);

	// 드래그 마우스 오프셋 반영한 아이템 좌상단 위치 계산
	FVector2D TopLeftPos = LocalMousePos - DragOffset;

	// 픽셀 연산 보정을 위해 약간의 중심점 픽셀Offset(TileSize * 0.1f) 반영 후 안전 계산
	int32 TileX = FMath::FloorToInt((TopLeftPos.X + (TileSize * 0.1f)) / TileSize);
	int32 TileY = FMath::FloorToInt((TopLeftPos.Y + (TileSize * 0.1f)) / TileSize);

	return FIntPoint(TileX, TileY);
}