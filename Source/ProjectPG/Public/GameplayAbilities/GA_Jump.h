// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CustomGameplayAbility.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GA_Jump.generated.h"

class UAbilityTask_WaitMovementModeChange;

/**
 * 
 */
UCLASS()
class PROJECTPG_API UGA_Jump : public UCustomGameplayAbility
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Jump",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float JumpStaminaCost = 20.f;

	UPROPERTY()
	TObjectPtr<UAbilityTask_WaitMovementModeChange> WaitLandingTask;

	bool bJumpStateApplied = false;

public:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void OnLanded(EMovementMode NewMovementMode);

	void SetJumpState(bool bJumpActive);
};
