// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/CustomAbilitySystemComponent.h"

void UCustomAbilitySystemComponent::AbilityInputPressed(FGameplayTag AbilityGameplayTag)
{
	if (!AbilityGameplayTag.IsValid())
		return;

	for (FGameplayAbilitySpec& abilitySpec : GetActivatableAbilities())
	{
		FGameplayTagContainer& tagContainer = abilitySpec.GetDynamicSpecSourceTags();
		if (!tagContainer.HasTagExact(AbilityGameplayTag))
			continue;

		abilitySpec.InputPressed = true;

		if (!abilitySpec.IsActive())
			TryActivateAbility(abilitySpec.Handle);
	}
}

void UCustomAbilitySystemComponent::AbilityInputReleased(FGameplayTag AbilityGameplayTag)
{
	if (!AbilityGameplayTag.IsValid())
		return;

	for (FGameplayAbilitySpec& abilitySpec : GetActivatableAbilities())
	{
		FGameplayTagContainer& tagContainer = abilitySpec.GetDynamicSpecSourceTags();
		if (!tagContainer.HasTagExact(AbilityGameplayTag))
			continue;

		abilitySpec.InputPressed = false;

		if (abilitySpec.IsActive())
			AbilitySpecInputReleased(abilitySpec);
	}
}
