// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "Characters/CustomCharacter.h"
#include "Characters/CustomPlayerCharacter.h"
#include "Net/UnrealNetwork.h"

UCustomAnimInstance::UCustomAnimInstance()
{
}

void UCustomAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	ACustomPlayerCharacter* character = Cast<ACustomPlayerCharacter>(TryGetPawnOwner());
	if (!IsValid(character))
		return;

	bIsCrouched = character->IsCrouched();

	USpringArmComponent* cameraArm = character->GetCameraArm();
	if (!IsValid(cameraArm))
		return;

	UCharacterMovementComponent* movementComp = character->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (!movementComp->IsWalking())
		return;

	UCharacterAttributeSet* characterAttributeSet = character->GetCharacterAttributeSet();
	if (nullptr == characterAttributeSet)
		return;

	FRotator rotation = character->GetActorRotation();
	FVector velocity = rotation.UnrotateVector(movementComp->Velocity);
	velocity.Z = 0;

	Direction = velocity.Rotation().Yaw;
	Speed = velocity.Size() / characterAttributeSet->GetWalkSpeed();
}

void UCustomAnimInstance::SyncAim(FRotator rotation)
{
	Aim.X = rotation.Yaw;
	Aim.Y = rotation.Pitch;
}

void UCustomAnimInstance::SyncAim(float Yaw, float Pitch)
{
	Aim.X = Yaw;
	Aim.Y = Pitch;
}
