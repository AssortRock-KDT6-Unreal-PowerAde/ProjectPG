#pragma once

#include "Input/NativeAction.h"
#include "NA_SelectWeapon.generated.h"

UCLASS()
class PROJECTPG_API UNA_SelectMainWeapon : public UNativeAction
{
	GENERATED_BODY()
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent Event) const override;
	virtual void Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character) override;
};

UCLASS()
class PROJECTPG_API UNA_SelectSubWeapon : public UNativeAction
{
	GENERATED_BODY()
public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent Event) const override;
	virtual void Started(const FInputActionValue& Value, ACustomPlayerCharacter* Character) override;
};
