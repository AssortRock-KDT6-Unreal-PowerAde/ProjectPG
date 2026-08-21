// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_Move.h"

#include "Characters/CustomPlayerCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"

bool UNA_Move::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	if (ETriggerEvent::Triggered == TriggerEvent)
		return true;

	return false;
}

void UNA_Move::Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	USpringArmComponent* cameraArmComp = PlayerCharacter->GetCameraArm();

	FVector2D value = InputActionValue.Get<FVector2D>();
	value = value.GetClampedToMaxSize(1.0f);

	FRotator cameraArmRotation = cameraArmComp->GetComponentRotation();
	FRotator actorRotation = FRotator(0, cameraArmRotation.Yaw, 0);
	PlayerCharacter->SetActorRotation(actorRotation);
	cameraArmComp->SetRelativeRotation(FRotator(cameraArmRotation.Pitch, 0, 0));

	UCharacterMovementComponent* movementComp = PlayerCharacter->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	FVector inputVector = FVector(value.X, value.Y, 0);
	inputVector = actorRotation.RotateVector(inputVector);
	movementComp->AddInputVector(inputVector);
}
