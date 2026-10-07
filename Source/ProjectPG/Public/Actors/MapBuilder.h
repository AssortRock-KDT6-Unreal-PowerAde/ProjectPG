// Visual layer for the procedural map: turns the logical AMapTile grid into tiles, facilities and the border lake.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// 처음엔 시설 자리 미리보기로 만들어짐
#include "MapBuilder.generated.h"

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
class PROJECTPG_API AMapBuilder : public AActor
{
	GENERATED_BODY()

public:
	AMapBuilder();

	// 데이터 전용 계약. 스폰·루팅 시스템이 읽기만 한다.
	const TArray<FLevelDesignPoint>& GetLevelDesignPoints() const { return LevelDesignPoints; }
	bool AreLevelDesignPointsBuilt() const { return bLevelDesignPointsBuilt; }

	FOnLevelDesignPointsBuilt OnLevelDesignPointsBuilt;

	// 판 시드. 서버는 게임모드(형님 생성기)의 시드, 들어온 사람은 맵 설계도(AMapManifestActor)로 받은 시드.
	// 맵 계산(언덕·호수·시설 씨앗·흙길·산)은 전부 이걸 쓴다 — 예전엔 게임모드에서 직접 읽어서 클라에서는 0 이었다.
	int64 GetRaidSeed() const;
	// 형님 생성기의 시작 구역 상자 크기. 같은 이유로 서버는 생성기에서, 클라는 설계도에서.
	int32 GetStartRangeSize() const;
	// 들어온 사람: 맵 설계도가 도착하면 시드·상자 크기를 넣는다(칸 쪽지를 만들기 전에).
	void ApplyReplicatedManifest(int64 InSeed, int32 InStartRange);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void TryReserveFootprint();
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
	bool AreAllFacilityLevelsLoaded() const;
	FVector GetDesignFootprintCenter(const FFacilityPlacement& Placement) const;

	// 협력객체 : 맵 자체 검사
	// friend class : 검사기가 private볼 수 있게.
	friend class UMapVerifier;
	// Transient : 레벨에 저장하지 않음(판마다 새로 만들어서) 
	UPROPERTY(Transient)
	// 검사기 주소를 담는 칸. 검사기쪽 Map과 서로 반대 방향
	// (맵->검사기,검사기-> 맵)
	TObjectPtr<class UMapVerifier> Verifier;

	// 보이는 것 목록(데이터 에셋 DA_MapAssets). 타일 BP·머티리얼·산·풀·시설 레벨을 여기서 고른다.
	// 비어 있거나 못 찾으면 MapAssetSet.h 의 C++ 기본값(예전 경로)을 쓴다.
	UPROPERTY(EditAnywhere, Category = "Map Assets")
	TSoftObjectPtr<class UMapAssetSet> MapAssets;
	// 한 번 불러온 목록을 들고 있는 칸(판마다 새로).
	UPROPERTY(Transient)
	TObjectPtr<class UMapAssetSet> LoadedMapAssets;
	// 목록 읽기. 일꾼들도 Map->GetMapAssets() 로 읽는다.
	const UMapAssetSet& GetMapAssets();
	// 판 시작 때 맵에 붙은 그릇(땅판·산·돌·풀 HISM)에 메시·머티리얼을 끼운다.
	void ApplyMapAssets();

	// 협력객체 : 건물 자리 담당. 고른 결과는 맵의 FacilityPlacements 에 넣는다.
	friend class UMapFacilityPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapFacilityPlanner> FacilityPlanner;

	// 협력객체 : 칸 모양 담당(칸마다 어떤 타일, 몇 도). 결과는 TileDesignPlacements·LayoutHash.
	friend class UMapTilePlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapTilePlanner> TilePlanner;

	// 협력객체 : 흙길 담당(시설·시작점·출구를 도로·워존에 잇는 흙길). 칸 모양 담당이 부른다.
	friend class UMapRoadPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapRoadPlanner> RoadPlanner;

	// 협력객체 : 공사 담당.
	friend class UMapTileSpawner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapTileSpawner> TileSpawner;

	// 협력객체 : 바닥 담당.
	friend class UMapGroundBuilder;
	UPROPERTY(Transient)
	TObjectPtr<class UMapGroundBuilder> GroundBuilder;

	// 협력객체 : 시작 구역 담당(멀티 최대 4명 — 가장자리에 시작 대기소를 더 고른다).
	friend class UMapSpawnRegionPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapSpawnRegionPlanner> SpawnRegionPlanner;

	// 협력객체 : 지점 담당(시작·상자·몬스터·출구·퀘스트 자리 찍기 + 끼임 정리). 결과는 LevelDesignPoints.
	friend class UMapPointPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapPointPlanner> PointPlanner;

	// 협력객체 : 아이템 담당(상자 자리마다 바닥 아이템 놓기).
	friend class UMapItemSpawner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapItemSpawner> ItemSpawner;

	// ---- 기획 숫자(에디터에서 맵 액터를 골라 바꾼다) ----
	// 워존에 놓는 2×2 마당 시설 수.
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WarZoneYardCount = 3;
	// 들판에 놓는 언덕(땅 모양) 최대 수.
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "0", ClampMax = "100"))
	int32 MaxTerrainFeatureCount = 28;
	// 몬스터 길찾기 막힘을 갱신하는 반경(cm). 플레이어 주변·맵 가운데 둘 다 이 값.
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "1000"))
	float NavigationBlockerRadiusCm = 6000.0f;

	// ---- 시작 구역(멀티) ---- 고르는 규칙은 MapSpawnRegionPlanner.cpp 주석.
	// 시작 구역 최대 수. 1번은 형님 생성기가 준 시작 칸, 나머지는 가장자리 빈 땅에서 더 고른다. 1 이면 예전처럼 한 곳.
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SpawnRegionCount = 4;
	// 시작 구역끼리 최소 거리(칸, 1칸 = 20m). 가까우면 나오자마자 마주친다.
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "2"))
	int32 MinSpawnRegionSpacingCells = 10;
	// 시작 구역과 출구 사이 최소 거리(칸). 나오자마자 탈출하는 걸 막는다.
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "0"))
	int32 MinSpawnToExitCells = 5;
	// 맵 가장자리에서 이 칸 수 안쪽만 후보(출발은 바깥, 워존은 가운데).
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "1"))
	int32 SpawnRegionEdgeBandCells = 5;
	// 이번 판 시작 구역 칸(0번 = 형님 시작 칸).
	UPROPERTY(VisibleInstanceOnly, Category = "Spawn Regions")
	TArray<FIntPoint> SpawnRegionCells;
	
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<USceneComponent> SceneRoot;

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
	// 길찾기 검사 끝남
	bool bLoggedAllFacilityDesignLevelsLoaded = false;
	bool bResolvedGameplayPointSafety = false;
	double LastPlayerNavigationBlockerUpdateTimeSeconds = -BIG_NUMBER;
	double NavigationValidationStartTimeSeconds = 0.0;
	FIntPoint LastPlayerNavigationBlockerCell = FIntPoint(MAX_int32, MAX_int32);
	TArray<double> FacilityLoadRequestTimeSeconds;

	FTimerHandle RetryTimer;

	// 들어온 사람이 설계도로 받은 값(서버에서는 안 씀).
	bool bHasReplicatedManifest = false;
	int64 ReplicatedRaidSeed = 0;
	int32 ReplicatedStartRange = 4;
	// 서버: 설계도를 이미 만들었나(쪽지를 읽는 재시도 때 두 번 만들지 않게).
	UPROPERTY(Transient)
	TObjectPtr<class AMapManifestActor> Manifest;
};
