// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ItemDragDropOperation.h"
#include "UI/ItemWidget.h"

void UItemDragDropOperation::RotateItem()
{
	bCurrentRotated = !bCurrentRotated;

	if (UItemWidget* DragVisual = Cast<UItemWidget>(DefaultDragVisual))
	{
		DragVisual->ItemInstance.bIsRotated = bCurrentRotated;
		DragVisual->RefreshWidget(); // 크기 재계산 (Width, Height 교체)

		// 회전된 위젯 크기에 맞춰 중앙 Pivot 위치(DragOffset) 재계산
		FIntPoint GridSize = DragVisual->ItemInstance.GetCurrentGridSize(DragVisual->GetCachedItemData());
		float NewWidth = GridSize.X * DragVisual->TileSize;
		float NewHeight = GridSize.Y * DragVisual->TileSize;

		DragOffset = FVector2D(NewWidth * 0.5f, NewHeight * 0.5f);
	}
}

void UItemDragDropOperation::DragCancelled_Implementation(const FPointerEvent& PointerEvent)
{
	Super::DragCancelled_Implementation(PointerEvent);

	// 드래그가 취소(드롭 실패)되었을 때 원본 위젯의 불투명도 복구
	if (WidgetReference)
	{
		WidgetReference->SetRenderOpacity(1.0f);
	}
}