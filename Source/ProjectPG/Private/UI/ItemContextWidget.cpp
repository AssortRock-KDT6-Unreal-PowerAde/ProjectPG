#include "UI/ItemContextWidget.h"

#include "Components/EquipComponent.h"
#include "Components/InventoryComponent.h"
#include "Components/Button.h"
#include "Components/Overlay.h"

#include "UI/InventoryWindow.h"
#include "Core/UIManagerSubSystem.h"
#include "GameMode/CustomPlayerState.h"
#include "UI/InventoryGridWidget.h"
#include "GameFrameWork/Actor.h"
#include "UI/BagPopupWindow.h"
#include "Core/TableSubSystem.h"

void UItemContextWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (MainOverlay)
	{
		MainOverlay->SetVisibility(
			ESlateVisibility::SelfHitTestInvisible
		);
	}
	if (EquipButton) {
		EquipButton->OnClicked.RemoveDynamic(this, &UItemContextWidget::OnEquipClickedBtn);
		EquipButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnEquipClickedBtn);
	}
	if (UnEquipButton) {
		UnEquipButton->OnClicked.RemoveDynamic(this, &UItemContextWidget::OnUnEquipClickedBtn);
		UnEquipButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnUnEquipClickedBtn);
	}
	if (UseButton) {
		UseButton->OnClicked.RemoveDynamic(this, &UItemContextWidget::OnUsedClickedBtn);
		UseButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnUsedClickedBtn);
	}
	if (DropButton) {
		DropButton->OnClicked.RemoveDynamic(this, &UItemContextWidget::OnDropClicked);
		DropButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnDropClicked);
	}
	if (CancleButton) {
		CancleButton->OnClicked.RemoveDynamic(this, &UItemContextWidget::OnCancledClicked);

		CancleButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnCancledClicked);
	}
	if (OpenButton) {
		OpenButton->OnClicked.RemoveDynamic(this, &UItemContextWidget::OnOpenClickBtn);

		OpenButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnOpenClickBtn);
	}
}

void UItemContextWidget::InitWidget(UInventoryComponent* InInventory, UEquipComponent* InEquip)
{
	InvenComp = InInventory;
	EquipComp = InEquip;
}

void UItemContextWidget::SetItem(const FItemInstance& InItem)
{
	CurrentItem = InItem;
}

void UItemContextWidget::InitButtonState()
{
	EquipButton->SetVisibility(ESlateVisibility::Visible);
	UnEquipButton->SetVisibility(ESlateVisibility::Visible);
	UseButton->SetVisibility(ESlateVisibility::Visible);
	DropButton->SetVisibility(ESlateVisibility::Visible);
	CancleButton->SetVisibility(ESlateVisibility::Visible);
	OpenButton->SetVisibility(ESlateVisibility::Visible);

}

void UItemContextWidget::UpdateButtonState(EItemType type)
{
	InitButtonState();
	switch (type)
	{
	case EItemType::Weapon:
		OpenButton->SetVisibility(ESlateVisibility::Collapsed);
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Armor:
		OpenButton->SetVisibility(ESlateVisibility::Collapsed);
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Consumable:
		OpenButton->SetVisibility(ESlateVisibility::Collapsed);
		EquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UnEquipButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Quest:
		OpenButton->SetVisibility(ESlateVisibility::Collapsed);
		EquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UnEquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Bag:
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::ETC:
		OpenButton->SetVisibility(ESlateVisibility::Collapsed);
		EquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UnEquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	default:
		break;
	}
}

void UItemContextWidget::OnEquipClickedBtn()
{
	UE_LOG(LogTemp, Warning, TEXT("장착버튼 누름"));

	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		ACustomPlayerState* PS = Cast<ACustomPlayerState>(PC->PlayerState);
		if (IsValid(PS))
		{
			EquipComp = PS->GetComponentByClass<UEquipComponent>();
			InvenComp = PS->GetComponentByClass<UInventoryComponent>();

			if (EquipComp && InvenComp)
			{
				if (EquipComp->Equip(CurrentItem))
				{
					// TODO: 장착 성공 시 기존 인벤토리 그리드 배열에서 해당 아이템 제거 처리 필요
					// InvenComp->RemoveItemByGuid(CurrentItem.parent_inventory_guid, CurrentItem.GUID);
				}
			}
		}

		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true;

		FSlateApplication::Get().ClearKeyboardFocus();
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UItemContextWidget::OnUnEquipClickedBtn()
{
	if (EquipComp)
	{
		EquipComp->UnEquip(CurrentItem);
	}

	UUIManagerSubSystem* UIMgr = UUIManagerSubSystem::Get(GetWorld());
	if (UIMgr)
	{
		//            UIMgr->UpdatePreview(); // ★ 여기서 호출!
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UItemContextWidget::OnUsedClickedBtn()
{
	if (InvenComp)
	{
		//        InvenComp->UseItem(CurrentItem.GUID);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UItemContextWidget::OnDropClicked()
{
	// Null Check 추가 (크래시 방지)
	if (EquipComp && EquipComp->IsEquipped(CurrentItem.GUID)) return;

	if (InvenComp)
	{
		// InvenComp->DropItem(CurrentItem);
	}

	SetVisibility(ESlateVisibility::Collapsed);
}

void UItemContextWidget::OnCancledClicked()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UItemContextWidget::OnOpenClickBtn()
{
	FItemInstance LocalItem = CurrentItem;
	if (LocalItem.ItemID.IsNone()) return;

	UUIManagerSubSystem* UIMgr = UUIManagerSubSystem::Get(GetWorld());
	if (!IsValid(UIMgr)) return;

	if (LocalItem.type == EItemType::Bag)
	{
		if (LocalItem.GUID.IsValid())
		{
			UUserWidget* Popup = UIMgr->OpenDynamicUI(EUIType::BackPackPopup, LocalItem.GUID);
			if (!IsValid(Popup)) return;

			APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
			if (!PC) return;


			UBagPopupWindow* Backpopup = Cast<UBagPopupWindow>(Popup);
			if (IsValid(Backpopup))
			{

				// 1순위: LocalItem.Owner가 유효한지 안전 검사
				AActor* TargetOwner = LocalItem.Owner.Get();

				// 만약 아이템 데이터 세팅 과정에서 Owner가 누락되었다면 플레이어 컨트롤러의 폰(Pawn)을 차선책으로 사용
				if (!IsValid(TargetOwner) && PC)
				{
					TargetOwner = PC->GetPawn();
				}

				if (!IsValid(TargetOwner))
				{
					UE_LOG(LogTemp, Error, TEXT("OnOpenClickBtn: 가방을 연 오너 액터를 찾을 수 없습니다! (Item.Owner 및 PlayerPawn 모두 유효하지 않음)"));
					Backpopup->RemoveFromParent();
					SetVisibility(ESlateVisibility::Collapsed);
					return;
				}

				// 안전하게 인벤토리 컴포넌트 추출
				UInventoryComponent* Comp = TargetOwner->GetComponentByClass<UInventoryComponent>();

				// 컴포넌트가 없다면 PlayerState 쪽도 한 번 더 탐색 (커스텀 플레이어 스테이트 구조 대응)
				if (!IsValid(Comp))
				{
					if (ACustomPlayerState* PS = Cast<ACustomPlayerState>(PC->PlayerState))
					{
						Comp = PS->GetComponentByClass<UInventoryComponent>();
					}
				}

				if (!IsValid(Comp))
				{
					UE_LOG(LogTemp, Error, TEXT("OnOpenClickBtn: Owner 및 PlayerState에 UInventoryComponent가 존재하지 않습니다!"));
					Backpopup->RemoveFromParent();
					SetVisibility(ESlateVisibility::Collapsed);
					return;
				}

				Backpopup->Init(Comp, LocalItem);
			}
		}
	}

	SetVisibility(ESlateVisibility::Collapsed);
}
