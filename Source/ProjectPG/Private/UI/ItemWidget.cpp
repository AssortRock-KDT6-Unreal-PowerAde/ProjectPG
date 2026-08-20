// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ItemWidget.h"
#include "UI/ItemDragDropOperation.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameMode/CustomPlayerState.h"

#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/InventoryComponent.h"
#include "UI/ItemContextWidget.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Core/UIManagerSubSystem.h"
void UItemWidget::InitWidget(const FItemInstance InItem, const FItemTableRow& InData, float InTileSize)
{
	ItemInstance = InItem;
	TileSize = InTileSize;
	CachedItemData = InData; // 💡 테이블 데이터 캐싱
	FIntPoint EffectiveSize = InItem.GetCurrentGridSize(&InData);

	// SizeBox 크기 동적 조절
	RootSizeBox->SetWidthOverride(EffectiveSize.X * TileSize);
	RootSizeBox->SetHeightOverride(EffectiveSize.Y * TileSize);

	ItemIcon->SetBrushFromTexture(InData.Icon);
	if (InItem.StackCount > 1)
	{
		TextStackCount->SetText(FText::AsNumber(InItem.StackCount));
		TextStackCount->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	}
	else
	{
		TextStackCount->SetVisibility(ESlateVisibility::Collapsed);
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

		_ContextWidget = Cast<UItemContextWidget>(subsystem->OpenUI(EUIType::ItemContext));
		if (nullptr == _ContextWidget) return FReply::Handled();

		_ContextWidget->SetItem(ItemInstance);
		_ContextWidget->SetVisibility(ESlateVisibility::Visible);
		_ContextWidget->UpdateButtonState(ItemInstance.type);
		// 마우스 절대 위치 가져오기
		FVector2D ScreenPosition = InMouseEvent.GetScreenSpacePosition();

		// Viewport 스케일링(DPI)을 고려하여 위치 설정 (RemoveDPIScale = true)
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

// 2. 드래그 시작 시 Drag Visual 및 DragDropOperation 생성
void UItemWidget::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

	UItemDragDropOperation* DragOp = NewObject<UItemDragDropOperation>();
	if (!DragOp) return;

	DragOp->WidgetReference = this;
	DragOp->DraggedItem = ItemInstance; // 현재 아이템 정보
	DragOp->bCurrentRotated = ItemInstance.bIsRotated;

	// 💡 [핵심 1] 클릭한 마우스 위치와 위젯 좌상단 간의 실제 상대 거리를 DragOffset으로 저장
	FVector2D LocalMousePos = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
	DragOp->DragOffset = LocalMousePos;

	// Drag Visual 생성 및 설정
	UItemWidget* DragVisual = CreateWidget<UItemWidget>(GetOwningPlayer(), GetClass());
	if (DragVisual)
	{
		DragVisual->InitWidget(ItemInstance, CachedItemData, TileSize);
		DragOp->DefaultDragVisual = DragVisual;

		// 💡 [핵심 2] DragOffset을 마우스 클릭 위치로 직접 지정했으므로 Pivot은 MouseDown을 사용합니다.
		DragOp->Pivot = EDragPivot::MouseDown;
	}

	SetRenderOpacity(0.5f);

	OutOperation = DragOp;
}
void UItemWidget::RefreshWidget()
{
	if (!RootSizeBox) return;
	FIntPoint GridSize = ItemInstance.GetCurrentGridSize(&CachedItemData);
	// 💡 저장해둔 OriginalSizeX, Y를 기반으로 스왑 계산
	RootSizeBox->SetWidthOverride(GridSize.X * TileSize);
	RootSizeBox->SetHeightOverride(GridSize.Y * TileSize);
}



