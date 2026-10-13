// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Equippable.h"
#include "Common/GameData.h"
#include "GameFramework/Actor.h"
#include "Equipment.generated.h"

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

public:
	virtual void InitializeEquipment(const struct FEquipTableRow& Data);
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
	void EquipInternal(ACharacter* Character);
	void UnequipInternal();
};
