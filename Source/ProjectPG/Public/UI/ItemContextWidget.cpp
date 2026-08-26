#include "ItemContextWidget.h"

#include "Components/EquipComponent.h"
#include "Components/InventoryComponent.h"
#include "Components/Button.h"

#include "Core/UIManagerSubSystem.h"
#include "GameMode/CustomPlayerState.h"

void UItemContextWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (EquipButton)
		EquipButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnEquipClickedBtn);

	if (UnEquipButton)
		UnEquipButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnUnEquipClickedBtn);
	if (UseButton)
		UseButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnUsedClickedBtn);

	if (DropButton)
		DropButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnDropClicked);

	if (CancleButton)
		CancleButton->OnClicked.AddDynamic(this, &UItemContextWidget::OnCancledClicked);
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
}

void UItemContextWidget::UpdateButtonState(EItemType type)
{
	InitButtonState();
	switch (type)
	{
	case EItemType::Weapon:
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Armor:
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Consumable:
		EquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UnEquipButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::Quest:
		EquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UnEquipButton->SetVisibility(ESlateVisibility::Collapsed);
		UseButton->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EItemType::ETC:
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
