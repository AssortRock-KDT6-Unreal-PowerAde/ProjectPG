// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_Look_Mouse.h"

#include "Characters/CustomPlayerCharacter.h"
#include "GameFramework/SpringArmComponent.h"

bool UNA_Look_Mouse::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	if (ETriggerEvent::Triggered == TriggerEvent)
		return true;

	return false;
}

void UNA_Look_Mouse::Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	USpringArmComponent* cameraArmComp = PlayerCharacter->GetCameraArm();

	FVector2D value = InputActionValue.Get<FVector2D>();

	FRotator rotator = cameraArmComp->GetRelativeRotation();
	rotator.Yaw += value.X;
	rotator.Pitch = FMath::Clamp(rotator.Pitch + value.Y, -89.f, 89.f);

	cameraArmComp->SetRelativeRotation(rotator);
}
