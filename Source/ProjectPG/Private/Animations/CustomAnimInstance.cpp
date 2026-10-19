// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "AbilitySystemComponent.h"
#include "Characters/CustomCharacter.h"
#include "Characters/CustomPlayerCharacter.h"
#include "Characters/CustomEnemyCharacter.h"
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

	ACustomCharacter* character = Cast<ACustomCharacter>(TryGetPawnOwner());
	if (!IsValid(character))
		return;

	bIsCrouched = character->IsCrouched();
	bIsAiming = character->IsAiming();

	if (character->IsLocallyControlled())
	{
		FRotator AimRotation = (character->GetControlRotation() - character->GetActorRotation()).GetNormalized();
		AimRotation.Roll = 0.f;
		SyncAim(AimRotation);
		// [멀티 임시수정 2026-10-19 조준 방향 덜 자주]
		// 예전: 화면마다(초당 60번쯤) 서버로 보내고, 서버는 받을 때마다 모두에게 방송했다.
		// 지금: 0.1초에 한 번, 0.5도 넘게 바뀌었을 때만 보낸다.
		//   - 가만히 있어도 1초에 한 번은 다시 보낸다(이 요청은 빠질 수 있어서, 마지막 값이 빠지면 남들 화면에서 계속 틀어져 보이므로)
		if (auto* Player = Cast<ACustomPlayerCharacter>(character))
		{
			const FVector2D Aim(AimRotation.Yaw, AimRotation.Pitch);
			const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
			const double SinceLastSend = Now - LastAimSendTime;
			if (LastAimSendTime < 0.0 || SinceLastSend >= 1.0 || (SinceLastSend >= 0.1 && !Aim.Equals(LastSentAim, 0.5f)))
			{
				LastAimSendTime = Now;
				LastSentAim = Aim;
				Player->OnReq_SyncAimRotation(Aim);
			}
		}
		// [멀티 임시수정] 끝
	}
	// [멀티 임시수정 2026-10-19 조준 방향 덜 자주] 다른 사람 캐릭터는 받은 목표 쪽으로 부드럽게 따라간다(0.1초 간격을 메움).
	else if (bHasRemoteAimTarget)
	{
		AimOffset = FMath::Vector2DInterpTo(AimOffset, RemoteAimTarget, DeltaSeconds, 15.f);
	}
	// [멀티 임시수정] 끝
	if (const auto* Enemy = Cast<ACustomEnemyCharacter>(character))
		SyncAim(Enemy->GetEnemyAimOffset().X, Enemy->GetEnemyAimOffset().Y);

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

// [멀티 임시수정 2026-10-19 조준 방향 덜 자주] 받은 방향은 목표로만 적고, NativeUpdateAnimation 이 따라간다.
void UCustomAnimInstance::SetRemoteAimTarget(float Yaw, float Pitch)
{
	RemoteAimTarget = FVector2D(Yaw, Pitch);
	bHasRemoteAimTarget = true;
}
// [멀티 임시수정] 끝
