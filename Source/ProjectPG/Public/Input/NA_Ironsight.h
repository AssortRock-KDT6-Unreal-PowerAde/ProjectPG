// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/NativeAction.h"
#include "NA_Ironsight.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UNA_Ironsight : public UNativeAction
{
	GENERATED_BODY()

public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
	virtual void Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
};
