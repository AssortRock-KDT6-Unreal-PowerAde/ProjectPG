#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/MapBuilder.h"
#include "MapTilePlanner.generated.h"

// 칸 모양 담당.
// 게임에서: 30×30 칸 하나하나가 "꺾인 길 90도", "벽 친 시작 대기소", "울창한 숲", "호수" 처럼
//           어떤 타일로 보일지를 정한다. 실제로 세우는 건 공사 담당(SpawnRuntimeBlueprintTiles)이 한다.
// 결과는 맵의 TileDesignPlacements(칸마다 설계 카드)와 LayoutHash(지문)에 들어간다.
UCLASS(Transient)
class UMapTilePlanner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 칸 설계 카드 전부 만들기. 건물 자리(FacilityPlacements)가 먼저 정해져 있어야 한다.
	void BuildTileDesignPlacements(const TMap<FIntPoint, AMapTile*>& TileByCell);

private:
	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
};
