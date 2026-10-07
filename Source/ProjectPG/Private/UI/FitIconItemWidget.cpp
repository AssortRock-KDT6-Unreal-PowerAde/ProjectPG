#include "UI/FitIconItemWidget.h"

#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Engine/Texture2D.h"

void UFitIconItemWidget::RefreshWidget()
{
	Super::RefreshWidget();
	if (!IconScale)
		return;

	// ① 그림 원래 크기를 브러시에 기록 → ScaleBox 가 그림 비율을 알고 늘리지 않고 맞춘다.
	//    (부모는 크기 없이 그림만 넣어서, 모든 그림이 정사각형으로 취급돼 찌그러졌다.)
	//    SetBrushFromTexture(그림, true) 는 쓰지 않는다: 부모가 이미 같은 그림을 넣어 두면 아무것도 안 하고 끝나서
	//    크기가 기록되지 않았다(10/7 비교 그림에서 긴 아이콘만 정사각형으로 찌그러짐). 크기를 직접 넣는다.
	if (ItemIcon && CachedItemData.Icon)
	{
		FSlateBrush Brush = ItemIcon->GetBrush();
		Brush.SetResourceObject(CachedItemData.Icon);
		Brush.ImageSize = FVector2D(CachedItemData.Icon->GetSizeX(), CachedItemData.Icon->GetSizeY());
		ItemIcon->SetBrush(Brush);
	}

	// ② 아이콘 자리: 평소엔 아이템 칸 전체(여백만 뺌). 돌린 아이템은 돌리기 전 칸 크기로 자리를 잡아
	//    가운데 기준 90° 돌린다(가로로 긴 총이 세로 칸에서도 크게 보이게).
	if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(IconScale->Slot))
	{
		if (ItemInstance.bIsRotated)
		{
			const FIntPoint BaseSize = CachedItemData.GridSize;
			IconSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			IconSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			IconSlot->SetOffsets(FMargin(0.0f, 0.0f,
				FMath::Max(1.0f, BaseSize.X * TileSize - 2.0f * IconPadding),
				FMath::Max(1.0f, BaseSize.Y * TileSize - 2.0f * IconPadding)));
		}
		else
		{
			IconSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
			IconSlot->SetAlignment(FVector2D::ZeroVector);
			IconSlot->SetOffsets(FMargin(IconPadding));
		}
	}
	IconScale->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
	IconScale->SetRenderTransformAngle(ItemInstance.bIsRotated ? 90.0f : 0.0f);
}
