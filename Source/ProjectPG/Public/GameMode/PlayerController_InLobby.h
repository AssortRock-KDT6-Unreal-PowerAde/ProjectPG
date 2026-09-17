// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "PlayerController_InLobby.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API APlayerController_InLobby : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	void ToggleInventory();
	void SetupInputComponent() override;
	void OnRotateKey();
	
};
