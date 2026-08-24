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

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UCustomAbilitySystemComponent* GetCustomAbilitySystemComponent() const;
	USpringArmComponent* GetCameraArm() const;
	
	UFUNCTION(Server, Unreliable)
	void OnReq_SyncAimRotation(FVector2D AimDirection);
	UFUNCTION(NetMulticast, Unreliable)
	void OnRep_SyncAimRotation(FVector2D AimDirection);
	
	UFUNCTION(Server, Unreliable)
	void OnReq_SyncCharacterRotation(FVector2D AimDirection, FRotator ActorRotation);
	UFUNCTION(NetMulticast, Unreliable)
	void OnRep_SyncCharacterRotation(FVector2D AimDirection);

protected:
	virtual void BeginPlay() override;
};
