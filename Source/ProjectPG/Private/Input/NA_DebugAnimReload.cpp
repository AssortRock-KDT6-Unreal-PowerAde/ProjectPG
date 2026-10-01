// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_DebugAnimReload.h"

#include "Animation/AnimMontage.h"
#include "Characters/CustomPlayerCharacter.h"
#include "Common/GameData.h"
#include "CustomGameplayTags.h"

bool UNA_DebugAnimReload::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Started;
}

void UNA_DebugAnimReload::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;

	const FWeaponAnimationSet* AnimationSet = PlayerCharacter->GetCurrentWeaponAnimationSet();
	if (!AnimationSet)
		return;

	const FGameplayTag& MontageTag = PlayerCharacter->IsAiming()
		? CustomGameplayTags::WeaponAnimation_Reload_Ironsights
		: CustomGameplayTags::WeaponAnimation_Reload_Hip;
	UAnimMontage* Montage = AnimationSet->FindMontage(MontageTag);
	if (IsValid(Montage))
		PlayerCharacter->PlayMontage(Montage);
}

