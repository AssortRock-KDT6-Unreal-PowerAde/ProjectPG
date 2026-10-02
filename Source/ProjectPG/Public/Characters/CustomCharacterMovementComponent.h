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
	float MaxAimWalkSpeed = 150.f;

	UPROPERTY(Category="Character Movement: Walking", EditAnywhere, BlueprintReadWrite,
		meta=(ClampMin="0", UIMin="0", ForceUnits="cm/s"))
	float MaxSprintSpeed = 600.f;

	// Sprinting 인정 범위
	// 1.0 : 정확한 전방
	// 0.7 : 전방 약 45도까지
	// 0.0 : 측면까지
	// -1.0 : 후방까지
	UPROPERTY(Category="Character Movement (General Settings)", EditAnywhere, BlueprintReadWrite,
		meta=(ClampMin="-1.0", ClampMax="1.0", UIMin="-1.0", UIMax="1.0"))
	float MinSprintForwardInputDot = 0.7f;

	UPROPERTY(Category="Character Movement (General Settings)", VisibleInstanceOnly, BlueprintReadOnly)
	uint8 bWantsToSprint : 1 = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=MovementProperties)
	uint8 bSprintEnabled : 1 = true;

public:
	UCustomCharacterMovementComponent();
	bool bWantsToAim = false;
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	virtual void PhysicsRotation(float DeltaTime) override;

	FORCEINLINE virtual bool IsSprintEnabled() const { return bSprintEnabled; }
	virtual bool IsSprinting() const;
	virtual bool IsAiming() const;
	virtual bool HasForwardMovementInput() const;
	virtual bool CanSprintInCurrentState() const;

	virtual float GetMaxSpeed() const override;
};
