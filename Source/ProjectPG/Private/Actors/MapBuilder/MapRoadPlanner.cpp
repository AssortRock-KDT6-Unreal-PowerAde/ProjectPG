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
	// 게임플레이 구역은 팀원이 만든 생성기가 계속 원본이다. 디자인 단계는 손작업 시설 사이에
	// 매번 똑같이 정해지는 흙 진입로를 더해서, 어디든 다닐 수 있는 들판 속에서
	// POI 가 외딴 상자처럼 보이지 않게 한다.
	// 이 칸들은 나중 화면용 TileManifest 에 들어갈 뿐이고,
	// 서버가 가진 AMapTile 의 타입을 바꾸지 않는다.
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
	// 예전에는 시설끼리 '가장 가까운 이웃' 신장 트리로 이었다. 그랬더니 생성기가 만든
	// 도로보다 더 많은 길이 깔렸다 - 원래 도로망에 비해 시설끼리 잇는 길이 약 131칸 -
	// 그리고 그중 플레이어가 실제로 필요한 경로는 하나도 없었다. 바깥 캠프 두 곳 사이에
	// 포장도로는 필요 없다: 땅은 어디나 평평하고 걸을 수 있으니, 그 길들은 생성기가
	// 일부러 짠 도로 배치를 정비용 길 밑에 묻어 버리기만 했다.
	//
	// 진짜 어색해 보이는 건 들어가는 길이 아예 없는 시설이다. 그래서 시설마다 이미 닿을 수
	// 있는 가장 가까운 도로까지 짧은 갈래길 하나만 주고, 거리 제한을 넘으면 포장하지 않는다.
	// 아래의 시작/탈출 끝점 보수와 WarZone 경로 보장은 그대로다
	// - 판을 플레이할 수 있게 만드는 길은 그쪽이다.
	// 1) 시설 흙길: 시설 입구에서 가장 가까운 도로를 칸 하나씩 넓혀 가며 찾는다(너비 우선 탐색).
	//    게임에서: 큰 건물 입구로 들어가는 짧은 흙길. 6칸(120m)보다 멀면 길을 안 깐다(맵이 흙길투성이가 되지 않게).
	int32 FacilitySpurCount = 0;
	int32 FacilitySpurCellCount = 0;
	int32 FacilitySpurSkippedCount = 0;
	// 건너뛴 시설들이 실제로 얼마나 멀었는지. MaxFacilitySpurCells 를 무작정 올리면
	// 예전의 길 과잉 문제가 다시 생긴다(시설끼리 잇는 신장 트리가 원래 도로망 39칸에
	// 131칸을 깐 적이 있다). 그래서 이 숫자로 실제 비용을 확인한 뒤에만 제한값을 바꾼다.
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
			// 높이 올린 시설은 경로를 차지 칸 안 건물 밑이 아니라 경사로 발치에서 끝낸다.
			const FIntPoint StartCell = Facility.AccessCell != FIntPoint::ZeroValue
				? Facility.AccessCell
				: GetFacilityCenterCell(Facility);
			if (!TileByCell.Contains(StartCell))
			{
				++FacilitySpurSkippedCount;
				continue;
			}

			// 너비 우선 탐색이라 갈래길이 구불구불하지 않고 가장 짧은 길이 된다.
			// 3칸 떨어진 도로에 닿으려고 40 m 를 돌아가는 정비용 길이야말로
			// 이 단계가 없애려는 잡음이다.
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
				// 거리 제한은 탐색이 아니라 *결과*에 건다. 탐색을 일찍 끊었더니 제한 바로 밖에
				// 도로가 있는 시설도 "도로 없음"으로 나와서, 실제로 도로망에서 얼마나 먼지
				// 가려졌다.
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
				// 탐색 범위 안 어디에도 도로가 없다 - 거리 제한 문제가 아니라 배치 문제다.
				++FacilitySpurSkippedCount;
				++UnreachableFacilityCount;
				continue;
			}

			const int32 RoadDistanceCells = DepthByCell.FindRef(FoundRoad);
			if (RoadDistanceCells > Map->MaxFacilitySpurCells)
			{
				// 일부러 이렇게 둔다: 도로망에서 이만큼 먼 시설은 빈 땅을 가로질러 긴 길을 끌어오지 않고
				// 포장 없이 둔다. 이름을 남기는 건 *어떤* 시설인지가 중요해서다: 주변 거점, 막사,
				// 참호는 원래 도로가 없는 WarZone 안에 있지만, Downtown 이나 Factory 가 고립되면
				// 진짜 문제다.
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

	// 위의 시설 길은 서버가 정한 시작/탈출 칸을 포함하지 않는다. 각 끝점을
	// 시설이 아닌 유효한 칸을 지나 가장 가까운 실제 도로까지 이어 준다. 안 그러면
	// 다른 끝점들은 우연히 도로망에 닿아도, 어떤 시드에서는 시작 지점 하나가
	// 길 없이 동떨어져 보일 수 있다.
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

	// 시작 지점이 근처 아무 도로에 닿는 것만으론 부족하다: 그 도로가 끊어진 바깥 가지일 수 있다.
	// 그래서 모든 시작 지점에서 가장 가까운 WarZone 경계까지 매번 똑같은 최단 경로를 하나 만든다.
	// 서버가 가진 타일 타입은 바꾸지 않고, 클라이언트 쪽 디자인 manifest 에
	// 끊기지 않고 눈에 잘 보이는 접근로가 있다는 것만 보장한다.
	// 탈출 지점도 같은 보장이 필요하다. 시작 지점만 챙겼더니 탈출 경로가 중심까지 안 가는
	// 막다른 가지로 끝나서, 탈출 지점을 찾은 플레이어가 WarZone 쪽으로
	// 돌아가는 길이 없었다.
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
