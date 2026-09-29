// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_DebugAnimFire.h"

#include "Animation/AnimMontage.h"
#include "Characters/CustomPlayerCharacter.h"
#include "Common/GameData.h"

bool UNA_DebugAnimFire::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Started;
}

void UNA_DebugAnimFire::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;

	const FWeaponAnimationSet* AnimationSet = PlayerCharacter->GetCurrentWeaponAnimationSet();
	if (!AnimationSet)
		return;

	UAnimMontage* Montage = PlayerCharacter->IsAiming()
		? AnimationSet->FireIronsights.Get()
		: AnimationSet->FireHip.Get();
	if (IsValid(Montage))
		PlayerCharacter->PlayMontage(Montage);
}

