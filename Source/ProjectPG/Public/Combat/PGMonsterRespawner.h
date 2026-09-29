// 세력별 리스폰 (기획 v0.4 3.3.5).
//  세력 A: 플레이어가 주위에 없으면 짧은 간격으로 리스폰
//  세력 B: 긴 간격으로 리스폰
//  세력 C: 세션 동안 리스폰 없음 (RespawnSeconds 0)
// 스포너가 몬스터를 놓을 때 자리·프리셋·세력을 여기 기록해 두고, 몇 초에 한 번 죽은 자리를 다시 채운다.
// 플레이어 눈앞에서 튀어나오지 않도록 자리 주변 PlayerClearRadius 안에 플레이어가 있으면 기다린다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Monster/PGMonsterCharacter.h"
#include "Robot/PGRobotCharacter.h"
#include "PGMonsterRespawner.generated.h"

USTRUCT()
struct FPGMonsterRespawnRecord
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	float Yaw = 0.0f;
	FName Preset;
	EPGMonsterFaction Faction = EPGMonsterFaction::None;
	float RespawnSeconds = 0.0f;
	TWeakObjectPtr<APGMonsterCharacter> Monster;
	double DeadAt = -1.0;
};

// 보스·탈것 로봇. 몬스터와 달리 원래 자리가 아니라 SpareSpots(비어 있는 AI 포인트) 중 플레이어에서 먼 곳에 랜덤으로 나온다.
// 왜: 보스 자리는 워존 한가운데라 플레이어가 그 근처에 계속 있고, 탈것은 "찾아다니는" 재미가 있어야 해서.
USTRUCT()
struct FPGRobotRespawnRecord
{
	GENERATED_BODY()

	FVector Location = FVector::ZeroVector;
	float Yaw = 0.0f;
	bool bBoss = false;
	float Scale = 1.0f;
	EPGMonsterFaction Faction = EPGMonsterFaction::None;
	float RespawnSeconds = 0.0f;
	TWeakObjectPtr<APGRobotCharacter> Robot;
	double DeadAt = -1.0;
};

UCLASS()
class PROJECTPG_API APGMonsterRespawner : public AActor
{
	GENERATED_BODY()

public:
	APGMonsterRespawner();

	void Configure(UClass* InMonsterClass, float InPlayerClearRadius) { MonsterClass = InMonsterClass; PlayerClearRadius = InPlayerClearRadius; }
	void Add(const FPGMonsterRespawnRecord& Record) { Records.Add(Record); }
	void AddRobot(const FPGRobotRespawnRecord& Record) { RobotRecords.Add(Record); }
	void SetSpareSpots(const TArray<FVector>& Spots) { SpareSpots = Spots; }
	int32 NumRecords() const { return Records.Num() + RobotRecords.Num(); }

protected:
	virtual void BeginPlay() override;
	void Poll();
	void PollRobots(double Now);
	bool IsPlayerNear(const FVector& Location) const;
	// SpareSpots 중 플레이어가 근처에 없는 곳을 랜덤으로. 없으면 Fallback.
	FVector PickSpareSpot(const FVector& Fallback) const;

	UPROPERTY()
	TObjectPtr<UClass> MonsterClass;

	float PlayerClearRadius = 3000.0f;
	TArray<FPGMonsterRespawnRecord> Records;
	TArray<FPGRobotRespawnRecord> RobotRecords;
	TArray<FVector> SpareSpots;
	FTimerHandle PollTimer;
};
