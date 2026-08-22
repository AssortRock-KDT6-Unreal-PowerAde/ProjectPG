// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/NativeActionComponent.h"

#include "Input/NativeAction.h"

// Sets default values for this component's properties
UNativeActionComponent::UNativeActionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UNativeAction* UNativeActionComponent::RegisterNativeAction(TSubclassOf<UNativeAction> NativeActionClass)
{
	UNativeAction* nativeAction = NewObject<UNativeAction>(this, NativeActionClass);
	Actions.Add(nativeAction);

	return nativeAction;
}
