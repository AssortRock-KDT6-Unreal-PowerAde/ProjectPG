// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/GA_Jump.h"

#include "Abilities/Tasks/AbilityTask_WaitMovementModeChange.h"
#include "AbilitySystemComponent.h"
#include "Characters/CustomCharacter.h"
#include "CustomGameplayTags.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameplayAbilities/CharacterAttributeSet.h"

bool UGA_Jump::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                  const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                  FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
		return false;

	if (!ActorInfo)
		return false;

	const ACustomCharacter* Character = Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(Character) || !Character->CanJump())
		return false;

	const UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement();
	if (!IsValid(MovementComponent) || !MovementComponent->IsMovingOnGround())
		return false;

	const UCharacterAttributeSet* AttributeSet = Character->GetCharacterAttributeSet();
	return IsValid(AttributeSet) && AttributeSet->GetStamina() >= JumpStaminaCost;
}

void UGA_Jump::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo,
                               const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACustomCharacter* Character = ActorInfo
		? Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	if (!IsValid(Character))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UCharacterAttributeSet* AttributeSet = Character->GetCharacterAttributeSet();
	if (!IsValid(AttributeSet) || AttributeSet->GetStamina() < JumpStaminaCost)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	WaitLandingTask = UAbilityTask_WaitMovementModeChange::CreateWaitMovementModeChange(this, MOVE_Walking);
	if (!IsValid(WaitLandingTask))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	WaitLandingTask->OnChange.AddDynamic(this, &UGA_Jump::OnLanded);
	WaitLandingTask->ReadyForActivation();

	SetJumpState(true);

	if (Character->HasAuthority())
	{
		AttributeSet->SetStamina(
			FMath::Max(0.f, AttributeSet->GetStamina() - JumpStaminaCost));
	}

	Character->Jump();
}

void UGA_Jump::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	SetJumpState(false);

	if (IsValid(WaitLandingTask))
	{
		WaitLandingTask->EndTask();
		WaitLandingTask = nullptr;
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}

void UGA_Jump::OnLanded(EMovementMode NewMovementMode)
{
	EndAbility(
		GetCurrentAbilitySpecHandle(),
		GetCurrentActorInfo(),
		GetCurrentActivationInfo(),
		true,
		false);
}

void UGA_Jump::SetJumpState(bool bJumpActive)
{
	if (bJumpStateApplied == bJumpActive)
		return;

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAbilitySystemComponent* AbilitySystemComponent = ActorInfo
		? ActorInfo->AbilitySystemComponent.Get()
		: nullptr;
	if (!IsValid(AbilitySystemComponent))
		return;

	bJumpStateApplied = bJumpActive;

	if (bJumpActive)
	{
		AbilitySystemComponent->AddLooseGameplayTag(CustomGameplayTags::State_Jumping);
		AbilitySystemComponent->AddLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
	}
	else
	{
		AbilitySystemComponent->RemoveLooseGameplayTag(CustomGameplayTags::State_Jumping);
		AbilitySystemComponent->RemoveLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
	}

	const AActor* AvatarActor = ActorInfo->AvatarActor.Get();
	if (!IsValid(AvatarActor) || !AvatarActor->HasAuthority())
		return;

	if (bJumpActive)
	{
		AbilitySystemComponent->AddReplicatedLooseGameplayTag(CustomGameplayTags::State_Jumping);
		AbilitySystemComponent->AddReplicatedLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
	}
	else
	{
		AbilitySystemComponent->RemoveReplicatedLooseGameplayTag(CustomGameplayTags::State_Jumping);
		AbilitySystemComponent->RemoveReplicatedLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
	}
}
