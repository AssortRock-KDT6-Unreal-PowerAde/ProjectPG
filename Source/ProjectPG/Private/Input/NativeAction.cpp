// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NativeAction.h"

bool UNativeAction::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return false;
}

void UNativeAction::Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
}

void UNativeAction::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
}

void UNativeAction::Ongoing(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
}

void UNativeAction::Canceled(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
}

void UNativeAction::Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
}
