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
#include "GameFramework/PlayerState.h"

#include "UI/ItemContextWidget.h"
#include "UI/ItemWidget.h"
#include "UI/SlotWidget.h"
#include "UI/ItemDragDropOperation.h"

void UInventoryGridWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (IsValid(TargetInventoryComp)) return;

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (APlayerState* PS = PC->PlayerState)
		{
			if (UInventoryComponent* InvComp = PS->FindComponentByClass<UInventoryComponent>())
			{
				if (InventoryGUID.IsValid())
				{
					InvComp->SetInventorySizeByGuid(InventoryGUID, SlotSize);
					BindInventoryComponent(InvComp);
				}
			}
		}
	}
}

void UInventoryGridWidget::InitWidget(const FGuid& InvenGuid, FIntPoint GridSize)
{
	InventoryGUID = InvenGuid;

	if (APlayerController* PC = GetOwningPlayer())
	{
		if (APlayerState* PS = PC->PlayerState)
		{
			if (UInventoryComponent* InvComp = PS->FindComponentByClass<UInventoryComponent>())
			{
				InvComp->SetInventorySizeByGuid(InventoryGUID, GridSize);
				BindInventoryComponent(InvComp);
			}
		}
	}
}

void UInventoryGridWidget::CreateBackGroundGrid(int32 Columns, int32 Rows)
{
	if (!BackGroundGrid || !SlotWidgetClass || !InventorySizeBox) return;

	BackGroundGrid->ClearChildren();
	BackGroundGrid->SetSlotPadding(FMargin(0.0f));

	const float CalculatedWidth = Columns * TileSize;
	const float CalculatedHeight = Rows * TileSize;

	// 1. SizeBox의 고정 영역 설정
	InventorySizeBox->SetWidthOverride(CalculatedWidth);
	InventorySizeBox->SetHeightOverride(CalculatedHeight);

	// 2. 슬롯 생성 및 배치
	for (int32 r = 0; r < Rows; ++r)
	{
		for (int32 c = 0; c < Columns; ++c)
		{
			USlotWidget* SlotWidget = CreateWidget<USlotWidget>(this, SlotWidgetClass);

			UUniformGridSlot* GridSlot = BackGroundGrid->AddChildToUniformGrid(SlotWidget, r, c);
			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Center);
				GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Center);
			}
		}
	}
}

void UInventoryGridWidget::BindInventoryComponent(UInventoryComponent* InComp)
{
	TargetInventoryComp = InComp;
	if (TargetInventoryComp && InventoryGUID.IsValid())
	{
		int32 GridColumns = TargetInventoryComp->GetColumns(InventoryGUID);
		int32 GridRows = TargetInventoryComp->GetRows(InventoryGUID);

		CreateBackGroundGrid(GridColumns, GridRows);

		TargetInventoryComp->OnInventoryUpdated.RemoveAll(this);
		TargetInventoryComp->OnInventoryUpdated.AddDynamic(this, &UInventoryGridWidget::RefreshGridUI);

		RefreshGridUI();
	}
}

void UInventoryGridWidget::RefreshGridUI()
{
	if (!TargetInventoryComp || !ItemWidgetClass || !ItemCanvas || !InventoryGUID.IsValid()) return;

	ItemCanvas->ClearChildren();

	const TArray<FItemInstance>& Items = TargetInventoryComp->GetItems(InventoryGUID);

	for (const FItemInstance& Item : Items)
	{
		const FItemTableRow* Data = TargetInventoryComp->GetItemData(Item.ItemID);
		if (!Data) continue;

		UItemWidget* NewItemWidget = CreateWidget<UItemWidget>(this, ItemWidgetClass);
		NewItemWidget->InitWidget(Item, *Data, TileSize);

		UCanvasPanelSlot* CanvasSlot = ItemCanvas->AddChildToCanvas(NewItemWidget);
		if (CanvasSlot)
		{
			CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 0.0f, 0.0f));
			CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
			CanvasSlot->SetAutoSize(true);

			FVector2D DrawPos = FVector2D((float)Item.Position.X * TileSize, (float)Item.Position.Y * TileSize);
			CanvasSlot->SetPosition(DrawPos);
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

	int32 Columns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 Rows = TargetInventoryComp->GetRows(InventoryGUID);

	if (TargetTile.X < 0 || TargetTile.Y < 0) return false;
	if (TargetTile.X + GridSize.X > Columns) return false;
	if (TargetTile.Y + GridSize.Y > Rows) return false;

	for (int32 x = 0; x < GridSize.X; ++x)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			int32 CheckX = TargetTile.X + x;
			int32 CheckY = TargetTile.Y + y;

			if (const FItemInstance* ExistingItem = GetItemAtCell(CheckX, CheckY))
			{
				if (ExistingItem->GUID == ItemToPlace.GUID)
				{
					continue;
				}

				return false;
			}
		}
	}

	return true;
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
	if (!ItemDragOp || !TargetInventoryComp || !BackGroundGrid || !InventoryGUID.IsValid()) return false;

	FGeometry GridGeometry = BackGroundGrid->GetCachedGeometry();
	FVector2D LocalMousePos = GridGeometry.AbsoluteToLocal(InDragDropEvent.GetScreenSpacePosition());

	FVector2D AdjustedPos = LocalMousePos - ItemDragOp->DragOffset;

	int32 TargetTileX = FMath::FloorToInt(AdjustedPos.X / TileSize);
	int32 TargetTileY = FMath::FloorToInt(AdjustedPos.Y / TileSize);

	if (ItemDragOp->WidgetReference)
	{
		ItemDragOp->WidgetReference->SetRenderOpacity(1.0f);
	}

	return TargetInventoryComp->MoveItem(
		InventoryGUID,
		ItemDragOp->DraggedItem.GUID,
		FIntPoint(TargetTileX, TargetTileY),
		ItemDragOp->bCurrentRotated
	);
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

	FIntPoint GridSize = DragVisual->ItemInstance.GetCurrentGridSize(ItemData);
	FIntPoint TargetTile = MouseToTilePosition(LocalMousePos, ItemDragOp->DragOffset);

	int32 Columns = TargetInventoryComp->GetColumns(InventoryGUID);
	int32 Rows = TargetInventoryComp->GetRows(InventoryGUID);

	bool bCanPlace = CanPlaceItemAt(ItemDragOp->DraggedItem, TargetTile, GridSize);
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