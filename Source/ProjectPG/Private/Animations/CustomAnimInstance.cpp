// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "Characters/CustomCharacter.h"
#include "Characters/CustomPlayerCharacter.h"

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

	FRotator cameraRotation = cameraArm->GetRelativeRotation();
	Aim.X = cameraRotation.Yaw;
	Aim.Y = cameraRotation.Pitch;

	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, FString::Printf(TEXT("%f"), Speed));
}
