// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animations/CustomAnimInstance.h"
#include "Characters/CustomCharacterMovementComponent.h"
#include "CustomGameplayTags.h"
#include "Core/TableSubSystem.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Components/CapsuleComponent.h"
#include "Animation/AnimMontage.h"

ACustomCharacter::ACustomCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCustomCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;

	SetReplicateMovement(true);

	USkeletalMeshComponent* meshComp = GetMesh();
	if (!IsValid(meshComp))
		return;

	UCharacterMovementComponent* movementComp = GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	movementComp->NavAgentProps.bCanCrouch = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CharacterAttributeSet = CreateDefaultSubobject<UCharacterAttributeSet>(TEXT("CharacterAttributeSet"));
}

void ACustomCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!IsValid(CharacterAttributeSet))
		return;

	if (HasAuthority() && !IsDead()
		&& IsValid(AbilitySystemComp)
		&& !AbilitySystemComp->HasMatchingGameplayTag(CustomGameplayTags::State_UsingStamina)
		&& CharacterAttributeSet->GetStamina() < CharacterAttributeSet->GetMaxStamina())
	{
		const float RecoveredStamina = CharacterAttributeSet->GetStamina()
			+ FMath::Max(0.f, DeltaTime) * StaminaRecoveryMultiplier;
		CharacterAttributeSet->SetStamina(
			FMath::Min(RecoveredStamina, CharacterAttributeSet->GetMaxStamina()));
	}

}

void ACustomCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (IsValid(AbilitySystemComp))
	{
		AbilitySystemComp->InitAbilityActorInfo(this, this);
	}
}

void ACustomCharacter::OnRep_CharacterAttributeSet()
{
}

void ACustomCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	if (IsValid(AbilitySystemComp))
	{
		AbilitySystemComp->InitAbilityActorInfo(this, this);
	}
}

void ACustomCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ACustomCharacter, CharacterAttributeSet);
	DOREPLIFETIME(ACustomCharacter, ActiveFirearm);
	DOREPLIFETIME_CONDITION(ACustomCharacter, ReserveAmmoStates, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(ACustomCharacter, bDoubleAmmoCapacity, COND_OwnerOnly);
	DOREPLIFETIME(ACustomCharacter, bIsAiming);
	DOREPLIFETIME(ACustomCharacter, DeathAnimationIndex);
	DOREPLIFETIME(ACustomCharacter, bIsDead);
}

UAbilitySystemComponent* ACustomCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComp;
}

void ACustomCharacter::PlayMontage(UAnimMontage* Montage)
{
	if (IsDead() || !IsValid(Montage)) return;
	if (HasAuthority())
	{
		// 서버 발사와 AI는 소유 클라이언트에서도 반드시 재생합니다.
		OnRep_PlayMontage(Montage, false);
	}
	else if (IsLocallyControlled())
	{
		PlayAnimMontage(Montage);
		OnReq_PlayMontage(Montage);
	}
}

void ACustomCharacter::OnReq_PlayMontage_Implementation(UAnimMontage* Montage)
{
	if (!IsDead() && IsValid(Montage)) OnRep_PlayMontage(Montage, true);
}

void ACustomCharacter::OnRep_PlayMontage_Implementation(UAnimMontage* Montage, bool bSkipOwningClient)
{
	if (IsDead() || !IsValid(Montage) || (bSkipOwningClient && !HasAuthority() && IsLocallyControlled())) return;
	PlayAnimMontage(Montage);
}

bool ACustomCharacter::ApplyWeaponAnimation(EWeaponType NewWeaponType, TSubclassOf<UCustomAnimInstance> AnimClass)
{
	if (IsDead() && DeathAnimInstance.IsValid()) return false;
	USkeletalMeshComponent* MeshComponent = GetMesh();
	if (!IsValid(MeshComponent))
		return false;

	const FWeaponAnimationSet* AnimationSet = FindWeaponAnimationSet(NewWeaponType);
	if (!AnimClass && AnimationSet) AnimClass = AnimationSet->AnimInstanceClass;
	if (!AnimClass)
		return false;

	if (MeshComponent->GetAnimClass() != AnimClass.Get())
		MeshComponent->SetAnimInstanceClass(AnimClass);
	CurrentWeaponType = NewWeaponType;
	if (IsDead()) ApplyDeathState();
	return true;
}

const FWeaponAnimationSet* ACustomCharacter::GetCurrentWeaponAnimationSet() const
{
	return FindWeaponAnimationSet(CurrentWeaponType);
}

void ACustomCharacter::SetAiming(bool bNewAiming)
{
	if (IsDead()) return;
	bIsAiming = bNewAiming;
	if (auto* Movement = GetCustomCharacterMovement())
		Movement->bWantsToAim = bNewAiming;

	if (!HasAuthority())
		OnReq_SetAiming(bNewAiming);
}

void ACustomCharacter::OnReq_SetAiming_Implementation(bool bNewAiming)
{
	if (IsDead()) return;
	bIsAiming = bNewAiming;
}

class UCustomCharacterMovementComponent* ACustomCharacter::GetCustomCharacterMovement() const
{
	return Cast<UCustomCharacterMovementComponent>(GetMovementComponent());
}

bool ACustomCharacter::IsAiming() const
{
	return bIsAiming;
}

UCharacterAttributeSet* ACustomCharacter::GetCharacterAttributeSet() const
{
	return CharacterAttributeSet;
}

void ACustomCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && CharacterAttributeSet)
		CharacterAttributeSet->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleHealthChanged);
	if (IsDead()) ApplyDeathState();
	// 기본 Pawn/CharacterMesh 프로파일은 Visibility를 무시합니다.
	// 이번 단순 전투는 캐릭터 캡슐을 피격 볼륨으로 사용합니다.
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

	USkeletalMeshComponent* meshComponent = GetMesh();
	if (!IsValid(meshComponent))
		return;

	UTableSubSystem* tableSubsystem = UTableSubSystem::Get(this);
	if (!IsValid(tableSubsystem))
		return;

	const FCharacterTableRow* characterRow = tableSubsystem->FindTableRow<FCharacterTableRow>(TEXT("CharacterTable"),
		CharacterID);
	if (!characterRow)
		return;

	if (!IsValid(characterRow->SkeletalMesh))
		return;
	meshComponent->SetSkeletalMesh(characterRow->SkeletalMesh.Get());
	meshComponent->SetRelativeTransform(characterRow->RootMeshTransform);

	TArray<USceneComponent*> childrenMeshComps;
	meshComponent->GetChildrenComponents(false, childrenMeshComps);

	TSet<FName> appliedChildrenMeshFNames;

	for (auto childComp : childrenMeshComps)
	{
		if (!IsValid(childComp))
			continue;

		FName compName = childComp->GetFName();

		if (characterRow->ChildSkeletalMeshes.Contains(compName))
		{
			USkeletalMeshComponent* childMeshComp = Cast<USkeletalMeshComponent>(childComp);
			childMeshComp->SetSkeletalMesh(characterRow->ChildSkeletalMeshes[compName]);
			childMeshComp->SetLeaderPoseComponent(meshComponent);

			appliedChildrenMeshFNames.Add(compName);
		}
	}

	for (auto [Key, NewMesh] : characterRow->ChildSkeletalMeshes)
	{
		if (appliedChildrenMeshFNames.Contains(Key))
			continue;

		USkeletalMeshComponent* newMeshComp = NewObject<USkeletalMeshComponent>(this, Key);
		newMeshComp->SetSkeletalMesh(NewMesh);

		newMeshComp->SetupAttachment(meshComponent);
		newMeshComp->RegisterComponent();

		newMeshComp->SetLeaderPoseComponent(meshComponent);
	}

	ApplyWeaponAnimation(CurrentWeaponType);
	if (IsDead()) ApplyDeathState();
}

const FWeaponAnimationSet* ACustomCharacter::FindWeaponAnimationSet(EWeaponType WeaponType) const
{
	UTableSubSystem* TableSubsystem = UTableSubSystem::Get(this);
	if (!IsValid(TableSubsystem))
		return nullptr;

	const FCharacterTableRow* CharacterRow = TableSubsystem->FindTableRow<FCharacterTableRow>(
		TEXT("CharacterTable"), CharacterID);
	if (!CharacterRow)
		return nullptr;

	return CharacterRow->WeaponAnimations.Find(WeaponType);
}
