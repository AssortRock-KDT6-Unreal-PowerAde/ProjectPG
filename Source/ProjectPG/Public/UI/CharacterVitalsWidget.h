#pragma once
#include "Blueprint/UserWidget.h"
#include "CharacterVitalsWidget.generated.h"

UCLASS()
class PROJECTPG_API UCharacterVitalsWidget : public UUserWidget
{
	GENERATED_BODY()
private:
	UPROPERTY(Transient) TObjectPtr<class UCharacterAttributeSet> Attributes;
	UPROPERTY(Transient) TObjectPtr<class UProgressBar> HealthBar;
	UPROPERTY(Transient) TObjectPtr<class UProgressBar> StaminaBar;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> HealthText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> StaminaText;
	UPROPERTY(Transient) TObjectPtr<class UTextBlock> AmmoText;
	UPROPERTY(Transient) TObjectPtr<class UWeaponAttributeSet> WeaponAttributes;
	TWeakObjectPtr<class ACustomCharacter> ObservedCharacter;
public:
	void ObserveCharacter(class ACustomCharacter* Character);
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
private:
	void Unsubscribe();
	UFUNCTION() void UpdateHealth(float Current, float Maximum);
	UFUNCTION() void UpdateStamina(float Current, float Maximum);
	UFUNCTION() void RefreshAmmoSubscription();
	UFUNCTION() void UpdateAmmo();
};
