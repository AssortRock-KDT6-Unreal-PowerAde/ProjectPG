// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/NativeAction.h"
#include "NA_Interact.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UNA_Interact : public UNativeAction
{
	GENERATED_BODY()
	
public:
	bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter);

	
	
};
