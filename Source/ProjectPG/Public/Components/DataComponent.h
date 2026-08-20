// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Common/GameData.h"
#include "DataComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECTPG_API UDataComponent : public UActorComponent
{
	GENERATED_BODY()
public:	
	// Sets default values for this component's properties
	UDataComponent();
private:
	UPROPERTY()
	TMap<FGuid,FItemArrayWrapper> ItemData;
protected:
	// Called when the game starts
	virtual void BeginPlay() override;
public:
	UFUNCTION()	void LoadInventoryData(FInventoryMapWrapper Items);


		
};
