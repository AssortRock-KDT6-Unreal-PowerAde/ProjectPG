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
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	UPROPERTY(ReplicatedUsing = OnRep_OwnerCharacter)
	TObjectPtr<ACharacter> OwnerCharacter;

	UPROPERTY(ReplicatedUsing = OnRep_SocketName)
	FName EquipSocketName;

	UPROPERTY(ReplicatedUsing = OnRep_EquipMesh)
	TObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY()
	TObjectPtr<USkeletalMeshComponent> OwnerMesh;   // 캐릭터 Mesh

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> EquipMesh;     // 장비 Mesh

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;


public:


	void Equip(ACharacter* Character, FName InSocketName);

	virtual void Unequip();

	virtual void Attach(FName InSocketName);

	virtual void Detach();
	void SetWorldMesh(UStaticMesh* mesh);

protected:
	UFUNCTION()
	void OnRep_OwnerCharacter();

	UFUNCTION()
	void OnRep_SocketName();

	UFUNCTION()
	void OnRep_EquipMesh();

private:
	void SyncVisual();
};
