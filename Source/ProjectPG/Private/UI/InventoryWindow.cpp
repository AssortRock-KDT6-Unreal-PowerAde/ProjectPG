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
#include <GameMode/CustomPlayerState.h>
#include <functional>
// 이름에 파일 이름을 붙임(10/7): EquipSlot·InventoryGridWidget 에도 같은 이름 함수가 있어, 언리얼이 .cpp 를 묶어 컴파일할 때(유니티 빌드) "이미 정의됨" 오류가 났다.
static void SafeRemoveWidget_InventoryWindow(UWidget* Widget)
{
	if (Widget && (Widget->GetParent() || Widget->IsInViewport()))
	{
		Widget->RemoveFromParent();
	}
}



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
	// Mark that main overlay should use the interact target explicitly
	bBoundToInteractTarget = true;

	// Apply centralized init to avoid duplicate delegate bindings
	ApplyInit(InvenComp.Get(), EquipComp.Get(), MainInventoryComp.Get(), true);

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

void UInventoryWindow::InitWidget(UInventoryComponent* InvenComponent, UEquipComponent* EquipComponent, UInventoryComponent* InMainInventory /*= nullptr*/)
{
	// Use centralized initializer to avoid duplicated binding/setup logic.
	ApplyInit(InvenComponent, EquipComponent, InMainInventory, false);

}

void UInventoryWindow::InitForPlayer(UInventoryComponent* PlayerInv, UEquipComponent* PlayerEquip, bool bShowMain /*= true*/)
{
	UE_LOG(LogTemp, Log, TEXT("InventoryWindow: InitForPlayer called. ShowMain=%d"), bShowMain ? 1 : 0);
	SetShowMainInventory(bShowMain);
	ApplyInit(PlayerInv, PlayerEquip, bShowMain ? PlayerInv : nullptr, false);
}

void UInventoryWindow::InitForContainer(UInventoryComponent* ContainerInv)
{
	UE_LOG(LogTemp, Log, TEXT("InventoryWindow: InitForContainer called."));
	// When initializing for a container, we must keep the player's own InvenComp/EquipComp
	// so stash/backpack/equipment areas still reflect the player. Only set MainInventoryComp
	// to the provided container inventory.
	SetShowMainInventory(true);
	// If InvenComp is not yet set (widget not initialized for player), try to obtain from owning player
	if (!InvenComp)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (ACustomPlayerState* PS = PC->GetPlayerState<ACustomPlayerState>())
			{
				InvenComp = PS->GetComponentByClass<UInventoryComponent>();
				EquipComp = PS->GetComponentByClass<UEquipComponent>();
				UE_LOG(LogTemp, Log, TEXT("InventoryWindow: Found player InvenComp=%s EquipComp=%s"), InvenComp ? TEXT("ok") : TEXT("null"), EquipComp ? TEXT("ok") : TEXT("null"));
			}
		}
	}

	// Choose MainInventoryGUID before ApplyInit because ApplyInit triggers RefreshAllGrids.
	// Keep a preselected valid GUID (e.g., from PreferredGuid path) if it exists on this container.
	if (!ContainerInv)
	{
		MainInventoryGUID.Invalidate();
	}
	else if (!MainInventoryGUID.IsValid() || !ContainerInv->GetItemsMap().Contains(MainInventoryGUID))
	{
		MainInventoryGUID.Invalidate();
		for (const auto& Pair : ContainerInv->GetItemsMap())
		{
			MainInventoryGUID = Pair.Key;
			break;
		}
	}

	// Ensure we set the main inventory to the container, but do not clobber player components
	ApplyInit(InvenComp.Get(), EquipComp.Get(), ContainerInv, true);

	// Do not initialize equipment widget for container
	// Ensure main inventory widget exists for container display
	if (UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(GetWorld()))
	{
		if (TSubclassOf<UUserWidget> InvenClass = UISub->GetUIClass(EUIType::Inventory))
		{
			// If main overlay has no child, create it
			if (MainInventoryOverlay && MainInventoryOverlay->GetChildrenCount() == 0)
			{
				SetupMainInventoryWidget(InvenClass);
			}
		}
	}

	// In some cases container registration may occur slightly after interaction. Schedule deferred refresh.
	RefreshAllGrids();
	GetWorld()->GetTimerManager().SetTimer(DeferredRefreshTimer, [this]() {
		RefreshAllGrids();
	}, 0.1f, false);
}

void UInventoryWindow::InitForContainer(UInventoryComponent* ContainerInv, const FGuid& PreferredGuid)
{
	UE_LOG(LogTemp, Log, TEXT("InventoryWindow: InitForContainer (with PreferredGuid) called."));
	if (ContainerInv && PreferredGuid.IsValid() && ContainerInv->GetItemsMap().Contains(PreferredGuid))
	{
		MainInventoryGUID = PreferredGuid;
	}
	else
	{
		MainInventoryGUID.Invalidate();
	}

	// Reuse existing initialization path to preserve player component wiring and widget setup.
	InitForContainer(ContainerInv);

	if (!ContainerInv)
	{
		return;
	}

	// If caller provided a valid, existing GUID, force main inventory to that container.
	if (PreferredGuid.IsValid() && ContainerInv->GetItemsMap().Contains(PreferredGuid))
	{
		MainInventoryGUID = PreferredGuid;
		RefreshAllGrids();
	}
}

void UInventoryWindow::InitForHuman(UInventoryComponent* HumanInv, UEquipComponent* HumanEquip)
{
	UE_LOG(LogTemp, Log, TEXT("InventoryWindow: InitForHuman called."));

	// Human: show both equipment and main inventory
	SetShowMainInventory(true);

	// Keep player-owned inventory for pocket/backpack while binding human inventory as main.
	UInventoryComponent* PlayerInv = InvenComp.Get();
	if (!PlayerInv)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			if (ACustomPlayerState* PS = PC->GetPlayerState<ACustomPlayerState>())
			{
				PlayerInv = PS->GetComponentByClass<UInventoryComponent>();
			}
		}
	}

	// Centralized initialization for delegate binding and main inventory wiring.
	ApplyInit(PlayerInv, HumanEquip, HumanInv, true);

	// Ensure equipment widget is initialized for the human target.
	if (EquipmentWidget && HumanEquip && HumanInv)
	{
		EquipmentWidget->InitWidget(HumanEquip, HumanInv);
		SetChildEquipOverlay(EquipmentWidget);
	}

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
		SafeRemoveWidget_InventoryWindow(ChildWidget);
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
		SafeRemoveWidget_InventoryWindow(childWidget);
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
		SafeRemoveWidget_InventoryWindow(childWidget);
		EquipOverlay->ClearChildren();
		EquipOverlay->AddChild(childWidget);
	}
}

void UInventoryWindow::SetChildBackpackInvenOverlay(UUserWidget* childWidget)
{
	if (BackPackInvenOverlay && childWidget)
	{
		SafeRemoveWidget_InventoryWindow(childWidget);
		BackPackInvenOverlay->ClearChildren();
		BackPackInvenOverlay->AddChild(childWidget);
	}
}

void UInventoryWindow::SetChildMainCanvas(UUserWidget* childWidget)
{
	if (MainCanvas && childWidget)
	{
		SafeRemoveWidget_InventoryWindow(childWidget);
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

	// Reset any interaction binding and clear main overlay to avoid stale grids when reopened
	bBoundToInteractTarget = false;
	MainInventoryComp = nullptr;
	if (MainInventoryOverlay)
	{
		MainInventoryOverlay->ClearChildren();
	}
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
	if (InvenComp && InvenComp->IsServerManaged()) return;

	if (InvenComp)
	{
		InvenComp->SetServerInventoryData(InventoryMapWrapper);
	}

	// Stash / Pocket / Backpack 전체 UI 리프레시 호출
	RefreshAllGrids();
}
void UInventoryWindow::ApplyInit(UInventoryComponent* PlayerInv, UEquipComponent* PlayerEquip, UInventoryComponent* MainInv, bool bBindToInteractTargetFlag)
{
	// Assign stored pointers
	InvenComp = PlayerInv;
	EquipComp = PlayerEquip;
	MainInventoryComp = MainInv;
	bBoundToInteractTarget = bBindToInteractTargetFlag;

	// Ensure Equipment widget is initialized with current player comps
	if (EquipmentWidget)
	{
		EquipmentWidget->InitWidget(EquipComp, InvenComp);
	}

	// Centralized delegate binding: remove then add once per relevant component
	if (InvenComp)
	{
		InvenComp->OnInventoryUpdated.RemoveDynamic(this, &UInventoryWindow::RefreshAllGrids);
		InvenComp->OnInventoryUpdated.AddDynamic(this, &UInventoryWindow::RefreshAllGrids);
	}

	if (MainInventoryComp)
	{
		MainInventoryComp->OnInventoryUpdated.RemoveDynamic(this, &UInventoryWindow::RefreshAllGrids);
		MainInventoryComp->OnInventoryUpdated.AddDynamic(this, &UInventoryWindow::RefreshAllGrids);
	}

	// Ensure main inventory widget exists when bound to an interact target
	if (bBoundToInteractTarget && MainInventoryComp)
	{
		if (UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(GetWorld()))
		{
			if (TSubclassOf<UUserWidget> MainClass = UISub->GetUIClass(EUIType::Inventory))
			{
				if (MainInventoryOverlay && MainInventoryOverlay->GetChildrenCount() == 0)
				{
					SetupMainInventoryWidget(MainClass);
				}
			}
		}
	}

	// Immediate refresh to reflect current bindings
	// If MainInventoryGUID is set (from InitForContainer), ensure it's used for the initial refresh.
	// Keep MainInventoryGUID until the window is closed to avoid race conditions where
	// multiple RefreshAllGrids calls fall back to stash prematurely.
	RefreshAllGrids();
}
void UInventoryWindow::RefreshAllGrids()
{
	if (!InvenComp) return;
	UE_LOG(LogTemp, Warning, TEXT("[InventoryWindow] RefreshAllGrids begin bBoundToInteractTarget=%d MainInv=%p Equip=%p MainGuid=%s Pocket=%s"), bBoundToInteractTarget ? 1 : 0, MainInventoryComp.Get(), EquipComp.Get(), *MainInventoryGUID.ToString(), *InvenComp->GetPocketInventoryID().ToString());

	// recursive finder to locate an InventoryGridWidget anywhere under a widget
	std::function<UInventoryGridWidget*(UWidget*)> FindInventoryGrid = [&](UWidget* Root) -> UInventoryGridWidget*
	{
		if (!Root) return nullptr;
		if (UInventoryGridWidget* Grid = Cast<UInventoryGridWidget>(Root)) return Grid;
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
		{
			for (UWidget* Child : Panel->GetAllChildren())
			{
				if (UInventoryGridWidget* Found = FindInventoryGrid(Child)) return Found;
			}
		}
		return nullptr;
	};

	auto BindOverlayGrid = [this, &FindInventoryGrid](UOverlay* TargetOverlay, UInventoryComponent* GridOwner, const FGuid& TargetGUID) -> bool
	{
		if (!TargetOverlay || !TargetGUID.IsValid() || !GridOwner) return false;

		UE_LOG(LogTemp, Warning, TEXT("BindOverlayGrid called. Overlay=%s GridOwner=%s GUID=%s ChildCount=%d"),
			TargetOverlay ? *TargetOverlay->GetName() : TEXT("null"),
			GridOwner ? *FString::Printf(TEXT("%p"), GridOwner) : TEXT("null"),
			*TargetGUID.ToString(),
			TargetOverlay->GetAllChildren().Num());

		for (UWidget* Child : TargetOverlay->GetAllChildren())
		{
			UE_LOG(LogTemp, Warning, TEXT("  Child widget: %s"), Child ? *Child->GetName() : TEXT("null"));
			if (UInventoryGridWidget* GridWidget = FindInventoryGrid(Child))
			{
				UE_LOG(LogTemp, Warning, TEXT("  Found InventoryGridWidget child %s - calling RefreshGrid and forcing visibility/refresh"), *GridWidget->GetName());
				// Ensure widget is visible and attempt immediate refresh. Some widgets are created lazily
				// so force visibility and call RefreshGridUI after binding.
				GridWidget->SetVisibility(ESlateVisibility::Visible);
				GridWidget->RefreshGrid(GridOwner, TargetGUID);
				GridWidget->RefreshGridUI();
				return true;
			}
		}
		return false;
	};

	// 1. 기본 Stash 및 Pocket 인벤토리 UI 갱신
	// MainInventoryOverlay may show either player's stash or MainInventoryComp (interact actor)
	// Main area display controlled by bShowMainInventory
	if (bShowMainInventory)
	{
		// Determine target GUID for main overlay based on whether main is bound to an interact target
		UInventoryComponent* GridOwner = nullptr;
		if (bBoundToInteractTarget && MainInventoryComp)
		{
			GridOwner = MainInventoryComp.Get();
		}
		else
		{
			GridOwner = InvenComp.Get();
		}
		FGuid TargetGuid;
		if (GridOwner)
		{
			// Decide GUID to display: prefer explicit MainInventoryGUID when set, otherwise stash, then any container
			if (MainInventoryGUID.IsValid() && GridOwner->GetItemsMap().Contains(MainInventoryGUID))
			{
				TargetGuid = MainInventoryGUID;
			}
			else
			{
				TargetGuid = GridOwner->GetStashInventoryID();
				if (!TargetGuid.IsValid())
				{
					// Fallback: find any registered container
					for (const auto& Pair : GridOwner->GetItemsMap())
					{
						TargetGuid = Pair.Key;
						break;
					}
				}
			}

			UE_LOG(LogTemp, Warning, TEXT("RefreshAllGrids: bBoundToInteractTarget=%d MainInventoryComp=%s(%p) InvenComp=%s(%p) -> ChosenGridOwner=%s(%p) ChosenGUID=%s"),
				bBoundToInteractTarget ? 1 : 0,
				MainInventoryComp ? *FString::Printf(TEXT("MainInventoryComp")) : TEXT("null"),
				MainInventoryComp ? MainInventoryComp.Get() : nullptr,
				InvenComp ? *FString::Printf(TEXT("InvenComp")) : TEXT("null"),
				InvenComp ? InvenComp.Get() : nullptr,
				GridOwner ? *FString::Printf(TEXT("GridOwner")) : TEXT("null"),
				GridOwner ? GridOwner : nullptr,
				TargetGuid.IsValid() ? *TargetGuid.ToString() : TEXT("(invalid)"));

			if (TargetGuid.IsValid())
			{
				BindOverlayGrid(MainInventoryOverlay, GridOwner, TargetGuid);
			}
		}
	}
	BindOverlayGrid(SubInventoryOverlay, InvenComp, InvenComp->GetPocketInventoryID());

	// 2. 장착된 가방(Backpack) UI 갱신
	if (EquipComp)
	{
		const FItemInstance* BackpackItem = EquipComp->GetEquipment(EEquipSlot::BackPack);
		UE_LOG(LogTemp, Warning, TEXT("[InventoryWindow] Backpack check item=%s guid=%s"), BackpackItem ? *BackpackItem->ItemID.ToString() : TEXT("null"), BackpackItem ? *BackpackItem->GUID.ToString() : TEXT("(null)"));
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

			// backpack grid를 다시 붙일 때, 예전 GUID 컨테이너에 남아 있던 child item이
			// 재사용되는 것을 막기 위해 현재 장착된 backpack GUID 기준으로만 바인딩한다.
			// 즉, 새로 아이템을 생성하는 것이 아니라 현재 BackpackItem->GUID에 연결된
			// 기존 컨테이너 내용을 그대로 표시해야 한다.

			// 가방 UI 오버레이 바인딩 시도
			if (!BindOverlayGrid(BackPackInvenOverlay, InvenComp, BackpackItem->GUID))
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
			UE_LOG(LogTemp, Warning, TEXT("[InventoryWindow] Backpack not equipped -> clearing backpack overlay"));
			if (BackPackInvenOverlay)
			{
				BackPackInvenOverlay->ClearChildren();
			}
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("UInventoryWindow::RefreshAllGrids - MainOverlayChildCount=%d BackPackChildCount=%d SubChildCount=%d"),
		MainInventoryOverlay ? MainInventoryOverlay->GetAllChildren().Num() : -1,
		BackPackInvenOverlay ? BackPackInvenOverlay->GetAllChildren().Num() : -1,
		SubInventoryOverlay ? SubInventoryOverlay->GetAllChildren().Num() : -1);
}