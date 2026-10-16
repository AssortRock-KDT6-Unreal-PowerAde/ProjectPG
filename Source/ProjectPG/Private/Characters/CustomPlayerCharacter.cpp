// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomPlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "Animation/AnimMontage.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Animations/CustomAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Characters/CustomCharacterMovementComponent.h"
#include "Components/NativeActionComponent.h"
#include "Components/InteractComponent.h"
#include "Core/TableSubSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameplayAbilities/CustomAbilitySystemComponent.h"
#include "GameMode/CustomPlayerState.h"
#include "Net/UnrealNetwork.h"
#include "UI/CharacterVitalsWidget.h"

void ACustomPlayerCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	RefreshVitalsWidget();
}

void ACustomPlayerCharacter::RefreshVitalsWidget()
{
	auto* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController()) return;
	if (VitalsWidget)
	{
		VitalsWidget->ObserveCharacter(this);
		return;
	}
	VitalsWidget = CreateWidget<UCharacterVitalsWidget>(PC);
	if (!VitalsWidget) return;
	VitalsWidget->ObserveCharacter(this);
	// 전체 플레이어 화면을 사용하고 체력/탄약과 크로스헤어는 내부 Canvas에서 배치합니다.
	VitalsWidget->SetAnchorsInViewport(FAnchors(0.f, 0.f, 1.f, 1.f));
	VitalsWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
	if (!VitalsWidget->AddToPlayerScreen())
	{
		VitalsWidget->ObserveCharacter(nullptr);
		VitalsWidget = nullptr;
	}
}

void ACustomPlayerCharacter::UnPossessed()
{
	if (VitalsWidget) VitalsWidget->RemoveFromParent();
	VitalsWidget = nullptr;
	Super::UnPossessed();
}

void ACustomPlayerCharacter::OnRep_Controller()
{
	Super::OnRep_Controller();
	RefreshVitalsWidget();
	if (!IsLocallyControlled() && VitalsWidget)
	{
		VitalsWidget->RemoveFromParent();
		VitalsWidget = nullptr;
	}
}

void ACustomPlayerCharacter::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	RefreshVitalsWidget();
}

void ACustomPlayerCharacter::OnRep_CharacterAttributeSet()
{
	Super::OnRep_CharacterAttributeSet();
	RefreshVitalsWidget();
}

void ACustomPlayerCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	if (VitalsWidget) VitalsWidget->RemoveFromParent();
	VitalsWidget = nullptr;
	Super::EndPlay(Reason);
}

ACustomPlayerCharacter::ACustomPlayerCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UCustomCharacterMovementComponent>(
		ACharacter::CharacterMovementComponentName))
{
	USceneComponent* rootComp = GetRootComponent();
	if (!IsValid(rootComp))
		return;

	CameraArmComp = CreateDefaultSubobject<USpringArmComponent>("CameraArm");
	if (!IsValid(CameraArmComp))
		return;

	CameraArmComp->SetupAttachment(rootComp);
	CameraArmComp->bUsePawnControlRotation = true;
	CameraArmComp->bInheritYaw = true;
	CameraArmComp->bInheritPitch = true;
	CameraArmComp->bInheritRoll = false;

	FVector cameraArmAdditiveLocation = FVector::ZeroVector;
	cameraArmAdditiveLocation.Z += 80.;
	CameraArmComp->AddRelativeLocation(cameraArmAdditiveLocation);
	CameraArmComp->TargetArmLength = 250.f;

	CameraComp = CreateDefaultSubobject<UCameraComponent>("Camera");
	if (!IsValid(CameraComp))
		return;

	CameraComp->SetupAttachment(CameraArmComp);

	NativeActionComp = CreateDefaultSubobject<UNativeActionComponent>(TEXT("NativeAction"));
	if (!IsValid(NativeActionComp))
		return;

	AbilitySystemComp = CreateDefaultSubobject<UCustomAbilitySystemComponent>(TEXT("AbilitySystem"));
	if (!IsValid(AbilitySystemComp))
		return;

	AbilitySystemComp->SetIsReplicated(true);
	AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);


	InteractComp = CreateDefaultSubobject<UInteractComponent>(TEXT("InteractComponent"));

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

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = nullptr;
	if (controller->GetLocalPlayer())
	{
		inputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
			controller->GetLocalPlayer());
	}
	if (!IsValid(inputSubsystem))
	{
		// Dedicated server / non-local player: cannot add local mapping context, but continue binding
		UE_LOG(LogTemp, Warning, TEXT("SetupPlayerInputComponent: EnhancedInputLocalPlayerSubsystem not available (likely server). Continuing without AddMappingContext."));
	}

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

void ACustomPlayerCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
}

UCustomAbilitySystemComponent* ACustomPlayerCharacter::GetCustomAbilitySystemComponent() const
{
	return Cast<UCustomAbilitySystemComponent>(AbilitySystemComp);
}

USpringArmComponent* ACustomPlayerCharacter::GetCameraArm() const
{
	return CameraArmComp;
}

void ACustomPlayerCharacter::GetCombatViewPoint(FVector& Location, FRotator& Rotation) const
{
	Super::GetCombatViewPoint(Location, Rotation);
	if (IsValid(CameraComp)) Location = CameraComp->GetComponentLocation();
	// 서버가 보유한 ControlRotation 기준. 클라이언트가 보낸 HitResult는 신뢰하지 않습니다.
	Rotation = GetControlRotation();
}

void ACustomPlayerCharacter::Interact()
{
	if (IsDead()) return;
	if (InteractComp) InteractComp->Interact();
}
void ACustomPlayerCharacter::OnReq_SyncAimRotation_Implementation(FVector2D AimDirection)
{
	if (!HasAuthority())
		return;

	OnRep_SyncAimRotation(AimDirection);
}

void ACustomPlayerCharacter::OnRep_SyncAimRotation_Implementation(FVector2D AimDirection)
{
	if (IsLocallyControlled())
		return;

	USkeletalMeshComponent* mesh = GetMesh();
	if (!IsValid(mesh))
		return;

	UCustomAnimInstance* animInstance = Cast<UCustomAnimInstance>(mesh->GetAnimInstance());
	if (!IsValid(animInstance))
		return;

	animInstance->SyncAim(AimDirection.X, AimDirection.Y);
}

void ACustomPlayerCharacter::OnReq_SyncCharacterRotation_Implementation(FVector2D AimDirection,
                                                                        FRotator ActorRotation)
{
	if (!HasAuthority())
		return;

	// Legacy RPC: CharacterMovement now owns body rotation.
	OnRep_SyncCharacterRotation(AimDirection);
}

void ACustomPlayerCharacter::OnRep_SyncCharacterRotation_Implementation(FVector2D AimDirection)
{
	if (IsLocallyControlled())
		return;

	USkeletalMeshComponent* mesh = GetMesh();
	if (!IsValid(mesh))
		return;

	UCustomAnimInstance* animInstance = Cast<UCustomAnimInstance>(mesh->GetAnimInstance());
	if (!IsValid(animInstance))
		return;

	animInstance->SyncAim(AimDirection.X, AimDirection.Y);
}

void ACustomPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();
	float playerAngle = FMath::DegreesToRadians(CameraComp->FieldOfView * 0.3333333333f);
	FVector cameraLocation = FVector::ZeroVector;
	cameraLocation.Y = CameraArmComp->TargetArmLength * FMath::Tan(playerAngle) * 0.5f;
	CameraComp->AddRelativeLocation(cameraLocation);

	AbilitySystemComp->InitAbilityActorInfo(this, this);

	if (HasAuthority())
	{
		if (!IsValid(CharacterAttributeSet))
			return;

		// TODO: Table로 옮기기
		CharacterAttributeSet->SetHealth(100.f);
		CharacterAttributeSet->SetMaxHealth(100.f);
		CharacterAttributeSet->SetMaxStamina(100.f);
		CharacterAttributeSet->SetStamina(100.f);

		UCharacterMovementComponent* movementComp = GetCharacterMovement();
		if (!IsValid(movementComp))
			return;

		// ~TODO: Table로 옮기기

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
	RefreshVitalsWidget();
}
