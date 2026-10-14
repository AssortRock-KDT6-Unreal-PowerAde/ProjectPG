// 코드로 만드는 여러 칸짜리 시설. 절차 맵의 화면 단계가 쓴다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralFacilityActor.generated.h"

class UArrowComponent;
class UHierarchicalInstancedStaticMeshComponent;
class USceneComponent;

UENUM(BlueprintType)
enum class EProceduralFacilityKind : uint8
{
	IndustrialRaid3x3,
	SatelliteCamp2x2,
	LongBarracks2x1,
	LinearTrench4x1,
	DowntownBlock3x3,
	FactoryConstruction2x2,
	RuralHideout2x2
};

UCLASS()
class PROJECTPG_API AProceduralFacilityActor : public AActor
{
	GENERATED_BODY()

public:
	AProceduralFacilityActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	void Configure(EProceduralFacilityKind InKind, int64 InSeed, uint8 InVariant);
	FIntPoint GetFootprintCells() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility")
	EProceduralFacilityKind FacilityKind = EProceduralFacilityKind::IndustrialRaid3x3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility")
	int64 LocalSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Facility", meta = (ClampMin = "0", ClampMax = "3"))
	uint8 LayoutVariant = 0;

private:
	void Rebuild();
	void BuildIndustrialRaid();
	void BuildSatelliteCamp();
	void BuildLongBarracks();
	void BuildLinearTrench();
	void BuildDowntownBlock();
	void BuildFactoryConstruction();
	void BuildRuralHideout();
	void AddFloorRect(float HalfX, float HalfY, float Z, float ModuleSize = 1000.0f);
	void AddRoofRect(const FVector& Center, float HalfX, float HalfY, float Z, float ModuleSize = 1000.0f);
	void AddWallRun(const FVector& Start, const FVector& End, float Z, bool bDoorGap, bool bWindows);
	void AddRailingRun(const FVector& Start, const FVector& End, float Z);
	// AddCover/AddContainer 에서 Location.Z 는 소품이 서는 바닥 면이다.
	FVector RestOnSurface(
		const UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& SurfaceLocation,
		const FVector& Scale) const;
	void AddCover(const FVector& Location, const FVector& Scale, float Yaw = 0.0f);
	void AddContainer(const FVector& Location, float Yaw, const FVector& Scale = FVector(0.65f));
	// Location 은 첫 계단이 놓이는 곳, RiseCm 은 올라가야 할 높이다.
	void AddStair(const FVector& Location, float Yaw, float RiseCm);
	float GetStairTopOffsetCm() const;
	void AddIndustrialWorkCell(const FVector& Center, float Yaw, uint8 Variant);
	void ConfigureMeshComponent(UHierarchicalInstancedStaticMeshComponent* Component, bool bCollision, bool bNavigation);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Floors;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> SolidWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WindowWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Roofs;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Stairs;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Covers;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Containers;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> Railings;

	// 3x3 WarZone 시설이 쓰는 진짜 Factory Pack 랜드마크. 게임플레이 통로는
	// 여전히 CQB 모듈 조각이 정하고, 이 컴포넌트들은 큰 공업 실루엣과
	// 알아보기 쉬운 POI 느낌을 준다.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryHalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryChimneys;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryTanks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryFences;

	// Downtown West 모듈을 먼 배경 풍경으로 쓰지 않고,
	// 알아보기 쉽고 들어갈 수 있는 거리 블록으로 짓는다.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DowntownStorefronts;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DowntownUpperWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DowntownStreetlights;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DowntownPlanters;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> DowntownBenches;

	// Rural Cabin 모듈을 일부러 문을 낸 작은 실내로 조립한다.
	// 소품 덕분에 나중에 전리품 자리를 두기 좋은 구역이 된다.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralWindowWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralDoorWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralRoofs;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralCaravans;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralFences;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RuralPicnicTables;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryCranes;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactorySiteHouses;

	// 작은 Factory Pack 모듈이 타일로 만든 큰 건물의 실내를 마무리한다.
	// 매번 똑같은 HISM 묶음으로 두면 manifest 약속을 지키면서
	// 시설마다 소품 액터 수십 개가 생기는 걸 피한다.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryPipes;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryPallets;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryBarrels;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryWorkTables;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryDoors;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryPowerBoxes;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryLamps;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> EntranceNorth;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> EntranceSouth;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> LootSocket;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> UpperLootSocket;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> AISocketA;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UArrowComponent> AISocketB;
};
