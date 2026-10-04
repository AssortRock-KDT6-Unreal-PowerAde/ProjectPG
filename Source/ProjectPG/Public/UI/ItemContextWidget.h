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
	TObjectPtr<class UButton> OpenButton;

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

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UOverlay> MainOverlay;

	// (10/4 기획서 1.2.1 우클릭 메뉴) 돌리기·나누기. WBP 에 없으면 그 버튼만 안 보인다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> RotateButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UButton> SplitButton;
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
	UFUNCTION()	void OnEquipClickedBtn();
	UFUNCTION()	void OnUnEquipClickedBtn();

	UFUNCTION()	void OnUsedClickedBtn();

	UFUNCTION()	void OnDropClicked();

	UFUNCTION()	void OnCancledClicked();

	UFUNCTION() void OnOpenClickBtn(); //가방열때만나옴

	UFUNCTION() void OnRotateClicked();
	UFUNCTION() void OnSplitClicked();

	// 지금 플레이어의 인벤토리 컴포넌트(플레이어 상태에 붙어 있음).
	class UInventoryComponent* FindInventory() const;

	void InitButtonState();
};
