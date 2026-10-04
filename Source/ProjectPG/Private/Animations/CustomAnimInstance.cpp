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

	// (10/4) 매 프레임 속도를 화면에 찍던 디버그 줄 — 타이틀·게임 화면 왼쪽을 파란 숫자로 덮어서 껐다. 필요하면 다시 켜기.
	// GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, FString::Printf(TEXT("%f"), Speed));
}
