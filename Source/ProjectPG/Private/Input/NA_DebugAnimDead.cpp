// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_DebugAnimDead.h"

#include "Animation/AnimMontage.h"
#include "Characters/CustomPlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

UNA_DebugAnimDead::UNA_DebugAnimDead()
{
	static ConstructorHelpers::FObjectFinder<UAnimMontage> HipAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Death_Hip.AM_Death_Hip"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> IronsightsAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Death_Ironsights.AM_Death_Ironsights"));

	HipMontage = HipAsset.Object;
	IronsightsMontage = IronsightsAsset.Object;
}

bool UNA_DebugAnimDead::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Started;
}

void UNA_DebugAnimDead::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;

	UAnimMontage* Montage = PlayerCharacter->IsAiming() ? IronsightsMontage.Get() : HipMontage.Get();
	if (IsValid(Montage))
		PlayerCharacter->PlayMontage(Montage);
}

