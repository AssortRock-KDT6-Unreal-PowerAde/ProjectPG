// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "EngineMinimal.h"
#include "Animation/AnimInstance.h"
#include "Common/GameData.h"
#include "CustomAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UCustomAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UCustomAnimInstance();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector2D AimOffset;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float NormalizedGroundSpeed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float MovementDirection;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsAiming : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsCrouched : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bWasJumping : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsFalling : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsSprinting : 1;

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	void SyncAim(FRotator rotation);
	void SyncAim(float Yaw, float Pitch);
};
