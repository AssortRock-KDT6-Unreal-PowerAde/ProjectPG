// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/Tasks/AbilityTask.h"
#include "AbilityTask_AbilityTick.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityTickDelegate, float, DeltaTime);

UCLASS()
class PROJECTPG_API UAbilityTask_AbilityTick : public UAbilityTask
{
	GENERATED_BODY()

public:
	UAbilityTask_AbilityTick();

	UPROPERTY(BlueprintAssignable)
	FAbilityTickDelegate OnTick;

	UFUNCTION(BlueprintCallable, Category="Ability|Tasks",
		meta=(DisplayName="Ability Tick", HidePin="OwningAbility", DefaultToSelf="OwningAbility",
		BlueprintInternalUseOnly="true"))
	static UAbilityTask_AbilityTick* CreateAbilityTickTask(UGameplayAbility* OwningAbility);

	virtual void TickTask(float DeltaTime) override;
};
