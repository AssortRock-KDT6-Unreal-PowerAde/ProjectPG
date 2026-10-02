#include "Actors/MapBuilder/MapTilePlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapRoadPlanner.h"
#include "Actors/MapBuilder/MapSpawnRegionPlanner.h"
#include "Actors/MapTile.h"
#include "GameModes/GameModePG.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"

using namespace MapBuild;

void UMapTilePlanner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapTilePlanner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 순서: 1) 언덕 자리 2) 맵 가장자리·시드 3) 흙길(흙길 담당) 4) 칸마다 모양 5) 지문 6) 길 연결 확인.
// 언덕과 건물이 먼저 칸을 차지해야 흙길이 그 칸을 피해 가고, 흙길이 정해져야 칸 모양(길 타일 종류)이 정해진다.
void UMapTilePlanner::BuildTileDesignPlacements(
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	Map->TileDesignPlacements.Reset();
	TSet<FIntPoint> ReservedCells;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
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
	// 1) 언덕: 들판·워존 바깥쪽에 흙 언덕을 최대 28개 솟게 하고 그 위에 돌·나무·덤불을 뿌린다.
	//    언덕이 차지한 칸은 "예약됨"(ReservedCells)이 돼서 평평한 타일을 안 깐다.
	for (UHierarchicalInstancedStaticMeshComponent* FeatureComponent : Map->TerrainFeatureHISMs)
	{
		if (IsValid(FeatureComponent))
			FeatureComponent->ClearInstances();
	}
	for (UHierarchicalInstancedStaticMeshComponent* DressingComponent :
		{ Map->TerrainRockHISM.Get(), Map->TerrainTreeHISM.Get(), Map->TerrainBushHISM.Get() })
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
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
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
			Map->TerrainFeatureHISMs.IsValidIndex(ComponentIndex) ? Map->TerrainFeatureHISMs[ComponentIndex] : nullptr;
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
		ScatterOnFeature(Map->TerrainRockHISM,
			bIndustrial ? (bHollow ? 5 : 8) : (bHollow ? 3 : 6), 0.55f, 1.25f, 18.0f);
		ScatterOnFeature(Map->TerrainTreeHISM,
			bIndustrial ? 0 : (bHollow ? 5 : 3), 0.85f, 1.35f, 12.0f);
		ScatterOnFeature(Map->TerrainBushHISM,
			bIndustrial ? 4 : (bHollow ? 11 : 8), 0.70f, 1.30f, 6.0f);
		++TerrainFeatureCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Terrain features: placed=%d kinds=%d"),
		TerrainFeatureCount, FeatureMeshes.Num());
	// 2) 맵 가장자리 칸 번호(MinCell~MaxCell)와 판 시드. 가장자리 숲·호수 위치에 쓴다.
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

	// 흙길: 시설 입구·시작점·출구가 도로나 워존에 이어지도록 흙길 칸을 정한다(흙길 담당).
	// 결과 두 개: 흙길이 될 칸 목록, 그리고 칸마다 어느 쪽으로 이어지는지(연결 번호 N=1,E=2,S=4,W=8).
	TSet<FIntPoint> SupplementalRoadCells;
	TMap<FIntPoint, uint8> SupplementalRoadMasks;
	Map->RoadPlanner->PlanAccessRoads(
		TileByCell, SortedCells, ReservedCells, RaidSeed, SupplementalRoadCells, SupplementalRoadMasks);

	// 4) 칸마다 모양 정하기. 게임에서 보이는 결과:
	//    도로 칸 → 이웃 연결 수로 직선/꺾임/T자/십자/막다른 길 타일 + 회전,
	//    시작점 → 벽 친 대기소(입구가 워존 가는 길 쪽), 출구·장애물 → 검문소, 워존 → 워존 땅,
	//    나머지 → 호수/가장자리 숲/미로 숲 띠/풀밭·덤불·바위 등(5×5칸 단위로 묶어서 숲 띠·공터처럼 보이게).
	// Counted while the placements are built below; declared here so the summary
	// that follows the placement loop can report them together.
	int32 SpawnOpeningCount = 0;
	int32 SpawnRoutedOpenings = 0;
	int32 WalledOffSpawnConnections = 0;

	// One lake for the whole pass; the corner is scored against the road network,
	// so this must not be re-derived per cell.
	const FBorderLake BorderLake = GetBorderLake(
		RaidSeed, MinCell, MaxCell, Map->BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	Map->BorderLakeCentreCell = BorderLake.CentreCell;
	Map->bHasBorderLake = Map->BorderLakeRadiusCells > 0.0f;

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

		FTileDesignPlacement& Placement = Map->TileDesignPlacements.AddDefaulted_GetRef();
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
			if (Map->BorderLakeRadiusCells > 0.0f
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
		SupplementalRoadCells.Num(), Map->FacilityPlacements.Num());

	// 5) 지문(LayoutHash): 칸 모양 전부를 숫자 하나로. 같은 시드면 같은 숫자 → 로그로 "같은 맵" 인지 확인.
	Map->LayoutHash = 0;
	TMap<FIntPoint, const FTileDesignPlacement*> PlacementByCell;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		PlacementByCell.Add(Placement.GridCell, &Placement);
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.GridCell.X));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.GridCell.Y));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(static_cast<uint8>(Placement.Visual)));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.ConnectionMask));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.bSupplementalAccessRoad));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.LayoutVariant));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	// routed_openings should equal openings: every spawn's single doorway ought to
	// face the road the WarZone guarantee actually planned. walled_connections is
	// the count of roads that still dead-end against a spawn's wall - the tile has
	// one opening, so anything above zero is a road the player can see but not use.
	UE_LOG(LogTemp, Display,
		TEXT("Spawn opening alignment: spawns=%d routed_openings=%d walled_connections=%d"),
		SpawnOpeningCount, SpawnRoutedOpenings, WalledOffSpawnConnections);

	// 6) 길 연결 확인: 길 타일 회전이 연결 번호와 맞는지, 옆 칸도 나를 향해 열려 있는지 센다(0이어야 정상).
	//    게임에서: 0이 아니면 길이 벽에 막혀 끊겨 보인다.
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
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
	{
		Map->LayoutHash = HashCombine(Map->LayoutHash, FCrc::StrCrc32(*Placement.FacilityId.ToString()));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(static_cast<uint8>(Placement.VisualSet)));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.AnchorCell.X));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.AnchorCell.Y));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.Footprint.X));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.Footprint.Y));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	// 7) 시작 구역 칸을 "시작 대기소" 로 바꾼다(시작 구역 담당). 지문·길 연결 확인 뒤에 해야 한다
	//    (그 확인은 길 타일 기준이라, 대기소로 바꾼 칸을 보면 틀렸다고 센다).
	Map->SpawnRegionPlanner->DressExtraSpawnRegions(MinCell, MaxCell);

	UE_LOG(LogTemp, Display,
		TEXT("Design placement spec: cells=%d reserved=%d invalid_rotations=%d map_extent_cm=%.0f layout_hash=%08X"),
		Map->TileDesignPlacements.Num(), ReservedCells.Num(), InvalidRotationCount,
		Map->GridCellSpan * DesignCellSize, Map->LayoutHash);
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

