// Fill out your copyright notice in the Description page of Project Settings.

#include "Input/NA_Look_Mouse.h"
#include "Characters/CustomPlayerCharacter.h"
#include "GameFramework/PlayerController.h"

bool UNA_Look_Mouse::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Triggered;
}

void UNA_Look_Mouse::Triggered(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;
	auto* Controller = Cast<APlayerController>(PlayerCharacter->GetController());
	if (!IsValid(Controller))
		return;
	const FVector2D Value = InputActionValue.Get<FVector2D>();
	FRotator ControlRotation = Controller->GetControlRotation();
	ControlRotation.Yaw += Value.X;
	ControlRotation.Pitch = FMath::Clamp(ControlRotation.Pitch + Value.Y, -89.f, 89.f);
	Controller->SetControlRotation(ControlRotation);
}
