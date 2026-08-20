// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CustomPlayerState.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ACustomPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	ACustomPlayerState();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UInventoryComponent> InvenComp;
	TObjectPtr<class UDataComponent> DataComp;
	TObjectPtr<class UEquipComponent> EquipComp;
private:
	FGuid InventoryGuid;

protected:
	virtual void BeginPlay() override;
};
