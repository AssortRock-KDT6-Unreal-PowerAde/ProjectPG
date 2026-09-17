// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "GameFramework/Character.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "CustomCharacter.generated.h"

UCLASS()
class PROJECTPG_API ACustomCharacter : public ACharacter, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	ACustomCharacter(const FObjectInitializer& ObjectInitializer);

public:
	UPROPERTY(BlueprintReadOnly, Replicated, Category=Character)
	uint8 bIsAiming : 1 = false;
	
protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Abilities")
	TObjectPtr<UCharacterAttributeSet> CharacterAttributeSet;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities|Stamina",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float StaminaRecoveryMultiplier = 25.f;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void EquipItem(const FString& SocketName, UObject* Item);

	void SetAiming(bool bNewAiming);

	UFUNCTION(Server, Reliable)
	void OnReq_SetAiming(bool bNewAiming);

	class UCustomCharacterMovementComponent* GetCustomCharacterMovement() const;

	bool CanSprintInCurrentState() const;
	bool IsAiming() const;

	UCharacterAttributeSet* GetCharacterAttributeSet() const;

protected:
	virtual void BeginPlay() override;
};
