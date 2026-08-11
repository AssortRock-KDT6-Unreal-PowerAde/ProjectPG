// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CustomPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ACustomPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	void ToggleInventory();
	void SetupInputComponent() override;
	void OnRotateKey();
};
