#include "Input/NA_WeaponFire.h"
#include "Characters/CustomPlayerCharacter.h"

bool UNA_WeaponFire::ShouldRegisterTriggerEvent(ETriggerEvent Event) const
{
	return Event == ETriggerEvent::Started || Event == ETriggerEvent::Completed || Event == ETriggerEvent::Canceled;
}
void UNA_WeaponFire::Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character)
{
	if (IsValid(Character)) Character->SetFireInput(true);
}
void UNA_WeaponFire::Completed(const FInputActionValue& Value, ACustomPlayerCharacter* Character)
{
	if (IsValid(Character)) Character->SetFireInput(false);
}
void UNA_WeaponFire::Canceled(const FInputActionValue& Value, ACustomPlayerCharacter* Character)
{
	Completed(Value, Character);
}
