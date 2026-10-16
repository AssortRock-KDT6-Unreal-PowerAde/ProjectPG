// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/CharacterAttributeSet.h"

#include "Net/UnrealNetwork.h"

UCharacterAttributeSet::UCharacterAttributeSet()
{
}

void UCharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
}

void UCharacterAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (OldValue == NewValue) return;
	if (Attribute == GetHealthAttribute() || Attribute == GetMaxHealthAttribute())
		OnHealthChanged.Broadcast(GetHealth(), GetMaxHealth());
	if (Attribute == GetStaminaAttribute() || Attribute == GetMaxStaminaAttribute())
		OnStaminaChanged.Broadcast(GetStamina(), GetMaxStamina());
}

void UCharacterAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, Health, OldValue);
	if (OldValue.GetCurrentValue() != GetHealth()) OnHealthChanged.Broadcast(GetHealth(), GetMaxHealth());
}
void UCharacterAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, MaxHealth, OldValue);
	if (OldValue.GetCurrentValue() != GetMaxHealth()) OnHealthChanged.Broadcast(GetHealth(), GetMaxHealth());
}
void UCharacterAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, Stamina, OldValue);
	if (OldValue.GetCurrentValue() != GetStamina()) OnStaminaChanged.Broadcast(GetStamina(), GetMaxStamina());
}
void UCharacterAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, MaxStamina, OldValue);
	if (OldValue.GetCurrentValue() != GetMaxStamina()) OnStaminaChanged.Broadcast(GetStamina(), GetMaxStamina());
}
