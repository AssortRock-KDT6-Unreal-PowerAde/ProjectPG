// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/NativeAction.h"
#include "NA_DebugAnimDead.generated.h"

class UAnimMontage;

/**
 * 
 */
UCLASS()
class PROJECTPG_API UNA_DebugAnimDead : public UNativeAction
{
	GENERATED_BODY()

public:
	UNA_DebugAnimDead();

private:
	UPROPERTY()
	TObjectPtr<UAnimMontage> HipMontage;

	UPROPERTY()
	TObjectPtr<UAnimMontage> IronsightsMontage;
	
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;

};
