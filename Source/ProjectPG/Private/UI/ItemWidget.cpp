// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ItemWidget.h"
#include "UI/ItemDragDropOperation.h"
#include "UI/ItemContextWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"


#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"


#include "Blueprint/WidgetLayoutLibrary.h"
#include "Core/UIManagerSubSystem.h"

void UItemWidget::InitWidget(const FItemInstance InItem, const FItemTableRow& InData, const FGuid& InInvenGUID, float InTileSize)
{
	ItemInstance = InItem;
	OwnerInventoryGUID = InInvenGUID; // 💡 출처 인벤토리 GUID 저장
	TileSize = InTileSize;
	CachedItemData = InData; // 테이블 데이터 캐싱

	FIntPoint EffectiveSize = InItem.GetCurrentGridSize(&InData);

	// SizeBox 크기 동적 조절
	if (RootSizeBox)
	{
		RootSizeBox->SetWidthOverride (EffectiveSize.X * TileSize);
		RootSizeBox->SetHeightOverride(EffectiveSize.Y * TileSize);
	}

	// SoftObjectPath / SoftObjectPtr 동기 로드 및 브러시 설정
	if (ItemIcon)
	{
		if (UTexture2D* IconTex = InData.Icon)
		{
			ItemIcon->SetBrushFromTexture(IconTex);
		}
	}

	if (TextStackCount)
	{
		if (InItem.StackCount > 1)
		{
			TextStackCount->SetText(FText::AsNumber(InItem.StackCount));
			TextStackCount->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			TextStackCount->SetVisibility(ESlateVisibility::Collapsed);
		}
	}

	RefreshWidget();
}

void UItemWidget::SetContextWidget(UItemContextWidget* widget)
{
	_ContextWidget = widget;
}

// 1. 마우스 누름 감지
FReply UItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// 드래그 감지 등록
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	else if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		UUIManagerSubSystem* subsystem = UUIManagerSubSystem::Get(GetWorld());
		if (!subsystem) return FReply::Handled();

		_ContextWidget = Cast<UItemContextWidget>(subsystem->OpenUI(EUIType::ItemContext));
		if (!_ContextWidget) return FReply::Handled();
		APlayerController* PC = GetOwningPlayer();
		_ContextWidget->SetItem(ItemInstance);
		_ContextWidget->SetVisibility(ESlateVisibility::Visible);
		_ContextWidget->UpdateButtonState(ItemInstance.type);
		

		// 마우스 절대 위치 가져오기
		FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();

		// Viewport 스케일링(DPI)을 고려하여 위치 설정
		_ContextWidget->SetPositionInViewport(ScreenPosition, true);

		return FReply::Handled();
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

bool UItemWidget::NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	SetRenderOpacity(1.0f);
	return Super::NativeOnDrop(MyGeometry, InDragDropEvent, InOperation);
}

void UItemWidget::NativeOnDragDetected(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(
		InGeometry,
		InMouseEvent,
		OutOperation);

	UItemDragDropOperation* DragOp =
		NewObject<UItemDragDropOperation>();

	if (!DragOp)
	{
		return;
	}

	DragOp->WidgetReference = this;
	DragOp->DraggedItem = ItemInstance;
	DragOp->SourceInventoryGUID = OwnerInventoryGUID;
	DragOp->bCurrentRotated =
		ItemInstance.bIsRotated;

	// =========================================================
	// ★ 모든 드래그의 공통 좌표 기준
	//
	// Mouse - Widget TopLeft
	// =========================================================

	const FVector2D MouseAbsolute =
		InMouseEvent.GetScreenSpacePosition();

	const FVector2D WidgetTopLeftAbsolute =
		InGeometry.GetAbsolutePosition();

	DragOp->DragOffsetAbs =
		MouseAbsolute - WidgetTopLeftAbsolute;

	// 호환용
	DragOp->DragOffset =
		InGeometry.AbsoluteToLocal(MouseAbsolute);

	// =========================================================
	// Drag Visual
	// =========================================================

	UItemWidget* DragVisual =
		CreateWidget<UItemWidget>(
			GetOwningPlayer(),
			GetClass());

	if (DragVisual)
	{
		DragVisual->InitWidget(
			ItemInstance,
			CachedItemData,
			OwnerInventoryGUID,
			TileSize);

		DragOp->DefaultDragVisual =
			DragVisual;
	}

	DragOp->Pivot =
		EDragPivot::MouseDown;

	SetRenderOpacity(0.5f);

	OutOperation = DragOp;
}

void UItemWidget::RefreshWidget()
{
	if (!RootSizeBox) return;

	FIntPoint GridSize = ItemInstance.GetCurrentGridSize(&CachedItemData);
	RootSizeBox->SetWidthOverride(GridSize.X * TileSize);
	RootSizeBox->SetHeightOverride(GridSize.Y * TileSize);
}