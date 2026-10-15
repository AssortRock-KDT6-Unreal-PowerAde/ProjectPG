#include "Equipments/FirearmEquipment.h"
#include "Characters/CustomCharacter.h"
#include "Combat/Hittable.h"
#include "Core/ItemSubSystem.h"
#include "GameplayAbilities/WeaponAttributeSet.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Animations/CustomAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"

void AFirearmEquipment::InitializeEquipment(FName ItemID)
{
	Super::InitializeEquipment(ItemID);
	UItemSubSystem* Items = UItemSubSystem::Get(this);
	const FWeaponTableRow* Row = Items ? Items->GetWeapon(ItemID) : nullptr;
	if (!Row) { WeaponType = EWeaponType::None; return; }
	FirearmData = Row->Firearm;
	WeaponType = Row->WeaponType;
	if (HasAuthority())
	{
		WeaponAttributes->InitMagazineCapacity(FMath::Max(1, FirearmData.MagazineCapacity));
		if (InitializedAmmoItemID != EquipmentItemID)
		{
			WeaponAttributes->InitLoadedAmmo(GetMagazineCapacity());
			InitializedAmmoItemID = EquipmentItemID;
		}
		WeaponAttributes->InitFireRate(FMath::Max(0.1f, FirearmData.FireRate));
		WeaponAttributes->InitRange(FMath::Max(1.f, FirearmData.Range));
		WeaponAttributes->InitDamage(FMath::Max(0.f, FirearmData.Damage));
	}
}

int32 AFirearmEquipment::LoadAmmo(int32 AvailableAmmo)
{
	if (!HasAuthority() || AvailableAmmo <= 0) return 0;
	const int32 Loaded = FMath::Min(AvailableAmmo, FMath::Max(0, GetMagazineCapacity() - GetLoadedAmmo()));
	WeaponAttributes->SetLoadedAmmo(GetLoadedAmmo() + Loaded);
	ForceNetUpdate();
	return Loaded;
}

void AFirearmEquipment::Attach()
{
	Super::Attach();
	if (auto* Character = Cast<ACustomCharacter>(OwnerCharacter))
	{
		Character->RegisterFirearm(this);
		Character->RefreshActiveWeaponPresentation();
	}
}

void AFirearmEquipment::SetSelected(bool bSelected)
{
	if (!bIsEquipped || !IsValid(OwnerMesh)) return;
	const FName PartName = FirearmData.MuzzlePartName.IsNone() ? LeftHandIKData.EquipmentPartName : FirearmData.MuzzlePartName;
	if (USceneComponent* Part = FindEquipmentPart(PartName)) Part->SetVisibility(bSelected, true);
	// Holster 등 부속 장비는 유지하고 총기 파츠만 표시/숨깁니다.
	if (const auto* ActorPart = BlueprintsBySocket.Find(PartName))
		if (IsValid(ActorPart->Get())) (*ActorPart)->SetActorHiddenInGame(!bSelected);
	if (!bSelected)
	{
		if (BoundAnimInstance.IsValid()) BoundAnimInstance->ClearLeftHandIKEquipment(this);
		return;
	}
	auto* Character = Cast<ACustomCharacter>(OwnerCharacter);
	UItemSubSystem* Items = UItemSubSystem::Get(this);
	const FWeaponTableRow* Row = Items ? Items->GetWeapon(EquipmentItemID) : nullptr;
	if (Character && Row) Character->ApplyWeaponAnimation(WeaponType, Row->AnimBlueprint);
	BoundAnimInstance = Cast<UCustomAnimInstance>(OwnerMesh->GetAnimInstance());
	if (BoundAnimInstance.IsValid()) BoundAnimInstance->SetLeftHandIKEquipment(this);
}

bool AFirearmEquipment::GetLeftHandIKTarget(FTransform& TargetTransform, float& Alpha, FName& ReferenceBoneName) const
{
	const auto* Character = Cast<ACustomCharacter>(OwnerCharacter);
	if (!Character || Character->GetActiveFirearm() != this)
	{
		TargetTransform = FTransform::Identity;
		Alpha = 0.f;
		ReferenceBoneName = LeftHandIKData.ReferenceBoneName;
		return false;
	}
	return Super::GetLeftHandIKTarget(TargetTransform, Alpha, ReferenceBoneName);
}

void AFirearmEquipment::Detach()
{
	if (auto* Character = Cast<ACustomCharacter>(OwnerCharacter)) Character->UnregisterFirearm(this);
	Super::Detach();
}

float AFirearmEquipment::GetFireInterval() const
{
	return 1.f / FMath::Max(0.1f, WeaponAttributes->GetFireRate());
}

int32 AFirearmEquipment::GetLoadedAmmo() const
{
	return FMath::Max(0, FMath::FloorToInt(WeaponAttributes->GetLoadedAmmo()));
}

int32 AFirearmEquipment::GetMagazineCapacity() const
{
	return FMath::Max(0, FMath::FloorToInt(WeaponAttributes->GetMagazineCapacity()));
}

bool AFirearmEquipment::ConsumeLoadedAmmo()
{
	if (!HasAuthority() || GetLoadedAmmo() <= 0) return false;
	WeaponAttributes->SetLoadedAmmo(GetLoadedAmmo() - 1);
	ForceNetUpdate();
	return true;
}

bool AFirearmEquipment::GetMuzzleLocation(FVector& Location) const
{
	USceneComponent* Part = FindEquipmentPart(FirearmData.MuzzlePartName);
	if (!Part && FirearmData.MuzzlePartName.IsNone() && !LeftHandIKData.EquipmentPartName.IsNone())
		Part = FindEquipmentPart(LeftHandIKData.EquipmentPartName);
	if (Part && FirearmData.MuzzleSocketName.IsNone())
	{
		Location = Part->GetComponentTransform().TransformPosition(FirearmData.MuzzleOffset);
		return !Location.ContainsNaN();
	}
	// Actor equipment's root may be a SceneComponent; search its mesh components for the socket.
	TArray<USceneComponent*> Candidates;
	if (Part) { Candidates.Add(Part); Part->GetChildrenComponents(true, Candidates); Candidates.AddUnique(Part); }
	if (!Part && FirearmData.MuzzlePartName.IsNone())
	{
		for (const auto& Entry : SocketItemTypes)
			if (USceneComponent* Candidate = FindEquipmentPart(Entry.Key))
			{
				TArray<USceneComponent*> ChildComponents;
				Candidate->GetChildrenComponents(true, ChildComponents);
				Candidates.Append(ChildComponents);
				Candidates.AddUnique(Candidate);
			}
	}
	for (USceneComponent* Candidate : Candidates)
		if (IsValid(Candidate) && Candidate->DoesSocketExist(FirearmData.MuzzleSocketName))
		{
			Location = Candidate->GetSocketLocation(FirearmData.MuzzleSocketName);
			return !Location.ContainsNaN();
		}
	return false; // 잘못된 소켓을 캐릭터 원점으로 대체해 발사하지 않습니다.
}

bool AFirearmEquipment::CanFire() const
{
	const auto* Character = Cast<ACustomCharacter>(OwnerCharacter);
	FVector Muzzle;
	return HasAuthority() && bIsEquipped && WeaponType != EWeaponType::None && IsValid(Character)
		&& Character->GetActiveFirearm() == this && GetLoadedAmmo() > 0
		&& GetWorld()->GetTimeSeconds() >= NextFireTime && GetMuzzleLocation(Muzzle);
}

bool AFirearmEquipment::Fire()
{
	if (!CanFire()) return false;
	auto* Character = CastChecked<ACustomCharacter>(OwnerCharacter);
	FVector Muzzle, ViewLocation;
	FRotator ViewRotation;
	if (!GetMuzzleLocation(Muzzle)) return false;
	Character->GetCombatViewPoint(ViewLocation, ViewRotation);
	const float Range = WeaponAttributes->GetRange();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(Firearm), true, Character);
	Params.AddIgnoredActor(this);
	TArray<AActor*> Attached;
	Character->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);
	FHitResult CameraHit;
	const FVector CameraEnd = ViewLocation + ViewRotation.Vector() * Range;
	GetWorld()->LineTraceSingleByChannel(CameraHit, ViewLocation, CameraEnd, ECC_Visibility, Params);
	const FVector AimPoint = CameraHit.bBlockingHit ? CameraHit.ImpactPoint : CameraEnd;
	const FVector Direction = (AimPoint - Muzzle).GetSafeNormal();
	if (Direction.IsNearlyZero() || !ConsumeLoadedAmmo()) return false;
	NextFireTime = GetWorld()->GetTimeSeconds() + GetFireInterval();
	const float HalfAngle = FMath::DegreesToRadians(FMath::Clamp(FirearmData.ConeHalfAngle, 0.f, 45.f));
	// Broad phase: 원뿔을 감싸는 구로 후보를 얻은 뒤 원뿔 내부 여부를 검사합니다.
	TArray<FOverlapResult> Overlaps;
	const FVector Center = Muzzle + Direction * (Range * 0.5f);
	const float Radius = Range * FMath::Sqrt(0.25f + FMath::Square(FMath::Tan(HalfAngle)));
	GetWorld()->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity,
		FCollisionObjectQueryParams::AllObjects, FCollisionShape::MakeSphere(Radius), Params);
	bool bHasTarget = false;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Target = Overlap.GetActor();
		if (!IsValid(Target) || !Target->Implements<UHittable>() || !IHittable::Execute_CanBeHit(Target)) continue;
		FVector Origin, Extent;
		Target->GetActorBounds(true, Origin, Extent);
		const FVector Offset = Origin - Muzzle;
		const float Along = FVector::DotProduct(Offset, Direction);
		const float BoundRadius = Extent.Size();
		if (Along + BoundRadius >= 0.f && Along - BoundRadius <= Range
			&& (Offset - Direction * Along).Size() <= FMath::Max(0.f, Along) * FMath::Tan(HalfAngle) + BoundRadius)
		{ bHasTarget = true; break; }
	}
	if (!bHasTarget) return true; // 대상이 없어도 한 발은 소모합니다.
	const int32 Count = FMath::Clamp(FirearmData.TraceCount, 1, 32);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Ray = FMath::VRandCone(Direction, HalfAngle);
		FHitResult HitResult;
		if (!GetWorld()->LineTraceSingleByChannel(HitResult, Muzzle, Muzzle + Ray * Range, ECC_Visibility, Params)) continue;
		AActor* Target = HitResult.GetActor();
		if (IsValid(Target) && Target->Implements<UHittable>() && IHittable::Execute_CanBeHit(Target))
			IHittable::Execute_Hit(Target, WeaponAttributes->GetDamage() / Count, this, HitResult);
	}
	return true;
}
