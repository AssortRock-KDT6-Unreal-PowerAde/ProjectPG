#pragma once
#include "Input/NativeAction.h"
#include "NA_WeaponFire.generated.h"

UCLASS()
class PROJECTPG_API UNA_WeaponFire : public UNativeAction
{
	GENERATED_BODY()
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character) override;
	virtual void Completed(const FInputActionValue& Value, ACustomPlayerCharacter* Character) override;
	virtual void Canceled(const FInputActionValue& Value, ACustomPlayerCharacter* Character) override;
};
