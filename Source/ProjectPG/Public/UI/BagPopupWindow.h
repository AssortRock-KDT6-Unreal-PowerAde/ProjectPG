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
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UCanvasPanel> RootCanvas;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UButton> CancleButton;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UOverlay> InventoryParent;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UOverlay> TopOverlay;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UOverlay> SubOverlay;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UOverlay> MainOverlay;

	UPROPERTY(meta = (BindWidget))	TObjectPtr<class USizeBox> TitleSizeBox;
	UPROPERTY(meta = (BindWidget))	TObjectPtr<class UBorder> WindowBorder;

	FGuid MyGuid;
	class UInventoryGridWidget* GridWidget;
	class UInventoryComponent* CachInvenComp;

	
	bool bIsDragging = false;

	FVector2D DragOffset = FVector2D::ZeroVector;
	FVector2D DragStartMousePosition;
	FVector2D DragStartPosition;
public:
	void NativeConstruct() override;
	UFUNCTION() void OnClickedCancleButton();

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseMove(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	void Init(class UInventoryComponent* InvenComp, FItemInstance Item);



};
