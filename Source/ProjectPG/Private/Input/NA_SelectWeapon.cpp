#include "Input/NA_SelectWeapon.h"
#include "Characters/CustomPlayerCharacter.h"

bool UNA_SelectMainWeapon::ShouldRegisterTriggerEvent(ETriggerEvent Event) const { return Event == ETriggerEvent::Started; }
bool UNA_SelectSubWeapon::ShouldRegisterTriggerEvent(ETriggerEvent Event) const { return Event == ETriggerEvent::Started; }

void UNA_SelectMainWeapon::Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character)
{
	if (IsValid(Character)) Character->SelectWeapon(EEquipSlot::MainWeapon);
}

void UNA_SelectSubWeapon::Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character)
{
	if (IsValid(Character)) Character->SelectWeapon(EEquipSlot::SubWeapon);
}
