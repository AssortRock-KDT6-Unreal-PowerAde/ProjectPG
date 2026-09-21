// Fill out your copyright notice in the Description page of Project Settings.


#include "Input/NA_DebugAnimReload.h"

#include "Animation/AnimMontage.h"
#include "Characters/CustomPlayerCharacter.h"
#include "UObject/ConstructorHelpers.h"

UNA_DebugAnimReload::UNA_DebugAnimReload()
{
	static ConstructorHelpers::FObjectFinder<UAnimMontage> HipAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Reload_Rifle_Hip.AM_Reload_Rifle_Hip"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> IronsightsAsset(
		TEXT("/Game/PG/Animations/Montages/AM_Reload_Rifle_Ironsights.AM_Reload_Rifle_Ironsights"));

	HipMontage = HipAsset.Object;
	IronsightsMontage = IronsightsAsset.Object;
}

bool UNA_DebugAnimReload::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	return TriggerEvent == ETriggerEvent::Started;
}

void UNA_DebugAnimReload::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter) || !PlayerCharacter->IsLocallyControlled())
		return;

	UAnimMontage* Montage = PlayerCharacter->IsAiming() ? IronsightsMontage.Get() : HipMontage.Get();
	if (IsValid(Montage))
		PlayerCharacter->PlayMontage(Montage);
}

