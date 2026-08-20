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
	TObjectPtr<class UDefaultInput> DefaultInput;

public:
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	
	UCustomAbilitySystemComponent* GetCustomAbilitySystemComponent() const;

	void MoveAction(const FInputActionValue& InputActionValue);
	void LookAction(const FInputActionValue& InputActionValue);
	void JumpAction(const FInputActionValue& InputActionValue);
	void CrouchAction(const FInputActionValue& InputActionValue);
	void InteractionAction(const FInputActionValue& InputActionValue);
	void FireAction(const FInputActionValue& InputActionValue);
	void ReloadAction(const FInputActionValue& InputActionValue);

protected:
	virtual void BeginPlay() override;
};
