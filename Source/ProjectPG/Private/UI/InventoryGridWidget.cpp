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
#include "Components/OverlaySlot.h"

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

FIntPoint UInventoryGridWidget::CalculateDropTile(
	const FVector2D& ScreenMousePosition,
	UItemDragDropOperation* DragOp) const
{
	if (!DragOp ||
		!BackGroundGrid ||
		!TargetInventoryComp ||
		!InventoryGUID.IsValid())
	{
		return FIntPoint(-1, -1);
	}

	const int32 Columns =
		TargetInventoryComp->GetColumns(InventoryGUID);

	const int32 Rows =
		TargetInventoryComp->GetRows(InventoryGUID);

	if (Columns <= 0 || Rows <= 0)
	{
		return FIntPoint(-1, -1);
	}

	/*
	 * 실제 화면에 렌더링된 Grid의 Geometry
	 */
	const FGeometry GridGeometry =
		BackGroundGrid->GetCachedGeometry();

	const FVector2D GridLocalSize =
		GridGeometry.GetLocalSize();

	if (GridLocalSize.X <= 0.f ||
		GridLocalSize.Y <= 0.f)
	{
		return FIntPoint(-1, -1);
	}

	/*
	 * ★ 중요
	 *
	 * TileSize(64)가 아니라
	 * 실제 Grid가 화면에 차지하는 크기로 계산한다.
	 *
	 * 현재:
	 *
	 * 500 / 10 = 50
	 * 750 / 15 = 50
	 */
	const float ActualTileWidth =
		GridLocalSize.X / static_cast<float>(Columns);

	const float ActualTileHeight =
		GridLocalSize.Y / static_cast<float>(Rows);

	if (ActualTileWidth <= 0.f ||
		ActualTileHeight <= 0.f)
	{
		return FIntPoint(-1, -1);
	}

	/*
	 * 마우스 위치에서 드래그 시작점 offset 제거
	 *
	 * 결과:
	 * Item의 좌상단 Absolute 위치
	 */
	const FVector2D ItemTopLeftAbsolute =
		ScreenMousePosition - DragOp->DragOffsetAbs;

	/*
	 * Absolute → Grid Local
	 */
	const FVector2D ItemTopLeftGrid =
		GridGeometry.AbsoluteToLocal(
			ItemTopLeftAbsolute);

	/*
	 * 실제 Grid 크기 기준으로 Tile 계산
	 */
	const int32 TileX =
		FMath::FloorToInt(
			ItemTopLeftGrid.X / ActualTileWidth);

	const int32 TileY =
		FMath::FloorToInt(
			ItemTopLeftGrid.Y / ActualTileHeight);

	if (TileX < 0 ||
		TileY < 0 ||
		TileX >= Columns ||
		TileY >= Rows)
	{
		return FIntPoint(-1, -1);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT(
			"[CalculateDropTile] "
			"Grid=(%.2f,%.2f) "
			"ActualTile=(%.2f,%.2f) "
			"TileSize=%.2f "
			"Tile=(%d,%d)"
		),
		GridLocalSize.X,
		GridLocalSize.Y,
		ActualTileWidth,
		ActualTileHeight,
		TileSize,
		TileX,
		TileY);

	return FIntPoint(TileX, TileY);
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
	}
}

// DB의 Columns, Rows 수치로 빈 배경 슬롯 격자 생성
void UInventoryGridWidget::CreateBackGroundGrid(int32 Columns, int32 Rows)
{
	if (!BackGroundGrid || !SlotWidgetClass || !InventorySizeBox)
	{
		return;
	}

	if (Columns <= 0 || Rows <= 0)
	{
		return;
	}

	// =========================================================
	// InventorySizeBox 크기
	// =========================================================

	const float CalculatedWidth =
		static_cast<float>(Columns) * TileSize;

	const float CalculatedHeight =
		static_cast<float>(Rows) * TileSize;

	InventorySizeBox->SetWidthOverride(CalculatedWidth);
	InventorySizeBox->SetHeightOverride(CalculatedHeight);

	// =========================================================
	// ★ 중요
	// Overlay 안에서 BackGroundGrid가 SizeBox 전체를
	// 차지하도록 강제
	// =========================================================

	if (UOverlaySlot* OverlaySlot =
		Cast<UOverlaySlot>(BackGroundGrid->Slot))
	{
		OverlaySlot->SetHorizontalAlignment(
			EHorizontalAlignment::HAlign_Fill);

		OverlaySlot->SetVerticalAlignment(
			EVerticalAlignment::VAlign_Fill);

		OverlaySlot->SetPadding(FMargin(0.f));
	}

	// =========================================================
	// ItemCanvas도 Overlay 전체를 차지하도록 강제
	// =========================================================

	if (ItemCanvas)
	{
		if (UOverlaySlot* OverlaySlot =
			Cast<UOverlaySlot>(ItemCanvas->Slot))
		{
			OverlaySlot->SetHorizontalAlignment(
				EHorizontalAlignment::HAlign_Fill);

			OverlaySlot->SetVerticalAlignment(
				EVerticalAlignment::VAlign_Fill);

			OverlaySlot->SetPadding(FMargin(0.f));
		}
	}

	// =========================================================
	// Background Grid 생성
	// =========================================================

	BackGroundGrid->ClearChildren();
	BackGroundGrid->SetSlotPadding(FMargin(0.f));

	for (int32 Row = 0; Row < Rows; ++Row)
	{
		for (int32 Column = 0; Column < Columns; ++Column)
		{
			USlotWidget* SlotWidget =
				CreateWidget<USlotWidget>(
					GetWorld(),
					SlotWidgetClass);

			if (!SlotWidget)
			{
				continue;
			}

			UUniformGridSlot* GridSlot =
				BackGroundGrid->AddChildToUniformGrid(
					SlotWidget,
					Row,
					Column);

			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(
					EHorizontalAlignment::HAlign_Fill);

				GridSlot->SetVerticalAlignment(
					EVerticalAlignment::VAlign_Fill);

			}
		}
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[CreateBackGroundGrid] Columns=%d Rows=%d Size=(%.1f,%.1f)"),
		Columns,
		Rows,
		CalculatedWidth,
		CalculatedHeight);
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
	if (!ItemCanvas ||
		!TargetInventoryComp ||
		!InventoryGUID.IsValid())
	{
		return;
	}

	ItemCanvas->ClearChildren();

	UItemSubSystem* ItemSubSystem =
		UItemSubSystem::Get(GetWorld());

	if (!ItemSubSystem)
	{
		return;
	}

	const TArray<FItemInstance>& Items =
		TargetInventoryComp->GetItems(InventoryGUID);

	for (const FItemInstance& Item : Items)
	{
		const FItemTableRow* ItemData =
			ItemSubSystem->GetItem(Item.ItemID);

		if (!ItemData)
		{
			continue;
		}

		UItemWidget* ItemWidget =
			CreateWidget<UItemWidget>(
				GetWorld(),
				ItemWidgetClass);

		if (!ItemWidget)
		{
			continue;
		}

		const FIntPoint CurrentGridSize =
			Item.GetCurrentGridSize(ItemData);

		ItemWidget->InitWidget(
			Item,
			*ItemData,
			InventoryGUID,
			TileSize);

		UCanvasPanelSlot* CanvasSlot =
			ItemCanvas->AddChildToCanvas(ItemWidget);

		if (!CanvasSlot)
		{
			continue;
		}

		const FVector2D PositionPixel(
			Item.Position.X * TileSize,
			Item.Position.Y * TileSize);

		const FVector2D SizePixel(
			CurrentGridSize.X * TileSize,
			CurrentGridSize.Y * TileSize);

		CanvasSlot->SetPosition(PositionPixel);
		CanvasSlot->SetSize(SizePixel);
		CanvasSlot->SetZOrder(10);

		
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

bool UInventoryGridWidget::NativeOnDrop(
	const FGeometry& MyGeometry,
	const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	ClearSlotHighlights();

	UItemDragDropOperation* ItemDragOp =
		Cast<UItemDragDropOperation>(InOperation);

	if (!ItemDragOp ||
		!BackGroundGrid ||
		!TargetInventoryComp ||
		!InventoryGUID.IsValid())
	{
		return false;
	}

	// =========================================================
	// Drag Visual
	// =========================================================

	UItemWidget* DragVisual =
		Cast<UItemWidget>(
			ItemDragOp->DefaultDragVisual);

	if (!DragVisual)
	{
		return false;
	}

	const FItemTableRow* ItemData =
		DragVisual->GetCachedItemData();

	if (!ItemData)
	{
		return false;
	}

	// =========================================================
	// 현재 회전 상태
	// =========================================================

	FItemInstance TempInstance =
		ItemDragOp->DraggedItem;

	TempInstance.bIsRotated =
		ItemDragOp->bCurrentRotated;

	const FIntPoint GridSize =
		TempInstance.GetCurrentGridSize(ItemData);

	// =========================================================
	// ★ 중요
	//
	// Drop 위치도 DragOver와 완전히 동일한 함수 사용
	// =========================================================

	const FVector2D MouseScreenPosition =
		InDragDropEvent.GetScreenSpacePosition();

	const FIntPoint TargetTile =
		CalculateDropTile(
			MouseScreenPosition,
			ItemDragOp);

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NativeOnDrop] TargetTile=(%d,%d) Mouse=(%.1f,%.1f) OffsetAbs=(%.1f,%.1f)"),
		TargetTile.X,
		TargetTile.Y,
		MouseScreenPosition.X,
		MouseScreenPosition.Y,
		ItemDragOp->DragOffsetAbs.X,
		ItemDragOp->DragOffsetAbs.Y);

	if (TargetTile.X < 0 ||
		TargetTile.Y < 0)
	{
		if (ItemDragOp->WidgetReference)
		{
			ItemDragOp->WidgetReference
				->SetRenderOpacity(1.0f);
		}

		return false;
	}

	// =========================================================
	// 배치 가능 여부
	// =========================================================

	const bool bCanPlace =
		CanPlaceItemAt(
			TempInstance,
			TargetTile,
			GridSize);

	if (!bCanPlace)
	{
		if (ItemDragOp->WidgetReference)
		{
			ItemDragOp->WidgetReference
				->SetRenderOpacity(1.0f);
		}

		UE_LOG(
			LogTemp,
			Warning,
			TEXT("[NativeOnDrop] 배치 불가능 TargetTile=(%d,%d)"),
			TargetTile.X,
			TargetTile.Y);

		return false;
	}

	// =========================================================
	// 장비 슬롯 → 인벤토리
	// =========================================================

	if (ItemDragOp->bFromEquip)
	{
		if (ItemDragOp->WidgetReference)
		{
			if (UEquipSlot* SrcSlot =
				Cast<UEquipSlot>(
					ItemDragOp->WidgetReference))
			{
				SrcSlot->RequestUnEquip();
			}
		}

		const FGuid SourceGuid =
			ItemDragOp->DraggedItem.parent_inventory_guid;

		TempInstance.parent_inventory_guid =
			InventoryGUID;

		const bool bAdded =
			TargetInventoryComp->AddItemAt(
				TempInstance,
				TargetTile);

		if (bAdded)
		{
			if (UWebSocketSubSystem* Web =
				UWebSocketSubSystem::Get(GetWorld()))
			{
				Web->RequestEquipItem(
					TempInstance.GUID,
					InventoryGUID,
					false);

				Web->RequestMoveItem(
					SourceGuid,
					InventoryGUID,
					TempInstance.GUID,
					TargetTile,
					TempInstance.bIsRotated);
			}

			UE_LOG(
				LogTemp,
				Warning,
				TEXT("[NativeOnDrop] Unequip Success Target=(%d,%d)"),
				TargetTile.X,
				TargetTile.Y);
		}

		if (ItemDragOp->WidgetReference)
		{
			ItemDragOp->WidgetReference
				->SetRenderOpacity(1.0f);

			if (UEquipSlot* SrcSlot =
				Cast<UEquipSlot>(
					ItemDragOp->WidgetReference))
			{
				SrcSlot->Clear();
			}
		}

		return bAdded;
	}

	// =========================================================
	// 일반 인벤토리 이동
	// =========================================================

	const bool bMoved =
		TargetInventoryComp->MoveItem(
			InventoryGUID,
			ItemDragOp->DraggedItem.GUID,
			TargetTile,
			ItemDragOp->bCurrentRotated);

	if (ItemDragOp->WidgetReference)
	{
		ItemDragOp->WidgetReference
			->SetRenderOpacity(1.0f);
	}

	UE_LOG(
		LogTemp,
		Warning,
		TEXT("[NativeOnDrop] MoveItem Target=(%d,%d) Success=%s"),
		TargetTile.X,
		TargetTile.Y,
		bMoved ? TEXT("true") : TEXT("false"));

	return bMoved;
}

bool UInventoryGridWidget::NativeOnDragOver(
	const FGeometry& InGeometry,
	const FDragDropEvent& InDragDropEvent,
	UDragDropOperation* InOperation)
{
	Super::NativeOnDragOver(
		InGeometry,
		InDragDropEvent,
		InOperation);

	if (!TargetInventoryComp ||
		!BackGroundGrid ||
		!InventoryGUID.IsValid())
	{
		return false;
	}

	UItemDragDropOperation* ItemDragOp =
		Cast<UItemDragDropOperation>(InOperation);

	if (!ItemDragOp)
	{
		return false;
	}

	UItemWidget* DragVisual =
		Cast<UItemWidget>(ItemDragOp->DefaultDragVisual);

	if (!DragVisual)
	{
		return false;
	}

	ClearSlotHighlights();

	const FItemTableRow* ItemData =
		DragVisual->GetCachedItemData();

	if (!ItemData)
	{
		return false;
	}

	// 현재 회전 상태 적용
	FItemInstance TempInstance =
		ItemDragOp->DraggedItem;

	TempInstance.bIsRotated =
		ItemDragOp->bCurrentRotated;

	const FIntPoint GridSize =
		TempInstance.GetCurrentGridSize(ItemData);

	const int32 Columns =
		TargetInventoryComp->GetColumns(InventoryGUID);

	const int32 Rows =
		TargetInventoryComp->GetRows(InventoryGUID);

	// ---------------------------------------------------------
	// ★ 핵심
	// 하이라이트 위치 계산을 CalculateDropTile 하나로 통일
	// ---------------------------------------------------------

	const FVector2D MouseScreenPosition =
		InDragDropEvent.GetScreenSpacePosition();

	const FIntPoint TargetTile =
		CalculateDropTile(
			MouseScreenPosition,
			ItemDragOp);

	LastHoveredTile = TargetTile;

	// ---------------------------------------------------------
	// 배치 가능 여부
	// ---------------------------------------------------------

	const bool bCanPlace =
		CanPlaceItemAt(
			TempInstance,
			TargetTile,
			GridSize);

	const EBorderHighlightState HighlightState =
		bCanPlace
		? EBorderHighlightState::Valid
		: EBorderHighlightState::Invalid;

	// ---------------------------------------------------------
	// 하이라이트
	// ---------------------------------------------------------

	for (int32 x = 0; x < GridSize.X; ++x)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			const int32 CheckX =
				TargetTile.X + x;

			const int32 CheckY =
				TargetTile.Y + y;

			if (CheckX >= 0 &&
				CheckX < Columns &&
				CheckY >= 0 &&
				CheckY < Rows)
			{
				if (USlotWidget* SlotWidget =
					GetSlotWidgetAt(CheckX, CheckY))
				{
					SlotWidget->SetHighlightState(
						HighlightState);

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

	FVector2D TopLeftPos = LocalMousePos;

	int32 TileX = FMath::FloorToInt((TopLeftPos.X + (TileSize * 0.1f)) / TileSize);
	int32 TileY = FMath::FloorToInt((TopLeftPos.Y + (TileSize * 0.1f)) / TileSize);

	if (TileX < 0) TileX = -1;
	if (TileY < 0) TileY = -1;

	return FIntPoint(TileX, TileY);
}