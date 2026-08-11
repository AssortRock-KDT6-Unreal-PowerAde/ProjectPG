// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "Common/TableData.h"
#include "ItemWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UItemWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly)
	FItemInstance ItemInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TileSize = 64.0f;
protected:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class USizeBox> RootSizeBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UImage> ItemIcon;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UTextBlock> TextStackCount;

	FItemTableRow CachedItemData;
public:
	UFUNCTION(BlueprintCallable)
	void InitWidget(const FItemInstance InItem, const FItemTableRow& InData, float InTileSize);

	FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent);
	virtual bool NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;//손땟을시

	void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation);
	void RefreshWidget();
	const FItemTableRow* GetCachedItemData() const { return &CachedItemData; }


};
