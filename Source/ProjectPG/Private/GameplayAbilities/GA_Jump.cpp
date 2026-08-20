// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/GA_Jump.h"

#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

bool UGA_Jump::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
                                  const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
                                  FGameplayTagContainer* OptionalRelevantTags) const
{
	ACharacter* character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(character))
		return false;

	UCharacterMovementComponent* movementComp = character->GetCharacterMovement();
	if (!IsValid(character))
		return false;

	return movementComp->IsWalking();
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

	ACharacter* character = Cast<ACharacter>(ActorInfo->AvatarActor.Get());
	if (!IsValid(character))
		return;

	character->Jump();

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
