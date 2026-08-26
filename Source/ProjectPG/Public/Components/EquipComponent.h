// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/GameData.h"
#include "EquipComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEquipmentChanged);
UCLASS()
class PROJECTPG_API UEquipComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UEquipComponent();
private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Equip", meta = (AllowPrivateAccess = "true"))	TMap<EEquipSlot, FItemInstance> Equipments;
	UPROPERTY()TMap < EEquipSlot, TObjectPtr<class AEquipActor>> EquipActors;
	UPROPERTY()TMap<FGuid,EEquipSlot> EquipSlotGuids;
public:
	FOnEquipmentChanged OnEquipmentChanged;
protected:
	// Called when the game starts
	virtual void BeginPlay() override;

public:
	const TMap<EEquipSlot, TObjectPtr<class AEquipActor>>& GetEquipActors() const { return EquipActors; }
	bool Equip(const FItemInstance& Item);
	bool UnEquip(const FItemInstance Item);
	bool UnEquip(EEquipSlot slot);

	bool Swap(EEquipSlot slot1, EEquipSlot slot2);

	bool IsEquipped(const FGuid& Guid);
	bool CanEquip(const FItemInstance& Item, EEquipSlot slot) const;

	class AEquipActor* GetEquipActor(EEquipSlot slot) const;
	const FItemInstance* GetEquipment(EEquipSlot slot) const;
	
	void CopyFrom(UEquipComponent* Other);
	void RegisterGuid(EEquipSlot slottype, FGuid guid) { if(!EquipSlotGuids.Contains(guid))EquipSlotGuids.Add(guid,slottype); }
	class UInventoryComponent* GetOwnerInventoryComponent() const;
private:
	void SpawnEquipActor(EEquipSlot Slot, class UStaticMesh* Mesh);


		void DestroyEquipActor(EEquipSlot Slot);

		//아이템의 타입에 따른 능력 적용(장비창관련)
		void ApplyItemData(const FItemInstance& Item);

		void RemoveItemData(EEquipSlot slot);

		UFUNCTION() void SetServerEquipData(const FInventoryMapWrapper InWrapper);
		
};
