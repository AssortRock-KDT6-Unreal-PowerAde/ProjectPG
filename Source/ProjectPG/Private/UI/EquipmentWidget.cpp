// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/EquipmentWidget.h"
#include "UI/EquipSlot.h"
#include "Components/Image.h"

void UEquipmentWidget::NativeConstruct()
{
	Super::NativeConstruct();
	WeaponSlot->SetSlot(EEquipSlot::MainWeapon);
	ConsumalSlot->SetSlot(EEquipSlot::Accuracy1);
	HealPackSlot->SetSlot(EEquipSlot::Accuracy2);
	HelmetSlot->SetSlot(EEquipSlot::HelMet);
	ClothSlot->SetSlot(EEquipSlot::Cloth);
	PantsSlot->SetSlot(EEquipSlot::Pants);
	ShoesSlot->SetSlot(EEquipSlot::Shose);
	SubWeaponSlot->SetSlot(EEquipSlot::SubWeapon);
	BackPackSlot->SetSlot(EEquipSlot::BackPack);
}
