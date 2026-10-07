// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/ItemWidget.h"
#include "UI/ItemDragDropOperation.h"
#include "UI/ItemContextWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"

#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"


#include "Blueprint/WidgetLayoutLibrary.h"
#include "Core/UIManagerSubSystem.h"
#include <Blueprint/SlateBlueprintLibrary.h>
#include "UI/ItemTooltipWidget.h"
#include "TimerManager.h"

void UItemWidget::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);
	if (UWorld* World = GetWorld())
		World->GetTimerManager().SetTimer(TooltipTimer, this, &UItemWidget::ShowTooltip, TooltipDelaySeconds, false);
}

void UItemWidget::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseLeave(InMouseEvent);
	HideTooltip();
}

void UItemWidget::NativeDestruct()
{
	HideTooltip();
	Super::NativeDestruct();
}

void UItemWidget::ShowTooltip()
{
	// 끌고 있는 중이면 띄우지 않는다.
	if (UWidgetBlueprintLibrary::IsDragDropping())
		return;
	UItemTooltipWidget::ShowFor(this, ItemInstance, CachedItemData);
}

void UItemWidget::HideTooltip()
{
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(TooltipTimer);
	UItemTooltipWidget::Hide(this);
}

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
			// true = 그림 원래 크기를 브러시에 기록 → ScaleBox 가 그림 비율을 알고 늘리지 않고 맞춘다.
			ItemIcon->SetBrushFromTexture(IconTex, true);
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

FReply UItemWidget::NativeOnMouseButtonDown(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent)
{

	UE_LOG(
		LogTemp,
		Error,
		TEXT("[ItemWidget] CLICK %s GUID=%s"),
		*InMouseEvent.GetEffectingButton().ToString(),
		*ItemInstance.GUID.ToString()
	);

	if (InMouseEvent.GetEffectingButton() ==
		EKeys::RightMouseButton)
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("[ItemWidget] RIGHT CLICK SUCCESS")
		);

		UUIManagerSubSystem* Subsystem =
			UUIManagerSubSystem::Get(GetWorld());

		if (Subsystem)
		{
			UItemContextWidget* ContextWidget =
				Cast<UItemContextWidget>(
					Subsystem->OpenUI(
						EUIType::ItemContext
					)
				);

			if (ContextWidget)
			{
				ContextWidget->SetItem(ItemInstance);
				ContextWidget->UpdateButtonState(
					ItemInstance.type
				);

				ContextWidget->SetVisibility(
					ESlateVisibility::Visible
				);

				FVector2D MousePosition =
					UWidgetLayoutLibrary::
					GetMousePositionOnViewport(
						GetWorld()
					);

				ContextWidget->SetPositionInViewport(
					MousePosition,
					false
				);
			}
		}

		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() ==
		EKeys::LeftMouseButton)
	{
		UUIManagerSubSystem* Subsystem =
			UUIManagerSubSystem::Get(GetWorld());

		if (Subsystem)
		{
			Subsystem->CloseItemContext();
		}

		return UWidgetBlueprintLibrary::
			DetectDragIfPressed(
				InMouseEvent,
				this,
				EKeys::LeftMouseButton
			).NativeReply;
	}

	return Super::NativeOnMouseButtonDown(
		InGeometry,
		InMouseEvent
	);
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

	HideTooltip();

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

	// 아이콘 자리 = 돌리기 전 칸 크기(여백 뺌). 돌린 아이템은 그 자리를 가운데 기준 90° 돌려 칸에 맞춘다.
	// (ScaleBox 가 그림 비율을 지켜 자리 안에 맞춰 줄이므로 긴 총이 정사각형 칸에서도 찌그러지지 않는다.)
	if (IconScale)
	{
		if (UCanvasPanelSlot* IconSlot = Cast<UCanvasPanelSlot>(IconScale->Slot))
		{
			const FIntPoint BaseSize = CachedItemData.GridSize;
			IconSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			IconSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			IconSlot->SetPosition(FVector2D::ZeroVector);
			IconSlot->SetSize(FVector2D(
				FMath::Max(1.0f, BaseSize.X * TileSize - 2.0f * IconPadding),
				FMath::Max(1.0f, BaseSize.Y * TileSize - 2.0f * IconPadding)));
		}
		IconScale->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		IconScale->SetRenderTransformAngle(ItemInstance.bIsRotated ? 90.0f : 0.0f);
	}
}