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
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UImage> Icon;
	TObjectPtr<class UTextBlock> ItemName;
	// 슬롯 하이라이트용 Background Image (옵션 바인드)
	UPROPERTY(meta = (BindWidgetOptional))	TObjectPtr<class UImage> BackGround;

	// 슬롯 하이라이트용 Border (옵션 바인드)
	UPROPERTY(meta = (BindWidgetOptional))	TObjectPtr<class UBorder> SlotBorder;

	// 기본 백그라운드 색상 저장
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FLinearColor DefaultBackColor = FLinearColor(0.05f, 0.05f, 0.05f, 0.8f);

	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
private:
	EEquipSlot Slot;
	const FItemInstance* Item;

	UPROPERTY()
	TObjectPtr<class UEquipComponent> EquipComp;

	UPROPERTY()	
	TObjectPtr<class UInventoryComponent> InvenComp;

public:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UFUNCTION(BlueprintCallable)
	bool RequestUnEquip();

	// 하이라이트 상태 설정 (InventoryGrid와 동일 방식)
	UFUNCTION(BlueprintCallable)
	void SetHighlightState(EBorderHighlightState InState);

	void SetSlot(EEquipSlot InSlot);
	void SetItem(const FItemInstance* InItem);
	void Clear();
	void InitWidget(class UEquipComponent* InEquip, class UInventoryComponent* InInvetory);

	virtual bool NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;

};
