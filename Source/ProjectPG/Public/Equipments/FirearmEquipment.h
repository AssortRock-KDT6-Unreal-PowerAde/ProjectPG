#pragma once
#include "CoreMinimal.h"
#include "Equipments/WeaponEquipment.h"
#include "FirearmEquipment.generated.h"

UCLASS()
class PROJECTPG_API AFirearmEquipment : public AWeaponEquipment
{
	GENERATED_BODY()
protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) FFirearmData FirearmData;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) EWeaponType WeaponType = EWeaponType::None;
private:
	double NextFireTime = 0.;
	FName InitializedAmmoItemID;
public:
	virtual void InitializeEquipment(FName ItemID = NAME_None) override;
	virtual void Attach() override;
	virtual void Detach() override;
	void SetSelected(bool bSelected);
	virtual bool GetLeftHandIKTarget(FTransform& TargetTransform, float& Alpha, FName& ReferenceBoneName) const override;
	bool CanFire() const;
	bool Fire();
	bool GetMuzzleLocation(FVector& Location) const;
	float GetFireInterval() const;

	// 각 총기 인스턴스의 장탄 상태. Character의 소지 탄약과 분리합니다.
	UFUNCTION(BlueprintPure, Category="Weapon|Ammo")
	int32 GetLoadedAmmo() const;
	UFUNCTION(BlueprintPure, Category="Weapon|Ammo")
	int32 GetMagazineCapacity() const;
	bool ConsumeLoadedAmmo();
	int32 LoadAmmo(int32 AvailableAmmo);

	EWeaponType GetWeaponType() const { return WeaponType; }
	const FFirearmData& GetFirearmData() const { return FirearmData; }
};
