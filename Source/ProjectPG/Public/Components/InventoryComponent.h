// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/GameData.h"
#include "InventoryComponent.generated.h"

// 인벤토리별 GridMap(1D 배열)을 TMap Value로 등록하기 위한 Wrapper 구조체
USTRUCT(BlueprintType)
struct FIntArrayWrapper
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<int32> Grid;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryUpdated);

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnInventoryUpdated OnInventoryUpdated;

private:
	// 가방 GUID별 격자 크기 (예: PocketGUID / StashGUID -> 10x15)
	UPROPERTY(EditAnywhere, Category = "Inventory")
	TMap<FGuid, FIntPoint> InventorySizeMap;

	// 가방 GUID별 격자 상태 배열 (-1: 빈칸, >=0: ItemsMap 내 배열 인덱스)
	UPROPERTY()
	TMap<FGuid, FIntArrayWrapper> InvenGridMap;

	// 가방 GUID별 아이템 목록
	UPROPERTY()
	TMap<FGuid, FItemArrayWrapper> ItemsMap;

	// 특수 인벤토리 고유 GUID 식별자
	UPROPERTY(VisibleAnywhere, Category = "Inventory|GUID")
	FGuid PocketInventoryID;

	UPROPERTY(VisibleAnywhere, Category = "Inventory|GUID")
	FGuid StashInventoryID;

public:
	UInventoryComponent();

protected:
	virtual void BeginPlay() override;

public:
	// ============================================================================
	// 1. 상태 조회 및 설정 (Getters & Setters)
	// ============================================================================

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetInventorySizeByGuid(const FGuid& InvenGuid, FIntPoint Size) { InventorySizeMap.FindOrAdd(InvenGuid) = Size; }

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	FIntPoint GetInventorySizeByGuid(const FGuid& InvenGuid) const { return InventorySizeMap.Contains(InvenGuid) ? InventorySizeMap[InvenGuid] : FIntPoint::ZeroValue; }

	const FItemTableRow* GetItemData(FName ItemID) const;
	const FItemInstance* GetItemInstance(FName ItemID) const;

	int32 GetColumns(const FGuid& InvenGuid) const;
	int32 GetRows(const FGuid& InvenGuid) const;

	const TMap<FGuid, FItemArrayWrapper>& GetItemsMap() const { return ItemsMap; }
	const TArray<FItemInstance>& GetItems(const FGuid& InvenGuid) const;

	FORCEINLINE FGuid GetPocketInventoryID() const { return PocketInventoryID; }
	FORCEINLINE FGuid GetStashInventoryID() const { return StashInventoryID; }

	FORCEINLINE void SetPocketInventoryID(const FGuid& InGuid) { PocketInventoryID = InGuid; }
	FORCEINLINE void SetStashInventoryID(const FGuid& InGuid) { StashInventoryID = InGuid; }

	// ============================================================================
	// 2. 배치 검사 및 조작 (Placement & Item Operations)
	// ============================================================================

	// 특정 인벤토리 내 배치 가능 여부 확인
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool CanPlaceItemByGuid(const FGuid& InvenGuid, const FName& ItemID, FIntPoint TargetPos, bool bRotated, FGuid IgnoreItemGUID = FGuid());

	// FItemInstance 내부의 parent_inventory_guid를 읽어 자동으로 공간을 찾아 배치
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItem(FItemInstance NewItem);

	// ItemID 및 수량을 전달받아 지정한 인벤토리에 추가
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItemByID(FName ItemID, const FGuid& TargetInvenGuid, int32 Quantity = 1);

	// GUID 기반 아이템 위치 변경
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MoveItem(const FGuid& TargetInvenGuid, FGuid ItemGUID, FIntPoint NewPos, bool bNewRotated);

	// 외부 저장 데이터 일괄 재할당
	void AllocateItemDataByGuid(const TMap<FGuid, FItemArrayWrapper>& ItemData);

	int32 GetGridIndex(const FGuid& InvenGuid, int32 X, int32 Y) const;

private:
	// 특정 가방의 GridMap 재구성
	void RebuildGridMapByGuid(const FGuid& InvenGuid);
};