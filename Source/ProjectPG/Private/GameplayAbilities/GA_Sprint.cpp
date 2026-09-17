// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/GA_Sprint.h"

#include "Characters/CustomCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool UGA_Sprint::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                    const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                    FGameplayTagContainer* OptionalRelevantTags) const
{
	return Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
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

	ACustomCharacter* character = Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(character))
		return;

	UCharacterMovementComponent* movementComp = character->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	UCharacterAttributeSet* attributeSet = character->GetCharacterAttributeSet();
	if (!IsValid(attributeSet))
		return;

	movementComp->MaxWalkSpeed = attributeSet->GetSprintSpeed();
}

void UGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                            const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility,
                            bool bWasCancelled)
{
	ACustomCharacter* character = Cast<ACustomCharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(character))
		return;

	UCharacterMovementComponent* movementComp = character->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	UCharacterAttributeSet* attributeSet = character->GetCharacterAttributeSet();
	if (!IsValid(attributeSet))
		return;

	movementComp->MaxWalkSpeed = attributeSet->GetWalkSpeed();

	Super::EndAbility(
		Handle,
		ActorInfo,
		ActivationInfo,
		bReplicateEndAbility,
		bWasCancelled);
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
