// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "InventoryGridWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UInventoryGridWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere,BlueprintReadWrite, Category = "UI")
	float TileSize = 64.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TSubclassOf<class USlotWidget> SlotWidgetClass; // 에디터 디테일 창에서 지정할 BP 클래스 변수
protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UUniformGridPanel> BackGroundGrid;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UCanvasPanel> ItemCanvas;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class USizeBox> InventorySizeBox;
private:
	UPROPERTY()
	TObjectPtr<class UInventoryComponent> TargetInventoryComp;
	TArray<USlotWidget*> HighlightedSlots;

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	TSubclassOf<class UItemWidget> ItemWidgetClass;



	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable)
	void CreateBackGroundGrid(int32 Columns, int32 Rows);

	UFUNCTION(BlueprintCallable)
	void BindInventoryComponent(class UInventoryComponent* InComp);

	UFUNCTION(BlueprintCallable)
	void RefreshGridUI();

	virtual FReply NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;//최초 한번 벗어날대
	virtual void NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;//위젯 영역 벗어날때
	virtual bool NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;//손땟을시
	void ClearSlotHighlights();
	FIntPoint MouseToTilePosition(const FVector2D& LocalMousePos, const FVector2D& DragOffset);
	UFUNCTION()
	class USlotWidget* GetSlotWidgetAt(int32 TileX, int32 TileY);

private:
	bool CanPlaceItemAt(const FItemInstance& ItemToPlace, FIntPoint TargetTile, FIntPoint GridSize);


	FItemInstance* GetItemAtCell(int32 TileX, int32 TileY);

};
