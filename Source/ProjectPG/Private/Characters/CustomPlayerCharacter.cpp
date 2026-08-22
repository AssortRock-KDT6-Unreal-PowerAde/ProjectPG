// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomPlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/NativeActionComponent.h"
#include "Core/TableSubSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameplayAbilities/CustomAbilitySystemComponent.h"

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

	NativeActionComp = CreateDefaultSubobject<UNativeActionComponent>(TEXT("NativeAction"));
	if (!IsValid(NativeActionComp))
		return;

	AbilitySystemComp = CreateDefaultSubobject<UCustomAbilitySystemComponent>(TEXT("AbilitySystem"));
	if (!IsValid(AbilitySystemComp))
		return;

	AbilitySystemComp->SetIsReplicated(true);
	AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);
}

void ACustomPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
}

void ACustomPlayerCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	APlayerController* controller = Cast<APlayerController>(GetController());
	if (!IsValid(controller))
		return;

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
		controller->GetLocalPlayer());
	if (!IsValid(inputSubsystem))
		return;

	UEnhancedInputComponent* inputComp = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(inputComp))
		return;

	UTableSubSystem* tableSubSystem = UTableSubSystem::Get(this);
	if (!IsValid(tableSubSystem))
		return;

	const FPlayerDefaultActionTableRow* playerDefaultActionRow = tableSubSystem->FindTableRow<
		FPlayerDefaultActionTableRow>(
		"PlayerDefaultActionTable", "PlayerDefault");
	if (nullptr == playerDefaultActionRow)
		return;

	inputSubsystem->AddMappingContext(playerDefaultActionRow->InputMappingContext.Get(), 0);

	if (!IsValid(NativeActionComp))
		return;

	for (auto& TaggedNativeAction : playerDefaultActionRow->TaggedNativeActions)
	{
		UNativeAction* nativeAction = NativeActionComp->RegisterNativeAction(TaggedNativeAction.NativeActionClass);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Started))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Started,
			                      nativeAction, &UNativeAction::Started, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Triggered))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Triggered,
			                      nativeAction, &UNativeAction::Triggered, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Ongoing))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Ongoing,
			                      nativeAction, &UNativeAction::Ongoing, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Completed))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Completed,
			                      nativeAction, &UNativeAction::Completed, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Canceled))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Canceled,
			                      nativeAction, &UNativeAction::Canceled, this);
	}

	UCustomAbilitySystemComponent* abilitySystemComp = GetCustomAbilitySystemComponent();
	if (!IsValid(abilitySystemComp))
		return;

	for (auto& TaggedInputAction : playerDefaultActionRow->TaggedAbilities)
	{
		inputComp->BindAction(TaggedInputAction.InputAction, ETriggerEvent::Started, abilitySystemComp,
		                      &UCustomAbilitySystemComponent::AbilityInputPressed, TaggedInputAction.Tag);
		inputComp->BindAction(TaggedInputAction.InputAction, ETriggerEvent::Completed, abilitySystemComp,
		                      &UCustomAbilitySystemComponent::AbilityInputReleased, TaggedInputAction.Tag);
	}
}

UCustomAbilitySystemComponent* ACustomPlayerCharacter::GetCustomAbilitySystemComponent() const
{
	return Cast<UCustomAbilitySystemComponent>(AbilitySystemComp);
}

USpringArmComponent* ACustomPlayerCharacter::GetCameraArm() const
{
	return CameraArmComp;
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
