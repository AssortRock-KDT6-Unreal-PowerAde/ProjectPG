// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "AbilitySystemComponent.h"
#include "Characters/CustomCharacter.h"
#include "Characters/CustomPlayerCharacter.h"
#include "CustomGameplayTags.h"
#include "Equipments/Equipment.h"

UCustomAnimInstance::UCustomAnimInstance()
{
}

void UCustomAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	LeftHandIKTransform = FTransform::Identity;
	LeftHandIKAlpha = 0.f;
	// ABP가 장착 이후 교체되어도 현재 캐릭터의 IK 장비를 다시 찾습니다.
	if (!LeftHandIKEquipment.IsValid())
	{
		if (AActor* Owner = GetOwningActor())
		{
			TArray<AActor*> AttachedActors;
			Owner->GetAttachedActors(AttachedActors);
			for (AActor* Actor : AttachedActors)
			{
				AEquipment* Equipment = Cast<AEquipment>(Actor);
				if (Equipment && Equipment->GetLeftHandIKTarget(LeftHandIKTransform, LeftHandIKAlpha, LeftHandIKReferenceBone))
				{
					LeftHandIKEquipment = Equipment;
					break;
				}
			}
		}
	}
	if (LeftHandIKEquipment.IsValid())
		LeftHandIKEquipment->GetLeftHandIKTarget(LeftHandIKTransform, LeftHandIKAlpha, LeftHandIKReferenceBone);

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
	// 질주와 몽타주에서는 원본 애니메이션의 왼손 동작을 유지합니다.
	if (bIsSprinting || Montage_IsPlaying(nullptr))
		LeftHandIKAlpha = 0.f;

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

void UCustomAnimInstance::SetLeftHandIKEquipment(AEquipment* Equipment)
{
	LeftHandIKEquipment = Equipment;
}

void UCustomAnimInstance::ClearLeftHandIKEquipment(const AEquipment* Equipment)
{
	if (LeftHandIKEquipment.Get() == Equipment)
	{
		LeftHandIKEquipment.Reset();
		LeftHandIKTransform = FTransform::Identity;
		LeftHandIKAlpha = 0.f;
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
