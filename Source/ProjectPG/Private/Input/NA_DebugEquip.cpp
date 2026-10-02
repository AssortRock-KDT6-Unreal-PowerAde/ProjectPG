// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_DebugEquip.h"

#include "Characters/CustomPlayerCharacter.h"

UNA_DebugEquip::UNA_DebugEquip()
{
	static ConstructorHelpers::FObjectFinder<UAnimMontage> MontageAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Equip_Rifle.AM_Equip_Rifle"));
	
	Montage = MontageAsset.Object;
}

bool UNA_DebugEquip::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Started;
}

void UNA_DebugEquip::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;

	UAnimMontage* montage = Montage.Get();
	if (IsValid(montage))
		PlayerCharacter->PlayMontage(montage);
}
