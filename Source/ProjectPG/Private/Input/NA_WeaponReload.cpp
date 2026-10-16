#include "Input/NA_WeaponReload.h"
#include "Characters/CustomPlayerCharacter.h"

bool UNA_WeaponReload::ShouldRegisterTriggerEvent(ETriggerEvent Event) const
{
	return Event == ETriggerEvent::Started;
}

void UNA_WeaponReload::Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character)
{
	if (IsValid(Character)) Character->Reload();
}
