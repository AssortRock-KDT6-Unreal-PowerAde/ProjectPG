// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemInterface.h"
#include "Common/GameDefine.h"
#include "GameFramework/Character.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "Common/GameData.h"
#include "Combat/Hittable.h"
#include "CustomCharacter.generated.h"

struct FWeaponAnimationSet;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FCharacterAmmoChanged);

UCLASS()
class PROJECTPG_API ACustomCharacter : public ACharacter, public IAbilitySystemInterface, public IHittable
{
	GENERATED_BODY()

public:
	ACustomCharacter(const FObjectInitializer& ObjectInitializer);

public:
	UPROPERTY(BlueprintAssignable, Category="Combat|Ammo")
	FCharacterAmmoChanged OnAmmoChanged;
	UPROPERTY(BlueprintReadOnly, Replicated, Category=Character)
	uint8 bIsAiming : 1 = false;

protected:
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "Abilities")
	TObjectPtr<UAbilitySystemComponent> AbilitySystemComp;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_CharacterAttributeSet, Category = "Abilities")
	TObjectPtr<UCharacterAttributeSet> CharacterAttributeSet;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FName CharacterID = TEXT("Character_Meta_Test");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abilities|Stamina",
		meta=(ClampMin="0.0", UIMin="0.0"))
	float StaminaRecoveryMultiplier = 25.f;

	// TODO: Component로 빼기
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Character|Animation")
	EWeaponType CurrentWeaponType = EWeaponType::Pistol;

	UPROPERTY(ReplicatedUsing=OnRep_ActiveFirearm, BlueprintReadOnly, Category="Combat")
	TObjectPtr<class AFirearmEquipment> ActiveFirearm;
	UPROPERTY(ReplicatedUsing=OnRep_ReserveAmmoStates, BlueprintReadOnly, Category="Combat|Ammo")
	TArray<FWeaponReserveAmmoState> ReserveAmmoStates;
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Combat|Ammo")
	bool bDoubleAmmoCapacity = false;
	// Hip 섹션들 다음 Ironsights 섹션들을 순서대로 센 사망 동작 번호입니다.
	UPROPERTY(ReplicatedUsing=OnRep_Death)
	int32 DeathAnimationIndex = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Death, BlueprintReadOnly, Category="Combat|Death")
	bool bIsDead = false;

private:
	TSet<TWeakObjectPtr<class AEquipment>> AmmoCapacityEquipment;
	TWeakObjectPtr<class AFirearmEquipment> PendingFirearm;
	FTimerHandle AutomaticFireTimer;
	FTimerHandle PendingFireTimeout;
	bool bFireInputHeld = false;
	bool bPendingFire = false;
	TWeakObjectPtr<class AFirearmEquipment> ReloadingFirearm;
	TWeakObjectPtr<class UAnimMontage> ReloadMontage;
	double NextHitReactionTime = 0.;
	TWeakObjectPtr<class UAnimInstance> DeathAnimInstance;

public:
	// 생명주기 / 복제
	virtual void Tick(float DeltaTime) override;
	virtual void OnRep_PlayerState() override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// GAS / 캐릭터 속성
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override;
	UCharacterAttributeSet* GetCharacterAttributeSet() const;

	// 이동 / 조준
	class UCustomCharacterMovementComponent* GetCustomCharacterMovement() const;
	bool IsAiming() const;
	void SetAiming(bool bNewAiming);
	UFUNCTION(Server, Reliable)
	void OnReq_SetAiming(bool bNewAiming);

	// 몽타주 / 무기 애니메이션
	void PlayMontage(class UAnimMontage* Montage);
	UFUNCTION(Server, Reliable)
	void OnReq_PlayMontage(class UAnimMontage* Montage);
	UFUNCTION(NetMulticast, Reliable)
	void OnRep_PlayMontage(class UAnimMontage* Montage, bool bSkipOwningClient);
	bool ApplyWeaponAnimation(EWeaponType NewWeaponType, TSubclassOf<class UCustomAnimInstance> AnimClass = nullptr);
	const FWeaponAnimationSet* GetCurrentWeaponAnimationSet() const;

	// 무기 등록 / 선택
	void SelectWeapon(EEquipSlot Slot);
	void RefreshActiveWeaponPresentation();
	UFUNCTION(Server, Reliable)
	void OnReq_SelectWeapon(EEquipSlot Slot);
	void RegisterFirearm(class AFirearmEquipment* Weapon);
	void UnregisterFirearm(class AFirearmEquipment* Weapon);
	class AFirearmEquipment* GetActiveFirearm() const { return ActiveFirearm; }

	// 발사 입력 / 발사 판정
	void SetFireInput(bool bPressed);
	UFUNCTION(Server, Reliable)
	void OnReq_SetFireInput(bool bPressed);
	virtual void Fire();
	virtual void GetCombatViewPoint(FVector& Location, FRotator& Rotation) const;

	// 재장전: 정상적인 몽타주 종료 시 서버에서 탄약을 이동합니다.
	void Reload();
	UFUNCTION(Server, Reliable)
	void OnReq_Reload();

	// 탄약 / 소지 한도
	void InitializeReserveAmmo(EWeaponType WeaponType, int32 BaseCapacity);
	void SetAmmoCapacityEquipment(class AEquipment* Equipment, bool bEquipped);
	UFUNCTION(BlueprintPure, Category="Combat|Ammo")
	int32 GetReserveAmmo(EWeaponType WeaponType) const;
	virtual bool HasInfiniteReserveAmmo() const { return false; }
	UFUNCTION(BlueprintPure, Category="Combat|Ammo")
	int32 GetReserveAmmoCapacity(EWeaponType WeaponType) const;
	// 서버 전용 획득 API. 장비 해제 후 초과 탄약은 보존하고 신규 획득만 제한합니다.
	int32 AddReserveAmmo(EWeaponType WeaponType, int32 Amount);

	// 피격 (IHittable)
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat")
	bool CanBeHit() const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat")
	void Hit(float Damage, AActor* DamageCauser, const FHitResult& HitResult);
	virtual bool CanBeHit_Implementation() const override;
	virtual void Hit_Implementation(float Damage, AActor* DamageCauser, const FHitResult& HitResult) override;

	UFUNCTION(BlueprintPure, Category="Combat|Death")
	bool IsDead() const { return bIsDead; }

protected:
	// 생명주기
	UFUNCTION()
	virtual void OnRep_CharacterAttributeSet();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	void ApplyDeathState();

	// 무기 애니메이션 조회
	const FWeaponAnimationSet* FindWeaponAnimationSet(EWeaponType WeaponType) const;

private:
	// 무기 선택 / 복제 후 표시 갱신
	UFUNCTION()
	void OnRep_ActiveFirearm();
	UFUNCTION()
	void OnRep_ReserveAmmoStates();
	void SelectWeaponInternal(EEquipSlot Slot);

	// 서버 발사 상태 처리
	void HandleFireInput(bool bPressed);
	void TryStartFireMontage();
	void CancelPendingFire();
	void HandleReloadEnded(class UAnimMontage* Montage, bool bInterrupted);
	UFUNCTION() void OnRep_Death();
	UFUNCTION() void HandleHealthChanged(float Current, float Maximum);
	void Die();
	class UAnimMontage* GetDeathMontage(int32& SectionIndex) const;
};
