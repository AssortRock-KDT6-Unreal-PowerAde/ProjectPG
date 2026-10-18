// 타일 맵에서 몬스터와 싸워 보는 시험 도구. 개발 빌드의 서버에서만 일한다(출시 빌드에서는 아무것도 안 함).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGTestBattleSubsystem.generated.h"

class APlayerController;

// 시험용 전투 준비 담당.
// 왜 있나: 타일 맵(MapBuilder)에는 아직 몬스터를 놓는 기능이 없고, 웹 서버가 없으면 플레이어 짐도 비어 총이 없다.
//   그래서 몬스터 외침·비명·동료 부르기를 타일 맵에서 들어 보려면 시험할 때만 몬스터와 총을 넣어 줘야 한다.
// 명령줄(서버에 준다, 전부 있어야 몬스터가 나옴):
//   -PGTestEnemies=4                                   플레이어마다 둘레에 놓을 몬스터 수
//   -PGTestEnemyClass=/Game/.../CBP_X.CBP_X_C          놓을 몬스터 BP 클래스(코드에 경로를 박지 않으려고 명령줄로 받음)
//   -PGTestRifle=1010                                  플레이어 손에 쥐어 줄 총 아이템 번호(ItemTable 행 이름)
// 서버는 GM_InGame 게임모드로 띄워야 한다(그래야 짐을 서버가 관리해 총이 모두에게 보임).
// 실행 예: Tools/test/EnemyVoiceTileTest.ps1
UCLASS()
class UPGTestBattleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

private:
	// 0.5초마다: 새로 들어온 사람에게 총을 주고, 맵이 준비됐으면 아직 몬스터를 안 받은 사람 둘레에 놓는다.
	void Check();
	void GiveRifle(APlayerController* Controller);
	// 그 사람 몸에 "내 둘레에 길(네비)을 만들어라" 부품을 붙인다.
	// 왜: 타일 맵은 길을 맵 가운데 120m 안에서만 만드는데, 시작 구역은 가장자리라 몬스터가 놓일 길이 없다.
	void AddNavInvoker(APawn* Pawn);
	// 그 사람 둘레에 몬스터를 놓는다. 다 놓았거나 기다리다 포기했으면 true, 아직 길이 덜 생겼으면 false(다음에 다시).
	bool SpawnEnemiesAround(APlayerController* Controller);

	int32 EnemiesPerPlayer = 0;
	FName RifleItemID;
	UPROPERTY(Transient)
	TSubclassOf<APawn> EnemyClass;

	TSet<TWeakObjectPtr<APlayerController>> RifleGiven;
	TSet<TWeakObjectPtr<APlayerController>> EnemiesPlaced;
	// 사람마다 "둘레에 길이 없어 못 놓음" 이 몇 번 났는지(시간 제한용).
	TMap<TWeakObjectPtr<APlayerController>, int32> NavMisses;
	// 맵이 준비된 서버 시각. 플레이어를 시작 자리에 옮길 틈을 조금 준 뒤에 몬스터를 놓는다.
	double MapReadyTime = -1.0;
	double StartTime = 0.0;
	bool bWarnedNotServerManaged = false;
	bool bGaveUpWaitingForMap = false;
	FTimerHandle CheckTimer;
};
