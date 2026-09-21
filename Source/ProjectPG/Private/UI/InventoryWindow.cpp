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
#include <Core/TableSubSystem.h>
#include <Server/InventorySubSystem.h>


void UInventoryWindow::NativeConstruct()
{
	Super::NativeConstruct();

	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvenSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
		InvenSub->OnInventoryReceived.AddDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
	}

	if (BackBtn)
	{
		BackBtn->SetVisibility(ESlateVisibility::Visible);
		BackBtn->SetIsEnabled(true);
		BackBtn->OnClicked.RemoveDynamic(this, &UInventoryWindow::OnClickedBackBtn);
		BackBtn->OnClicked.AddDynamic(this, &UInventoryWindow::OnClickedBackBtn);
		UE_LOG(LogTemp, Log, TEXT("UInventoryWindow::NativeConstruct - BackBtn bound, visible, enabled."));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UInventoryWindow::NativeConstruct - BackBtn is null. Check widget binding."));
	}
}
void UInventoryWindow::InitWidgetForActor(UInventoryComponent* ActorInventory)
{
	// Actor inventory should be bound to main inventory overlay only
	// For actor (interact) binding, we treat this as 'Main' inventory display.
	// Store separately to avoid clobbering the player-owned InvenComp used for Stash/Backpack.
	// Introduce a local variable MainInvenComp via a new member if not present.
	MainInventoryComp = ActorInventory;

	if (InvenComp)
	{
		InvenComp->OnInventoryUpdated.RemoveDynamic(this, &UInventoryWindow::RefreshAllGrids);
		InvenComp->OnInventoryUpdated.AddDynamic(this, &UInventoryWindow::RefreshAllGrids);
	}

	// Create main inventory grid if class registered in subsystem
	if (UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(GetWorld()))
	{
		if (TSubclassOf<UUserWidget> MainClass = UISub->GetUIClass(EUIType::Inventory))
		{
			SetupMainInventoryWidget(MainClass);
		}
	}


	if (BackBtn)
	{
		BackBtn->OnClicked.RemoveDynamic(this, &UInventoryWindow::OnClickedBackBtn);
		BackBtn->OnClicked.AddDynamic(this, &UInventoryWindow::OnClickedBackBtn);
	}
}


void UInventoryWindow::NativeDestruct()
{
	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvenSub->OnInventoryReceived.RemoveDynamic(this, &UInventoryWindow::OnInventoryDataReceived);
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

	// 즉시 현재 상태로 모든 그리드(스태시/포켓/백팩)를 강제 갱신하여
	// 장비(백팩) 복구 시점에서 UI가 빠르게 반영되도록 합니다.
	RefreshAllGrids();

}

// 메인 인벤토리(Stash) 위젯 동적 생성 및 배치
void UInventoryWindow::SetupMainInventoryWidget(TSubclassOf<UUserWidget> InvenClass)
{
	if (!InvenClass) return;

	UUserWidget* MainInvenWidget = CreateWidget<UUserWidget>(this, InvenClass);
	if (MainInvenWidget)
	{
		SetChildMainInvenOverlay(MainInvenWidget);
		// 즉시 바인딩 및 초기 렌더링 보장
		// If a MainInventoryComp (interact actor) is bound, show that; otherwise show player's stash
		UInventoryComponent* GridOwner = MainInventoryComp ? MainInventoryComp.Get() : InvenComp.Get();
		if (GridOwner)
		{
			if (UInventoryGridWidget* GridWidget = Cast<UInventoryGridWidget>(MainInvenWidget))
			{
				// If showing actor's inventory, use its first available inventory container (assume stash)
				FGuid TargetGuid = GridOwner->GetStashInventoryID();
				if (!TargetGuid.IsValid() && MainInventoryComp)
				{
					// Fallback: find any registered container
					for (const auto& Pair : GridOwner->GetItemsMap())
					{
						TargetGuid = Pair.Key;
						break;
					}
				}
				GridWidget->RefreshGrid(GridOwner, TargetGuid);
			}
		}
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
		// 즉시 바인딩 및 초기 렌더링 보장
		if (InvenComp)
		{
			if (UInventoryGridWidget* GridWidget = Cast<UInventoryGridWidget>(PocketInvenWidget))
			{
				GridWidget->RefreshGrid(InvenComp, InvenComp->GetPocketInventoryID());
			}
		}
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
		UE_LOG(LogTemp, Warning, TEXT("UInventoryWindow::SetupBackPackInventoryWidget called for GUID=%s"), *Instance->GUID.ToString());
		// 매번 ClearChildren / CreateWidget을 수행하지 않도록 기존 동작 유지
		// 기존 오버레이에 이미 자식이 있다면 새로 생성하지 않도록 처리
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
	UE_LOG(LogTemp, Log, TEXT("UInventoryWindow::OnClickedBackBtn called."));

	UUIManagerSubSystem* subSystem = UUIManagerSubSystem::Get(GetWorld());

	if (!IsValid(subSystem))
	{
		UE_LOG(LogTemp, Warning, TEXT("UInventoryWindow::OnClickedBackBtn - UIManagerSubSystem is invalid."));
	}
	else
	{
		UUserWidget* Registered = subSystem->GetUI(EUIType::Character);
		UE_LOG(LogTemp, Log, TEXT("UInventoryWindow::OnClickedBackBtn - UIManagerSubSystem reports Character widget %s"), Registered ? TEXT("present") : TEXT("null"));
	}

	// 요청으로 UIManager에게 닫기 처리를 맡기고,
	// 안전을 위해 위젯 자신도 뷰포트에서 제거합니다.
	if (IsValid(subSystem)) subSystem->CloseUI(EUIType::Character);

	UE_LOG(LogTemp, Log, TEXT("UInventoryWindow::OnClickedBackBtn - calling RemoveFromParent/Collapse."));

	if (IsInViewport())
	{
		RemoveFromParent();
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UInventoryWindow::UpdateState()
{

	if (UInventorySubSystem* InvenSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvenSub->RequestGetInventory();
	}
}

void UInventoryWindow::OnInventoryDataReceived(const FInventoryMapWrapper& InventoryMapWrapper)
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
	// MainInventoryOverlay may show either player's stash or MainInventoryComp (interact actor)
	if (MainInventoryComp)
	{
		BindOverlayGrid(MainInventoryOverlay, MainInventoryComp->GetStashInventoryID());
	}
	else
	{
		BindOverlayGrid(MainInventoryOverlay, InvenComp->GetStashInventoryID());
	}
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