#include "GameplayAbilities/WeaponAttributeSet.h"
#include "Net/UnrealNetwork.h"

void UWeaponAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UWeaponAttributeSet, LoadedAmmo, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME(UWeaponAttributeSet, MagazineCapacity);
	DOREPLIFETIME(UWeaponAttributeSet, FireRate);
	DOREPLIFETIME(UWeaponAttributeSet, Range);
	DOREPLIFETIME(UWeaponAttributeSet, Damage);
}

void UWeaponAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (Attribute == GetLoadedAmmoAttribute() && OldValue != NewValue) OnLoadedAmmoChanged.Broadcast();
}

void UWeaponAttributeSet::OnRep_LoadedAmmo(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UWeaponAttributeSet, LoadedAmmo, OldValue);
	if (OldValue.GetCurrentValue() != GetLoadedAmmo()) OnLoadedAmmoChanged.Broadcast();
}
