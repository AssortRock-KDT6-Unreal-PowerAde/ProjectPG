// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomCharacterMovementComponent.h"

#include "Characters/CustomCharacter.h"
#include "AbilitySystemComponent.h"
#include "CustomGameplayTags.h"

namespace
{
	class FSavedMove_CustomCharacter : public FSavedMove_Character
	{
	public:
		using Super = FSavedMove_Character;
		bool bSavedSprint = false;
		bool bSavedAim = false;

		virtual void Clear() override
		{
			Super::Clear();
			bSavedSprint = false;
			bSavedAim = false;
		}

		virtual uint8 GetCompressedFlags() const override
		{
			return Super::GetCompressedFlags()
				| (bSavedSprint ? FLAG_Custom_0 : 0)
				| (bSavedAim ? FLAG_Custom_1 : 0);
		}

		virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
		{
			const auto* Other = static_cast<const FSavedMove_CustomCharacter*>(NewMove.Get());
			return bSavedSprint == Other->bSavedSprint && bSavedAim == Other->bSavedAim
				&& Super::CanCombineWith(NewMove, Character, MaxDelta);
		}

		virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
			FNetworkPredictionData_Client_Character& ClientData) override
		{
			Super::SetMoveFor(Character, InDeltaTime, NewAccel, ClientData);
			const auto* Movement = CastChecked<UCustomCharacterMovementComponent>(Character->GetCharacterMovement());
			bSavedSprint = Movement->bWantsToSprint;
			bSavedAim = Movement->bWantsToAim;
		}

		virtual void PrepMoveFor(ACharacter* Character) override
		{
			Super::PrepMoveFor(Character);
			auto* Movement = CastChecked<UCustomCharacterMovementComponent>(Character->GetCharacterMovement());
			Movement->bWantsToSprint = bSavedSprint;
			Movement->bWantsToAim = bSavedAim;
		}
	};

	class FPredictionData_CustomCharacter : public FNetworkPredictionData_Client_Character
	{
	public:
		explicit FPredictionData_CustomCharacter(const UCharacterMovementComponent& Movement)
			: FNetworkPredictionData_Client_Character(Movement) {}

		virtual FSavedMovePtr AllocateNewMove() override
		{
			return FSavedMovePtr(new FSavedMove_CustomCharacter());
		}
	};
}

UCustomCharacterMovementComponent::UCustomCharacterMovementComponent()
{
	MaxWalkSpeed = 300.f;
	RotationRate = FRotator(0.f, 540.f, 0.f);
}

void UCustomCharacterMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!HasValidData() || !CharacterOwner->Controller
		|| CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy
		|| UpdatedComponent->IsSimulatingPhysics())
		return;

	if (HasAnimRootMotion() && !bAllowPhysicsRotationDuringAnimRootMotion)
		return;

	// Looking around while standing still should not rotate the character body.
	if (Acceleration.IsNearlyZero())
		return;

	const FVector GroundVelocity = Velocity.GetSafeNormal2D();
	const float TargetYaw = IsSprinting() && !GroundVelocity.IsNearlyZero()
		? GroundVelocity.Rotation().Yaw
		: CharacterOwner->GetControlRotation().Yaw;

	FRotator NewRotation = UpdatedComponent->GetComponentRotation();
	NewRotation.Yaw = FMath::FixedTurn(NewRotation.Yaw, TargetYaw, GetDeltaRotation(DeltaTime).Yaw);
	MoveUpdatedComponent(FVector::ZeroVector, NewRotation, false);
}

void UCustomCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	Super::UpdateFromCompressedFlags(Flags);
	bWantsToSprint = (Flags & FSavedMove_Character::FLAG_Custom_0) != 0;
	bWantsToAim = (Flags & FSavedMove_Character::FLAG_Custom_1) != 0;
}

FNetworkPredictionData_Client* UCustomCharacterMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		auto* MutableThis = const_cast<UCustomCharacterMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FPredictionData_CustomCharacter(*this);
	}
	return ClientPredictionData;
}

bool UCustomCharacterMovementComponent::IsSprinting() const
{
	if (CharacterOwner && CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		const auto* Character = Cast<ACustomCharacter>(CharacterOwner);
		const auto* ASC = Character ? Character->GetAbilitySystemComponent() : nullptr;
		return ASC && ASC->HasMatchingGameplayTag(CustomGameplayTags::State_Sprinting)
			&& IsMovingOnGround() && !IsCrouching() && !IsAiming();
	}
	const auto* Character = Cast<ACustomCharacter>(CharacterOwner);
	const auto* Attributes = Character ? Character->GetCharacterAttributeSet() : nullptr;
	if (!Attributes || Attributes->GetStamina() <= 0.f)
		return false;
	return bWantsToSprint && CanSprintInCurrentState();
}

bool UCustomCharacterMovementComponent::IsAiming() const
{
	const ACustomCharacter* Character = Cast<ACustomCharacter>(CharacterOwner);
	return Character && (Character->GetLocalRole() == ROLE_SimulatedProxy
		? Character->IsAiming() : bWantsToAim);
}

bool UCustomCharacterMovementComponent::HasForwardMovementInput() const
{
	if (!UpdatedComponent || !CharacterOwner || !CharacterOwner->Controller || Acceleration.IsNearlyZero())
		return false;

	const FVector InputDirection =
		Acceleration.GetSafeNormal2D();

	const FVector ForwardDirection =
		FRotator(0.f, CharacterOwner->GetControlRotation().Yaw, 0.f).Vector();

	const float ForwardDot =
		FVector::DotProduct(ForwardDirection, InputDirection);

	return ForwardDot >= MinSprintForwardInputDot;
}

bool UCustomCharacterMovementComponent::CanSprintInCurrentState() const
{
	if (!IsSprintEnabled())
		return false;

	if (!HasValidData() || !UpdatedComponent)
		return false;

	if (MOVE_Walking != MovementMode)
		return false;

	if (IsCrouching())
		return false;

	if (IsAiming())
		return false;

	if (!HasForwardMovementInput())
		return false;

	return IsMovingOnGround() && !UpdatedComponent->IsSimulatingPhysics();
}

float UCustomCharacterMovementComponent::GetMaxSpeed() const
{
	if (!IsValid(CharacterOwner))
		return Super::GetMaxSpeed();

	const ACustomCharacter* Character = Cast<ACustomCharacter>(CharacterOwner);
	if (!IsValid(Character))
		return Super::GetMaxSpeed();

	if (MOVE_Custom == MovementMode)
		return Super::GetMaxSpeed();

	if (MOVE_Walking == MovementMode)
	{
		if (IsCrouching())
			return Super::GetMaxSpeed();

		if (IsSprinting())
			return MaxSprintSpeed;

		if (IsAiming())
			return MaxAimWalkSpeed;
	}

	return Super::GetMaxSpeed();
}
