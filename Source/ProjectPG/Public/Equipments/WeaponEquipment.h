#pragma once
#include "CoreMinimal.h"
#include "Equipments/Equipment.h"
#include "AbilitySystemInterface.h"
#include "WeaponEquipment.generated.h"

UCLASS()
class PROJECTPG_API AWeaponEquipment : public AEquipment, public IAbilitySystemInterface
{
	GENERATED_BODY()
public:
	AWeaponEquipment();
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UAbilitySystemComponent> WeaponAbilitySystem;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<class UWeaponAttributeSet> WeaponAttributes;
public:
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UWeaponAttributeSet* GetWeaponAttributeSet() const { return WeaponAttributes; }
protected:
	virtual void BeginPlay() override;
};
