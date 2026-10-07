// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InteractComponent.generated.h"


UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class PROJECTPG_API UInteractComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteractComponent();

public:
	UPROPERTY(EditAnywhere, Category = "Interaction")
	float InteractDistance = 200.f;

	UPROPERTY(EditAnywhere, Category = "Interaction")
	float SphereRadius = 50.f;
protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<AActor> CurrentTarget;
public:



	void BeginInteract(class AActor* Target);

	void EndInteract();

	class AActor* FindInteractTarget();
	void Interact();

private:

	void LineTrace();

	void SphereTrace();
		
	
};
