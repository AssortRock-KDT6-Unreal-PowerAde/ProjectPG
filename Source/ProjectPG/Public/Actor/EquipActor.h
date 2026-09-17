// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "EngineMinimal.h"
#include "GameFramework/Actor.h"
#include "EquipActor.generated.h"

UCLASS()
class PROJECTPG_API AEquipActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AEquipActor();
protected:
	UPROPERTY()
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> OwnerMesh;   // 캐릭터 Mesh

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> EquipMesh;     // 장비 Mesh

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;


public:


	void Equip(ACharacter* Character, FName SocketName);

	virtual void Unequip();

	virtual void Attach(FName SocketName);

	virtual void Detach();
	void SetWorldMesh(UStaticMesh* mesh);
};
