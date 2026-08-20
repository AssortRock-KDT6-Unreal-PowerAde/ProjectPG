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
#include "Server/WebSocketSubSystem.h"

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
	TargetInventoryComp = InComp;
	if (!TargetInventoryComp) return;

	// 1. 델리게이트 중복 바인딩 방지 후 등록 (데이터 수신 시 RefreshGridUI 호출)
	TargetInventoryComp->OnInventoryUpdated.RemoveAll(this);
	TargetInventoryComp->OnInventoryUpdated.AddDynamic(this, &UInventoryGridWidget::RefreshGridUI);

	// 2. 인벤토리 UI 갱신 시도
	RefreshGridUI();
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

			UUniformGridSlot* GridSlot = BackGroundGrid->AddChildToUniformGrid(SlotWidget, r, c);
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
	if (!TargetInventoryComp) return;

	// 1. 전달받은 InventoryGUID가 유효하지 않으면 Component의 최신 StashGUID를 가져옴
	if (!InventoryGUID.IsValid())
	{
		InventoryGUID = TargetInventoryComp->GetStashInventoryID();
	}

	// 여전히 GUID가 Invalid 상태면 서버 응답 대기
	if (!InventoryGUID.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RefreshGridUI] 아직 서버로부터 InventoryGUID를 수신받지 못했습니다."));
		return;
	}

	int32 GridColumns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 GridRows = TargetInventoryComp->GetRows(InventoryGUID);

	UE_LOG(LogTemp, Warning, TEXT("[RefreshGridUI] Columns : %d , Rows : %d"), GridColumns, GridRows);

	if (GridColumns > 0 && GridRows > 0)
	{
		// 1. 배경 격자 슬롯 생성
		CreateBackGroundGrid(GridColumns, GridRows);

		// 2. ★ 실제 아이템 위젯배치 호출 ★
		RenderItems();
	}
}
void UInventoryGridWidget::RenderItems()
{
	// ItemCanvas(CanvasPanel) 또는 ItemsOverlay 등 아이템을 올릴 캔버스 패널 체크
	// ※ InventoryGridWidget.h에 UCanvasPanel* ItemCanvas; 혹은 UOverlay* ItemOverlay가 선언되어 있어야 합니다.
	if (!ItemCanvas || !ItemWidgetClass || !TargetInventoryComp || !InventoryGUID.IsValid()) return;

	// 기존 배치된 아이템 위젯 제거
	ItemCanvas->ClearChildren();

	UItemSubSystem* ItemSubSystem = UItemSubSystem::Get(GetWorld());
	if (!ItemSubSystem) return;

	const TArray<FItemInstance>& ItemList = TargetInventoryComp->GetItems(InventoryGUID);

	for (const FItemInstance& Item : ItemList)
	{
		const FItemTableRow* ItemData = ItemSubSystem->GetItem(Item.ItemID);
		if (!ItemData) continue;

		// 아이템 위젯 생성
		UItemWidget* ItemWidget = CreateWidget<UItemWidget>(this, ItemWidgetClass);
		if (!ItemWidget) continue;

		// 아이템 데이터 초기화 및 바인딩
		ItemWidget->InitWidget(Item, *ItemData, InventoryGUID);

		// CanvasPanel에 자식으로 추가
		UCanvasPanelSlot* CanvasSlot = ItemCanvas->AddChildToCanvas(ItemWidget);
		if (CanvasSlot)
		{
			// 현재 회전 여부에 따른 그리드 크기 계산 (Ex: 1x2 -> 2x1)
			FIntPoint GridSize = Item.GetCurrentGridSize(ItemData);

			// 픽셀 단위 위치 및 크기 계산
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

	// Component의 배치 검증 로직 직접 활용
	// (Source, Target 인벤토리 관계 및 회전, 자기 자신 제외 검사가 완벽하게 처리됨)
	return TargetInventoryComp->CanPlaceItemByGuid(
		InventoryGUID,
		ItemToPlace.ItemID,
		TargetTile,
		ItemToPlace.bIsRotated,
		ItemToPlace.GUID
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
			DragOp->bCurrentRotated = !DragOp->bCurrentRotated;

			if (UItemWidget* DragVisual = Cast<UItemWidget>(DragOp->DefaultDragVisual))
			{
				DragVisual->ItemInstance.bIsRotated = DragOp->bCurrentRotated;
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

	// 2. 드래그 중인 아이템 객체 준비 (현재 회전값 반영)
	FItemInstance TempInstance = ItemDragOp->DraggedItem;
	TempInstance.bIsRotated = ItemDragOp->bCurrentRotated;

	UItemWidget* DragVisual = Cast<UItemWidget>(ItemDragOp->DefaultDragVisual);
	if (!DragVisual) return false;

	const FItemTableRow* ItemData = DragVisual->GetCachedItemData();
	if (!ItemData) return false;

	FIntPoint GridSize = TempInstance.GetCurrentGridSize(ItemData);

	// 3. ★ 핵심: 타겟 인벤토리에 아이템 배치 가능 여부 검증 ★
	bool bCanPlace = CanPlaceItemAt(TempInstance, TargetTile, GridSize);

	// 배치가 불가능한 지역(빨간색 하이라이트)이면 드롭 실패 처리 (제자리 복귀)
	if (!bCanPlace)
	{
		if (ItemDragOp->WidgetReference)
		{
			ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
		}
		UE_LOG(LogTemp, Warning, TEXT("[NativeOnDrop] 배치 불가능한 위치입니다. 드롭을 취소합니다."));
		return false; // false 리턴시 원래 위치로 복귀
	}

	// 4. 배치 가능 시 불투명도 복원 및 서버 이동 요청
	if (ItemDragOp->WidgetReference)
	{
		ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
	}

	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->RequestMoveItem(
			ItemDragOp->SourceInventoryGUID,
			InventoryGUID,
			ItemDragOp->DraggedItem.GUID,
			TargetTile,
			ItemDragOp->bCurrentRotated
		);
		return true;
	}

	if (TargetInventoryComp)
	{
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
	FVector2D TopLeftPos = LocalMousePos - DragOffset;
	int32 TileX = FMath::FloorToInt(TopLeftPos.X / TileSize);
	int32 TileY = FMath::FloorToInt(TopLeftPos.Y / TileSize);

	return FIntPoint(TileX, TileY);
}