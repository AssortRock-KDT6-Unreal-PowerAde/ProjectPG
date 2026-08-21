// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CustomGameplayTags.h"
#include "InputActionValue.h"
#include "Characters/CustomCharacter.h"
#include "CustomPlayerCharacter.generated.h"

class UCustomAbilitySystemComponent;
/**
 * 
 */
UCLASS()
class PROJECTPG_API ACustomPlayerCharacter : public ACustomCharacter
{
	GENERATED_BODY()

public:
	ACustomPlayerCharacter();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UCameraComponent> CameraComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class USpringArmComponent> CameraArmComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UNativeActionComponent> NativeActionComp;

public:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UCustomAbilitySystemComponent* GetCustomAbilitySystemComponent() const;
	USpringArmComponent* GetCameraArm() const;

protected:
	virtual void BeginPlay() override;
};
