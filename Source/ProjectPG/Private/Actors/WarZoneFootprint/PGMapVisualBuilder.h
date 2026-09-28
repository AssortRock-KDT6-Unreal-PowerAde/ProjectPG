// 맵 그리기 — 바닥·도로·호수 묶음(HISM), 풀 장식(PCG), 맵 경계(보이지 않는 벽·산·둘레 바닥판).
// 2026-09-26 SOLID(한 책임): 맵 액터(AWarZoneFootprintPreview)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 맵 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 맵 액터에 그대로 두고 Map-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 맵 액터의 비공개 멤버를 읽어야 해서 맵 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "PGMapVisualBuilder.generated.h"

// BuildLightweightWorldVisuals 가 칸마다 모으는 땅판·도로·호수 트랜스폼(9/28 단계로 나누며 한 묶음으로).
struct FPGGroundBatches
{
	TArray<FTransform> GroundTransforms;
	TArray<FTransform> WarZoneGroundTransforms;
	TArray<FTransform> TransitionGroundTransforms;
	TArray<FTransform> RoadTransforms;
	TArray<FTransform> LakeBedTransforms;
	TArray<FTransform> LakeWaterTransforms;
	FIntPoint LakeSheetMin = FIntPoint(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint LakeSheetMax = FIntPoint(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	int32 LakeWaterCellCount = 0;
	int32 RoadTileCount = 0;
};

UCLASS(Transient)
class UPGMapVisualBuilder : public UObject
{
	GENERATED_BODY()

public:
	void Init(AWarZoneFootprintPreview* InMap) { Map = InMap; }
	virtual UWorld* GetWorld() const override { return Map ? Map->GetWorld() : nullptr; }

	void BuildLightweightWorldVisuals();
	// 맵 밖으로 못 나가게 막는 보이지 않는 벽. 안개는 가리기만 하지 막지는 못한다.
	// 맵 둘레 바닥판(치마). 산과 따로 부른다 — cpp 주석 참고.
	void BuildGroundSkirt();
	void BuildMapBoundaryWall();
	void BuildPCGDressingGraph();
	void BuildBorderMountains();
	// 경계 밖으로 나간 플레이어를 안쪽으로 되민다.
	void EnforceMapBoundary();

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;

	// 경계 상자. BuildMapBoundaryWall 이 채우고 Tick 이 읽는다.
	FVector2D BoundaryMinXY = FVector2D::ZeroVector;
	FVector2D BoundaryMaxXY = FVector2D::ZeroVector;
	float BoundaryTopZ = 0.0f;
	bool bBoundaryReady = false;
	// BuildLightweightWorldVisuals 의 단계(9/28 떼어 냄).
	void AddCellGroundSlabs(FPGGroundBatches& Batches, const TSet<FIntPoint>& FacilityCells, const float TerrainSolidBottomZ,
		const TMap<FIntPoint, TPair<int32, int32>>& ShoreTileByCell, const FIntPoint& VisualWarZoneCenter);
	void AddFacilityTerrainPads(const float TerrainSolidBottomZ, TArray<FTransform>& GroundTransforms, TArray<FTransform>& WarZoneGroundTransforms);
	void BuildShoreDressing(TArray<FTransform>& ShoreRockTransforms, TArray<FTransform>& ShoreReedTransforms);
	int32 PlaceShoreTransitionMeshes(const TMap<FIntPoint, TPair<int32, int32>>& ShoreTileByCell);
	UPROPERTY(Transient)
	TObjectPtr<UPCGGraph> RuntimeDressingGraph;
};
