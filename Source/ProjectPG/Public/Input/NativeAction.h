// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "UObject/Interface.h"
#include "NativeAction.generated.h"

class ACustomPlayerCharacter;
// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UNativeActionInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PROJECTPG_API INativeActionInterface
{
	GENERATED_BODY()

public:
	virtual void Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) = 0;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) = 0;
	virtual void Ongoing(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) = 0;
	virtual void Canceled(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) = 0;
	virtual void Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) = 0;
};

UCLASS()
class PROJECTPG_API UNativeAction : public UObject, public INativeActionInterface
{
	GENERATED_BODY()

public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const;

	virtual void Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
	virtual void Ongoing(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
	virtual void Canceled(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
	virtual void Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
};
