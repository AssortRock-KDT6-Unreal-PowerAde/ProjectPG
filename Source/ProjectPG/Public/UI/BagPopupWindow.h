// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/TableData.h"

#include "BagPopupWindow.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UBagPopupWindow : public UUserWidget
{
	GENERATED_BODY()
	

private:
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UButton> CancleButton;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UOverlay> InventoryParent;

	FGuid MyGuid;
	class UInventoryGridWidget* GridWidget;
public:
	void NativeConstruct() override;
	UFUNCTION() void OnClickedCancleButton();

	void Init(class UInventoryComponent* InvenComp, FItemInstance Item);



};
