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

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestGetInventory();

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestMoveItem(const FGuid& FromInventoryGuid, const FGuid& ToInventoryGuid, const FGuid& ItemGuid, const FIntPoint& TargetPosition, bool bIsRotated);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void RequestEquipItem(const FGuid& ItemGuid, const FGuid& TargetParentGuid, bool bIsEquipped);

	// Inventory 관련 델리게이트 배치
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryReceived OnInventoryReceived;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnEquipReceived OnEquipReceived;
};