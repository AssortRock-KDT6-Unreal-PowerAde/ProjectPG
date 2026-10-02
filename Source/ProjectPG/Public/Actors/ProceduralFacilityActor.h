// Code-authored multi-cell facility used by the procedural map visual layer.

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
	// For AddCover/AddContainer, Location.Z is the surface the prop stands on.
	FVector RestOnSurface(
		const UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& SurfaceLocation,
		const FVector& Scale) const;
	void AddCover(const FVector& Location, const FVector& Scale, float Yaw = 0.0f);
	void AddContainer(const FVector& Location, float Yaw, const FVector& Scale = FVector(0.65f));
	// Location is where the bottom step rests; RiseCm is the height it must climb.
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

	// Real Factory Pack landmarks used by the 3x3 WarZone facility.  The modular
	// CQB pieces still define gameplay lanes; these components provide the large
	// industrial silhouette and readable POI identity.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryHalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryChimneys;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryTanks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> FactoryFences;

	// Downtown West modules form readable, enterable street blocks instead of
	// treating the pack as distant background scenery.
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

	// Rural Cabin modules are assembled into small interiors with intentional
	// doors; the props make the district suitable for later loot sockets.
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

	// Small Factory Pack modules finish the interiors of the large tile-authored
	// shells. Keeping them in deterministic HISM batches preserves the manifest
	// contract while avoiding dozens of individual prop actors per facility.
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
