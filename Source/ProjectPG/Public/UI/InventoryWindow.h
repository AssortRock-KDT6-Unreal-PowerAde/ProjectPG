// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryWindow.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UInventoryWindow : public UUserWidget
{
	GENERATED_BODY()

private:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UOverlay> EquipOverlay;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UOverlay> SubInventoryOverlay;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UOverlay> MainInventoryOverlay;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UOverlay> BackPackInvenOverlay;

	UPROPERTY()TObjectPtr<class UInventoryComponent> InvenComp = nullptr;
	UPROPERTY()TObjectPtr<class UEquipComponent> EquipComp = nullptr;

public:
	void InitWidget(class UInventoryComponent* InvenComponent, class UEquipComponent* EquipComponent);
	void SetChildEquipOverlay(UUserWidget * childWidget);
	void SetChildSubInvenOverlay(UUserWidget* childWidget);
	void SetChildMainInvenOverlay(UUserWidget* childWidget);
	void SetChildBackpackInvenOverlay(UUserWidget* childWidget);

	void UpdateState();

};
