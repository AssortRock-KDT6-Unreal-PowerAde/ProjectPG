#pragma once
#include "Input/NativeAction.h"
#include "NA_WeaponReload.generated.h"

UCLASS()
class PROJECTPG_API UNA_WeaponReload : public UNativeAction
{
	GENERATED_BODY()
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent Event) const override;
	virtual void Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character) override;
};
