// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"

#include "ItemContextWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UItemContextWidget : public UUserWidget
{
	GENERATED_BODY()
	
protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> EquipButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> UnEquipButton;


	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> UseButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> DropButton;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> CancleButton;
private:
	UPROPERTY()
	TObjectPtr<class UInventoryComponent> InvenComp;

	UPROPERTY()
	TObjectPtr<class UEquipComponent> EquipComp;

	FItemInstance CurrentItem;
public:
	virtual void NativeConstruct() override;
	void InitWidget(class UInventoryComponent* InInventory, class UEquipComponent* InEquip);
	void SetItem(const FItemInstance& InItem);
	void UpdateButtonState(EItemType type);
private:
	UFUNCTION()
	void OnEquipClickedBtn();
	UFUNCTION()
	void OnUnEquipClickedBtn();

	UFUNCTION()
	void OnUsedClickedBtn();

	UFUNCTION()
	void OnDropClicked();

	UFUNCTION()
	void OnCancledClicked();

	void InitButtonState();
};
