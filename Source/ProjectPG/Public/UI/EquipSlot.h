// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "EquipSlot.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UEquipSlot : public UUserWidget
{
	GENERATED_BODY()

protected:
	TObjectPtr<class UImage> Icon;
	TObjectPtr<class UTextBlock> ItemName;
private:
	EEquipSlot Slot;
	const FItemInstance* Item;

	UPROPERTY()
	TObjectPtr<class UEquipComponent> EquipComp;

	UPROPERTY()
	TObjectPtr<class UInventoryComponent> InvenComp;

public:
	virtual void NativeConstruct() override;

	void SetSlot(EEquipSlot InSlot);
	void SetItem(const FItemInstance* InItem);
	void Clear();
	void InitWidget(class UEquipComponent* InEquip, class UInventoryComponent* InInvetory);


	
};
