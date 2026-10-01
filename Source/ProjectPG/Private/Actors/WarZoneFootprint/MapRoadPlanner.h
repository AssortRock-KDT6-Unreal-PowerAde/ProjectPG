#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "MapRoadPlanner.generated.h"

// 흙길 담당.
// 게임에서: 형님 맵 생성기가 깐 도로만으로는 시설 입구나 시작점이 길에서 떨어져 있을 때가 있다.
//           그 사이를 흙길로 이어서 "길만 따라가면 건물·워존·출구에 닿는" 맵을 만든다.
// 형님 쪽지(AMapTile)의 칸 종류는 바꾸지 않는다. 흙길은 우리 쪽(보이는 맵)에만 있는 칸이다.
UCLASS(Transient)
class UMapRoadPlanner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AWarZoneFootprintPreview* InMap);
	virtual UWorld* GetWorld() const override;

	// 흙길 칸 정하기. 입력: 칸 쪽지, 줄 세운 칸 목록, 이미 건물·언덕이 차지한 칸, 판 시드.
	// 출력: OutRoadCells(흙길이 될 칸), OutRoadMasks(칸마다 이어진 방향 N=1,E=2,S=4,W=8).
	void PlanAccessRoads(
		const TMap<FIntPoint, AMapTile*>& TileByCell,
		const TArray<FIntPoint>& SortedCells,
		const TSet<FIntPoint>& ReservedCells,
		int64 RaidSeed,
		TSet<FIntPoint>& SupplementalRoadCells,
		TMap<FIntPoint, uint8>& SupplementalRoadMasks);

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;
};
