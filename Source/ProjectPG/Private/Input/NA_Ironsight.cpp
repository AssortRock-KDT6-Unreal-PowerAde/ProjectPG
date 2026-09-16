// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_Ironsight.h"

#include "Animations/CustomAnimInstance.h"
#include "Characters/CustomPlayerCharacter.h"

bool UNA_Ironsight::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	switch (TriggerEvent)
	{
	case ETriggerEvent::Started:
	case ETriggerEvent::Completed:
		return true;
	default:
		return false;
	}
}

void UNA_Ironsight::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter))
		return;

	PlayerCharacter->OnReq_SetIronsight(true);
}

void UNA_Ironsight::Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter))
		return;

	PlayerCharacter->OnReq_SetIronsight(false);
}
