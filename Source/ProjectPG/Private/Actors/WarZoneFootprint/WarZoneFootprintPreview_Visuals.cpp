// AWarZoneFootprintPreview — 묶음 그리기 — 바닥·도로·호수 HISM, PCG 장식, 맵 경계(벽·산·치마판).
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"
#include "GameFramework/Character.h"
#include "Vehicle/PGVehiclePawn.h"
#include "PGMapVisualBuilder.h"

// ---- 바닥 그리기 도우미 (2026-09-28 BuildLightweightWorldVisuals 에서 떼어 냄 — 동작 그대로) ----
// 월드 위치·크기(cm)의 1m 정육면체 → 그 HISM 기준 인스턴스 트랜스폼. 이 파일 전용 이름공간(유니티 빌드에서 이름이 안 겹치게).
namespace PGVisualSteps
{
	FTransform MakeCubeTransform(UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& WorldLocation, const FVector& WorldSize)
	{
		return FTransform(FRotator::ZeroRotator, WorldLocation, WorldSize / 100.0f)
			.GetRelativeTransform(Component->GetComponentTransform());
	}

	FTransform MakeOrientedCubeTransform(UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& WorldLocation, const FRotator& WorldRotation, const FVector& WorldSize)
	{
		return FTransform(WorldRotation, WorldLocation, WorldSize / 100.0f)
			.GetRelativeTransform(Component->GetComponentTransform());
	}
}

void UPGMapVisualBuilder::BuildLightweightWorldVisuals()
{
	// 9/28: 446줄이던 것을 단계 함수로 나눴다(동작·순서 그대로). 칸 땅판 → 시설 땅판 → 호숫가 돌·갈대 → 호숫가 메시 → 묶음에 넣기.
	using namespace PGVisualSteps;
	// Use the engine's batch path so navigation bounds are cached only after the
	// complete HISM instance set exists. Individual AddInstance calls can expose
	// an intermediate invalid bound to the dynamic navigation system.
	Map->RoadSurfaceHISM->SetCanEverAffectNavigation(false);
	Map->GroundHISM->ClearInstances();
	Map->WarZoneGroundHISM->ClearInstances();
	Map->TransitionGroundHISM->ClearInstances();
	Map->RoadSurfaceHISM->ClearInstances();
	Map->LakeBedHISM->ClearInstances();
	Map->LakeWaterHISM->ClearInstances();

	FPGGroundBatches Batches;
	TArray<FTransform>& GroundTransforms = Batches.GroundTransforms;
	TArray<FTransform>& WarZoneGroundTransforms = Batches.WarZoneGroundTransforms;
	TArray<FTransform>& TransitionGroundTransforms = Batches.TransitionGroundTransforms;
	TArray<FTransform>& RoadTransforms = Batches.RoadTransforms;
	TArray<FTransform>& LakeBedTransforms = Batches.LakeBedTransforms;
	TArray<FTransform>& LakeWaterTransforms = Batches.LakeWaterTransforms;
	FIntPoint& LakeSheetMin = Batches.LakeSheetMin;
	FIntPoint& LakeSheetMax = Batches.LakeSheetMax;
	int32& LakeWaterCellCount = Batches.LakeWaterCellCount;
	int32& RoadTileCount = Batches.RoadTileCount;
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
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
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
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Water)
			LakeCells.Add(Placement.GridCell);
	TMap<FIntPoint, TPair<int32, int32>> ShoreTileByCell;
	if (LakeCells.Num() > 0)
		Map->BuildShoreTransitionMap(LakeCells, ShoreTileByCell);

	FIntPoint VisualWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			VisualWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	AddCellGroundSlabs(Batches, FacilityCells, TerrainSolidBottomZ, ShoreTileByCell, VisualWarZoneCenter);

	AddFacilityTerrainPads(TerrainSolidBottomZ, GroundTransforms, WarZoneGroundTransforms);

	// Shoreline dressing. Water membership is a per-cell decision, so the waterline
	// steps along the 20 m grid. River stones and reeds standing in the shallows
	// break that line up.
	//
	// A tilted bank slab was tried here first and removed: a rotated box 27 m long
	// lifts its far end more than 3 m clear of a ground plane at Z=20, so instead of
	// a shore it produced planes jutting out of the terrain. A real curved shoreline
	// needs sub-cell geometry - authored shore meshes in the style of
	// SM_Terrain_Mound_2x2 - not a rotated cube.
	Map->ShoreRockHISM->ClearInstances();
	Map->ShoreReedHISM->ClearInstances();
	TArray<FTransform> ShoreRockTransforms;
	TArray<FTransform> ShoreReedTransforms;
	BuildShoreDressing(ShoreRockTransforms, ShoreReedTransforms);

	const int32 ShoreTransitionCount = PlaceShoreTransitionMeshes(ShoreTileByCell);

	Map->GroundHISM->AddInstances(GroundTransforms, false, false, true);
	Map->ShoreRockHISM->AddInstances(ShoreRockTransforms, false, false, true);
	Map->ShoreReedHISM->AddInstances(ShoreReedTransforms, false, false, true);
	Map->WarZoneGroundHISM->AddInstances(WarZoneGroundTransforms, false, false, true);
	Map->TransitionGroundHISM->AddInstances(TransitionGroundTransforms, false, false, true);
	Map->RoadSurfaceHISM->AddInstances(RoadTransforms, false, false, false);
	if (LakeSheetMax.X >= LakeSheetMin.X && LakeSheetMax.Y >= LakeSheetMin.Y)
	{
		LakeWaterTransforms.Add(MakeCubeTransform(
			Map->LakeWaterHISM,
			FVector(
				(LakeSheetMin.X + LakeSheetMax.X) * 0.5f * DesignCellSize,
				(LakeSheetMin.Y + LakeSheetMax.Y) * 0.5f * DesignCellSize,
				LakeSurfaceZ - 5.0f),
			FVector(
				(LakeSheetMax.X - LakeSheetMin.X + 1) * DesignCellSize,
				(LakeSheetMax.Y - LakeSheetMin.Y + 1) * DesignCellSize,
				10.0f)));
	}
	Map->LakeBedHISM->AddInstances(LakeBedTransforms, false, false, true);
	Map->LakeWaterHISM->AddInstances(LakeWaterTransforms, false, false, false);

	Map->GroundHISM->BuildTreeIfOutdated(false, true);
	Map->WarZoneGroundHISM->BuildTreeIfOutdated(false, true);
	Map->TransitionGroundHISM->BuildTreeIfOutdated(false, true);
	Map->RoadSurfaceHISM->BuildTreeIfOutdated(false, true);
	Map->LakeBedHISM->BuildTreeIfOutdated(false, true);
	Map->LakeWaterHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border lake: cells=%d shore_cells=%d shore_meshes=%d shore_rocks=%d shore_reeds=%d ")
		TEXT("surface_z=%.0f bed_z=%.0f bank_drop_cm=%.0f"),
		LakeWaterCellCount, ShoreRockTransforms.Num() / 5, ShoreTransitionCount,
		ShoreRockTransforms.Num(), ShoreReedTransforms.Num(),
		LakeSurfaceZ, LakeBedZ, BaseGroundSurfaceZ - LakeSurfaceZ);
	Map->GroundHISM->RecreatePhysicsState();
	Map->WarZoneGroundHISM->RecreatePhysicsState();
	Map->TransitionGroundHISM->RecreatePhysicsState();
	Map->RoadSurfaceHISM->RecreatePhysicsState();

	UE_LOG(LogTemp, Display,
		TEXT("HISM design world: ground_instances=%d warzone_ground_instances=%d transition_ground_instances=%d road_tiles=%d road_surface_instances=%d collision=true facility_support_cells=%d"),
		Map->GroundHISM->GetInstanceCount(), Map->WarZoneGroundHISM->GetInstanceCount(), Map->TransitionGroundHISM->GetInstanceCount(), RoadTileCount, Map->RoadSurfaceHISM->GetInstanceCount(),
		2025 - Map->TileDesignPlacements.Num());
}

// 칸마다 땅판(일반·워존·경계)·도로 판·호수 바닥·호수 물 범위를 모은다. 9/28 BuildLightweightWorldVisuals 에서 떼어 냄(동작 그대로).
void UPGMapVisualBuilder::AddCellGroundSlabs(FPGGroundBatches& Batches, const TSet<FIntPoint>& FacilityCells, const float TerrainSolidBottomZ,
	const TMap<FIntPoint, TPair<int32, int32>>& ShoreTileByCell, const FIntPoint& VisualWarZoneCenter)
{
	using namespace PGVisualSteps;
	TArray<FTransform>& GroundTransforms = Batches.GroundTransforms;
	TArray<FTransform>& WarZoneGroundTransforms = Batches.WarZoneGroundTransforms;
	TArray<FTransform>& TransitionGroundTransforms = Batches.TransitionGroundTransforms;
	TArray<FTransform>& RoadTransforms = Batches.RoadTransforms;
	TArray<FTransform>& LakeBedTransforms = Batches.LakeBedTransforms;
	TArray<FTransform>& LakeWaterTransforms = Batches.LakeWaterTransforms;
	FIntPoint& LakeSheetMin = Batches.LakeSheetMin;
	FIntPoint& LakeSheetMax = Batches.LakeSheetMax;
	int32& LakeWaterCellCount = Batches.LakeWaterCellCount;
	int32& RoadTileCount = Batches.RoadTileCount;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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
			? Map->WarZoneGroundHISM
			: (bTransitionGround ? Map->TransitionGroundHISM : Map->GroundHISM);
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
				Map->LakeBedHISM,
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
				Map->LakeBedHISM,
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

		const float AuthoredElevation = Map->GetSurfaceElevationForCell(Placement.GridCell);
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
			Map->RoadSurfaceHISM, Center, FVector(600.0f, 600.0f, RoadSurfaceThicknessCm)));
		auto AddRoadArm = [this, &Placement, &RoadTransforms](
			const FIntPoint& Direction, uint8 ConnectionBit)
		{
			if ((Placement.ConnectionMask & ConnectionBit) == 0)
				return;
			const float ThisSurfaceZ = Map->GetSurfaceElevationForCell(Placement.GridCell);
			const float NeighbourSurfaceZ = Map->GetSurfaceElevationForCell(Placement.GridCell + Direction);
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
				Map->RoadSurfaceHISM,
				(Start + End) * 0.5f,
				Delta.Rotation(),
				FVector(Delta.Size() + 2.0f, 600.0f, RoadSurfaceThicknessCm)));
		};
		AddRoadArm(FIntPoint(0, 1), NorthConnection);
		AddRoadArm(FIntPoint(1, 0), EastConnection);
		AddRoadArm(FIntPoint(0, -1), SouthConnection);
		AddRoadArm(FIntPoint(-1, 0), WestConnection);
	}
}

// 시설 발자국마다 이음새 없는 땅판 하나(시설 바닥 높이). 9/28 떼어 냄(동작 그대로).
void UPGMapVisualBuilder::AddFacilityTerrainPads(const float TerrainSolidBottomZ, TArray<FTransform>& GroundTransforms, TArray<FTransform>& WarZoneGroundTransforms)
{
	using namespace PGVisualSteps;
	// Each multi-cell facility owns one continuous terrain tile matching its exact
	// 2x1, 2x2, 3x3 (or rotated) footprint. This removes internal seams and gives
	// the building, props, collision and access pieces one authoritative top Z.
	int32 RaisedPadCount = 0;
	int32 LoweredPadCount = 0;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
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
				? Map->WarZoneGroundHISM : Map->GroundHISM;
		TArray<FTransform>& FacilityGroundTransforms = FacilityGroundComponent == Map->WarZoneGroundHISM
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
		Map->FacilityPlacements.Num(), RaisedPadCount, LoweredPadCount);
}

// 호숫가 돌·갈대(물가 칸 둘레). 9/28 떼어 냄(동작 그대로) — 설명은 BuildLightweightWorldVisuals 의 호숫가 주석.
void UPGMapVisualBuilder::BuildShoreDressing(TArray<FTransform>& ShoreRockTransforms, TArray<FTransform>& ShoreReedTransforms)
{
	const int64 ShoreRaidSeed = Map->GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
	TSet<FIntPoint> WaterCells;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Water)
			WaterCells.Add(Placement.GridCell);

	const FIntPoint ShoreOffsets[] = {
		FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
	};
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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
				.GetRelativeTransform(Map->ShoreRockHISM->GetComponentTransform()));
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
				.GetRelativeTransform(Map->ShoreReedHISM->GetComponentTransform()));
		}
	}
}

// 호숫가 메시(생성기가 만든 4가지 모양)를 칸마다 놓는다. 놓은 수를 돌려준다. 9/28 떼어 냄(동작 그대로).
int32 UPGMapVisualBuilder::PlaceShoreTransitionMeshes(const TMap<FIntPoint, TPair<int32, int32>>& ShoreTileByCell)
{
	// Place the shore meshes themselves. Their local origin already sits on the cell
	// centre with the land face at Z=20, so the only transform needed is the cell
	// position and the quarter turn that points the authored water side at the lake.
	int32 ShoreTransitionCount = 0;
	for (int32 VariantIndex = 0; VariantIndex < Map->ShoreTransitionHISMs.Num(); ++VariantIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = Map->ShoreTransitionHISMs[VariantIndex];
		if (!IsValid(Component))
			continue;
		if (Component->GetStaticMesh() == nullptr)
		{
			if (Map->ShoreTransitionHISMs.IsValidIndex(VariantIndex)
				&& VariantIndex < UE_ARRAY_COUNT(WzfpShoreMeshPaths))
			{
				if (UStaticMesh* ShoreMesh = LoadObject<UStaticMesh>(
					nullptr, WzfpShoreMeshPaths[VariantIndex]))
				{
					Component->SetStaticMesh(ShoreMesh);
					// 호숫가 메시는 생성기(PG.BuildShoreMeshes)와 묶여 코드에 두고, 바닥 재질은 맵 에셋 묶음(DA_PGMapVisuals)에서.
					const TSoftObjectPtr<UMaterialInterface>& GroundAsset = UPGMapVisualSet::GetActive()->UnifiedGroundMaterial;
					UMaterialInterface* SharedGroundMaterial = GroundAsset.IsNull() ? nullptr : GroundAsset.LoadSynchronous();
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
	return ShoreTransitionCount;
}

void UPGMapVisualBuilder::BuildPCGDressingGraph()
{
	if (!IsValid(Map->DressingPCGComponent))
		return;

	// 풀·관목 메시는 맵 에셋 묶음(DA_PGMapVisuals, 없으면 원래 코드 에셋)에서(9/23 블루프린트 분리).
	// Execution-plan requirement: mix in at least two shrub species so grass-area
	// tiles (Meadow/Scrub) don't read as a single mesh stamped on a grid.
	const UPGMapVisualSet* MapVisuals = UPGMapVisualSet::GetActive();
	const TArray<TSoftObjectPtr<UStaticMesh>>& GrassMeshes = MapVisuals->DressingGrass;
	const TArray<TSoftObjectPtr<UStaticMesh>>& ShrubMeshes = MapVisuals->DressingShrubs;

	RuntimeDressingGraph = NewObject<UPCGGraph>(Map->DressingPCGComponent, TEXT("RuntimeDressingGraph"), RF_Transient);
	UPCGCreatePointsSettings* CreatePointsSettings = nullptr;
	UPCGStaticMeshSpawnerSettings* SpawnerSettings = nullptr;
	UPCGNode* CreatePointsNode = RuntimeDressingGraph->AddNodeOfType<UPCGCreatePointsSettings>(CreatePointsSettings);
	UPCGNode* SpawnerNode = RuntimeDressingGraph->AddNodeOfType<UPCGStaticMeshSpawnerSettings>(SpawnerSettings);
	if (!IsValid(CreatePointsNode) || !IsValid(SpawnerNode) || !IsValid(CreatePointsSettings) || !IsValid(SpawnerSettings))
		return;

	CreatePointsSettings->CoordinateSpace = EPCGCoordinateSpace::World;
	CreatePointsSettings->bCullPointsOutsideVolume = false;
	FRandomStream RandomStream(Map->DressingPCGComponent->Seed);
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
		for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
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
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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
		// 풀 밀도 표(공용 헤더 GetTileVisualTraits): 초원 4칸에 1, 덤불 7칸에 1, 폐허·빈 땅 13칸에 1.
		const FTileVisualTraits& GrassTraits = GetTileVisualTraits(Placement.Visual);
		if (GrassTraits.GrassCellDivisor <= 0)
			continue;
		CellDivisor = GrassTraits.GrassCellDivisor;
		ClumpCount = GrassTraits.GrassClumps;

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
		// 패킹 경로의 풀과 같은 값으로 맞춘다. 전에는 18000 이라 같은 풀인데도 PCG 쪽만
		// 3 배 멀리까지 그리고 있었다 — 멀리 있는 풀 카드는 화면에서 몇 픽셀도 안 된다.
		Entry.Descriptor.InstanceStartCullDistance = 1800;
		Entry.Descriptor.InstanceEndCullDistance = 6000;
		Entry.Descriptor.bCastShadow = false;
		Entry.Descriptor.ComponentTags.Add(TEXT("PCG_Dressing"));
	}
	// Lower relative weight than grass (2 x weight-1 vs 3 x weight-3) so shrubs
	// read as an occasional accent rather than half the ground cover.
	for (const TSoftObjectPtr<UStaticMesh>& ShrubMesh : ShrubMeshes)
	{
		FPCGMeshSelectorWeightedEntry& Entry = MeshSelector->MeshEntries.Emplace_GetRef(ShrubMesh, 1);
		Entry.Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Entry.Descriptor.bCanEverAffectNavigation = false;
		// 관목도 패킹 경로의 덤불(2500/8000)에 맞춘다.
		Entry.Descriptor.InstanceStartCullDistance = 2500;
		Entry.Descriptor.InstanceEndCullDistance = 8000;
		Entry.Descriptor.bCastShadow = false;
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

	Map->DressingPCGComponent->SetGraph(RuntimeDressingGraph);
	Map->DressingPCGComponent->GenerateLocal(true);
	UE_LOG(LogTemp, Display,
		TEXT("PCG dressing requested: seed=%d candidate_cells=%d (meadow=%d scrub=%d) facility_skipped=%d points=%d meshes=%d collision=false navigation=false"),
		Map->DressingPCGComponent->Seed,
		CandidateCellCount,
		MeadowCellCount,
		ScrubCellCount,
		FacilityAdjacentSkipCount,
		CreatePointsSettings->PointsToCreate.Num(),
		GrassMeshes.Num() + ShrubMeshes.Num());
}

void UPGMapVisualBuilder::EnforceMapBoundary()
{
	// 조종 중인 폰만 본다. 드래곤·배·몬스터·잔해는 건드리지 않는다 — 그것들이 경계를 넘는 것은
	// 정상이고(드래곤은 600m 반지름으로 선회한다), 막으면 연출이 깨진다.
	if (!bBoundaryReady || !Map->GetWorld())
		return;
	for (FConstPlayerControllerIterator It = Map->GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
			continue;
		const FVector Where = Pawn->GetActorLocation();
		if (Where.Z > BoundaryTopZ)
			continue; // 그 위는 배가 지나는 하늘이다. 타고 있으면 같이 올라간다.

		// 경계 안쪽 2m 로 되민다. 딱 경계선에 놓으면 다음 프레임에 또 걸려 덜덜 떤다.
		// 탈것(탱크·차)은 몸 크기만큼 더 안쪽 — 중심만 안에 두면 9m 탱크의 앞쪽이 타일 밖 허공에 걸려 떨어지기 시작했고,
		//   이 되밀기는 가로만 되돌려서 탱크가 경계에 붙은 채 땅 밑으로 계속 빠졌다(9/28 사용자 PIE, 서버에서 이미 그랬다).
		double InsetCm = 200.0;
		if (!Pawn->IsA<ACharacter>())
		{
			FVector Origin, Extent;
			Pawn->GetActorBounds(true, Origin, Extent);
			InsetCm += FMath::Min(static_cast<double>(FMath::Max(Extent.X, Extent.Y)), 1500.0);
		}
		// double 로 받아야 한다. 9/21 에 float 로 받았더니 좌표가 잘려(26000cm 에서 0.002cm) 안쪽에 있어도
		//   "밖에 있다" 로 보고 0.1초마다 차 속도를 0 으로 죽였다 — 모든 지상 차가 거북이처럼 기던 원인.
		//   안쪽이면 Clamp 가 입력을 그대로 돌려주므로 == 로 정확히 비교한다.
		const double ClampedX = FMath::Clamp(Where.X, BoundaryMinXY.X + InsetCm, BoundaryMaxXY.X - InsetCm);
		const double ClampedY = FMath::Clamp(Where.Y, BoundaryMinXY.Y + InsetCm, BoundaryMaxXY.Y - InsetCm);
		const bool bOutsideX = ClampedX != Where.X;
		const bool bOutsideY = ClampedY != Where.Y;
		if (!bOutsideX && !bOutsideY)
			continue;

		Pawn->SetActorLocation(FVector(ClampedX, ClampedY, Where.Z), false, nullptr, ETeleportType::TeleportPhysics);
		// 밖으로 향하던 속도를 죽인다. 안 그러면 되밀어 놔도 다시 같은 방향으로 나간다.
		if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
			IsValid(Body) && Body->IsSimulatingPhysics())
		{
			FVector Velocity = Body->GetPhysicsLinearVelocity();
			if (bOutsideX) Velocity.X = 0.0f;
			if (bOutsideY) Velocity.Y = 0.0f;
			Body->SetPhysicsLinearVelocity(Velocity);
		}
		// 땅 차는 모는 사람 화면이 기준이다 — 그 화면에도 되민 자리를 보낸다(안 그러면 다음 순간 밖 자리를 다시 보내 되돌린다).
		if (APGVehiclePawn* Car = Cast<APGVehiclePawn>(Pawn))
			Car->NotifyServerTeleport();
		UE_LOG(LogTemp, Display, TEXT("PGBoundary: pushed %s back inside (%.0f, %.0f)"), *GetNameSafe(Pawn), ClampedX, ClampedY);
	}
}

void UPGMapVisualBuilder::BuildMapBoundaryWall()
{
	// 안개는 **가리기만 하고 막지는 못한다.** 사용자 요구(9/21): "가까이에서 봐도 안개로 가려져서
	// 캐릭터나 차량이 못 빠져나가는 부분이 형성되어야" — 앞부분은 안개가, 뒷부분은 이 벽이 한다.
	//
	// 왜 필요한가: 날 수 있는 차가 경계를 넘어 산 너머로 나가면 아무것도 없는 허공에 떨어진다.
	//   지면 타일이 거기서 끝나므로 되돌아올 방법도 없다.
	//
	// 왜 보이지 않게 하나: 보이는 벽을 세우면 "여기가 만들다 만 맵" 이라고 광고하는 꼴이다.
	//   안개가 이미 그 자리를 뿌옇게 덮고 있으므로, 플레이어는 "더 가면 안 보인다" 로 읽고 돌아선다.
	if (!Map->bBuildMapBoundaryWall || Map->TileDesignPlacements.IsEmpty() || !Map->GetWorld())
		return;

	FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint MaxCell(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		MinCell = FIntPoint(
			FMath::Min(MinCell.X, Placement.GridCell.X), FMath::Min(MinCell.Y, Placement.GridCell.Y));
		MaxCell = FIntPoint(
			FMath::Max(MaxCell.X, Placement.GridCell.X), FMath::Max(MaxCell.Y, Placement.GridCell.Y));
	}

	// 타일 바깥 한 칸 자리에 세운다. 마지막 타일 위는 걸어 다닐 수 있어야 한다.
	const float MinX = (MinCell.X - 0.5f) * DesignCellSize;
	const float MaxX = (MaxCell.X + 0.5f) * DesignCellSize;
	const float MinY = (MinCell.Y - 0.5f) * DesignCellSize;
	const float MaxY = (MaxCell.Y + 0.5f) * DesignCellSize;
	// 공중은 풀어 준다 — 땅에서 25m 위부터는 되밀지 않는다(BoundaryWallHeightCm 이 더 커도 이 값으로 자른다).
	// 왜(9/21 로그): 전함은 맵 안쪽에 떠 있어도 437m 라 꼬리가 맵 끝을 넘는다. 날아서 배 뒤 입구로 가는 차가
	//   y=-30800 에서 214 번 되밀렸다 — 사용자: "보이지 않는 벽에 막혀서 함선 타기 자체가 어렵다".
	//   경계가 막아야 하는 것은 "땅으로 걸어·달려 산 쪽 빈 곳으로 나가는 것" 이라 낮은 높이만 지키면 된다.
	constexpr float GroundOnlyHeightCm = 2500.0f;
	const float TopZ = BaseGroundSurfaceZ + FMath::Min(Map->BoundaryWallHeightCm, GroundOnlyHeightCm);

	// **막는 상자를 세우지 않는다.** 사용자가 바로 짚었다(9/21): "벽 괜히 만들었다가 드래곤
	// 등장하는데 못 나오고 타일이 밑으로 못 빠지거나 하면 안 된다?"
	//
	// 맞는 걱정이다. 상자를 세우면 채널로 막는데, 드래곤도 Pawn 이라 같이 막힌다. 드래곤은
	// 맵 반지름의 0.9 배 자리에서 솟아 600m 반지름으로 선회하므로 이 경계를 반드시 넘는다.
	// 붕괴한 타일이 아래로 떨어지는 것도 막을 위험이 있다. 채널 하나로 "플레이어만" 을
	// 가려낼 방법이 없다 — 채널은 액터별이 아니라 종류별이기 때문이다.
	//
	// 그래서 막는 대신 **조종 중인 폰만 되밀어 놓는다.** 경계 밖으로 나가면 Tick 이 안쪽으로
	// 옮긴다. 드래곤·배·타일·잔해는 아무 영향을 안 받는다. 충돌이 아예 없으니 부작용도 없다.
	BoundaryMinXY = FVector2D(MinX, MinY);
	BoundaryMaxXY = FVector2D(MaxX, MaxY);
	BoundaryTopZ = TopZ;
	bBoundaryReady = true;

	UE_LOG(LogTemp, Display,
		TEXT("PGBoundary: soft edge x %.0f..%.0f y %.0f..%.0f up to %.0f m — players pushed back, nothing blocked"),
		MinX, MaxX, MinY, MaxY, (TopZ - BaseGroundSurfaceZ) / 100.0f);
}

void UPGMapVisualBuilder::BuildBorderMountains()
{
	if (!IsValid(Map->MountainHISM) || Map->MountainHISM->GetStaticMesh() == nullptr)
		return;
	Map->MountainHISM->ClearInstances();
	if (!Map->bBuildBorderMountains || Map->TileDesignPlacements.IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("Border mountains: skipped (bBuildBorderMountains=%d, placements=%d)"),
			Map->bBuildBorderMountains ? 1 : 0, Map->TileDesignPlacements.Num());
		return;
	}

	FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint MaxCell(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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
	const int64 RaidSeed = Map->GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
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
				.GetRelativeTransform(Map->MountainHISM->GetComponentTransform()));
		}
	}
	Map->MountainHISM->AddInstances(MountainTransforms, false, false, true);
	Map->MountainHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border mountains: instances=%d inner_radius_cm=%.0f outer_radius_cm=%.0f grid_half_span_cm=%.0f"),
		MountainTransforms.Num(), Rings[0].Radius, Rings[1].Radius, GridHalfSpanCm);

}

void UPGMapVisualBuilder::BuildGroundSkirt()
{
	// 산(BuildBorderMountains)과 따로 부른다. 처음엔 그 함수 끝에 붙였는데, 그 함수가 일찍 빠지는 판에는
	// 바닥판도 같이 안 깔렸다(9/21 PIE: 로그에 "Border mountains" 도 "Ground skirt" 도 없었다).
	if (Map->TileDesignPlacements.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Ground skirt: no tile placements yet — skipped"));
		return;
	}
	FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint MaxCell(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		MinCell = FIntPoint(FMath::Min(MinCell.X, Placement.GridCell.X), FMath::Min(MinCell.Y, Placement.GridCell.Y));
		MaxCell = FIntPoint(FMath::Max(MaxCell.X, Placement.GridCell.X), FMath::Max(MaxCell.Y, Placement.GridCell.Y));
	}
	// ---- 둘레 바닥판(치마) ----
	// 왜: 맵은 네모, 산은 둥근 두 줄이라 모서리·변에 산이 안 닿는 틈이 생긴다. 옆에서는 산과 거리 안개가 가려 주지만
	//   공중에서 내려다보면 그 틈으로 아래 하늘이 비쳤다(9/21 사용자 스크린샷의 파란 삼각형).
	// 어떻게: 타일 끝 바깥으로 GroundSkirtWidthCm 만큼, 산과 같은 재질의 판을 깐다. 판 하나를 크게 늘리면 무늬가 흐물흐물 늘어나므로
	//   GroundSkirtTileCm 크기로 잘라 같은 것을 여러 번 찍는다(HISM, 그리기 호출은 사실상 한 번).
	// 충돌 없음: 산과 같은 이유 — 맵 끝은 떨어지는 곳이고, 경계 되밀기가 사람을 붙잡는다. 길찾기에도 안 넣는다.
	// 높이는 땅보다 30cm 아래 — 타일과 겹쳐 깜빡이지 않게.
	if (!IsValid(Map->GroundSkirtHISM))
	{
		Map->GroundSkirtHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Map.Get(), TEXT("GroundSkirtHISM"));
		Map->GroundSkirtHISM->SetupAttachment(Map->GetRootComponent());
		Map->GroundSkirtHISM->SetMobility(EComponentMobility::Movable);
		Map->GroundSkirtHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Map->GroundSkirtHISM->SetCanEverAffectNavigation(false);
		Map->GroundSkirtHISM->SetGenerateOverlapEvents(false);
		Map->GroundSkirtHISM->SetCastShadow(false); // 바닥 판이라 드리울 그림자가 없다
		if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
			Map->GroundSkirtHISM->SetStaticMesh(Plane);
		Map->GroundSkirtHISM->RegisterComponent();
	}
	Map->GroundSkirtHISM->ClearInstances();
	if (!Map->bBuildGroundSkirt || !Map->GroundSkirtHISM->GetStaticMesh())
	{
		UE_LOG(LogTemp, Display, TEXT("Ground skirt: off (bBuildGroundSkirt=%d, plane mesh=%s)"),
			Map->bBuildGroundSkirt ? 1 : 0, Map->GroundSkirtHISM->GetStaticMesh() ? TEXT("ok") : TEXT("missing"));
		return;
	}
	// 산과 같은 재질. 산 묶음에 메시가 없는 판에도 깔리도록 재질은 직접 읽는다.
	const TSoftObjectPtr<UMaterialInterface>& SkirtMaterial = UPGMapVisualSet::GetActive()->RockEdgeMaterial;
	if (UMaterialInterface* MountainMaterial = SkirtMaterial.IsNull() ? nullptr : SkirtMaterial.LoadSynchronous())
		Map->GroundSkirtHISM->SetMaterial(0, MountainMaterial);
	const float EdgeMinX = (MinCell.X - 0.5f) * DesignCellSize;
	const float EdgeMaxX = (MaxCell.X + 0.5f) * DesignCellSize;
	const float EdgeMinY = (MinCell.Y - 0.5f) * DesignCellSize;
	const float EdgeMaxY = (MaxCell.Y + 0.5f) * DesignCellSize;
	const float Tile = FMath::Max(Map->GroundSkirtTileCm, 1000.0f);
	const float Width = FMath::Max(Map->GroundSkirtWidthCm, Tile);
	const float PlaneScale = Tile / 100.0f; // 엔진 기본 평면은 한 변 100cm
	const float SkirtZ = BaseGroundSurfaceZ - 30.0f;
	TArray<FTransform> SkirtTransforms;
	// 9/22 고침: 격자를 타일 끝(EdgeMin)에 맞춰 긋고, 맵과 조금이라도 겹치는 칸은 깔지 않는다.
	//   전에는 격자를 바깥 끝에서 그어 가장자리를 걸치는 칸까지 깔았다. 그 판은 땅보다 30cm 아래라 타일 밑에 숨지만,
	//   호수 수면(z=-35)보다는 위라 호수를 계단 모양으로 덮었다(사용자 스크린샷). 타일 끝에 맞추면 칸이 맵 안이거나 밖이거나 둘 중 하나다
	//   (맵 폭이 판 크기로 나눠떨어질 때 — 기본 600m / 50m).
	const int32 FirstX = -FMath::CeilToInt(Width / Tile);
	const int32 LastX = FMath::CeilToInt((EdgeMaxX - EdgeMinX + Width) / Tile);
	const int32 FirstY = -FMath::CeilToInt(Width / Tile);
	const int32 LastY = FMath::CeilToInt((EdgeMaxY - EdgeMinY + Width) / Tile);
	for (int32 IX = FirstX; IX < LastX; ++IX)
	{
		const float X = EdgeMinX + IX * Tile;
		for (int32 IY = FirstY; IY < LastY; ++IY)
		{
			const float Y = EdgeMinY + IY * Tile;
			const bool bOverlapsMap = X < EdgeMaxX && X + Tile > EdgeMinX && Y < EdgeMaxY && Y + Tile > EdgeMinY;
			if (bOverlapsMap)
				continue;
			SkirtTransforms.Add(FTransform(FRotator::ZeroRotator,
				FVector(X + Tile * 0.5f, Y + Tile * 0.5f, SkirtZ), FVector(PlaneScale, PlaneScale, 1.0f))
				.GetRelativeTransform(Map->GroundSkirtHISM->GetComponentTransform()));
		}
	}
	Map->GroundSkirtHISM->AddInstances(SkirtTransforms, false, false, true);
	Map->GroundSkirtHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display, TEXT("Ground skirt: %d plane(s) of %.0f m, %.0f m out from the tile edge (x %.0f..%.0f y %.0f..%.0f), z %.0f cm, material %s"),
		SkirtTransforms.Num(), Tile * 0.01f, Width * 0.01f, EdgeMinX, EdgeMaxX, EdgeMinY, EdgeMaxY, SkirtZ,
		*GetNameSafe(Map->GroundSkirtHISM->GetMaterial(0)));
}
