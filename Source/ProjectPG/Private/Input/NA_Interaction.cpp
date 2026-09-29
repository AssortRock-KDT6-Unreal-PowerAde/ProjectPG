#include "Input/NA_Interaction.h"

#include "Characters/CustomPlayerCharacter.h"
#include "Objects/PGInteractionComponent.h"

bool UNA_Interaction::ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const
{
	// Started = 누르는 순간 한 번, Completed = 떼는 순간 한 번. Triggered 는 누르고 있는 동안 매 프레임이라 쓰지 않는다.
	return TriggerEvent == ETriggerEvent::Started || TriggerEvent == ETriggerEvent::Completed;
}

void UNA_Interaction::Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter))
		return;
	if (UPGInteractionComponent* Interaction = PlayerCharacter->GetInteraction())
		Interaction->BeginInteract();
}

void UNA_Interaction::Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter)
{
	if (!IsValid(PlayerCharacter))
		return;
	if (UPGInteractionComponent* Interaction = PlayerCharacter->GetInteraction())
		Interaction->EndInteract();
}
