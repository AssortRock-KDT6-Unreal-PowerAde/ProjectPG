// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomPlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Core/TableSubSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameplayAbilities/CustomAbilitySystemComponent.h"
#include "Input/DefaultInput.h"

ACustomPlayerCharacter::ACustomPlayerCharacter()
{
	USceneComponent* rootComp = GetRootComponent();
	if (!IsValid(rootComp))
		return;

	CameraArmComp = CreateDefaultSubobject<USpringArmComponent>("CameraArm");
	if (!IsValid(CameraArmComp))
		return;

	CameraArmComp->SetupAttachment(rootComp);

	FVector cameraArmAdditiveLocation = FVector::ZeroVector;
	cameraArmAdditiveLocation.Z += 50.;
	CameraArmComp->AddRelativeLocation(cameraArmAdditiveLocation);
	CameraArmComp->TargetArmLength = 200.f;

	CameraComp = CreateDefaultSubobject<UCameraComponent>("Camera");
	if (!IsValid(CameraComp))
		return;

	CameraComp->SetupAttachment(CameraArmComp);

	FVector cameraAdditiveLocation = FVector::ZeroVector;
	cameraAdditiveLocation.Y += 30.;
	CameraComp->AddRelativeLocation(cameraAdditiveLocation);

	AbilitySystemComp = CreateDefaultSubobject<UCustomAbilitySystemComponent>(TEXT("AbilitySystem"));
	if (!IsValid(AbilitySystemComp))
		return;

	AbilitySystemComp->SetIsReplicated(true);
	AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void ACustomPlayerCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	DefaultInput = NewObject<UDefaultInput>(this);
	if (!DefaultInput)
		return;

	APlayerController* controller = Cast<APlayerController>(GetController());
	if (!IsValid(controller))
		return;

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
		controller->GetLocalPlayer());
	if (!IsValid(inputSubsystem))
		return;

	inputSubsystem->AddMappingContext(DefaultInput->InputMappingContext.Get(), 0);

	UEnhancedInputComponent* inputComp = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(inputComp))
		return;

	inputComp->BindAction(DefaultInput->IA_Move, ETriggerEvent::Triggered, this, &ACustomPlayerCharacter::MoveAction);
	inputComp->BindAction(DefaultInput->IA_Look, ETriggerEvent::Triggered, this, &ACustomPlayerCharacter::LookAction);

	UTableSubSystem* tableSubSystem = UTableSubSystem::Get(this);
	if (!IsValid(tableSubSystem))
		return;

	const FPlayerDefaultActionTableRow* playerDefaultActionRow = tableSubSystem->FindTableRow<
		FPlayerDefaultActionTableRow>(
		"PlayerDefaultActionTable", "PlayerDefault");
	if (nullptr == playerDefaultActionRow)
		return;

	for (auto& TaggedInputAction : playerDefaultActionRow->TaggedAbilities)
	{
		inputComp->BindAction(TaggedInputAction.InputAction, ETriggerEvent::Started,
		                      GetCustomAbilitySystemComponent(),
		                      &UCustomAbilitySystemComponent::AbilityInputPressed, TaggedInputAction.Tag);
		inputComp->BindAction(TaggedInputAction.InputAction, ETriggerEvent::Completed,
		                      GetCustomAbilitySystemComponent(),
		                      &UCustomAbilitySystemComponent::AbilityInputReleased, TaggedInputAction.Tag);
	}
}

UCustomAbilitySystemComponent* ACustomPlayerCharacter::GetCustomAbilitySystemComponent() const
{
	return Cast<UCustomAbilitySystemComponent>(AbilitySystemComp);
}

void ACustomPlayerCharacter::MoveAction(const FInputActionValue& InputActionValue)
{
	FVector2D value = InputActionValue.Get<FVector2D>();
	value = value.GetClampedToMaxSize(1.0f);

	FRotator cameraArmRotation = CameraArmComp->GetComponentRotation();
	FRotator actorRotation = FRotator(0, cameraArmRotation.Yaw, 0);
	SetActorRotation(actorRotation);
	CameraArmComp->SetRelativeRotation(FRotator(cameraArmRotation.Pitch, 0, 0));

	UCharacterMovementComponent* movementComp = GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	FVector inputVector = FVector(value.X, value.Y, 0);
	inputVector = actorRotation.RotateVector(inputVector);
	movementComp->AddInputVector(inputVector);
}

void ACustomPlayerCharacter::LookAction(const FInputActionValue& InputActionValue)
{
	FVector2D value = InputActionValue.Get<FVector2D>();

	FRotator rotator = CameraArmComp->GetRelativeRotation();
	rotator.Yaw += value.X;
	rotator.Pitch = FMath::Clamp(rotator.Pitch + value.Y, -89.f, 89.f);

	CameraArmComp->SetRelativeRotation(rotator);
}

void ACustomPlayerCharacter::JumpAction(const FInputActionValue& InputActionValue)
{
}

void ACustomPlayerCharacter::CrouchAction(const FInputActionValue& InputActionValue)
{
}

void ACustomPlayerCharacter::InteractionAction(const FInputActionValue& InputActionValue)
{
}

void ACustomPlayerCharacter::FireAction(const FInputActionValue& InputActionValue)
{
}

void ACustomPlayerCharacter::ReloadAction(const FInputActionValue& InputActionValue)
{
}

void ACustomPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComp->InitAbilityActorInfo(this, this);

	if (HasAuthority())
	{
		if (!IsValid(CharacterAttributeSet))
			return;

		// TODO: Table로 옮기기
		CharacterAttributeSet->InitHealth(100.f);
		CharacterAttributeSet->InitMaxHealth(100.f);
		CharacterAttributeSet->InitStamina(100.f);
		CharacterAttributeSet->InitMaxStamina(100.f);
		CharacterAttributeSet->InitWalkSpeed(300.f);
		CharacterAttributeSet->InitSprintSpeed(700.f);

		UCharacterMovementComponent* movementComp = GetCharacterMovement();
		if (!IsValid(movementComp))
			return;

		movementComp->MaxWalkSpeed = 300.f;
		// TODO: Table로 옮기기

		UTableSubSystem* tableSubSystem = UTableSubSystem::Get(this);
		if (!IsValid(tableSubSystem))
			return;

		const FPlayerDefaultActionTableRow* playerDefaultActionRow = tableSubSystem->FindTableRow<
			FPlayerDefaultActionTableRow>(
			"PlayerDefaultActionTable", "PlayerDefault");
		if (nullptr == playerDefaultActionRow)
			return;

		if (!IsValid(AbilitySystemComp))
			return;

		for (auto& TaggedInputAction : playerDefaultActionRow->TaggedAbilities)
		{
			FGameplayAbilitySpec spec(TaggedInputAction.GameAbilityClass);
			spec.GetDynamicSpecSourceTags().AddTag(TaggedInputAction.Tag);

			AbilitySystemComp->GiveAbility(spec);
		}
	}
}
