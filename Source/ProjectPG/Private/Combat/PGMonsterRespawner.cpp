#include "Combat/PGMonsterRespawner.h"

#include "Combat/PGCombatSpawner.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Objects/PGObjectTypes.h"
#include "TimerManager.h"

APGMonsterRespawner::APGMonsterRespawner()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // 서버에서만 산다. 클라이언트는 리스폰된 몬스터만 본다.
}

void APGMonsterRespawner::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
		GetWorldTimerManager().SetTimer(PollTimer, this, &APGMonsterRespawner::Poll, 5.0f, true, 5.0f);
}

bool APGMonsterRespawner::IsPlayerNear(const FVector& Location) const
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->IsValid() ? It->Get()->GetPawn() : nullptr;
		if (IsValid(Pawn) && FVector::DistSquared(Pawn->GetActorLocation(), Location) < FMath::Square(PlayerClearRadius))
			return true;
	}
	return false;
}

FVector APGMonsterRespawner::PickSpareSpot(const FVector& Fallback) const
{
	TArray<FVector> Clear;
	for (const FVector& Spot : SpareSpots)
		if (!IsPlayerNear(Spot))
			Clear.Add(Spot);
	// 시드를 안 쓴다: 리스폰 시각 자체가 플레이어 행동(언제 죽였나·어디 있나)에 달려 있어 이미 결정적이지 않다.
	return Clear.Num() > 0 ? Clear[FMath::RandRange(0, Clear.Num() - 1)] : Fallback;
}

void APGMonsterRespawner::PollRobots(double Now)
{
	for (FPGRobotRespawnRecord& R : RobotRecords)
	{
		if (R.RespawnSeconds <= 0.0f)
			continue;
		const APGRobotCharacter* Alive = R.Robot.Get();
		if (IsValid(Alive) && !Alive->IsDead())
		{
			R.DeadAt = -1.0;
			continue;
		}
		if (R.DeadAt < 0.0)
		{
			R.DeadAt = Now;
			continue;
		}
		if (Now - R.DeadAt < R.RespawnSeconds)
			continue;
		const FVector Spot = PickSpareSpot(R.Location);
		if (IsPlayerNear(Spot))
			continue;
		APGRobotCharacter* Spawned = PGCombatSpawner::SpawnRobot(GetWorld(), Spot, R.Yaw, R.bBoss, R.Scale, R.Faction);
		if (!IsValid(Spawned))
			continue;
		R.Robot = Spawned;
		R.DeadAt = -1.0;
		UE_LOG(LogPGObjects, Display, TEXT("Respawner: %s robot respawned at %s after %.0fs"), R.bBoss ? TEXT("boss") : TEXT("rideable"), *Spot.ToCompactString(), R.RespawnSeconds);
	}
}

void APGMonsterRespawner::Poll()
{
	const double Now = GetWorld()->GetTimeSeconds();
	PollRobots(Now);
	for (FPGMonsterRespawnRecord& R : Records)
	{
		if (R.RespawnSeconds <= 0.0f)
			continue; // 세력 C: 리스폰 없음
		const APGMonsterCharacter* Alive = R.Monster.Get();
		if (IsValid(Alive) && !Alive->IsDead())
		{
			R.DeadAt = -1.0;
			continue;
		}
		if (R.DeadAt < 0.0)
		{
			R.DeadAt = Now;
			continue;
		}
		if (Now - R.DeadAt < R.RespawnSeconds || IsPlayerNear(R.Location))
			continue;
		APGMonsterCharacter* Spawned = PGCombatSpawner::SpawnMonster(GetWorld(), MonsterClass, R.Location, R.Yaw, R.Preset, R.Faction);
		if (!IsValid(Spawned))
			continue;
		R.Monster = Spawned;
		R.DeadAt = -1.0;
		UE_LOG(LogPGObjects, Display, TEXT("Respawner: faction %d %s respawned at %s after %.0fs"), static_cast<int32>(R.Faction), *R.Preset.ToString(), *R.Location.ToCompactString(), R.RespawnSeconds);
	}
}
