#pragma once

// 사망 통계표 전체 흐름 그림: Source/ProjectPG/Private/GameMode/README.md

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGRaidStatsSubsystem.generated.h"

class AController;
class ACustomCharacter;

// 한 판 동안 한 플레이어가 한 일. 죽으면 서버가 이 값을 그 사람 화면으로 보내 통계표(WBP_DeathStats)에 띄운다.
USTRUCT(BlueprintType)
struct FPGRaidStats
{
	GENERATED_BODY()

	// 캐릭터를 잡은 때부터 죽을 때까지 초.
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") float SurvivalSeconds = 0.f;
	// 내가 쏜 총알로 죽은 몬스터·다른 플레이어 수.
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") int32 Kills = 0;
	// 방아쇠를 당겨 실제로 나간 발 수(산탄 한 번 = 1발).
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") int32 ShotsFired = 0;
	// 맞힌 횟수(산탄은 맞은 알갱이마다 1).
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") int32 Hits = 0;
	// 실제로 깎은 체력 합(이미 0 인 대상에 쏜 것은 빼고).
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") float DamageDealt = 0.f;
	// 내가 잃은 체력 합.
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") float DamageTaken = 0.f;
	// 마지막으로 나를 맞힌 것의 이름(몬스터면 "몬스터", 사람이면 그 사람 이름). 모르면 비어 있음.
	UPROPERTY(BlueprintReadOnly, Category="RaidStats") FString KilledBy;
};

// 서버에서 판마다 플레이어별 기록을 모은다. 클라이언트에는 아무것도 없다(죽을 때 결과만 RPC 로 받음).
// 누가 부르나:
// - PlayerController_InGame: 캐릭터를 잡을 때 StartLife, 죽었을 때 FinishLife
// - FirearmEquipment::Fire: 한 발 쏠 때 RecordShot, 무언가를 맞힐 때 RecordHit (몬스터·플레이어 모두 이 한 곳을 지난다)
UCLASS()
class PROJECTPG_API UPGRaidStatsSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGRaidStatsSubsystem* Get(const UObject* WorldContext);

	// 서버: 이 사람의 기록을 새로 시작한다(생존 시간 시계 켬).
	void StartLife(AController* Player);
	// 서버: 이 사람이 방금 죽었다 → 생존 시간을 여기서 멈춘다.
	// 기록은 아직 열어 둔다. 왜: 죽인 총알의 피해·"누구에게" 는 죽음 알림 다음에 기록되기 때문.
	void MarkDied(AController* Player);
	// 서버: 이 사람의 기록을 닫고 돌려준다(통계표를 보낼 때). 기록이 없으면 빈 값.
	FPGRaidStats FinishLife(AController* Player);

	// 서버: Shooter 가 한 발 쐈다. 플레이어가 아니면 무시.
	void RecordShot(const ACustomCharacter* Shooter);
	// 맞히기 직전 대상의 체력(RecordHit 에 넘길 값). 체력이 없는 대상이면 0.
	static float GetHealthBeforeHit(const AActor* Target);
	// 서버: Shooter 의 총알이 Target 을 맞혔다. 맞히기 전 체력과 지금 체력을 비교해 피해·처치를 센다.
	void RecordHit(const ACustomCharacter* Shooter, const AActor* Target, float HealthBefore);

private:
	struct FLife
	{
		FPGRaidStats Stats;
		double StartTime = 0.0;
		double DeathTime = -1.0;
		bool bFinished = false;
	};
	TMap<TWeakObjectPtr<AController>, FLife> Lives;

	// 플레이어가 조종하는 캐릭터면 그 기록, 아니면(몬스터 등) nullptr.
	FLife* FindLife(const ACustomCharacter* Character);
};
