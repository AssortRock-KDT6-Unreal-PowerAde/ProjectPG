// Fill out your copyright notice in the Description page of Project Settings.


#include "Actor/EquipActor.h"

// Sets default values
AEquipActor::AEquipActor()
{
    PrimaryActorTick.bCanEverTick = false;
    EquipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EquipMesh"));
    RootComponent = EquipMesh;
    EquipMesh->SetSimulatePhysics(false);
    EquipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

}

// Called when the game starts or when spawned
void AEquipActor::BeginPlay()
{
	Super::BeginPlay();
	
}

void AEquipActor::Equip(ACharacter* Character, FName SocketName)
{
	OwnerCharacter = Character;
	OwnerMesh = Character->GetMesh();
	Attach(SocketName);
}

void AEquipActor::Unequip()
{
	Detach();

	OwnerCharacter = nullptr;
	OwnerMesh = nullptr;
}

void AEquipActor::Attach(FName SocketName)
{
    if (!IsValid(OwnerMesh))
        return;

    bool bSuccess =
        AttachToComponent(
            OwnerMesh,
            FAttachmentTransformRules::SnapToTargetIncludingScale,
            SocketName);

    if (!bSuccess)
    {
        UE_LOG(LogTemp, Warning, TEXT("Equip Failed"));
        return;
    }

    UE_LOG(LogTemp, Warning, TEXT("Equip Success"));
}

void AEquipActor::Detach()
{
    DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

}

void AEquipActor::SetWorldMesh(UStaticMesh* mesh)
{
    if (EquipMesh) EquipMesh->SetStaticMesh(mesh);

}

