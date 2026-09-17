// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/EquipSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Common/TableData.h"
#include "Core/ItemSubSystem.h"
#include <UI/ItemDragDropOperation.h>
#include <UI/ItemWidget.h>
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Server/WebSocketSubSystem.h"

void UEquipSlot::NativeConstruct()
{
	Super::NativeConstruct();
	Clear();
}

void UEquipSlot::SetSlot(EEquipSlot InSlot)
{
	Slot = InSlot;
}

void UEquipSlot::SetItem(const FItemInstance* InItem)
{
	Item = InItem;
	if (!Item)
	{
		Clear();
		return;
	}

	UItemSubSystem* ItemSubsystem = UItemSubSystem::Get(GetWorld());
	if (!ItemSubsystem)
	{
		Clear();
		return;
	}

	const FItemTableRow* Data = ItemSubsystem->GetItem(Item->ItemID);
	if (!Data)
	{
		Clear();
		return;
	}

	// 1. 아이콘 UI 갱신
	if (Icon)
	{
		if (Data->Icon)
		{
			Icon->SetBrushFromTexture(Data->Icon, true);
			Icon->SetVisibility(ESlateVisibility::Visible);
		}
		else
		{
			Icon->SetVisibility(ESlateVisibility::Hidden);
		}
	}

	// 2. 아이템 이름 UI 갱신 (Icon과 독립적으로 안전하게 체크)
	if (ItemName)
	{
		ItemName->SetText(FText::FromName(Data->DisPlayName));
		ItemName->SetVisibility(ESlateVisibility::Visible);
	}
}

void UEquipSlot::Clear()
{
	// 방어 코드: 드래그 중인 아이템이 이 슬롯과 관련 있으면 Clear를 건너뜁니다.
	if (UDragDropOperation* Op = UWidgetBlueprintLibrary::GetDragDroppingContent())
	{
		if (UItemDragDropOperation* ItemOp = Cast<UItemDragDropOperation>(Op))
		{
			// 출처가 이 슬롯이거나 드래그 중인 아이템 GUID가 현재 장착된 아이템과 같다면 Clear 무시
			if (ItemOp->WidgetReference == this)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[EquipSlot::Clear] Skip clear because drag originates from this slot. Slot=%d"), (int32)Slot);
				return;
			}
			if (Item && ItemOp->DraggedItem.GUID == Item->GUID)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[EquipSlot::Clear] Skip clear because dragged item matches equipped item. Slot=%d GUID=%s"), (int32)Slot, *Item->GUID.ToString());
				return;
			}
		}
	}

	UE_LOG(LogTemp, Verbose, TEXT("[EquipSlot::Clear] Clearing slot=%d"), (int32)Slot);
	Item = nullptr;

	if (Icon)
	{
		Icon->SetBrushFromTexture(nullptr);
		Icon->SetVisibility(ESlateVisibility::Hidden);
	}

	if (ItemName)
	{
		ItemName->SetText(FText::GetEmpty());
		ItemName->SetVisibility(ESlateVisibility::Hidden);
	}

	// Ensure slot background/border remains visible so slot does not appear removed
	if (SlotBorder)
	{
		SlotBorder->SetVisibility(ESlateVisibility::Visible);
		SlotBorder->SetBrushColor(FLinearColor::Transparent);
	}
}

void UEquipSlot::InitWidget(UEquipComponent* InEquip, UInventoryComponent* InInvetory)
{
	EquipComp = InEquip;
	InvenComp = InInvetory;
}


bool UEquipSlot::NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDrop(MyGeometry, InDragDropEvent, InOperation);
	UItemDragDropOperation* ItemOp = Cast<UItemDragDropOperation>(InOperation);
	if (!ItemOp || !EquipComp) return false;

	// 드래그가 인벤토리에서 온 경우 -> 장착 시도
	if (!ItemOp->bFromEquip)
	{
		SetHighlightState(EBorderHighlightState::None);

		// 아이템이 이 슬롯에 장착 가능한지 확인
		if (!EquipComp->CanEquip(ItemOp->DraggedItem, Slot))
		{
			if (ItemOp->WidgetReference)
				ItemOp->WidgetReference->SetRenderOpacity(1.0f);

			return false;
		}

		// 장착 수행 (로컬 적용 및 서버 요청은 Equip 내부에서 처리)
		bool bResult = EquipComp->Equip(ItemOp->DraggedItem);
		if (ItemOp->WidgetReference)
			ItemOp->WidgetReference->SetRenderOpacity(1.0f);


		return bResult;
	}


	return false;
}

bool UEquipSlot::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	bool  overResult = Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
	UItemDragDropOperation* ItemOp = Cast<UItemDragDropOperation>(InOperation);
	if (!ItemOp) return false;

	// 인벤토리에서 온 드래그는 장착 가능 여부에 따라 하이라이트
	if (!ItemOp->bFromEquip && EquipComp)
	{
		bool Result = EquipComp->CanEquip(ItemOp->DraggedItem, Slot);
		if (Result)
		{
			SetHighlightState(EBorderHighlightState::Valid);
		}
		else
		{
			SetHighlightState(EBorderHighlightState::Invalid);

		}
		return Result;
	}
	return overResult;
}

void UEquipSlot::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);
	// 드래그가 떠나면 아이콘 색상 원상복구
	if (Icon)
	{
		Icon->SetColorAndOpacity(FLinearColor::White);
	}
	// Restore highlight state to None
	UE_LOG(LogTemp, Verbose, TEXT("[EquipSlot::NativeOnDragLeave] Slot=%d DragLeft"), (int32)Slot);
	SetHighlightState(EBorderHighlightState::None);

}

void UEquipSlot::NativeOnDragDetected(
	const FGeometry& InGeometry,
	const FPointerEvent& InMouseEvent,
	UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(
		InGeometry,
		InMouseEvent,
		OutOperation);

	if (!Item)
	{
		return;
	}

	if (!EquipComp)
	{
		return;
	}

	UItemDragDropOperation* DragOp =
		NewObject<UItemDragDropOperation>();

	if (!DragOp)
	{
		return;
	}

	// =========================================================
	// 드래그 기본 정보
	// =========================================================

	DragOp->bFromEquip = true;
	DragOp->DraggedItem = *Item;
	DragOp->bCurrentRotated = Item->bIsRotated;

	// =========================================================
	// ★ 핵심
	//
	// 마우스가 클릭된 위치와
	// EquipSlot 좌상단의 차이를 저장
	//
	// 이 값은 Absolute 좌표 기준이다.
	// InventoryGrid에서도 동일한 기준으로 사용한다.
	// =========================================================

	const FVector2D MouseAbsolute =
		InMouseEvent.GetScreenSpacePosition();

	const FVector2D SlotTopLeftAbsolute =
		InGeometry.GetAbsolutePosition();

	DragOp->DragOffsetAbs =
		MouseAbsolute - SlotTopLeftAbsolute;

	// 기존 DragOffset은 호환용으로만 저장
	// 실제 Inventory 배치 계산에서는 사용하지 않는다.
	DragOp->DragOffset =
		InGeometry.AbsoluteToLocal(MouseAbsolute);

	// =========================================================
	// Drag Visual 생성
	// =========================================================

	UItemSubSystem* ItemSubsystem =
		UItemSubSystem::Get(GetWorld());

	if (ItemSubsystem)
	{
		const FItemTableRow* Data =
			ItemSubsystem->GetItem(Item->ItemID);

		if (Data)
		{
			UItemWidget* Visual =
				CreateWidget<UItemWidget>(
					GetOwningPlayer(),
					UItemWidget::StaticClass());

			if (Visual)
			{
				// Inventory와 동일한 TileSize를 사용하는 것이 가장 좋음.
				// 일단 기존 64.f 유지.
				Visual->InitWidget(
					*Item,
					*Data,
					FGuid(),
					64.0f);

				DragOp->DefaultDragVisual =
					Visual;
			}
		}
	}

	// =========================================================
	// 출발 Widget 저장
	// =========================================================

	DragOp->WidgetReference = this;

	// 마우스 클릭 위치를 Pivot으로 사용
	DragOp->Pivot = EDragPivot::MouseDown;

	UE_LOG(
		LogTemp,
		Warning,
		TEXT(
			"[EquipSlot::NativeOnDragDetected] "
			"ItemGUID=%s "
			"DragOffsetAbs=(%.2f, %.2f) "
			"DragOffset=(%.2f, %.2f) "
			"bFromEquip=%d"
		),
		*DragOp->DraggedItem.GUID.ToString(),
		DragOp->DragOffsetAbs.X,
		DragOp->DragOffsetAbs.Y,
		DragOp->DragOffset.X,
		DragOp->DragOffset.Y,
		DragOp->bFromEquip ? 1 : 0
	);

	OutOperation = DragOp;
}

void UEquipSlot::SetHighlightState(EBorderHighlightState InState)
{
	// Mirror SlotWidget behavior: tint BackGround image if present, else use SlotBorder
	FLinearColor TargetColor = DefaultBackColor;

	switch (InState)
	{
	case EBorderHighlightState::None:
		TargetColor = DefaultBackColor;
		break;
	case EBorderHighlightState::Valid:
		TargetColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.35f);
		break;
	case EBorderHighlightState::Invalid:
		TargetColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.35f);
		break;
	default:
		TargetColor = DefaultBackColor;
		break;
	}

	if (BackGround)
	{
		BackGround->SetBrushTintColor(FSlateColor(TargetColor));
		BackGround->SetVisibility(ESlateVisibility::Visible);
	}
	else if (SlotBorder)
	{
		// For border, use semi-transparent brush color for highlight, or transparent for None
		if (InState == EBorderHighlightState::None)
		{
			SlotBorder->SetBrushColor(FLinearColor::Transparent);
		}
		else
		{
			SlotBorder->SetBrushColor(TargetColor);
		}
		SlotBorder->SetVisibility(ESlateVisibility::Visible);
	}
}

bool UEquipSlot::RequestUnEquip()
{
	if (!EquipComp) return false;
	// 안전하게 현재 슬롯의 장착 해제 요청
	bool bResult = EquipComp->UnEquip(Slot);
	// 로컬 UI 즉시 정리
	Clear();
	return bResult;
}

FReply UEquipSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}
	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}
