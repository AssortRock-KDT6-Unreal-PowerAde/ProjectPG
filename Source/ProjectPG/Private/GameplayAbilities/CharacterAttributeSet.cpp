// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/CharacterAttributeSet.h"

#include "Net/UnrealNetwork.h"

UCharacterAttributeSet::UCharacterAttributeSet()
{
}

void UCharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UCharacterAttributeSet, Health);
	DOREPLIFETIME(UCharacterAttributeSet, MaxHealth);
	DOREPLIFETIME(UCharacterAttributeSet, Stamina);
	DOREPLIFETIME(UCharacterAttributeSet, MaxStamina);
	DOREPLIFETIME(UCharacterAttributeSet, WalkSpeed);
	DOREPLIFETIME(UCharacterAttributeSet, SprintSpeed);
}
