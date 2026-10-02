// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/GA_Sprint.h"

#include "AbilitySystemComponent.h"
#include "Characters/CustomCharacter.h"
#include "Characters/CustomCharacterMovementComponent.h"
#include "CustomGameplayTags.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "GameplayAbilities/Tasks/AbilityTask_AbilityTick.h"

bool UGA_Sprint::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                    FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
		return false;
	if (!ActorInfo)
		return false;

	const ACustomCharacter* Character = Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(Character))
		return false;

	const UCharacterAttributeSet* AttributeSet = Character->GetCharacterAttributeSet();
	if (!IsValid(AttributeSet) || AttributeSet->GetStamina() <= 0.f)
		return false;

	const auto* Movement = Character->GetCustomCharacterMovement();
	return IsValid(Movement) && Movement->IsSprintEnabled();
}

void UGA_Sprint::InputReleased(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo)
{
	Super::InputReleased(Handle, ActorInfo, ActivationInfo);

	EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		true,
		false);
}

void UGA_Sprint::ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                 const FGameplayAbilityActivationInfo ActivationInfo,
                                 const FGameplayEventData* TriggerEventData)
{
	if (!CommitAbility(Handle, ActorInfo, ActivationInfo))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	ACustomCharacter* Character = Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(Character))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	UCustomCharacterMovementComponent* MovementComponent = Character->GetCustomCharacterMovement();
	if (!IsValid(MovementComponent))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	MovementComponent->bWantsToSprint = true;

	SprintTickTask = UAbilityTask_AbilityTick::CreateAbilityTickTask(this);
	if (!IsValid(SprintTickTask))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	SprintTickTask->OnTick.AddDynamic(this, &UGA_Sprint::HandleSprintTick);
	SprintTickTask->ReadyForActivation();
	HandleSprintTick(0.f);
}

void UGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled)
{
	SetSprintState(false);

	if (IsValid(SprintTickTask))
	{
		SprintTickTask->EndTask();
		SprintTickTask = nullptr;
	}

	ACustomCharacter* Character = ActorInfo
		? Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	if (IsValid(Character))
	{
		UCustomCharacterMovementComponent* MovementComponent = Character->GetCustomCharacterMovement();
		if (IsValid(MovementComponent))
			MovementComponent->bWantsToSprint = false;
	}

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
}

void UGA_Sprint::HandleSprintTick(float DeltaTime)
{
	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	ACustomCharacter* Character = ActorInfo
		? Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get())
		: nullptr;
	if (!IsValid(Character))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}

	UCustomCharacterMovementComponent* MovementComponent = Character->GetCustomCharacterMovement();
	UCharacterAttributeSet* AttributeSet = Character->GetCharacterAttributeSet();
	if (!IsValid(MovementComponent) || !IsValid(AttributeSet))
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, true);
		return;
	}

	if (AttributeSet->GetStamina() <= 0.f)
	{
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, false);
		return;
	}

	// Keep the held sprint request while crouched, but stop its state and cost.
	const bool bSprintActive = !MovementComponent->bWantsToCrouch
		&& MovementComponent->IsSprinting();
	SetSprintState(bSprintActive);

	if (!bSprintActive || !Character->HasAuthority())
		return;

	const float StaminaCost = FMath::Max(0.f, DeltaTime) * StaminaCostMultiplier;
	const float NewStamina = FMath::Max(0.f, AttributeSet->GetStamina() - StaminaCost);
	AttributeSet->SetStamina(NewStamina);

	if (NewStamina <= 0.f)
		EndAbility(GetCurrentAbilitySpecHandle(), ActorInfo, GetCurrentActivationInfo(), true, false);
}

void UGA_Sprint::SetSprintState(bool bSprintActive)
{
	if (bSprintStateApplied == bSprintActive)
		return;

	const FGameplayAbilityActorInfo* ActorInfo = GetCurrentActorInfo();
	UAbilitySystemComponent* AbilitySystemComponent = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	if (!IsValid(AbilitySystemComponent))
		return;

	bSprintStateApplied = bSprintActive;

	AbilitySystemComponent->SetLooseGameplayTagCount(
		CustomGameplayTags::State_Sprinting,
		bSprintActive ? 1 : 0);
	if (bSprintActive)
		AbilitySystemComponent->AddLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
	else
		AbilitySystemComponent->RemoveLooseGameplayTag(CustomGameplayTags::State_UsingStamina);

	const AActor* AvatarActor = ActorInfo->AvatarActor.Get();
	if (IsValid(AvatarActor) && AvatarActor->HasAuthority())
	{
		AbilitySystemComponent->SetReplicatedLooseGameplayTagCount(
			CustomGameplayTags::State_Sprinting,
			bSprintActive ? 1 : 0);

		if (bSprintActive)
			AbilitySystemComponent->AddReplicatedLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
		else
			AbilitySystemComponent->RemoveReplicatedLooseGameplayTag(CustomGameplayTags::State_UsingStamina);
	}
}
