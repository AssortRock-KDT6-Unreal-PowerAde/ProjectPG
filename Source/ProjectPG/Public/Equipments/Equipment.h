// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Equippable.h"
#include "GameFramework/Actor.h"
#include "Equipment.generated.h"

UCLASS()
class PROJECTPG_API AEquipment : public AActor, public IEquippable
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

	// EquipMesh는 Skeletal / Static 여부에 따라 상속하여 구현

public:
	virtual void Equip(ACharacter* Character, FName SocketName) override;
	virtual void Unequip() override;
	virtual void Attach(FName SocketName) override;
	virtual void Detach() override;

protected:
	virtual void BeginPlay() override;
};
