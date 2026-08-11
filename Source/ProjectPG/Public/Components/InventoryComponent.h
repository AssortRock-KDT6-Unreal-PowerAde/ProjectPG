// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/GameData.h"
#include "InventoryComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryUpdated);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECTPG_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

private:
	FIntPoint InventorySize = FIntPoint(10, 15);// 10열 15행 

public:
	UPROPERTY(BlueprintAssignable)
	FOnInventoryUpdated OnInventoryUpdated;
private:
	TArray<int32> GridMap;

	UPROPERTY()
	TArray<FItemInstance> Items;


public:	
	// Sets default values for this component's properties
	UInventoryComponent();

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:	
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	//아이템 놓을수 있는지 검사
	bool CanPlaceItem(const FName& ItemID, FIntPoint TargetPos, bool bRotated, FGuid IgnoreItemGUID = FGuid());

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MoveItem(FGuid ItemGUID, FIntPoint NewPos, bool bNewRotated);

	const FItemTableRow* GetItemData(FName ItemID) const;

	const TArray<FItemInstance>& GetItems() const { return Items; }

	int32 GetColumns() { return InventorySize.X; }
	int32 GetRows() { return InventorySize.Y; }

	// 1. 기존 생성된 FItemInstance 구조체를 인벤토리에 추가 (자동 공간 탐색)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItem(FItemInstance NewItem);

	// 2. ItemID 기반으로 신규 아이템을 생성하여 추가하는 편의 함수
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItemByID(FName ItemID, int32 Quantity = 1);

	bool AddItemByPosition(FName ItemID, int32 Quantity, FIntPoint pos);

private:
	void RebuildGridMap();
	int32 GetGridIndex(int32 X, int32 Y) const { return Y * InventorySize.X + X; }
};
