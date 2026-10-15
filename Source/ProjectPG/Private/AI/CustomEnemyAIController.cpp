#include "AI/CustomEnemyAIController.h"
#include "Characters/CustomEnemyCharacter.h"
#include "Characters/CustomPlayerCharacter.h"
#include "Equipments/FirearmEquipment.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Navigation/PathFollowingComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"

void ACustomEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	PatrolOrigin = InPawn->GetActorLocation();
	GetWorldTimerManager().SetTimer(DecisionTimer, this, &ThisClass::UpdateCombat, 0.15f, true);
}

void ACustomEnemyAIController::OnUnPossess()
{
	if (auto* Enemy = Cast<ACustomEnemyCharacter>(GetPawn())) Enemy->OnReq_SetFireInput(false);
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	Target.Reset();
	Phase = ECombatPhase::None;
	Super::OnUnPossess();
}

void ACustomEnemyAIController::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(DecisionTimer);
	Super::EndPlay(Reason);
}

bool ACustomEnemyAIController::CanSeeFrom(const FVector& Location) const
{
	if (!Target.IsValid()) return false;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySight), false, GetPawn());
	if (GetPawn())
	{
		TArray<AActor*> Attached;
		GetPawn()->GetAttachedActors(Attached, true, true);
		Params.AddIgnoredActors(Attached);
	}
	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Location, Target->GetActorLocation(), ECC_Visibility, Params);
	return !bBlocked || Hit.GetActor() == Target.Get();
}

bool ACustomEnemyAIController::FindCover(const ACustomEnemyCharacter* Enemy)
{
	auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav || !Target.IsValid() || Enemy->GetEnemyData().CoverSearchRadius <= 0.f) return false;
	FVector TargetEyes; FRotator Rotation;
	Target->GetActorEyesViewPoint(TargetEyes, Rotation);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyCover), false, Enemy);
	Params.AddIgnoredActor(Target.Get());
	for (int32 Attempt = 0; Attempt < 16; ++Attempt)
	{
		FNavLocation Cover;
		if (!Nav->GetRandomReachablePointInRadius(Enemy->GetActorLocation(), Enemy->GetEnemyData().CoverSearchRadius, Cover)) continue;
		FHitResult Obstacle;
		if (!GetWorld()->LineTraceSingleByChannel(Obstacle, TargetEyes, Cover.Location + FVector(0,0,50), ECC_Visibility, Params)) continue;
		if (!Obstacle.GetComponent() || Obstacle.GetComponent()->Mobility != EComponentMobility::Static) continue;
		const FVector Side = FVector::CrossProduct((TargetEyes - Cover.Location).GetSafeNormal2D(), FVector::UpVector);
		for (float Sign : { -1.f, 1.f })
		{
			FNavLocation Peek;
			if (!Nav->ProjectPointToNavigation(Cover.Location + Side * Sign * 170.f, Peek, FVector(60,60,150))) continue;
			if (!CanSeeFrom(Peek.Location + FVector(0,0,Enemy->BaseEyeHeight))) continue;
			auto* Path = UNavigationSystemV1::FindPathToLocationSynchronously(GetWorld(), Cover.Location, Peek.Location, const_cast<ACustomEnemyCharacter*>(Enemy));
			if (!Path || !Path->IsValid() || Path->IsPartial() || Path->GetPathLength() > 450.f) continue;
			CoverLocation = Cover.Location;
			PeekLocation = Peek.Location;
			return true;
		}
	}
	return false;
}

void ACustomEnemyAIController::Attack(ACustomEnemyCharacter* Enemy)
{
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(Enemy->GetActorRotation().Yaw, Enemy->GetControlRotation().Yaw)) > 60.f)
	{
		Enemy->OnReq_SetFireInput(false);
		return;
	}
	auto* Weapon = Enemy->GetActiveFirearm();
	if (!IsValid(Weapon)) return;
	if (Weapon->GetLoadedAmmo() <= 0)
	{
		Enemy->OnReq_SetFireInput(false);
		Enemy->Reload();
		return;
	}
	Enemy->OnReq_SetFireInput(true);
	if (Weapon->GetWeaponType() != EWeaponType::Rifle) Enemy->OnReq_SetFireInput(false);
}

void ACustomEnemyAIController::UpdateCombat()
{
	auto* Enemy = Cast<ACustomEnemyCharacter>(GetPawn());
	if (!Enemy || !HasAuthority()) return;
	if (Enemy->IsDead())
	{
		Enemy->OnReq_SetFireInput(false);
		StopMovement();
		ClearFocus(EAIFocusPriority::Gameplay);
		GetWorldTimerManager().ClearTimer(DecisionTimer);
		return;
	}
	const auto& Data = Enemy->GetEnemyData();
	const double Now = GetWorld()->GetTimeSeconds();
	if (Target.IsValid() && (Target->IsDead() || FVector::DistSquared(Enemy->GetActorLocation(), Target->GetActorLocation()) > FMath::Square(Data.DetectionRange * 1.5f)))
		Target.Reset();
	if (!Target.IsValid())
	{
		Phase = ECombatPhase::None;
		float BestDistance = FMath::Square(Data.DetectionRange);
		for (TActorIterator<ACustomPlayerCharacter> It(GetWorld()); It; ++It)
		{
			const float Distance = FVector::DistSquared(Enemy->GetActorLocation(), It->GetActorLocation());
			if (It->IsDead() || !It->CanBeHit_Implementation() || Distance >= BestDistance || !LineOfSightTo(*It)) continue;
			Target = *It;
			BestDistance = Distance;
		}
		if (Target.IsValid()) LastSeenTime = Now;
	}
	if (!Target.IsValid())
	{
		Enemy->OnReq_SetFireInput(false);
		ClearFocus(EAIFocusPriority::Gameplay);
		Enemy->UnCrouch();
		Enemy->GetCharacterMovement()->bOrientRotationToMovement = true;
		Enemy->GetCharacterMovement()->bUseControllerDesiredRotation = false;
		if (GetMoveStatus() != EPathFollowingStatus::Moving && Data.PatrolRadius > 0.f)
			if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
			{
				FNavLocation Point;
				if (Nav->GetRandomReachablePointInRadius(PatrolOrigin, Data.PatrolRadius, Point)) MoveToLocation(Point.Location, 60.f);
			}
		return;
	}
	SetFocus(Target.Get());
	Enemy->GetCharacterMovement()->bOrientRotationToMovement = false;
	Enemy->GetCharacterMovement()->bUseControllerDesiredRotation = true;
	FVector Eyes; FRotator EyeRotation;
	Enemy->GetActorEyesViewPoint(Eyes, EyeRotation);
	const bool bVisible = CanSeeFrom(Eyes);
	if (bVisible) LastSeenTime = Now;
	else if (Now - LastSeenTime > 5.f)
	{
		Enemy->OnReq_SetFireInput(false);
		Target.Reset();
		Phase = ECombatPhase::None;
		StopMovement();
		return;
	}
	if (Phase == ECombatPhase::MovingToCover || Phase == ECombatPhase::MovingToPeek)
	{
		Enemy->OnReq_SetFireInput(false);
		const FVector Destination = Phase == ECombatPhase::MovingToCover ? CoverLocation : PeekLocation;
		if (FVector::Dist2D(Enemy->GetActorLocation(), Destination) < 90.f)
		{
			StopMovement();
			if (Phase == ECombatPhase::MovingToCover)
			{
				Enemy->Crouch();
				Phase = ECombatPhase::InCover;
				PhaseEndTime = Now + Data.CoverWaitTime;
			}
			else { Phase = ECombatPhase::Attacking; PhaseEndTime = Now + Data.AttackDuration; }
		}
		else if (GetMoveStatus() != EPathFollowingStatus::Moving) Phase = ECombatPhase::None;
		return;
	}
	if (Phase == ECombatPhase::InCover)
	{
		Enemy->OnReq_SetFireInput(false);
		if (Now >= PhaseEndTime)
		{
			Enemy->UnCrouch();
			MoveToLocation(PeekLocation, 60.f);
			Phase = ECombatPhase::MovingToPeek;
		}
		return;
	}
	if (Phase == ECombatPhase::Attacking && Now >= PhaseEndTime)
	{
		Enemy->OnReq_SetFireInput(false);
		MoveToLocation(CoverLocation, 60.f);
		Phase = ECombatPhase::MovingToCover;
		return;
	}
	const float Distance = FVector::Dist(Enemy->GetActorLocation(), Target->GetActorLocation());
	if (Phase == ECombatPhase::None && Now >= NextCoverSearchTime && Distance <= Data.AttackRange)
	{
		NextCoverSearchTime = Now + 4.f;
		if (FindCover(Enemy))
		{
			Enemy->OnReq_SetFireInput(false);
			Enemy->UnCrouch();
			MoveToLocation(CoverLocation, 60.f);
			Phase = ECombatPhase::MovingToCover;
			return;
		}
	}
	if (bVisible && Distance <= Data.AttackRange)
	{
		StopMovement();
		Attack(Enemy);
	}
	else
	{
		Enemy->OnReq_SetFireInput(false);
		if (Phase == ECombatPhase::None) MoveToActor(Target.Get(), Data.AttackRange * 0.6f);
	}
}
