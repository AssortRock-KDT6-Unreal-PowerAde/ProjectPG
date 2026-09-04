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
	uint8 bCanWalkOffLedgesWhenProne : 1;

	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadOnly)
	uint8 bWantsToEnterProne : 1;

	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadWrite,
		AdvancedDisplay)
	uint8 bProneMaintainsBaseLocation : 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=MovementProperties)
	uint8 bCanEverEnterProne : 1;

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
