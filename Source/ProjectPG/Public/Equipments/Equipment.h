// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Equippable.h"
#include "Common/GameData.h"
#include "GameFramework/Actor.h"
#include "Equipment.generated.h"

USTRUCT()
struct FEquipmentReplicationState
{
	GENERATED_BODY()

	UPROPERTY()
	FName ItemID;
	UPROPERTY()
	TObjectPtr<ACharacter> Character;
	UPROPERTY()
	bool bEquipped = false;
};

UCLASS()
class PROJECTPG_API AEquipment : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AEquipment();

protected:
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> OwnerMesh;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, EEquipType> SocketItemTypes;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, TObjectPtr<UStaticMeshComponent>> StaticMeshesBySocket;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, TObjectPtr<USkeletalMeshComponent>> SkeletalMeshesBySocket;

	UPROPERTY(VisibleAnywhere)
	TMap<FName, TObjectPtr<AActor>> BlueprintsBySocket;

	UPROPERTY()
	FName EquipmentItemID;

	UPROPERTY()
	FWeaponHandIKData LeftHandIKData;

	UPROPERTY()
	TWeakObjectPtr<class UCustomAnimInstance> BoundAnimInstance;

	bool bIsEquipped = false;

private:
	UPROPERTY(ReplicatedUsing = OnRep_EquipmentState)
	FEquipmentReplicationState EquipmentState;
	FTimerHandle EquipmentStateRetryTimer;
	FName BuiltItemID;
	int32 EquipmentStateRetryCount = 0;

public:
	virtual void InitializeEquipment(FName ItemID = NAME_None);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Attach();
	virtual void Detach();

	void Equip(ACharacter* Character);
	void Unequip();

	UFUNCTION(Server, Reliable)
	void Req_Equip(ACharacter* Character);
	UFUNCTION(Server, Reliable)
	void Req_Unequip();

	UFUNCTION(BlueprintPure, Category = "Weapon|IK")
	bool GetLeftHandIKTarget(FTransform& TargetTransform, float& Alpha, FName& ReferenceBoneName) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	USceneComponent* FindEquipmentPart(FName PartName) const;

private:
	UFUNCTION()
	void OnRep_EquipmentState();
	void ApplyReplicatedEquipmentState();
	void EquipInternal(ACharacter* Character);
	void UnequipInternal();
};
