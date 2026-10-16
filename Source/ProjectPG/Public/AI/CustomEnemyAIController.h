#pragma once

#include "AIController.h"
#include "CustomEnemyAIController.generated.h"

UCLASS()
class PROJECTPG_API ACustomEnemyAIController : public AAIController
{
	GENERATED_BODY()
private:
	enum class ECombatPhase : uint8 { None, MovingToCover, InCover, MovingToPeek, Attacking };
	TWeakObjectPtr<class ACustomPlayerCharacter> Target;
	FVector PatrolOrigin = FVector::ZeroVector;
	FVector CoverLocation = FVector::ZeroVector;
	FVector PeekLocation = FVector::ZeroVector;
	FTimerHandle DecisionTimer;
	ECombatPhase Phase = ECombatPhase::None;
	double PhaseEndTime = 0.;
	double NextCoverSearchTime = 0.;
	double LastSeenTime = 0.;
protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void UpdateCombat();
	bool CanSeeFrom(const FVector& Location) const;
	bool FindCover(const class ACustomEnemyCharacter* Enemy);
	void Attack(class ACustomEnemyCharacter* Enemy);
};
