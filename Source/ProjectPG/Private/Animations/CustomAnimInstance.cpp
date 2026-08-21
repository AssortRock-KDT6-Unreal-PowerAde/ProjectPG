// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "Characters/CustomCharacter.h"

UCustomAnimInstance::UCustomAnimInstance()
{
}

void UCustomAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	ACustomCharacter* owner = Cast<ACustomCharacter>(TryGetPawnOwner());
	if (!IsValid(owner))
		return;

	UCharacterMovementComponent* movementComp = owner->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (!movementComp->IsWalking())
		return;

	FVector velocity = movementComp->Velocity;
	velocity.Z = 0;

	UCharacterAttributeSet* characterAttributeSet = owner->GetCharacterAttributeSet();
	if (nullptr == characterAttributeSet)
		return;

	float walkSpeed = characterAttributeSet->GetWalkSpeed();

	if (velocity.Size() < walkSpeed)
		Speed = FMath::Lerp(0.f, 0.5f, velocity.Size() / walkSpeed);
	else
		Speed = FMath::Lerp(0.5f, 1.f,
		                    (velocity.Size() - walkSpeed) / (characterAttributeSet->GetSprintSpeed() - walkSpeed));

	GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, FString::Printf(TEXT("%f"), Speed));
}
