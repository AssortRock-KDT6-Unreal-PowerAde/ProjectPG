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

		// 💡 회전된 위젯 크기에 맞춰 중앙 Pivot 위치(DragOffset) 재계산
		FIntPoint GridSize = DragVisual->ItemInstance.GetCurrentGridSize(DragVisual->GetCachedItemData());
		float NewWidth = GridSize.X * DragVisual->TileSize;
		float NewHeight = GridSize.Y * DragVisual->TileSize;

		DragOffset = FVector2D(NewWidth * 0.5f, NewHeight * 0.5f);
	}
}

void UItemDragDropOperation::DragCancelled_Implementation(const FPointerEvent& PointerEvent)
{
	Super::DragCancelled_Implementation(PointerEvent);
	if (WidgetReference)
	{
		WidgetReference->SetRenderOpacity(1.0f);
	}
}


