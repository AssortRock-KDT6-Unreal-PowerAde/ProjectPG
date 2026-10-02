// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Equipments/Equipment.h"
#include "SkeletalEquipActor.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ASkeletalEquipActor : public AEquipment
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ASkeletalEquipActor();

protected:
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USkeletalMeshComponent> EquipMesh; // 장비 Mesh

	UPROPERTY(VisibleAnywhere)
	FName AttachedPartName;

public:
	void SetWorldMesh(USkeletalMesh* mesh);

	virtual void Attach(FName PartName) override;
	virtual void Detach() override;
};
