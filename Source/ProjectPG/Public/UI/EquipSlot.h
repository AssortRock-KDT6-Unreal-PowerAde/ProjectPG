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
	// 주의: UEquipComponent::Equipments(TMap)의 포인터를 직접 들고 있으면
	// TMap Add/Remove 시 재해싱으로 인해 다른 슬롯 변경만으로도 댕글링 포인터가 되어
	// 유령/스테일 아이템 데이터가 드래그 payload로 복사되는 문제가 있었다.
	// 따라서 값(FItemInstance)으로 스냅샷을 보관한다.
	bool bHasItem = false;
	FItemInstance Item;

	UPROPERTY()
	TObjectPtr<class UEquipComponent> EquipComp;

	UPROPERTY()	
	TObjectPtr<class UInventoryComponent> InvenComp;

public:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	UFUNCTION(BlueprintCallable)
	bool RequestUnEquip();

	// UI 드래그 롤백 등에서 원본 슬롯의 EquipComponent에 접근하기 위한 getter
	class UEquipComponent* GetEquipComponent() const { return EquipComp; }
	EEquipSlot GetSlot() const { return Slot; }

	// 강제 클리어: 드래그 중에도 슬롯을 즉시 비우기 위해 사용
	UFUNCTION(BlueprintCallable)
	void ForceClear();

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
