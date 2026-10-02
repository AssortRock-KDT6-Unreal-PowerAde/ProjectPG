// Fill out your copyright notice in the Description page of Project Settings.


#include "Equipments/Equipment.h"

#include "GameFramework/Character.h"

AEquipment::AEquipment()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AEquipment::Equip(ACharacter* Character, FName SocketName)
{
	OwnerCharacter = Character;
	OwnerMesh = Character->GetMesh();
	Attach(SocketName);
}

void AEquipment::Unequip()
{
	Detach();
	OwnerCharacter = nullptr;
	OwnerMesh = nullptr;
}

void AEquipment::Attach(FName SocketName)
{
	if (!IsValid(OwnerMesh))
		return;

	bool bSuccess =
		AttachToComponent(
			OwnerMesh,
			FAttachmentTransformRules::SnapToTargetIncludingScale,
			SocketName);
}

void AEquipment::Detach()
{
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}

void AEquipment::BeginPlay()
{
	Super::BeginPlay();
}
