#pragma once
#include "CoreMinimal.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "WeaponAttributeSet.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWeaponAmmoChanged);

UCLASS()
class PROJECTPG_API UWeaponAttributeSet : public UAttributeSet
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintAssignable) FWeaponAmmoChanged OnLoadedAmmoChanged;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_LoadedAmmo) FGameplayAttributeData LoadedAmmo;
	UPROPERTY(BlueprintReadOnly, Replicated) FGameplayAttributeData MagazineCapacity;
	UPROPERTY(BlueprintReadOnly, Replicated) FGameplayAttributeData FireRate;
	UPROPERTY(BlueprintReadOnly, Replicated) FGameplayAttributeData Range;
	UPROPERTY(BlueprintReadOnly, Replicated) FGameplayAttributeData Damage;
public:
	ATTRIBUTE_ACCESSORS(UWeaponAttributeSet, LoadedAmmo)
	ATTRIBUTE_ACCESSORS(UWeaponAttributeSet, MagazineCapacity)
	ATTRIBUTE_ACCESSORS(UWeaponAttributeSet, FireRate)
	ATTRIBUTE_ACCESSORS(UWeaponAttributeSet, Range)
	ATTRIBUTE_ACCESSORS(UWeaponAttributeSet, Damage)
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
private:
	UFUNCTION() void OnRep_LoadedAmmo(const FGameplayAttributeData& OldValue);
};
