// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "UI/SlotWidget.h"
#include "InventoryGridWidget.generated.h"


UCLASS()
class PROJECTPG_API UInventoryGridWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FGuid InventoryGUID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	float TileSize = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	FIntPoint SlotSize = FIntPoint(10, 15);

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<class USlotWidget> SlotWidgetClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<class UItemWidget> ItemWidgetClass;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UUniformGridPanel> BackGroundGrid;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UCanvasPanel> ItemCanvas;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class USizeBox> InventorySizeBox;

	UPROPERTY()
	class UInventoryComponent* TargetInventoryComp;

	UPROPERTY()
	TArray<class USlotWidget*> HighlightedSlots;
protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual bool NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

public:
	// GUID 기반 초기화 함수
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void InitWidget(const FGuid& InvenGuid, FIntPoint GridSize);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void BindInventoryComponent(class UInventoryComponent* InComp);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RefreshGridUI();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void CreateBackGroundGrid(int32 Columns, int32 Rows);

	class USlotWidget* GetSlotWidgetAt(int32 TileX, int32 TileY);
	bool CanPlaceItemAt(const FItemInstance& ItemToPlace, FIntPoint TargetTile, FIntPoint GridSize);
	const FItemInstance* GetItemAtCell(int32 TileX, int32 TileY);
	void ClearSlotHighlights();

	FIntPoint MouseToTilePosition(const FVector2D& LocalMousePos, const FVector2D& DragOffset);

	FORCEINLINE FGuid GetInventoryGUID() const { return InventoryGUID; }
	FORCEINLINE void SetInventoryGUID(const FGuid& InGuid) { InventoryGUID = InGuid; }


};