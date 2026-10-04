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

	UPROPERTY(EditAnywhere, Category = "Inventory")
	TSubclassOf<class USlotWidget> SlotWidgetClass;

	UPROPERTY(EditAnywhere, Category = "Inventory")
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

	// 마지막으로 하이라이트된 타일 (NativeOnDragOver에서 갱신, NativeOnDrop에서 우선 사용)
	UPROPERTY()
	FIntPoint LastHoveredTile = FIntPoint(-1, -1);
protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);
	virtual bool NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

public:


	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void BindInventoryComponent(class UInventoryComponent* InComp);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RefreshGridUI();
	
	void RenderItems();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void CreateBackGroundGrid(int32 Columns, int32 Rows);

	class USlotWidget* GetSlotWidgetAt(int32 TileX, int32 TileY);
	bool CanPlaceItemAt(const FItemInstance& ItemToPlace, FIntPoint TargetTile, FIntPoint GridSize);
	const FItemInstance* GetItemAtCell(int32 TileX, int32 TileY);
	void ClearSlotHighlights();

	FIntPoint MouseToTilePosition(const FVector2D& LocalMousePos, const FVector2D& DragOffset);

	FORCEINLINE FGuid GetInventoryGUID() const { return InventoryGUID; }
	FORCEINLINE void SetInventoryGUID(const FGuid& InGuid) { InventoryGUID = InGuid; }

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RefreshGrid(class UInventoryComponent* InComp, const FGuid& InvenGuid);

	// (10/4 기획서 1.2.1 필터링·검색) 이름에 Text 가 들어 있고 종류가 맞는 아이템만 진하게, 나머지는 흐리게 그린다.
	// TypeFilter < 0 = 모든 종류. 옮기기·놓기는 그대로 된다(보이는 것만 바뀜).
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetFilter(const FString& Text, int32 TypeFilter);
private:
	bool MatchesFilter(const FItemTableRow& Data) const;
	FString FilterText;
	int32 FilterType = -1;

	FIntPoint CalculateDropTile(
		const FVector2D& ScreenMousePosition,
		class UItemDragDropOperation* DragOp
	) const;
};