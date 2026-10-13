// Fill out your copyright notice in the Description page of Project Settings.


#include "Actor/EquipActor.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "Net/UnrealNetwork.h"

// Sets default values
AEquipActor::AEquipActor()
{
    PrimaryActorTick.bCanEverTick = false;
    bReplicates = true;
    bNetUseOwnerRelevancy = true;

    EquipMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EquipMesh"));
    RootComponent = EquipMesh;
    EquipMesh->SetMobility(EComponentMobility::Movable);
    EquipMesh->SetSimulatePhysics(false);
    EquipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

}

// Called when the game starts or when spawned
void AEquipActor::BeginPlay()
{
	Super::BeginPlay();
    SyncVisual();
}

void AEquipActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AEquipActor, OwnerCharacter);
    DOREPLIFETIME(AEquipActor, EquipSocketName);
    DOREPLIFETIME(AEquipActor, WorldMesh);
}

void AEquipActor::Equip(ACharacter* Character, FName InSocketName)
{
	OwnerCharacter = Character;
    if (HasAuthority()) SetOwner(Character);
    EquipSocketName = InSocketName;
    OwnerMesh = Character ? Character->GetMesh() : nullptr;
    SyncVisual();
}

void AEquipActor::OnRep_OwnerCharacter()
{
    OwnerMesh = OwnerCharacter ? OwnerCharacter->GetMesh() : nullptr;
    SyncVisual();
}

void AEquipActor::OnRep_SocketName()
{
    SyncVisual();
}

void AEquipActor::Unequip()
{
	Detach();

	OwnerCharacter = nullptr;
	OwnerMesh = nullptr;
    EquipSocketName = NAME_None;
    if (HasAuthority()) ForceNetUpdate();
}

void AEquipActor::Attach(FName InSocketName)
{
    if (!IsValid(OwnerMesh))
        return;

    bool bSuccess =
        AttachToComponent(
            OwnerMesh,
            FAttachmentTransformRules::SnapToTargetIncludingScale,
            InSocketName);

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
    WorldMesh = mesh;
    SyncVisual();
}

void AEquipActor::OnRep_EquipMesh()
{
    SyncVisual();
}

void AEquipActor::SyncVisual()
{
    if (EquipMesh)
    {
        EquipMesh->SetStaticMesh(WorldMesh);
    }
    OwnerMesh = IsValid(OwnerCharacter) ? OwnerCharacter->GetMesh() : nullptr;
    if (OwnerMesh)
    {
        Attach(EquipSocketName);
    }
    else
    {
        Detach();
    }
}

