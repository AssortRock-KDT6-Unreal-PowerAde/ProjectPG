// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/EquipSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Common/TableData.h"
#include "Core/ItemSubSystem.h"

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

	UItemSubSystem* ItemSubsystem =
		UItemSubSystem::Get(GetWorld());

	if (!ItemSubsystem)
	{
		Clear();
		return;
	}

	const FItemTableRow* Data =
		ItemSubsystem->GetItem(Item->ItemID);

	if (!Data)
	{
		Clear();
		return;
	}

	if (Icon)
	{
		Icon->SetBrushFromTexture(Data->Icon, true);
		Icon->SetVisibility(ESlateVisibility::Visible);
		ItemName->SetText(FText::FromString(Data->DisPlayName.ToString()));
	}
}

void UEquipSlot::Clear()
{
	Item = nullptr;

	if (Icon)
	{
		Icon->SetBrushFromTexture(nullptr);
		Icon->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UEquipSlot::InitWidget(UEquipComponent* InEquip, UInventoryComponent* InInvetory)
{
	EquipComp = InEquip;
	InvenComp = InInvetory;
}
