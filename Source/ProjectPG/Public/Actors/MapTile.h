// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Common/GameDefine.h"
#include "GameFramework/Actor.h"
#include "MapTile.generated.h"

UCLASS()
class PROJECTPG_API AMapTile : public AActor
{
	GENERATED_BODY()

public:
	AMapTile();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UStaticMeshComponent> _staticMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ETileType _type = ETileType::None;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

public:
	void SetType(ETileType Type);
	ETileType GetType() const;
};
