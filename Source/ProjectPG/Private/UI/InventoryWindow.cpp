// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/InventoryWindow.h"
#include "UI/InventoryGridWidget.h"
#include "Components/Overlay.h"
#include "Components/EquipComponent.h"
#include "Components/InventoryComponent.h"
#include "Core/TableSubSystem.h"
#include "Core/ItemSubSystem.h"
#include "Core/UIManagerSubSystem.h"

void UInventoryWindow::InitWidget(UInventoryComponent* InvenComponent, UEquipComponent* EquipComponent)
{
	InvenComp = InvenComponent;
	EquipComp = EquipComponent;
}

void UInventoryWindow::SetChildEquipOverlay(UUserWidget* childWidget)
{
	if (EquipOverlay) EquipOverlay->AddChild(childWidget);
}

void UInventoryWindow::SetChildSubInvenOverlay(UUserWidget* childWidget)
{

	if (SubInventoryOverlay) SubInventoryOverlay->AddChild(childWidget);
}

void UInventoryWindow::SetChildMainInvenOverlay(UUserWidget* childWidget)
{
	if (MainInventoryOverlay) MainInventoryOverlay->AddChild(childWidget);
}

void UInventoryWindow::SetChildBackpackInvenOverlay(UUserWidget* childWidget)
{
	if (BackPackInvenOverlay) BackPackInvenOverlay->AddChild(childWidget);

}

void UInventoryWindow::UpdateState()
{
	if (false == IsValid(EquipComp)) return;
	if (false == IsValid(InvenComp)) return;
	const FItemInstance* item  = EquipComp->GetEquipment(EEquipSlot::BackPack);
	if (item)
	{
		if (item->bEquip)
		{
			UTableSubSystem* subSystem = UTableSubSystem::Get(GetWorld());
			if (nullptr == subSystem) return;
			UItemSubSystem* Itemsubsystem = UItemSubSystem::Get(GetWorld());

			if (nullptr == Itemsubsystem) return;

			UUIManagerSubSystem* UIsubsystem = UUIManagerSubSystem::Get(GetWorld());
			if (nullptr == UIsubsystem) return;
			
			const FItemBackpackTable* itemData = subSystem->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), *item->ItemID.ToString());
			UInventoryGridWidget* invenwidget = Cast<UInventoryGridWidget>(UIsubsystem->OpenUI(EUIType::Inventory));
			if (nullptr == invenwidget) return;
			
			SetChildBackpackInvenOverlay(invenwidget);				
			const FItemInstance* itemInstance  =InvenComp->GetItemInstance(*FString::FromInt(itemData->BackpackID));
			
		}

	}
}
