// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/ItemWidget.h"
#include "UI/ItemDragDropOperation.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameMode/CustomPlayerState.h"

#include "Components/SizeBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/InventoryComponent.h"

void UItemWidget::InitWidget(const FItemInstance InItem, const FItemTableRow& InData, float InTileSize)
{
	ItemInstance = InItem;
	TileSize = InTileSize;
	CachedItemData = InData; // 💡 테이블 데이터 캐싱
	FIntPoint EffectiveSize = InItem.GetCurrentGridSize(&InData);

	// SizeBox 크기 동적 조절
	RootSizeBox->SetWidthOverride(EffectiveSize.X * TileSize);
	RootSizeBox->SetHeightOverride(EffectiveSize.Y * TileSize);

	UTexture2D* LoadedTex = InData.Icon.LoadSynchronous();

	if (LoadedTex)
	{
		ItemIcon->SetBrushFromTexture(LoadedTex);
	}
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
// 1. 마우스 누름 감지
FReply UItemWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		// 드래그 감지 등록
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
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
	DragOp->DraggedItem = ItemInstance;
	DragOp->bCurrentRotated = ItemInstance.bIsRotated;

	// 1. 현재 아이템 위젯의 회전 반영 격자 크기 가져오기
	FIntPoint GridSize = ItemInstance.GetCurrentGridSize(&CachedItemData);

	// 2. 위젯의 전체 픽셀 크기 계산
	float WidgetWidth = GridSize.X * TileSize;
	float WidgetHeight = GridSize.Y * TileSize;

	// 💡 3. 마우스 커서가 아이템 위젯의 정중앙에 오도록 오프셋 설정
	DragOp->DragOffset = FVector2D(WidgetWidth * 0.5f, WidgetHeight * 0.5f);

	// 4. Drag Visual 생성 및 설정
	UItemWidget* DragVisual = CreateWidget<UItemWidget>(GetOwningPlayer(), GetClass());
	if (DragVisual)
	{
		DragVisual->InitWidget(ItemInstance, CachedItemData, TileSize);
		DragOp->DefaultDragVisual = DragVisual;

		// Pivot 설정 (Visual 위젯 기준)
		DragOp->Pivot = EDragPivot::CenterCenter;
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

