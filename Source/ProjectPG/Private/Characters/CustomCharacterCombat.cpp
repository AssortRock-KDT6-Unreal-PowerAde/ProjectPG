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
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
// [멀티 임시수정 2026-10-18] 쏠 때 달리기 어빌리티를 찾아 끄려고 넣음
#include "AbilitySystemComponent.h"
#include "GameplayAbilities/GA_Sprint.h"
// [멀티 임시수정] 끝

void ACustomCharacter::RegisterFirearm(AFirearmEquipment* Weapon)
{
	if (!HasAuthority() || IsDead() || !IsValid(Weapon)) return;
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
	if (HasInfiniteReserveAmmo()) return;
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
	if (HasInfiniteReserveAmmo() && Type != EWeaponType::None) return MAX_int32;
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
	if (!HasAuthority() || IsDead() || (Slot != EEquipSlot::MainWeapon && Slot != EEquipSlot::SubWeapon)) return;
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
	if (!IsLocallyControlled()) return;
	// [멀티 임시수정 2026-10-18 달리는 중 발사]
	// 내 화면에서 발사를 누르면 → 먼저 달리기 어빌리티를 끈다(배그처럼 쏘면 달리기가 풀림).
	// 끄는 소식은 발사 요청보다 먼저 서버로 간다(둘 다 이 캐릭터의 순서 보장 요청이라 순서가 안 바뀜).
	// 다시 달리려면 달리기 키를 다시 눌러야 한다(달리기는 키를 누른 순간에만 켜짐).
	if (bPressed)
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			FGameplayAbilitySpecHandle SprintHandle;
			for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
				if (Spec.IsActive() && Spec.Ability && Spec.Ability->IsA<UGA_Sprint>()) SprintHandle = Spec.Handle;
			if (SprintHandle.IsValid()) ASC->CancelAbilityHandle(SprintHandle);
		}
	// [멀티 임시수정] 끝
	// [진단용, 2026-10-18] 내 화면(클라)에서 쏠 때 카메라가 어디서 어느 쪽을 보고, 그 조준선이 무엇에 닿는지 남긴다.
	// 서버 로그의 [FireHit] 와 비교해 조준이 어긋나는 원인을 찾는다. 원인을 고치면 지운다.
	if (bPressed && !HasAuthority())
	{
		FVector ViewLocation; FRotator ViewRotation;
		GetCombatViewPoint(ViewLocation, ViewRotation);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(FireAimLog), true, this);
		GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation, ViewLocation + ViewRotation.Vector() * 20000.f, ECC_Visibility, Params);
		UE_LOG(LogTemp, Display, TEXT("[FireAim] 클라 %s: 카메라-몸 차이 %s, 방향 %s, 조준선이 닿은 것=%s 거리 %.1fm"), *GetName(),
			*(ViewLocation - GetActorLocation()).ToCompactString(), *ViewRotation.ToCompactString(), *GetNameSafe(Hit.GetActor()), Hit.bBlockingHit ? Hit.Distance / 100.f : -1.f);
	}
	OnReq_SetFireInput(bPressed);
}

void ACustomCharacter::OnReq_SetFireInput_Implementation(bool bPressed)
{
	if (!HasAuthority()) return;
	HandleFireInput(bPressed);
}

void ACustomCharacter::HandleFireInput(bool bPressed)
{
	if (IsDead()) bPressed = false;
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
	// [멀티 임시수정 2026-10-18 달리는 중 발사]
	// 서버가 보기에 아직 달리는 중이면(State.Sprinting 표시) 이번 발사는 거절한다.
	// - 소총을 누르고 있으면 → 반복 타이머가 곧 다시 불러서 달리기가 풀린 뒤 바로 쏜다.
	// - 권총 한 발 → 이번 클릭은 버려진다(정상 클라는 위 SetFireInput 에서 달리기를 먼저 꺼서 거의 안 생김).
	if (const UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		if (ASC->HasMatchingGameplayTag(CustomGameplayTags::State_Sprinting)) return;
	// [멀티 임시수정] 끝
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
	if (HasInfiniteReserveAmmo())
	{
		Weapon->LoadAmmo(Weapon->GetMagazineCapacity());
		OnAmmoChanged.Broadcast();
		return;
	}
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
	return !IsDead() && IsValid(CharacterAttributeSet) && CharacterAttributeSet->GetHealth() > 0.f;
}

void ACustomCharacter::Hit_Implementation(float Damage, AActor* DamageCauser, const FHitResult& HitResult)
{
	if (!HasAuthority() || !FMath::IsFinite(Damage) || Damage <= 0.f || !CanBeHit_Implementation()) return;
	CharacterAttributeSet->SetHealth(FMath::Max(0.f, CharacterAttributeSet->GetHealth() - Damage));
	if (CharacterAttributeSet->GetHealth() <= 0.f) Die();
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
	if (CharacterAttributeSet) CharacterAttributeSet->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleHealthChanged);
	GetWorldTimerManager().ClearTimer(AutomaticFireTimer);
	GetWorldTimerManager().ClearTimer(PendingFireTimeout);
	Super::EndPlay(Reason);
}

void ACustomCharacter::HandleHealthChanged(float Current, float Maximum)
{
	if (HasAuthority() && Current <= 0.f) Die();
}

void ACustomCharacter::Die()
{
	if (!HasAuthority() || IsDead()) return;
	const FWeaponAnimationSet* Set = GetCurrentWeaponAnimationSet();
	const UAnimMontage* Hip = Set ? Set->FindMontage(CustomGameplayTags::WeaponAnimation_Death_Hip) : nullptr;
	const UAnimMontage* Aimed = Set ? Set->FindMontage(CustomGameplayTags::WeaponAnimation_Death_Ironsights) : nullptr;
	const int32 HipCount = Hip ? Hip->CompositeSections.Num() : 0;
	const int32 AimedCount = Aimed ? Aimed->CompositeSections.Num() : 0;
	DeathAnimationIndex = (IsAiming() || HipCount == 0) && AimedCount > 0
		? HipCount + FMath::RandHelper(AimedCount)
		: (HipCount > 0 ? FMath::RandHelper(HipCount) : 0);
	bIsDead = true;
	ApplyDeathState();
	ForceNetUpdate();
	// 죽었다고 알린다 → 플레이어면 컨트롤러가 몇 초 뒤 그 사람만 로비로 보낸다(몬스터는 받는 쪽이 없어 아무 일 없음).
	OnDied.Broadcast(this);
}

UAnimMontage* ACustomCharacter::GetDeathMontage(int32& SectionIndex) const
{
	SectionIndex = DeathAnimationIndex;
	const FWeaponAnimationSet* Set = GetCurrentWeaponAnimationSet();
	if (!Set || SectionIndex < 0) return nullptr;
	const FGameplayTag DeathTags[] = { CustomGameplayTags::WeaponAnimation_Death_Hip, CustomGameplayTags::WeaponAnimation_Death_Ironsights };
	for (const FGameplayTag& Tag : DeathTags)
	{
		UAnimMontage* Montage = Set->FindMontage(Tag);
		const int32 Count = Montage ? Montage->CompositeSections.Num() : 0;
		if (SectionIndex < Count) return Montage;
		SectionIndex -= Count;
	}
	return nullptr;
}

void ACustomCharacter::OnRep_Death() { ApplyDeathState(); }

void ACustomCharacter::ApplyDeathState()
{
	if (!IsDead()) return;
	bFireInputHeld = false;
	GetWorldTimerManager().ClearTimer(AutomaticFireTimer);
	CancelPendingFire();
	ReloadingFirearm.Reset();
	ReloadMontage.Reset();
	bIsAiming = false;
	StopJumping();
	if (AbilitySystemComp) AbilitySystemComp->CancelAllAbilities();
	GetCharacterMovement()->StopMovementImmediately();
	GetCharacterMovement()->DisableMovement();
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	UAnimInstance* Anim = GetMesh()->GetAnimInstance();
	if (!Anim) return; // BeginPlay/ABP 설정이 완료되면 다시 적용합니다.
	int32 SectionIndex = INDEX_NONE;
	UAnimMontage* Montage = GetDeathMontage(SectionIndex);
	const FName Section = Montage ? Montage->CompositeSections[SectionIndex].SectionName : NAME_None;
	if (DeathAnimInstance.Get() == Anim && (!Montage || (Anim->Montage_IsActive(Montage)
		&& Anim->Montage_GetCurrentSection(Montage) == Section))) return;
	DeathAnimInstance = Anim;
	Anim->Montage_Stop(0.f);
	if (!Montage) return;
	float Start = 0.f, End = Montage->GetPlayLength();
	Montage->GetSectionStartAndEndTime(SectionIndex, Start, End);
	GetMesh()->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Anim->Montage_Play(Montage, 1.f, EMontagePlayReturnType::MontageLength, Start);
	Anim->Montage_SetNextSection(Section, NAME_None, Montage);
}
