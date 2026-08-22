// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_Crouch.h"

#include "Characters/CustomPlayerCharacter.h"

bool UNA_Crouch::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	if (ETriggerEvent::Triggered == TriggerEvent)
		return true;

	return false;
}

void UNA_Crouch::Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (PlayerCharacter->IsCrouched())
		PlayerCharacter->UnCrouch();
	else
		PlayerCharacter->Crouch();
}
