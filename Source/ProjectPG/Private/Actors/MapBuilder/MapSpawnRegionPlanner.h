#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/MapBuilder.h"
#include "MapSpawnRegionPlanner.generated.h"

// 시작 구역 담당 (멀티 최대 4명).
// 게임에서: 형님 맵 생성기는 시작 칸을 딱 1개만 만든다. 4명이 한 곳에서 출발하면 나오자마자 싸움이 난다.
//           그래서 맵 가장자리에서 서로 멀리 떨어진 빈 땅을 3곳 더 골라, 벽 친 시작 대기소로 만든다.
// 형님 칸 쪽지(AMapTile)는 안 바꾼다. 우리 설계 카드(TileDesignPlacements)만 바꾼다. 출구 칸을 가져다 쓰지 않으니 출구는 안 줄어든다.
// ProjectTest2 의 WarZoneFootprintPreview_SpawnRegions.cpp(9/28) 를 옮긴 것. 플레이어를 구역에 나눠 세우는 일은 다음 단계.
UCLASS(Transient)
class UMapSpawnRegionPlanner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 시작 구역 고르기. 흙길 담당이 시설 흙길을 깐 직후, 시작점·출구 흙길을 깔기 전에 부른다.
	// 왜 그때: 고른 칸에서 워존까지 흙길을 "시작점·출구 → 워존" 과 같은 방법으로 깔아야 해서.
	// BlockedCells = 건물·언덕·이미 흙길인 칸(여기는 고르지 않는다). 결과는 Map->SpawnRegionCells(0번 = 형님 시작 칸).
	void PickSpawnRegions(
		const TMap<FIntPoint, AMapTile*>& TileByCell,
		const TSet<FIntPoint>& BlockedCells,
		int64 RaidSeed);

	// 추가로 고른 칸만(1번부터). 흙길 담당이 "여기 지나가지 마/여기서도 워존까지 길 깔아" 에 쓴다.
	TSet<FIntPoint> GetExtraSpawnCells() const;

	// 고른 칸의 설계 카드를 "시작 대기소" 로 바꾼다. 칸 모양 담당이 지문(LayoutHash)을 다 계산한 뒤 부른다.
	// 담장의 유일한 입구가 워존 가는 흙길 쪽을 보게 돌린다.
	void DressExtraSpawnRegions(const FIntPoint& MinCell, const FIntPoint& MaxCell);

private:
	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
};
