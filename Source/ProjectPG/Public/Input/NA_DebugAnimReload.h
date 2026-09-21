// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/NativeAction.h"
#include "NA_DebugAnimReload.generated.h"

class UAnimMontage;

/**
 * 
 */
UCLASS()
class PROJECTPG_API UNA_DebugAnimReload : public UNativeAction
{
	GENERATED_BODY()

public:
	UNA_DebugAnimReload();

private:
	UPROPERTY()
	TObjectPtr<UAnimMontage> HipMontage;

	UPROPERTY()
	TObjectPtr<UAnimMontage> IronsightsMontage;
	
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
};
