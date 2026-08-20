#include "UI/InventoryWindow.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/InventoryComponent.h"
#include "UI/InventoryGridWidget.h"
#include "Server/WebSocketSubSystem.h"

void UInventoryWindow::NativeConstruct()
{
	Super::NativeConstruct();

	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
		WebSocketSub->OnInventoryReceived.AddDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
	}
}

void UInventoryWindow::NativeDestruct()
{
	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
	}

	Super::NativeDestruct();
}

void UInventoryWindow::InitWidget(UInventoryComponent* InvenComponent, UEquipComponent* EquipComponent)
{
	InvenComp = InvenComponent;
	EquipComp = EquipComponent;
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

void UInventoryWindow::UpdateState()
{
	if (UWebSocketSubSystem* WebSocketSub = UWebSocketSubSystem::Get(GetWorld()))
	{
		WebSocketSub->RequestGetInventory();
	}
}

void UInventoryWindow::OnInventoryDataReceived(const FInventoryMapWrapper InventoryMapWrapper)
{
	// 1. InventoryComponent에 최신 서버 데이터 동기화
	if (InvenComp)
	{
		InvenComp->SetServerInventoryData(InventoryMapWrapper);
	}

	// 2. Overlay 내부 자식 GridWidget을 찾아 바인딩
	auto BindOverlayGrid = [this](UOverlay* TargetOverlay, const FGuid& TargetGUID)
		{
			if (!TargetOverlay || !TargetGUID.IsValid()) return;

			for (UWidget* Child : TargetOverlay->GetAllChildren())
			{
				if (UInventoryGridWidget* GridWidget = Cast<UInventoryGridWidget>(Child))
				{
					GridWidget->SetInventoryGUID(TargetGUID);
					GridWidget->BindInventoryComponent(InvenComp);
					break;
				}
			}
		};

	// 3. Stash(Main) 및 Pocket(Sub) 각각 바인딩
	BindOverlayGrid(MainInventoryOverlay, InventoryMapWrapper.StashGuid);
	BindOverlayGrid(SubInventoryOverlay, InventoryMapWrapper.PocketGuid);
}