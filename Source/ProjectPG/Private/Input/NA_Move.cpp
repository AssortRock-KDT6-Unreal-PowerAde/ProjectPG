// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_Move.h"

#include "Animations/CustomAnimInstance.h"
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
	if (!IsValid(PlayerCharacter))
		return;

	APlayerController* controller = Cast<APlayerController>(PlayerCharacter->GetController());
	if (!IsValid(controller))
		return;

	USpringArmComponent* cameraArmComp = PlayerCharacter->GetCameraArm();
	if (!IsValid(cameraArmComp))
		return;

	FVector2D value = InputActionValue.Get<FVector2D>();
	value = value.GetClampedToMaxSize(1.0f);

	FRotator controlRotation = PlayerCharacter->GetControlRotation();
	FRotator actorRotation = FRotator(0, controlRotation.Yaw, 0);

	USkeletalMeshComponent* mesh = PlayerCharacter->GetMesh();
	if (!IsValid(mesh))
		return;

	FRotator aimRotation = (controlRotation - actorRotation).GetNormalized();
	aimRotation.Roll = 0.f;

	if (PlayerCharacter->IsLocallyControlled())
	{
		UCustomAnimInstance* animInstance = Cast<UCustomAnimInstance>(mesh->GetAnimInstance());
		if (!IsValid(animInstance))
			return;

		PlayerCharacter->SetActorRotation(actorRotation);
		animInstance->SyncAim(aimRotation.Yaw, aimRotation.Pitch);

		UCharacterMovementComponent* movementComp = PlayerCharacter->GetCharacterMovement();
		if (!IsValid(movementComp))
			return;

		FVector inputVector = FVector(value.X, value.Y, 0);
		inputVector = actorRotation.RotateVector(inputVector);
		movementComp->AddInputVector(inputVector);
	}

	PlayerCharacter->OnReq_SyncCharacterRotation({aimRotation.Yaw, aimRotation.Pitch}, actorRotation);
}
