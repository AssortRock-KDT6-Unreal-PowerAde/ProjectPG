// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
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
	ACustomPlayerCharacter(const FObjectInitializer& ObjectInitializer);

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UCameraComponent> CameraComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class USpringArmComponent> CameraArmComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UNativeActionComponent> NativeActionComp;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UInteractComponent> InteractComp;
private:
	UPROPERTY(Transient)
	TObjectPtr<class UCharacterVitalsWidget> VitalsWidget;
public:
	virtual void PawnClientRestart() override;
	virtual void UnPossessed() override;
	virtual void OnRep_Controller() override;
	virtual void OnRep_PlayerState() override;
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UCustomAbilitySystemComponent* GetCustomAbilitySystemComponent() const;
	USpringArmComponent* GetCameraArm() const;
	void Interact();
	virtual void GetCombatViewPoint(FVector& Location, FRotator& Rotation) const override;

	UFUNCTION(Server, Unreliable)
	void OnReq_SyncAimRotation(FVector2D AimDirection);
	UFUNCTION(NetMulticast, Unreliable)
	void OnRep_SyncAimRotation(FVector2D AimDirection);

	UFUNCTION(Server, Unreliable)
	void OnReq_SyncCharacterRotation(FVector2D AimDirection, FRotator ActorRotation);
	UFUNCTION(NetMulticast, Unreliable)
	void OnRep_SyncCharacterRotation(FVector2D AimDirection);

protected:
	virtual void OnRep_CharacterAttributeSet() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void RefreshVitalsWidget();
};
