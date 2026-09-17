// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "EngineMinimal.h"
#include "Animation/AnimInstance.h"
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
	FVector2D Aim;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Speed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float Direction;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bIsIronSighted;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bIsCrouched;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bIsProne;

public:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
