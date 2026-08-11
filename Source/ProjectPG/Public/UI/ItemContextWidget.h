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
	TObjectPtr<class UButton> CancelButton;
private:
	UPROPERTY()
	TObjectPtr<class UInventoryComponent> InvenComp;

	FItemInstance CurrnetItem;
};
