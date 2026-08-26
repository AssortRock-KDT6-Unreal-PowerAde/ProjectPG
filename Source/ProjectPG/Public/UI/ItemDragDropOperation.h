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

	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	FItemInstance DraggedItem;

	// [추가] 드래그를 시작한 출발지 인벤토리의 GUID
	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	FGuid SourceInventoryGUID;

	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	bool bCurrentRotated = false;

	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	FVector2D DragOffset;

	// 드래그 출처가 장착 슬롯인지 여부
	UPROPERTY(BlueprintReadWrite, Category = "DragDrop")
	bool bFromEquip = false;

	void RotateItem();

public:
	virtual void DragCancelled_Implementation(const FPointerEvent& PointerEvent) override;
};