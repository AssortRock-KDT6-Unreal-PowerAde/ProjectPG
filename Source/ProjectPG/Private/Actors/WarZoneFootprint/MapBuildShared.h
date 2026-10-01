// 맵 액터와 일꾼들(건물 자리 담당 등)이 같이 쓰는 숫자·경로·작은 계산.
// 왜 따로 뺐나: 원래 맵 cpp 안쪽(익명 namespace)에만 있어서 맵만 볼 수 있었다.
//   일꾼이 맵 cpp 밖(다른 파일)으로 나가면 못 본다. 그렇다고 일꾼 파일에 복사하면
//   예) 호수 위치 계산이 두 벌이 돼서, 한쪽만 고치면 호수와 호숫가 마을이 서로 다른 모서리에 생긴다.
//   그래서 한 벌만 여기 두고 맵 cpp 와 일꾼 cpp 가 같이 include 한다.
// MapBuild 라는 이름 상자에 넣는 이유: 다른 파일의 같은 이름과 섞이지 않게(검사기 때 겪은 유니티 빌드 충돌).
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Actors/MapTile.h"
#include "Actors/WarZoneFootprintPreview.h"

namespace MapBuild
{
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

inline const TArray<FTerrainFeatureMesh>& GetTerrainFeatureMeshes()
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
inline float SampleTerrainFeatureHeight(const FTerrainFeatureMesh& Feature, float U, float V)
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
	inline bool FacilityUsesAuthoredLevel(EFacilityVisualSet VisualSet, const FIntPoint& Footprint)
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
	inline bool FacilityBringsOwnTerrain(EFacilityVisualSet VisualSet)
	{
		return false;
	}
	// Hand-authored facility Blueprints. Every wall and prop in these is an
	// individual StaticMeshComponent, so a designer can select one in the editor
	// viewport and drag or rescale it - which is impossible for the HISM instances
	// AProceduralFacilityActor emits. A visual set with no entry here has not been
	// authored yet and falls back to the procedural builder, so the library can be
	// filled in one facility at a time.
	inline const TCHAR* GetAuthoredFacilityBlueprintPath(EFacilityVisualSet VisualSet)
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
	// 칸 크기(2000cm). 맵 
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

	inline FBorderLake GetBorderLake(int64 RaidSeed, const FIntPoint& MinCell, const FIntPoint& MaxCell,
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
	inline float GetLakeShoreJitter(int64 RaidSeed, const FIntPoint& Cell)
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

	inline uint8 RotateConnectionMaskPositiveYaw(uint8 Mask)
	{
		uint8 Rotated = 0;
		if (Mask & NorthConnection) Rotated |= WestConnection;
		if (Mask & EastConnection) Rotated |= NorthConnection;
		if (Mask & SouthConnection) Rotated |= EastConnection;
		if (Mask & WestConnection) Rotated |= SouthConnection;
		return Rotated;
	}

	inline int32 FindPositiveYawRotation(uint8 CanonicalMask, uint8 TargetMask)
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

	inline bool IsRoadTraversalType(ETileType Type)
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
	inline TSet<FIntPoint> CollectTraversalCells(const TMap<FIntPoint, AMapTile*>& TileByCell)
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
