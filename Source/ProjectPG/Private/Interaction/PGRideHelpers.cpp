#include "Interaction/PGRideHelpers.h"
#include "CollisionQueryParams.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"

namespace PGRide
{
	void BoardRider(APawn* Ride, APawn* Rider, APlayerController* PC, USceneComponent* AttachTo, const FVector& RelativeOffset)
	{
		if (!IsValid(Ride) || !IsValid(Rider) || !IsValid(PC) || !IsValid(AttachTo))
			return;
		// 탑승자는 탈것 안에 있는 셈: 안 보이고, 안 부딪히고, 탈것을 따라다닌다.
		if (ACharacter* RiderCharacter = Cast<ACharacter>(Rider))
		{
			RiderCharacter->GetCharacterMovement()->StopMovementImmediately();
			RiderCharacter->GetCharacterMovement()->DisableMovement();
		}
		Rider->SetActorEnableCollision(false);
		Rider->SetActorHiddenInGame(true);
		Rider->AttachToComponent(AttachTo, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		Rider->SetActorRelativeLocation(RelativeOffset);
		// 컨트롤러가 몸을 갈아탄다. 이게 탑승의 전부다.
		PC->UnPossess();
		PC->Possess(Ride);
	}

	FVector FindExitSpot(const APawn* Ride, const APawn* Rider, const FVector& ExitLocation, float RingCm, float CandidateLiftCm)
	{
		FVector Spot = ExitLocation + FVector(0.0f, 0.0f, 100.0f);
		UWorld* World = IsValid(Ride) ? Ride->GetWorld() : nullptr;
		if (RingCm <= 0.0f || !World)
			return Spot;
		// 좌석 옆이 벽에 막혀 있으면(스폰된 차가 벽에 끼는 경우 등) 탈것 둘레 8방향에서 사람 캡슐이 들어가는 자리를 찾는다.
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(42.0f, 88.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRideExit), false, Ride);
		Params.AddIgnoredActor(Rider);
		if (!World->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, Capsule, Params))
			return Spot;
		for (int32 Step = 0; Step < 8; ++Step)
		{
			const FVector Candidate = Ride->GetActorLocation()
				+ FRotator(0.0f, Step * 45.0f, 0.0f).Vector() * RingCm + FVector(0.0f, 0.0f, CandidateLiftCm);
			if (!World->OverlapBlockingTestByChannel(Candidate, FQuat::Identity, ECC_Pawn, Capsule, Params))
				return Candidate;
		}
		return Spot; // 다 막혔으면 좌석 자리(예전과 같다)
	}

	void ReleaseRider(APawn* Ride, APawn* Rider, APlayerController* PC, const FVector& StandAt, bool bResetControlRotation)
	{
		if (!IsValid(Ride) || !IsValid(Rider))
			return;
		Rider->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Rider->SetActorHiddenInGame(false);
		Rider->SetActorEnableCollision(true);
		// 탈것에 붙어 있던 동안 탈것이 기울면 탑승자도 같이 기울어진다. 내릴 땐 똑바로 세운다(시야가 비틀리던 원인).
		const FRotator Upright(0.0f, Ride->GetActorRotation().Yaw, 0.0f);
		Rider->SetActorLocationAndRotation(StandAt, Upright, false, nullptr, ETeleportType::TeleportPhysics);
		if (ACharacter* RiderCharacter = Cast<ACharacter>(Rider))
			RiderCharacter->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		if (IsValid(PC))
		{
			PC->UnPossess();
			PC->Possess(Rider);
			if (bResetControlRotation)
				SetViewRotation(PC, Upright);
			// 멀티(9/27): 내린 사람이 원격이면 선 자리를 그 사람 컴퓨터에도 바로 알린다. 서버가 옮긴 위치는 조종하는 본인에게는
			//   복제되지 않아서, 클라에선 캐릭터가 차 가운데에 멈춰 있다가 첫 위치 보정 때 내릴 자리로 튀었다.
			if (!PC->IsLocalController())
				PC->ClientSetLocation(StandAt, Upright);
		}
	}

	void SetViewRotation(APlayerController* PC, const FRotator& Rotation)
	{
		if (!IsValid(PC))
			return;
		PC->SetControlRotation(Rotation);
		if (!PC->IsLocalController())
			PC->ClientSetRotation(Rotation);
	}

	void OnRiderChangedOnClient(APawn* NewRider, APawn* OldRider)
	{
		if (IsValid(NewRider) && !NewRider->HasAuthority())
			NewRider->SetActorEnableCollision(false);
		if (IsValid(OldRider) && OldRider != NewRider && !OldRider->HasAuthority())
		{
			OldRider->SetActorEnableCollision(true);
			// 내가 내린 사람이면 걷기 상태로(서버가 바꾼 걷기 상태는 조종하는 본인에게 복제되지 않는다).
			if (ACharacter* Character = Cast<ACharacter>(OldRider); Character && Character->IsLocallyControlled())
				Character->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		}
	}
}
