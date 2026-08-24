// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_Look_Mouse.h"

#include "Animations/CustomAnimInstance.h"
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
	FVector2D value = InputActionValue.Get<FVector2D>();

	FRotator controlRotation = PlayerCharacter->GetControlRotation();
	controlRotation.Yaw += value.X;
	controlRotation.Pitch = FMath::Clamp(controlRotation.Pitch + value.Y, -89.f, 89.f);

	FRotator actorRotation = PlayerCharacter->GetActorRotation();
	FRotator aimRotation = (controlRotation - actorRotation).GetNormalized();
	aimRotation.Roll = 0.f;

	if (PlayerCharacter->IsLocallyControlled())
	{
		USkeletalMeshComponent* meshComp = PlayerCharacter->GetMesh();
		if (!IsValid(meshComp))
			return;

		UCustomAnimInstance* animInstance = Cast<UCustomAnimInstance>(meshComp->GetAnimInstance());
		if (!IsValid(animInstance))
			return;

		APlayerController* controller = Cast<APlayerController>(PlayerCharacter->GetController());
		if (!IsValid(controller))
			return;

		controller->SetControlRotation(controlRotation);
		animInstance->SyncAim(aimRotation);
	}

	PlayerCharacter->OnReq_SyncAimRotation({aimRotation.Yaw, aimRotation.Pitch});
}
