#include "Actors/MapBuilder/MapRoadPlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapSpawnRegionPlanner.h"
#include "Actors/MapTile.h"
#include "Engine/World.h"

using namespace MapBuild;

void UMapRoadPlanner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapRoadPlanner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 흙길 세 종류를 차례로 깐다: 1) 시설 입구 2) 시작점·출구 → 가까운 도로 3) 시작점·출구 → 워존.
// 같은 시드면 같은 길(갈림길에서 어느 쪽부터 볼지도 시드로 정함).
void UMapRoadPlanner::PlanAccessRoads(
	const TMap<FIntPoint, AMapTile*>& TileByCell,
	const TArray<FIntPoint>& SortedCells,
	const TSet<FIntPoint>& ReservedCells,
	int64 RaidSeed,
	TSet<FIntPoint>& SupplementalRoadCells,
	TMap<FIntPoint, uint8>& SupplementalRoadMasks)
{
	// The senior generator remains authoritative for gameplay regions. The design
	// layer adds deterministic dirt access roads between the authored facilities so
	// POIs do not read as isolated boxes in an otherwise traversable wilderness.
	// These cells become part of the future visual TileManifest, not a mutation of
	// AMapTile's server-owned type.
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
	// 1) 시설 흙길: 시설 입구에서 가장 가까운 도로를 칸 하나씩 넓혀 가며 찾는다(너비 우선 탐색).
	//    게임에서: 큰 건물 입구로 들어가는 짧은 흙길. 6칸(120m)보다 멀면 길을 안 깐다(맵이 흙길투성이가 되지 않게).
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
	if (!Map->FacilityPlacements.IsEmpty())
	{
		const FIntPoint SpurOffsets[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
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
				if (CurrentDepth >= Map->MaxFacilitySpurSearchCells)
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
			if (RoadDistanceCells > Map->MaxFacilitySpurCells)
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
		Map->FacilityPlacements.Num(), FacilitySpurCount, FacilitySpurSkippedCount,
		FacilitySpurCellCount, Map->MaxFacilitySpurCells,
		BudgetedUnpavedCount > 0 ? NearestUnpavedCells : 0,
		BudgetedUnpavedCount > 0 ? static_cast<float>(UnpavedCellsTotal) / BudgetedUnpavedCount : 0.0f,
		FarthestUnpavedCells, UnreachableFacilityCount,
		FacilitySpurCellCount + UnpavedCellsTotal);

	// 시작 구역 고르기(시작 구역 담당). 시설 흙길 다음, 시작점·출구 흙길 전.
	// 건물·언덕·이미 흙길인 칸은 고르지 않는다. 고른 칸은 아래 3) 에서 워존까지 흙길을 받는다.
	TSet<FIntPoint> ExtraSpawnCells;
	{
		TSet<FIntPoint> Blocked = ReservedCells;
		Blocked.Append(SupplementalRoadCells);
		Map->SpawnRegionPlanner->PickSpawnRegions(TileByCell, Blocked, RaidSeed);
		ExtraSpawnCells = Map->SpawnRegionPlanner->GetExtraSpawnCells();
	}

	// The facility tree above does not include server-authored Spawn/Exit cells.
	// Repair each endpoint to the nearest real road through valid non-facility
	// cells, otherwise one seed can leave a spawn pad visually stranded even
	// though the other endpoints happen to touch the generated road network.
	// 2) 시작점·출구 흙길: 시작점/출구에서 가장 가까운 도로까지 잇는다.
	//    게임에서: 시작하자마자 길이 안 보여서 헤매는 일이 없게.
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
				// 시작 구역 칸은 지나가지 않는다(대기소 담장에 입구가 둘 생기면 안 되니까).
				if (ParentByCell.Contains(Neighbor) || ReservedCells.Contains(Neighbor) || ExtraSpawnCells.Contains(Neighbor))
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
	// 3) 시작점·출구 → 워존 흙길: 가까운 도로가 맵 바깥쪽 막다른 길일 수도 있으니, 워존까지 이어지는 길을 하나 보장한다.
	//    게임에서: 시작점에서 길만 따라가면 한가운데 전투 지역에 도착한다.
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
		// 시작 구역 칸도 워존까지 흙길을 받는다.
		if (EndpointType != ETileType::Spawn && EndpointType != ETileType::Exit && !ExtraSpawnCells.Contains(SpawnCell))
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
				// 다른 시작 구역을 지나가면 그 담장에 입구가 둘 필요해진다 — 돌아간다.
				if (ParentByCell.Contains(Neighbor) || ExtraSpawnCells.Contains(Neighbor))
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
}
