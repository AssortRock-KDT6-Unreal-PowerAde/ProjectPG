// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "EquipmentWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UEquipmentWidget : public UUserWidget
{
	GENERATED_BODY()


protected:

    UPROPERTY(meta = (BindWidget))
    TObjectPtr<class UImage> Icon;
    UPROPERTY(meta = (BindWidget))
    TObjectPtr<class UTextBlock> ItemName;

private:

	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> CharacterView;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> WeaponSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> ConsumalSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> HealPackSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> HelmetSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> ClothSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> PantsSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> ShoesSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> SubWeaponSlot;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UEquipSlot> BackPackSlot;


    EEquipSlot Slot;

    const FItemInstance* Item = nullptr;

    UPROPERTY()
    TObjectPtr<class UEquipComponent> EquipComponent;

    UPROPERTY()
    TObjectPtr<class UInventoryComponent> InventoryComponent;

public:
	virtual void NativeConstruct() override;

//protected:
//
//    virtual bool NativeOnDrop(
//        const FGeometry& Geometry,
//        const FDragDropEvent& DragDropEvent,
//        UDragDropOperation* Operation) override;
//
//    virtual FReply NativeOnMouseButtonDown(
//        const FGeometry& Geometry,
//        const FPointerEvent& MouseEvent) override;

};
