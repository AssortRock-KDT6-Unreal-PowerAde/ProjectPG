// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/CustomAnimInstance.h"

#include "Characters/CustomCharacter.h"
#include "Characters/CustomPlayerCharacter.h"

UCustomAnimInstance::UCustomAnimInstance()
{
}

void UCustomAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	ACustomPlayerCharacter* character = Cast<ACustomPlayerCharacter>(TryGetPawnOwner());
	if (!IsValid(character))
		return;

	bIsCrouched = character->IsCrouched();

	USpringArmComponent* cameraArm = character->GetCameraArm();
	if (!IsValid(cameraArm))
		return;

	UCharacterMovementComponent* movementComp = character->GetCharacterMovement();
	if (!IsValid(movementComp))
		return;

	if (!movementComp->IsWalking())
		return;

	UCharacterAttributeSet* characterAttributeSet = character->GetCharacterAttributeSet();
	if (nullptr == characterAttributeSet)
		return;

	FRotator rotation = character->GetActorRotation();
	FVector velocity = rotation.UnrotateVector(movementComp->Velocity);
	velocity.Z = 0;

	Direction = velocity.Rotation().Yaw;
	// [멀티 임시수정 2026-09-27 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md)
	// 무엇: 걷기 속도가 아직 0이면(클라이언트에 값이 도착하기 전) 나누지 않고 0 으로 둔다.
	// 왜: 0 ÷ 0 은 숫자가 깨져(NaN) 서 있어도 걷는 동작이 나왔다. 원래 줄: Speed = velocity.Size() / characterAttributeSet->GetWalkSpeed();
	const float walkSpeed = characterAttributeSet->GetWalkSpeed();
	Speed = walkSpeed > KINDA_SMALL_NUMBER ? velocity.Size() / walkSpeed : 0.f;
	// [멀티 임시수정] 끝

	FRotator cameraRotation = cameraArm->GetRelativeRotation();
	Aim.X = cameraRotation.Yaw;
	Aim.Y = cameraRotation.Pitch;

	// 개인 프로젝트에서 끔(9/19): 매 프레임 속도를 화면에 찍어 0.000000 이 화면을 덮었다. 팀 원본은 그대로.
	// GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Blue, FString::Printf(TEXT("%f"), Speed));
}
