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
	ACustomCharacter();

protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UCharacterAttributeSet> CharacterAttributeSet;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;

	virtual void EquipItem(const FString& SocketName, UObject* Item);

	UCharacterAttributeSet* GetCharacterAttributeSet() const;
	
protected:
	virtual void BeginPlay() override;
};
