// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/BagPopupWindow.h"
#include "UI/InventoryGridWidget.h"
#include "Core/UIManagerSubSystem.h"

#include "Components/OverlaySlot.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/InventoryComponent.h"
#include <Core/TableSubSystem.h>

void UBagPopupWindow::NativeConstruct()
{
	if (CancleButton) {
		CancleButton->OnClicked.RemoveDynamic(this, &UBagPopupWindow::OnClickedCancleButton);
		CancleButton->OnClicked.AddDynamic(this, &UBagPopupWindow::OnClickedCancleButton);
	}
}

void UBagPopupWindow::OnClickedCancleButton()
{
	UUIManagerSubSystem* UIMgr = UUIManagerSubSystem::Get(GetWorld());
	if (UIMgr)
	{		
		UIMgr->CloseDynamicUI(MyGuid); // 혹은 RemoveFromParent()
		RemoveFromParent();
	}
}

void UBagPopupWindow::Init(UInventoryComponent* InvenComp, FItemInstance Item)
{
	if (InvenComp == nullptr) return;
	if (InventoryParent == nullptr) return;
	if (Item.type != EItemType::Bag) return;


	UUIManagerSubSystem* UIMgr = UUIManagerSubSystem::Get(GetWorld());
	if (!IsValid(UIMgr)) return;

	// 항상 새로운 인벤토리 그리드 인스턴스를 생성하여 상태가 공유되지 않도록 함
	UUserWidget* widget = nullptr;
	TSubclassOf<UUserWidget> WidgetClass = UIMgr->GetUIClass(EUIType::Inventory);
	if (WidgetClass)
	{
		APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
		if (PC)
		{
			widget = CreateWidget<UUserWidget>(PC, WidgetClass);
		}
	}

	MyGuid = Item.GUID;
	if (IsValid(widget))
	{
		GridWidget = Cast<UInventoryGridWidget>(widget);
		if (IsValid(GridWidget))
		{
			// 이제 이 그리드는 메인 인벤토리와 완전히 분리된 가방 전용 위젯이므로 안전하게 부착 가능합니다.
			GridWidget->RemoveFromParent();
			InventoryParent->ClearChildren();


			UTableSubSystem* subSystem = UTableSubSystem::Get(GetWorld());
			if (!subSystem) return;
			const FItemBackpackTable* ItemData = subSystem->FindTableRow<FItemBackpackTable>("BackpackTable", Item.ItemID);
			if (ItemData && ItemData->SlotSize.X > 0 && ItemData->SlotSize.Y > 0)
			{
				// 테이블 정보로 가방 컨테이너 크기 즉시 등록
				InvenComp->RegisterContainer(Item.GUID, FIntPoint(ItemData->SlotSize.X, ItemData->SlotSize.Y));
			}

			if (UOverlaySlot* OverlaySlot = InventoryParent->AddChildToOverlay(GridWidget))
			{
				OverlaySlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				OverlaySlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
			GridWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			// 초기화: Grid에 인벤토리 컴포넌트와 GUID 바인딩 후 즉시 UI 갱신 호출
			GridWidget->RefreshGrid(InvenComp, Item.GUID);
			GridWidget->RefreshGridUI();
		}
	}
}
