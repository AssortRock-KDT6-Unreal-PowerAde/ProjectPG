// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Common/GameData.h"
#include "ItemDragDropOperation.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UItemDragDropOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:

	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	class UUserWidget* WidgetReference;

	UPROPERTY(BlueprintReadWrite)
	FItemInstance DraggedItem;

	UPROPERTY(BlueprintReadWrite)
	bool bCurrentRotated = false;

	UPROPERTY(BlueprintReadWrite)
	FVector2D DragOffset;
	void RotateItem();

public:
	virtual void DragCancelled_Implementation(const FPointerEvent& PointerEvent) override;
};
