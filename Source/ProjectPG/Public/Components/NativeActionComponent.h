// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NativeActionComponent.generated.h"


class UNativeAction;

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class PROJECTPG_API UNativeActionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UNativeActionComponent();

protected:
	TArray<TObjectPtr<UNativeAction>> Actions;

public:
	UNativeAction* RegisterNativeAction(TSubclassOf<UNativeAction> NativeActionClass); 
};
