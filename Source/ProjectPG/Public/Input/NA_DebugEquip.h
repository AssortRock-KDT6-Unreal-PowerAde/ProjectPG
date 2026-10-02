// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/NativeAction.h"
#include "NA_DebugEquip.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UNA_DebugEquip : public UNativeAction
{
	GENERATED_BODY()
	
public:
	UNA_DebugEquip();
	
private:
	UPROPERTY()
	TObjectPtr<UAnimMontage> Montage;
	
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
};
