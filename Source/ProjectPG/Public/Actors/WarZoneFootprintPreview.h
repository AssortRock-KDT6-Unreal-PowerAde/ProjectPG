// Visual layer for the procedural map: turns the logical AMapTile grid into tiles, facilities and the border lake.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WarZoneFootprintPreview.generated.h"

class AMapTile;
class ANavigationData;
class ATacticalTileActor;
class UHierarchicalInstancedStaticMeshComponent;
class ULevelStreamingDynamic;
class UNavigationInvokerComponent;
class UPCGComponent;
class UPCGGraph;
class UStaticMeshComponent;
class UTacticalTileNavModifierComponent;
class UWorld;

UENUM(BlueprintType)
enum class EFacilityVisualSet : uint8
{
	Warehouse,
	Yard,
	LongBarracks,
	LinearTrench,
	DowntownBlock,
	FactoryConstruction,
	RuralHideout,
	Checkpoint
};

UENUM(BlueprintType)
enum class EFacilityElevationProfile : uint8
{
	Ground,
	RaisedBarracks,
	RaisedCompound,
	WarZoneStronghold
};

UENUM(BlueprintType)
enum class ETileDesignVisual : uint8
{
	OpenGround,
	Ruins,
	NatureMeadow,
	NatureForestSparse,
	NatureForestDense,
	NatureRocky,
	NatureScrub,
	NatureAmbush,
	NatureServiceCamp,
	NatureDitch,
	WarZoneGround,
	RoadStraight,
	RoadCorner,
	RoadTJunction,
	RoadCross,
	RoadDeadEnd,
	Spawn,
	Exit,
	Obstacle,
	// Border lake. The design doc lists water among the Maze's impassable terrain,
	// so these cells are deliberately not walkable: the bank at the shoreline is the
	// barrier, and VerifyTraversableElevation excludes them for that reason.
	Water
};

USTRUCT(BlueprintType)
struct FTileDesignPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ETileDesignVisual Visual = ETileDesignVisual::OpenGround;

	// N=1, E=2, S=4, W=8 in design-world coordinates.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 ConnectionMask = 0;

	// True for deterministic design-layer access roads that connect large POIs.
	// These use the lightweight road builder so a long corridor does not repeat
	// the fully dressed 1x1 authored showcase tile every 20 metres.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bSupplementalAccessRoad = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RotationQuarterTurns = 0;

	// Stable authored combat-layout selection (0..3). This is serialized in the
	// future TileManifest instead of being recomputed differently per client.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 LayoutVariant = 0;

	// Per-cell deterministic prop/dressing seed. The server manifest sends this
	// value directly; clients never use local time or recompute it differently.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LocalSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> VisualLevel;
};

UENUM(BlueprintType)
enum class ELevelDesignPointType : uint8
{
	Spawn,
	Loot,
	AISpawn,
	Exit,
	Quest
};

USTRUCT(BlueprintType)
struct FLevelDesignPoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ELevelDesignPointType Type = ELevelDesignPointType::Spawn;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName PointId = NAME_None;

	// Data-only contract consumed later by the authoritative spawn/loot system.
	// No replicated gameplay actor is created by the level designer.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName ArchetypeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 Tier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float RadiusCm = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Capacity = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 PointSeed = 0;
};

USTRUCT(BlueprintType)
struct FFacilityPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName FacilityId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EFacilityVisualSet VisualSet = EFacilityVisualSet::Warehouse;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint Footprint = FIntPoint(2, 2);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RotationQuarterTurns = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LocalSeed = 0;

	// Height and access are part of the deterministic facility manifest.  Clients
	// must not infer these from local traces or asset bounds.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EFacilityElevationProfile ElevationProfile = EFacilityElevationProfile::Ground;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float BaseElevationCm = 0.0f;

	// Occupied edge cell, adjacent ground cell, and outward cardinal direction.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint EntranceCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint AccessCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint EntranceDirection = FIntPoint(0, -1);

	// The authored facility level selected for this logical reservation.
	// A soft reference keeps the heavy level unloaded during grid generation.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> FacilityLevel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FIntPoint> OccupiedCells;
};

// 게임플레이 포인트(Spawn/Loot/AI/Exit/Quest)가 확정됐을 때 한 번 알린다.
// 오브젝트 스포너(UPGObjectSpawnerSubsystem)가 여기에 붙어 Loot/Exit/Quest 자리에 실제 오브젝트를 만든다.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelDesignPointsBuilt, const TArray<FLevelDesignPoint>&);

UCLASS()
class PROJECTPG_API AWarZoneFootprintPreview : public AActor
{
	GENERATED_BODY()

public:
	AWarZoneFootprintPreview();

	// 데이터 전용 계약. 스폰·루팅 시스템이 읽기만 한다.
	const TArray<FLevelDesignPoint>& GetLevelDesignPoints() const { return LevelDesignPoints; }
	bool AreLevelDesignPointsBuilt() const { return bLevelDesignPointsBuilt; }

	FOnLevelDesignPointsBuilt OnLevelDesignPointsBuilt;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void TryReserveFootprint();
	void BuildTileDesignPlacements(const TMap<FIntPoint, AMapTile*>& TileByCell);
	void SpawnRuntimeBlueprintTiles();
	void BuildElevatedFacilityTerrain();
	// Entrance cell / outward cardinal direction for every ramp-and-stair approach a
	// facility owns. Shared by the terrain builder and VerifyTraversableElevation.
	void GetFacilityAccessEdges(
		const FFacilityPlacement& Placement,
		TArray<TPair<FIntPoint, FIntPoint>>& OutAccessEdges) const;
	float GetSurfaceElevationForCell(const FIntPoint& Cell) const;
	// Chooses a shore mesh variant and quarter-turn per land cell touching the lake.
	// Value is (variant index, quarter turns).
	void BuildShoreTransitionMap(
		const TSet<FIntPoint>& LakeCells,
		TMap<FIntPoint, TPair<int32, int32>>& OutShoreTileByCell) const;
	void BuildLightweightWorldVisuals();
	void BuildGameplayPointMarkers();
	void ResolveGameplayPointSafety();
	void RebuildGameplayPointHash();
	void BuildPCGDressingGraph();
	void VerifyPCGDressing();
	void VerifyLocalPerformance(float DeltaSeconds);
	void VerifyWorldCollision();
	void VerifyNavigation();
	void VerifyTacticalLayoutQuality();
	void VerifyTravelCoverDensity();
	void VerifyGameplayPointDistribution();
	void VerifyCriticalRoutes();
	void VerifyTraversableElevation();
	void VerifyCoplanarSurfaces();
	void RefreshNavigationBlockerRegion(
		UTacticalTileNavModifierComponent* Modifier,
		const FVector& WorldCenter,
		float RadiusCm);
	void DrawReservation() const;
	void ConfigureProxyMesh(
		UStaticMeshComponent* Component,
		const FVector& RelativeLocation,
		const FVector& Size);
	void ShowWarehouseProxy(const FVector& FootprintCenter);
	void ShowYardProxy(const FVector& FootprintCenter);
	void LoadFacilityDesignLevel(const FFacilityPlacement& Placement, int32 PlacementIndex);
	void BuildBorderMountains();
	bool AreAllFacilityLevelsLoaded() const;
	void VerifyDesignLevelSeparation();
	void ReserveFacility(
		EFacilityVisualSet VisualSet,
		const FIntPoint& Anchor,
		const FIntPoint& Footprint,
		int32 RotationQuarterTurns,
		const TArray<FIntPoint>& OccupiedCells,
		const TMap<FIntPoint, AMapTile*>& TileByCell);
	FVector GetFootprintCenter(const FFacilityPlacement& Placement) const;
	FVector GetDesignFootprintCenter(const FFacilityPlacement& Placement) const;
	void DrawDesignScalePreview() const;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> FloorProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> BackWallProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> LeftWallProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> RightWallProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> FrontWallLeftProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> FrontWallRightProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> RoofProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardFloorProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverNorthProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverSouthProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverWestProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverEastProxy;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GroundHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WarZoneGroundHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TransitionGroundHISM;

	// Border lake: a sunken bed and the water sheet above it.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LakeBedHISM;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LakeWaterHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MountainHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RoadSurfaceHISM;

	// Sculpted multi-cell ground features. Each mesh is authored flat around its whole
	// perimeter and only curves inside, so it drops into reserved cells without any
	// edge matching: every neighbouring tile still meets it at the shared surface.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TerrainFeatureHISMs;

	// Dressing for those features. Terrain cells carry no tile actor, so these are the
	// only props on them and they are sampled straight off the authored height field.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainRockHISM;

	// Rocks and reeds standing in the shallows, used to break up the cell-aligned
	// waterline.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShoreRockHISM;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShoreReedHISM;

	// Straight / outer corner / inner corner, in that order.
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ShoreTransitionHISMs;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainTreeHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainBushHISM;

	// One invisible static surface feeds Recast. Per-cell visuals/collision stay
	// on HISM, avoiding thousands of navigation geometry exports.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UStaticMeshComponent> NavigationFloor;

	// A small number of code-authored platform/ramp/stair components provide true
	// macro elevation and navigation without turning every 20m visual cell into a
	// navigation source.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ElevatedTerrainComponents;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;

	// Two small local obstacle sets keep Recast aware of tactical walls without
	// exporting all 2,000 runtime tiles at once.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UTacticalTileNavModifierComponent> CenterNavigationBlockers;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UTacticalTileNavModifierComponent> PlayerNavigationBlockers;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UPCGComponent> DressingPCGComponent;

	UPROPERTY(VisibleInstanceOnly, Category = "Footprint Preview")
	TArray<TObjectPtr<AMapTile>> ReservedTiles;

	UPROPERTY(VisibleInstanceOnly, Category = "Footprint Preview")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleInstanceOnly, Category = "Footprint Preview")
	float GridStep = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Footprint Preview", meta = (AllowPrivateAccess = "true"))
	TArray<FFacilityPlacement> FacilityPlacements;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Design Placements", meta = (AllowPrivateAccess = "true"))
	TArray<FTileDesignPlacement> TileDesignPlacements;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Design Points", meta = (AllowPrivateAccess = "true"))
	TArray<FLevelDesignPoint> LevelDesignPoints;

	bool bLevelDesignPointsBuilt = false;

	// Cells across the generated grid, measured from the tiles the logical layer
	// actually produced rather than assumed. UMapGeneratorComponent::_mapSize is
	// editable, so nothing downstream may hardcode a 45-cell / 900 m world.
	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	int32 GridCellSpan = 45;

	// Where the border lake ended up this raid, in cell coordinates. The corner is
	// chosen against the road network per seed, so anything that wants to face the
	// water - the hamlet's boat-landing spawn point - must read this rather than
	// assume a direction.
	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	FVector2D BorderLakeCentreCell = FVector2D::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	bool bHasBorderLake = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	uint32 LayoutHash = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	uint32 GameplayPointHash = 0;

	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bUseRuntimeBlueprintTiles = true;

	// Off by default: the audit walks every instance in the finished world. Turn it
	// on when hunting z-fighting.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bRunCoplanarSurfaceAudit = false;

	// Ring the map with the Downtown pack's background-mountain mesh so the world
	// reads as a basin: tile map on the valley floor, mountains on every horizon.
	// Purely visual and deliberately collision-free - leaving the tiles still ends
	// in a fall, exactly as before. Placement is derived from the raid seed, so
	// every client computes the identical ring with nothing to replicate.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bBuildBorderMountains = true;

	// Spawn the hand-authored facility Blueprint wherever one exists instead of the
	// code-built AProceduralFacilityActor. Turn off to put every facility back on
	// the procedural builder in one step.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bUseAuthoredFacilityBlueprints = true;

	// How far a facility may reach for a paved approach, in 20 m cells. A facility
	// with no road inside this budget is left unpaved on purpose. Set to 0 to drop
	// facility spurs entirely and keep only the Spawn/Exit and WarZone routes.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0", ClampMax = "20"))
	int32 MaxFacilitySpurCells = 6;

	// How far the spur search looks before giving up, independent of the budget
	// above. The search has to run past the budget or the log cannot report how far
	// the unpaved facilities actually were - and without that number the budget can
	// only be guessed at. Purely diagnostic reach; it never lays road by itself.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "1", ClampMax = "40"))
	int32 MaxFacilitySpurSearchCells = 20;

	// How hard facility placement is pulled toward the road network, against its
	// thematic target position. Only distance past MaxFacilitySpurCells is charged,
	// so 0 restores the old target-only behaviour and larger values will trade a
	// district's intended part of the map for a spot beside a road.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0", ClampMax = "8"))
	int32 FacilityRoadProximityWeight = 1;

	// Radius of the border lake in 20 m cells, measured from a point just outside one
	// map corner. 0 disables the lake entirely: no water cells, no shoreline meshes,
	// and the rural settlement falls back to an inland site.
	//
	// Defaulted off. The lake reads well from above but marrying a height change to a
	// 20 m tile grid keeps producing new seams at the waterline, and the map is in a
	// known-good state without it. Set this to 9 to work on it again - the shoreline
	// meshes need PG.BuildShoreMeshes to have been run and saved first.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0.0", ClampMax = "14.0"))
	float BorderLakeRadiusCells = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedRuntimeTiles;

	// Runtime tile Blueprints are converted into mesh-keyed HISM batches after
	// deterministic construction. This keeps the visual result while avoiding
	// roughly two thousand persistent tile actors and tens of thousands of scene
	// components on every client.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RuntimePackedVisualHISMs;

	// One local visual Level Instance per manifest facility placement. The same
	// authored facility map may appear several times with different rotations.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> FacilityDesignLevelInstances;

	UPROPERTY(Transient)
	TObjectPtr<UPCGGraph> RuntimeDressingGraph;

	TSet<int32> LoggedFacilityDesignLevelIndices;
	bool bLoggedAllFacilityDesignLevelsLoaded = false;
	bool bLoggedDesignLevelSeparation = false;
	bool bLoggedWorldCollision = false;
	bool bLoggedNavigation = false;
	bool bLoggedTacticalLayoutQuality = false;
	bool bLoggedTravelCoverDensity = false;
	bool bLoggedGameplayPointDistribution = false;
	bool bResolvedGameplayPointSafety = false;
	bool bLoggedCriticalRoutes = false;
	bool bLoggedMissingWarZoneFootprint = false;
	bool bLoggedTraversableElevation = false;
	bool bLoggedCoplanarSurfaces = false;
	bool bLoggedPCGDressing = false;
	double LastPlayerNavigationBlockerUpdateTimeSeconds = -BIG_NUMBER;
	int32 PerformanceSampleCount = 0;
	double PerformanceDeltaSecondsTotal = 0.0;
	double NavigationValidationStartTimeSeconds = 0.0;
	FIntPoint LastPlayerNavigationBlockerCell = FIntPoint(MAX_int32, MAX_int32);
	TArray<double> FacilityLoadRequestTimeSeconds;

	FTimerHandle RetryTimer;
};
