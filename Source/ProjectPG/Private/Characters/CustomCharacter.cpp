// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animations/CustomAnimInstance.h"
#include "Characters/CustomCharacterMovementComponent.h"
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
			"/Script/Engine.AnimBlueprint'/Game/PG/Blueprint/Animations/ABP_CharacterDefault.ABP_CharacterDefault_C'"));
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
	bCanProne = true;

	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	CharacterAttributeSet = CreateDefaultSubobject<UCharacterAttributeSet>(TEXT("CharacterAttributeSet"));
}

void ACustomCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
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
	DOREPLIFETIME(ACustomCharacter, bIsProne);
}

UAbilitySystemComponent* ACustomCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComp;
}

void ACustomCharacter::RecalculateBaseEyeHeight()
{
	if (IsProne())
		BaseEyeHeight = ProneEyeHeight;
	else
		Super::RecalculateBaseEyeHeight();
}

bool ACustomCharacter::CanEnterProne() const
{
	UCustomCharacterMovementComponent* customCharacterMovement = GetCustomCharacterMovement();
	if (!IsValid(customCharacterMovement))
		return false;

	return !IsProne() && customCharacterMovement->CanEverEnterProne() && GetRootComponent() && !GetRootComponent()->
		IsSimulatingPhysics();
}

void ACustomCharacter::OnEndProne(float HeightAdjust, float ScaledHeightAdjust)
{
	RecalculateBaseEyeHeight();

	const ACustomCharacter* DefaultChar = GetDefault<ACustomCharacter>(GetClass());
	if (!IsValid(DefaultChar))
		return;

	USkeletalMeshComponent* thisMesh = GetMesh();
	USkeletalMeshComponent* defaultMesh = DefaultChar->GetMesh();
	if (thisMesh && defaultMesh)
	{
		FVector& MeshRelativeLocation = thisMesh->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = defaultMesh->GetRelativeLocation().Z;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->BaseTranslationOffset.Z;
	}

	// K2_OnEndProne(HeightAdjust, ScaledHeightAdjust);
}

void ACustomCharacter::OnStartProne(float HeightAdjust, float ScaledHeightAdjust)
{
	RecalculateBaseEyeHeight();

	const ACustomCharacter* DefaultChar = GetDefault<ACustomCharacter>(GetClass());
	if (!IsValid(DefaultChar))
		return;

	USkeletalMeshComponent* thisMesh = GetMesh();
	USkeletalMeshComponent* defaultMesh = DefaultChar->GetMesh();
	if (thisMesh && defaultMesh)
	{
		FVector& MeshRelativeLocation = thisMesh->GetRelativeLocation_DirectMutable();
		MeshRelativeLocation.Z = defaultMesh->GetRelativeLocation().Z + HeightAdjust;
		BaseTranslationOffset.Z = MeshRelativeLocation.Z;
	}
	else
	{
		BaseTranslationOffset.Z = DefaultChar->BaseTranslationOffset.Z + HeightAdjust;
	}

	// K2_OnStartProne(HeightAdjust, ScaledHeightAdjust);
}

void ACustomCharacter::EnterProne(bool bClientSimulation)
{
	UCustomCharacterMovementComponent* movementComp = GetCustomCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (movementComp && CanEnterProne())
		movementComp->bWantsToEnterProne = true;
}

void ACustomCharacter::ExitProne(bool bClientSimulation)
{
	UCustomCharacterMovementComponent* movementComp = GetCustomCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (movementComp)
		movementComp->bWantsToEnterProne = false;
}

void ACustomCharacter::OnRep_IsProne()
{
	UCustomCharacterMovementComponent* movementComp = GetCustomCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (movementComp)
	{
		if (IsProne())
		{
			movementComp->bWantsToEnterProne = true;
			movementComp->EnterProne(true);
		}
		else
		{
			movementComp->bWantsToEnterProne = false;
			movementComp->ExitProne(true);
		}
		movementComp->bNetworkUpdateReceived = true;
	}
}

class UCustomCharacterMovementComponent* ACustomCharacter::GetCustomCharacterMovement() const
{
	return Cast<UCustomCharacterMovementComponent>(GetMovementComponent());
}

bool ACustomCharacter::IsProne() const
{
	return bIsProne;
}

void ACustomCharacter::SetIsProne(const bool bInIsProne)
{
	bIsProne = bInIsProne;
}

void ACustomCharacter::RecalculateProneEyeHeight()
{
	UCustomCharacterMovementComponent* movementComp = GetCustomCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (movementComp != nullptr)
	{
		constexpr float EyeHeightRatio = 0.8f;

		ProneEyeHeight = movementComp->GetProneHalfHeight() * EyeHeightRatio;
	}
}

void ACustomCharacter::EquipItem(const FString& SocketName, UObject* Item)
{
}

UCharacterAttributeSet* ACustomCharacter::GetCharacterAttributeSet() const
{
	return CharacterAttributeSet;
}

void ACustomCharacter::BeginPlay()
{
	Super::BeginPlay();
}
