#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/MapBuilder.h"
#include "MapEnemySpawner.generated.h"

// 몬스터 담당 (서버만). 흐름 그림: 이 폴더 README.md
// 게임에서: 맵이 시설·상자까지 다 짓고 길찾기 지도도 다 구워지면 준비 끝.
//           그때부터 플레이어가 한 명 들어올 때마다 워존 한가운데 근처(CenterSpawnRadiusCm 안)에 EnemiesPerPlayer 마리씩 놓는다.
//           (지도 전에 이미 들어온 사람 몫은 준비 끝 순간에 놓음. 2026-10-19 사용자: 중앙 근처에 한 명당 4마리 정도)
//           놓인 몬스터는 스스로 순찰하다가 플레이어를 보면 외치고, 외침을 들은 동료가 모여든다(Private/AI/README.md).
// 왜 지도까지 기다리나: 지도 전에는 몬스터가 한 발짝도 못 움직이고, 담·지붕 꼭대기 같은 잘못된 자리를 지도로 걸러 내서.
// 몇 마리·어떤 몬스터·시작 구역과의 거리는 DA_MapAssets 의 "몬스터" 칸(코드·빌드 없이 바꾼다).
// 같은 시드면 같은 자리에 같은 수가 나온다(판 시드로만 주사위를 굴림).
// 클라이언트는 아무것도 안 한다. 서버가 놓은 몬스터를 자동으로 전달(복제)받는다.
UCLASS(Transient)
class UMapEnemySpawner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 맵이 상자까지 놓은 뒤 한 번 부른다(AMapBuilder::CheckFacilityDesignLevels).
	// - 지도가 이미 다 구워짐 → 바로 놓음
	// - 아직 → "다 구웠다" 알림(OnNavigationReady)이 오면 놓음. 정한 시간이 지나도 안 오면 Error 로그 후 계속 기다림
	void SpawnWhenNavigationReady();

private:
	void HandleNavigationReady();
	void HandleNavigationWaitTimeout();
	// 준비 끝: 몬스터 BP·중앙 칸을 정하고, 이미 들어온 사람 몫을 놓는다. 판마다 한 번만.
	void SpawnEnemiesOnce();
	// 누가 들어옴(서버) → 준비가 끝났으면 그 사람 몫을 놓는다.
	void HandlePlayerJoined(APlayerController* Player);
	// 이 사람 몫(EnemiesPerPlayer 마리)을 중앙 칸에 놓는다. 사람마다 한 번만, 판 전체 MaxEnemies 를 넘지 않음.
	void SpawnForPlayer(APlayerController* Player);
	// 워존 칸들의 한가운데에서 CenterSpawnRadiusCm 안의 칸(가까운 순).
	TArray<FIntPoint> CollectCenterCells() const;
	// 몬스터 BP 목록을 읽는다. 못 읽는 칸은 빼고 Error 로그.
	void LoadEnemyClasses();
	// 놓을 자리 순서: 시작 구역마다 가까운 자리 몇 곳 먼저, 나머지는 섞어서. 시작 구역에 너무 가까운 자리는 뺀다.
	TArray<const FLevelDesignPoint*> PickPoints(FRandomStream& Stream, int32& OutTooCloseCount) const;
	// 자리 둘레에서 몬스터 한 마리가 설 길 위 점을 찾는다. Index/Count 로 같은 자리 몬스터끼리 둘레에 나눠 선다.
	bool FindSpot(const FLevelDesignPoint& Point, int32 Index, int32 Count, FRandomStream& Stream, FVector& OutFloor) const;
	// 워존 칸(칸 종류 WarZone) 번호 모음 / 이 자리가 워존 칸 안인가.
	TSet<FIntPoint> CollectWarZoneCells() const;
	bool IsInWarZone(const FVector& Location, const TSet<FIntPoint>& WarZoneCells) const;
	// 몬스터 자리 목록을 앞에서부터 돌며 최대 Limit 마리까지 놓고, 놓은 수를 돌려준다.
	struct FSpawnTally
	{
		int32 UsedPoints = 0;
		int32 NoFloorCount = 0;
	};
	int32 SpawnAtPoints(const TArray<const FLevelDesignPoint*>& Points, int32 Limit, FRandomStream& Stream, FSpawnTally& Tally);
	// 주어진 칸들 바닥의 길 위 점을 골라 Need 마리까지 놓는다.
	int32 SpawnOnCells(const TArray<FIntPoint>& WarZoneCells, int32 Need, FRandomStream& Random);
	// 바닥 점 위에 몬스터 한 마리를 놓는다(BP 는 목록에서 무작위).
	bool SpawnEnemyAt(const FVector& Floor, FRandomStream& Stream);

	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
	// DA_MapAssets 에서 읽은 몬스터 BP 중 실제로 찾은 것.
	UPROPERTY()
	TArray<TObjectPtr<UClass>> EnemyClasses;

	bool bWaiting = false;
	// 지도가 구워져 몬스터를 놓을 수 있게 됨.
	bool bReady = false;
	FRandomStream RandomStream;
	TArray<FIntPoint> CenterCells;
	TSet<TWeakObjectPtr<APlayerController>> PlayersWithEnemies;
	int32 TotalSpawned = 0;
	FDelegateHandle NavigationReadyHandle;
	FTimerHandle NavigationWaitTimer;
};
