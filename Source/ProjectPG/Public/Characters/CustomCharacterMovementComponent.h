// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CustomCharacterMovementComponent.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UCustomCharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite,
		meta=(ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float MaxWalkSpeedProne;

	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite)
	bool bCanWalkOffLedgesWhenProne;

	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadOnly)
	bool bWantsToEnterProne;

	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadWrite,
		AdvancedDisplay)
	bool bProneMaintainsBaseLocation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=MovementProperties)
	bool bCanEverEnterProne;

private:
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere,
		BlueprintSetter=SetProneHalfHeight, BlueprintGetter=GetProneHalfHeight,
		meta=(ClampMin="0", UIMin="0", ForceUnits=cm))
	float ProneHalfHeight;

public:
	FORCEINLINE virtual bool CanEverEnterProne() const { return bCanEverEnterProne; }
	virtual bool IsProne() const;
	virtual void EnterProne(bool bClientSimulation = false);
	virtual void ExitProne(bool bClientSimulation = false);
	virtual bool CanEnterProneInCurrentState() const;

	virtual float GetMaxSpeed() const override;

	UFUNCTION(BlueprintSetter)
	void SetProneHalfHeight(const float NewValue);
	UFUNCTION(BlueprintGetter)
	float GetProneHalfHeight() const;
};
