// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_DebugAnimFire.h"

#include "Animation/AnimMontage.h"
#include "Characters/CustomPlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

UNA_DebugAnimFire::UNA_DebugAnimFire()
{
	static ConstructorHelpers::FObjectFinder<UAnimMontage> HipAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Fire_Rifle_Hip.AM_Fire_Rifle_Hip"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> IronsightsAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Fire_Rifle_Ironsights.AM_Fire_Rifle_Ironsights"));

	HipMontage = HipAsset.Object;
	IronsightsMontage = IronsightsAsset.Object;
}

bool UNA_DebugAnimFire::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Started;
}

void UNA_DebugAnimFire::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;

	UAnimMontage* Montage = PlayerCharacter->IsAiming() ? IronsightsMontage.Get() : HipMontage.Get();
	if (IsValid(Montage))
		PlayerCharacter->PlayMontage(Montage);
}

