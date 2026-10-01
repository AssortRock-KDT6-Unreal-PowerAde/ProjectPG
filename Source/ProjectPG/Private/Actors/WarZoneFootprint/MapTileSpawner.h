#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "MapTileSpawner.generated.h"

// 공사 담당.
// 게임에서: 설계 카드대로 도로·시작 대기소·검문소·숲·워존 타일(BP)을 실제로 세우고,
//           큰 건물은 레벨 파일(창고·다운타운 등)을 불러오거나 코드로 짓고, 건물 밑 언덕·경사로를 깐다.
//           몬스터 길찾기가 벽을 뚫고 지나가지 않게 플레이어 주변 길찾기 막힘도 갱신한다.
// 세운 타일은 HISM 묶음으로 다시 모아서(그리기 비용 절약) 맵 액터에 붙인다.
UCLASS(Transient)
class UMapTileSpawner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AWarZoneFootprintPreview* InMap);
	virtual UWorld* GetWorld() const override;

	// 타일·건물 전부 세우기. 판 시작 때 한 번.
	void SpawnRuntimeBlueprintTiles();
	// 큰 건물 하나의 레벨 파일을 그 자리에 불러온다(창고·다운타운·공장 등).
	void LoadFacilityDesignLevel(const FFacilityPlacement& Placement, int32 PlacementIndex);
	// Center 둘레 Radius 안의 벽·건물로 길찾기 막힘을 다시 계산한다. 맵 Tick 이 플레이어가 칸을 옮길 때 부른다.
	void RefreshNavigationBlockerRegion(
		UTacticalTileNavModifierComponent* Modifier,
		const FVector& Center,
		float Radius);

private:
	// 언덕 위 건물(마당·막사) 밑에 단과 경사로를 깐다. SpawnRuntimeBlueprintTiles 가 맨 처음 부른다.
	void BuildElevatedFacilityTerrain();

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;
};
