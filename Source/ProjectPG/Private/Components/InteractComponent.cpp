// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/InteractComponent.h"
#include "Actor/InteractActor.h"
#include "Interface/Interactable.h"


UInteractComponent::UInteractComponent()
{
}
void UInteractComponent::Interact()
{

	CurrentTarget = FindInteractTarget();
	if (!IsValid(CurrentTarget))
		return;
	if (CurrentTarget->GetOwner())
	{
		CurrentTarget = CurrentTarget->GetOwner();
	}
	ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character)
		return;

	if (!IsValid(CurrentTarget))
		return;
	UE_LOG(LogTemp, Warning, TEXT("Interact()"));

	if (CurrentTarget->Implements<UInteractable>())
	{
		IInteractable::Execute_Interact(CurrentTarget, Character);
	}

}


void UInteractComponent::BeginInteract(AActor* Target)
{
	CurrentTarget = Target;

}

void UInteractComponent::EndInteract()
{
	CurrentTarget = nullptr;

}

AActor* UInteractComponent::FindInteractTarget()
{
	LineTrace();

	if (CurrentTarget)
		return CurrentTarget;

	SphereTrace();

	return CurrentTarget;
}



void UInteractComponent::LineTrace()
{
	CurrentTarget = nullptr;

	ACharacter* Character = Cast<ACharacter>(GetOwner());

	if (!Character)
		return;

	FVector Start = Character->GetPawnViewLocation();
	FVector End = Start + Character->GetControlRotation().Vector() * InteractDistance;

	FHitResult Hit;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);

	if (GetWorld()->LineTraceSingleByChannel(
		Hit,
		Start,
		End,
		ECC_Visibility,
		Params))
	{

		CurrentTarget = Hit.GetActor();

		// 맞은 위치에 빨간 점
		DrawDebugSphere(
			GetWorld(),
			Hit.ImpactPoint,
			10.f,
			12,
			FColor::Red,
			false,
			2.f);
	}
}



void UInteractComponent::SphereTrace()
{
	if (CurrentTarget)
		return;

	ACharacter* Character = Cast<ACharacter>(GetOwner());

	if (!Character)
		return;

	FVector Start = Character->GetActorLocation();
	FVector End = Start;

	FHitResult Hit;

	FCollisionQueryParams Params;
	Params.AddIgnoredActor(Character);

	if (GetWorld()->SweepSingleByChannel(
		Hit,
		Start,
		End,
		FQuat::Identity,
		ECC_Visibility,
		FCollisionShape::MakeSphere(SphereRadius),
		Params))
	{
		CurrentTarget = Hit.GetActor();

		DrawDebugSphere(
			GetWorld(),
			Character->GetActorLocation(),
			SphereRadius,
			24,
			FColor::Blue,
			false,
			2.f);
	}
}

