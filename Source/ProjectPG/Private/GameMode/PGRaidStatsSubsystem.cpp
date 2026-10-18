// 사망 통계표 전체 흐름 그림: Source/ProjectPG/Private/GameMode/README.md

#include "GameMode/PGRaidStatsSubsystem.h"
#include "Characters/CustomCharacter.h"
#include "Characters/CustomEnemyCharacter.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Engine/World.h"

UPGRaidStatsSubsystem* UPGRaidStatsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UPGRaidStatsSubsystem>() : nullptr;
}

void UPGRaidStatsSubsystem::StartLife(AController* Player)
{
	if (!Player || !Player->HasAuthority()) return;
	FLife& Life = Lives.FindOrAdd(Player);
	Life = FLife();
	Life.StartTime = GetWorld()->GetTimeSeconds();
}

void UPGRaidStatsSubsystem::MarkDied(AController* Player)
{
	FLife* Life = Lives.Find(Player);
	if (Life && Life->DeathTime < 0.0) Life->DeathTime = GetWorld()->GetTimeSeconds();
}

FPGRaidStats UPGRaidStatsSubsystem::FinishLife(AController* Player)
{
	FLife* Life = Lives.Find(Player);
	if (!Life) return FPGRaidStats();
	if (!Life->bFinished)
	{
		Life->bFinished = true;
		const double EndTime = Life->DeathTime >= 0.0 ? Life->DeathTime : GetWorld()->GetTimeSeconds();
		Life->Stats.SurvivalSeconds = static_cast<float>(EndTime - Life->StartTime);
	}
	return Life->Stats;
}

UPGRaidStatsSubsystem::FLife* UPGRaidStatsSubsystem::FindLife(const ACustomCharacter* Character)
{
	// 플레이어 컨트롤러가 잡은 캐릭터만 센다. 몬스터(AI 컨트롤러)는 기록이 없어 nullptr.
	APlayerController* Player = Character ? Cast<APlayerController>(Character->GetController()) : nullptr;
	FLife* Life = Player ? Lives.Find(Player) : nullptr;
	return Life && !Life->bFinished ? Life : nullptr;
}

void UPGRaidStatsSubsystem::RecordShot(const ACustomCharacter* Shooter)
{
	if (FLife* Life = FindLife(Shooter)) ++Life->Stats.ShotsFired;
}

float UPGRaidStatsSubsystem::GetHealthBeforeHit(const AActor* Target)
{
	const ACustomCharacter* Character = Cast<ACustomCharacter>(Target);
	const UCharacterAttributeSet* Attributes = Character ? Character->GetCharacterAttributeSet() : nullptr;
	return Attributes ? Attributes->GetHealth() : 0.f;
}

void UPGRaidStatsSubsystem::RecordHit(const ACustomCharacter* Shooter, const AActor* Target, float HealthBefore)
{
	const ACustomCharacter* Victim = Cast<ACustomCharacter>(Target);
	if (!Victim) return;
	// 실제로 깎인 체력만 센다(이미 죽은 몸, 0 피해는 0).
	const float Lost = FMath::Max(0.f, HealthBefore - GetHealthBeforeHit(Victim));
	const bool bKilled = HealthBefore > 0.f && Victim->IsDead();

	if (FLife* ShooterLife = FindLife(Shooter))
	{
		++ShooterLife->Stats.Hits;
		ShooterLife->Stats.DamageDealt += Lost;
		if (bKilled) ++ShooterLife->Stats.Kills;
	}
	if (FLife* VictimLife = FindLife(Victim))
	{
		VictimLife->Stats.DamageTaken += Lost;
		if (Lost > 0.f)
		{
			// 나를 맞힌 것 이름: 몬스터면 "몬스터", 사람이면 그 사람 이름(PlayerState 이름).
			const APlayerState* ShooterState = Shooter ? Shooter->GetPlayerState() : nullptr;
			VictimLife->Stats.KilledBy = Shooter && Shooter->IsA<ACustomEnemyCharacter>() ? TEXT("몬스터")
				: ShooterState ? ShooterState->GetPlayerName() : FString();
		}
	}
}
