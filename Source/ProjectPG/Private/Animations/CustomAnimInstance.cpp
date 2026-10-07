// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "AbilitySystemComponent.h"
#include "Characters/CustomCharacter.h"
#include "Characters/CustomPlayerCharacter.h"
#include "CustomGameplayTags.h"
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
	bIsAiming = character->IsAiming();

	if (character->IsLocallyControlled())
	{
		FRotator AimRotation = (character->GetControlRotation() - character->GetActorRotation()).GetNormalized();
		AimRotation.Roll = 0.f;
		SyncAim(AimRotation);
		character->OnReq_SyncAimRotation({AimRotation.Yaw, AimRotation.Pitch});
	}

	const UAbilitySystemComponent* abilitySystemComp = character->GetAbilitySystemComponent();
	bIsSprinting = IsValid(abilitySystemComp)
		&& abilitySystemComp->HasMatchingGameplayTag(CustomGameplayTags::State_Sprinting);

	UCharacterMovementComponent* movementComp = character->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	bWasJumping = character->bWasJumping;
	bIsFalling = movementComp->IsFalling() && !character->bWasJumping;

	if (movementComp->IsWalking())
	{
		FRotator rotation = character->GetActorRotation();
		FVector velocity = rotation.UnrotateVector(movementComp->Velocity);
		velocity.Z = 0;

		MovementDirection = velocity.Rotation().Yaw;

		// Keep the BlendSpace scale continuous when crouch/aim/sprint changes.
		const float MaxSpeed = movementComp->MaxWalkSpeed;
		NormalizedGroundSpeed = MaxSpeed > UE_KINDA_SMALL_NUMBER
			                        ? velocity.Size() / MaxSpeed
			                        : 0.f;
	}
}

void UCustomAnimInstance::SyncAim(FRotator rotation)
{
	AimOffset.X = rotation.Yaw;
	AimOffset.Y = rotation.Pitch;
}

void UCustomAnimInstance::SyncAim(float Yaw, float Pitch)
{
	AimOffset.X = Yaw;
	AimOffset.Y = Pitch;
}
