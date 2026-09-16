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
	uint8 bIsIronsight : 1 = false;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_IsProne, Category=Character)
	uint8 bIsProne : 1 = false;
	
protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Replicated, Category = "Abilities")
	TObjectPtr<UCharacterAttributeSet> CharacterAttributeSet;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Camera)
	float ProneEyeHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	uint8 bCanProne : 1;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	virtual void RecalculateBaseEyeHeight() override;
	virtual bool CanEnterProne() const;
	virtual void OnEndProne(float HalfHeightAdjust, float ScaledHalfHeightAdjust); // TODO? : K2 메소드 구현 여부
	virtual void OnStartProne(float HalfHeightAdjust, float ScaledHalfHeightAdjust); // TODO? : K2 메소드 구현 여부
	virtual void EquipItem(const FString& SocketName, UObject* Item);

	UFUNCTION(BlueprintCallable, Category=Character, meta=(HidePin="bClientSimulation"))
	virtual void EnterProne(bool bClientSimulation = false);
	UFUNCTION(BlueprintCallable, Category=Character, meta=(HidePin="bClientSimulation"))
	virtual void ExitProne(bool bClientSimulation = false);

	UFUNCTION(Server, Reliable)
	virtual void OnReq_SetIronsight(bool IsIronsight);
	UFUNCTION()
	virtual void OnRep_IsProne();

	class UCustomCharacterMovementComponent* GetCustomCharacterMovement() const;
	bool IsProne() const;
	bool IsIronsight() const;
	void SetIsProne(const bool bInIsProne);
	void RecalculateProneEyeHeight();

	UCharacterAttributeSet* GetCharacterAttributeSet() const;

protected:
	virtual void BeginPlay() override;
};
