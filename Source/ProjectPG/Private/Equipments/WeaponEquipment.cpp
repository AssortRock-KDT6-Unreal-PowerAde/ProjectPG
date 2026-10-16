#include "Equipments/WeaponEquipment.h"
#include "AbilitySystemComponent.h"
#include "GameplayAbilities/WeaponAttributeSet.h"

AWeaponEquipment::AWeaponEquipment()
{
	WeaponAbilitySystem = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("WeaponAbilitySystem"));
	WeaponAbilitySystem->SetIsReplicated(true);
	WeaponAbilitySystem->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	WeaponAttributes = CreateDefaultSubobject<UWeaponAttributeSet>(TEXT("WeaponAttributes"));
}

UAbilitySystemComponent* AWeaponEquipment::GetAbilitySystemComponent() const { return WeaponAbilitySystem; }

void AWeaponEquipment::BeginPlay()
{
	Super::BeginPlay();
	WeaponAbilitySystem->InitAbilityActorInfo(this, this);
}
