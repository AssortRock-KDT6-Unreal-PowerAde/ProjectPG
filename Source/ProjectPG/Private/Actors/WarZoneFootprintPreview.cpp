// Visual layer for the procedural map: turns the logical AMapTile grid into tiles, facilities and the border lake.

#include "Actors/WarZoneFootprintPreview.h"
#include "Actors/TacticalTileActor.h"
#include "Actors/TacticalTileRoadStraight.h"
#include "Actors/ProceduralFacilityActor.h"

#include "Algo/AnyOf.h"
#include "Algo/Count.h"
#include "Algo/MinElement.h"
#include "Actors/MapTile.h"
#include "GameFramework/Pawn.h"
#include "GameModes/GameModePG.h"
#include "AIController.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/TargetPoint.h"
#include "Engine/StaticMesh.h"
#include "LandscapeProxy.h"
#include "GameFramework/PlayerController.h"
#include "NavigationInvokerComponent.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "Elements/PCGCreatePoints.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TacticalTileNavModifierComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "HAL/PlatformMemory.h"
#include "Materials/MaterialInterface.h"

// Sculpted ground features. Each mesh was generated with Geometry Script over an
// exact multiple of the 20 m cell and is flat around its entire perimeter, curving
// only inside, so it needs no edge matching with its neighbours - a dropped-in
// feature always meets the surrounding flat slabs at the shared surface.
struct FTerrainFeatureMesh
{
	const TCHAR* ShapeName;
	const TCHAR* AssetPath;
	FIntPoint Footprint;
	// Amplitude and profile must match the values the mesh was generated with, so the
	// dressing sampler below lands props exactly on the authored surface.
	float Amplitude;
	bool bRidgeProfile;
};

static const TArray<FTerrainFeatureMesh>& GetTerrainFeatureMeshes()
{
	static const TArray<FTerrainFeatureMesh> Meshes = {
		{ TEXT("Mound2x2"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Mound_2x2.SM_Terrain_Mound_2x2"), FIntPoint(2, 2), 320.0f, false },
		{ TEXT("Bowl2x2"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Bowl_2x2.SM_Terrain_Bowl_2x2"), FIntPoint(2, 2), -260.0f, false },
		{ TEXT("Ridge1x3"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Ridge_1x3.SM_Terrain_Ridge_1x3"), FIntPoint(1, 3), 280.0f, true },
		{ TEXT("Saddle2x1"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Saddle_2x1.SM_Terrain_Saddle_2x1"), FIntPoint(2, 1), 240.0f, false }
	};
	return Meshes;
}

// The exact height field the Geometry Script generator used: sin^2 falls to zero
// value AND zero slope at both ends, which is what makes the perimeter blend into
// the surrounding flat slabs. Reproducing it here rather than tracing the collision
// keeps dressing placement deterministic and free of per-instance line traces.
static float SampleTerrainFeatureHeight(const FTerrainFeatureMesh& Feature, float U, float V)
{
	auto Bump = [](float T)
	{
		const float S = FMath::Sin(PI * FMath::Clamp(T, 0.0f, 1.0f));
		return S * S;
	};
	const float Profile = Feature.bRidgeProfile
		? Bump(U) * FMath::Pow(Bump(V), 0.35f)
		: Bump(U) * Bump(V);
	return Feature.Amplitude * Profile;
}

namespace
{
	const FName ReservedTag(TEXT("Reserved_Facility"));
	const FName WarZoneFacilityTag(TEXT("WarZoneFacility"));
	const FName WarehouseTag(TEXT("Facility_Compound_3x3"));
	const FName YardTag(TEXT("Facility_Yard_2x2"));
	const FName BarracksTag(TEXT("Facility_Barracks_2x1"));
	const FName TrenchTag(TEXT("Facility_Trench_4x1"));
	const FName CheckpointTag(TEXT("Facility_Checkpoint_1x2"));
	const FName DowntownTag(TEXT("Facility_Downtown_3x3"));
	const FName FactoryConstructionTag(TEXT("Facility_FactoryConstruction_2x2"));
	const FName RuralHideoutTag(TEXT("Facility_RuralHideout_2x2"));
	const FSoftObjectPath WarehouseLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_Warehouse_2x2.LD_Facility_Warehouse_2x2"));
	const FSoftObjectPath YardLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_Yard_2x2.LD_Facility_Yard_2x2"));
	const FSoftObjectPath CheckpointLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_Checkpoint_1x2.LD_Facility_Checkpoint_1x2"));
	// A hand-built lakeside settlement trimmed from the Modular Rural Cabin demo:
	// cabins, a caravan, a pier, water and two rowing boats on sculpted ground.
	// Unlike every other facility level this one carries its own terrain, so the
	// shared flat pad has to be suppressed underneath it.
	const FSoftObjectPath RuralDioramaLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_RuralDiorama_2x2.LD_Facility_RuralDiorama_2x2"));
	// Four connected factory halls harvested from the Factory Pack demo map, complete
	// with their interiors - racks, roof trusses, skylights. Unlike the rural diorama
	// this level carries no ground of its own: its floor slabs were deleted so the
	// shared tile terrain runs straight through, which is what stops a facility
	// reading as a diorama parked on the map.
	const FSoftObjectPath FactoryHallLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_FactoryHall_2x2.LD_Facility_FactoryHall_2x2"));
	// A cafe and storefront block harvested from the Downtown West demo environment.
	// Its paved walkways and kerbs are kept rather than deleted: unlike a sculpted
	// terrain they are a thin surface laid a few centimetres over the shared ground,
	// and a city block standing on bare dirt reads worse than the seam they cost. The
	// level was lifted so that paving clears the shared datum by about 10 cm, the same
	// margin the runtime road slab uses to stay out of depth-buffer range.
	// 6x6: a complete two-sided street segment cut alley-to-alley from the pack's
	// demo city. Every earlier attempt cut a 3x3 window through physically attached
	// building rows, which always left some building's back or side face open -
	// the pack authors its blocks as continuous strips, so the only clean cuts are
	// the real alleys at demo x=-10100 and x=-1000.
	const FSoftObjectPath DowntownBlockLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_DowntownBlock_6x6.LD_Facility_DowntownBlock_6x6"));
	// The WarZone core: two warehouse halls and the barrel yard between them, cut
	// from the Factory pack demo's west compound (window centre (400,-2000), half
	// 3000 — every edge passes through open yard, the demo interior there is flat
	// at z=100 and was dropped to local 0). Replaces the code-built IndustrialRaid3x3.
	const FSoftObjectPath WarZoneCoreLevelPath(
		TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_WarZoneCore_3x3.LD_Facility_WarZoneCore_3x3"));

	// The WarZone core's cell footprint. 3x5 because the harvested factory compound
	// is four attached hall rows spanning 100 m north-south: a 3x3 window held only
	// the middle two and cut the outer rows in half, the same mistake the downtown
	// district went through before it grew to 6x6. Every identity check, the
	// reservation search and the centre math read this one constant.
	const FIntPoint WarZoneCoreFootprint(3, 5);
	const FIntPoint WarZoneCoreCentreOffset(
		(WarZoneCoreFootprint.X - 1) / 2, (WarZoneCoreFootprint.Y - 1) / 2);

	// Facility visual sets whose geometry comes from an authored level instead of
	// AProceduralFacilityActor. Kept in one place so the reservation, the ground
	// pad and the runtime spawn loop cannot disagree about which is which.
	bool FacilityUsesAuthoredLevel(EFacilityVisualSet VisualSet, const FIntPoint& Footprint)
	{
		// Warehouse is footprint-gated: the 3x3 is the WarZone core with its own
		// harvested level, while 2x2 warehouses stay procedural satellites.
		return VisualSet == EFacilityVisualSet::Checkpoint
			|| VisualSet == EFacilityVisualSet::RuralHideout
			|| VisualSet == EFacilityVisualSet::FactoryConstruction
			|| VisualSet == EFacilityVisualSet::DowntownBlock
			|| (VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint);
	}

	// Authored levels that bring their own sculpted ground, for which the generator
	// must not draw its flat terrain pad.
	//
	// Nothing qualifies now, and that is deliberate. The rural diorama used to: it
	// arrived with its own island and water table, which never lined up with the flat
	// cells around it, and suppressing the pad underneath left open seams at the
	// boundary. Harvested levels now have their ground deleted instead, so the shared
	// tile terrain runs straight through and the facility reads as part of the map.
	bool FacilityBringsOwnTerrain(EFacilityVisualSet VisualSet)
	{
		return false;
	}
	// Hand-authored facility Blueprints. Every wall and prop in these is an
	// individual StaticMeshComponent, so a designer can select one in the editor
	// viewport and drag or rescale it - which is impossible for the HISM instances
	// AProceduralFacilityActor emits. A visual set with no entry here has not been
	// authored yet and falls back to the procedural builder, so the library can be
	// filled in one facility at a time.
	const TCHAR* GetAuthoredFacilityBlueprintPath(EFacilityVisualSet VisualSet)
	{
		switch (VisualSet)
		{
		case EFacilityVisualSet::Warehouse:
			return TEXT("/Game/PG/LevelDesign/Facilities/Blueprints/BP_Facility_IndustrialRaid_3x3.BP_Facility_IndustrialRaid_3x3_C");
		default:
			// BP_Facility_DowntownBlock_2x2 and BP_Facility_RuralCamp_2x2 exist as
			// assets but hold only an empty FacilityVisual placeholder, so wiring
			// them would replace a crude building with nothing at all.
			return nullptr;
		}
	}

	const FSoftObjectPath RoadStraightLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Straight.LD_Tile_Road_Straight"));
	const FSoftObjectPath RoadCornerLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Corner.LD_Tile_Road_Corner"));
	const FSoftObjectPath RoadTJunctionLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_TJunction.LD_Tile_Road_TJunction"));
	const FSoftObjectPath RoadCrossLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Cross.LD_Tile_Road_Cross"));
	const FSoftObjectPath RoadDeadEndLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_DeadEnd.LD_Tile_Road_DeadEnd"));
	const FSoftObjectPath SpawnLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Spawn_Staging.LD_Tile_Spawn_Staging"));
	const FSoftObjectPath ExitLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Exit_Checkpoint.LD_Tile_Exit_Checkpoint"));
	const FSoftObjectPath ObstacleLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Obstacle_Checkpoint.LD_Tile_Obstacle_Checkpoint"));
	const FSoftObjectPath OpenGroundLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_None_OpenGround.LD_Tile_None_OpenGround"));
	const FSoftObjectPath RuinsLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_None_Ruins.LD_Tile_None_Ruins"));
	constexpr float DesignCellSize = 2000.0f;
	// The one walking datum every flat cell, tile prop and road slab is authored
	// against. Only facility footprints are allowed to leave it.
	constexpr float BaseGroundSurfaceZ = 20.0f;
	// ACharacter::CharacterMovement->MaxStepHeight default, which
	// ALevelDesignValidationCharacter does not override. Any riser above this is a
	// lip the player has to jump, not walk.
	constexpr float MaxTraversableStepCm = 45.0f;
	// The road's walking face clears the shared terrain top by this much, and the
	// slab is thick enough that its underside sits well inside the ground cube.
	constexpr float RoadSurfaceLiftCm = 10.0f;
	constexpr float RoadSurfaceThicknessCm = 30.0f;
	// Border lake. The bed sits far enough under the surface that the water reads as
	// deep rather than as a puddle, and the surface sits below the shared ground top
	// so the bank is a real drop the player cannot simply walk off into.
	// Variant index meaning "all four corners wet". Not one of the four generated
	// meshes - it routes the cell to the lake bed instead.
	constexpr int32 SubmergedShoreVariant = -2;
	constexpr float LakeSurfaceZ = -35.0f;
	// Must equal the generated shore meshes' wet-corner height, otherwise the beach
	// ends at -110 and the flat bed starts at a different depth, putting a step
	// right where the two meet.
	constexpr float LakeBedZ = -110.0f;

	// The lake is one metaball centred just outside a map corner, so it bites into
	// the grid as a rounded bay rather than a band along an edge. Both the tile pass
	// that floods the cells and the facility reservation that puts the lakeside
	// settlement on its shore have to agree on where it is, so it lives here.
	struct FBorderLake
	{
		FVector2D CentreCell = FVector2D::ZeroVector;
		float Radius = 0.0f;
	};

	FBorderLake GetBorderLake(int64 RaidSeed, const FIntPoint& MinCell, const FIntPoint& MaxCell,
		float RadiusCells, const TSet<FIntPoint>& TraversalCells)
	{
		// The corner used to be the seed hash alone. When the generator happened to
		// run roads or a spawn into that corner, the per-cell traversal guard in the
		// flood pass shredded the disc into leftover puddles - the lake's size was
		// being decided by the road layout, not by RadiusCells (observed as
		// cells=28/25/5/3/1 across runs with the radius fixed at 9). Score all four
		// corners and take the one the road network reaches least; the seed only
		// picks where the scan starts, so equally clean corners still vary per raid.
		FBorderLake Lake;
		Lake.Radius = RadiusCells;
		const int32 FirstCorner = static_cast<int32>(GetTypeHash(RaidSeed) % 4u);
		int32 BestCount = TNumericLimits<int32>::Max();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 Corner = (FirstCorner + Index) % 4;
			const FVector2D Centre(
				(Corner & 1) ? MaxCell.X + 3.0f : MinCell.X - 3.0f,
				(Corner & 2) ? MaxCell.Y + 3.0f : MinCell.Y - 3.0f);
			int32 Count = 0;
			for (const FIntPoint& Cell : TraversalCells)
				if (FVector2D::Distance(FVector2D(Cell.X, Cell.Y), Centre) <= RadiusCells + 2.0f)
					++Count;
			// Strict less-than: the first corner in scan order wins ties, which keeps
			// the choice identical at both call sites.
			if (Count < BestCount)
			{
				BestCount = Count;
				Lake.CentreCell = Centre;
			}
		}
		return Lake;
	}

	// Per-cell wobble on the waterline. Without it the shore is a clean arc, which
	// reads as machine-made just as plainly as a straight edge does.
	float GetLakeShoreJitter(int64 RaidSeed, const FIntPoint& Cell)
	{
		const uint32 ShoreHash = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(Cell.X * 7), GetTypeHash(Cell.Y * 13)));
		return (static_cast<int32>(ShoreHash % 33u) - 16) * 0.1f;
	}
	constexpr uint8 NorthConnection = 1 << 0;
	constexpr uint8 EastConnection = 1 << 1;
	constexpr uint8 SouthConnection = 1 << 2;
	constexpr uint8 WestConnection = 1 << 3;

	uint8 RotateConnectionMaskPositiveYaw(uint8 Mask)
	{
		uint8 Rotated = 0;
		if (Mask & NorthConnection) Rotated |= WestConnection;
		if (Mask & EastConnection) Rotated |= NorthConnection;
		if (Mask & SouthConnection) Rotated |= EastConnection;
		if (Mask & WestConnection) Rotated |= SouthConnection;
		return Rotated;
	}

	int32 FindPositiveYawRotation(uint8 CanonicalMask, uint8 TargetMask)
	{
		uint8 RotatedMask = CanonicalMask;
		for (int32 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
		{
			if (RotatedMask == TargetMask)
				return QuarterTurns;
			RotatedMask = RotateConnectionMaskPositiveYaw(RotatedMask);
		}
		return 0;
	}

	bool IsRoadTraversalType(ETileType Type)
	{
		return Type == ETileType::Road
			|| Type == ETileType::Obstacle
			|| Type == ETileType::Spawn
			|| Type == ETileType::Exit
			|| Type == ETileType::WarZone;
	}

	// The facility reservation and the flood pass both ask where the lake is and
	// must agree, so the corner choice may only depend on data both of them see
	// identically: the logical tile map. Supplemental spur roads are visual-layer
	// additions and deliberately excluded from the score.
	TSet<FIntPoint> CollectTraversalCells(const TMap<FIntPoint, AMapTile*>& TileByCell)
	{
		TSet<FIntPoint> Cells;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
			if (IsValid(Pair.Value) && IsRoadTraversalType(Pair.Value->GetType()))
				Cells.Add(Pair.Key);
		return Cells;
	}

	const FIntPoint FootprintOffsets[] = {
		FIntPoint(0, 0), FIntPoint(1, 0),
		FIntPoint(0, 1), FIntPoint(1, 1)
	};
}

AWarZoneFootprintPreview::AWarZoneFootprintPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// The hidden navigation slab is static. Keep its attachment hierarchy static as
	// well so PIE does not reject the attachment before Recast can consume it.
	SceneRoot->SetMobility(EComponentMobility::Static);

	FloorProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorProxy"));
	BackWallProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackWallProxy"));
	LeftWallProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftWallProxy"));
	RightWallProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightWallProxy"));
	FrontWallLeftProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontWallLeftProxy"));
	FrontWallRightProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontWallRightProxy"));
	RoofProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RoofProxy"));
	YardFloorProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardFloorProxy"));
	YardCoverNorthProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverNorthProxy"));
	YardCoverSouthProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverSouthProxy"));
	YardCoverWestProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverWestProxy"));
	YardCoverEastProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverEastProxy"));
	GroundHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GroundHISM"));
	WarZoneGroundHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WarZoneGroundHISM"));
	TransitionGroundHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TransitionGroundHISM"));
	RoadSurfaceHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RoadSurfaceHISM"));
	LakeBedHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LakeBedHISM"));
	LakeWaterHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LakeWaterHISM"));
	MountainHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("MountainHISM"));
	NavigationFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NavigationFloor"));
	NavigationInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavigationInvoker"));
	CenterNavigationBlockers = CreateDefaultSubobject<UTacticalTileNavModifierComponent>(TEXT("CenterNavigationBlockers"));
	PlayerNavigationBlockers = CreateDefaultSubobject<UTacticalTileNavModifierComponent>(TEXT("PlayerNavigationBlockers"));
	DressingPCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("DressingPCGComponent"));
	NavigationInvoker->SetGenerationRadii(12000.0f, 15000.0f);
	DressingPCGComponent->Seed = 1337;
	DressingPCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	UStaticMeshComponent* ProxyComponents[] = {
		FloorProxy, BackWallProxy, LeftWallProxy, RightWallProxy,
		FrontWallLeftProxy, FrontWallRightProxy, RoofProxy,
		YardFloorProxy, YardCoverNorthProxy, YardCoverSouthProxy,
		YardCoverWestProxy, YardCoverEastProxy
	};

	for (UStaticMeshComponent* Component : ProxyComponents)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(CubeMesh.Object);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetVisibility(false);
		Component->SetMobility(EComponentMobility::Movable);
	}

	UHierarchicalInstancedStaticMeshComponent* WorldComponents[] = {
		GroundHISM, WarZoneGroundHISM, TransitionGroundHISM, RoadSurfaceHISM,
		LakeBedHISM, LakeWaterHISM
	};
	for (UHierarchicalInstancedStaticMeshComponent* Component : WorldComponents)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(CubeMesh.Object);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetMobility(EComponentMobility::Movable);
	}

	// The basin's mountain ring. Collision-free on purpose: the map edge must
	// still end in a fall, and Recast must never path onto scenery kilometres out.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MountainMesh(
		TEXT("/Game/Downtown_West/Assets/background_mountain/SM_background_mountains"));
	MountainHISM->SetupAttachment(SceneRoot);
	if (MountainMesh.Succeeded())
		MountainHISM->SetStaticMesh(MountainMesh.Object);
	MountainHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MountainHISM->SetCanEverAffectNavigation(false);
	MountainHISM->SetGenerateOverlapEvents(false);
	MountainHISM->SetMobility(EComponentMobility::Movable);
	GroundHISM->SetCanEverAffectNavigation(false);
	WarZoneGroundHISM->SetCanEverAffectNavigation(false);
	TransitionGroundHISM->SetCanEverAffectNavigation(false);
	RoadSurfaceHISM->SetCanEverAffectNavigation(false);
	LakeBedHISM->SetCanEverAffectNavigation(false);
	// The surface is a visual sheet only. Giving it collision would let a player
	// stand on the lake, and giving it navigation would let AI path across it.
	LakeWaterHISM->SetCanEverAffectNavigation(false);
	LakeWaterHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Sculpted ground features: one HISM per authored shape *per ground band*. The mesh
	// is shared and only the component's material is overridden, so a feature dropped
	// into the WarZone reads as the same dirt as the flat cells around it instead of a
	// green patch in an industrial yard. Index is Band * ShapeCount + Shape.
	{
		const TCHAR* BandMaterialPaths[] = {
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified"),
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_Transition"),
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_WarZone")
		};
		const TCHAR* BandNames[] = { TEXT("Nature"), TEXT("Transition"), TEXT("WarZone") };
		const TArray<FTerrainFeatureMesh>& FeatureMeshes = GetTerrainFeatureMeshes();
		for (int32 BandIndex = 0; BandIndex < UE_ARRAY_COUNT(BandNames); ++BandIndex)
		{
			ConstructorHelpers::FObjectFinder<UMaterialInterface> BandMaterial(BandMaterialPaths[BandIndex]);
			for (int32 ShapeIndex = 0; ShapeIndex < FeatureMeshes.Num(); ++ShapeIndex)
			{
				const FTerrainFeatureMesh& Feature = FeatureMeshes[ShapeIndex];
				const FName ComponentName(*FString::Printf(
					TEXT("Terrain%s%sHISM"), BandNames[BandIndex], Feature.ShapeName));
				UHierarchicalInstancedStaticMeshComponent* Component =
					CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(ComponentName);
				Component->SetupAttachment(SceneRoot);
				ConstructorHelpers::FObjectFinder<UStaticMesh> FeatureMesh(Feature.AssetPath);
				if (FeatureMesh.Succeeded())
					Component->SetStaticMesh(FeatureMesh.Object);
				if (BandMaterial.Succeeded())
					Component->SetMaterial(0, BandMaterial.Object);
				Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Component->SetGenerateOverlapEvents(false);
				Component->SetMobility(EComponentMobility::Movable);
				Component->SetCanEverAffectNavigation(false);
				TerrainFeatureHISMs.Add(Component);
			}
		}
	}

	// Rocks and trees block; bushes stay walk-through so a slope never becomes a wall.
	struct FTerrainDressingSpec
	{
		const TCHAR* ComponentName;
		const TCHAR* AssetPath;
		bool bCollides;
	};
	const FTerrainDressingSpec DressingSpecs[] = {
		{ TEXT("TerrainRockHISM"), TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_medium_a_low.SM_rock_medium_a_low"), true },
		{ TEXT("TerrainTreeHISM"), TEXT("/PCGBiomeSample/Meshes/PCG_Pine_01.PCG_Pine_01"), true },
		{ TEXT("TerrainBushHISM"), TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2"), false },
		// Shore dressing. The waterline is decided per 20 m cell, so on its own it
		// steps along the grid. Rocks and reeds standing in the shallows break that
		// line up - the same trick the diorama uses along its own bank.
		{ TEXT("ShoreRockHISM"), TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/River_Stone_2.River_Stone_2"), true },
		{ TEXT("ShoreReedHISM"), TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Cat_Tail.Cat_Tail"), false }
	};
	TArray<UHierarchicalInstancedStaticMeshComponent*> DressingComponents;
	for (const FTerrainDressingSpec& Spec : DressingSpecs)
	{
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Spec.ComponentName);
		Component->SetupAttachment(SceneRoot);
		ConstructorHelpers::FObjectFinder<UStaticMesh> DressingMesh(Spec.AssetPath);
		if (DressingMesh.Succeeded())
			Component->SetStaticMesh(DressingMesh.Object);
		Component->SetCollisionEnabled(Spec.bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Component->SetCollisionProfileName(Spec.bCollides
			? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCullDistances(14000, 32000);
		DressingComponents.Add(Component);
	}
	TerrainRockHISM = DressingComponents[0];
	TerrainTreeHISM = DressingComponents[1];
	TerrainBushHISM = DressingComponents[2];
	ShoreRockHISM = DressingComponents[3];
	ShoreReedHISM = DressingComponents[4];

	// Shoreline transition meshes, one HISM per variant. Generated by
	// PG.BuildShoreMeshes; their land-facing edges sit flat on the shared Z=20 datum
	// so they drop in beside ordinary cells, and their interiors curve down under the
	// waterline. This is what actually removes the 20 m staircase - rotated boxes and
	// scattered rocks only ever hid it.
	const TCHAR* ShoreMeshPaths[] = {
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner1.SM_Shore_Corner1"),
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Adjacent.SM_Shore_Corner2Adjacent"),
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Diagonal.SM_Shore_Corner2Diagonal"),
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner3.SM_Shore_Corner3")
	};
	static const TCHAR* ShoreComponentNames[] = {
		TEXT("ShoreCorner1HISM"), TEXT("ShoreCorner2AdjacentHISM"),
		TEXT("ShoreCorner2DiagonalHISM"), TEXT("ShoreCorner3HISM")
	};
	for (int32 ShoreIndex = 0; ShoreIndex < UE_ARRAY_COUNT(ShoreMeshPaths); ++ShoreIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
				ShoreComponentNames[ShoreIndex]);
		Component->SetupAttachment(SceneRoot);
		// Deliberately not ConstructorHelpers: these meshes are generated by
		// PG.BuildShoreMeshes, so on a fresh checkout - or right after regenerating
		// them - they do not exist when the CDO is constructed at editor start. A
		// constructor-time finder would fail silently and leave the shore invisible
		// until the next restart. BuildLightweightWorldVisuals loads them instead.
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCanEverAffectNavigation(false);
		ShoreTransitionHISMs.Add(Component);
	}

	NavigationFloor->SetupAttachment(SceneRoot);
	NavigationFloor->SetStaticMesh(CubeMesh.Object);
	NavigationFloor->SetRelativeLocation(FVector(0.0f, 0.0f, -20.0f));
	NavigationFloor->SetRelativeScale3D(FVector(900.0f, 900.0f, 0.2f));
	NavigationFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	NavigationFloor->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	NavigationFloor->SetGenerateOverlapEvents(false);
	NavigationFloor->SetVisibility(false);
	NavigationFloor->SetHiddenInGame(true);
	NavigationFloor->SetCanEverAffectNavigation(true);
	NavigationFloor->SetMobility(EComponentMobility::Static);

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WarZoneGroundMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_WarZone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TransitionGroundMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_Transition"));
	if (GroundMaterial.Succeeded())
		GroundHISM->SetMaterial(0, GroundMaterial.Object);
	if (WarZoneGroundMaterial.Succeeded())
		WarZoneGroundHISM->SetMaterial(0, WarZoneGroundMaterial.Object);
	if (TransitionGroundMaterial.Succeeded())
		TransitionGroundHISM->SetMaterial(0, TransitionGroundMaterial.Object);
	if (RoadMaterial.Succeeded())
		RoadSurfaceHISM->SetMaterial(0, RoadMaterial.Object);
	// Reuse the rural diorama's own lake material so the border water and the
	// water already inside that facility read as one body.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LakeWaterMaterial(
		TEXT("/Game/Modular_Rural_Cabin/Materials/Instances/Water_Lake"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LakeBedMaterial(
		TEXT("/Game/Modular_Rural_Cabin/Materials/Instances/Diorama_Ground"));
	if (LakeWaterMaterial.Succeeded())
		LakeWaterHISM->SetMaterial(0, LakeWaterMaterial.Object);
	if (LakeBedMaterial.Succeeded())
		LakeBedHISM->SetMaterial(0, LakeBedMaterial.Object);
}

void AWarZoneFootprintPreview::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthority())
		return;

	// GameMode creates the logical grid during BeginPlay. Retry briefly so this
	// preview does not depend on actor BeginPlay ordering.
	GetWorldTimerManager().SetTimer(
		RetryTimer,
		this,
		&AWarZoneFootprintPreview::TryReserveFootprint,
		0.2f,
		true,
		0.2f);
}

void AWarZoneFootprintPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	bool bAnyGameWorldRunning = false;
	if (IsValid(GEngine))
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
			{
				bAnyGameWorldRunning = true;
				break;
			}
		}
	}
	if (!bAnyGameWorldRunning && IsValid(GetWorld()) && !GetWorld()->IsGameWorld())
	{
		DrawReservation();
		DrawDesignScalePreview();
	}

	for (int32 Index = 0; Index < FacilityDesignLevelInstances.Num(); ++Index)
	{
		ULevelStreamingDynamic* Instance = FacilityDesignLevelInstances[Index];
		if (LoggedFacilityDesignLevelIndices.Contains(Index)
			|| !IsValid(Instance)
			|| !Instance->IsLevelLoaded()
			|| !Instance->IsLevelVisible())
		{
			continue;
		}

		LoggedFacilityDesignLevelIndices.Add(Index);
		const ULevel* LoadedLevel = Instance->GetLoadedLevel();
		const int32 LoadedActorCount = IsValid(LoadedLevel) ? LoadedLevel->Actors.Num() : 0;
		const double RequestedAt = FacilityLoadRequestTimeSeconds.IsValidIndex(Index)
			? FacilityLoadRequestTimeSeconds[Index] : FPlatformTime::Seconds();
		UE_LOG(LogTemp, Display,
			TEXT("Facility design level loaded: index=%d type=%d actors=%d visible=true load_ms=%.2f package=%s"),
			Index,
			FacilityPlacements.IsValidIndex(Index) ? static_cast<int32>(FacilityPlacements[Index].VisualSet) : -1,
			LoadedActorCount,
			(FPlatformTime::Seconds() - RequestedAt) * 1000.0,
			*Instance->GetWorldAssetPackageName());
	}
	if (!bLoggedAllFacilityDesignLevelsLoaded
		&& !FacilityPlacements.IsEmpty()
		&& AreAllFacilityLevelsLoaded())
	{
		bLoggedAllFacilityDesignLevelsLoaded = true;
		int32 LoadedLevelCount = 0;
		for (const ULevelStreamingDynamic* Instance : FacilityDesignLevelInstances)
			if (IsValid(Instance) && Instance->IsLevelLoaded() && Instance->IsLevelVisible())
				++LoadedLevelCount;
		UE_LOG(LogTemp, Display,
			TEXT("All facility visuals ready: placements=%d runtime_blueprints=%d streamed_levels=%d"),
			FacilityPlacements.Num(),
			Algo::CountIf(SpawnedRuntimeTiles, [](const AActor* Actor)
			{
				return IsValid(Actor) && Actor->Tags.Contains(TEXT("RuntimeTacticalFacility"));
			}),
			LoadedLevelCount);
	}

	VerifyDesignLevelSeparation();
	VerifyWorldCollision();
	ResolveGameplayPointSafety();
	VerifyTacticalLayoutQuality();
	VerifyTravelCoverDensity();
	VerifyGameplayPointDistribution();
	VerifyNavigation();
	VerifyCriticalRoutes();
	VerifyTraversableElevation();
	VerifyCoplanarSurfaces();
	VerifyPCGDressing();
	VerifyLocalPerformance(DeltaSeconds);

	// 지금 조작 중인 플레이어 폰(팀 캐릭터) 주변 60 m 길찾기 차단을 갱신한다.
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (IsValid(PlayerPawn) && IsValid(PlayerNavigationBlockers))
	{
		const FVector PlayerLocation = PlayerPawn->GetActorLocation();
		const FIntPoint PlayerCell(
			FMath::FloorToInt(PlayerLocation.X / 4000.0f),
			FMath::FloorToInt(PlayerLocation.Y / 4000.0f));
		if (PlayerCell != LastPlayerNavigationBlockerCell)
		{
			LastPlayerNavigationBlockerCell = PlayerCell;
			RefreshNavigationBlockerRegion(PlayerNavigationBlockers, PlayerLocation, 6000.0f);
			LastPlayerNavigationBlockerUpdateTimeSeconds = FPlatformTime::Seconds();
		}
	}
}

void AWarZoneFootprintPreview::TryReserveFootprint()
{
	TArray<AMapTile*> AllTiles;
	TArray<float> UniqueX;

	for (TActorIterator<AMapTile> It(GetWorld()); It; ++It)
	{
		AMapTile* Tile = *It;
		AllTiles.Add(Tile);
		UniqueX.AddUnique(Tile->GetActorLocation().X);
	}

	// Runtime Blueprint mode treats AMapTile and the HISM world as logical/build
	// data only. Hide them before the relatively expensive placement conversion
	// starts so the coloured metaball preview cannot flash for a frame at PIE start.
	if (bUseRuntimeBlueprintTiles)
	{
		GroundHISM->SetVisibility(false, true);
		WarZoneGroundHISM->SetVisibility(false, true);
		TransitionGroundHISM->SetVisibility(false, true);
		RoadSurfaceHISM->SetVisibility(false, true);
		GroundHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WarZoneGroundHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TransitionGroundHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RoadSurfaceHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (AMapTile* Tile : AllTiles)
		{
			if (!IsValid(Tile))
				continue;
			Tile->SetActorHiddenInGame(true);
			Tile->SetActorEnableCollision(false);
			Tile->SetActorTickEnabled(false);
		}
	}

	if (AllTiles.Num() == 0)
		return;

	UniqueX.Sort();
	GridStep = 0.0f;
	for (int32 Index = 1; Index < UniqueX.Num(); ++Index)
	{
		const float Difference = UniqueX[Index] - UniqueX[Index - 1];
		if (Difference > KINDA_SMALL_NUMBER && (GridStep <= 0.0f || Difference < GridStep))
			GridStep = Difference;
	}

	if (GridStep <= 0.0f)
		return;

	TMap<FIntPoint, AMapTile*> WarZoneByCell;
	TMap<FIntPoint, AMapTile*> TileByCell;
	for (AMapTile* Tile : AllTiles)
	{
		const FVector Location = Tile->GetActorLocation();
		const FIntPoint Cell(
			FMath::RoundToInt(Location.X / GridStep),
			FMath::RoundToInt(Location.Y / GridStep));
		TileByCell.Add(Cell, Tile);
		if (Tile->GetType() == ETileType::WarZone)
			WarZoneByCell.Add(Cell, Tile);
	}

	TArray<FIntPoint> Candidates;
	TArray<FIntPoint> CompoundCandidates;

	for (const TPair<FIntPoint, AMapTile*>& Pair : WarZoneByCell)
	{
		const FIntPoint Cell = Pair.Key;
		const bool bFits2x2 = WarZoneByCell.Contains(Cell + FIntPoint(1, 0))
			&& WarZoneByCell.Contains(Cell + FIntPoint(0, 1))
			&& WarZoneByCell.Contains(Cell + FIntPoint(1, 1));
		if (bFits2x2)
			Candidates.Add(Cell);

		bool bFitsCore = true;
		for (int32 X = 0; X < WarZoneCoreFootprint.X && bFitsCore; ++X)
		{
			for (int32 Y = 0; Y < WarZoneCoreFootprint.Y; ++Y)
			{
				if (!WarZoneByCell.Contains(Cell + FIntPoint(X, Y)))
				{
					bFitsCore = false;
					break;
				}
			}
		}
		if (bFitsCore)
			CompoundCandidates.Add(Cell);
	}

	if (Candidates.IsEmpty() || CompoundCandidates.IsEmpty())
	{
		// This runs from Tick and used to return silently, so a WarZone that came
		// out too small to hold the compound produced an empty world and not one
		// line of explanation. Report it once instead of every frame.
		if (!bLoggedMissingWarZoneFootprint)
		{
			bLoggedMissingWarZoneFootprint = true;
			UE_LOG(LogTemp, Error,
				TEXT("WarZone footprint unavailable: tiles=%d warzone_cells=%d fits_2x2=%d fits_core=%d ")
				TEXT("grid_step=%.1f - generation cannot proceed without a %dx%d WarZone block"),
				AllTiles.Num(), WarZoneByCell.Num(), Candidates.Num(), CompoundCandidates.Num(), GridStep,
				WarZoneCoreFootprint.X, WarZoneCoreFootprint.Y);
		}
		return;
	}

	auto SortByCenterDistance = [](const FIntPoint& A, const FIntPoint& B)
	{
		const int64 DistanceA = FMath::Square(static_cast<int64>(A.X))
			+ FMath::Square(static_cast<int64>(A.Y));
		const int64 DistanceB = FMath::Square(static_cast<int64>(B.X))
			+ FMath::Square(static_cast<int64>(B.Y));
		if (DistanceA != DistanceB)
			return DistanceA < DistanceB;
		if (A.X != B.X)
			return A.X < B.X;
		return A.Y < B.Y;
	};
	Candidates.Sort(SortByCenterDistance);
	CompoundCandidates.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		// Compare footprint centres, not anchors, so the largest facility actually
		// occupies the middle of the authoritative WarZone.
		const int64 CenterAX = static_cast<int64>(A.X) + WarZoneCoreCentreOffset.X;
		const int64 CenterAY = static_cast<int64>(A.Y) + WarZoneCoreCentreOffset.Y;
		const int64 CenterBX = static_cast<int64>(B.X) + WarZoneCoreCentreOffset.X;
		const int64 CenterBY = static_cast<int64>(B.Y) + WarZoneCoreCentreOffset.Y;
		const int64 DistanceA = CenterAX * CenterAX + CenterAY * CenterAY;
		const int64 DistanceB = CenterBX * CenterBX + CenterBY * CenterBY;
		if (DistanceA != DistanceB)
			return DistanceA < DistanceB;
		if (A.X != B.X)
			return A.X < B.X;
		return A.Y < B.Y;
	});

	// Reserve one 3x3 anchor facility, then several well-spaced 2x2 satellite
	// camps. AMapTile remains the logical authority; the future server manifest
	// only needs anchor, footprint, rotation, visual type and stable seed.
	TArray<FIntPoint> SelectedFacilityAnchors;
	TSet<FIntPoint> SelectedFacilityCells;
	const FIntPoint CompoundAnchor = CompoundCandidates[0];
	TArray<FIntPoint> CompoundOccupiedCells;
	for (int32 X = 0; X < WarZoneCoreFootprint.X; ++X)
	{
		for (int32 Y = 0; Y < WarZoneCoreFootprint.Y; ++Y)
		{
			const FIntPoint Cell = CompoundAnchor + FIntPoint(X, Y);
			SelectedFacilityCells.Add(Cell);
			CompoundOccupiedCells.Add(Cell);
		}
	}
	auto CanSelectFacility = [&SelectedFacilityCells](const FIntPoint& Candidate)
	{
		for (const FIntPoint& Offset : FootprintOffsets)
		{
			const FIntPoint Cell = Candidate + Offset;
			if (SelectedFacilityCells.Contains(Cell))
				return false;
			for (int32 X = -2; X <= 2; ++X)
				for (int32 Y = -2; Y <= 2; ++Y)
					if (SelectedFacilityCells.Contains(Cell + FIntPoint(X, Y)))
						return false;
		}
		return true;
	};
	auto SelectFacility = [&SelectedFacilityAnchors, &SelectedFacilityCells](const FIntPoint& Anchor)
	{
		SelectedFacilityAnchors.Add(Anchor);
		for (const FIntPoint& Offset : FootprintOffsets)
			SelectedFacilityCells.Add(Anchor + Offset);
	};
	while (SelectedFacilityAnchors.Num() < 3)
	{
		bool bFoundNext = false;
		int64 BestMinimumDistanceSquared = -1;
		FIntPoint BestCandidate = FIntPoint::ZeroValue;
		for (const FIntPoint& Candidate : Candidates)
		{
			if (!CanSelectFacility(Candidate))
				continue;
			int64 MinimumDistanceSquared = TNumericLimits<int64>::Max();
			for (const FIntPoint& Existing : SelectedFacilityAnchors)
			{
				const int64 DX = Candidate.X - Existing.X;
				const int64 DY = Candidate.Y - Existing.Y;
				MinimumDistanceSquared = FMath::Min(MinimumDistanceSquared, DX * DX + DY * DY);
			}
			if (MinimumDistanceSquared > BestMinimumDistanceSquared)
			{
				bFoundNext = true;
				BestMinimumDistanceSquared = MinimumDistanceSquared;
				BestCandidate = Candidate;
			}
		}
		if (!bFoundNext)
			break;
		SelectFacility(BestCandidate);
	}

	if (SelectedFacilityAnchors.IsEmpty())
		return;

	AnchorCell = CompoundAnchor;
	ReservedTiles.Reset();
	FacilityPlacements.Reset();
	ReserveFacility(
		EFacilityVisualSet::Warehouse,
		CompoundAnchor,
		WarZoneCoreFootprint,
		0,
		CompoundOccupiedCells,
		TileByCell);
	for (int32 FacilityIndex = 0; FacilityIndex < SelectedFacilityAnchors.Num(); ++FacilityIndex)
	{
		const FIntPoint FacilityAnchor = SelectedFacilityAnchors[FacilityIndex];
		TArray<FIntPoint> OccupiedCells;
		for (const FIntPoint& Offset : FootprintOffsets)
			OccupiedCells.Add(FacilityAnchor + Offset);
		ReserveFacility(
			EFacilityVisualSet::Yard,
			FacilityAnchor,
			FIntPoint(2, 2),
			FacilityIndex % 4,
			OccupiedCells,
			TileByCell);
	}

	// The design contract also needs genuinely multi-cell shapes, not only square
	// compounds. Reserve two 2x1 interiors and one 4x1 linear encounter while the
	// brother's WarZone cells remain the authoritative eligibility mask.
	auto ReserveBestRect = [this, &WarZoneByCell, &TileByCell, &SelectedFacilityCells](
		EFacilityVisualSet VisualSet,
		FIntPoint Footprint,
		int32 RotationQuarterTurns)
	{
		TArray<FIntPoint> SearchCells;
		WarZoneByCell.GetKeys(SearchCells);
		SearchCells.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			const int64 DA = static_cast<int64>(A.X) * A.X + static_cast<int64>(A.Y) * A.Y;
			const int64 DB = static_cast<int64>(B.X) * B.X + static_cast<int64>(B.Y) * B.Y;
			return DA != DB ? DA < DB : (A.X != B.X ? A.X < B.X : A.Y < B.Y);
		});

		for (const FIntPoint& Anchor : SearchCells)
		{
			TArray<FIntPoint> Occupied;
			bool bValid = true;
			for (int32 X = 0; X < Footprint.X && bValid; ++X)
			{
				for (int32 Y = 0; Y < Footprint.Y; ++Y)
				{
					const FIntPoint Cell = Anchor + FIntPoint(X, Y);
					if (!WarZoneByCell.Contains(Cell)) { bValid = false; break; }
					for (int32 PadX = -2; PadX <= 2 && bValid; ++PadX)
						for (int32 PadY = -2; PadY <= 2; ++PadY)
							if (SelectedFacilityCells.Contains(Cell + FIntPoint(PadX, PadY)))
							{ bValid = false; break; }
					Occupied.Add(Cell);
				}
			}
			if (!bValid)
				continue;
			for (const FIntPoint& Cell : Occupied)
				SelectedFacilityCells.Add(Cell);
			ReserveFacility(VisualSet, Anchor, Footprint, RotationQuarterTurns, Occupied, TileByCell);
			return true;
		}
		return false;
	};
	ReserveBestRect(EFacilityVisualSet::LongBarracks, FIntPoint(2, 1), 0);
	ReserveBestRect(EFacilityVisualSet::LongBarracks, FIntPoint(1, 2), 1);
	ReserveBestRect(EFacilityVisualSet::LinearTrench, FIntPoint(4, 1), 0);

	bool bFoundCheckpoint = false;
	float BestCheckpointDistanceSquared = TNumericLimits<float>::Max();
	FIntPoint CheckpointAnchor = FIntPoint::ZeroValue;
	FIntPoint CheckpointFootprint(1, 2);
	int32 CheckpointRotationQuarterTurns = 0;
	TArray<FIntPoint> CheckpointOccupiedCells;
	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(0, 1),
		FIntPoint(-1, 0), FIntPoint(0, -1)
	};

	TArray<FIntPoint> CheckpointSearchCells;
	TileByCell.GetKeys(CheckpointSearchCells);
	CheckpointSearchCells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	});
	for (const FIntPoint& SearchCell : CheckpointSearchCells)
	{
		AMapTile* const* SearchTilePtr = TileByCell.Find(SearchCell);
		if (SearchTilePtr == nullptr || !IsValid(*SearchTilePtr)
			|| (*SearchTilePtr)->GetType() != ETileType::Obstacle)
			continue;

		for (const FIntPoint& Direction : Directions)
		{
			const FIntPoint NeighborCell = SearchCell + Direction;
			if (SelectedFacilityCells.Contains(SearchCell)
				|| SelectedFacilityCells.Contains(NeighborCell))
			{
				continue;
			}
			AMapTile* const* NeighborPtr = TileByCell.Find(NeighborCell);
			if (NeighborPtr == nullptr || (*NeighborPtr)->GetType() != ETileType::Road)
				continue;

			const FVector Center = ((*SearchTilePtr)->GetActorLocation()
				+ (*NeighborPtr)->GetActorLocation()) * 0.5f;
			const float DistanceSquared = FVector2D(Center.X, Center.Y).SizeSquared();
			if (DistanceSquared >= BestCheckpointDistanceSquared)
				continue;

			bFoundCheckpoint = true;
			BestCheckpointDistanceSquared = DistanceSquared;
			CheckpointAnchor = FIntPoint(
				FMath::Min(SearchCell.X, NeighborCell.X),
				FMath::Min(SearchCell.Y, NeighborCell.Y));
			CheckpointRotationQuarterTurns = Direction.X != 0 ? 1 : 0;
			CheckpointFootprint = FIntPoint(1, 2);
			CheckpointOccupiedCells = { SearchCell, NeighborCell };
		}
	}

	if (bFoundCheckpoint)
	{
		ReserveFacility(
			EFacilityVisualSet::Checkpoint,
			CheckpointAnchor,
			CheckpointFootprint,
			CheckpointRotationQuarterTurns,
			CheckpointOccupiedCells,
			TileByCell);
	}

	// Reserve three themed districts outside the combat core.  They deliberately
	// consume ordinary None cells: the brother's metaball/road result stays the
	// authority, while the design manifest replaces only open visual cells with a
	// multi-cell POI.  Roads to each entrance are added by BuildTileDesignPlacements.
	int32 MinCellX = MAX_int32;
	int32 MinCellY = MAX_int32;
	int32 MaxCellX = MIN_int32;
	int32 MaxCellY = MIN_int32;
	FIntPoint SpawnCellSum = FIntPoint::ZeroValue;
	int32 SpawnCellCount = 0;
	for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
	{
		MinCellX = FMath::Min(MinCellX, Pair.Key.X);
		MinCellY = FMath::Min(MinCellY, Pair.Key.Y);
		MaxCellX = FMath::Max(MaxCellX, Pair.Key.X);
		MaxCellY = FMath::Max(MaxCellY, Pair.Key.Y);
		if (IsValid(Pair.Value) && Pair.Value->GetType() == ETileType::Spawn)
		{
			SpawnCellSum += Pair.Key;
			++SpawnCellCount;
		}
	}
	GridCellSpan = FMath::Max(MaxCellX - MinCellX + 1, MaxCellY - MinCellY + 1);
	// Recast is fed by this single floor rather than by geometry exported from every
	// tile, so it has to cover the generated grid and no more. Sized for a fixed
	// 900 m it left walkable void hanging off the edge of a smaller map.
	if (IsValid(NavigationFloor) && GridCellSpan > 0)
	{
		const float NavigationFloorScale = GridCellSpan * DesignCellSize / 100.0f;
		NavigationFloor->SetRelativeScale3D(
			FVector(NavigationFloorScale, NavigationFloorScale, 0.2f));
	}

	const FIntPoint AverageSpawnCell = SpawnCellCount > 0
		? FIntPoint(SpawnCellSum.X / SpawnCellCount, SpawnCellSum.Y / SpawnCellCount)
		: FIntPoint(MinCellX, MinCellY);
	auto LerpCell = [](int32 MinValue, int32 MaxValue, float Alpha)
	{
		return FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinValue), static_cast<float>(MaxValue), Alpha));
	};
	const FIntPoint DowntownTarget(
		AverageSpawnCell.X + FMath::Sign(-AverageSpawnCell.X) * 6,
		AverageSpawnCell.Y + FMath::Sign(-AverageSpawnCell.Y) * 6);
	const FIntPoint FactoryTarget(LerpCell(MinCellX, MaxCellX, 0.76f), LerpCell(MinCellY, MaxCellY, 0.26f));
	// The rural settlement is a lakeside hamlet - cabins, a pier and two rowing
	// boats. Dropped in the middle of a dry field it reads as a diorama someone
	// parked there, which is what the first placement looked like. Aim it just
	// inland of the border lake's waterline so the pier has water to sit on and the
	// boats have somewhere to be. ReserveThemedDistrict picks the nearest legal 2x2
	// to this point, and flooding never touches reserved cells, so the settlement
	// itself always ends up on dry land.
	const AGameModePG* LakeGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 LakeRaidSeed = IsValid(LakeGameMode) ? LakeGameMode->GetMapGenerationSeed() : 0;
	const FBorderLake RuralLake = GetBorderLake(
		LakeRaidSeed, FIntPoint(MinCellX, MinCellY), FIntPoint(MaxCellX, MaxCellY),
		BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	const FVector2D InlandDirection =
		(FVector2D((MinCellX + MaxCellX) * 0.5f, (MinCellY + MaxCellY) * 0.5f)
			- RuralLake.CentreCell).GetSafeNormal();
	const FVector2D RuralShorePoint =
		RuralLake.CentreCell + InlandDirection * (RuralLake.Radius + 1.5f);
	const FIntPoint RuralTarget(
		FMath::Clamp(FMath::RoundToInt(RuralShorePoint.X), MinCellX, MaxCellX),
		FMath::Clamp(FMath::RoundToInt(RuralShorePoint.Y), MinCellY, MaxCellY));
	// Cells-to-nearest-road for every cell, as one multi-source breadth-first sweep
	// from the whole network at once. Facility placement used to score only the
	// distance to its thematic target, so compounds landed an average of 13 cells
	// from any road and the access-spur pass then had to drag a 260 m service lane
	// out to each one - or give up, which it did for 7 to 10 of the 11 facilities.
	// Paying for the road afterwards was the wrong order; sitting near one is free.
	TMap<FIntPoint, int32> RoadDistanceByCell;
	{
		TArray<FIntPoint> Frontier;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
		{
			if (!IsValid(Pair.Value))
				continue;
			// The WarZone is deliberately excluded even though it is traversable:
			// it is a wide region, and counting it as road would mark most of the
			// middle of the map as road-adjacent and defeat the whole term.
			const ETileType Type = Pair.Value->GetType();
			if (Type == ETileType::Road || Type == ETileType::Spawn
				|| Type == ETileType::Exit || Type == ETileType::Obstacle)
			{
				RoadDistanceByCell.Add(Pair.Key, 0);
				Frontier.Add(Pair.Key);
			}
		}
		const FIntPoint RoadDistanceSteps[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (int32 Index = 0; Index < Frontier.Num(); ++Index)
		{
			const FIntPoint Current = Frontier[Index];
			const int32 NextDistance = RoadDistanceByCell[Current] + 1;
			for (const FIntPoint& Step : RoadDistanceSteps)
			{
				const FIntPoint Neighbor = Current + Step;
				if (!TileByCell.Contains(Neighbor) || RoadDistanceByCell.Contains(Neighbor))
					continue;
				RoadDistanceByCell.Add(Neighbor, NextDistance);
				Frontier.Add(Neighbor);
			}
		}
	}

	auto ReserveThemedDistrict = [this, &TileByCell, &SelectedFacilityCells, &RoadDistanceByCell](
		EFacilityVisualSet VisualSet,
		const FIntPoint& Footprint,
		const FIntPoint& Target)
	{
		bool bFound = false;
		int64 BestScore = TNumericLimits<int64>::Max();
		FIntPoint BestAnchor = FIntPoint::ZeroValue;
		TArray<FIntPoint> BestOccupied;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
		{
			const FIntPoint Candidate = Pair.Key;
			TArray<FIntPoint> Occupied;
			bool bValid = true;
			for (int32 X = 0; X < Footprint.X && bValid; ++X)
			{
				for (int32 Y = 0; Y < Footprint.Y; ++Y)
				{
					const FIntPoint Cell = Candidate + FIntPoint(X, Y);
					AMapTile* const* Tile = TileByCell.Find(Cell);
					if (Tile == nullptr || !IsValid(*Tile) || (*Tile)->GetType() != ETileType::None)
					{
						bValid = false;
						break;
					}
					for (int32 PadX = -1; PadX <= 1 && bValid; ++PadX)
						for (int32 PadY = -1; PadY <= 1; ++PadY)
							if (SelectedFacilityCells.Contains(Cell + FIntPoint(PadX, PadY)))
							{ bValid = false; break; }
					Occupied.Add(Cell);
				}
			}
			if (!bValid)
				continue;
			const int32 CenterX2 = Candidate.X * 2 + Footprint.X - 1;
			const int32 CenterY2 = Candidate.Y * 2 + Footprint.Y - 1;
			const int64 DX = static_cast<int64>(CenterX2) - Target.X * 2;
			const int64 DY = static_cast<int64>(CenterY2) - Target.Y * 2;
			// Only the part of the road distance a spur cannot cover is charged for.
			// Anything already inside the spur budget gets a paved approach for free,
			// so penalising it would push districts around for no gain; beyond the
			// budget the cost grows quadratically, the same shape as the target term
			// so the two stay comparable. DX/DY are in half-cells, hence the *4.
			const int32 RoadCells = RoadDistanceByCell.Contains(Candidate)
				? RoadDistanceByCell[Candidate]
				: MaxFacilitySpurSearchCells;
			const int64 RoadExcess = FMath::Max(0, RoadCells - MaxFacilitySpurCells);
			const int64 Score = DX * DX + DY * DY
				+ RoadExcess * RoadExcess * 4 * FacilityRoadProximityWeight;
			if (Score < BestScore)
			{
				bFound = true;
				BestScore = Score;
				BestAnchor = Candidate;
				BestOccupied = MoveTemp(Occupied);
			}
		}
		if (!bFound)
		{
			UE_LOG(LogTemp, Warning, TEXT("Themed district reservation failed: set=%s footprint=%dx%d"),
				*StaticEnum<EFacilityVisualSet>()->GetNameStringByValue(static_cast<int64>(VisualSet)),
				Footprint.X, Footprint.Y);
			return false;
		}
		for (const FIntPoint& Cell : BestOccupied)
			SelectedFacilityCells.Add(Cell);
		// road_cells is what the access-spur pass will have to bridge. Anything at or
		// under MaxFacilitySpurCells gets paved; above it the district is stranded,
		// which is exactly what this scoring term exists to stop happening.
		UE_LOG(LogTemp, Display,
			TEXT("Themed district placed: set=%s anchor=(%d,%d) road_cells=%d target_cells=%.1f"),
			*StaticEnum<EFacilityVisualSet>()->GetNameStringByValue(static_cast<int64>(VisualSet)),
			BestAnchor.X, BestAnchor.Y,
			RoadDistanceByCell.Contains(BestAnchor) ? RoadDistanceByCell[BestAnchor] : -1,
			FMath::Sqrt(static_cast<float>(
				FMath::Square(BestAnchor.X - Target.X) + FMath::Square(BestAnchor.Y - Target.Y))));
		ReserveFacility(VisualSet, BestAnchor, Footprint, 0, BestOccupied, TileByCell);
		return true;
	};
	ReserveThemedDistrict(EFacilityVisualSet::DowntownBlock, FIntPoint(6, 6), DowntownTarget);
	ReserveThemedDistrict(EFacilityVisualSet::FactoryConstruction, FIntPoint(2, 2), FactoryTarget);
	ReserveThemedDistrict(EFacilityVisualSet::RuralHideout, FIntPoint(2, 2), RuralTarget);

	BuildTileDesignPlacements(TileByCell);

	Tags.AddUnique(ReservedTag);
	Tags.AddUnique(TEXT("FacilityPlacementPreview"));
	if (!bUseRuntimeBlueprintTiles)
	{
		ShowWarehouseProxy(GetFootprintCenter(FacilityPlacements[0]));
		ShowYardProxy(GetFootprintCenter(FacilityPlacements[1]));
	}
	else
	{
		UStaticMeshComponent* ProxyComponents[] = {
			FloorProxy, BackWallProxy, LeftWallProxy, RightWallProxy,
			FrontWallLeftProxy, FrontWallRightProxy, RoofProxy,
			YardFloorProxy, YardCoverNorthProxy, YardCoverSouthProxy,
			YardCoverWestProxy, YardCoverEastProxy
		};
		for (UStaticMeshComponent* Component : ProxyComponents)
		{
			if (IsValid(Component))
				Component->SetVisibility(false, true);
		}
	}
	BuildLightweightWorldVisuals();
	SpawnRuntimeBlueprintTiles();
	BuildGameplayPointMarkers();
	// Runs alongside runtime Blueprint tiles: their HISM dressing covers per-tile
	// props, while this pass scatters grass/shrub clumps across meadow, scrub,
	// ruin and open-ground cells to break up the 20m tile seams between them.
	BuildPCGDressingGraph();
	NavigationValidationStartTimeSeconds = FPlatformTime::Seconds();
	FacilityDesignLevelInstances.Reset();
	FacilityLoadRequestTimeSeconds.Reset();
	LoggedFacilityDesignLevelIndices.Reset();
	for (int32 FacilityIndex = 0; FacilityIndex < FacilityPlacements.Num(); ++FacilityIndex)
		LoadFacilityDesignLevel(FacilityPlacements[FacilityIndex], FacilityIndex);
	BuildBorderMountains();

	// The brother's AMapTile actors remain authoritative logical records. Once
	// converted, disable only their runtime presentation/collision so HISM and
	// Level Instances become the visible/playable world without destroying data.
	for (AMapTile* Tile : AllTiles)
	{
		if (!IsValid(Tile))
			continue;
		Tile->SetActorHiddenInGame(true);
		Tile->SetActorEnableCollision(false);
		Tile->SetActorTickEnabled(false);
	}

	GetWorldTimerManager().ClearTimer(RetryTimer);

	UE_LOG(LogTemp, Display,
		TEXT("Reserved facility set: total=%d Compound3x3 origin=(%d,%d) Camps2x2=%d Checkpoint found=%s grid_step=%.1f"),
		FacilityPlacements.Num(),
		CompoundAnchor.X, CompoundAnchor.Y, SelectedFacilityAnchors.Num(),
		bFoundCheckpoint ? TEXT("true") : TEXT("false"), GridStep);
}

void AWarZoneFootprintPreview::BuildGameplayPointMarkers()
{
	LevelDesignPoints.Reset();
	TMap<ELevelDesignPointType, int32> Counts;
	auto IsProtectedFromAI = [this](const FIntPoint& Cell)
	{
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		{
			const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
				+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
			if (Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
				return true; // 60m minimum spawn safe band
			if (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2)
				return true; // do not camp directly on the extraction stencil
		}
		// The boat landing is a spawn too, but it is a gameplay point on the rural
		// hamlet, not a Spawn tile, so the band above cannot see it. Without this
		// the natural encounters crept to 49.5 m of the landing and the hamlet
		// itself was tallied as a facility missing its resident AI.
		if (bHasBorderLake)
		{
			for (const FFacilityPlacement& Facility : FacilityPlacements)
			{
				if (Facility.VisualSet != EFacilityVisualSet::RuralHideout)
					continue;
				for (const FIntPoint& Occupied : Facility.OccupiedCells)
					if (FMath::Abs(Cell.X - Occupied.X) + FMath::Abs(Cell.Y - Occupied.Y) < 3)
						return true;
			}
		}
		return false;
	};

	auto AddPoint = [this, &Counts](
		ELevelDesignPointType Type,
		const FIntPoint& Cell,
		const FVector& Offset,
		const TCHAR* Prefix,
		const TCHAR* Archetype,
		uint8 Tier,
		float RadiusCm,
		int32 Capacity)
	{
		const int32 Index = Counts.FindOrAdd(Type)++;
		FLevelDesignPoint& Point = LevelDesignPoints.AddDefaulted_GetRef();
		Point.Type = Type;
		Point.GridCell = Cell;
		FVector TacticalOffset = Offset;
		if (const FTileDesignPlacement* Placement = TileDesignPlacements.FindByPredicate(
			[Cell](const FTileDesignPlacement& Candidate) { return Candidate.GridCell == Cell; }))
		{
			const FRotator Rotation(0.0f, Placement->RotationQuarterTurns * 90.0f, 0.0f);
			TacticalOffset = Rotation.RotateVector(Offset);
			switch (Type)
			{
			case ELevelDesignPointType::Spawn:
				TacticalOffset += Rotation.RotateVector(FVector(-600.0f, 0.0f, 0.0f)); break;
			case ELevelDesignPointType::Exit:
				TacticalOffset += Rotation.RotateVector(FVector(550.0f, 0.0f, 0.0f)); break;
			case ELevelDesignPointType::Loot:
				TacticalOffset += FVector(-320.0f, 480.0f, 0.0f); break;
			case ELevelDesignPointType::AISpawn:
				TacticalOffset += FVector(420.0f, -420.0f, 0.0f); break;
			case ELevelDesignPointType::Quest:
				TacticalOffset += FVector(0.0f, 0.0f, 0.0f); break;
			}
		}
		Point.WorldLocation = FVector(
			Cell.X * DesignCellSize,
			Cell.Y * DesignCellSize,
			120.0f + GetSurfaceElevationForCell(Cell)) + TacticalOffset;
		// The authored offsets are preferred, but random tactical variants can place
		// cover there. Search a deterministic 3x3 pocket so every emitted point is
		// actually spawnable without changing the selected tile or layout hash.
		const FVector CandidateOffsets[] = {
			FVector::ZeroVector,
			FVector(320, 0, 0), FVector(-320, 0, 0), FVector(0, 320, 0), FVector(0, -320, 0),
			FVector(320, 320, 0), FVector(-320, 320, 0), FVector(320, -320, 0), FVector(-320, -320, 0)
		};
		FCollisionQueryParams PointQuery(SCENE_QUERY_STAT(LevelDesignPointPlacement), false);
		const FCollisionShape PointCapsule = FCollisionShape::MakeCapsule(55.0f, 95.0f);
		for (const FVector& CandidateOffset : CandidateOffsets)
		{
			const FVector Candidate = Point.WorldLocation + CandidateOffset;
			TArray<FOverlapResult> Overlaps;
			const bool bOverlap = GetWorld()->OverlapMultiByChannel(
				Overlaps,
				Candidate + FVector(0, 0, 95.0f),
				FQuat::Identity,
				ECC_Pawn,
				PointCapsule,
				PointQuery);
			const bool bBlocked = bOverlap && Overlaps.ContainsByPredicate(
				[this](const FOverlapResult& Result)
				{
					const AActor* HitActor = Result.GetActor();
					const UPrimitiveComponent* HitComponent = Result.GetComponent();
					return IsValid(HitActor) && HitActor != this
						&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
						&& !(bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
						&& IsValid(HitComponent)
						&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
				});
			const bool bTooCloseToSpawn = Type == ELevelDesignPointType::Spawn
				&& LevelDesignPoints.ContainsByPredicate(
					[&Point, &Candidate](const FLevelDesignPoint& Existing)
					{
						return &Existing != &Point
							&& Existing.Type == ELevelDesignPointType::Spawn
							&& FVector::DistSquared2D(Existing.WorldLocation, Candidate) < FMath::Square(250.0f);
					});
			if (!bBlocked && !bTooCloseToSpawn)
			{
				Point.WorldLocation = Candidate;
				break;
			}
		}
		Point.PointId = FName(*FString::Printf(TEXT("%s_%02d"), Prefix, Index));
		Point.ArchetypeId = FName(Archetype);
		Point.Tier = FMath::Clamp<uint8>(Tier, 1, 3);
		Point.RadiusCm = FMath::Max(50.0f, RadiusCm);
		Point.Capacity = FMath::Max(1, Capacity);
		Point.PointSeed = static_cast<int64>(HashCombine(
			LayoutHash,
			HashCombine(
				GetTypeHash(static_cast<uint8>(Type)),
				HashCombine(GetTypeHash(Cell.X), HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Index))))));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATargetPoint* Marker = GetWorld()->SpawnActor<ATargetPoint>(
			ATargetPoint::StaticClass(),
			Point.WorldLocation,
			FRotator::ZeroRotator,
			SpawnParameters);
		if (IsValid(Marker))
		{
			Marker->SetActorLabel(Point.PointId.ToString());
			Marker->Tags.AddUnique(TEXT("LevelDesignPoint"));
			Marker->Tags.AddUnique(FName(Prefix));
			Marker->Tags.AddUnique(Point.PointId);
			Marker->SetFolderPath(TEXT("RuntimeDesign/Points"));
		}
	};

	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		// Nothing gameplay-facing belongs in the lake.
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (Placement.Visual == ETileDesignVisual::Spawn)
		{
			// Four candidates prevent a future squad from stacking into one capsule.
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		}
		else if (Placement.Visual == ETileDesignVisual::Exit)
			AddPoint(ELevelDesignPointType::Exit, Placement.GridCell, FVector::ZeroVector, TEXT("ExitPoint"), TEXT("Extraction"), 1, 600, 8);
		else if (((Placement.Visual == ETileDesignVisual::WarZoneGround && Placement.LocalSeed % 17 == 0)
				|| ((Placement.Visual == ETileDesignVisual::OpenGround
						|| Placement.Visual == ETileDesignVisual::Ruins)
					&& Placement.LocalSeed % 31 == 0))
			&& !IsProtectedFromAI(Placement.GridCell))
		{
			const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::WarZoneGround
				? TEXT("ScavPatrol") : TEXT("PerimeterPatrol");
			AddPoint(ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector, TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
		}
		else if (Placement.Visual == ETileDesignVisual::Ruins
			&& Placement.LocalSeed % 3 == 0)
		{
			const uint8 LootTier = Placement.LocalSeed % 19 == 0 ? 3 : (Placement.LocalSeed % 5 == 0 ? 2 : 1);
			AddPoint(ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector, TEXT("LootPoint"), TEXT("RuinsLooseLoot"), LootTier, 100, 1);
		}

		const bool bNaturalTile = Placement.Visual == ETileDesignVisual::NatureMeadow
			|| Placement.Visual == ETileDesignVisual::NatureForestSparse
			|| Placement.Visual == ETileDesignVisual::NatureForestDense
			|| Placement.Visual == ETileDesignVisual::NatureRocky
			|| Placement.Visual == ETileDesignVisual::NatureScrub
			|| Placement.Visual == ETileDesignVisual::NatureAmbush
			|| Placement.Visual == ETileDesignVisual::NatureServiceCamp
			|| Placement.Visual == ETileDesignVisual::NatureDitch;
		if (bNaturalTile)
		{
			// A deterministic low-density lattice makes the entire 900m field useful
			// for a loot-shooter. The server can serialize these markers in the future
			// manifest; no client-side random choice is involved.
			const int32 StableX = Placement.GridCell.X + 64;
			const int32 StableY = Placement.GridCell.Y + 64;
			const bool bNaturalLoot = (StableX % 7 == 0 && StableY % 7 == 0)
				|| (Placement.Visual == ETileDesignVisual::NatureServiceCamp && Placement.LocalSeed % 3 == 0);
			if (bNaturalLoot)
			{
				const bool bHighValue = Placement.Visual == ETileDesignVisual::NatureServiceCamp
					|| Placement.Visual == ETileDesignVisual::NatureRocky;
				AddPoint(ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector,
					TEXT("LootPoint"), bHighValue ? TEXT("FieldCache") : TEXT("HiddenStash"),
					bHighValue ? 2 : 1, 120, 1);
			}

			const bool bNaturalEncounter = (StableX % 8 == 4 && StableY % 8 == 4)
				|| (Placement.Visual == ETileDesignVisual::NatureAmbush && Placement.LocalSeed % 11 == 0);
			if (bNaturalEncounter && !IsProtectedFromAI(Placement.GridCell))
			{
				const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::NatureAmbush
					? TEXT("ForestAmbush") : TEXT("WildernessPatrol");
				AddPoint(ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector,
					TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
			}
		}
	}

	auto AddFacilityPoint = [this, &AddPoint](
		const FFacilityPlacement& Facility,
		ELevelDesignPointType Type,
		const FVector& LocalSocket,
		const TCHAR* Prefix,
		const TCHAR* Archetype,
		uint8 Tier,
		float RadiusCm,
		int32 Capacity)
	{
		if (Facility.OccupiedCells.IsEmpty())
			return;
		const FVector FacilityCenter = GetDesignFootprintCenter(Facility);
		const FRotator FacilityRotation(0.0f, Facility.RotationQuarterTurns * 90.0f, 0.0f);
		const FVector DesiredWorld = FacilityCenter + FacilityRotation.RotateVector(LocalSocket);
		const FIntPoint SocketCell = *Algo::MinElementBy(
			Facility.OccupiedCells,
			[&DesiredWorld](const FIntPoint& Cell)
			{
				return FVector2D(
					Cell.X * DesignCellSize - DesiredWorld.X,
					Cell.Y * DesignCellSize - DesiredWorld.Y).SizeSquared();
			});
		const FVector CellBase(
			SocketCell.X * DesignCellSize,
			SocketCell.Y * DesignCellSize,
			120.0f + Facility.BaseElevationCm);
		AddPoint(Type, SocketCell, DesiredWorld - CellBase, Prefix, Archetype, Tier, RadiusCm, Capacity);
	};

	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		if (Facility.VisualSet == EFacilityVisualSet::Warehouse)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1850, -830, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 3, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(2100, 1850, 580), TEXT("LootPoint"), TEXT("WarehouseContainer"), 3, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-500, 1500, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1400, -1200, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			// The 3x5 compound's outer hall rows. Growing the core ate WarZone_Mid
			// band cells and their tile loot with them - the map-wide count fell
			// under the verifier's 40 - so the halls that replaced those cells carry
			// the loot instead.
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(0, -3900, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(0, 3800, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1400, 2600, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-900, -1650, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(900, 650, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(2100, 1850, 580), TEXT("QuestPoint"), TEXT("WarZonePrimaryObjective"), 3, 160, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::Yard)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, -700, 120), TEXT("LootPoint"), TEXT("YardStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1050, -950, 360), TEXT("LootPoint"), TEXT("YardStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(1200, 900, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-1200, -900, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::LongBarracks)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1100, 350, 120), TEXT("LootPoint"), TEXT("BarracksLocker"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1100, -350, 120), TEXT("LootPoint"), TEXT("BarracksLocker"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-1500, -450, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			if ((Facility.LocalSeed & 1u) == 0u)
				AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(1100, -350, 120), TEXT("QuestPoint"), TEXT("OptionalFacilityObjective"), 2, 140, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::LinearTrench)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-2600, 0, 120), TEXT("LootPoint"), TEXT("TrenchCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(2600, 0, 120), TEXT("LootPoint"), TEXT("TrenchCache"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-3400, 0, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::DowntownBlock)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1600, 700, 120), TEXT("LootPoint"), TEXT("DowntownShopCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1450, -750, 120), TEXT("LootPoint"), TEXT("DowntownBackroom"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(0, 1400, 120), TEXT("AISpawnPoint"), TEXT("DowntownPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(1500, 850, 120), TEXT("QuestPoint"), TEXT("DowntownObjective"), 2, 140, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::FactoryConstruction)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-800, -700, 120), TEXT("LootPoint"), TEXT("FactoryToolCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, 650, 120), TEXT("LootPoint"), TEXT("FactoryOfficeCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(900, -900, 120), TEXT("AISpawnPoint"), TEXT("FactoryPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::RuralHideout)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-900, 250, 120), TEXT("LootPoint"), TEXT("RuralCabinStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, -250, 120), TEXT("LootPoint"), TEXT("RuralCaravanStash"), 1, 100, 1);
			// The hamlet hosts the boat-landing spawn whenever the lake exists, and
			// a facility that spawns players keeps no resident AI - the same rule
			// the spawn safe band applies everywhere else. A 40 m hamlet cannot hold
			// both a spawn and an ambush 50 m apart (measured 26 m: pass=false).
			if (!bHasBorderLake)
				AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(0, -900, 120), TEXT("AISpawnPoint"), TEXT("RuralAmbush"), 1, 260, 2);
			if (bHasBorderLake)
			{
				// "Arrived by boat": a spawn on the hamlet's water-facing edge. The
				// lake corner moves per seed, so aim at the recorded lake centre and
				// undo the facility's yaw - a fixed socket would face the water only
				// on the seeds that happen to rotate the hamlet the authored way.
				const FVector FacilityCentre = GetDesignFootprintCenter(Facility);
				const FVector2D ToLake = (BorderLakeCentreCell * DesignCellSize
					- FVector2D(FacilityCentre.X, FacilityCentre.Y)).GetSafeNormal();
				const FRotator FacilityYaw(0.0f, Facility.RotationQuarterTurns * 90.0f, 0.0f);
				const FVector LakeSocket = FacilityYaw.UnrotateVector(
					FVector(ToLake.X, ToLake.Y, 0.0f) * 1900.0f) + FVector(0.0f, 0.0f, 120.0f);
				AddFacilityPoint(Facility, ELevelDesignPointType::Spawn, LakeSocket,
					TEXT("SpawnPoint"), TEXT("BoatLanding"), 2, 150, 2);
			}
		}
		else
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector::ZeroVector, TEXT("LootPoint"), TEXT("CheckpointCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-500, 500, 120), TEXT("AISpawnPoint"), TEXT("CheckpointGuard"), 2, 300, 2);
		}
	}

	RebuildGameplayPointHash();

	// 오브젝트 스포너 등 데이터 소비자에게 알린다. 재시도로 다시 만들어져도 첫 확정만 알린다.
	if (!bLevelDesignPointsBuilt)
	{
		bLevelDesignPointsBuilt = true;
		OnLevelDesignPointsBuilt.Broadcast(LevelDesignPoints);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Design points: total=%d spawn=%d loot=%d ai=%d exit=%d quest=%d point_hash=%08X"),
		LevelDesignPoints.Num(),
		Counts.FindRef(ELevelDesignPointType::Spawn),
		Counts.FindRef(ELevelDesignPointType::Loot),
		Counts.FindRef(ELevelDesignPointType::AISpawn),
		Counts.FindRef(ELevelDesignPointType::Exit),
		Counts.FindRef(ELevelDesignPointType::Quest),
		GameplayPointHash);
}

void AWarZoneFootprintPreview::RebuildGameplayPointHash()
{
	GameplayPointHash = 0;
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		// FName's runtime comparison index is process-local. Hash the serialized
		// string contents so server and clients agree after independent launches.
		GameplayPointHash = HashCombine(GameplayPointHash, FCrc::StrCrc32(*Point.PointId.ToString()));
		GameplayPointHash = HashCombine(GameplayPointHash, FCrc::StrCrc32(*Point.ArchetypeId.ToString()));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.GridCell.X));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.GridCell.Y));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.Tier));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.X)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Y)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Z)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.RadiusCm)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.Capacity));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.PointSeed));
	}
}

void AWarZoneFootprintPreview::ResolveGameplayPointSafety()
{
	if (bResolvedGameplayPointSafety || !AreAllFacilityLevelsLoaded() || LevelDesignPoints.IsEmpty())
		return;

	TArray<FVector> CandidateOffsets;
	for (int32 X = -4; X <= 4; ++X)
		for (int32 Y = -4; Y <= 4; ++Y)
			CandidateOffsets.Add(FVector(X * 225.0f, Y * 225.0f, 0.0f));
	CandidateOffsets.Sort([](const FVector& A, const FVector& B)
	{
		const float ADistance = A.SizeSquared2D();
		const float BDistance = B.SizeSquared2D();
		if (!FMath::IsNearlyEqual(ADistance, BDistance)) return ADistance < BDistance;
		return !FMath::IsNearlyEqual(A.X, B.X) ? A.X < B.X : A.Y < B.Y;
	});

	auto IsBlocked = [this](const FVector& Location)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ResolveGameplayPointSafety), false);
		const bool bOverlap = GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Location + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(55.0f, 95.0f),
			Query);
		return bOverlap && Overlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != this
					&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
					&& !(bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
					&& IsValid(HitComponent)
					&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			});
	};

	int32 RelocatedCount = 0;
	int32 UnresolvedCount = 0;
	TArray<FString> UnresolvedIds;
	TArray<FVector> ResolvedSpawnLocations;
	for (FLevelDesignPoint& Point : LevelDesignPoints)
	{
		const bool bSpawnTooClose = Point.Type == ELevelDesignPointType::Spawn
			&& ResolvedSpawnLocations.ContainsByPredicate(
				[&Point](const FVector& Existing)
				{
					return FVector::DistSquared2D(Existing, Point.WorldLocation) < FMath::Square(250.0f);
				});
		if (!IsBlocked(Point.WorldLocation) && !bSpawnTooClose)
		{
			if (Point.Type == ELevelDesignPointType::Spawn)
				ResolvedSpawnLocations.Add(Point.WorldLocation);
			continue;
		}

		const FVector CellCenter(
			Point.GridCell.X * DesignCellSize,
			Point.GridCell.Y * DesignCellSize,
			120.0f + GetSurfaceElevationForCell(Point.GridCell));
		bool bFound = false;
		for (const FVector& Offset : CandidateOffsets)
		{
			const FVector Candidate = CellCenter + Offset;
			if (IsBlocked(Candidate))
				continue;
			if (Point.Type == ELevelDesignPointType::Spawn
				&& ResolvedSpawnLocations.ContainsByPredicate(
					[&Candidate](const FVector& Existing)
					{
						return FVector::DistSquared2D(Existing, Candidate) < FMath::Square(250.0f);
					}))
			{
				continue;
			}
			Point.WorldLocation = Candidate;
			if (Point.Type == ELevelDesignPointType::Spawn)
				ResolvedSpawnLocations.Add(Point.WorldLocation);
			bFound = true;
			++RelocatedCount;
			for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
			{
				if (It->Tags.Contains(Point.PointId))
				{
					It->SetActorLocation(Point.WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
					break;
				}
			}
			break;
		}
		if (!bFound)
		{
			++UnresolvedCount;
			if (UnresolvedIds.Num() < 8)
				UnresolvedIds.Add(Point.PointId.ToString());
		}
	}

	bResolvedGameplayPointSafety = true;
	RebuildGameplayPointHash();
	UE_LOG(LogTemp, Display,
		TEXT("Gameplay point safety resolution: relocated=%d unresolved=%d final_point_hash=%08X pass=%s sample=[%s]"),
		RelocatedCount,
		UnresolvedCount,
		GameplayPointHash,
		UnresolvedCount == 0 ? TEXT("true") : TEXT("false"),
		*FString::Join(UnresolvedIds, TEXT(",")));
}

void AWarZoneFootprintPreview::BuildPCGDressingGraph()
{
	if (!IsValid(DressingPCGComponent))
		return;

	static const TSoftObjectPtr<UStaticMesh> GrassMeshes[] = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarA.SM_vlkhcbxia_VarA"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarC.SM_vlkhcbxia_VarC"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarF.SM_vlkhcbxia_VarF")))
	};
	// Execution-plan requirement: mix in at least two shrub species so grass-area
	// tiles (Meadow/Scrub) don't read as a single mesh stamped on a grid.
	static const TSoftObjectPtr<UStaticMesh> ShrubMeshes[] = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Shrubs_1.Shrubs_1"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2")))
	};

	RuntimeDressingGraph = NewObject<UPCGGraph>(DressingPCGComponent, TEXT("RuntimeDressingGraph"), RF_Transient);
	UPCGCreatePointsSettings* CreatePointsSettings = nullptr;
	UPCGStaticMeshSpawnerSettings* SpawnerSettings = nullptr;
	UPCGNode* CreatePointsNode = RuntimeDressingGraph->AddNodeOfType<UPCGCreatePointsSettings>(CreatePointsSettings);
	UPCGNode* SpawnerNode = RuntimeDressingGraph->AddNodeOfType<UPCGStaticMeshSpawnerSettings>(SpawnerSettings);
	if (!IsValid(CreatePointsNode) || !IsValid(SpawnerNode) || !IsValid(CreatePointsSettings) || !IsValid(SpawnerSettings))
		return;

	CreatePointsSettings->CoordinateSpace = EPCGCoordinateSpace::World;
	CreatePointsSettings->bCullPointsOutsideVolume = false;
	FRandomStream RandomStream(DressingPCGComponent->Seed);
	int32 CandidateCellCount = 0;
	int32 MeadowCellCount = 0;
	int32 ScrubCellCount = 0;
	// Facility footprints get custom-raised/lowered terrain pads and access ramps
	// that this pass has no visibility into (TileDesignPlacements only carries the
	// flat pre-elevation Z). A clump anchored one cell outside a facility's
	// reserved footprint can end up floating over or sinking into that pad, so
	// skip a one-cell buffer around every facility instead of guessing its height.
	auto IsNearFacilityFootprint = [this](const FIntPoint& Cell)
	{
		for (const FFacilityPlacement& Facility : FacilityPlacements)
		{
			if (Cell.X >= Facility.AnchorCell.X - 1
				&& Cell.X <= Facility.AnchorCell.X + Facility.Footprint.X
				&& Cell.Y >= Facility.AnchorCell.Y - 1
				&& Cell.Y <= Facility.AnchorCell.Y + Facility.Footprint.Y)
			{
				return true;
			}
		}
		return false;
	};
	int32 FacilityAdjacentSkipCount = 0;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (IsNearFacilityFootprint(Placement.GridCell))
		{
			++FacilityAdjacentSkipCount;
			continue;
		}

		// Grass-area visuals only. Forest/rocky/ambush/service-camp tiles author
		// their own HISM dressing in ATacticalTileActor; this pass fills the open
		// meadow, scrub, ruin and clearing tiles that otherwise read as bare,
		// repeated ground between them. Cell divisor controls how many of the
		// eligible cells get a clump at all; clump count controls how many
		// clumps land per chosen cell -- together these approximate the
		// "Meadow reads full, Scrub is patchier" density spread from the level
		// design plan without duplicating the WarZone band-resolution logic
		// that SpawnRuntimeBlueprintTiles uses for its own HISM density scale.
		int32 CellDivisor;
		int32 ClumpCount;
		const bool bIsMeadow = Placement.Visual == ETileDesignVisual::NatureMeadow;
		const bool bIsScrub = Placement.Visual == ETileDesignVisual::NatureScrub;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::NatureMeadow: CellDivisor = 4; ClumpCount = 2; break;
		case ETileDesignVisual::NatureScrub: CellDivisor = 7; ClumpCount = 1; break;
		case ETileDesignVisual::Ruins: CellDivisor = 13; ClumpCount = 2; break;
		case ETileDesignVisual::OpenGround: CellDivisor = 13; ClumpCount = 1; break;
		default: continue;
		}

		const uint32 CellHash = HashCombine(GetTypeHash(Placement.GridCell.X), GetTypeHash(Placement.GridCell.Y));
		if (CellHash % CellDivisor != 0)
			continue;

		++CandidateCellCount;
		if (bIsMeadow) ++MeadowCellCount;
		if (bIsScrub) ++ScrubCellCount;
		for (int32 ClumpIndex = 0; ClumpIndex < ClumpCount; ++ClumpIndex)
		{
			const FVector Location = Placement.WorldLocation + FVector(
				RandomStream.FRandRange(-750.0f, 750.0f),
				RandomStream.FRandRange(-750.0f, 750.0f),
				5.0f);
			const float UniformScale = RandomStream.FRandRange(0.65f, 1.2f);
			const FTransform Transform(
				FRotator(0.0f, RandomStream.FRandRange(0.0f, 360.0f), 0.0f),
				Location,
				FVector(UniformScale));
			FPCGPoint& Point = CreatePointsSettings->PointsToCreate.Emplace_GetRef(
				Transform, 1.0f, RandomStream.RandHelper(MAX_int32));
			Point.SetExtents(FVector(30.0f, 30.0f, 60.0f));
		}
	}

	UPCGMeshSelectorWeighted* MeshSelector = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
	if (!IsValid(MeshSelector))
		return;
	for (const TSoftObjectPtr<UStaticMesh>& GrassMesh : GrassMeshes)
	{
		FPCGMeshSelectorWeightedEntry& Entry = MeshSelector->MeshEntries.Emplace_GetRef(GrassMesh, 3);
		Entry.Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Entry.Descriptor.bCanEverAffectNavigation = false;
		Entry.Descriptor.InstanceStartCullDistance = 5000;
		Entry.Descriptor.InstanceEndCullDistance = 18000;
		Entry.Descriptor.ComponentTags.Add(TEXT("PCG_Dressing"));
	}
	// Lower relative weight than grass (2 x weight-1 vs 3 x weight-3) so shrubs
	// read as an occasional accent rather than half the ground cover.
	for (const TSoftObjectPtr<UStaticMesh>& ShrubMesh : ShrubMeshes)
	{
		FPCGMeshSelectorWeightedEntry& Entry = MeshSelector->MeshEntries.Emplace_GetRef(ShrubMesh, 1);
		Entry.Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Entry.Descriptor.bCanEverAffectNavigation = false;
		Entry.Descriptor.InstanceStartCullDistance = 6000;
		Entry.Descriptor.InstanceEndCullDistance = 22000;
		Entry.Descriptor.ComponentTags.Add(TEXT("PCG_Dressing"));
	}

	RuntimeDressingGraph->AddLabeledEdge(
		CreatePointsNode,
		PCGPinConstants::DefaultOutputLabel,
		SpawnerNode,
		PCGPinConstants::DefaultInputLabel);
	RuntimeDressingGraph->AddLabeledEdge(
		SpawnerNode,
		PCGPinConstants::DefaultOutputLabel,
		RuntimeDressingGraph->GetOutputNode(),
		PCGPinConstants::DefaultOutputLabel);

	DressingPCGComponent->SetGraph(RuntimeDressingGraph);
	DressingPCGComponent->GenerateLocal(true);
	UE_LOG(LogTemp, Display,
		TEXT("PCG dressing requested: seed=%d candidate_cells=%d (meadow=%d scrub=%d) facility_skipped=%d points=%d meshes=%d collision=false navigation=false"),
		DressingPCGComponent->Seed,
		CandidateCellCount,
		MeadowCellCount,
		ScrubCellCount,
		FacilityAdjacentSkipCount,
		CreatePointsSettings->PointsToCreate.Num(),
		UE_ARRAY_COUNT(GrassMeshes) + UE_ARRAY_COUNT(ShrubMeshes));
}

void AWarZoneFootprintPreview::VerifyPCGDressing()
{
	if (bLoggedPCGDressing || !IsValid(DressingPCGComponent) || DressingPCGComponent->IsGenerating())
		return;

	if (!DressingPCGComponent->bGenerated)
		return;

	bLoggedPCGDressing = true;
	int32 ManagedResourceCount = 0;
	DressingPCGComponent->ForEachConstManagedResource(
		[&ManagedResourceCount](const UPCGManagedResource*)
		{
			++ManagedResourceCount;
		});
	UE_LOG(LogTemp, Display,
		TEXT("PCG dressing generated: generated=true procedural_instances=%s managed_resources=%d"),
		DressingPCGComponent->AreProceduralInstancesInUse() ? TEXT("true") : TEXT("false"),
		ManagedResourceCount);
}

void AWarZoneFootprintPreview::BuildShoreTransitionMap(
	const TSet<FIntPoint>& LakeCells,
	TMap<FIntPoint, TPair<int32, int32>>& OutShoreTileByCell) const
{
	OutShoreTileByCell.Reset();
	if (LakeCells.IsEmpty())
		return;

	// Marching squares. A cell corner is wet when any of the four cells meeting there
	// is water, so the waterline cuts across cells instead of running along their
	// edges. Two neighbours always share two corners, and the generated meshes
	// interpolate each edge purely from that edge's two corner heights, so their
	// profiles are identical from both sides - no wall can appear between them.
	//
	// Choosing by *sides* instead was the earlier mistake: a beach cell's edge runs
	// -110 to +20 along its length while the flat cell beside it is +20 throughout,
	// which put a 130 cm step wherever the shoreline ended.
	auto IsCornerWet = [&LakeCells](const FIntPoint& Cell, int32 CornerIndex)
	{
		// Corner order SW, SE, NE, NW - matching the generated meshes.
		static const FIntPoint CornerOffsets[4][4] = {
			{ FIntPoint(0, 0), FIntPoint(-1, 0), FIntPoint(0, -1), FIntPoint(-1, -1) },
			{ FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(1, -1) },
			{ FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(1, 1) },
			{ FIntPoint(0, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(-1, 1) }
		};
		for (int32 Index = 0; Index < 4; ++Index)
			if (LakeCells.Contains(Cell + CornerOffsets[CornerIndex][Index]))
				return true;
		return false;
	};

	// Base masks in the same order the generator writes its assets.
	static const uint8 BaseMasks[] = { 0b0001, 0b0011, 0b0101, 0b0111 };
	auto RotateMask = [](uint8 Mask, int32 QuarterTurns)
	{
		uint8 Rotated = 0;
		for (int32 Bit = 0; Bit < 4; ++Bit)
			if (Mask & (1 << Bit))
				Rotated |= 1 << ((Bit + QuarterTurns) % 4);
		return Rotated;
	};

	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		uint8 CornerMask = 0;
		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
			if (IsCornerWet(Placement.GridCell, CornerIndex))
				CornerMask |= 1 << CornerIndex;

		// All dry is ordinary flat ground. All wet means fully submerged, which the
		// lake bed and water sheet already cover - but only for cells the generator
		// actually marked as water. A *land* cell touching water on two or more sides
		// also ends up with four wet corners, and it used to fall between both paths:
		// no shore mesh, and a flat slab left at Z=20 while everything around it
		// dropped to the lake floor. Those were the holes along the shoreline.
		if (CornerMask == 0)
			continue;
		if (CornerMask == 0b1111)
		{
			OutShoreTileByCell.Add(Placement.GridCell, TPair<int32, int32>(SubmergedShoreVariant, 0));
			continue;
		}

		for (int32 VariantIndex = 0; VariantIndex < UE_ARRAY_COUNT(BaseMasks); ++VariantIndex)
		{
			bool bMatched = false;
			for (int32 QuarterTurns = 0; QuarterTurns < 4 && !bMatched; ++QuarterTurns)
			{
				if (RotateMask(BaseMasks[VariantIndex], QuarterTurns) != CornerMask)
					continue;
				OutShoreTileByCell.Add(
					Placement.GridCell, TPair<int32, int32>(VariantIndex, QuarterTurns));
				bMatched = true;
			}
			if (bMatched)
				break;
		}
	}
}

void AWarZoneFootprintPreview::BuildLightweightWorldVisuals()
{
	// Use the engine's batch path so navigation bounds are cached only after the
	// complete HISM instance set exists. Individual AddInstance calls can expose
	// an intermediate invalid bound to the dynamic navigation system.
	RoadSurfaceHISM->SetCanEverAffectNavigation(false);
	GroundHISM->ClearInstances();
	WarZoneGroundHISM->ClearInstances();
	TransitionGroundHISM->ClearInstances();
	RoadSurfaceHISM->ClearInstances();
	LakeBedHISM->ClearInstances();
	LakeWaterHISM->ClearInstances();

	auto MakeCubeTransform = [](UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& WorldLocation, const FVector& WorldSize)
	{
		return FTransform(FRotator::ZeroRotator, WorldLocation, WorldSize / 100.0f)
			.GetRelativeTransform(Component->GetComponentTransform());
	};
	auto MakeOrientedCubeTransform = [](UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& WorldLocation, const FRotator& WorldRotation, const FVector& WorldSize)
	{
		return FTransform(WorldRotation, WorldLocation, WorldSize / 100.0f)
			.GetRelativeTransform(Component->GetComponentTransform());
	};
	TArray<FTransform> GroundTransforms;
	TArray<FTransform> WarZoneGroundTransforms;
	TArray<FTransform> TransitionGroundTransforms;
	TArray<FTransform> RoadTransforms;
	TArray<FTransform> LakeBedTransforms;
	// The water is one sheet over the bounding box of every lake and shore cell,
	// not a sheet per cell. Per-cell sheets met edge to edge, and the material's
	// ripple normals restarted at every cell border - a visible 20 m grid drawn on
	// the water. Where the box overlaps dry land the sheet runs inside the solid
	// ground slabs, so only the lake part of it ever renders.
	TArray<FTransform> LakeWaterTransforms;
	FIntPoint LakeSheetMin(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint LakeSheetMax(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	int32 LakeWaterCellCount = 0;
	GroundTransforms.Reserve(2025);
	WarZoneGroundTransforms.Reserve(256);
	TransitionGroundTransforms.Reserve(512);
	RoadTransforms.Reserve(256);
	TSet<FIntPoint> FacilityCells;
	// One shared underside for every ground slab and facility pad. Each slab used to
	// pick its own bottom just under its own top, so a pad sunk to -70 cm left an
	// open slot between its floor and the -10 cm underside of the neighbouring cell:
	// the pit wall had a gap the player could see and clip through. Taking the
	// deepest authored floor once makes the whole terrain one solid body.
	float TerrainSolidBottomZ = -10.0f;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		for (const FIntPoint& Cell : FacilityPlacement.OccupiedCells)
			FacilityCells.Add(Cell);
		const float FacilitySurfaceZ = FacilityPlacement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : FacilityPlacement.BaseElevationCm;
		TerrainSolidBottomZ = FMath::Min(TerrainSolidBottomZ, FacilitySurfaceZ - 60.0f);
	}
	// The lake floor is the deepest thing in the world, so the slabs that form its
	// banks have to reach past it. Taller boxes cost no extra instances.
	TerrainSolidBottomZ = FMath::Min(TerrainSolidBottomZ, LakeBedZ - 40.0f);

	// Cells that carry a generated shore mesh get no flat slab: the mesh is the
	// ground there, and a slab at Z=20 would cut straight through its beach.
	TSet<FIntPoint> LakeCells;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Water)
			LakeCells.Add(Placement.GridCell);
	TMap<FIntPoint, TPair<int32, int32>> ShoreTileByCell;
	if (LakeCells.Num() > 0)
		BuildShoreTransitionMap(LakeCells, ShoreTileByCell);

	int32 RoadTileCount = 0;
	FIntPoint VisualWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			VisualWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		// Every 20x20 m cell shares one ground renderer. Its upper face is Z=20,
		// matching ATacticalTileActor's authored prop/foliage datum. Exact cell
		// dimensions keep adjacent top faces edge-to-edge without coplanar overlap.
		const int32 WarZoneDeltaX = Placement.GridCell.X - VisualWarZoneCenter.X;
		const int32 WarZoneDeltaY = Placement.GridCell.Y - VisualWarZoneCenter.Y;
		const int32 WarZoneDistanceSquared = WarZoneDeltaX * WarZoneDeltaX + WarZoneDeltaY * WarZoneDeltaY;
		const bool bLogicalWarZoneCell = Placement.Visual == ETileDesignVisual::WarZoneGround;
		const bool bIndustrialCoreGround = bLogicalWarZoneCell && WarZoneDistanceSquared <= 64;
		// Matches the WarZone_Mid tile band in SpawnRuntimeBlueprintTiles, which runs to
		// d^2 <= 225 off the same centre cell. The ground stopped at 144, so cells
		// between radius 12 and 15 received industrial container/factory tiles while
		// standing on green nature ground - the WarZone visibly broke apart before
		// reaching its own edge. Beyond 225 the nature ground is intentional: that is
		// the WarZone_Outer natural buffer band.
		const bool bTransitionGround = bLogicalWarZoneCell
			&& WarZoneDistanceSquared > 64
			&& WarZoneDistanceSquared <= 225;
		UHierarchicalInstancedStaticMeshComponent* GroundComponent = bIndustrialCoreGround
			? WarZoneGroundHISM
			: (bTransitionGround ? TransitionGroundHISM : GroundHISM);
		TArray<FTransform>& TargetGroundTransforms = bIndustrialCoreGround
			? WarZoneGroundTransforms
			: (bTransitionGround ? TransitionGroundTransforms : GroundTransforms);
		// A land cell whose four corners are all wet is submerged: it belongs to the
		// lake floor, not to the walking datum. Without this it fell between both
		// paths and left a flat slab stranded at Z=20 amid the shoreline.
		const TPair<int32, int32>* ShoreEntry = ShoreTileByCell.Find(Placement.GridCell);
		const bool bSubmergedCell = ShoreEntry != nullptr
			&& ShoreEntry->Key == SubmergedShoreVariant;
		if (Placement.Visual == ETileDesignVisual::Water || bSubmergedCell)
		{
			// A sunken bed plus the shared water sheet, in place of the flat cell slab.
			// The bank between LakeSurfaceZ and the neighbouring ground top is what
			// makes the lake impassable, so nothing here reaches the shared datum.
			// Solid from the lake floor down to the shared underside. The height used
			// to be Max(20, LakeBedZ - TerrainSolidBottomZ), and with the bed below
			// the underside that expression is negative - it clamped to a 20 cm sheet
			// hanging in the water with a see-through band above it.
			LakeBedTransforms.Add(MakeCubeTransform(
				LakeBedHISM,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (LakeBedZ + TerrainSolidBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize,
					FMath::Max(40.0f, LakeBedZ - TerrainSolidBottomZ))));
			++LakeWaterCellCount;
			LakeSheetMin = FIntPoint(
				FMath::Min(LakeSheetMin.X, Placement.GridCell.X),
				FMath::Min(LakeSheetMin.Y, Placement.GridCell.Y));
			LakeSheetMax = FIntPoint(
				FMath::Max(LakeSheetMax.X, Placement.GridCell.X),
				FMath::Max(LakeSheetMax.Y, Placement.GridCell.Y));
			continue;
		}

		if (ShoreEntry != nullptr)
		{
			// The shore mesh is now only the beach surface; the solid below it is
			// this bed slab, same as a water cell's. Its top meets the beach exactly
			// at the wet corners and sits under it everywhere else.
			LakeBedTransforms.Add(MakeCubeTransform(
				LakeBedHISM,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (LakeBedZ + TerrainSolidBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize,
					FMath::Max(40.0f, LakeBedZ - TerrainSolidBottomZ))));
			// The beach dips under the waterline near its wet corners, so the shared
			// sheet must span shore cells too - without this every beach ended in a
			// dry olive basin, an empty pool beside the lake.
			LakeSheetMin = FIntPoint(
				FMath::Min(LakeSheetMin.X, Placement.GridCell.X),
				FMath::Min(LakeSheetMin.Y, Placement.GridCell.Y));
			LakeSheetMax = FIntPoint(
				FMath::Max(LakeSheetMax.X, Placement.GridCell.X),
				FMath::Max(LakeSheetMax.Y, Placement.GridCell.Y));
		}

		const float AuthoredElevation = GetSurfaceElevationForCell(Placement.GridCell);
		const float SurfaceZ = AuthoredElevation;
		const float GroundBottomZ = TerrainSolidBottomZ;
		if (!FacilityCells.Contains(Placement.GridCell)
			&& !ShoreTileByCell.Contains(Placement.GridCell))
		{
			TargetGroundTransforms.Add(MakeCubeTransform(
				GroundComponent,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (SurfaceZ + GroundBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize, SurfaceZ - GroundBottomZ)));
		}

		const bool bHasRoadSurface =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle;
		if (!bHasRoadSurface)
			continue;

		++RoadTileCount;
		// The slab used to be 2 cm thick sitting 1 cm above the shared terrain, which
		// put its underside exactly coplanar with the ground top and its walking face
		// only two centimetres clear. Up close the depth buffer separates that fine;
		// across a 900 m map it cannot, and the pair shimmered - the coplanar audit
		// found 174 overlapping cell pairs at a 0.00 cm gap. Lift the walking face to
		// a curb-like 10 cm, well under the 45 cm step height, and bury the underside
		// 20 cm inside the ground cube where it can never be coplanar with anything.
		const FVector Center = Placement.WorldLocation
			+ FVector(0.0f, 0.0f, SurfaceZ + RoadSurfaceLiftCm - RoadSurfaceThicknessCm * 0.5f);
		RoadTransforms.Add(MakeCubeTransform(
			RoadSurfaceHISM, Center, FVector(600.0f, 600.0f, RoadSurfaceThicknessCm)));
		auto AddRoadArm = [this, &Placement, &RoadTransforms, &MakeOrientedCubeTransform](
			const FIntPoint& Direction, uint8 ConnectionBit)
		{
			if ((Placement.ConnectionMask & ConnectionBit) == 0)
				return;
			const float ThisSurfaceZ = GetSurfaceElevationForCell(Placement.GridCell);
			const float NeighbourSurfaceZ = GetSurfaceElevationForCell(Placement.GridCell + Direction);
			const FVector Direction3D(static_cast<float>(Direction.X), static_cast<float>(Direction.Y), 0.0f);
			// Arms share the centre patch's datum so the whole road surface stays one
			// continuous plane at the lifted height.
			const float ArmCenterOffsetZ = RoadSurfaceLiftCm - RoadSurfaceThicknessCm * 0.5f;
			const FVector Start = Placement.WorldLocation + Direction3D * 300.0f
				+ FVector(0.0f, 0.0f, ThisSurfaceZ + ArmCenterOffsetZ);
			const FVector End = Placement.WorldLocation + Direction3D * 1000.0f
				+ FVector(0.0f, 0.0f, (ThisSurfaceZ + NeighbourSurfaceZ) * 0.5f + ArmCenterOffsetZ);
			const FVector Delta = End - Start;
			RoadTransforms.Add(MakeOrientedCubeTransform(
				RoadSurfaceHISM,
				(Start + End) * 0.5f,
				Delta.Rotation(),
				FVector(Delta.Size() + 2.0f, 600.0f, RoadSurfaceThicknessCm)));
		};
		AddRoadArm(FIntPoint(0, 1), NorthConnection);
		AddRoadArm(FIntPoint(1, 0), EastConnection);
		AddRoadArm(FIntPoint(0, -1), SouthConnection);
		AddRoadArm(FIntPoint(-1, 0), WestConnection);
	}

	// Each multi-cell facility owns one continuous terrain tile matching its exact
	// 2x1, 2x2, 3x3 (or rotated) footprint. This removes internal seams and gives
	// the building, props, collision and access pieces one authoritative top Z.
	int32 RaisedPadCount = 0;
	int32 LoweredPadCount = 0;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.OccupiedCells.IsEmpty())
			continue;
		// A facility with its own sculpted ground gets no flat pad: the pad would
		// slice straight through the terrain mesh at the datum height, cutting off
		// everything below it - which for the rural diorama is the shoreline, the
		// water and both boats.
		if (FacilityBringsOwnTerrain(FacilityPlacement.VisualSet))
			continue;
		int32 MinX = MAX_int32;
		int32 MinY = MAX_int32;
		int32 MaxX = MIN_int32;
		int32 MaxY = MIN_int32;
		for (const FIntPoint& Cell : FacilityPlacement.OccupiedCells)
		{
			MinX = FMath::Min(MinX, Cell.X);
			MinY = FMath::Min(MinY, Cell.Y);
			MaxX = FMath::Max(MaxX, Cell.X);
			MaxY = FMath::Max(MaxY, Cell.Y);
		}
		const float SurfaceZ = FacilityPlacement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : FacilityPlacement.BaseElevationCm;
		const float GroundBottomZ = TerrainSolidBottomZ;
		const FVector PadCenter(
			(MinX + MaxX) * DesignCellSize * 0.5f,
			(MinY + MaxY) * DesignCellSize * 0.5f,
			(SurfaceZ + GroundBottomZ) * 0.5f);
		const FVector PadSize(
			(MaxX - MinX + 1) * DesignCellSize,
			(MaxY - MinY + 1) * DesignCellSize,
			SurfaceZ - GroundBottomZ);
		UHierarchicalInstancedStaticMeshComponent* FacilityGroundComponent =
			FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			|| FacilityPlacement.VisualSet == EFacilityVisualSet::DowntownBlock
			|| FacilityPlacement.VisualSet == EFacilityVisualSet::FactoryConstruction
				? WarZoneGroundHISM : GroundHISM;
		TArray<FTransform>& FacilityGroundTransforms = FacilityGroundComponent == WarZoneGroundHISM
			? WarZoneGroundTransforms : GroundTransforms;
		FacilityGroundTransforms.Add(MakeCubeTransform(
			FacilityGroundComponent, PadCenter, PadSize));
		if (SurfaceZ > 21.0f)
			++RaisedPadCount;
		else if (SurfaceZ < 19.0f)
			++LoweredPadCount;
	}
	UE_LOG(LogTemp, Display,
		TEXT("Facility terrain pads: total=%d raised=%d lowered=%d continuous_rectangles=true"),
		FacilityPlacements.Num(), RaisedPadCount, LoweredPadCount);

	// Shoreline dressing. Water membership is a per-cell decision, so the waterline
	// steps along the 20 m grid. River stones and reeds standing in the shallows
	// break that line up.
	//
	// A tilted bank slab was tried here first and removed: a rotated box 27 m long
	// lifts its far end more than 3 m clear of a ground plane at Z=20, so instead of
	// a shore it produced planes jutting out of the terrain. A real curved shoreline
	// needs sub-cell geometry - authored shore meshes in the style of
	// SM_Terrain_Mound_2x2 - not a rotated cube.
	ShoreRockHISM->ClearInstances();
	ShoreReedHISM->ClearInstances();
	TArray<FTransform> ShoreRockTransforms;
	TArray<FTransform> ShoreReedTransforms;
	{
		const AGameModePG* ShoreGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
		const int64 ShoreRaidSeed = IsValid(ShoreGameMode) ? ShoreGameMode->GetMapGenerationSeed() : 0;
		TSet<FIntPoint> WaterCells;
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
			if (Placement.Visual == ETileDesignVisual::Water)
				WaterCells.Add(Placement.GridCell);

		const FIntPoint ShoreOffsets[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		{
			if (Placement.Visual == ETileDesignVisual::Water)
				continue;

			FVector2D WaterDirection = FVector2D::ZeroVector;
			for (const FIntPoint& Offset : ShoreOffsets)
				if (WaterCells.Contains(Placement.GridCell + Offset))
					WaterDirection += FVector2D(Offset.X, Offset.Y);
			if (WaterDirection.IsNearlyZero())
				continue;
			WaterDirection = WaterDirection.GetSafeNormal();

			const uint32 ShoreHash = HashCombine(
				GetTypeHash(ShoreRaidSeed),
				HashCombine(GetTypeHash(Placement.GridCell.X * 31),
					GetTypeHash(Placement.GridCell.Y * 17)));
			FRandomStream ShoreStream(static_cast<int32>(ShoreHash));

			for (int32 RockIndex = 0; RockIndex < 5; ++RockIndex)
			{
				const FVector2D Along(-WaterDirection.Y, WaterDirection.X);
				const FVector Location = Placement.WorldLocation
					+ FVector(WaterDirection.X, WaterDirection.Y, 0.0f)
						* ShoreStream.FRandRange(700.0f, 1450.0f)
					+ FVector(Along.X, Along.Y, 0.0f) * ShoreStream.FRandRange(-950.0f, 950.0f)
					+ FVector(0.0f, 0.0f, LakeSurfaceZ - ShoreStream.FRandRange(10.0f, 70.0f));
				ShoreRockTransforms.Add(FTransform(
					FRotator(0.0f, ShoreStream.FRandRange(0.0f, 360.0f), 0.0f),
					Location,
					FVector(ShoreStream.FRandRange(0.8f, 2.1f)))
					.GetRelativeTransform(ShoreRockHISM->GetComponentTransform()));
			}
			for (int32 ReedIndex = 0; ReedIndex < 9; ++ReedIndex)
			{
				const FVector2D Along(-WaterDirection.Y, WaterDirection.X);
				const FVector Location = Placement.WorldLocation
					+ FVector(WaterDirection.X, WaterDirection.Y, 0.0f)
						* ShoreStream.FRandRange(600.0f, 1300.0f)
					+ FVector(Along.X, Along.Y, 0.0f) * ShoreStream.FRandRange(-980.0f, 980.0f)
					+ FVector(0.0f, 0.0f, LakeSurfaceZ - 20.0f);
				ShoreReedTransforms.Add(FTransform(
					FRotator(0.0f, ShoreStream.FRandRange(0.0f, 360.0f), 0.0f),
					Location,
					FVector(ShoreStream.FRandRange(0.9f, 1.6f)))
					.GetRelativeTransform(ShoreReedHISM->GetComponentTransform()));
			}
		}
	}

	// Place the shore meshes themselves. Their local origin already sits on the cell
	// centre with the land face at Z=20, so the only transform needed is the cell
	// position and the quarter turn that points the authored water side at the lake.
	int32 ShoreTransitionCount = 0;
	for (int32 VariantIndex = 0; VariantIndex < ShoreTransitionHISMs.Num(); ++VariantIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = ShoreTransitionHISMs[VariantIndex];
		if (!IsValid(Component))
			continue;
		if (Component->GetStaticMesh() == nullptr)
		{
			static const TCHAR* ShoreMeshAssetPaths[] = {
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner1.SM_Shore_Corner1"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Adjacent.SM_Shore_Corner2Adjacent"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Diagonal.SM_Shore_Corner2Diagonal"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner3.SM_Shore_Corner3")
			};
			if (ShoreTransitionHISMs.IsValidIndex(VariantIndex)
				&& VariantIndex < UE_ARRAY_COUNT(ShoreMeshAssetPaths))
			{
				if (UStaticMesh* ShoreMesh = LoadObject<UStaticMesh>(
					nullptr, ShoreMeshAssetPaths[VariantIndex]))
				{
					Component->SetStaticMesh(ShoreMesh);
					static UMaterialInterface* SharedGroundMaterial = LoadObject<UMaterialInterface>(
						nullptr,
						TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
					if (IsValid(SharedGroundMaterial))
						Component->SetMaterial(0, SharedGroundMaterial);
				}
			}
		}
		if (Component->GetStaticMesh() == nullptr)
		{
			// Nothing to place. Say so once rather than leaving a hole where the
			// suppressed flat slabs used to be - run PG.BuildShoreMeshes.
			UE_LOG(LogTemp, Warning,
				TEXT("Shore mesh missing for variant %d - run PG.BuildShoreMeshes"), VariantIndex);
			continue;
		}
		Component->ClearInstances();
		TArray<FTransform> ShoreTransforms;
		for (const TPair<FIntPoint, TPair<int32, int32>>& Entry : ShoreTileByCell)
		{
			if (Entry.Value.Key != VariantIndex)
				continue;
			const FVector ShoreLocation(
				Entry.Key.X * DesignCellSize, Entry.Key.Y * DesignCellSize, 0.0f);
			ShoreTransforms.Add(FTransform(
				FRotator(0.0f, Entry.Value.Value * 90.0f, 0.0f),
				ShoreLocation,
				FVector::OneVector).GetRelativeTransform(Component->GetComponentTransform()));
		}
		Component->AddInstances(ShoreTransforms, false, false, true);
		Component->BuildTreeIfOutdated(false, true);
		ShoreTransitionCount += ShoreTransforms.Num();
	}

	GroundHISM->AddInstances(GroundTransforms, false, false, true);
	ShoreRockHISM->AddInstances(ShoreRockTransforms, false, false, true);
	ShoreReedHISM->AddInstances(ShoreReedTransforms, false, false, true);
	WarZoneGroundHISM->AddInstances(WarZoneGroundTransforms, false, false, true);
	TransitionGroundHISM->AddInstances(TransitionGroundTransforms, false, false, true);
	RoadSurfaceHISM->AddInstances(RoadTransforms, false, false, false);
	if (LakeSheetMax.X >= LakeSheetMin.X && LakeSheetMax.Y >= LakeSheetMin.Y)
	{
		LakeWaterTransforms.Add(MakeCubeTransform(
			LakeWaterHISM,
			FVector(
				(LakeSheetMin.X + LakeSheetMax.X) * 0.5f * DesignCellSize,
				(LakeSheetMin.Y + LakeSheetMax.Y) * 0.5f * DesignCellSize,
				LakeSurfaceZ - 5.0f),
			FVector(
				(LakeSheetMax.X - LakeSheetMin.X + 1) * DesignCellSize,
				(LakeSheetMax.Y - LakeSheetMin.Y + 1) * DesignCellSize,
				10.0f)));
	}
	LakeBedHISM->AddInstances(LakeBedTransforms, false, false, true);
	LakeWaterHISM->AddInstances(LakeWaterTransforms, false, false, false);

	GroundHISM->BuildTreeIfOutdated(false, true);
	WarZoneGroundHISM->BuildTreeIfOutdated(false, true);
	TransitionGroundHISM->BuildTreeIfOutdated(false, true);
	RoadSurfaceHISM->BuildTreeIfOutdated(false, true);
	LakeBedHISM->BuildTreeIfOutdated(false, true);
	LakeWaterHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border lake: cells=%d shore_cells=%d shore_meshes=%d shore_rocks=%d shore_reeds=%d ")
		TEXT("surface_z=%.0f bed_z=%.0f bank_drop_cm=%.0f"),
		LakeWaterCellCount, ShoreRockTransforms.Num() / 5, ShoreTransitionCount,
		ShoreRockTransforms.Num(), ShoreReedTransforms.Num(),
		LakeSurfaceZ, LakeBedZ, BaseGroundSurfaceZ - LakeSurfaceZ);
	GroundHISM->RecreatePhysicsState();
	WarZoneGroundHISM->RecreatePhysicsState();
	TransitionGroundHISM->RecreatePhysicsState();
	RoadSurfaceHISM->RecreatePhysicsState();

	UE_LOG(LogTemp, Display,
		TEXT("HISM design world: ground_instances=%d warzone_ground_instances=%d transition_ground_instances=%d road_tiles=%d road_surface_instances=%d collision=true facility_support_cells=%d"),
		GroundHISM->GetInstanceCount(), WarZoneGroundHISM->GetInstanceCount(), TransitionGroundHISM->GetInstanceCount(), RoadTileCount, RoadSurfaceHISM->GetInstanceCount(),
		2025 - TileDesignPlacements.Num());
}

void AWarZoneFootprintPreview::VerifyWorldCollision()
{
	if (bLoggedWorldCollision
		|| !AreAllFacilityLevelsLoaded())
	{
		return;
	}

	bLoggedWorldCollision = true;
	int32 HitCount = 0;
	TArray<FIntPoint> MissingCells;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DesignWorldGroundValidation), true);
	for (int32 Y = -22; Y <= 22; ++Y)
	{
		for (int32 X = -22; X <= 22; ++X)
		{
			const FVector CellCenter(X * DesignCellSize, Y * DesignCellSize, 0.0f);
			FHitResult HitResult;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(
				HitResult,
				CellCenter + FVector(0.0f, 0.0f, 1000.0f),
				CellCenter - FVector(0.0f, 0.0f, 1000.0f),
				ECC_Visibility,
				QueryParams);
			if (bHit)
				++HitCount;
			else
				MissingCells.Add(FIntPoint(X, Y));
		}
	}

	FString MissingSummary;
	for (int32 Index = 0; Index < FMath::Min(MissingCells.Num(), 12); ++Index)
	{
		MissingSummary += FString::Printf(
			TEXT("(%d,%d)%s"),
			MissingCells[Index].X,
			MissingCells[Index].Y,
			Index + 1 < FMath::Min(MissingCells.Num(), 12) ? TEXT(",") : TEXT(""));
	}
	UE_LOG(LogTemp, Display,
		TEXT("Design world collision: cells=2025 hits=%d missing=%d sample=[%s]"),
		HitCount, MissingCells.Num(), *MissingSummary);
}

void AWarZoneFootprintPreview::VerifyTacticalLayoutQuality()
{
	int32 ExpectedRuntimeFacilityCount = 0;
	for (const FFacilityPlacement& Placement : FacilityPlacements)
		if (Placement.VisualSet != EFacilityVisualSet::Checkpoint)
			++ExpectedRuntimeFacilityCount;
	if (bLoggedTacticalLayoutQuality
		|| TileDesignPlacements.IsEmpty()
		|| SpawnedRuntimeTiles.Num() != TileDesignPlacements.Num() + ExpectedRuntimeFacilityCount
		|| !AreAllFacilityLevelsLoaded())
		return;

	bLoggedTacticalLayoutQuality = true;
	int32 GridMisalignments = 0;
	int32 OutOfBoundsComponents = 0;
	TArray<FString> OverflowSamples;
	int32 InvalidMasks = 0;
	int32 UnsafeAnchorOverlaps = 0;
	TArray<FString> UnsafeAnchorSamples;
	TSet<FIntPoint> OccupiedCells;
	for (AActor* Actor : SpawnedRuntimeTiles)
	{
		if (!IsValid(Actor) || Actor->ActorHasTag(TEXT("RuntimeTacticalFacility")))
			continue;

		const FVector Location = Actor->GetActorLocation();
		const FIntPoint Cell(
			FMath::RoundToInt(Location.X / DesignCellSize),
			FMath::RoundToInt(Location.Y / DesignCellSize));
		if (!FMath::IsNearlyEqual(Location.X, Cell.X * DesignCellSize, 1.0f)
			|| !FMath::IsNearlyEqual(Location.Y, Cell.Y * DesignCellSize, 1.0f)
			|| OccupiedCells.Contains(Cell))
		{
			++GridMisalignments;
		}
		OccupiedCells.Add(Cell);

		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(false, Origin, Extent, true);
		// Ignore editor-only connection arrows and tall debug vectors. Validate only
		// colliding primitive bounds because those are what can overlap neighbors.
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		bool bActorOverflow = false;
		for (const UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive)
				|| Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				|| !Primitive->IsVisible())
			{
				continue;
			}
			const FString ComponentName = Primitive->GetName();
			if (ComponentName.Contains(TEXT("Ground"))
				|| ComponentName.Contains(TEXT("Road"))
				|| ComponentName.Contains(TEXT("Roof"))
				|| ComponentName.Contains(TEXT("Grass"))
				|| ComponentName.Contains(TEXT("Tree"))
				|| ComponentName.Contains(TEXT("Bush"))
				|| ComponentName.Contains(TEXT("Rock"))
				|| ComponentName.Contains(TEXT("Marking")))
			{
				continue;
			}
			const FBoxSphereBounds Bounds = Primitive->Bounds;
			// Seam dressing and angled CQB cover may overhang the nominal 10 m
			// half-cell slightly. Industrial props are intentionally used as visual
			// bridges between adjacent WarZone cells, so they receive a larger but
			// still bounded allowance. Structural walls retain the stricter limit.
			const bool bIndustrialSeamProp = ComponentName.Contains(TEXT("Container"))
				|| ComponentName.Contains(TEXT("Crane"))
				|| ComponentName.Contains(TEXT("Tank"))
				|| ComponentName.Contains(TEXT("Pallet"))
				|| ComponentName.Contains(TEXT("Fence"))
				|| ComponentName.Contains(TEXT("PipeRack"));
			const float MaxComponentReach = bIndustrialSeamProp ? 1800.0f : 1400.0f;
			if (FMath::Abs(Bounds.Origin.X - Location.X) + Bounds.BoxExtent.X > MaxComponentReach
				|| FMath::Abs(Bounds.Origin.Y - Location.Y) + Bounds.BoxExtent.Y > MaxComponentReach)
			{
				bActorOverflow = true;
				break;
			}
		}
		if (bActorOverflow)
		{
			++OutOfBoundsComponents;
			if (OverflowSamples.Num() < 12)
				OverflowSamples.Add(Actor->GetActorLabel());
		}

		if (const ATacticalTileActor* TacticalTile = Cast<ATacticalTileActor>(Actor))
		{
			if (TacticalTile->GetEffectiveConnectionMask() > 15
				|| TacticalTile->GetEffectiveLayoutVariant() > 3)
			{
				++InvalidMasks;
			}
		}
	}

	FCollisionQueryParams AnchorQuery(SCENE_QUERY_STAT(TacticalAnchorSafety), false);
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Spawn
			&& Point.Type != ELevelDesignPointType::Exit
			&& Point.Type != ELevelDesignPointType::Loot
			&& Point.Type != ELevelDesignPointType::AISpawn
			&& Point.Type != ELevelDesignPointType::Quest)
		{
			continue;
		}
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(55.0f, 95.0f);
		TArray<FOverlapResult> AnchorOverlaps;
		const bool bHasOverlap = GetWorld()->OverlapMultiByChannel(
			AnchorOverlaps,
			Point.WorldLocation + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			Capsule,
			AnchorQuery);
		const bool bBlockedByTacticalGeometry = bHasOverlap && AnchorOverlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != this
					&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
					&& !(bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
					&& IsValid(HitComponent)
					&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			});
		if (bBlockedByTacticalGeometry)
		{
			++UnsafeAnchorOverlaps;
			if (UnsafeAnchorSamples.Num() < 8)
			{
				const FOverlapResult* Blocking = AnchorOverlaps.FindByPredicate(
					[this](const FOverlapResult& Result)
					{
						const AActor* HitActor = Result.GetActor();
						const UPrimitiveComponent* HitComponent = Result.GetComponent();
						return IsValid(HitActor) && HitActor != this
							&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
							&& !(bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
							&& IsValid(HitComponent)
							&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
					});
				UnsafeAnchorSamples.Add(FString::Printf(TEXT("%s:%s/%s"),
					*Point.PointId.ToString(),
					Blocking && IsValid(Blocking->GetActor()) ? *Blocking->GetActor()->GetActorLabel() : TEXT("UnknownActor"),
					Blocking && IsValid(Blocking->GetComponent()) ? *Blocking->GetComponent()->GetName() : TEXT("UnknownComponent")));
			}
		}
	}
	const FString UnsafeSummary = FString::Join(UnsafeAnchorSamples, TEXT(","));

	UE_LOG(LogTemp, Display,
		TEXT("Tactical layout quality: single_cell_tiles=%d facilities=%d grid_or_duplicate=%d bounds_overflow=%d invalid_specs=%d unsafe_anchors=%d pass=%s overflow_sample=[%s] unsafe_sample=[%s]"),
		TileDesignPlacements.Num(),
		ExpectedRuntimeFacilityCount,
		GridMisalignments,
		OutOfBoundsComponents,
		InvalidMasks,
		UnsafeAnchorOverlaps,
		GridMisalignments == 0 && OutOfBoundsComponents == 0 && InvalidMasks == 0 && UnsafeAnchorOverlaps == 0
			? TEXT("true") : TEXT("false"),
		*FString::Join(OverflowSamples, TEXT(",")),
		*UnsafeSummary);
}

void AWarZoneFootprintPreview::VerifyTravelCoverDensity()
{
	int32 ExpectedRuntimeFacilityCount = 0;
	for (const FFacilityPlacement& Placement : FacilityPlacements)
		if (Placement.VisualSet != EFacilityVisualSet::Checkpoint)
			++ExpectedRuntimeFacilityCount;
	if (bLoggedTravelCoverDensity
		|| TileDesignPlacements.IsEmpty()
		|| SpawnedRuntimeTiles.Num() != TileDesignPlacements.Num() + ExpectedRuntimeFacilityCount)
		return;

	bLoggedTravelCoverDensity = true;
	int32 SampleCount = 0;
	int32 FullyExposedSamples = 0;
	float LongestExposedRunCm = 0.0f;
	for (int32 Y = -22; Y <= 22; Y += 2)
	{
		float CurrentRunCm = 0.0f;
		for (int32 X = -22; X <= 22; ++X)
		{
			++SampleCount;
			// Cover is useful when it protects a crouched player; the previous 1.4m
			// standing-eye trace incorrectly rejected deliberate chest-high cover.
			const FVector EyeLocation(X * DesignCellSize, Y * DesignCellSize, 90.0f);
			bool bHasNearbyCover = false;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(TravelCoverDensity), true);
			for (int32 DirectionIndex = 0; DirectionIndex < 8; ++DirectionIndex)
			{
				const float Angle = FMath::DegreesToRadians(DirectionIndex * 45.0f);
				const FVector Direction(FMath::Cos(Angle) * 900.0f, FMath::Sin(Angle) * 900.0f, 0.0f);
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByChannel(Hit, EyeLocation, EyeLocation + Direction, ECC_Visibility, Query))
				{
					const UPrimitiveComponent* Component = Hit.GetComponent();
					const FVector Normal = Hit.ImpactNormal;
					// Ground is a horizontal hit; meaningful cover has a lateral face.
					if (IsValid(Component) && FMath::Abs(Normal.Z) < 0.55f)
					{
						bHasNearbyCover = true;
						break;
					}
				}
			}

			if (bHasNearbyCover)
			{
				CurrentRunCm = 0.0f;
			}
			else
			{
				++FullyExposedSamples;
				CurrentRunCm += DesignCellSize;
				LongestExposedRunCm = FMath::Max(LongestExposedRunCm, CurrentRunCm);
			}
		}
	}

	const float ExposedRatio = SampleCount > 0
		? static_cast<float>(FullyExposedSamples) / SampleCount
		: 1.0f;
	UE_LOG(LogTemp, Display,
		TEXT("Travel cover audit (crouch_height=90cm): samples=%d fully_exposed=%d exposed_ratio=%.3f longest_exposed_run_m=%.1f target_ratio<=0.55 target_run<=120m pass=%s"),
		SampleCount,
		FullyExposedSamples,
		ExposedRatio,
		LongestExposedRunCm / 100.0f,
		ExposedRatio <= 0.55f && LongestExposedRunCm <= 12000.0f ? TEXT("true") : TEXT("false"));
}

void AWarZoneFootprintPreview::VerifyGameplayPointDistribution()
{
	if (bLoggedGameplayPointDistribution || LevelDesignPoints.IsEmpty() || !bResolvedGameplayPointSafety)
		return;

	bLoggedGameplayPointDistribution = true;
	TMap<ELevelDesignPointType, int32> Counts;
	TSet<FName> UniqueIds;
	int32 InvalidMetadata = 0;
	TArray<const FLevelDesignPoint*> SpawnPoints;
	TArray<const FLevelDesignPoint*> LootPoints;
	TArray<const FLevelDesignPoint*> AIPoints;
	auto IsProtectedFromAI = [this](const FIntPoint& Cell)
	{
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		{
			const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
				+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
			if ((Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
				|| (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2))
				return true;
		}
		// Mirror of the point-building lambda: the boat-landing hamlet is a spawn
		// area even though no Spawn tile marks it. Both copies must agree or the
		// verifier fails layouts the builder considers legal.
		if (bHasBorderLake)
		{
			for (const FFacilityPlacement& Facility : FacilityPlacements)
			{
				if (Facility.VisualSet != EFacilityVisualSet::RuralHideout)
					continue;
				for (const FIntPoint& Occupied : Facility.OccupiedCells)
					if (FMath::Abs(Cell.X - Occupied.X) + FMath::Abs(Cell.Y - Occupied.Y) < 3)
						return true;
			}
		}
		return false;
	};
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		Counts.FindOrAdd(Point.Type)++;
		if (Point.PointId.IsNone() || UniqueIds.Contains(Point.PointId)
			|| Point.ArchetypeId.IsNone() || Point.Tier < 1 || Point.Tier > 3
			|| Point.RadiusCm < 50.0f || Point.Capacity < 1)
		{
			++InvalidMetadata;
		}
		UniqueIds.Add(Point.PointId);
		if (Point.Type == ELevelDesignPointType::Spawn) SpawnPoints.Add(&Point);
		else if (Point.Type == ELevelDesignPointType::Loot) LootPoints.Add(&Point);
		else if (Point.Type == ELevelDesignPointType::AISpawn) AIPoints.Add(&Point);
	}

	float MinimumSpawnAIDistanceCm = BIG_NUMBER;
	for (const FLevelDesignPoint* Spawn : SpawnPoints)
		for (const FLevelDesignPoint* AI : AIPoints)
			MinimumSpawnAIDistanceCm = FMath::Min(
				MinimumSpawnAIDistanceCm,
				FVector::Dist2D(Spawn->WorldLocation, AI->WorldLocation));
	float MinimumSpawnSeparationCm = BIG_NUMBER;
	for (int32 A = 0; A < SpawnPoints.Num(); ++A)
		for (int32 B = A + 1; B < SpawnPoints.Num(); ++B)
			MinimumSpawnSeparationCm = FMath::Min(
				MinimumSpawnSeparationCm,
				FVector::Dist2D(SpawnPoints[A]->WorldLocation, SpawnPoints[B]->WorldLocation));

	// Sample the playable cell field every 100m. A loot-shooter can contain open
	// traversal space, but no sampled region should be excessively far from both
	// a loot opportunity and a possible encounter.
	float MaximumLootGapCm = 0.0f;
	float MaximumAIGapCm = 0.0f;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		if (FMath::Abs(Placement.GridCell.X) % 5 != 0
			|| FMath::Abs(Placement.GridCell.Y) % 5 != 0)
			continue;
		float NearestLoot = BIG_NUMBER;
		for (const FLevelDesignPoint* Loot : LootPoints)
			NearestLoot = FMath::Min(NearestLoot, FVector::Dist2D(Placement.WorldLocation, Loot->WorldLocation));
		float NearestAI = BIG_NUMBER;
		for (const FLevelDesignPoint* AI : AIPoints)
			NearestAI = FMath::Min(NearestAI, FVector::Dist2D(Placement.WorldLocation, AI->WorldLocation));
		MaximumLootGapCm = FMath::Max(MaximumLootGapCm, NearestLoot);
		MaximumAIGapCm = FMath::Max(MaximumAIGapCm, NearestAI);
	}

	int32 FacilitiesMissingLoot = 0;
	int32 FacilitiesMissingAI = 0;
	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		const bool bHasLoot = LootPoints.ContainsByPredicate(
			[&Facility](const FLevelDesignPoint* Point)
			{
				return Facility.OccupiedCells.Contains(Point->GridCell);
			});
		const bool bHasAI = AIPoints.ContainsByPredicate(
			[&Facility](const FLevelDesignPoint* Point)
			{
				return Facility.OccupiedCells.Contains(Point->GridCell);
			});
		if (!bHasLoot) ++FacilitiesMissingLoot;
		// A facility inside the spawn safe band intentionally has no resident AI.
		if (!bHasAI && !Facility.OccupiedCells.ContainsByPredicate(IsProtectedFromAI))
			++FacilitiesMissingAI;
	}

	const bool bPass = Counts.FindRef(ELevelDesignPointType::Spawn) >= 4
		&& Counts.FindRef(ELevelDesignPointType::Loot) >= 40
		&& Counts.FindRef(ELevelDesignPointType::AISpawn) >= 24
		&& Counts.FindRef(ELevelDesignPointType::Exit) >= 2
		&& Counts.FindRef(ELevelDesignPointType::Quest) >= 1
		&& InvalidMetadata == 0
		&& MinimumSpawnSeparationCm >= 250.0f
		&& MinimumSpawnAIDistanceCm >= 5000.0f
		&& MaximumLootGapCm <= 25000.0f
		&& MaximumAIGapCm <= 30000.0f
		&& FacilitiesMissingLoot == 0
		&& FacilitiesMissingAI == 0;
	UE_LOG(LogTemp, Display,
		TEXT("Gameplay point distribution: spawn=%d loot=%d ai=%d exit=%d quest=%d invalid=%d min_spawn_spacing_m=%.1f min_spawn_ai_m=%.1f max_loot_gap_m=%.1f max_ai_gap_m=%.1f facilities_missing_loot=%d facilities_missing_ai=%d point_hash=%08X pass=%s"),
		Counts.FindRef(ELevelDesignPointType::Spawn),
		Counts.FindRef(ELevelDesignPointType::Loot),
		Counts.FindRef(ELevelDesignPointType::AISpawn),
		Counts.FindRef(ELevelDesignPointType::Exit),
		Counts.FindRef(ELevelDesignPointType::Quest),
		InvalidMetadata,
		MinimumSpawnSeparationCm / 100.0f,
		MinimumSpawnAIDistanceCm / 100.0f,
		MaximumLootGapCm / 100.0f,
		MaximumAIGapCm / 100.0f,
		FacilitiesMissingLoot,
		FacilitiesMissingAI,
		GameplayPointHash,
		bPass ? TEXT("true") : TEXT("false"));
}

void AWarZoneFootprintPreview::VerifyNavigation()
{
	if (bLoggedNavigation || !bLoggedWorldCollision || LevelDesignPoints.IsEmpty())
		return;

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavigationSystem))
		return;

	const double ElapsedSeconds = FPlatformTime::Seconds() - NavigationValidationStartTimeSeconds;
	if (NavigationSystem->IsNavigationBuildInProgress() && ElapsedSeconds < 20.0)
		return;

	int32 CandidateCount = 0;
	int32 ProjectedCount = 0;
	TArray<FName> FailedPointIds;
	const FVector QueryExtent(180.0f, 180.0f, 650.0f);
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		// Navigation is generated only around the central invoker. Points outside
		// its 120 m generation radius are validated later when a player/AI invoker
		// approaches them.
		if (FVector::DistSquared2D(Point.WorldLocation, GetActorLocation()) > FMath::Square(10000.0f))
			continue;

		++CandidateCount;
		FNavLocation ProjectedLocation;
		if (NavigationSystem->ProjectPointToNavigation(Point.WorldLocation, ProjectedLocation, QueryExtent))
		{
			++ProjectedCount;
			// Projection alone can succeed on an isolated polygon. Accept a movement
			// pocket in any cardinal direction; the old +X-only check falsely rejected
			// valid narrow rooms and rotated upper decks.
			const FVector NeighborOffsets[] = {
				FVector(350.0f, 0.0f, 0.0f), FVector(-350.0f, 0.0f, 0.0f),
				FVector(0.0f, 350.0f, 0.0f), FVector(0.0f, -350.0f, 0.0f)
			};
			const bool bHasMovementPocket = Algo::AnyOf(
				NeighborOffsets,
				[NavigationSystem, &ProjectedLocation, &QueryExtent](const FVector& Offset)
				{
					FNavLocation NeighborLocation;
					return NavigationSystem->ProjectPointToNavigation(
						ProjectedLocation.Location + Offset,
						NeighborLocation,
						QueryExtent);
				});
			if (!bHasMovementPocket)
			{
				--ProjectedCount;
				FailedPointIds.Add(Point.PointId);
			}
		}
		else
			FailedPointIds.Add(Point.PointId);
	}

	// Wait a little longer when the dynamic Recast generator has not exposed
	// any polygon yet, then report a deterministic pass/fail result.
	if (ProjectedCount == 0 && ElapsedSeconds < 20.0)
		return;

	bLoggedNavigation = true;
	FString FailedSummary;
	for (int32 Index = 0; Index < FMath::Min(FailedPointIds.Num(), 8); ++Index)
	{
		FailedSummary += FString::Printf(
			TEXT("%s%s"),
			*FailedPointIds[Index].ToString(),
			Index + 1 < FMath::Min(FailedPointIds.Num(), 8) ? TEXT(",") : TEXT(""));
	}

	UE_LOG(LogTemp, Display,
		TEXT("Design navigation: invoker_radius_cm=12000 candidates=%d projected=%d failed=%d build_pending=%s elapsed_ms=%.2f sample=[%s]"),
		CandidateCount,
		ProjectedCount,
		FailedPointIds.Num(),
		NavigationSystem->IsNavigationBuildInProgress() ? TEXT("true") : TEXT("false"),
		ElapsedSeconds * 1000.0,
		*FailedSummary);
}

void AWarZoneFootprintPreview::VerifyCriticalRoutes()
{
	if (bLoggedCriticalRoutes || !bLoggedNavigation || TileDesignPlacements.IsEmpty())
		return;

	const FLevelDesignPoint* Spawn = LevelDesignPoints.FindByPredicate(
		[](const FLevelDesignPoint& Point) { return Point.Type == ELevelDesignPointType::Spawn; });
	if (Spawn == nullptr)
		return;

	// Whole-raid reachability is a logical graph question. Runtime Recast is
	// generated only around invokers, so attempting one 900m nav query reports
	// false failures. BFS proves the generated walkable cell field is connected;
	// local Recast and AI movement are verified separately below.
	TSet<FIntPoint> WalkableCells;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		WalkableCells.Add(Placement.GridCell);
	}
	for (const FFacilityPlacement& Facility : FacilityPlacements)
		for (const FIntPoint& Cell : Facility.OccupiedCells)
			WalkableCells.Add(Cell);

	TSet<FIntPoint> Visited;
	TQueue<FIntPoint> Queue;
	Queue.Enqueue(Spawn->GridCell);
	Visited.Add(Spawn->GridCell);
	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
	FIntPoint Cell;
	while (Queue.Dequeue(Cell))
	{
		for (const FIntPoint& Direction : Directions)
		{
			const FIntPoint Neighbor = Cell + Direction;
			if (WalkableCells.Contains(Neighbor) && !Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	int32 TargetCount = 0;
	int32 ReachableTargets = 0;
	TArray<FString> FailedRoutes;
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Exit)
			continue;
		++TargetCount;
		if (Visited.Contains(Point.GridCell)) ++ReachableTargets;
		else FailedRoutes.Add(Point.PointId.ToString());
	}
	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		++TargetCount;
		const bool bReached = Facility.OccupiedCells.ContainsByPredicate(
			[&Visited](const FIntPoint& FacilityCell) { return Visited.Contains(FacilityCell); });
		if (bReached) ++ReachableTargets;
		else FailedRoutes.Add(FString::Printf(TEXT("Facility_%d_%d"), Facility.AnchorCell.X, Facility.AnchorCell.Y));
	}

	bLoggedCriticalRoutes = true;
	UE_LOG(LogTemp, Display,
		TEXT("Critical logical route audit: from=%s visited_cells=%d/%d targets=%d reachable=%d failed=%d pass=%s sample=[%s]"),
		*Spawn->PointId.ToString(), Visited.Num(), WalkableCells.Num(), TargetCount, ReachableTargets,
		TargetCount - ReachableTargets,
		TargetCount == ReachableTargets ? TEXT("true") : TEXT("false"),
		*FString::Join(FailedRoutes, TEXT(",")));
}

void AWarZoneFootprintPreview::VerifyTraversableElevation()
{
	if (bLoggedTraversableElevation || TileDesignPlacements.IsEmpty())
		return;

	const FLevelDesignPoint* Spawn = LevelDesignPoints.FindByPredicate(
		[](const FLevelDesignPoint& Point) { return Point.Type == ELevelDesignPointType::Spawn; });
	if (Spawn == nullptr)
		return;

	bLoggedTraversableElevation = true;

	// VerifyCriticalRoutes answers "is the cell field connected"; it says nothing
	// about height, so a route it calls reachable can still be walled off by a lip
	// the character cannot step over. This audit walks the same field with the
	// authored surface heights applied.
	TMap<FIntPoint, float> SurfaceByCell;
	SurfaceByCell.Reserve(TileDesignPlacements.Num());
	int32 WaterCellCount = 0;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		// Lake cells are deliberately unreachable - the bank is the Maze barrier the
		// design calls for. Counting them as walkable would report every shoreline as
		// an unclimbable lip and mark the whole map unreachable.
		if (Placement.Visual == ETileDesignVisual::Water)
		{
			++WaterCellCount;
			continue;
		}
		SurfaceByCell.Add(Placement.GridCell, GetSurfaceElevationForCell(Placement.GridCell));
	}
	TSet<FIntPoint> FacilityCells;
	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		for (const FIntPoint& Cell : Facility.OccupiedCells)
		{
			FacilityCells.Add(Cell);
			SurfaceByCell.Add(Cell, GetSurfaceElevationForCell(Cell));
		}
	}

	// Each vehicle ramp and infantry stair bridges exactly one cell edge: an entrance
	// cell inside the footprint and the cell just outside it. Every other pad edge is
	// a deliberate wall. Read the access points from the same helper the terrain
	// builder uses so the audit cannot drift out of step with what was built.
	TArray<TPair<FIntPoint, FIntPoint>> RampBridgedEdges;
	TArray<TPair<FIntPoint, FIntPoint>> AccessEdges;
	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		GetFacilityAccessEdges(Facility, AccessEdges);
		for (const TPair<FIntPoint, FIntPoint>& AccessEdge : AccessEdges)
		{
			const FIntPoint OutsideCell = AccessEdge.Key + AccessEdge.Value;
			RampBridgedEdges.Emplace(AccessEdge.Key, OutsideCell);
			RampBridgedEdges.Emplace(OutsideCell, AccessEdge.Key);
		}
	}

	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
	auto IsTraversableEdge = [&SurfaceByCell, &RampBridgedEdges](
		const FIntPoint& From, const FIntPoint& To)
	{
		const float* FromZ = SurfaceByCell.Find(From);
		const float* ToZ = SurfaceByCell.Find(To);
		if (FromZ == nullptr || ToZ == nullptr)
			return false;
		if (FMath::Abs(*ToZ - *FromZ) <= MaxTraversableStepCm)
			return true;
		return RampBridgedEdges.Contains(TPair<FIntPoint, FIntPoint>(From, To));
	};

	// Census of unclimbable risers, split by whether they belong to a facility pad
	// (intended, and ramped) or sit out on open ground (always a defect).
	int32 OpenGroundHardEdges = 0;
	int32 FacilityWallEdges = 0;
	float WorstOpenGroundStepCm = 0.0f;
	FIntPoint WorstOpenGroundCell = FIntPoint::ZeroValue;
	for (const TPair<FIntPoint, float>& Entry : SurfaceByCell)
	{
		for (const FIntPoint& Direction : Directions)
		{
			const float* NeighborZ = SurfaceByCell.Find(Entry.Key + Direction);
			if (NeighborZ == nullptr)
				continue;
			const float StepCm = FMath::Abs(*NeighborZ - Entry.Value);
			if (StepCm <= MaxTraversableStepCm)
				continue;
			if (FacilityCells.Contains(Entry.Key) || FacilityCells.Contains(Entry.Key + Direction))
			{
				++FacilityWallEdges;
				continue;
			}
			++OpenGroundHardEdges;
			if (StepCm > WorstOpenGroundStepCm)
			{
				WorstOpenGroundStepCm = StepCm;
				WorstOpenGroundCell = Entry.Key;
			}
		}
	}

	TSet<FIntPoint> Visited;
	TQueue<FIntPoint> Queue;
	Queue.Enqueue(Spawn->GridCell);
	Visited.Add(Spawn->GridCell);
	FIntPoint Cell;
	while (Queue.Dequeue(Cell))
	{
		for (const FIntPoint& Direction : Directions)
		{
			const FIntPoint Neighbor = Cell + Direction;
			if (!Visited.Contains(Neighbor) && IsTraversableEdge(Cell, Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	// The WarZone core is the raid's headline destination, so it is reported by name
	// rather than folded into the facility tally.
	FIntPoint WarZoneCoreCell = FIntPoint::ZeroValue;
	bool bHasWarZoneCore = false;
	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		if (Facility.VisualSet == EFacilityVisualSet::Warehouse
			&& Facility.Footprint == WarZoneCoreFootprint)
		{
			WarZoneCoreCell = Facility.AnchorCell + WarZoneCoreCentreOffset;
			bHasWarZoneCore = true;
			break;
		}
	}
	const bool bWarZoneCoreWalkable = bHasWarZoneCore && Visited.Contains(WarZoneCoreCell);

	int32 TargetCount = 0;
	int32 ReachableTargets = 0;
	TArray<FString> FailedTargets;
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Exit)
			continue;
		++TargetCount;
		if (Visited.Contains(Point.GridCell)) ++ReachableTargets;
		else FailedTargets.Add(Point.PointId.ToString());
	}
	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		++TargetCount;
		const bool bReached = Facility.OccupiedCells.ContainsByPredicate(
			[&Visited](const FIntPoint& FacilityCell) { return Visited.Contains(FacilityCell); });
		if (bReached) ++ReachableTargets;
		else FailedTargets.Add(FString::Printf(TEXT("Facility_%s_%d_%d"),
			*Facility.FacilityId.ToString(), Facility.AnchorCell.X, Facility.AnchorCell.Y));
	}

	const bool bPass = OpenGroundHardEdges == 0
		&& (!bHasWarZoneCore || bWarZoneCoreWalkable)
		&& TargetCount == ReachableTargets;
	UE_LOG(LogTemp, Display,
		TEXT("Ground step continuity: max_step_cm=%.0f water_cells_excluded=%d cells=%d open_ground_hard_edges=%d ")
		TEXT("worst_open_step_cm=%.0f worst_open_cell=(%d,%d) facility_wall_edges=%d ")
		TEXT("step_aware_reachable=%d/%d warzone_core=%s targets=%d failures=%d pass=%s sample=[%s]"),
		MaxTraversableStepCm,
		WaterCellCount,
		SurfaceByCell.Num(),
		OpenGroundHardEdges,
		WorstOpenGroundStepCm,
		WorstOpenGroundCell.X, WorstOpenGroundCell.Y,
		FacilityWallEdges,
		Visited.Num(), SurfaceByCell.Num(),
		bHasWarZoneCore ? (bWarZoneCoreWalkable ? TEXT("reachable") : TEXT("blocked")) : TEXT("absent"),
		TargetCount, TargetCount - ReachableTargets,
		bPass ? TEXT("true") : TEXT("false"),
		*FString::Join(FailedTargets, TEXT(",")));
}

void AWarZoneFootprintPreview::VerifyCoplanarSurfaces()
{
	if (bLoggedCoplanarSurfaces || !bRunCoplanarSurfaceAudit || TileDesignPlacements.IsEmpty())
		return;

	bLoggedCoplanarSurfaces = true;

	// Z-fighting is two faces landing on the same depth, and the runtime world is
	// assembled from several independent sources - shared ground slabs, the road
	// HISM, facility pads, packed tile geometry and terrain features. No single
	// builder can see the others, so the only way to catch a coplanar pair is to
	// inspect the finished world.
	//
	// An earlier version traced 123,000 rays downward and reported a confident zero.
	// That was worthless: a ray only stops on collision, and packed tile visuals
	// inherit whatever collision their source mesh had. A render-only marking or
	// slab lets every ray straight through while still fighting on screen. Compare
	// instance bounds instead, which is both collision-agnostic and far cheaper.
	constexpr float CoplanarToleranceCm = 5.0f;
	// 0.25 m^2. Neighbouring ground slabs are authored edge to edge, so they share a
	// boundary line but no area; only a real shared area puts two surfaces in the
	// same screen pixels.
	constexpr float MinimumSharedAreaCm2 = 2500.0f;
	constexpr int32 MaxTrackedSurfaces = 60000;
	// Depth precision is the whole reason this artifact exists: a 2 cm separation
	// reads as solid up close and collapses into a shimmer far away. Anything the
	// renderer culls before that distance therefore cannot be the cause of a flicker
	// seen across the map. Grass patches cull out at 80 m and overlap each other by
	// the tens of thousands, which was enough to blow through MaxTrackedSurfaces and
	// hide every long-range pair behind noise. Audit only what stays drawn.
	constexpr float MinAuditDrawDistanceCm = 15000.0f;

	struct FFlatSurface
	{
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
		float TopZ = 0.0f;
		FString Label;
	};

	TMap<FIntPoint, TArray<FFlatSurface>> SurfacesByCell;
	int32 InspectedInstanceCount = 0;
	int32 FlatSurfaceCount = 0;
	int32 SkippedNearFieldComponents = 0;
	bool bTruncated = false;

	auto ConsiderBounds = [&SurfacesByCell, &FlatSurfaceCount, &bTruncated](
		const FBox& WorldBounds, const FString& Label)
	{
		if (!WorldBounds.IsValid || bTruncated)
			return;
		const FVector Size = WorldBounds.GetSize();
		const float MinHorizontal = FMath::Min(Size.X, Size.Y);
		// Plate-like geometry only. A wall or a shipping container has a top face
		// too, but it is not a surface another surface can fight with in a way the
		// player sees; including them would bury the real hits in noise.
		if (MinHorizontal < 100.0f || Size.Z > MinHorizontal * 0.5f)
			return;
		if (++FlatSurfaceCount > MaxTrackedSurfaces)
		{
			bTruncated = true;
			return;
		}

		FFlatSurface Surface;
		Surface.Min = FVector2D(WorldBounds.Min.X, WorldBounds.Min.Y);
		Surface.Max = FVector2D(WorldBounds.Max.X, WorldBounds.Max.Y);
		Surface.TopZ = WorldBounds.Max.Z;
		Surface.Label = Label;
		const FVector Center = WorldBounds.GetCenter();
		const FIntPoint Cell(
			FMath::RoundToInt(Center.X / DesignCellSize),
			FMath::RoundToInt(Center.Y / DesignCellSize));
		SurfacesByCell.FindOrAdd(Cell).Add(MoveTemp(Surface));
	};

	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor) || Actor->IsHidden())
			continue;

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			if (!IsValid(MeshComponent) || !MeshComponent->IsVisible())
				continue;
			const UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
			if (!IsValid(Mesh))
				continue;

			float EndDrawDistanceCm = MeshComponent->CachedMaxDrawDistance;
			if (const UInstancedStaticMeshComponent* CullSource =
				Cast<UInstancedStaticMeshComponent>(MeshComponent))
			{
				int32 StartCullDistance = 0;
				int32 EndCullDistance = 0;
				CullSource->GetCullDistances(StartCullDistance, EndCullDistance);
				if (EndCullDistance > 0)
					EndDrawDistanceCm = static_cast<float>(EndCullDistance);
			}
			if (EndDrawDistanceCm > 0.0f && EndDrawDistanceCm < MinAuditDrawDistanceCm)
			{
				++SkippedNearFieldComponents;
				continue;
			}

			const FBox LocalBounds = Mesh->GetBoundingBox();
			const FString Label = FString::Printf(TEXT("%s/%s"),
				*MeshComponent->GetName(), *Mesh->GetName());
			if (UInstancedStaticMeshComponent* Instances =
				Cast<UInstancedStaticMeshComponent>(MeshComponent))
			{
				const int32 InstanceCount = Instances->GetInstanceCount();
				for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
				{
					FTransform InstanceTransform;
					if (!Instances->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
						continue;
					++InspectedInstanceCount;
					ConsiderBounds(LocalBounds.TransformBy(InstanceTransform), Label);
				}
			}
			else
			{
				++InspectedInstanceCount;
				ConsiderBounds(
					LocalBounds.TransformBy(MeshComponent->GetComponentTransform()), Label);
			}
		}
	}

	// A slab can straddle a cell boundary, so each cell is compared against itself
	// and the three neighbours on its positive side. That covers every adjacent pair
	// exactly once instead of finding each one twice from both directions.
	const FIntPoint CompareOffsets[] = {
		FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(1, 1)
	};
	int32 CoplanarPairCount = 0;
	float TightestGapCm = CoplanarToleranceCm;
	TArray<FString> Samples;
	TMap<FString, int32> PairCounts;
	for (const TPair<FIntPoint, TArray<FFlatSurface>>& CellEntry : SurfacesByCell)
	{
		for (const FIntPoint& Offset : CompareOffsets)
		{
			const TArray<FFlatSurface>* Neighbours = SurfacesByCell.Find(CellEntry.Key + Offset);
			if (Neighbours == nullptr)
				continue;
			const bool bSameCell = Offset == FIntPoint::ZeroValue;
			for (int32 LeftIndex = 0; LeftIndex < CellEntry.Value.Num(); ++LeftIndex)
			{
				const FFlatSurface& Left = CellEntry.Value[LeftIndex];
				const int32 FirstRight = bSameCell ? LeftIndex + 1 : 0;
				for (int32 RightIndex = FirstRight; RightIndex < Neighbours->Num(); ++RightIndex)
				{
					const FFlatSurface& Right = (*Neighbours)[RightIndex];
					const float GapCm = FMath::Abs(Left.TopZ - Right.TopZ);
					if (GapCm > CoplanarToleranceCm)
						continue;

					const float SharedX = FMath::Min(Left.Max.X, Right.Max.X)
						- FMath::Max(Left.Min.X, Right.Min.X);
					const float SharedY = FMath::Min(Left.Max.Y, Right.Max.Y)
						- FMath::Max(Left.Min.Y, Right.Min.Y);
					if (SharedX <= 0.0f || SharedY <= 0.0f
						|| SharedX * SharedY < MinimumSharedAreaCm2)
					{
						continue;
					}

					++CoplanarPairCount;
					TightestGapCm = FMath::Min(TightestGapCm, GapCm);
					PairCounts.FindOrAdd(FString::Printf(TEXT("%s|%s"), *Left.Label, *Right.Label))++;
					if (Samples.Num() < 6)
					{
						const float SharedCenterX = (FMath::Max(Left.Min.X, Right.Min.X)
							+ FMath::Min(Left.Max.X, Right.Max.X)) * 0.5f;
						const float SharedCenterY = (FMath::Max(Left.Min.Y, Right.Min.Y)
							+ FMath::Min(Left.Max.Y, Right.Max.Y)) * 0.5f;
						Samples.Add(FString::Printf(
							TEXT("world=(%.0f,%.0f) cell=(%d,%d) z=%.1f gap=%.2f area_m2=%.1f %s|%s"),
							SharedCenterX, SharedCenterY,
							FMath::RoundToInt(SharedCenterX / DesignCellSize),
							FMath::RoundToInt(SharedCenterY / DesignCellSize),
							Left.TopZ, GapCm, SharedX * SharedY / 10000.0f,
							*Left.Label, *Right.Label));
					}
				}
			}
		}
	}

	PairCounts.ValueSort([](int32 Left, int32 Right) { return Left > Right; });
	TArray<FString> RankedPairs;
	for (const TPair<FString, int32>& Entry : PairCounts)
	{
		RankedPairs.Add(FString::Printf(TEXT("%s x%d"), *Entry.Key, Entry.Value));
		if (RankedPairs.Num() >= 5)
			break;
	}

	UE_LOG(LogTemp, Display,
		TEXT("Coplanar surface audit: tolerance_cm=%.0f min_draw_distance_cm=%.0f ")
		TEXT("inspected_instances=%d near_field_components_skipped=%d flat_surfaces=%d ")
		TEXT("truncated=%s coplanar_pairs=%d distinct_pairs=%d tightest_gap_cm=%.2f pass=%s ")
		TEXT("top_pairs=[%s] sample=[%s]"),
		CoplanarToleranceCm,
		MinAuditDrawDistanceCm,
		InspectedInstanceCount,
		SkippedNearFieldComponents,
		FlatSurfaceCount,
		bTruncated ? TEXT("true") : TEXT("false"),
		CoplanarPairCount,
		PairCounts.Num(),
		CoplanarPairCount > 0 ? TightestGapCm : 0.0f,
		CoplanarPairCount == 0 ? TEXT("true") : TEXT("false"),
		*FString::Join(RankedPairs, TEXT(" ; ")),
		*FString::Join(Samples, TEXT(" ; ")));
}

void AWarZoneFootprintPreview::VerifyLocalPerformance(float DeltaSeconds)
{
	if (!bLoggedNavigation || PerformanceSampleCount >= 50)
		return;

	PerformanceDeltaSecondsTotal += FApp::GetDeltaTime();
	++PerformanceSampleCount;
	if (PerformanceSampleCount < 50)
		return;

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		++ActorCount;

	int32 PCGInstanceCount = 0;
	TArray<UActorComponent*> DressingInstanceComponents;
	GetComponents(UInstancedStaticMeshComponent::StaticClass(), DressingInstanceComponents);
	for (UActorComponent* Component : DressingInstanceComponents)
	{
		const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
		if (IsValid(ISM) && ISM->ComponentTags.Contains(TEXT("PCG_Dressing")))
			PCGInstanceCount += ISM->GetInstanceCount();
	}

	const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	const double AverageFrameSeconds = PerformanceDeltaSecondsTotal / PerformanceSampleCount;
	UE_LOG(LogTemp, Display,
		TEXT("Local performance: samples=%d avg_frame_ms=%.3f sampled_fps=%.1f actors=%d ground_hism=%d road_hism=%d pcg_instances=%d used_physical_mb=%.1f layout_hash=%08X"),
		PerformanceSampleCount,
		AverageFrameSeconds * 1000.0,
		AverageFrameSeconds > SMALL_NUMBER ? 1.0 / AverageFrameSeconds : 0.0,
		ActorCount,
		GroundHISM->GetInstanceCount(),
		RoadSurfaceHISM->GetInstanceCount(),
		PCGInstanceCount,
		MemoryStats.UsedPhysical / (1024.0 * 1024.0),
		LayoutHash);
}

void AWarZoneFootprintPreview::BuildTileDesignPlacements(
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	TileDesignPlacements.Reset();
	TSet<FIntPoint> ReservedCells;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		for (const FIntPoint& OccupiedCell : FacilityPlacement.OccupiedCells)
			ReservedCells.Add(OccupiedCell);
	}

	TMap<ETileDesignVisual, int32> Counts;
	int32 InvalidRotationCount = 0;
	TArray<FIntPoint> SortedCells;
	TileByCell.GetKeys(SortedCells);
	SortedCells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	});

	// Sculpted ground features claim whole cells exactly the way facilities do: a
	// reserved cell gets no flat slab and no tile actor, and the feature mesh fills it
	// instead. Only plain nature cells with a one-cell nature margin qualify, so a
	// feature never borders a road, spawn, exit, WarZone or facility footprint - the
	// props in those tiles all assume a flat surface and would be left floating.
	for (UHierarchicalInstancedStaticMeshComponent* FeatureComponent : TerrainFeatureHISMs)
	{
		if (IsValid(FeatureComponent))
			FeatureComponent->ClearInstances();
	}
	for (UHierarchicalInstancedStaticMeshComponent* DressingComponent :
		{ TerrainRockHISM.Get(), TerrainTreeHISM.Get(), TerrainBushHISM.Get() })
	{
		if (IsValid(DressingComponent))
			DressingComponent->ClearInstances();
	}
	const TArray<FTerrainFeatureMesh>& FeatureMeshes = GetTerrainFeatureMeshes();
	auto GetCellType = [&TileByCell](const FIntPoint& Cell, ETileType& OutType)
	{
		AMapTile* const* TilePtr = TileByCell.Find(Cell);
		if (TilePtr == nullptr || !IsValid(*TilePtr))
			return false;
		OutType = (*TilePtr)->GetType();
		return true;
	};
	// The same centre the ground and tile bands use, so a feature's material always
	// matches the flat cells it is dropped among.
	FIntPoint FeatureWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			FeatureWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	const AGameModePG* FeatureGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 FeatureSeed = IsValid(FeatureGameMode) ? FeatureGameMode->GetMapGenerationSeed() : 0;
	int32 TerrainFeatureCount = 0;
	for (const FIntPoint& FeatureAnchor : SortedCells)
	{
		if (TerrainFeatureCount >= 28 || FeatureMeshes.IsEmpty())
			break;

		const uint32 FeatureHash = HashCombine(
			GetTypeHash(FeatureSeed),
			HashCombine(GetTypeHash(FeatureAnchor.X), GetTypeHash(FeatureAnchor.Y)));
		if (FeatureHash % 13 != 0)
			continue;

		const int32 MeshIndex = static_cast<int32>((FeatureHash >> 8) % static_cast<uint32>(FeatureMeshes.Num()));
		const FIntPoint Footprint = FeatureMeshes[MeshIndex].Footprint;

		// Nature and WarZone cells both qualify, but a feature never straddles the two:
		// every cell it touches, margin included, must share the anchor's type.
		ETileType AnchorType = ETileType::None;
		if (!GetCellType(FeatureAnchor, AnchorType)
			|| (AnchorType != ETileType::None && AnchorType != ETileType::WarZone))
		{
			continue;
		}

		// The industrial core stays flat: it holds the central facility, its approach
		// lanes and the densest authored cover.
		const int32 CoreDeltaX = FeatureAnchor.X - FeatureWarZoneCenter.X;
		const int32 CoreDeltaY = FeatureAnchor.Y - FeatureWarZoneCenter.Y;
		const int32 CoreDistanceSquared = CoreDeltaX * CoreDeltaX + CoreDeltaY * CoreDeltaY;
		if (AnchorType == ETileType::WarZone && CoreDistanceSquared <= 64)
			continue;

		bool bFits = true;
		for (int32 OffsetX = -1; OffsetX <= Footprint.X && bFits; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= Footprint.Y && bFits; ++OffsetY)
			{
				const FIntPoint Cell(FeatureAnchor.X + OffsetX, FeatureAnchor.Y + OffsetY);
				ETileType CellType = ETileType::None;
				bFits = GetCellType(Cell, CellType) && CellType == AnchorType
					&& !ReservedCells.Contains(Cell);
			}
		}
		if (!bFits)
			continue;

		for (int32 OffsetX = 0; OffsetX < Footprint.X; ++OffsetX)
		{
			for (int32 OffsetY = 0; OffsetY < Footprint.Y; ++OffsetY)
				ReservedCells.Add(FIntPoint(FeatureAnchor.X + OffsetX, FeatureAnchor.Y + OffsetY));
		}

		// The authored perimeter sits at local Z=0 and the shared walking surface is
		// Z=20, so placing the centre there makes the seam exact.
		const FTerrainFeatureMesh& FeatureMesh = FeatureMeshes[MeshIndex];
		const FVector FeatureCenter(
			(FeatureAnchor.X + (Footprint.X - 1) * 0.5f) * DesignCellSize,
			(FeatureAnchor.Y + (Footprint.Y - 1) * 0.5f) * DesignCellSize,
			20.0f);
		// Band mirrors BuildLightweightWorldVisuals: a WarZone cell inside d^2=225 sits
		// on transition dirt, anything further out shares the nature ground.
		const int32 BandIndex = (AnchorType == ETileType::WarZone && CoreDistanceSquared <= 225) ? 1 : 0;
		const int32 ComponentIndex = BandIndex * FeatureMeshes.Num() + MeshIndex;
		UHierarchicalInstancedStaticMeshComponent* FeatureComponent =
			TerrainFeatureHISMs.IsValidIndex(ComponentIndex) ? TerrainFeatureHISMs[ComponentIndex] : nullptr;
		if (IsValid(FeatureComponent))
		{
			FeatureComponent->AddInstance(
				FTransform(FRotator::ZeroRotator, FeatureCenter, FVector::OneVector), true);
		}

		// Dress the slope off the same height field the mesh was generated from. These
		// cells carry no tile actor, so without this a feature reads as a bare lump.
		FRandomStream DressingStream(static_cast<int32>(FeatureHash));
		auto ScatterOnFeature = [&](
			UHierarchicalInstancedStaticMeshComponent* Component,
			int32 Count, float MinScale, float MaxScale, float SinkCm)
		{
			if (!IsValid(Component))
				return;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				// Stay off the outer eighth so nothing straddles the flat seam.
				const float U = DressingStream.FRandRange(0.12f, 0.88f);
				const float V = DressingStream.FRandRange(0.12f, 0.88f);
				const FVector Location = FeatureCenter + FVector(
					(U - 0.5f) * Footprint.X * DesignCellSize,
					(V - 0.5f) * Footprint.Y * DesignCellSize,
					SampleTerrainFeatureHeight(FeatureMesh, U, V) - SinkCm);
				Component->AddInstance(FTransform(
					FRotator(0.0f, DressingStream.FRandRange(0.0f, 360.0f), 0.0f),
					Location,
					FVector(DressingStream.FRandRange(MinScale, MaxScale))), true);
			}
		};
		// A hollow reads as a sheltered thicket, a rise as an exposed rocky crown. On
		// WarZone ground the pines are dropped entirely - a stand of forest inside an
		// industrial yard is exactly the kind of seam this band split exists to avoid.
		const bool bHollow = FeatureMesh.Amplitude < 0.0f;
		const bool bIndustrial = AnchorType == ETileType::WarZone;
		ScatterOnFeature(TerrainRockHISM,
			bIndustrial ? (bHollow ? 5 : 8) : (bHollow ? 3 : 6), 0.55f, 1.25f, 18.0f);
		ScatterOnFeature(TerrainTreeHISM,
			bIndustrial ? 0 : (bHollow ? 5 : 3), 0.85f, 1.35f, 12.0f);
		ScatterOnFeature(TerrainBushHISM,
			bIndustrial ? 4 : (bHollow ? 11 : 8), 0.70f, 1.30f, 6.0f);
		++TerrainFeatureCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Terrain features: placed=%d kinds=%d"),
		TerrainFeatureCount, FeatureMeshes.Num());
	FIntPoint MinCell = SortedCells.Num() > 0 ? SortedCells[0] : FIntPoint::ZeroValue;
	FIntPoint MaxCell = MinCell;
	for (const FIntPoint& Cell : SortedCells)
	{
		MinCell.X = FMath::Min(MinCell.X, Cell.X);
		MinCell.Y = FMath::Min(MinCell.Y, Cell.Y);
		MaxCell.X = FMath::Max(MaxCell.X, Cell.X);
		MaxCell.Y = FMath::Max(MaxCell.Y, Cell.Y);
	}
	const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 RaidSeed = IsValid(GameMode) ? GameMode->GetMapGenerationSeed() : 0;

	// The senior generator remains authoritative for gameplay regions. The design
	// layer adds deterministic dirt access roads between the authored facilities so
	// POIs do not read as isolated boxes in an otherwise traversable wilderness.
	// These cells become part of the future visual TileManifest, not a mutation of
	// AMapTile's server-owned type.
	TSet<FIntPoint> SupplementalRoadCells;
	TMap<FIntPoint, uint8> SupplementalRoadMasks;
	auto GetFacilityCenterCell = [](const FFacilityPlacement& Facility)
	{
		int32 SumX = 0;
		int32 SumY = 0;
		for (const FIntPoint& OccupiedCell : Facility.OccupiedCells)
		{
			SumX += OccupiedCell.X;
			SumY += OccupiedCell.Y;
		}
		const int32 Divisor = FMath::Max(1, Facility.OccupiedCells.Num());
		return FIntPoint(
			FMath::RoundToInt(static_cast<float>(SumX) / Divisor),
			FMath::RoundToInt(static_cast<float>(SumY) / Divisor));
	};
	auto ConnectSupplementalRoadCells = [&SupplementalRoadCells, &SupplementalRoadMasks, &TileByCell](
		const FIntPoint& A,
		const FIntPoint& B)
	{
		if (!TileByCell.Contains(A) || !TileByCell.Contains(B))
			return;
		SupplementalRoadCells.Add(A);
		SupplementalRoadCells.Add(B);
		const FIntPoint Delta = B - A;
		if (Delta == FIntPoint(1, 0))
		{
			SupplementalRoadMasks.FindOrAdd(A) |= EastConnection;
			SupplementalRoadMasks.FindOrAdd(B) |= WestConnection;
		}
		else if (Delta == FIntPoint(-1, 0))
		{
			SupplementalRoadMasks.FindOrAdd(A) |= WestConnection;
			SupplementalRoadMasks.FindOrAdd(B) |= EastConnection;
		}
		else if (Delta == FIntPoint(0, 1))
		{
			SupplementalRoadMasks.FindOrAdd(A) |= NorthConnection;
			SupplementalRoadMasks.FindOrAdd(B) |= SouthConnection;
		}
		else if (Delta == FIntPoint(0, -1))
		{
			SupplementalRoadMasks.FindOrAdd(A) |= SouthConnection;
			SupplementalRoadMasks.FindOrAdd(B) |= NorthConnection;
		}
	};
	// Facilities used to be wired to one another with a nearest-neighbour spanning
	// tree. That laid more road than the generator itself produced - roughly 131
	// cells of facility-to-facility web against the authored network - and none of
	// it served the route the player actually needs. Two outlying camps do not need
	// a paved road between them: the ground is flat and walkable everywhere, so the
	// web only buried the generator's deliberate road layout under service lanes.
	//
	// What genuinely reads as wrong is a compound with no approach at all. So give
	// each facility one short spur to the nearest road it can already reach, and
	// leave anything beyond the budget unpaved. Spawn/Exit endpoint repair and the
	// WarZone route guarantee below are untouched - those are the roads that make
	// the raid playable.
	int32 FacilitySpurCount = 0;
	int32 FacilitySpurCellCount = 0;
	int32 FacilitySpurSkippedCount = 0;
	// How far the skipped facilities actually were. Raising MaxFacilitySpurCells
	// blindly re-creates the old over-roading problem (the facility-to-facility
	// spanning tree once laid 131 cells against a 39-cell authored network), so the
	// budget only moves once these numbers say what it would actually cost.
	int32 NearestUnpavedCells = TNumericLimits<int32>::Max();
	int32 FarthestUnpavedCells = 0;
	int32 UnpavedCellsTotal = 0;
	int32 UnreachableFacilityCount = 0;
	if (!FacilityPlacements.IsEmpty())
	{
		const FIntPoint SpurOffsets[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (const FFacilityPlacement& Facility : FacilityPlacements)
		{
			// Elevated facilities terminate their route at the foot of the ramp, not
			// underneath the building in the reserved footprint.
			const FIntPoint StartCell = Facility.AccessCell != FIntPoint::ZeroValue
				? Facility.AccessCell
				: GetFacilityCenterCell(Facility);
			if (!TileByCell.Contains(StartCell))
			{
				++FacilitySpurSkippedCount;
				continue;
			}

			// Breadth-first so the spur is the shortest legal approach rather than a
			// meander; a service road that wanders 40 m to reach a road 3 cells away
			// is exactly the noise this pass exists to remove.
			TArray<FIntPoint> OpenCells;
			TMap<FIntPoint, FIntPoint> ParentByCell;
			TMap<FIntPoint, int32> DepthByCell;
			OpenCells.Add(StartCell);
			ParentByCell.Add(StartCell, StartCell);
			DepthByCell.Add(StartCell, 0);
			FIntPoint FoundRoad = StartCell;
			bool bFoundRoad = false;
			int32 ReadIndex = 0;
			const uint32 SpurHash = HashCombine(
				GetTypeHash(RaidSeed),
				HashCombine(GetTypeHash(StartCell.X), GetTypeHash(StartCell.Y)));

			while (ReadIndex < OpenCells.Num() && !bFoundRoad)
			{
				const FIntPoint Current = OpenCells[ReadIndex++];
				const int32 CurrentDepth = DepthByCell[Current];
				// The budget is applied to the *result*, not to the search. Cutting the
				// search short reported "no road" for facilities that had one just past
				// the limit, which hid how far off the network they really were.
				if (CurrentDepth >= MaxFacilitySpurSearchCells)
					continue;

				const int32 DirectionOffset = static_cast<int32>((SpurHash + ReadIndex) % 4u);
				for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
				{
					const FIntPoint Neighbor = Current + SpurOffsets[(DirectionIndex + DirectionOffset) % 4];
					if (ParentByCell.Contains(Neighbor) || ReservedCells.Contains(Neighbor))
						continue;
					AMapTile* const* NeighborTilePtr = TileByCell.Find(Neighbor);
					if (NeighborTilePtr == nullptr || !IsValid(*NeighborTilePtr))
						continue;
					const ETileType NeighborType = (*NeighborTilePtr)->GetType();
					if (NeighborType == ETileType::Obstacle)
						continue;

					ParentByCell.Add(Neighbor, Current);
					DepthByCell.Add(Neighbor, CurrentDepth + 1);
					if (NeighborType == ETileType::Road || SupplementalRoadCells.Contains(Neighbor))
					{
						FoundRoad = Neighbor;
						bFoundRoad = true;
						break;
					}
					OpenCells.Add(Neighbor);
				}
			}

			if (!bFoundRoad)
			{
				// No road anywhere within the search horizon - a placement problem,
				// not a budget one.
				++FacilitySpurSkippedCount;
				++UnreachableFacilityCount;
				continue;
			}

			const int32 RoadDistanceCells = DepthByCell.FindRef(FoundRoad);
			if (RoadDistanceCells > MaxFacilitySpurCells)
			{
				// Deliberate: a compound this far from the network stays unpaved
				// rather than dragging a long service lane across open terrain.
				// Named because it matters *which* ones these are: satellite camps,
				// barracks and the trench sit inside the WarZone where no road runs
				// by design, while a stranded Downtown or Factory is a real fault.
				UE_LOG(LogTemp, Display,
					TEXT("  unpaved facility: set=%s cell=(%d,%d) road_cells=%d"),
					*StaticEnum<EFacilityVisualSet>()->GetNameStringByValue(
						static_cast<int64>(Facility.VisualSet)),
					StartCell.X, StartCell.Y, RoadDistanceCells);
				++FacilitySpurSkippedCount;
				NearestUnpavedCells = FMath::Min(NearestUnpavedCells, RoadDistanceCells);
				FarthestUnpavedCells = FMath::Max(FarthestUnpavedCells, RoadDistanceCells);
				UnpavedCellsTotal += RoadDistanceCells;
				continue;
			}

			TArray<FIntPoint> ReversePath;
			FIntPoint PathCell = FoundRoad;
			ReversePath.Add(PathCell);
			while (PathCell != StartCell)
			{
				const FIntPoint* Parent = ParentByCell.Find(PathCell);
				if (Parent == nullptr || *Parent == PathCell)
					break;
				PathCell = *Parent;
				ReversePath.Add(PathCell);
			}
			if (ReversePath.Last() != StartCell)
			{
				++FacilitySpurSkippedCount;
				continue;
			}
			for (int32 PathIndex = ReversePath.Num() - 1; PathIndex > 0; --PathIndex)
				ConnectSupplementalRoadCells(ReversePath[PathIndex], ReversePath[PathIndex - 1]);
			++FacilitySpurCount;
			FacilitySpurCellCount += ReversePath.Num();
		}
	}
	const int32 BudgetedUnpavedCount = FacilitySpurSkippedCount - UnreachableFacilityCount;
	UE_LOG(LogTemp, Display,
		TEXT("Facility access spurs: facilities=%d spurs=%d unpaved=%d cells=%d budget_cells=%d ")
		TEXT("unpaved_distance_cells=[min=%d avg=%.1f max=%d] unreachable=%d ")
		TEXT("cells_if_budget_covered_all=%d"),
		FacilityPlacements.Num(), FacilitySpurCount, FacilitySpurSkippedCount,
		FacilitySpurCellCount, MaxFacilitySpurCells,
		BudgetedUnpavedCount > 0 ? NearestUnpavedCells : 0,
		BudgetedUnpavedCount > 0 ? static_cast<float>(UnpavedCellsTotal) / BudgetedUnpavedCount : 0.0f,
		FarthestUnpavedCells, UnreachableFacilityCount,
		FacilitySpurCellCount + UnpavedCellsTotal);

	// The facility tree above does not include server-authored Spawn/Exit cells.
	// Repair each endpoint to the nearest real road through valid non-facility
	// cells, otherwise one seed can leave a spawn pad visually stranded even
	// though the other endpoints happen to touch the generated road network.
	int32 EndpointCount = 0;
	int32 RepairedEndpointCount = 0;
	int32 FailedEndpointCount = 0;
	const FIntPoint NeighborOffsets[] = {
		FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
	};
	for (const FIntPoint& EndpointCell : SortedCells)
	{
		AMapTile* const* EndpointTilePtr = TileByCell.Find(EndpointCell);
		if (EndpointTilePtr == nullptr || !IsValid(*EndpointTilePtr))
			continue;
		const ETileType EndpointType = (*EndpointTilePtr)->GetType();
		if (EndpointType != ETileType::Spawn && EndpointType != ETileType::Exit)
			continue;

		++EndpointCount;
		TArray<FIntPoint> OpenCells;
		TMap<FIntPoint, FIntPoint> ParentByCell;
		OpenCells.Add(EndpointCell);
		ParentByCell.Add(EndpointCell, EndpointCell);
		FIntPoint FoundRoad = EndpointCell;
		bool bFoundRoad = false;
		int32 ReadIndex = 0;
		const uint32 EndpointHash = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(EndpointCell.X), GetTypeHash(EndpointCell.Y)));

		while (ReadIndex < OpenCells.Num() && !bFoundRoad)
		{
			const FIntPoint Current = OpenCells[ReadIndex++];
			const int32 DirectionOffset = static_cast<int32>((EndpointHash + ReadIndex) % 4u);
			for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
			{
				const FIntPoint Neighbor = Current + NeighborOffsets[(DirectionIndex + DirectionOffset) % 4];
				if (ParentByCell.Contains(Neighbor) || ReservedCells.Contains(Neighbor))
					continue;
				AMapTile* const* NeighborTilePtr = TileByCell.Find(Neighbor);
				if (NeighborTilePtr == nullptr || !IsValid(*NeighborTilePtr))
					continue;
				const ETileType NeighborType = (*NeighborTilePtr)->GetType();
				if (NeighborType == ETileType::Obstacle)
					continue;

				ParentByCell.Add(Neighbor, Current);
				if (NeighborType == ETileType::Road || SupplementalRoadCells.Contains(Neighbor))
				{
					FoundRoad = Neighbor;
					bFoundRoad = true;
					break;
				}
				OpenCells.Add(Neighbor);
			}
		}

		if (!bFoundRoad)
		{
			++FailedEndpointCount;
			continue;
		}

		TArray<FIntPoint> ReversePath;
		FIntPoint PathCell = FoundRoad;
		ReversePath.Add(PathCell);
		while (PathCell != EndpointCell)
		{
			const FIntPoint* Parent = ParentByCell.Find(PathCell);
			if (Parent == nullptr || *Parent == PathCell)
				break;
			PathCell = *Parent;
			ReversePath.Add(PathCell);
		}
		if (ReversePath.Last() != EndpointCell)
		{
			++FailedEndpointCount;
			continue;
		}
		for (int32 PathIndex = ReversePath.Num() - 1; PathIndex > 0; --PathIndex)
		{
			ConnectSupplementalRoadCells(ReversePath[PathIndex], ReversePath[PathIndex - 1]);
		}
		++RepairedEndpointCount;
	}
	UE_LOG(LogTemp, Log,
		TEXT("Endpoint access road repair: endpoints=%d repaired=%d failed=%d"),
		EndpointCount, RepairedEndpointCount, FailedEndpointCount);

	// A spawn touching an arbitrary nearby road is not sufficient: that road can
	// still belong to a disconnected outer branch.  Build one deterministic
	// shortest visual route from every spawn to the nearest WarZone boundary.
	// This does not change the server-owned tile types; it only guarantees that
	// the client-side design manifest has a continuous readable approach route.
	// Exits need the same guarantee. Covering only spawns left exit routes ending in
	// a dead-end branch that never reached the centre, so a player who found an exit
	// pad had no road leading back toward the WarZone.
	int32 SpawnRouteCount = 0;
	int32 ConnectedSpawnRouteCount = 0;
	int32 FailedSpawnRouteCount = 0;
	int32 LongestSpawnRouteCells = 0;
	for (const FIntPoint& SpawnCell : SortedCells)
	{
		AMapTile* const* SpawnTilePtr = TileByCell.Find(SpawnCell);
		if (SpawnTilePtr == nullptr || !IsValid(*SpawnTilePtr))
			continue;
		const ETileType EndpointType = (*SpawnTilePtr)->GetType();
		if (EndpointType != ETileType::Spawn && EndpointType != ETileType::Exit)
			continue;

		++SpawnRouteCount;
		TArray<FIntPoint> OpenCells;
		TMap<FIntPoint, FIntPoint> ParentByCell;
		OpenCells.Add(SpawnCell);
		ParentByCell.Add(SpawnCell, SpawnCell);
		FIntPoint FoundWarZone = SpawnCell;
		bool bFoundWarZone = false;
		int32 ReadIndex = 0;
		const uint32 SpawnHash = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(SpawnCell.X), GetTypeHash(SpawnCell.Y)));

		while (ReadIndex < OpenCells.Num() && !bFoundWarZone)
		{
			const FIntPoint Current = OpenCells[ReadIndex++];
			const int32 DirectionOffset = static_cast<int32>((SpawnHash + ReadIndex) % 4u);
			for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
			{
				const FIntPoint Neighbor = Current + NeighborOffsets[(DirectionIndex + DirectionOffset) % 4];
				if (ParentByCell.Contains(Neighbor))
					continue;
				AMapTile* const* NeighborTilePtr = TileByCell.Find(Neighbor);
				if (NeighborTilePtr == nullptr || !IsValid(*NeighborTilePtr))
					continue;
				const ETileType NeighborType = (*NeighborTilePtr)->GetType();
				if (NeighborType == ETileType::Obstacle
					|| (ReservedCells.Contains(Neighbor) && NeighborType != ETileType::WarZone))
				{
					continue;
				}

				ParentByCell.Add(Neighbor, Current);
				if (NeighborType == ETileType::WarZone)
				{
					FoundWarZone = Neighbor;
					bFoundWarZone = true;
					break;
				}
				OpenCells.Add(Neighbor);
			}
		}

		if (!bFoundWarZone)
		{
			++FailedSpawnRouteCount;
			continue;
		}

		TArray<FIntPoint> ReversePath;
		FIntPoint PathCell = FoundWarZone;
		ReversePath.Add(PathCell);
		while (PathCell != SpawnCell)
		{
			const FIntPoint* Parent = ParentByCell.Find(PathCell);
			if (Parent == nullptr || *Parent == PathCell)
				break;
			PathCell = *Parent;
			ReversePath.Add(PathCell);
		}
		if (ReversePath.Last() != SpawnCell)
		{
			++FailedSpawnRouteCount;
			continue;
		}

		for (int32 PathIndex = ReversePath.Num() - 1; PathIndex > 0; --PathIndex)
		{
			ConnectSupplementalRoadCells(ReversePath[PathIndex], ReversePath[PathIndex - 1]);
		}
		LongestSpawnRouteCells = FMath::Max(LongestSpawnRouteCells, ReversePath.Num());
		++ConnectedSpawnRouteCount;
	}
	UE_LOG(LogTemp, Log,
		TEXT("Spawn/Exit to WarZone road guarantee: endpoints=%d connected=%d failed=%d longest_cells=%d"),
		SpawnRouteCount, ConnectedSpawnRouteCount, FailedSpawnRouteCount, LongestSpawnRouteCells);

	// Counted while the placements are built below; declared here so the summary
	// that follows the placement loop can report them together.
	int32 SpawnOpeningCount = 0;
	int32 SpawnRoutedOpenings = 0;
	int32 WalledOffSpawnConnections = 0;

	// One lake for the whole pass; the corner is scored against the road network,
	// so this must not be re-derived per cell.
	const FBorderLake BorderLake = GetBorderLake(
		RaidSeed, MinCell, MaxCell, BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	BorderLakeCentreCell = BorderLake.CentreCell;
	bHasBorderLake = BorderLakeRadiusCells > 0.0f;

	for (const FIntPoint& Cell : SortedCells)
	{
		const AMapTile* const* TilePtr = TileByCell.Find(Cell);
		const AMapTile* Tile = TilePtr != nullptr ? *TilePtr : nullptr;
		if (!IsValid(Tile) || ReservedCells.Contains(Cell))
			continue;
		const ETileType Type = Tile->GetType();
		const bool bSupplementalRoad = SupplementalRoadCells.Contains(Cell)
			&& Type != ETileType::Road
			&& Type != ETileType::Spawn
			&& Type != ETileType::Exit
			&& Type != ETileType::WarZone
			&& Type != ETileType::Obstacle;

		auto IsAuthoritativeTraversalNeighbor = [&TileByCell, &SupplementalRoadCells](const FIntPoint& NeighborCell)
		{
			AMapTile* const* Neighbor = TileByCell.Find(NeighborCell);
			if (Neighbor == nullptr || !IsValid(*Neighbor))
				return false;
			const ETileType NeighborType = (*Neighbor)->GetType();
			const bool bNeighborBecomesAccessRoad = SupplementalRoadCells.Contains(NeighborCell)
				&& NeighborType != ETileType::Road
				&& NeighborType != ETileType::Spawn
				&& NeighborType != ETileType::Exit
				&& NeighborType != ETileType::WarZone
				&& NeighborType != ETileType::Obstacle;
			// An access road consumes the original visual cell. Only the explicit
			// planned route mask may connect to it; incidental side adjacency is not
			// a socket and must not turn a one-lane path into a giant crossroad.
			if (bNeighborBecomesAccessRoad)
				return false;
			return IsRoadTraversalType((*Neighbor)->GetType());
		};

		// Explicit route edges are authoritative for supplemental connectors. If a
		// route crosses an existing generator road, union those explicit edges with
		// that road's normal neighbours so both sides keep reciprocal sockets.
		uint8 ConnectionMask = SupplementalRoadMasks.FindRef(Cell);
		if (!bSupplementalRoad)
		{
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(0, 1))) ConnectionMask |= NorthConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(1, 0))) ConnectionMask |= EastConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(0, -1))) ConnectionMask |= SouthConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(-1, 0))) ConnectionMask |= WestConnection;
		}

		FTileDesignPlacement& Placement = TileDesignPlacements.AddDefaulted_GetRef();
		Placement.GridCell = Cell;
		Placement.WorldLocation = FVector(
			Cell.X * DesignCellSize,
			Cell.Y * DesignCellSize,
			0.0f);
		Placement.ConnectionMask = ConnectionMask;
		Placement.bSupplementalAccessRoad = bSupplementalRoad;
		Placement.LocalSeed = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(Cell.X), GetTypeHash(Cell.Y)));
		Placement.LayoutVariant = static_cast<uint8>(Placement.LocalSeed % 4);

		if (Type == ETileType::Road || bSupplementalRoad)
		{
			const int32 ConnectionCount = FMath::CountBits(ConnectionMask);
			if (ConnectionCount >= 4)
			{
				Placement.Visual = ETileDesignVisual::RoadCross;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadCrossLevelPath);
			}
			else if (ConnectionCount == 3)
			{
				Placement.Visual = ETileDesignVisual::RoadTJunction;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadTJunctionLevelPath);
				Placement.RotationQuarterTurns = FindPositiveYawRotation(
					NorthConnection | EastConnection | WestConnection,
					ConnectionMask);
			}
			else if (ConnectionCount == 2
				&& (ConnectionMask == (EastConnection | WestConnection)
					|| ConnectionMask == (NorthConnection | SouthConnection)))
			{
				Placement.Visual = ETileDesignVisual::RoadStraight;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadStraightLevelPath);
				Placement.RotationQuarterTurns = ConnectionMask == (EastConnection | WestConnection) ? 0 : 1;
			}
			else if (ConnectionCount == 2)
			{
				Placement.Visual = ETileDesignVisual::RoadCorner;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadCornerLevelPath);
				Placement.RotationQuarterTurns = FindPositiveYawRotation(
					NorthConnection | WestConnection,
					ConnectionMask);
			}
			else
			{
				Placement.Visual = ETileDesignVisual::RoadDeadEnd;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadDeadEndLevelPath);
				const uint8 TargetMask = ConnectionMask != 0 ? ConnectionMask : WestConnection;
				Placement.RotationQuarterTurns = FindPositiveYawRotation(WestConnection, TargetMask);
			}
		}
		else if (Type == ETileType::Spawn)
		{
			Placement.Visual = ETileDesignVisual::Spawn;
			Placement.VisualLevel = TSoftObjectPtr<UWorld>(SpawnLevelPath);
			// LD_Tile_Spawn_Staging is a walled compound with a single opening on its
			// authored east face, so the rotation chosen here decides which one of the
			// cell's connections the player can actually walk out through. Picking the
			// lowest set bit meant a spawn wired north+east faced north and sealed the
			// east road behind a wall - the player saw no way to the WarZone at all.
			//
			// Prefer the edge the spawn-to-WarZone guarantee actually routed through,
			// so the opening always faces the path that is promised to lead somewhere.
			const uint8 RouteMask = SupplementalRoadMasks.FindRef(Cell) & ConnectionMask;
			const uint8 PreferredMask = RouteMask != 0 ? RouteMask : ConnectionMask;
			const uint8 TargetMask = PreferredMask != 0
				? static_cast<uint8>(1 << FMath::CountTrailingZeros(PreferredMask))
				: EastConnection;
			Placement.RotationQuarterTurns = FindPositiveYawRotation(EastConnection, TargetMask);
			// Any remaining connection is a road that dead-ends against this tile's
			// wall. Harmless for routing, but it reads as a road to nowhere, so count
			// them instead of letting them pass silently.
			SpawnOpeningCount += ConnectionMask != 0 ? 1 : 0;
			WalledOffSpawnConnections += FMath::Max(0, FMath::CountBits(ConnectionMask) - 1);
			SpawnRoutedOpenings += RouteMask != 0 ? 1 : 0;
		}
		else if (Type == ETileType::Exit || Type == ETileType::Obstacle)
		{
			Placement.Visual = Type == ETileType::Exit
				? ETileDesignVisual::Exit
				: ETileDesignVisual::Obstacle;
			Placement.VisualLevel = TSoftObjectPtr<UWorld>(
				Type == ETileType::Exit ? ExitLevelPath : ObstacleLevelPath);
			Placement.RotationQuarterTurns =
				(ConnectionMask & (NorthConnection | SouthConnection)) != 0
				&& (ConnectionMask & (EastConnection | WestConnection)) == 0
				? 1 : 0;
		}
		else if (Type == ETileType::WarZone)
		{
			Placement.Visual = ETileDesignVisual::WarZoneGround;
		}
		else
		{
			auto HasTraversalWithin = [&TileByCell, &SupplementalRoadCells, &Cell](int32 Radius)
			{
				for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
				{
					for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
					{
						if (FMath::Abs(OffsetX) + FMath::Abs(OffsetY) > Radius)
							continue;
						const FIntPoint CandidateCell = Cell + FIntPoint(OffsetX, OffsetY);
						if (SupplementalRoadCells.Contains(CandidateCell))
							return true;
						AMapTile* const* Candidate = TileByCell.Find(CandidateCell);
						if (Candidate != nullptr && IsValid(*Candidate) && IsRoadTraversalType((*Candidate)->GetType()))
							return true;
					}
				}
				return false;
			};

			const int32 BorderDistance = FMath::Min(
				FMath::Min(Cell.X - MinCell.X, MaxCell.X - Cell.X),
				FMath::Min(Cell.Y - MinCell.Y, MaxCell.Y - Cell.Y));
			const bool bMapBorder = BorderDistance <= 2;
			const bool bNearTraversal = HasTraversalWithin(2);
			const int32 MacroX = FMath::FloorToInt(static_cast<float>(Cell.X) / 5.0f);
			const int32 MacroY = FMath::FloorToInt(static_cast<float>(Cell.Y) / 5.0f);
			const uint32 MacroHash = HashCombine(
				GetTypeHash(RaidSeed),
				HashCombine(GetTypeHash(MacroX), GetTypeHash(MacroY)));
			const uint32 FineHash = HashCombine(
				GetTypeHash(Placement.LocalSeed),
				HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Cell.X)));
			const int32 MacroRoll = MacroHash % 100;
			const int32 FineRoll = FineHash % 100;
			const int32 MazeBandValue = FMath::Abs(MacroX * 3 + MacroY * 5
				+ static_cast<int32>(GetTypeHash(RaidSeed) % 17));
			const bool bMazeBarrierBand = !bNearTraversal && !bMapBorder
				&& MazeBandValue % 9 == 0;

			// A lake eats into one corner of the map. The grid is square and always
			// will be, but the coastline does not have to be: a metaball centred
			// outside the corner plus per-cell jitter gives a ragged shore, which is
			// what stops the border reading as a drawn box. It also gives the shore
			// assets - the rural diorama, its pier and boats - somewhere they belong
			// instead of sitting in the middle of a dry field.
			//
			// Traversal cells are never flooded, and neither are their neighbours, so
			// a spawn or exit that the generator placed near the edge keeps a
			// peninsula out to it rather than drowning.
			if (BorderLakeRadiusCells > 0.0f
				&& !bNearTraversal && Placement.Visual != ETileDesignVisual::WarZoneGround)
			{
				const float LakeRadius = BorderLake.Radius + GetLakeShoreJitter(RaidSeed, Cell);
				const float DistanceToLakeCentre =
					FVector2D::Distance(FVector2D(Cell.X, Cell.Y), BorderLake.CentreCell);
				if (DistanceToLakeCentre <= LakeRadius)
					Placement.Visual = ETileDesignVisual::Water;
			}

			// Natural cells are grouped into 5x5-cell macro biomes so the result reads
			// as forest belts, clearings and rocky fields rather than visual noise.
			// Road-adjacent cells deliberately remain more open for combat readability.
			if (Placement.Visual == ETileDesignVisual::Water)
			{
				// Already decided above; the biome roll must not overwrite it.
			}
			else if (bMapBorder)
			{
				Placement.Visual = FineRoll < 72
					? ETileDesignVisual::NatureForestDense
					: ETileDesignVisual::NatureForestSparse;
			}
			else if (bMazeBarrierBand)
			{
				// Five-cell macro belts become the Maze's impassable-looking terrain.
				// Existing/generated traversal cells and a two-cell shoulder are always
				// exempt, so this shapes routes without changing server connectivity.
				Placement.Visual = FineRoll < 76
					? ETileDesignVisual::NatureForestDense
					: ETileDesignVisual::NatureRocky;
			}
			else if (bNearTraversal)
			{
				if (FineRoll < 32) Placement.Visual = ETileDesignVisual::NatureMeadow;
				else if (FineRoll < 54) Placement.Visual = ETileDesignVisual::NatureScrub;
				else if (FineRoll < 72) Placement.Visual = ETileDesignVisual::NatureForestSparse;
				else if (FineRoll < 84) Placement.Visual = ETileDesignVisual::NatureRocky;
				else if (FineRoll < 94) Placement.Visual = ETileDesignVisual::NatureDitch;
				else Placement.Visual = ETileDesignVisual::NatureServiceCamp;
			}
			else if (MacroRoll < 36)
			{
				Placement.Visual = FineRoll < 74
					? ETileDesignVisual::NatureForestDense
					: ETileDesignVisual::NatureForestSparse;
			}
			else if (MacroRoll < 61)
			{
				Placement.Visual = FineRoll < 72
					? ETileDesignVisual::NatureForestSparse
					: ETileDesignVisual::NatureAmbush;
			}
			else if (MacroRoll < 77)
			{
				Placement.Visual = FineRoll < 64
					? ETileDesignVisual::NatureMeadow
					: ETileDesignVisual::NatureScrub;
			}
			else if (MacroRoll < 88)
			{
				Placement.Visual = FineRoll < 62
					? ETileDesignVisual::NatureRocky
					: ETileDesignVisual::NatureDitch;
			}
			else if (MacroRoll < 96)
			{
				Placement.Visual = FineRoll < 60
					? ETileDesignVisual::NatureScrub
					: ETileDesignVisual::NatureAmbush;
			}
			else
			{
				Placement.Visual = FineRoll < 45
					? ETileDesignVisual::Ruins
					: ETileDesignVisual::NatureServiceCamp;
			}

			if (Placement.Visual == ETileDesignVisual::Ruins)
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RuinsLevelPath);
		}

		Counts.FindOrAdd(Placement.Visual)++;
		if (Placement.RotationQuarterTurns < 0 || Placement.RotationQuarterTurns > 3)
			++InvalidRotationCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Supplemental POI road design: cells=%d facilities=%d"),
		SupplementalRoadCells.Num(), FacilityPlacements.Num());

	LayoutHash = 0;
	TMap<FIntPoint, const FTileDesignPlacement*> PlacementByCell;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		PlacementByCell.Add(Placement.GridCell, &Placement);
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.GridCell.X));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.GridCell.Y));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(static_cast<uint8>(Placement.Visual)));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.ConnectionMask));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.bSupplementalAccessRoad));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.LayoutVariant));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	// routed_openings should equal openings: every spawn's single doorway ought to
	// face the road the WarZone guarantee actually planned. walled_connections is
	// the count of roads that still dead-end against a spawn's wall - the tile has
	// one opening, so anything above zero is a road the player can see but not use.
	UE_LOG(LogTemp, Display,
		TEXT("Spawn opening alignment: spawns=%d routed_openings=%d walled_connections=%d"),
		SpawnOpeningCount, SpawnRoutedOpenings, WalledOffSpawnConnections);

	int32 RoadRotationMismatchCount = 0;
	int32 ReciprocalConnectionMismatchCount = 0;
	const struct FDirectionCheck
	{
		FIntPoint Offset;
		uint8 Bit;
		uint8 OppositeBit;
	} DirectionChecks[] = {
		{ FIntPoint(0, 1), NorthConnection, SouthConnection },
		{ FIntPoint(1, 0), EastConnection, WestConnection },
		{ FIntPoint(0, -1), SouthConnection, NorthConnection },
		{ FIntPoint(-1, 0), WestConnection, EastConnection }
	};
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		uint8 CanonicalMask = 0;
		const bool bTraversalPlacement =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle
			|| Placement.Visual == ETileDesignVisual::WarZoneGround;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::RoadStraight: CanonicalMask = EastConnection | WestConnection; break;
		case ETileDesignVisual::RoadCorner: CanonicalMask = NorthConnection | WestConnection; break;
		case ETileDesignVisual::RoadTJunction: CanonicalMask = NorthConnection | EastConnection | WestConnection; break;
		case ETileDesignVisual::RoadCross: CanonicalMask = NorthConnection | EastConnection | SouthConnection | WestConnection; break;
		case ETileDesignVisual::RoadDeadEnd: CanonicalMask = WestConnection; break;
		default: break;
		}
		if (CanonicalMask != 0)
		{
			uint8 RotatedMask = CanonicalMask;
			for (int32 RotationIndex = 0; RotationIndex < Placement.RotationQuarterTurns; ++RotationIndex)
				RotatedMask = RotateConnectionMaskPositiveYaw(RotatedMask);
			if (RotatedMask != Placement.ConnectionMask)
				++RoadRotationMismatchCount;
		}

		if (!bTraversalPlacement)
			continue;

		for (const FDirectionCheck& DirectionCheck : DirectionChecks)
		{
			if ((Placement.ConnectionMask & DirectionCheck.Bit) == 0)
				continue;
			const FTileDesignPlacement* const* Neighbor = PlacementByCell.Find(
				Placement.GridCell + DirectionCheck.Offset);
			if (Neighbor != nullptr
				&& ((*Neighbor)->ConnectionMask & DirectionCheck.OppositeBit) == 0)
			{
				++ReciprocalConnectionMismatchCount;
			}
		}
	}
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		LayoutHash = HashCombine(LayoutHash, FCrc::StrCrc32(*Placement.FacilityId.ToString()));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(static_cast<uint8>(Placement.VisualSet)));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.AnchorCell.X));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.AnchorCell.Y));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.Footprint.X));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.Footprint.Y));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		LayoutHash = HashCombine(LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	UE_LOG(LogTemp, Display,
		TEXT("Design placement spec: cells=%d reserved=%d invalid_rotations=%d map_extent_cm=%.0f layout_hash=%08X"),
		TileDesignPlacements.Num(), ReservedCells.Num(), InvalidRotationCount,
		GridCellSpan * DesignCellSize, LayoutHash);
	UE_LOG(LogTemp, Display,
		TEXT("Road connection validation: rotation_mismatches=%d reciprocal_mismatches=%d"),
		RoadRotationMismatchCount,
		ReciprocalConnectionMismatchCount);
	for (const TPair<ETileDesignVisual, int32>& Count : Counts)
	{
		UE_LOG(LogTemp, Display, TEXT("Design placement count: visual=%d count=%d"),
			static_cast<int32>(Count.Key), Count.Value);
	}
}

float AWarZoneFootprintPreview::GetSurfaceElevationForCell(const FIntPoint& Cell) const
{
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		if (!Placement.OccupiedCells.Contains(Cell))
			continue;
		return Placement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : Placement.BaseElevationCm;
	}
	// Everything outside a facility footprint shares the one ground datum.
	//
	// This used to blend a one- and two-cell "shoulder" ring around every raised or
	// lowered pad. That reads as a slope on paper only: ground is rendered as one
	// flat 20 m slab per cell, so the ring was really a stack of 20 m terraces whose
	// risers measured 30-90 cm. Against a 45 cm MaxStepHeight that produced hard
	// lips out in the open - including across roads that merely passed within two
	// cells of a facility - and the rings of neighbouring facilities stacked into
	// still taller ones. Deliberate elevation now exists only on facility
	// footprints, which own an explicit vehicle ramp and infantry stair built by
	// BuildElevatedFacilityTerrain.
	return BaseGroundSurfaceZ;
}

void AWarZoneFootprintPreview::GetFacilityAccessEdges(
	const FFacilityPlacement& Placement,
	TArray<TPair<FIntPoint, FIntPoint>>& OutAccessEdges) const
{
	OutAccessEdges.Reset();
	if (Placement.OccupiedCells.IsEmpty() || Placement.EntranceDirection == FIntPoint::ZeroValue)
		return;

	OutAccessEdges.Emplace(Placement.EntranceCell, Placement.EntranceDirection);

	// The WarZone core is the raid's final engagement and its pad is 3x3 cells, so
	// one ramp leaves roughly 5% of a 240 m perimeter climbable and funnels every
	// attacker into a single lane. Give it an approach on each side instead. The
	// outlying 2x2 and 2x1 facilities keep their single entrance: at that size one
	// way in reads as a defensible outpost rather than a bottleneck.
	const bool bWarZoneCore = Placement.VisualSet == EFacilityVisualSet::Warehouse
		&& Placement.Footprint == WarZoneCoreFootprint;
	if (!bWarZoneCore)
		return;

	int32 MinX = MAX_int32;
	int32 MinY = MAX_int32;
	int32 MaxX = MIN_int32;
	int32 MaxY = MIN_int32;
	for (const FIntPoint& Cell : Placement.OccupiedCells)
	{
		MinX = FMath::Min(MinX, Cell.X);
		MinY = FMath::Min(MinY, Cell.Y);
		MaxX = FMath::Max(MaxX, Cell.X);
		MaxY = FMath::Max(MaxY, Cell.Y);
	}
	const int32 MidX = (MinX + MaxX) / 2;
	const int32 MidY = (MinY + MaxY) / 2;
	const TPair<FIntPoint, FIntPoint> SideCandidates[] = {
		{ FIntPoint(MidX, MaxY), FIntPoint(0, 1) },
		{ FIntPoint(MaxX, MidY), FIntPoint(1, 0) },
		{ FIntPoint(MidX, MinY), FIntPoint(0, -1) },
		{ FIntPoint(MinX, MidY), FIntPoint(-1, 0) }
	};
	for (const TPair<FIntPoint, FIntPoint>& Candidate : SideCandidates)
	{
		// The authored entrance already covers its own side. Adding the mid-edge cell
		// for that same direction would stack a second ramp on top of the first.
		if (Candidate.Value == Placement.EntranceDirection)
			continue;
		if (!Placement.OccupiedCells.Contains(Candidate.Key))
			continue;
		OutAccessEdges.Add(Candidate);
	}
}

void AWarZoneFootprintPreview::BuildElevatedFacilityTerrain()
{
	for (UStaticMeshComponent* Component : ElevatedTerrainComponents)
	{
		if (IsValid(Component))
			Component->DestroyComponent();
	}
	ElevatedTerrainComponents.Reset();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	UMaterialInterface* RampMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean.MI_RuntimeRoad_AsphaltClean"));
	if (!IsValid(Cube))
	{
		UE_LOG(LogTemp, Error, TEXT("Elevated terrain: engine cube missing"));
		return;
	}

	auto AddTerrainComponent = [this, Cube](
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Size,
		UMaterialInterface* Material,
		const FName& TerrainRole)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->ComponentTags.AddUnique(TEXT("ElevatedFacilityTerrain"));
		Component->ComponentTags.AddUnique(TerrainRole);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(Cube);
		Component->SetRelativeLocation(Location);
		Component->SetRelativeRotation(Rotation);
		Component->SetRelativeScale3D(Size / 100.0f);
		if (IsValid(Material))
			Component->SetMaterial(0, Material);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(true);
		AddInstanceComponent(Component);
		Component->RegisterComponent();
		ElevatedTerrainComponents.Add(Component);
		return Component;
	};

	int32 RaisedFacilityCount = 0;
	int32 LoweredFacilityCount = 0;
	int32 RampCount = 0;
	int32 StairStepCount = 0;
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		const float FacilitySurfaceZ = Placement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : Placement.BaseElevationCm;
		if (FMath::IsNearlyEqual(FacilitySurfaceZ, BaseGroundSurfaceZ, 1.0f)
			|| Placement.OccupiedCells.IsEmpty())
			continue;

		TArray<TPair<FIntPoint, FIntPoint>> AccessEdges;
		GetFacilityAccessEdges(Placement, AccessEdges);
		if (AccessEdges.IsEmpty())
			continue;

		const float RampRun = Placement.ElevationProfile == EFacilityElevationProfile::WarZoneStronghold
			? 1800.0f : (Placement.ElevationProfile == EFacilityElevationProfile::RaisedCompound
				? 1400.0f : 1000.0f);
		const float RampWidth = Placement.ElevationProfile == EFacilityElevationProfile::WarZoneStronghold
			? 900.0f : 700.0f;

		for (const TPair<FIntPoint, FIntPoint>& AccessEdge : AccessEdges)
		{
			const FIntPoint& EntranceCell = AccessEdge.Key;
			const FIntPoint& EntranceDirection = AccessEdge.Value;
			const FVector2D Outward(
				static_cast<float>(EntranceDirection.X),
				static_cast<float>(EntranceDirection.Y));
			const FVector EdgePoint(
				EntranceCell.X * DesignCellSize + Outward.X * DesignCellSize * 0.5f,
				EntranceCell.Y * DesignCellSize + Outward.Y * DesignCellSize * 0.5f,
				0.0f);
			const FIntPoint OutsideCell = EntranceCell + EntranceDirection;
			const float OutsideSurfaceZ = GetSurfaceElevationForCell(OutsideCell);
			const FVector GroundPoint = EdgePoint + FVector(Outward.X, Outward.Y, 0.0f) * RampRun
				+ FVector(0.0f, 0.0f, OutsideSurfaceZ);
			const FVector TopPoint = EdgePoint + FVector(0.0f, 0.0f, FacilitySurfaceZ + 1.0f);
			const FVector RampDelta = TopPoint - GroundPoint;
			const FRotator RampRotation = RampDelta.Rotation();
			constexpr float RampThickness = 35.0f;
			// The slab is centred on the line from ground to pad, so its walkable face
			// used to stand half a thickness proud at both ends: an 18 cm lip to climb
			// before the ramp even began, and another 18 cm drop on to the pad at the
			// top. Sink it by that half thickness along its own up axis so the top face,
			// not the centre line, is what meets the ground and the pad.
			const FVector RampUp = RampRotation.RotateVector(FVector::UpVector);
			AddTerrainComponent(
				(GroundPoint + TopPoint) * 0.5f - RampUp * (RampThickness * 0.5f),
				RampRotation,
				FVector(RampDelta.Size(), RampWidth, RampThickness),
				RampMaterial,
				TEXT("VehicleRamp"));
			++RampCount;

			// A parallel infantry stair remains usable if the vehicle ramp is occupied.
			const FVector2D Perpendicular(-Outward.Y, Outward.X);
			const FVector StairSideOffset(Perpendicular.X * (RampWidth * 0.5f + 230.0f),
				Perpendicular.Y * (RampWidth * 0.5f + 230.0f), 0.0f);
			const int32 StepCount = FMath::Clamp(
				FMath::CeilToInt(FMath::Abs(TopPoint.Z - GroundPoint.Z) / 35.0f), 4, 12);
			const float StepRun = RampRun / StepCount;
			const FVector Inward(-Outward.X, -Outward.Y, 0.0f);
			const float StairSolidBottomZ = FMath::Min(-10.0f, FMath::Min(GroundPoint.Z, TopPoint.Z) - 50.0f);
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				const float StepTop = FMath::Lerp(
					GroundPoint.Z, TopPoint.Z, static_cast<float>(StepIndex + 1) / StepCount);
				const float StepHeight = FMath::Max(10.0f, StepTop - StairSolidBottomZ);
				const FVector StepCenter = GroundPoint + StairSideOffset
					+ Inward * (StepRun * (StepIndex + 0.5f))
					+ FVector(0.0f, 0.0f, (StairSolidBottomZ + StepTop) * 0.5f - GroundPoint.Z);
				AddTerrainComponent(
					StepCenter,
					FRotator(0.0f, Inward.Rotation().Yaw, 0.0f),
					FVector(StepRun + 4.0f, 360.0f, StepHeight),
					RampMaterial,
					TEXT("InfantryStair"));
				++StairStepCount;
			}
		}

		if (FacilitySurfaceZ > BaseGroundSurfaceZ)
			++RaisedFacilityCount;
		else
			++LoweredFacilityCount;
		UE_LOG(LogTemp, Display,
			TEXT("Elevated facility: id=%s footprint=%dx%d elevation_cm=%.0f entrance=(%d,%d) access=(%d,%d) direction=(%d,%d) access_points=%d"),
			*Placement.FacilityId.ToString(), Placement.Footprint.X, Placement.Footprint.Y,
			FacilitySurfaceZ, Placement.EntranceCell.X, Placement.EntranceCell.Y,
			Placement.AccessCell.X, Placement.AccessCell.Y,
			Placement.EntranceDirection.X, Placement.EntranceDirection.Y,
			AccessEdges.Num());
	}

	UE_LOG(LogTemp, Display,
		TEXT("Macro elevation terrain: raised_facilities=%d lowered_facilities=%d ramps=%d stair_steps=%d nav_components=%d"),
		RaisedFacilityCount, LoweredFacilityCount, RampCount, StairStepCount, ElevatedTerrainComponents.Num());
}

void AWarZoneFootprintPreview::SpawnRuntimeBlueprintTiles()
{
	if (!bUseRuntimeBlueprintTiles || !IsValid(GetWorld()))
		return;

	for (AActor* SpawnedTile : SpawnedRuntimeTiles)
	{
		if (IsValid(SpawnedTile))
			SpawnedTile->Destroy();
	}
	SpawnedRuntimeTiles.Reset();
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->DestroyComponent();
	}
	RuntimePackedVisualHISMs.Reset();
	BuildElevatedFacilityTerrain();

	// The reviewed LD_Tile maps are packed into reusable Blueprint classes by
	// PG.BuildPackedTileBlueprints. Spawning those classes keeps the authored
	// meshes/materials/collision while avoiding thousands of streamed UWorlds.
	static const TCHAR* CornerPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Corner.BPP_Tile_Road_Corner_C");
	static const TCHAR* StraightPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Straight.BPP_Tile_Road_Straight_C");
	static const TCHAR* TPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_TJunction.BPP_Tile_Road_TJunction_C");
	static const TCHAR* CrossPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Cross.BPP_Tile_Road_Cross_C");
	static const TCHAR* DeadEndPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_DeadEnd.BPP_Tile_Road_DeadEnd_C");
	static const TCHAR* SpawnPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Spawn_Staging.BPP_Tile_Spawn_Staging_C");
	static const TCHAR* ExitPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Exit_Checkpoint.BPP_Tile_Exit_Checkpoint_C");
	static const TCHAR* ObstaclePath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Obstacle_Checkpoint.BPP_Tile_Obstacle_Checkpoint_C");
	static const TCHAR* OpenPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_OpenGround.BPP_Tile_None_OpenGround_C");
	static const TCHAR* RuinsPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_Ruins.BPP_Tile_None_Ruins_C");
	static const TCHAR* YardPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Yard.BPP_Tile_WarZone_Yard_C");
	static const TCHAR* WarehousePath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Warehouse.BPP_Tile_WarZone_Warehouse_C");
	static const TCHAR* NatureMeadowPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Meadow_V2.BP_Tile_Nature_Meadow_V2_C");
	static const TCHAR* NatureForestSparsePath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestSparse.BP_Tile_Nature_ForestSparse_C");
	static const TCHAR* NatureForestDensePath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestDense.BP_Tile_Nature_ForestDense_C");
	static const TCHAR* NatureRockyPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Rocky.BP_Tile_Nature_Rocky_C");
	static const TCHAR* NatureScrubPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Scrub.BP_Tile_Nature_Scrub_C");
	static const TCHAR* NatureAmbushPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ambush.BP_Tile_Nature_Ambush_C");
	static const TCHAR* NatureServiceCampPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ServiceCamp.BP_Tile_Nature_ServiceCamp_C");
	static const TCHAR* NatureDitchPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ditch.BP_Tile_Nature_Ditch_C");
	static const TCHAR* WarZoneIndustrialOpenPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_IndustrialOpen.BP_Tile_WarZoneV2_IndustrialOpen_C");
	static const TCHAR* WarZoneContainerLanePath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_ContainerLane.BP_Tile_WarZoneV2_ContainerLane_C");
	static const TCHAR* WarZoneFactoryYardPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_FactoryYard.BP_Tile_WarZoneV2_FactoryYard_C");
	static const TCHAR* WarZoneUtilityYardPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_UtilityYard.BP_Tile_WarZoneV2_UtilityYard_C");

	FIntPoint WarZoneCoreCell = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			WarZoneCoreCell = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}

	TMap<FString, UClass*> ClassCache;
	auto ResolveClass = [&ClassCache](const TCHAR* Path)
	{
		const FString Key(Path);
		if (UClass** Existing = ClassCache.Find(Key))
			return *Existing;
		UClass* LoadedClass = LoadClass<AActor>(nullptr, Path);
		ClassCache.Add(Key, LoadedClass);
		return LoadedClass;
	};
	auto DisableCollisionOnHiddenPrimitives = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			// Packed tile Blueprints retain intentionally hidden alternate props.
			// Hidden meshes must never leave an invisible gameplay collision behind.
			if (IsValid(Component) && !Component->IsVisible())
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	};
	auto NormalizePackedBaseGround = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		static UMaterialInterface* UnifiedGround = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
		if (!IsValid(UnifiedGround))
			return;

		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			const UMaterialInterface* Material = IsValid(Component) ? Component->GetMaterial(0) : nullptr;
			if (!IsValid(Mesh) || !IsValid(Material)
				|| Mesh->GetFName() != TEXT("SM_Floor_2x2")
				|| !Material->GetPathName().Contains(TEXT("MI_RoadStraight_Ground")))
			{
				continue;
			}
			// Four 10x10 m packed slabs form the 20x20 m base. Only replace that
			// terrain material; road lanes, roofs and authored industrial floors stay.
			Component->SetMaterial(0, UnifiedGround);
		}
	};
	auto HidePerTileTerrainUnderlay = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return 0;

		int32 HiddenComponentCount = 0;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive))
				continue;

			const FName ComponentName = Primitive->GetFName();
			bool bIsTerrainUnderlay = ComponentName == TEXT("Ground_20m")
				|| ComponentName == TEXT("RoadPieces")
				|| ComponentName == TEXT("Road_6_5m")
				|| ComponentName.ToString().Contains(TEXT("RoadSurface"));
			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Primitive))
			{
				const UStaticMesh* Mesh = Instances->GetStaticMesh();
				const UMaterialInterface* Material = Instances->GetMaterial(0);
				const bool bPackedGroundMesh = IsValid(Mesh) && Mesh->GetFName() == TEXT("SM_Floor_2x2");
				const bool bGroundMaterial = IsValid(Material)
					&& (Material->GetPathName().Contains(TEXT("MI_RoadStraight_Ground"))
						|| Material->GetPathName().Contains(TEXT("MI_RuntimeGround_NatureUnified")));
				// Runtime tactical-tile actors use SM_Floor_2x2 only as their old base
				// terrain/road slab.  Facilities are handled separately and never enter
				// this lambda, so hiding every such slab is both safe and deterministic.
				bIsTerrainUnderlay |= bPackedGroundMesh || bGroundMaterial;
			}

			if (!bIsTerrainUnderlay)
				continue;

			Primitive->SetVisibility(false, true);
			Primitive->SetHiddenInGame(true);
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetCanEverAffectNavigation(false);
			++HiddenComponentCount;
		}
		return HiddenComponentCount;
	};
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> PackedComponentByKey;
	int32 PackedVisualInstanceCount = 0;
	auto PackActorVisuals = [this, &PackedComponentByKey, &PackedVisualInstanceCount](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		for (UStaticMeshComponent* Source : MeshComponents)
		{
			const UStaticMesh* Mesh = IsValid(Source) ? Source->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || !Source->IsVisible())
				continue;
			int32 StartCullDistance = 0;
			int32 EndCullDistance = 0;
			if (const UInstancedStaticMeshComponent* SourceInstances = Cast<UInstancedStaticMeshComponent>(Source))
				SourceInstances->GetCullDistances(StartCullDistance, EndCullDistance);
			// Authored Fab props are often plain StaticMeshComponents and therefore
			// arrive with infinite draw distance. Once packed into the runtime HISM,
			// apply foliage-specific culling while preserving the rare Pivot Painter
			// hero shrubs and their wind close to the player.
			const FString MeshPath = Mesh->GetPathName();
			const bool bIsFreeShrub = MeshPath.Contains(TEXT("/GV_FreeShrubsPack/"));
			const bool bIsRuntimeGrass = MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
				|| MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_"));
			const bool bIsSmallFoliage = bIsRuntimeGrass
				|| MeshPath.Contains(TEXT("/Meshes/Foliage/Bush_"))
				|| MeshPath.Contains(TEXT("/Meshes/Foliage/Shrubs_"))
				|| bIsFreeShrub;
			static UMaterialInterface* RuntimeHeroShrubMaterial = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeHeroShrub_Dark.MI_RuntimeHeroShrub_Dark"));
			auto ResolvePackedMaterial = [bIsFreeShrub](UMaterialInterface* SourceMaterial)
			{
				if (!bIsFreeShrub || !IsValid(SourceMaterial) || !IsValid(RuntimeHeroShrubMaterial))
					return SourceMaterial;
				// Preserve bark while replacing the neon, high-amplitude Pivot Painter
				// leaf material with the local dark, nearly-static gameplay variant.
				return SourceMaterial->GetPathName().Contains(TEXT("Leaf"))
					? RuntimeHeroShrubMaterial
					: SourceMaterial;
			};
			const bool bPackedEvaluateWorldPositionOffset = Source->bEvaluateWorldPositionOffset && !bIsFreeShrub;
			if (bIsRuntimeGrass)
			{
				// Dense grass remains unchanged at player distance; only far-away cells
				// stop drawing earlier. This keeps the Tarkov-like silhouette without
				// submitting tens of thousands of invisible grass cards.
				StartCullDistance = 1800;
				EndCullDistance = 6000;
			}
			else if (MeshPath.Contains(TEXT("/Meshes/Foliage/Bush_"))
				|| MeshPath.Contains(TEXT("/Meshes/Foliage/Shrubs_")))
			{
				StartCullDistance = 2500;
				EndCullDistance = 8000;
			}
			else if (MeshPath.Contains(TEXT("/Meshes/Foliage/SM_Pine_Tree_")))
			{
				StartCullDistance = 10000;
				EndCullDistance = 22000;
			}
			else if (MeshPath.Contains(TEXT("/Assets/props/prop_rocks/")))
			{
				StartCullDistance = 8000;
				EndCullDistance = 18000;
			}
			else if (EndCullDistance <= 0)
			{
				if (MeshPath.Contains(TEXT("/GV_FreeShrubsPack/")))
				{
					StartCullDistance = 4500;
					EndCullDistance = 12000;
				}
				else if (MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
					|| MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_")))
				{
					StartCullDistance = 2500;
					EndCullDistance = 8000;
				}
			}

			FString Key = FString::Printf(TEXT("%s|C%d|P%s|S%d|W%d|D%d-%d"),
				*Mesh->GetPathName(),
				static_cast<int32>(Source->GetCollisionEnabled()),
				*Source->GetCollisionProfileName().ToString(),
				Source->CastShadow ? 1 : 0,
				bPackedEvaluateWorldPositionOffset ? 1 : 0,
				StartCullDistance,
				EndCullDistance);
			for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
			{
				const UMaterialInterface* Material = ResolvePackedMaterial(Source->GetMaterial(MaterialIndex));
				Key += TEXT("|M") + (IsValid(Material) ? Material->GetPathName() : TEXT("None"));
			}

			UHierarchicalInstancedStaticMeshComponent*& Target = PackedComponentByKey.FindOrAdd(Key);
			if (!IsValid(Target))
			{
				Target = NewObject<UHierarchicalInstancedStaticMeshComponent>(
					this,
					*FString::Printf(TEXT("RuntimePackedVisual_%d"), RuntimePackedVisualHISMs.Num()));
				Target->SetupAttachment(SceneRoot);
				Target->SetMobility(EComponentMobility::Movable);
				Target->SetStaticMesh(const_cast<UStaticMesh*>(Mesh));
				Target->SetCollisionProfileName(Source->GetCollisionProfileName());
				Target->SetCollisionEnabled(Source->GetCollisionEnabled());
				Target->SetGenerateOverlapEvents(false);
				Target->SetCanEverAffectNavigation(false);
				Target->SetCastShadow(Source->CastShadow && !bIsSmallFoliage);
				Target->SetEvaluateWorldPositionOffset(bPackedEvaluateWorldPositionOffset);
				Target->SetCullDistances(StartCullDistance, EndCullDistance);
				for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
					Target->SetMaterial(MaterialIndex, ResolvePackedMaterial(Source->GetMaterial(MaterialIndex)));
				Target->RegisterComponent();
				RuntimePackedVisualHISMs.Add(Target);
			}

			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Source))
			{
				for (int32 InstanceIndex = 0; InstanceIndex < Instances->GetInstanceCount(); ++InstanceIndex)
				{
					FTransform WorldTransform;
					if (Instances->GetInstanceTransform(InstanceIndex, WorldTransform, true))
					{
						Target->AddInstance(WorldTransform, true);
						++PackedVisualInstanceCount;
					}
				}
			}
			else
			{
				Target->AddInstance(Source->GetComponentTransform(), true);
				++PackedVisualInstanceCount;
			}
		}
	};
	auto LiftCoplanarPackedRoadSurfaces = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || Mesh->GetFName() != TEXT("SM_Floor_2x2"))
				continue;
			for (int32 InstanceIndex = 0; InstanceIndex < Component->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, false)
					|| InstanceTransform.GetScale3D().Z > 0.1f)
					continue;
				// Packed road slabs started almost coplanar with the common 20 cm
				// terrain surface. Keep their centre at least 25 cm high: this leaves
				// a small, stable render separation without creating a gameplay step.
				FVector Translation = InstanceTransform.GetTranslation();
				Translation.Z = FMath::Max(Translation.Z, 25.0f);
				InstanceTransform.SetTranslation(Translation);
				Component->UpdateInstanceTransform(InstanceIndex, InstanceTransform, false, true, true);
			}
		}
	};
	auto LiftPackedExitMarking = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || Mesh->GetPathName() != TEXT("/Engine/BasicShapes/Plane.Plane"))
				continue;
			for (int32 InstanceIndex = 0; InstanceIndex < Component->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, false))
					continue;
				// The EXIT stencil is authored 2 cm above the terrain datum, which is
				// where the road surface used to sit, and a flat 8 cm lift was enough
				// to clear it. Raising the road to RoadSurfaceLiftCm put the stencil
				// back exactly on the asphalt - the audit caught 16 m2 of it at a
				// 0.00 cm gap. Derive the lift from the road height instead of a
				// constant so the two cannot drift apart again.
				constexpr float AuthoredMarkingHeightCm = 2.0f;
				constexpr float MarkingClearanceCm = 8.0f;
				const float MarkingLiftCm =
					RoadSurfaceLiftCm + MarkingClearanceCm - AuthoredMarkingHeightCm;
				InstanceTransform.AddToTranslation(FVector(0.0f, 0.0f, MarkingLiftCm));
				Component->UpdateInstanceTransform(InstanceIndex, InstanceTransform, false, true, true);
			}
		}
	};

	int32 SpawnFailures = 0;
	int32 PackedTileActorCount = 0;
	int32 WaterTileSkipCount = 0;
	TSet<FIntPoint> ShoreCellsForTileSkip;
	{
		TSet<FIntPoint> LakeCellsForTileSkip;
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
			if (Placement.Visual == ETileDesignVisual::Water)
				LakeCellsForTileSkip.Add(Placement.GridCell);
		if (LakeCellsForTileSkip.Num() > 0)
		{
			TMap<FIntPoint, TPair<int32, int32>> ShoreTiles;
			BuildShoreTransitionMap(LakeCellsForTileSkip, ShoreTiles);
			for (const TPair<FIntPoint, TPair<int32, int32>>& Entry : ShoreTiles)
				ShoreCellsForTileSkip.Add(Entry.Key);
		}
	}
	int32 HiddenTerrainUnderlayCount = 0;
	TMap<ETacticalTileKind, int32> KindCounts;
	TMap<FName, int32> WarZoneVariantCounts;
	const AGameModePG* RuntimeGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 RuntimeRaidSeed = IsValid(RuntimeGameMode) ? RuntimeGameMode->GetMapGenerationSeed() : 0;
	auto FloorDivide = [](const int32 Value, const int32 Divisor)
	{
		const int32 Quotient = Value / Divisor;
		const int32 Remainder = Value % Divisor;
		return Remainder < 0 ? Quotient - 1 : Quotient;
	};
	auto MakeWarZoneClusterHash = [RuntimeRaidSeed, &FloorDivide](
		const int32 DeltaX,
		const int32 DeltaY,
		const int32 ClusterSize,
		const uint32 BandSalt)
	{
		// Offset by half a cluster so the central 3x3 facility and its immediate
		// apron belong to one coherent patch instead of straddling four quadrants.
		const int32 ClusterX = FloorDivide(DeltaX + ClusterSize / 2, ClusterSize);
		const int32 ClusterY = FloorDivide(DeltaY + ClusterSize / 2, ClusterSize);
		return HashCombine(
			GetTypeHash(RuntimeRaidSeed),
			HashCombine(
				GetTypeHash(ClusterX),
				HashCombine(GetTypeHash(ClusterY), BandSalt)));
	};
	auto GetWarZoneClusterSlot = [&FloorDivide](
		const int32 DeltaX,
		const int32 DeltaY,
		const int32 ClusterSize)
	{
		const int32 OffsetX = DeltaX + ClusterSize / 2;
		const int32 OffsetY = DeltaY + ClusterSize / 2;
		const int32 LocalX = OffsetX - FloorDivide(OffsetX, ClusterSize) * ClusterSize;
		const int32 LocalY = OffsetY - FloorDivide(OffsetY, ClusterSize) * ClusterSize;
		return LocalY * ClusterSize + LocalX;
	};
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		const uint32 StableHash = Placement.LocalSeed;
		// Lake cells get no tile actor at all. Their ground is 2.8 m under the water
		// surface, so a spawned tile would put its cars, containers and walls in open
		// water - which is exactly how the first pass looked.
		//
		// Shore cells are skipped for the same reason: every tile places its props
		// against a flat Z=20 surface, but a shore cell's ground is the generated
		// beach mesh sloping away underneath. The first pass left pine trees and
		// boulders standing in mid-air over the slope. The beach is meant to be open
		// anyway - the shore rock and reed scatter dresses it.
		if (Placement.Visual == ETileDesignVisual::Water
			|| ShoreCellsForTileSkip.Contains(Placement.GridCell))
		{
			++WaterTileSkipCount;
			continue;
		}

		ETacticalTileKind Kind = ETacticalTileKind::OpenGround;
		const TCHAR* ClassPath = OpenPath;
		FName WarZoneBandTag = NAME_None;
		FName WarZoneVisualTag = NAME_None;
		int32 WarZoneRotationQuarterTurns = INDEX_NONE;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::RoadStraight: Kind = ETacticalTileKind::RoadStraight; ClassPath = StraightPath; break;
		case ETileDesignVisual::RoadCorner: Kind = ETacticalTileKind::RoadCorner; ClassPath = CornerPath; break;
		case ETileDesignVisual::RoadTJunction: Kind = ETacticalTileKind::RoadTJunction; ClassPath = TPath; break;
		case ETileDesignVisual::RoadCross: Kind = ETacticalTileKind::RoadCross; ClassPath = CrossPath; break;
		case ETileDesignVisual::RoadDeadEnd: Kind = ETacticalTileKind::RoadDeadEnd; ClassPath = DeadEndPath; break;
		case ETileDesignVisual::Spawn: Kind = ETacticalTileKind::SpawnStaging; ClassPath = SpawnPath; break;
		case ETileDesignVisual::Exit: Kind = ETacticalTileKind::ExitCheckpoint; ClassPath = ExitPath; break;
		case ETileDesignVisual::Obstacle: Kind = ETacticalTileKind::ObstacleCheckpoint; ClassPath = ObstaclePath; break;
		case ETileDesignVisual::Ruins: Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; break;
		case ETileDesignVisual::NatureMeadow: Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; break;
		case ETileDesignVisual::NatureForestSparse: Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; break;
		case ETileDesignVisual::NatureForestDense: Kind = ETacticalTileKind::NatureForestDense; ClassPath = NatureForestDensePath; break;
		case ETileDesignVisual::NatureRocky: Kind = ETacticalTileKind::NatureRocky; ClassPath = NatureRockyPath; break;
		case ETileDesignVisual::NatureScrub: Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; break;
		case ETileDesignVisual::NatureAmbush: Kind = ETacticalTileKind::NatureAmbush; ClassPath = NatureAmbushPath; break;
		case ETileDesignVisual::NatureServiceCamp: Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; break;
		case ETileDesignVisual::NatureDitch: Kind = ETacticalTileKind::NatureDitch; ClassPath = NatureDitchPath; break;
		case ETileDesignVisual::WarZoneGround:
		{
			// WarZone is one large logical region, not the single 3x3 anchor building.
			// Convert it into deterministic concentric combat bands: an unmistakably
			// industrial core, a mixed firefight belt and a natural outer buffer.
			const int32 DeltaX = Placement.GridCell.X - WarZoneCoreCell.X;
			const int32 DeltaY = Placement.GridCell.Y - WarZoneCoreCell.Y;
			const int32 DistanceSquared = DeltaX * DeltaX + DeltaY * DeltaY;
			if (DistanceSquared <= 64)
			{
				WarZoneBandTag = TEXT("WarZone_Core");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 3, 0xA341316Cu);
				const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0x51ED270Bu);
				const int32 ClusterRoll = ClusterHash % 100;
				const int32 DetailRoll = DetailHash % 100;
				const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 3);
				const int32 FeatureSlotA = static_cast<int32>((ClusterHash >> 16) % 9u);
				const int32 FeatureSlotB = (FeatureSlotA + 2 + static_cast<int32>((ClusterHash >> 21) % 5u)) % 9;
				const int32 FeatureSlotC = (FeatureSlotA + 5 + static_cast<int32>((ClusterHash >> 25) % 4u)) % 9;
				const bool bFeatureCell = LocalSlot == FeatureSlotA
					|| LocalSlot == FeatureSlotB
					|| LocalSlot == FeatureSlotC;
				WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
				if (bFeatureCell)
				{
					const int32 FeatureRoll = (ClusterRoll + LocalSlot * 17) % 100;
					if (FeatureRoll < 24) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneContainerLanePath; WarZoneVisualTag = TEXT("WZ_ContainerLane"); }
					else if (FeatureRoll < 47) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarZoneFactoryYardPath; WarZoneVisualTag = TEXT("WZ_FactoryYard"); }
					else if (FeatureRoll < 68) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneUtilityYardPath; WarZoneVisualTag = TEXT("WZ_UtilityYard"); }
					else if (FeatureRoll < 86) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
					else if (FeatureRoll < 96) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarehousePath; WarZoneVisualTag = TEXT("WZ_AuthoredWarehouse"); }
					else { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				}
				// The combat core must read as a broad industrial yard, not a maze of
				// one-cell wall fragments.  Feature cells still provide containers,
				// tanks and authored yards; only a small minority become ruins.
				else if (DetailRoll < 88) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
				else { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
			}
			else if (DistanceSquared <= 225)
			{
				WarZoneBandTag = TEXT("WarZone_Mid");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 4, 0xC8013EA4u);
				const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0x68E31DA4u);
				const int32 ClusterRoll = ClusterHash % 100;
				const int32 DetailRoll = DetailHash % 100;
				const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 4);
				const int32 FeatureSlotA = static_cast<int32>((ClusterHash >> 16) % 16u);
				const int32 FeatureSlotB = (FeatureSlotA + 5 + static_cast<int32>((ClusterHash >> 22) % 7u)) % 16;
				WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
				if (LocalSlot == FeatureSlotA || LocalSlot == FeatureSlotB)
				{
					if (ClusterRoll < 30) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneContainerLanePath; WarZoneVisualTag = TEXT("WZ_ContainerLane"); }
					else if (ClusterRoll < 50) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarZoneFactoryYardPath; WarZoneVisualTag = TEXT("WZ_FactoryYard"); }
					else if (ClusterRoll < 70) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneUtilityYardPath; WarZoneVisualTag = TEXT("WZ_UtilityYard"); }
					else if (ClusterRoll < 85) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
					else if (ClusterRoll < 93) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarehousePath; WarZoneVisualTag = TEXT("WZ_AuthoredWarehouse"); }
					else { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				}
				else if (DetailRoll < 26) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
				else if (DetailRoll < 34) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				else if (DetailRoll < 46) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				else if (DetailRoll < 70) { Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; WarZoneVisualTag = TEXT("WZ_Scrub"); }
				else if (DetailRoll < 91) { Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; WarZoneVisualTag = TEXT("WZ_Meadow"); }
				else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
			}
			else
			{
				WarZoneBandTag = TEXT("WarZone_Outer");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 5, 0xAD90777Du);
				const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0xB5297A4Du);
				const int32 ClusterRoll = ClusterHash % 100;
				const int32 DetailRoll = DetailHash % 100;
				const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 5);
				const int32 FeatureSlot = static_cast<int32>((ClusterHash >> 16) % 25u);
				const int32 BlendedRoll = (ClusterRoll * 3 + DetailRoll) / 4;
				WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
				if (LocalSlot == FeatureSlot)
				{
					if (ClusterRoll < 25) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
					else if (ClusterRoll < 50) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
					else if (ClusterRoll < 62) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
					else if (ClusterRoll < 75) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
					else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
				}
				else if (BlendedRoll < 14) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				else if (BlendedRoll < 27) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				else if (BlendedRoll < 52) { Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; WarZoneVisualTag = TEXT("WZ_Scrub"); }
				else if (BlendedRoll < 79) { Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; WarZoneVisualTag = TEXT("WZ_Meadow"); }
				else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
			}
			break;
		}
		default: break;
		}

		// Long POI access roads deliberately use the C++ tactical road builder.
		// The packed LD_Tile roads remain on generator-authored road cells and at
		// landmarks, while access corridors get clean shoulders plus sparse cover.
		// This prevents walls, barrels and cars from repeating every single cell.
		UClass* TileClass = Placement.bSupplementalAccessRoad
			? ATacticalTileActor::StaticClass()
			: ResolveClass(ClassPath);
		if (!IsValid(TileClass)) { ++SpawnFailures; continue; }
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const bool bSocketOrientedTile =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle;
		const int32 RotationQuarterTurns = bSocketOrientedTile
			? Placement.RotationQuarterTurns
			: (WarZoneRotationQuarterTurns != INDEX_NONE
				? WarZoneRotationQuarterTurns
				: Placement.LayoutVariant);
		const int32 SpawnRotationQuarterTurns = Placement.bSupplementalAccessRoad
			? 0
			: RotationQuarterTurns;
		AActor* Tile = GetWorld()->SpawnActor<AActor>(
			TileClass,
			Placement.WorldLocation,
			FRotator(0.0f, SpawnRotationQuarterTurns * 90.0f, 0.0f),
			Parameters);
		if (!IsValid(Tile)) { ++SpawnFailures; continue; }
		if (ATacticalTileActor* TacticalTile = Cast<ATacticalTileActor>(Tile))
		{
			// Runtime placement is authoritative. Blueprint defaults are only an
			// editor preview and must not override the server/seed-selected tile kind.
			TacticalTile->TileKind = Kind;
			if (!WarZoneVisualTag.IsNone())
			{
				TacticalTile->bShowDynamicProps = false;
				const bool bNatureWarZoneTile = Kind >= ETacticalTileKind::NatureMeadow
					&& Kind <= ETacticalTileKind::NatureDitch;
				if (bNatureWarZoneTile)
				{
					TacticalTile->DressingDensityScale = WarZoneBandTag == TEXT("WarZone_Core") ? 0.55f
						: (WarZoneBandTag == TEXT("WarZone_Mid") ? 0.85f : 1.05f);
				}
				else if (Kind == ETacticalTileKind::WarZoneYard
					|| Kind == ETacticalTileKind::WarZoneWarehouse)
				{
					// Authored industrial cells still need an overgrown shoulder. The old
					// zero multiplier silently removed all grass added by their layouts.
					TacticalTile->DressingDensityScale = 0.70f;
				}
				else if (Kind == ETacticalTileKind::OpenGround
					|| Kind == ETacticalTileKind::Ruins)
				{
					TacticalTile->DressingDensityScale = WarZoneBandTag == TEXT("WarZone_Core") ? 0.50f : 0.75f;
				}
				else
				{
					TacticalTile->DressingDensityScale = 0.20f;
				}
			}
			if (Placement.bSupplementalAccessRoad)
			{
				TacticalTile->bShowDynamicProps = false;
				TacticalTile->bIsAccessRoad = true;
				TacticalTile->DressingDensityScale = 0.35f;
			}
			// Rebuild needs the combat-band tags to choose its ground treatment.
			if (!WarZoneBandTag.IsNone()) TacticalTile->Tags.AddUnique(WarZoneBandTag);
			if (!WarZoneVisualTag.IsNone()) TacticalTile->Tags.AddUnique(WarZoneVisualTag);
			TacticalTile->RebuildFromRuntimeSpec(
				static_cast<int32>(StableHash),
				Placement.ConnectionMask,
				Placement.LayoutVariant);
		}
		NormalizePackedBaseGround(Tile);
		HiddenTerrainUnderlayCount += HidePerTileTerrainUnderlay(Tile);
		DisableCollisionOnHiddenPrimitives(Tile);
		// All authored road/ground underlays are hidden above.  The one shared road
		// HISM is now the only rendered surface, eliminating both the leaf-pattern
		// material and coplanar flicker.
		if (Placement.Visual == ETileDesignVisual::Exit)
			LiftPackedExitMarking(Tile);
		Tile->Tags.AddUnique(TEXT("RuntimeTacticalTile"));
		Tile->Tags.AddUnique(FName(*StaticEnum<ETacticalTileKind>()->GetNameStringByValue(
			static_cast<int64>(Kind))));
		if (!WarZoneBandTag.IsNone())
			Tile->Tags.AddUnique(WarZoneBandTag);
		if (!WarZoneVisualTag.IsNone())
		{
			Tile->Tags.AddUnique(WarZoneVisualTag);
			WarZoneVariantCounts.FindOrAdd(WarZoneVisualTag)++;
		}
		#if WITH_EDITOR
		Tile->SetActorLabel(FString::Printf(TEXT("RuntimeTile_%d_%d"), Placement.GridCell.X, Placement.GridCell.Y));
		Tile->SetFolderPath(TEXT("RuntimeTacticalTiles"));
		#endif
		// The tile actor is only a deterministic construction template. Consolidate
		// its visible static meshes into shared HISM batches, then discard the actor;
		// the authoritative placement manifest and gameplay points remain unchanged.
		PackActorVisuals(Tile);
		Tile->Destroy();
		++PackedTileActorCount;
		KindCounts.FindOrAdd(Kind)++;
	}
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->BuildTreeIfOutdated(false, true);
	}

	int32 SpawnedFacilityCount = 0;
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		// An authored level supplies the whole facility. Spawning the procedural
		// builder as well would stack a second building inside the first.
		if (FacilityUsesAuthoredLevel(Placement.VisualSet, Placement.Footprint))
			continue;

		EProceduralFacilityKind FacilityKind = EProceduralFacilityKind::SatelliteCamp2x2;
		if (Placement.VisualSet == EFacilityVisualSet::Warehouse)
			FacilityKind = EProceduralFacilityKind::IndustrialRaid3x3;
		else if (Placement.VisualSet == EFacilityVisualSet::LongBarracks)
			FacilityKind = EProceduralFacilityKind::LongBarracks2x1;
		else if (Placement.VisualSet == EFacilityVisualSet::LinearTrench)
			FacilityKind = EProceduralFacilityKind::LinearTrench4x1;
		else if (Placement.VisualSet == EFacilityVisualSet::DowntownBlock)
			FacilityKind = EProceduralFacilityKind::DowntownBlock3x3;
		else if (Placement.VisualSet == EFacilityVisualSet::FactoryConstruction)
			FacilityKind = EProceduralFacilityKind::FactoryConstruction2x2;
		else if (Placement.VisualSet == EFacilityVisualSet::RuralHideout)
			FacilityKind = EProceduralFacilityKind::RuralHideout2x2;

		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector FacilityLocation = GetDesignFootprintCenter(Placement);
		const FRotator FacilityRotation(0.0f, Placement.RotationQuarterTurns * 90.0f, 0.0f);

		// An authored Blueprint, when one exists for this facility, is preferred over
		// the code-built version: its walls and props are individual components a
		// designer can select and drag in the editor, which the HISM instances
		// AProceduralFacilityActor emits can never be. The procedural builder stays
		// as the fallback for every kind that has not been authored yet, so the two
		// can coexist while the library is filled in one facility at a time.
		UClass* AuthoredFacilityClass = nullptr;
		if (bUseAuthoredFacilityBlueprints)
		{
			if (const TCHAR* AuthoredPath = GetAuthoredFacilityBlueprintPath(Placement.VisualSet))
				AuthoredFacilityClass = ResolveClass(AuthoredPath);
		}

		AActor* Facility = nullptr;
		if (IsValid(AuthoredFacilityClass))
		{
			Facility = GetWorld()->SpawnActor<AActor>(
				AuthoredFacilityClass, FacilityLocation, FacilityRotation, Parameters);
			if (IsValid(Facility))
			{
				// The authored Blueprint derives from ATacticalTileActor, so its
				// inherited 20 m ground slab and nature dressing would be layered
				// under a 40-60 m footprint. The shared terrain pad already covers
				// this cell range; hide the inherited underlay exactly as the packed
				// tiles do.
				HiddenTerrainUnderlayCount += HidePerTileTerrainUnderlay(Facility);
			}
		}
		if (!IsValid(Facility))
		{
			AProceduralFacilityActor* ProceduralFacility = GetWorld()->SpawnActor<AProceduralFacilityActor>(
				AProceduralFacilityActor::StaticClass(),
				FacilityLocation,
				FacilityRotation,
				Parameters);
			if (IsValid(ProceduralFacility))
			{
				// Rotation controls how the footprint connects to the generated road
				// graph; the facility's deterministic interior variant is derived
				// independently from the server-authored local seed.
				const uint8 FacilityLayoutVariant = static_cast<uint8>(Placement.LocalSeed) & 3;
				ProceduralFacility->Configure(FacilityKind, Placement.LocalSeed, FacilityLayoutVariant);
			}
			Facility = ProceduralFacility;
		}
		if (!IsValid(Facility))
		{
			++SpawnFailures;
			continue;
		}
		DisableCollisionOnHiddenPrimitives(Facility);
		Facility->Tags.AddUnique(TEXT("RuntimeTacticalFacility"));
		Facility->Tags.AddUnique(FName(*StaticEnum<EProceduralFacilityKind>()->GetNameStringByValue(
			static_cast<int64>(FacilityKind))));
		#if WITH_EDITOR
		Facility->SetActorLabel(FString::Printf(TEXT("RuntimeFacility_%s_%d_%d%s"),
			*Placement.FacilityId.ToString(), Placement.AnchorCell.X, Placement.AnchorCell.Y,
			IsValid(AuthoredFacilityClass) ? TEXT("_Authored") : TEXT("")));
		Facility->SetFolderPath(TEXT("RuntimeTacticalFacilities"));
		#endif
		UE_LOG(LogTemp, Display,
			TEXT("Facility visual source: id=%s source=%s class=%s at=(%.0f,%.0f,%.0f) rotation=%.0f"),
			*Placement.FacilityId.ToString(),
			IsValid(AuthoredFacilityClass) ? TEXT("authored_blueprint") : TEXT("procedural_code"),
			*Facility->GetClass()->GetName(),
			FacilityLocation.X, FacilityLocation.Y, FacilityLocation.Z,
			FacilityRotation.Yaw);
		SpawnedRuntimeTiles.Add(Facility);
		++SpawnedFacilityCount;
	}

	// Render a single unified ground layer for the whole generated footprint.
	// Per-tile terrain slabs are hidden above, so there are no coplanar surfaces
	// and no material discontinuity at the 20 m cell boundary.
	GroundHISM->SetVisibility(true, true);
	GroundHISM->SetHiddenInGame(false);
	WarZoneGroundHISM->SetVisibility(true, true);
	WarZoneGroundHISM->SetHiddenInGame(false);
	TransitionGroundHISM->SetVisibility(true, true);
	TransitionGroundHISM->SetHiddenInGame(false);
	RoadSurfaceHISM->SetVisibility(true, true);
	RoadSurfaceHISM->SetHiddenInGame(false);
	GroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WarZoneGroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TransitionGroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RoadSurfaceHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogTemp, Display, TEXT("Runtime packed tile world: requested=%d packed_tile_actors=%d water_cells_skipped=%d packed_hism_components=%d packed_visual_instances=%d persistent_facilities=%d failures=%d shared_ground_visible=true shared_road_visible=true hidden_tile_ground_components=%d"),
		TileDesignPlacements.Num(), PackedTileActorCount, WaterTileSkipCount,
		RuntimePackedVisualHISMs.Num(), PackedVisualInstanceCount,
		SpawnedFacilityCount, SpawnFailures, HiddenTerrainUnderlayCount);
	for (const TPair<ETacticalTileKind, int32>& Pair : KindCounts)
		UE_LOG(LogTemp, Display, TEXT("Runtime Blueprint tile count: kind=%s count=%d"),
			*StaticEnum<ETacticalTileKind>()->GetNameStringByValue(static_cast<int64>(Pair.Key)), Pair.Value);
	for (const TPair<FName, int32>& Pair : WarZoneVariantCounts)
		UE_LOG(LogTemp, Display, TEXT("WarZone visual count: variant=%s count=%d"),
			*Pair.Key.ToString(), Pair.Value);

	RefreshNavigationBlockerRegion(CenterNavigationBlockers, GetActorLocation(), 6000.0f);
}

void AWarZoneFootprintPreview::RefreshNavigationBlockerRegion(
	UTacticalTileNavModifierComponent* Modifier,
	const FVector& WorldCenter,
	float RadiusCm)
{
	if (!IsValid(Modifier))
		return;

	TArray<FBox> WorldBlockers;
	const float RadiusSquared = FMath::Square(RadiusCm);
	for (AActor* TileActor : SpawnedRuntimeTiles)
	{
		if (!IsValid(TileActor)
			|| FVector::DistSquared2D(TileActor->GetActorLocation(), WorldCenter) > RadiusSquared)
		{
			continue;
		}

		TArray<AActor*> GeometryActors;
		GeometryActors.Add(TileActor);
		TArray<AActor*> AttachedActors;
		TileActor->GetAttachedActors(AttachedActors, true, true);
		GeometryActors.Append(AttachedActors);
		for (const AActor* GeometryActor : GeometryActors)
		{
			if (!IsValid(GeometryActor))
				continue;
			TArray<UPrimitiveComponent*> PrimitiveComponents;
			GeometryActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (const UPrimitiveComponent* Primitive : PrimitiveComponents)
			{
				if (!IsValid(Primitive)
					|| Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				{
					continue;
				}
				const FString ComponentName = Primitive->GetName();
				if (ComponentName.Contains(TEXT("Ground"))
					|| ComponentName.Contains(TEXT("Road"))
					|| ComponentName.Contains(TEXT("Roof"))
					|| ComponentName.Contains(TEXT("Grass"))
					|| ComponentName.Contains(TEXT("Marking")))
				{
					continue;
				}

				auto AddBlocker = [&WorldBlockers](const FBox& Box)
				{
					if (Box.GetExtent().Z >= 45.0f && Box.GetSize().X * Box.GetSize().Y >= 900.0f)
						WorldBlockers.Add(Box.ExpandBy(FVector(20.0f, 20.0f, 0.0f)));
				};
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Primitive))
				{
					if (!IsValid(ISM->GetStaticMesh()))
						continue;
					const FBox MeshBox = ISM->GetStaticMesh()->GetBoundingBox();
					for (int32 InstanceIndex = 0; InstanceIndex < ISM->GetInstanceCount(); ++InstanceIndex)
					{
						FTransform WorldTransform;
						if (ISM->GetInstanceTransform(InstanceIndex, WorldTransform, true))
							AddBlocker(MeshBox.TransformBy(WorldTransform));
					}
				}
				else
				{
					AddBlocker(Primitive->Bounds.GetBox());
				}
			}
		}
	}

	Modifier->SetWorldBlockers(WorldBlockers);
	UE_LOG(LogTemp, Display,
		TEXT("Local navigation blockers: center=(%.0f,%.0f) radius=%.0f boxes=%d"),
		WorldCenter.X, WorldCenter.Y, RadiusCm, WorldBlockers.Num());
}

void AWarZoneFootprintPreview::LoadFacilityDesignLevel(
	const FFacilityPlacement& Placement,
	int32 PlacementIndex)
{
	if (Placement.FacilityLevel.IsNull() || PlacementIndex < 0)
		return;

	while (FacilityDesignLevelInstances.Num() <= PlacementIndex)
		FacilityDesignLevelInstances.Add(nullptr);
	while (FacilityLoadRequestTimeSeconds.Num() <= PlacementIndex)
		FacilityLoadRequestTimeSeconds.Add(0.0);

	const FVector Location = GetDesignFootprintCenter(Placement);
	const FRotator Rotation(0.0f, Placement.RotationQuarterTurns * 90.0f, 0.0f);
	bool bRequested = false;
	FacilityLoadRequestTimeSeconds[PlacementIndex] = FPlatformTime::Seconds();
	const FString OptionalName = FString::Printf(TEXT("Facility_%02d_Type_%d"),
		PlacementIndex, static_cast<int32>(Placement.VisualSet));
	FacilityDesignLevelInstances[PlacementIndex] = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
		this,
		Placement.FacilityLevel,
		Location,
		Rotation,
		bRequested,
		OptionalName);
	if (IsValid(FacilityDesignLevelInstances[PlacementIndex]))
	{
		FacilityDesignLevelInstances[PlacementIndex]->SetShouldBeLoaded(true);
		FacilityDesignLevelInstances[PlacementIndex]->SetShouldBeVisible(true);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Facility design level instance: index=%d type=%d %s at=(%.0f,%.0f,%.0f) rotation=%.0f"),
		PlacementIndex,
		static_cast<int32>(Placement.VisualSet),
		bRequested ? TEXT("requested") : TEXT("failed"),
		Location.X, Location.Y, Location.Z, Rotation.Yaw);
}

void AWarZoneFootprintPreview::BuildBorderMountains()
{
	if (!IsValid(MountainHISM) || MountainHISM->GetStaticMesh() == nullptr)
		return;
	MountainHISM->ClearInstances();
	if (!bBuildBorderMountains || TileDesignPlacements.IsEmpty())
		return;

	FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint MaxCell(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		MinCell = FIntPoint(
			FMath::Min(MinCell.X, Placement.GridCell.X), FMath::Min(MinCell.Y, Placement.GridCell.Y));
		MaxCell = FIntPoint(
			FMath::Max(MaxCell.X, Placement.GridCell.X), FMath::Max(MaxCell.Y, Placement.GridCell.Y));
	}
	const FVector2D GridCentre(
		(MinCell.X + MaxCell.X) * 0.5f * DesignCellSize,
		(MinCell.Y + MaxCell.Y) * 0.5f * DesignCellSize);
	const float GridHalfSpanCm = FMath::Max(
		(MaxCell.X - MinCell.X + 1) * DesignCellSize,
		(MaxCell.Y - MinCell.Y + 1) * DesignCellSize) * 0.5f;

	// Two staggered rings of the pack's background-mountain mesh. The numbers
	// follow how Downtown_West's own demo dressed its horizon: instances 550 m+
	// from centre, scales 0.4-1.5, sunk 15-34 m so only ridgelines rise over the
	// valley floor. The inner ring carries the silhouette; the sparser, larger
	// outer ring gives the range depth so it does not read as a fence of hills.
	const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 RaidSeed = IsValid(GameMode) ? GameMode->GetMapGenerationSeed() : 0;
	FRandomStream MountainStream(static_cast<int32>(GetTypeHash(RaidSeed) ^ 0x304Au));
	TArray<FTransform> MountainTransforms;
	const struct { int32 Count; float Radius; float ScaleMin; float ScaleMax; float BaseZ; } Rings[] = {
		{ 14, GridHalfSpanCm + 24000.0f, 0.9f, 1.3f, -2000.0f },
		{  8, GridHalfSpanCm + 46000.0f, 1.3f, 1.7f, -2600.0f },
	};
	for (const auto& Ring : Rings)
	{
		for (int32 Index = 0; Index < Ring.Count; ++Index)
		{
			const float Angle = (2.0f * PI * Index) / Ring.Count
				+ MountainStream.FRandRange(-0.12f, 0.12f);
			const float Radius = Ring.Radius + MountainStream.FRandRange(-6000.0f, 6000.0f);
			const FVector Location(
				GridCentre.X + FMath::Cos(Angle) * Radius,
				GridCentre.Y + FMath::Sin(Angle) * Radius,
				Ring.BaseZ + MountainStream.FRandRange(-600.0f, 200.0f));
			const float Scale = MountainStream.FRandRange(Ring.ScaleMin, Ring.ScaleMax);
			MountainTransforms.Add(FTransform(
				FRotator(0.0f, MountainStream.FRandRange(0.0f, 360.0f), 0.0f),
				Location,
				FVector(Scale, Scale, Scale * MountainStream.FRandRange(0.9f, 1.25f)))
				.GetRelativeTransform(MountainHISM->GetComponentTransform()));
		}
	}
	MountainHISM->AddInstances(MountainTransforms, false, false, true);
	MountainHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border mountains: instances=%d inner_radius_cm=%.0f outer_radius_cm=%.0f grid_half_span_cm=%.0f"),
		MountainTransforms.Num(), Rings[0].Radius, Rings[1].Radius, GridHalfSpanCm);
}

bool AWarZoneFootprintPreview::AreAllFacilityLevelsLoaded() const
{
	if (FacilityPlacements.IsEmpty())
		return false;

	if (bUseRuntimeBlueprintTiles)
	{
		for (int32 Index = 0; Index < FacilityPlacements.Num(); ++Index)
		{
			if (FacilityPlacements[Index].VisualSet != EFacilityVisualSet::Checkpoint)
				continue;
			if (!FacilityDesignLevelInstances.IsValidIndex(Index))
				return false;
			const ULevelStreamingDynamic* Instance = FacilityDesignLevelInstances[Index];
			if (!IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
				return false;
		}
		return true;
	}

	if (FacilityDesignLevelInstances.Num() != FacilityPlacements.Num()
		|| FacilityDesignLevelInstances.IsEmpty())
		return false;
	for (const ULevelStreamingDynamic* Instance : FacilityDesignLevelInstances)
		if (!IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
			return false;
	return true;
}

void AWarZoneFootprintPreview::VerifyDesignLevelSeparation()
{
	if (bLoggedDesignLevelSeparation || !AreAllFacilityLevelsLoaded())
	{
		return;
	}

	auto GetLoadedLevelBounds = [](const ULevel* Level)
	{
		FBox Bounds(EForceInit::ForceInit);
		if (!IsValid(Level))
			return Bounds;

		for (const AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor) || Actor->IsHidden())
				continue;

			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents(PrimitiveComponents);
			for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (IsValid(PrimitiveComponent)
					&& PrimitiveComponent->IsRegistered()
					&& PrimitiveComponent->IsVisible())
				{
					Bounds += PrimitiveComponent->Bounds.GetBox();
				}
			}
		}
		return Bounds;
	};

	TArray<FBox> BoundsByFacility;
	if (bUseRuntimeBlueprintTiles)
	{
		for (const AActor* SpawnedActor : SpawnedRuntimeTiles)
		{
			if (!IsValid(SpawnedActor)
				|| !SpawnedActor->Tags.Contains(TEXT("RuntimeTacticalFacility")))
			{
				continue;
			}
			const FBox Bounds = SpawnedActor->GetComponentsBoundingBox(true);
			if (Bounds.IsValid)
				BoundsByFacility.Add(Bounds);
		}
	}
	else
	{
		for (const ULevelStreamingDynamic* Instance : FacilityDesignLevelInstances)
		{
			if (IsValid(Instance))
				BoundsByFacility.Add(GetLoadedLevelBounds(Instance->GetLoadedLevel()));
		}
	}
	int32 OverlapPairs = 0;
	float MinimumGapCm = 0.0f;
	bool bMeasuredGap = false;
	for (int32 A = 0; A < BoundsByFacility.Num(); ++A)
	{
		for (int32 B = A + 1; B < BoundsByFacility.Num(); ++B)
		{
			if (BoundsByFacility[A].Intersect(BoundsByFacility[B]))
				++OverlapPairs;
			const FVector Delta = BoundsByFacility[A].GetCenter() - BoundsByFacility[B].GetCenter();
			const float PairGapX = FMath::Abs(Delta.X)
				- BoundsByFacility[A].GetExtent().X - BoundsByFacility[B].GetExtent().X;
			const float PairGapY = FMath::Abs(Delta.Y)
				- BoundsByFacility[A].GetExtent().Y - BoundsByFacility[B].GetExtent().Y;
			// For axis-aligned facility bounds, separation on either axis is enough.
			// The old radial extent calculation could report a negative gap even when
			// Intersect() correctly said the facilities did not overlap.
			const float PairGapCm = FMath::Max(PairGapX, PairGapY);
			MinimumGapCm = bMeasuredGap ? FMath::Min(MinimumGapCm, PairGapCm) : PairGapCm;
			bMeasuredGap = true;
		}
	}

	bLoggedDesignLevelSeparation = true;
	UE_LOG(LogTemp, Display,
		TEXT("Facility visible separation: count=%d overlap_pairs=%d minimum_gap_cm=%.1f pass=%s"),
		BoundsByFacility.Num(), OverlapPairs, MinimumGapCm,
		OverlapPairs == 0 ? TEXT("true") : TEXT("false"));
}

void AWarZoneFootprintPreview::ReserveFacility(
	EFacilityVisualSet VisualSet,
	const FIntPoint& Anchor,
	const FIntPoint& Footprint,
	int32 RotationQuarterTurns,
	const TArray<FIntPoint>& OccupiedCells,
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	FFacilityPlacement& Placement = FacilityPlacements.AddDefaulted_GetRef();
	Placement.VisualSet = VisualSet;
	switch (VisualSet)
	{
	case EFacilityVisualSet::Warehouse: Placement.FacilityId = TEXT("IndustrialRaid_3x3"); break;
	case EFacilityVisualSet::Yard: Placement.FacilityId = TEXT("SatelliteCamp_2x2"); break;
	case EFacilityVisualSet::LongBarracks: Placement.FacilityId = TEXT("LongBarracks_2x1"); break;
	case EFacilityVisualSet::LinearTrench: Placement.FacilityId = TEXT("LinearTrench_4x1"); break;
	case EFacilityVisualSet::Checkpoint: Placement.FacilityId = TEXT("Checkpoint_1x2"); break;
	case EFacilityVisualSet::DowntownBlock: Placement.FacilityId = TEXT("DowntownBlock_6x6"); break;
	case EFacilityVisualSet::FactoryConstruction: Placement.FacilityId = TEXT("FactoryConstruction_2x2"); break;
	case EFacilityVisualSet::RuralHideout: Placement.FacilityId = TEXT("RuralHideout_2x2"); break;
	default: Placement.FacilityId = TEXT("UnknownFacility"); break;
	}
	Placement.AnchorCell = Anchor;
	Placement.Footprint = Footprint;
	Placement.RotationQuarterTurns = RotationQuarterTurns;
	const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 RaidSeed = IsValid(GameMode) ? GameMode->GetMapGenerationSeed() : 0;
	Placement.LocalSeed = static_cast<int64>(HashCombine(
		GetTypeHash(RaidSeed),
		HashCombine(GetTypeHash(Anchor.X), HashCombine(GetTypeHash(Anchor.Y), GetTypeHash(static_cast<uint8>(VisualSet))))));
	const bool bLoweredVariant = (Placement.LocalSeed & 1ll) != 0;

	if (VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint)
	{
		// Ground, not WarZoneStronghold: the +220 pad and its four ramps existed to
		// give the code-built core silhouette and approaches. The harvested factory
		// compound is authored flat, and raising it would hang its yard fences and
		// barrel rows over the pad edge the way the rural island once hung over its.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
	}
	else if (VisualSet == EFacilityVisualSet::Yard)
	{
		Placement.ElevationProfile = bLoweredVariant
			? EFacilityElevationProfile::RaisedCompound
			: EFacilityElevationProfile::RaisedCompound;
		Placement.BaseElevationCm = bLoweredVariant ? -60.0f : 120.0f;
	}
	else if (VisualSet == EFacilityVisualSet::LongBarracks)
	{
		Placement.ElevationProfile = bLoweredVariant
			? EFacilityElevationProfile::RaisedCompound
			: EFacilityElevationProfile::RaisedBarracks;
		Placement.BaseElevationCm = bLoweredVariant ? -50.0f : 100.0f;
	}
	else if (VisualSet == EFacilityVisualSet::DowntownBlock)
	{
		// The authored block sits on its own paving just above the shared datum, so
		// the pad beneath it must stay there too. A 160 cm raise would leave the cafe
		// terrace on a plinth with its kerbs hanging over the edge.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}
	else if (VisualSet == EFacilityVisualSet::FactoryConstruction)
	{
		// The authored hall level was trimmed so its own floor slabs are gone and its
		// building bases sit at local Z=0. Raising or lowering the pad under it would
		// leave the halls standing on a step instead of on the surrounding ground.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}
	else if (VisualSet == EFacilityVisualSet::RuralHideout)
	{
		// Its own ground is deleted, so the cabins stand on the shared terrain.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}

	// Pick an edge that approaches the closest generator-authored traversal cell.
	// This is deterministic and is serialized with the facility, so the visual ramp,
	// the access-road endpoint and a future server manifest all agree.
	const FIntPoint CardinalDirections[] = {
		FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
	};
	int32 BestAccessScore = MAX_int32;
	uint32 BestAccessTieBreak = MAX_uint32;
	for (const FIntPoint& OccupiedCell : OccupiedCells)
	{
		for (const FIntPoint& Direction : CardinalDirections)
		{
			if (VisualSet == EFacilityVisualSet::LongBarracks)
			{
				const bool bLongAlongX = Footprint.X > Footprint.Y;
				if ((bLongAlongX && Direction.X != 0)
					|| (!bLongAlongX && Direction.Y != 0))
				{
					continue;
				}
			}
			const FIntPoint CandidateAccess = OccupiedCell + Direction;
			if (OccupiedCells.Contains(CandidateAccess) || !TileByCell.Contains(CandidateAccess))
				continue;

			int32 NearestTraversalDistance = MAX_int32;
			for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
			{
				if (!IsValid(Pair.Value) || !IsRoadTraversalType(Pair.Value->GetType()))
					continue;
				const int32 Distance = FMath::Abs(Pair.Key.X - CandidateAccess.X)
					+ FMath::Abs(Pair.Key.Y - CandidateAccess.Y);
				NearestTraversalDistance = FMath::Min(NearestTraversalDistance, Distance);
			}
			const uint32 TieBreak = HashCombine(
				GetTypeHash(Placement.LocalSeed),
				HashCombine(GetTypeHash(CandidateAccess.X), GetTypeHash(CandidateAccess.Y)));
			if (NearestTraversalDistance < BestAccessScore
				|| (NearestTraversalDistance == BestAccessScore && TieBreak < BestAccessTieBreak))
			{
				BestAccessScore = NearestTraversalDistance;
				BestAccessTieBreak = TieBreak;
				Placement.EntranceCell = OccupiedCell;
				Placement.AccessCell = CandidateAccess;
				Placement.EntranceDirection = Direction;
			}
		}
	}
	if (Placement.ElevationProfile != EFacilityElevationProfile::Ground)
	{
		// Procedural facilities expose north/south entrances in local space. Rotate
		// local south toward the chosen road-facing edge so the ramp never meets a wall.
		if (Placement.EntranceDirection == FIntPoint(0, -1)) Placement.RotationQuarterTurns = 0;
		else if (Placement.EntranceDirection == FIntPoint(1, 0)) Placement.RotationQuarterTurns = 1;
		else if (Placement.EntranceDirection == FIntPoint(0, 1)) Placement.RotationQuarterTurns = 2;
		else if (Placement.EntranceDirection == FIntPoint(-1, 0)) Placement.RotationQuarterTurns = 3;
	}

	FName FacilityTag = WarehouseTag;
	Placement.FacilityLevel = TSoftObjectPtr<UWorld>(
		VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint
			? WarZoneCoreLevelPath : WarehouseLevelPath);
	if (VisualSet == EFacilityVisualSet::Yard)
	{
		FacilityTag = YardTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(YardLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::LongBarracks)
	{
		FacilityTag = BarracksTag;
	}
	else if (VisualSet == EFacilityVisualSet::LinearTrench)
	{
		FacilityTag = TrenchTag;
	}
	else if (VisualSet == EFacilityVisualSet::Checkpoint)
	{
		FacilityTag = CheckpointTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(CheckpointLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::DowntownBlock)
	{
		FacilityTag = DowntownTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(DowntownBlockLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::FactoryConstruction)
	{
		FacilityTag = FactoryConstructionTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(FactoryHallLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::RuralHideout)
	{
		FacilityTag = RuralHideoutTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(RuralDioramaLevelPath);
	}
	if (bUseRuntimeBlueprintTiles && !FacilityUsesAuthoredLevel(VisualSet, Footprint))
		Placement.FacilityLevel.Reset();

	for (const FIntPoint& Cell : OccupiedCells)
	{
		AMapTile* Tile = TileByCell.FindChecked(Cell);
		Tile->Tags.AddUnique(ReservedTag);
		const bool bIsWarZoneFacility = VisualSet == EFacilityVisualSet::Warehouse
			|| VisualSet == EFacilityVisualSet::Yard
			|| VisualSet == EFacilityVisualSet::LongBarracks
			|| VisualSet == EFacilityVisualSet::LinearTrench;
		if (bIsWarZoneFacility)
			Tile->Tags.AddUnique(WarZoneFacilityTag);
		Tile->Tags.AddUnique(FacilityTag);
		ReservedTiles.Add(Tile);
		Placement.OccupiedCells.Add(Cell);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Facility mapping: %s -> %s (Anchor=%d,%d Footprint=%d,%d Rotation=%d)"),
		*FacilityTag.ToString(),
		*Placement.FacilityLevel.ToSoftObjectPath().ToString(),
		Anchor.X, Anchor.Y, Footprint.X, Footprint.Y,
		RotationQuarterTurns * 90);
}

FVector AWarZoneFootprintPreview::GetFootprintCenter(const FFacilityPlacement& Placement) const
{
	if (Placement.OccupiedCells.IsEmpty())
		return FVector::ZeroVector;

	FVector Center = FVector::ZeroVector;
	for (const FIntPoint& Cell : Placement.OccupiedCells)
	{
		Center.X += Cell.X * GridStep;
		Center.Y += Cell.Y * GridStep;
	}
	return Center / Placement.OccupiedCells.Num();
}

FVector AWarZoneFootprintPreview::GetDesignFootprintCenter(
	const FFacilityPlacement& Placement) const
{
	if (Placement.OccupiedCells.IsEmpty())
		return FVector::ZeroVector;

	FVector Center = FVector::ZeroVector;
	for (const FIntPoint& Cell : Placement.OccupiedCells)
	{
		Center.X += Cell.X * DesignCellSize;
		Center.Y += Cell.Y * DesignCellSize;
	}
	Center /= Placement.OccupiedCells.Num();
	// Keep authored floor meshes just clear of the shared terrain top to avoid z-fighting
	// without making the structure appear to float.
	// This removes coplanar shimmer without producing a visible gameplay step.
	const float SurfaceZ = Placement.ElevationProfile == EFacilityElevationProfile::Ground
		? BaseGroundSurfaceZ : Placement.BaseElevationCm;
	Center.Z = SurfaceZ + 1.0f;
	return Center;
}

void AWarZoneFootprintPreview::ConfigureProxyMesh(
	UStaticMeshComponent* Component,
	const FVector& RelativeLocation,
	const FVector& Size)
{
	if (!IsValid(Component))
		return;

	Component->SetRelativeLocation(RelativeLocation);
	Component->SetRelativeScale3D(Size / 100.0f);
	Component->SetVisibility(true);
}

void AWarZoneFootprintPreview::ShowWarehouseProxy(const FVector& FootprintCenter)
{
	const float FootprintSize = GridStep * 1.8f;
	const float HalfSize = FootprintSize * 0.5f;
	const float WallThickness = GridStep * 0.12f;
	const float WallHeight = GridStep * 1.0f;
	const float FloorThickness = GridStep * 0.12f;
	const float DoorWidth = GridStep * 0.65f;
	const float FrontSegmentWidth = (FootprintSize - DoorWidth) * 0.5f;

	const FVector BaseOffset = FootprintCenter - GetActorLocation();
	ConfigureProxyMesh(
		FloorProxy,
		BaseOffset + FVector(0.0f, 0.0f, FloorThickness * 0.5f),
		FVector(FootprintSize, FootprintSize, FloorThickness));
	ConfigureProxyMesh(
		BackWallProxy,
		BaseOffset + FVector(0.0f, HalfSize - WallThickness * 0.5f, WallHeight * 0.5f),
		FVector(FootprintSize, WallThickness, WallHeight));
	ConfigureProxyMesh(
		LeftWallProxy,
		BaseOffset + FVector(-HalfSize + WallThickness * 0.5f, 0.0f, WallHeight * 0.5f),
		FVector(WallThickness, FootprintSize, WallHeight));
	ConfigureProxyMesh(
		RightWallProxy,
		BaseOffset + FVector(HalfSize - WallThickness * 0.5f, 0.0f, WallHeight * 0.5f),
		FVector(WallThickness, FootprintSize, WallHeight));

	const float FrontSegmentOffset = DoorWidth * 0.5f + FrontSegmentWidth * 0.5f;
	ConfigureProxyMesh(
		FrontWallLeftProxy,
		BaseOffset + FVector(-FrontSegmentOffset, -HalfSize + WallThickness * 0.5f, WallHeight * 0.5f),
		FVector(FrontSegmentWidth, WallThickness, WallHeight));
	ConfigureProxyMesh(
		FrontWallRightProxy,
		BaseOffset + FVector(FrontSegmentOffset, -HalfSize + WallThickness * 0.5f, WallHeight * 0.5f),
		FVector(FrontSegmentWidth, WallThickness, WallHeight));
	ConfigureProxyMesh(
		RoofProxy,
		BaseOffset + FVector(0.0f, 0.0f, WallHeight + FloorThickness * 0.5f),
		FVector(FootprintSize, FootprintSize, FloorThickness));
}

void AWarZoneFootprintPreview::ShowYardProxy(const FVector& FootprintCenter)
{
	const FVector BaseOffset = FootprintCenter - GetActorLocation();
	const float FootprintSize = GridStep * 1.8f;
	const float FloorThickness = GridStep * 0.08f;
	const float CoverLength = GridStep * 0.65f;
	const float CoverThickness = GridStep * 0.18f;
	const float CoverHeight = GridStep * 0.45f;
	const float CoverOffset = GridStep * 0.48f;

	ConfigureProxyMesh(
		YardFloorProxy,
		BaseOffset + FVector(0.0f, 0.0f, FloorThickness * 0.5f),
		FVector(FootprintSize, FootprintSize, FloorThickness));
	ConfigureProxyMesh(
		YardCoverNorthProxy,
		BaseOffset + FVector(0.0f, CoverOffset, CoverHeight * 0.5f),
		FVector(CoverLength, CoverThickness, CoverHeight));
	ConfigureProxyMesh(
		YardCoverSouthProxy,
		BaseOffset + FVector(0.0f, -CoverOffset, CoverHeight * 0.5f),
		FVector(CoverLength, CoverThickness, CoverHeight));
	ConfigureProxyMesh(
		YardCoverWestProxy,
		BaseOffset + FVector(-CoverOffset, 0.0f, CoverHeight * 0.5f),
		FVector(CoverThickness, CoverLength, CoverHeight));
	ConfigureProxyMesh(
		YardCoverEastProxy,
		BaseOffset + FVector(CoverOffset, 0.0f, CoverHeight * 0.5f),
		FVector(CoverThickness, CoverLength, CoverHeight));
}

void AWarZoneFootprintPreview::DrawReservation() const
{
	if (FacilityPlacements.Num() < 2 || GridStep <= 0.0f)
		return;

	const FVector CellExtent(GridStep * 0.42f, GridStep * 0.42f, GridStep * 0.12f);
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		FColor Color = FColor::Cyan;
		const TCHAR* Label = TEXT("WAREHOUSE 2x2");
		if (Placement.VisualSet == EFacilityVisualSet::Yard)
		{
			Color = FColor::Yellow;
			Label = TEXT("YARD 2x2");
		}
		else if (Placement.VisualSet == EFacilityVisualSet::Checkpoint)
		{
			Color = FColor::Green;
			Label = TEXT("CHECKPOINT 1x2");
		}
		for (const FIntPoint& Cell : Placement.OccupiedCells)
		{
			const FVector Location(Cell.X * GridStep, Cell.Y * GridStep, GridStep * 0.7f);
			DrawDebugBox(GetWorld(), Location, CellExtent, Color, false, 0.12f, 0, 1.5f);
		}

		const FVector Center = GetFootprintCenter(Placement) + FVector(0.0f, 0.0f, GridStep * 0.7f);
		DrawDebugBox(
			GetWorld(), Center,
			FVector(GridStep * 0.92f, GridStep * 0.92f, GridStep * 0.22f),
			Color, false, 0.12f, 0, 3.0f);
		DrawDebugString(
			GetWorld(), Center + FVector(0.0f, 0.0f, GridStep * 0.5f),
			Label, nullptr, Color, 0.12f, true, 1.0f);
	}
}

void AWarZoneFootprintPreview::DrawDesignScalePreview() const
{
	if (FacilityPlacements.Num() < 2)
		return;

	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		FVector PreviewCenter = FVector::ZeroVector;
		FColor Color = FColor::Cyan;
		const TCHAR* Label = TEXT("DESIGN: WAREHOUSE 2x2");
		if (Placement.VisualSet == EFacilityVisualSet::Warehouse)
		{
			PreviewCenter = FVector(-5000.0f, 4000.0f, 100.0f);
		}
		else if (Placement.VisualSet == EFacilityVisualSet::Yard)
		{
			PreviewCenter = FVector(0.0f, 4000.0f, 100.0f);
			Color = FColor::Yellow;
			Label = TEXT("DESIGN: YARD 2x2");
		}
		else
		{
			PreviewCenter = FVector(4500.0f, 4000.0f, 100.0f);
			Color = FColor::Green;
			Label = TEXT("DESIGN: CHECKPOINT 1x2");
		}

		int32 MinX = TNumericLimits<int32>::Max();
		int32 MinY = TNumericLimits<int32>::Max();
		int32 MaxX = TNumericLimits<int32>::Lowest();
		int32 MaxY = TNumericLimits<int32>::Lowest();
		for (const FIntPoint& Cell : Placement.OccupiedCells)
		{
			MinX = FMath::Min(MinX, Cell.X);
			MinY = FMath::Min(MinY, Cell.Y);
			MaxX = FMath::Max(MaxX, Cell.X);
			MaxY = FMath::Max(MaxY, Cell.Y);
		}

		const float Width = (MaxX - MinX + 1) * DesignCellSize;
		const float Height = (MaxY - MinY + 1) * DesignCellSize;
		for (const FIntPoint& Cell : Placement.OccupiedCells)
		{
			const float LocalX = (Cell.X - MinX + 0.5f) * DesignCellSize - Width * 0.5f;
			const float LocalY = (Cell.Y - MinY + 0.5f) * DesignCellSize - Height * 0.5f;
			DrawDebugBox(
				GetWorld(),
				PreviewCenter + FVector(LocalX, LocalY, 0.0f),
				FVector(DesignCellSize * 0.48f, DesignCellSize * 0.48f, 100.0f),
				Color, false, 0.12f, 0, 10.0f);
		}

		DrawDebugBox(
			GetWorld(), PreviewCenter,
			FVector(Width * 0.5f, Height * 0.5f, 180.0f),
			Color, false, 0.12f, 0, 15.0f);
		DrawDebugString(
			GetWorld(), PreviewCenter + FVector(0.0f, 0.0f, 350.0f),
			FString::Printf(TEXT("%s | CELL=20m | ROT=%d"), Label, Placement.RotationQuarterTurns * 90),
			nullptr, Color, 0.12f, true, 1.5f);
	}
}
