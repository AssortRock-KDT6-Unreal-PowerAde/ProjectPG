// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animations/CustomAnimInstance.h"
#include "GameFramework/CharacterMovementComponent.h"

// Sets default values
ACustomCharacter::ACustomCharacter()
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

	// TODO: Table로 옮기기
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

	// AbilitySystemComp = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	// if (!IsValid(AbilitySystemComp))
	// 	return;
	//
	// AbilitySystemComp->SetIsReplicated(true);
	// AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);

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

UAbilitySystemComponent* ACustomCharacter::GetAbilitySystemComponent() const
{
	return AbilitySystemComp;
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
