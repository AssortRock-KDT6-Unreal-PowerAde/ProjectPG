#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/MapBuilder.h"
#include "MapEnemySpawner.generated.h"

// 몬스터 담당 (서버만). 흐름 그림: 이 폴더 README.md
// 게임에서: 맵이 시설·상자까지 다 짓고 길찾기 지도도 다 구워지면, 몬스터 자리(AISpawn 지점)마다 몬스터를 한 번 놓는다.
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
	// 실제로 놓기. 판마다 한 번만.
	void SpawnEnemiesOnce();
	// 몬스터 BP 목록을 읽는다. 못 읽는 칸은 빼고 Error 로그.
	void LoadEnemyClasses();
	// 놓을 자리 순서: 시작 구역마다 가까운 자리 몇 곳 먼저, 나머지는 섞어서. 시작 구역에 너무 가까운 자리는 뺀다.
	TArray<const FLevelDesignPoint*> PickPoints(FRandomStream& Stream, int32& OutTooCloseCount) const;
	// 자리 둘레에서 몬스터 한 마리가 설 길 위 점을 찾는다. Index/Count 로 같은 자리 몬스터끼리 둘레에 나눠 선다.
	bool FindSpot(const FLevelDesignPoint& Point, int32 Index, int32 Count, FRandomStream& Stream, FVector& OutFloor) const;

	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
	// DA_MapAssets 에서 읽은 몬스터 BP 중 실제로 찾은 것.
	UPROPERTY()
	TArray<TObjectPtr<UClass>> EnemyClasses;

	bool bWaiting = false;
	bool bSpawned = false;
	FDelegateHandle NavigationReadyHandle;
	FTimerHandle NavigationWaitTimer;
};
