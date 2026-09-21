// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animations/CustomAnimInstance.h"
#include "Characters/CustomCharacterMovementComponent.h"
#include "CustomGameplayTags.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"

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

	// TODO: Table로 옮기기
	FVector meshLocation = FVector::ZeroVector;
	meshLocation.Z = -90.;
	meshComp->SetRelativeLocation(meshLocation);

	FRotator meshRotator = FRotator::ZeroRotator;
	meshRotator.Yaw = -90.;
	meshComp->SetRelativeRotation(meshRotator);

	ConstructorHelpers::FObjectFinder<USkeletalMesh> skeletalMesh(
		TEXT("/Script/Engine.SkeletalMesh'/Game/ControlRig/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny'"));
	if (!skeletalMesh.Succeeded())
		return;

	ConstructorHelpers::FClassFinder<UCustomAnimInstance> AnimInstance(
		TEXT(
			"/Script/Engine.AnimBlueprint'/Game/PG/Blueprint/Animations/ABP_CharacterManny.ABP_CharacterManny_C'"));
	if (!AnimInstance.Succeeded())
		return;

	meshComp->SetSkeletalMesh(skeletalMesh.Object);
	meshComp->SetAnimInstanceClass(AnimInstance.Class);
	// ~TODO: Table로 옮기기

	// AbilitySystemComp = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	// if (!IsValid(AbilitySystemComp))
	// 	return;
	//
	// AbilitySystemComp->SetIsReplicated(true);
	// AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

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

	if (HasAuthority()
		&& IsValid(AbilitySystemComp)
		&& !AbilitySystemComp->HasMatchingGameplayTag(CustomGameplayTags::State_UsingStamina)
		&& CharacterAttributeSet->GetStamina() < CharacterAttributeSet->GetMaxStamina())
	{
		const float RecoveredStamina = CharacterAttributeSet->GetStamina()
			+ FMath::Max(0.f, DeltaTime) * StaminaRecoveryMultiplier;
		CharacterAttributeSet->SetStamina(
			FMath::Min(RecoveredStamina, CharacterAttributeSet->GetMaxStamina()));
	}

	if (IsLocallyControlled() && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			0,
			0.f,
			FColor::Red,
			FString::Printf(
				TEXT("Health: %.1f / %.1f"),
				CharacterAttributeSet->GetHealth(),
				CharacterAttributeSet->GetMaxHealth()));

		GEngine->AddOnScreenDebugMessage(
			1,
			0.f,
			FColor::Green,
			FString::Printf(
				TEXT("Stamina: %.1f / %.1f"),
				CharacterAttributeSet->GetStamina(),
				CharacterAttributeSet->GetMaxStamina()));
	}
}

void ACustomCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

void ACustomCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();

	if (IsValid(AbilitySystemComp))
	{
		AbilitySystemComp->InitAbilityActorInfo(this, this);
	}
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
	DOREPLIFETIME(ACustomCharacter, bIsAiming);
}

UAbilitySystemComponent* ACustomCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComp;
}

void ACustomCharacter::EquipItem(const FString& SocketName, UObject* Item)
{
}

void ACustomCharacter::Fire()
{
	// TODO : 무기 장비 여부 확인
	// TODO : Muzzle을 찾고 이펙트, 레이캐스트
}

void ACustomCharacter::SetAiming(bool bNewAiming)
{
	bIsAiming = bNewAiming;
	if (auto* Movement = GetCustomCharacterMovement())
		Movement->bWantsToAim = bNewAiming;

	if (!HasAuthority())
		OnReq_SetAiming(bNewAiming);
}

void ACustomCharacter::OnReq_SetAiming_Implementation(bool bNewAiming)
{
	bIsAiming = bNewAiming;
}

class UCustomCharacterMovementComponent* ACustomCharacter::GetCustomCharacterMovement() const
{
	return Cast<UCustomCharacterMovementComponent>(GetMovementComponent());
}

bool ACustomCharacter::CanSprintInCurrentState() const
{
	UCustomCharacterMovementComponent* MovementComponent = GetCustomCharacterMovement();
	if (!IsValid(MovementComponent))
		return false;

	return MovementComponent->CanSprintInCurrentState();
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
}
