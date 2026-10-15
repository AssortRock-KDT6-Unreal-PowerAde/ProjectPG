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
	if (const auto* Character = Cast<ACustomCharacter>(TryGetPawnOwner()); Character && Character->IsDead())
	{
		LeftHandIKAlpha = 0.f;
		AimOffset = FVector2D::ZeroVector;
		bIsAiming = bIsSprinting = bWasJumping = bIsFalling = false;
		NormalizedGroundSpeed = 0.f;
		return;
	}
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
	// 사격 반동에는 IK를 유지하고, 손을 떼어야 하는 동작에만 해제합니다.
	const FWeaponAnimationSet* AnimationSet = character->GetCurrentWeaponAnimationSet();
	const auto IsPlayingAction = [this, AnimationSet](const FGameplayTag& Tag)
	{
		UAnimMontage* Montage = AnimationSet ? AnimationSet->FindMontage(Tag) : nullptr;
		return Montage && Montage_IsPlaying(Montage);
	};
	if (bIsSprinting
		|| IsPlayingAction(CustomGameplayTags::WeaponAnimation_Reload_Hip)
		|| IsPlayingAction(CustomGameplayTags::WeaponAnimation_Reload_Ironsights)
		|| IsPlayingAction(CustomGameplayTags::WeaponAnimation_Equip)
		|| IsPlayingAction(CustomGameplayTags::WeaponAnimation_Death_Hip)
		|| IsPlayingAction(CustomGameplayTags::WeaponAnimation_Death_Ironsights))
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
