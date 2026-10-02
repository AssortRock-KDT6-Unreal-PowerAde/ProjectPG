// Fill out your copyright notice in the Description page of Project Settings.


#include "Actor/SkeletalEquipActor.h"

ASkeletalEquipActor::ASkeletalEquipActor()
{
	PrimaryActorTick.bCanEverTick = false;
	EquipMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("EquipMesh"));
	RootComponent = EquipMesh;
	EquipMesh->SetSimulatePhysics(false);
	EquipMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ASkeletalEquipActor::SetWorldMesh(USkeletalMesh* mesh)
{
	if (EquipMesh) EquipMesh->SetSkeletalMesh(mesh);
}

void ASkeletalEquipActor::Attach(FName PartName)
{
	if (!IsValid(OwnerMesh))
		return;

	bool bSuccess =
		AttachToComponent(
			OwnerMesh,
			FAttachmentTransformRules::SnapToTargetIncludingScale);

	if (IsValid(EquipMesh))
		EquipMesh->SetLeaderPoseComponent(OwnerMesh);

	TArray<USceneComponent*> childrenComps;
	OwnerMesh->GetChildrenComponents(false, childrenComps);

	for (auto* comp : childrenComps)
	{
		if (!IsValid(comp))
			continue;

		if (comp->GetName() == PartName)
		{
			comp->SetVisibility(false, true);
			break;
		}
	}

	AttachedPartName = PartName;
}

void ASkeletalEquipActor::Detach()
{
	if (IsValid(EquipMesh))
		EquipMesh->SetLeaderPoseComponent(nullptr);

	TArray<USceneComponent*> childrenComps;
	OwnerMesh->GetChildrenComponents(false, childrenComps);

	for (auto* comp : childrenComps)
	{
		if (!IsValid(comp))
			continue;

		if (comp->GetName() == AttachedPartName)
		{
			comp->SetVisibility(true, true);
			break;
		}
	}

	AttachedPartName = TEXT("");

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}
