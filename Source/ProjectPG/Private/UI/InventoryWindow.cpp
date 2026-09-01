#include "UI/InventoryWindow.h"
#include "UI/EquipmentWidget.h"
#include "UI/InventoryGridWidget.h"

#include "Components/CanvasPanel.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"

#include "Core/UIManagerSubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include <Core/TableSubSystem.h>


void UInventoryWindow::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
		WebSocketSub->OnInventoryReceived.AddDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
	}
	if (BackBtn)
	{
		BackBtn->OnClicked.RemoveDynamic(this, &UInventoryWindow::OnClickedBackBtn);
		BackBtn->OnClicked.AddDynamic(this, &UInventoryWindow::OnClickedBackBtn);

	}
}

void UInventoryWindow::NativeDestruct()
{
	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
	}

	if (BackBtn)
	{
		BackBtn->OnClicked.RemoveDynamic(this, &UInventoryWindow::OnClickedBackBtn);
	}

	Super::NativeDestruct();
}

void UInventoryWindow::InitWidget(UInventoryComponent* InvenComponent, UEquipComponent* EquipComponent)
{
	InvenComp = InvenComponent;
	EquipComp = EquipComponent;

	if (EquipmentWidget) {
		EquipmentWidget->InitWidget(EquipComp, InvenComp);
	}
	// ★ 아이템 배치/이동 시 가방 포함 모든 그리드 UI 즉시 갱신
	if (InvenComp)
	{
		InvenComp->OnInventoryUpdated.RemoveDynamic(this, &UInventoryWindow::RefreshAllGrids);
		InvenComp->OnInventoryUpdated.AddDynamic(this, &UInventoryWindow::RefreshAllGrids);

	}

}

// 메인 인벤토리(Stash) 위젯 동적 생성 및 배치
void UInventoryWindow::SetupMainInventoryWidget(TSubclassOf<UUserWidget> InvenClass)
{
	if (!InvenClass) return;

	UUserWidget* MainInvenWidget = CreateWidget<UUserWidget>(this, InvenClass);
	if (MainInvenWidget)
	{
		SetChildMainInvenOverlay(MainInvenWidget);
	}
}

// 포켓(Sub) 인벤토리 위젯 동적 생성 및 배치
void UInventoryWindow::SetupPocketInventoryWidget(TSubclassOf<UUserWidget> InvenClass)
{
	if (!InvenClass) return;

	UUserWidget* PocketInvenWidget = CreateWidget<UUserWidget>(this, InvenClass);
	if (PocketInvenWidget)
	{
		SetChildSubInvenOverlay(PocketInvenWidget);
	}
}
void UInventoryWindow::SetupBackPackInventoryWidget(TSubclassOf<UUserWidget> InvenClass)
{
	if (!InvenClass || !EquipComp || !InvenComp)
	{
		if (BackPackInvenOverlay) BackPackInvenOverlay->ClearChildren();
		return;
	}

	const FItemInstance* Instance = EquipComp->GetEquipment(EEquipSlot::BackPack);
	if (Instance && Instance->GUID.IsValid())
	{
		// ❌ 매번 ClearChildren / CreateWidget을 수행하면 델리게이트 재귀 시 프리징 발생
		// 기존 오버레이에 이미 자식이 있다면 새로 생성하지 않도록 확실히 처리
		if (BackPackInvenOverlay && BackPackInvenOverlay->GetChildrenCount() > 0)
		{
			if (UInventoryGridWidget* GridWidget = Cast<UInventoryGridWidget>(BackPackInvenOverlay->GetChildAt(0)))
			{
				GridWidget->RefreshGrid(InvenComp, Instance->GUID);
				return;
			}
		}

		UUserWidget* BackPackWidget = CreateWidget<UUserWidget>(this, InvenClass);
		if (BackPackWidget)
		{
			SetChildBackpackInvenOverlay(BackPackWidget);
			if (UInventoryGridWidget* GridWidget = Cast<UInventoryGridWidget>(BackPackWidget))
			{
				GridWidget->RefreshGrid(InvenComp, Instance->GUID);
			}
		}
	}
	else
	{
		if (BackPackInvenOverlay)
		{
			BackPackInvenOverlay->ClearChildren();
		}
	}
	
}

void UInventoryWindow::SetChildMainInvenOverlay(UUserWidget* ChildWidget)
{
	if (MainInventoryOverlay && ChildWidget)
	{
		ChildWidget->RemoveFromParent();
		MainInventoryOverlay->ClearChildren();

		if (UOverlaySlot* OverlaySlot = MainInventoryOverlay->AddChildToOverlay(ChildWidget))
		{
			OverlaySlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			OverlaySlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		ChildWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UInventoryWindow::SetChildSubInvenOverlay(UUserWidget* childWidget)
{
	if (SubInventoryOverlay && childWidget)
	{
		childWidget->RemoveFromParent();
		SubInventoryOverlay->ClearChildren();

		if (UOverlaySlot* OverlaySlot = SubInventoryOverlay->AddChildToOverlay(childWidget))
		{
			OverlaySlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			OverlaySlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
		childWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UInventoryWindow::SetChildEquipOverlay(UUserWidget* childWidget)
{
	if (EquipOverlay && childWidget)
	{
		childWidget->RemoveFromParent();
		EquipOverlay->ClearChildren();
		EquipOverlay->AddChild(childWidget);
	}
}

void UInventoryWindow::SetChildBackpackInvenOverlay(UUserWidget* childWidget)
{
	if (BackPackInvenOverlay && childWidget)
	{
		childWidget->RemoveFromParent();
		BackPackInvenOverlay->ClearChildren();
		BackPackInvenOverlay->AddChild(childWidget);
	}
}

void UInventoryWindow::SetChildMainCanvas(UUserWidget* childWidget)
{
	if (MainCanvas && childWidget)
	{
		childWidget->RemoveFromParent();
		MainCanvas->ClearChildren();
		MainCanvas->AddChild(childWidget);
	}
}

void UInventoryWindow::OnClickedBackBtn()
{
	UUIManagerSubSystem* subSystem = UUIManagerSubSystem::Get(GetWorld());
	if (!IsValid(subSystem)) return;

	subSystem->CloseUI(EUIType::Character);
}

void UInventoryWindow::UpdateState()
{
	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->RequestGetInventory();
	}
}

void UInventoryWindow::OnInventoryDataReceived(const FInventoryMapWrapper InventoryMapWrapper)
{
	if (InvenComp)
	{
		InvenComp->SetServerInventoryData(InventoryMapWrapper);
	}

	// Stash / Pocket / Backpack 전체 UI 리프레시 호출
	RefreshAllGrids();
}
void UInventoryWindow::RefreshAllGrids()
{
	if (!InvenComp) return;

	auto BindOverlayGrid = [this](UOverlay* TargetOverlay, const FGuid& TargetGUID) -> bool
		{
			if (!TargetOverlay || !TargetGUID.IsValid()) return false;

			for (UWidget* Child : TargetOverlay->GetAllChildren())
			{
				if (UInventoryGridWidget* GridWidget = Cast<UInventoryGridWidget>(Child))
				{
					GridWidget->RefreshGrid(InvenComp, TargetGUID);
					return true;
				}
			}
			return false;
		};

	// 1. 기본 Stash 및 Pocket 인벤토리 UI 갱신
	BindOverlayGrid(MainInventoryOverlay, InvenComp->GetStashInventoryID());
	BindOverlayGrid(SubInventoryOverlay, InvenComp->GetPocketInventoryID());

	// 2. 장착된 가방(Backpack) UI 갱신
	if (EquipComp)
	{
		const FItemInstance* BackpackItem = EquipComp->GetEquipment(EEquipSlot::BackPack);
		if (BackpackItem && BackpackItem->GUID.IsValid())
		{
			// 💡 [수정] 컴포넌트에 이미 크기 정보가 등록되어 있다면 RegisterContainer를 건너뜁니다.
			FIntPoint RegisteredSize = InvenComp->GetInventorySizeByGuid(BackpackItem->GUID);

			if (RegisteredSize.X <= 0 || RegisteredSize.Y <= 0)
			{
				// 등록된 적이 없을 때만 가방 크기 확인 후 컨테이너 최초 등록
				if (UTableSubSystem* TableSub = UTableSubSystem::Get(GetWorld()))
				{
					const FItemBackpackTable* Data = TableSub->FindTableRow<FItemBackpackTable>("BackpackTable", BackpackItem->ItemID);
					if (Data && Data->SlotSize.X > 0 && Data->SlotSize.Y > 0)
					{
						InvenComp->RegisterContainer(BackpackItem->GUID, FIntPoint(Data->SlotSize.X, Data->SlotSize.Y));
					}
				}
			}

			// 가방 UI 오버레이 바인딩 시도
			if (!BindOverlayGrid(BackPackInvenOverlay, BackpackItem->GUID))
			{
				UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(GetWorld());
				if (UISub)
				{
					SetupBackPackInventoryWidget(UISub->GetUIClass(EUIType::Inventory));
				}
			}
		}
		else
		{
			if (BackPackInvenOverlay)
			{
				BackPackInvenOverlay->ClearChildren();
			}
		}
	}
	if (EquipmentWidget) {
		EquipmentWidget->HandleBackpackContainerUpdate();
	}
}