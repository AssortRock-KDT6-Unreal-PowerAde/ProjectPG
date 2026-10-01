#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "MapFacilityPlanner.generated.h"

// 건물 자리 담당.
// 게임에서: 판이 시작되면 워존 한가운데 공장 단지, 둘레의 마당·막사·참호, 도로 위 검문소,
//           바깥쪽 다운타운·공장·호숫가 마을이 어디에 설지를 정한다.
// 왜 따로 뺐나: 맵 클래스가 "칸 읽기 → 건물 자리 → 타일 → 바닥 → 공사 → 지점 → 검사" 를 혼자 다 해서 너무 컸다.
//               고른 결과(FacilityPlacements)는 다른 일꾼도 읽으니 맵에 두고, 고르는 일만 여기서 한다.
UCLASS(Transient)
class UMapFacilityPlanner : public UObject
{
	GENERATED_BODY()
public:
	// 맵 주소를 받아 둔다. 맵 BeginPlay 에서 만들자마자 부른다.
	void Init(AWarZoneFootprintPreview* InMap);
	// 레벨에 시드 묻기(GameMode)에 월드가 필요 → 맵 액터의 월드를 빌려 쓴다.
	virtual UWorld* GetWorld() const override;

	// 건물 자리를 전부 고른다. 워존이 너무 작아 공장 단지가 안 들어가면 false(맵이 다음에 다시 부름).
	// TileByCell: 칸 번호 → 형님 쪽지(AMapTile). WarZoneByCell: 그중 워존 칸만.
	bool PlanFacilities(
		const TMap<FIntPoint, AMapTile*>& TileByCell,
		const TMap<FIntPoint, AMapTile*>& WarZoneByCell,
		int32 AllTileCount);

	// 맵의 마지막 로그용 값.
	FIntPoint GetCompoundAnchor() const { return CompoundAnchorResult; }
	int32 GetCampCount() const { return CampCountResult; }
	bool FoundCheckpoint() const { return bFoundCheckpointResult; }

private:
	void ReserveFacility(
		EFacilityVisualSet VisualSet,
		const FIntPoint& Anchor,
		const FIntPoint& Footprint,
		int32 RotationQuarterTurns,
		const TArray<FIntPoint>& OccupiedCells,
		const TMap<FIntPoint, AMapTile*>& TileByCell);

	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;

	// "워존이 작아서 못 지음" 에러를 한 번만 찍으려는 스티커. 이 일꾼만 읽는다.
	bool bLoggedMissingWarZoneFootprint = false;
	FIntPoint CompoundAnchorResult = FIntPoint::ZeroValue;
	int32 CampCountResult = 0;
	bool bFoundCheckpointResult = false;
};
