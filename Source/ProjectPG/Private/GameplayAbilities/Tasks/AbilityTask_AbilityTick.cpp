// Fill out your copyright notice in the Description page of Project Settings.

#include "GameplayAbilities/Tasks/AbilityTask_AbilityTick.h"

UAbilityTask_AbilityTick::UAbilityTask_AbilityTick()
{
	bTickingTask = true;
}

UAbilityTask_AbilityTick* UAbilityTask_AbilityTick::CreateAbilityTickTask(UGameplayAbility* OwningAbility)
{
	return NewAbilityTask<UAbilityTask_AbilityTick>(OwningAbility);
}

void UAbilityTask_AbilityTick::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);

	if (ShouldBroadcastAbilityTaskDelegates())
		OnTick.Broadcast(DeltaTime);
}
