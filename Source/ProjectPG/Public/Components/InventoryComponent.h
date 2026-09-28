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
	UPROPERTY()	TMap<FGuid, FItemArrayWrapper> ItemsMap;

	// 특수 인벤토리 고유 GUID 식별자
	UPROPERTY(VisibleAnywhere, Category = "Inventory|GUID")
	FGuid PocketInventoryID;

	UPROPERTY(VisibleAnywhere, Category = "Inventory|GUID")
	FGuid StashInventoryID;

	UPROPERTY(VisibleAnywhere, Category = "Inventory|GUID")
	TMap<EEquipSlot, FGuid> EquipSlotID;

	// ★ 방어: local-only(InGame) 모드로 전환된 후 이미 최초 동기화를 받은 컴포넌트는
	// 이후의 캐시 재생(ReplayCachedInventory)이나 지연된 브로드캐스트로 인해
	// ItemsMap 전체가 오래된 스냅샷으로 덮어써지는 것을 막는다.
	// (원인: ReplayCachedInventory()는 새로 생성된 다른 컴포넌트의 BeginPlay에서도
	// 호출되는데, 이 델리게이트는 전역 브로드캐스트라서 이미 로컬로 갱신된
	// 살아있는 InventoryComponent도 함께 리셋되어 유령 아이템이 재생성되었다.)
	bool bHasReceivedInitialSync = false;
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

	// 아이템을 특정 위치에 강제로 추가합니다 (장비 해제 시 사용)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItemAt(FItemInstance NewItem, FIntPoint TargetPos);

	// ItemID 및 수량을 전달받아 지정한 인벤토리에 추가
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItemByID(FName ItemID, const FGuid& TargetInvenGuid, int32 Quantity = 1);

	// GUID 기반 아이템 위치 변경 (동일 컴포넌트 내부에서만 사용)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MoveItem(const FGuid& TargetInvenGuid, FGuid ItemGUID, FIntPoint NewPos, bool bNewRotated);

	// 💡 이 컴포넌트에서 아이템을 찾아 제거하고 반환 (다른 InventoryComponent로 이동시킬 때 사용)
	bool RemoveItemByGUID(const FGuid& ItemGUID, FItemInstance& OutItem);

	// 💡 특정 컨테이너(GUID)에서만 아이템을 찾아 제거 (동일 GUID 아이템이 다른 컨테이너에도
	// 존재할 수 있는 상황, 예: 장비 슬롯 정리 시 목표 인벤토리에 이미 배치된 동일 GUID
	// 항목을 잘못 지우지 않도록 컨테이너를 명시적으로 지정할 때 사용)
	bool RemoveItemByGUIDFromContainer(const FGuid& ContainerGuid, const FGuid& ItemGUID, FItemInstance& OutItem);

	// 💡 다른 InventoryComponent가 소유한 아이템을 이 컴포넌트의 지정 위치로 이동 (Owner가 다른 경우)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool TransferItemFrom(UInventoryComponent* SourceComp, const FGuid& ItemGUID, const FGuid& TargetInvenGuid, FIntPoint NewPos, bool bNewRotated);


	UFUNCTION()	void SetServerInventoryData(const FInventoryMapWrapper InWrapper);

	int32 GetGridIndex(const FGuid& InvenGuid, int32 X, int32 Y) const;

	void RegisterContainer(const FGuid& ContainerGUID, FIntPoint ContainerSize);

	// 동적 컨테이너(가방 등) 해제
	void UnregisterContainer(const FGuid& ContainerGUID);

	// GUID로 인벤토리 크기 가져오는 보조 함수
	FIntPoint GetInventorySizeByGuid(const FGuid& InvenGuid) const;

	UFUNCTION()	void HandleInventoryReceived(const FInventoryMapWrapper& InventoryMapWrapper);

	void PurgeDuplicateGuidEverywhere(const FGuid& ItemGUID);

private:
	void RebuildGridMapByGuid(const FGuid& InvenGuid);

	// ★ 방어 로직: 실제로 아이템을 컨테이너에 삽입하기 직전, 목표 영역(TargetPos~Size)과
	// 겹치는 기존 아이템(자기 자신 GUID 제외)이 남아있다면 강제로 제거한다.
	// CanPlaceItemByGuid가 사전 검사를 하지만, 여러 경로(장착 해제/재장착/크로스 인벤토리 이동)에서
	// 그리드 재구축 타이밍 차이로 유령 아이템이 검사를 통과해 겹치게 배치되는 경우가 있었기 때문에
	// 삽입 시점에 한 번 더 강제로 정리하여 고스트 아이템 재생성을 원천 차단한다.
	void PurgeOverlappingItems(const FGuid& ContainerGuid, FIntPoint TargetPos, FIntPoint ItemSize, const FGuid& IgnoreItemGUID);

	// ★ 방어 로직: 어떤 경로로든 동일 GUID를 가진 아이템이 두 개 이상의 컨테이너에
	// 동시에 존재하게 되는 상황(비정상 상태)을 감지하고, ExceptContainerGuid를 제외한
	// 모든 컨테이너에서 해당 GUID 항목을 제거하여 중복/유령 아이템이 영구적으로
	// 남지 않도록 한다. 배치(Add/Move/Transfer) 계열 함수 진입 시 항상 먼저 호출한다.
	void PurgeDuplicateGuidExcept(const FGuid& ItemGUID, const FGuid& ExceptContainerGuid);

	// ★ 더 강한 방어: 같은 월드에 존재하는 다른 InventoryComponent들까지 포함해
	// 동일 GUID를 제거한다. backpack처럼 장착/해제와 이동이 반복되는 아이템은
	// 이전에 다른 인벤토리 컴포넌트에 남아 있던 유령 복사본까지 함께 정리해야
	// “이전 장착 해제 위치에 다시 생기는” 증상을 막을 수 있다.

};