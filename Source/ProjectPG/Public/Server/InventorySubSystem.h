#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "Common/GameData.h"
#include "InventorySubSystem.generated.h"

// Inventory 전용 델리게이트 분배
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryReceived, const FInventoryMapWrapper&, ItemsWrapper);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEquipReceived, const FInventoryMapWrapper&, ItemsWrapper);

UCLASS()
class PROJECTPG_API UInventorySubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UInventorySubSystem* Get(UWorld* World);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void HandleInventoryMessage(const FString& MessageType, TSharedPtr<FJsonObject> PayloadObject);
	bool CaptureTravelInventory(const class UInventoryComponent* Inventory);
	const FInventorySnapshot* GetTravelInventory() const { return TravelInventory.bInitialized ? &TravelInventory : nullptr; }
	void ClearTravelInventory() { TravelInventory = FInventorySnapshot(); }

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestGetInventory();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestMoveItem(const FGuid& FromInventoryGuid, const FGuid& ToInventoryGuid, const FGuid& ItemGuid, const FIntPoint& TargetPosition, bool bIsRotated);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestEquipItem(const FGuid& ItemGuid, const FGuid& TargetParentGuid, bool bIsEquipped);

	// 로컬 캐시 및 WebSocket 사용 제어
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetUseWebSocket(bool bUse);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool IsUsingWebSocket() const { return bUseWebSocket; }

	// 로컬에 변경된 캐시를 강제로 서버에 저장(필요 시 GameMode에서 호출)
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ForceSaveToServer();

	// Replay cached inventory/equip data to newly bound listeners
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ReplayCachedInventory();

	// Inventory 관련 델리게이트 배치
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryReceived OnInventoryReceived;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnEquipReceived OnEquipReceived;

	bool bHasCachedInventory = false;
	// 서버에서 받은 장착(Equip) 캐시
	FInventoryMapWrapper CachedEquip;
	bool bHasCachedEquip = false;
	// 로컬에서 변경이 발생했는지 여부 (InGame 모드에서 로컬 변경 후 Lobby 복귀 시 동기화 필요)
	bool bHasLocalChanges = false;
	// 기본은 WebSocket 사용(로비 등)
	UPROPERTY()
	bool bUseWebSocket = true;

	// 강제 로컬 이동 모드: true면 RequestMoveItem 호출은 항상 로컬로 처리
	bool bForceLocalMoves = false;

public:
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetForceLocalMoves(bool bForce);

	FInventoryMapWrapper CachedInventory;

	// If true, we requested initial inventory and are waiting for the server response.
	// During this window we may prefer local handling of moves to avoid racing with server replay.
	bool bWaitingForInitialInventory = false;


public:
	// 로컬 변경 여부 확인
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool HasLocalChanges() const { return bHasLocalChanges; }

	// 단일 정책 지점: true면 RequestMoveItem/RequestEquipItem은 반드시 로컬 캐시에서만 처리하고
	// WebSocket으로 전송하지 않는다. 호출부는 이 함수를 직접 검사하지 말고
	// RequestMoveItem/RequestEquipItem을 그대로 호출하면 된다 (분기는 내부에서 처리).
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool IsLocalOnly() const { return bForceLocalMoves || !bUseWebSocket || bWaitingForInitialInventory; }

private:
	UPROPERTY()
	FInventorySnapshot TravelInventory;
};