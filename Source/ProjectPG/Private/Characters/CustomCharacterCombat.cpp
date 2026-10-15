#include "Characters/CustomCharacter.h"
#include "Equipments/FirearmEquipment.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "CustomGameplayTags.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Components/EquipComponent.h"
#include "GameFramework/PlayerState.h"

void ACustomCharacter::RegisterFirearm(AFirearmEquipment* Weapon)
{
	if (!HasAuthority() || !IsValid(Weapon)) return;
	OnReq_SetFireInput(false);
	CancelPendingFire();
	ActiveFirearm = Weapon;
	InitializeReserveAmmo(Weapon->GetWeaponType(), Weapon->GetFirearmData().ReserveCapacity);
	RefreshActiveWeaponPresentation();
	ForceNetUpdate();
}

void ACustomCharacter::InitializeReserveAmmo(EWeaponType Type, int32 BaseCapacity)
{
	if (!HasAuthority()) return;
	if (Type == EWeaponType::None) return;
	FWeaponReserveAmmoState* State = ReserveAmmoStates.FindByPredicate([Type](const FWeaponReserveAmmoState& S) { return S.WeaponType == Type; });
	if (!State)
	{
		FWeaponReserveAmmoState NewState;
		NewState.WeaponType = Type;
		NewState.BaseReserveCapacity = FMath::Max(0, BaseCapacity);
		NewState.ReserveAmmo = NewState.BaseReserveCapacity;
		ReserveAmmoStates.Add(NewState);
	}
	else
	{
		// 재장착은 기본 소지 한도만 갱신하고 탄약을 생성하지 않습니다.
		State->BaseReserveCapacity = FMath::Max(0, BaseCapacity);
	}
	OnAmmoChanged.Broadcast();
	ForceNetUpdate();
}

void ACustomCharacter::UnregisterFirearm(AFirearmEquipment* Weapon)
{
	if (!HasAuthority() || ActiveFirearm != Weapon) return;
	OnReq_SetFireInput(false);
	CancelPendingFire();
	ActiveFirearm = nullptr;
	RefreshActiveWeaponPresentation();
	ForceNetUpdate();
}

void ACustomCharacter::SetAmmoCapacityEquipment(AEquipment* Equipment, bool bEquipped)
{
	if (!HasAuthority() || !IsValid(Equipment)) return;
	if (bEquipped) AmmoCapacityEquipment.Add(Equipment);
	else AmmoCapacityEquipment.Remove(Equipment);
	for (auto It = AmmoCapacityEquipment.CreateIterator(); It; ++It)
		if (!It->IsValid()) It.RemoveCurrent();
	bDoubleAmmoCapacity = !AmmoCapacityEquipment.IsEmpty();
	ForceNetUpdate();
}

int32 ACustomCharacter::GetReserveAmmo(EWeaponType Type) const
{
	const auto* State = ReserveAmmoStates.FindByPredicate([Type](const FWeaponReserveAmmoState& S) { return S.WeaponType == Type; });
	return State ? State->ReserveAmmo : 0;
}

int32 ACustomCharacter::GetReserveAmmoCapacity(EWeaponType Type) const
{
	const auto* State = ReserveAmmoStates.FindByPredicate([Type](const FWeaponReserveAmmoState& S) { return S.WeaponType == Type; });
	return State ? State->BaseReserveCapacity * (bDoubleAmmoCapacity ? 2 : 1) : 0;
}

int32 ACustomCharacter::AddReserveAmmo(EWeaponType Type, int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return 0;
	auto* State = ReserveAmmoStates.FindByPredicate([Type](const FWeaponReserveAmmoState& S) { return S.WeaponType == Type; });
	if (!State) return 0;
	const int32 Added = FMath::Min(Amount, FMath::Max(0, GetReserveAmmoCapacity(Type) - State->ReserveAmmo));
	State->ReserveAmmo += Added;
	if (Added > 0) OnAmmoChanged.Broadcast();
	ForceNetUpdate();
	return Added;
}

void ACustomCharacter::GetCombatViewPoint(FVector& Location, FRotator& Rotation) const
{
	GetActorEyesViewPoint(Location, Rotation);
	Rotation = GetControlRotation();
}

void ACustomCharacter::SelectWeapon(EEquipSlot Slot)
{
	if (Slot != EEquipSlot::MainWeapon && Slot != EEquipSlot::SubWeapon) return;
	if (HasAuthority()) SelectWeaponInternal(Slot);
	else if (IsLocallyControlled()) OnReq_SelectWeapon(Slot);
}

void ACustomCharacter::OnReq_SelectWeapon_Implementation(EEquipSlot Slot) { SelectWeaponInternal(Slot); }

void ACustomCharacter::SelectWeaponInternal(EEquipSlot Slot)
{
	if (!HasAuthority() || (Slot != EEquipSlot::MainWeapon && Slot != EEquipSlot::SubWeapon)) return;
	APlayerState* State = GetPlayerState();
	UEquipComponent* Equipment = State ? State->FindComponentByClass<UEquipComponent>() : nullptr;
	AFirearmEquipment* Weapon = Equipment ? Cast<AFirearmEquipment>(Equipment->GetEquipActor(Slot)) : nullptr;
	if (!IsValid(Weapon) || Weapon == ActiveFirearm || Weapon->GetOwner() != this
		|| !Weapon->GetRootComponent() || Weapon->GetRootComponent()->GetAttachParent() != GetMesh()) return;
	OnReq_SetFireInput(false);
	CancelPendingFire();
	if (GetMesh()->GetAnimInstance()) GetMesh()->GetAnimInstance()->Montage_Stop(0.1f);
	ActiveFirearm = Weapon;
	RefreshActiveWeaponPresentation();
	ForceNetUpdate();
}

void ACustomCharacter::OnRep_ActiveFirearm() { RefreshActiveWeaponPresentation(); }

void ACustomCharacter::OnRep_ReserveAmmoStates() { OnAmmoChanged.Broadcast(); }

void ACustomCharacter::RefreshActiveWeaponPresentation()
{
	TArray<AActor*> Attached;
	GetAttachedActors(Attached, true, true);
	// 비활성 무기를 먼저 정리해 선택 무기의 ABP/IK를 마지막에 적용합니다.
	for (AActor* Actor : Attached)
		if (auto* Weapon = Cast<AFirearmEquipment>(Actor))
			if (Weapon != ActiveFirearm) Weapon->SetSelected(false);
	if (IsValid(ActiveFirearm)) ActiveFirearm->SetSelected(true);
	OnAmmoChanged.Broadcast();
}

void ACustomCharacter::SetFireInput(bool bPressed)
{
	if (IsLocallyControlled()) OnReq_SetFireInput(bPressed);
}

void ACustomCharacter::OnReq_SetFireInput_Implementation(bool bPressed)
{
	if (!HasAuthority()) return;
	HandleFireInput(bPressed);
}

void ACustomCharacter::HandleFireInput(bool bPressed)
{
	if (bPressed && bFireInputHeld) return;
	bFireInputHeld = bPressed;
	GetWorldTimerManager().ClearTimer(AutomaticFireTimer);
	if (!bPressed || !IsValid(ActiveFirearm)) return;
	TryStartFireMontage();
	if (ActiveFirearm->GetWeaponType() == EWeaponType::Rifle)
		GetWorldTimerManager().SetTimer(AutomaticFireTimer, this, &ACustomCharacter::TryStartFireMontage,
			FMath::Min(0.01f, ActiveFirearm->GetFireInterval()), true);
}

void ACustomCharacter::TryStartFireMontage()
{
	if (!HasAuthority() || ReloadingFirearm.IsValid() || !bFireInputHeld || bPendingFire || !CanBeHit_Implementation()
		|| !IsValid(ActiveFirearm) || !ActiveFirearm->CanFire()) return;
	const FWeaponAnimationSet* Set = GetCurrentWeaponAnimationSet();
	UAnimMontage* Montage = Set ? Set->FindMontage(IsAiming()
		? CustomGameplayTags::WeaponAnimation_Fire_Ironsights : CustomGameplayTags::WeaponAnimation_Fire_Hip) : nullptr;
	if (!IsValid(Montage) || !GetMesh()->GetAnimInstance()) return;
	bPendingFire = true;
	PendingFirearm = ActiveFirearm;
	// Dedicated server도 몽타주 Notify와 머즐 본 위치를 평가해야 합니다.
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	GetWorldTimerManager().SetTimer(PendingFireTimeout, this, &ACustomCharacter::CancelPendingFire,
		FMath::Max(0.1f, Montage->GetPlayLength() + 0.25f), false);
	PlayMontage(Montage);
}

void ACustomCharacter::Reload()
{
	if (!HasAuthority())
	{
		if (IsLocallyControlled()) OnReq_Reload();
		return;
	}
	if (ReloadingFirearm.IsValid() || !CanBeHit_Implementation() || !IsValid(ActiveFirearm)
		|| ActiveFirearm->GetLoadedAmmo() >= ActiveFirearm->GetMagazineCapacity()
		|| GetReserveAmmo(ActiveFirearm->GetWeaponType()) <= 0) return;
	const FWeaponAnimationSet* Set = GetCurrentWeaponAnimationSet();
	UAnimMontage* Montage = Set ? Set->FindMontage(IsAiming()
		? CustomGameplayTags::WeaponAnimation_Reload_Ironsights : CustomGameplayTags::WeaponAnimation_Reload_Hip) : nullptr;
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Montage || !Anim) return;
	OnReq_SetFireInput(false);
	CancelPendingFire();
	ReloadingFirearm = ActiveFirearm;
	ReloadMontage = Montage;
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	PlayMontage(Montage);
	if (!Anim->Montage_IsPlaying(Montage))
	{
		ReloadingFirearm.Reset();
		ReloadMontage.Reset();
		return;
	}
	FOnMontageEnded Ended;
	Ended.BindUObject(this, &ACustomCharacter::HandleReloadEnded);
	Anim->Montage_SetEndDelegate(Ended, Montage);
}

void ACustomCharacter::OnReq_Reload_Implementation() { Reload(); }

void ACustomCharacter::HandleReloadEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (!HasAuthority() || ReloadMontage.Get() != Montage) return;
	AFirearmEquipment* Weapon = ReloadingFirearm.Get();
	ReloadingFirearm.Reset();
	ReloadMontage.Reset();
	if (bInterrupted || !IsValid(Weapon) || ActiveFirearm != Weapon || !CanBeHit_Implementation()) return;
	auto* State = ReserveAmmoStates.FindByPredicate([Weapon](const FWeaponReserveAmmoState& S) { return S.WeaponType == Weapon->GetWeaponType(); });
	if (!State) return;
	State->ReserveAmmo -= Weapon->LoadAmmo(State->ReserveAmmo);
	OnAmmoChanged.Broadcast();
	ForceNetUpdate();
}

void ACustomCharacter::Fire()
{
	// Client/모듈형 follower mesh/중복 Notify에서는 데미지와 탄약 연산을 하지 않습니다.
	if (!HasAuthority() || !bPendingFire || PendingFirearm.Get() != ActiveFirearm || !IsValid(ActiveFirearm)) return;
	CancelPendingFire();
	if (CanBeHit_Implementation()) ActiveFirearm->Fire();
}

void ACustomCharacter::CancelPendingFire()
{
	bPendingFire = false;
	PendingFirearm.Reset();
	GetWorldTimerManager().ClearTimer(PendingFireTimeout);
}

bool ACustomCharacter::CanBeHit_Implementation() const
{
	return IsValid(CharacterAttributeSet) && CharacterAttributeSet->GetHealth() > 0.f;
}

void ACustomCharacter::Hit_Implementation(float Damage, AActor* DamageCauser, const FHitResult& HitResult)
{
	if (!HasAuthority() || !FMath::IsFinite(Damage) || Damage <= 0.f || !CanBeHit_Implementation()) return;
	CharacterAttributeSet->SetHealth(FMath::Max(0.f, CharacterAttributeSet->GetHealth() - Damage));
	ForceNetUpdate();
	// 산탄의 여러 트레이스는 각각 피해를 주되 반응 몽타주는 매번 초기화하지 않습니다.
	if (!CanBeHit_Implementation() || GetWorld()->GetTimeSeconds() < NextHitReactionTime) return;
	const FWeaponAnimationSet* Set = GetCurrentWeaponAnimationSet();
	if (!Set || !GetMesh()->GetAnimInstance()) return;
	const FGameplayTag HitReactionTags[] = { CustomGameplayTags::WeaponAnimation_HitReact_1,
		CustomGameplayTags::WeaponAnimation_HitReact_2, CustomGameplayTags::WeaponAnimation_HitReact_3,
		CustomGameplayTags::WeaponAnimation_HitReact_4 };
	TArray<UAnimMontage*, TInlineAllocator<4>> Montages;
	for (const FGameplayTag& Tag : HitReactionTags)
		if (UAnimMontage* Montage = Set->FindMontage(Tag)) Montages.Add(Montage);
	if (Montages.IsEmpty()) return;
	NextHitReactionTime = GetWorld()->GetTimeSeconds() + 0.2;
	CancelPendingFire();
	PlayMontage(Montages[FMath::RandHelper(Montages.Num())]);
}

void ACustomCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(AutomaticFireTimer);
	GetWorldTimerManager().ClearTimer(PendingFireTimeout);
	Super::EndPlay(Reason);
}
