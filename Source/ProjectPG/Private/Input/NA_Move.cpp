// Fill out your copyright notice in the Description page of Project Settings.

#include "Input/NA_Move.h"
#include "Characters/CustomPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"

bool UNA_Move::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Triggered;
}

void UNA_Move::Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;
	auto* Movement = PlayerCharacter->GetCharacterMovement();
	if (!IsValid(Movement))
		return;
	const FVector2D Value = InputActionValue.Get<FVector2D>().GetClampedToMaxSize(1.f);
	const FRotator ControlYaw(0.f, PlayerCharacter->GetControlRotation().Yaw, 0.f);
	Movement->AddInputVector(ControlYaw.RotateVector(FVector(Value.X, Value.Y, 0.f)));
}
