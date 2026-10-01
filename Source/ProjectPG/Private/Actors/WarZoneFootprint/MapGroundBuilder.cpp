#include "Actors/WarZoneFootprint/MapGroundBuilder.h"

#include "Actors/WarZoneFootprint/MapBuildShared.h"
#include "Actors/WarZoneFootprint/MapAssetSet.h"
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

using namespace MapBuild;

void UMapGroundBuilder::Init(AWarZoneFootprintPreview* InMap)
{
	Map = InMap;
}

UWorld* UMapGroundBuilder::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 땅판·도로 판·호수 깔기. 칸마다 땅 종류(들판/워존/경계)에 맞는 판을 한 장씩 HISM 에 넣고,
// 도로 칸에는 연결 방향대로 아스팔트 팔을 붙이고, 호수 칸은 바닥판 + 물 한 장으로 덮는다.
void UMapGroundBuilder::BuildLightweightWorldVisuals()
{
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

	int32 RoadTileCount = 0;
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
		auto AddRoadArm = [this, &Placement, &RoadTransforms, &MakeOrientedCubeTransform](
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
	{
		const AGameModePG* ShoreGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
		const int64 ShoreRaidSeed = IsValid(ShoreGameMode) ? ShoreGameMode->GetMapGenerationSeed() : 0;
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
			static const TCHAR* ShoreMeshAssetPaths[] = {
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner1.SM_Shore_Corner1"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Adjacent.SM_Shore_Corner2Adjacent"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Diagonal.SM_Shore_Corner2Diagonal"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner3.SM_Shore_Corner3")
			};
			if (Map->ShoreTransitionHISMs.IsValidIndex(VariantIndex)
				&& VariantIndex < UE_ARRAY_COUNT(ShoreMeshAssetPaths))
			{
				if (UStaticMesh* ShoreMesh = LoadObject<UStaticMesh>(
					nullptr, ShoreMeshAssetPaths[VariantIndex]))
				{
					Component->SetStaticMesh(ShoreMesh);
					// 호숫가 비탈도 들판 땅과 같은 머티리얼(DA_MapAssets 의 NatureGroundMaterial).
					UMaterialInterface* SharedGroundMaterial = Map->GetMapAssets().NatureGroundMaterial.LoadSynchronous();
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

// 맵 바깥 산 두르기. 시드로 정한 간격·크기로 산 메시를 몇 겹의 원으로 놓는다.
void UMapGroundBuilder::BuildBorderMountains()
{
	if (!IsValid(Map->MountainHISM) || Map->MountainHISM->GetStaticMesh() == nullptr)
		return;
	Map->MountainHISM->ClearInstances();
	if (!Map->bBuildBorderMountains || Map->TileDesignPlacements.IsEmpty())
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
				.GetRelativeTransform(Map->MountainHISM->GetComponentTransform()));
		}
	}
	Map->MountainHISM->AddInstances(MountainTransforms, false, false, true);
	Map->MountainHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border mountains: instances=%d inner_radius_cm=%.0f outer_radius_cm=%.0f grid_half_span_cm=%.0f"),
		MountainTransforms.Num(), Rings[0].Radius, Rings[1].Radius, GridHalfSpanCm);
}

// 풀·덤불 뿌리기. 들판·덤불·폐허 칸 위에 PCG 그래프로 풀 덩어리를 뿌린다(시설 주변은 비운다).
void UMapGroundBuilder::BuildPCGDressingGraph()
{
	if (!IsValid(Map->DressingPCGComponent))
		return;

	// 풀·덤불 메시는 DA_MapAssets 에서 고른다(풀 2종 이상, 덤불 2종 이상 섞어야 도장 찍은 것처럼 안 보인다).
	const TArray<TSoftObjectPtr<UStaticMesh>>& GrassMeshes = Map->GetMapAssets().GrassMeshes;
	const TArray<TSoftObjectPtr<UStaticMesh>>& ShrubMeshes = Map->GetMapAssets().ShrubMeshes;

	Map->RuntimeDressingGraph = NewObject<UPCGGraph>(Map->DressingPCGComponent, TEXT("RuntimeDressingGraph"), RF_Transient);
	UPCGCreatePointsSettings* CreatePointsSettings = nullptr;
	UPCGStaticMeshSpawnerSettings* SpawnerSettings = nullptr;
	UPCGNode* CreatePointsNode = Map->RuntimeDressingGraph->AddNodeOfType<UPCGCreatePointsSettings>(CreatePointsSettings);
	UPCGNode* SpawnerNode = Map->RuntimeDressingGraph->AddNodeOfType<UPCGStaticMeshSpawnerSettings>(SpawnerSettings);
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

	Map->RuntimeDressingGraph->AddLabeledEdge(
		CreatePointsNode,
		PCGPinConstants::DefaultOutputLabel,
		SpawnerNode,
		PCGPinConstants::DefaultInputLabel);
	Map->RuntimeDressingGraph->AddLabeledEdge(
		SpawnerNode,
		PCGPinConstants::DefaultOutputLabel,
		Map->RuntimeDressingGraph->GetOutputNode(),
		PCGPinConstants::DefaultOutputLabel);

	Map->DressingPCGComponent->SetGraph(Map->RuntimeDressingGraph);
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

