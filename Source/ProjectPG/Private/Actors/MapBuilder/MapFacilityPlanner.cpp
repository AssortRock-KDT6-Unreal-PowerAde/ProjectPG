#include "Actors/MapBuilder/MapFacilityPlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/MapTile.h"
#include "GameModes/GameModePG.h"
#include "Engine/World.h"

// 맵 cpp 와 같은 도우미(호수 위치, 시설 레벨 경로, 태그)를 이름 상자째로 쓴다.
using namespace MapBuild;

void UMapFacilityPlanner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapFacilityPlanner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 큰 건물 자리 고르기. 게임에서: 판이 시작되면 맵 한가운데 워존에 공장 단지(3×5)가 서고,
// 그 둘레에 마당 3곳·막사 2곳·참호 1곳, 장애물 옆 도로에 검문소, 바깥쪽에 다운타운·공장·호숫가 마을이 선다.
// 그 "어디" 를 여기서 정한다. 같은 시드면 항상 같은 자리.
// 순서가 중요: 큰 것부터 자리를 잡고(공장 단지 → 마당 → 막사·참호 → 검문소 → 바깥 동네),
// 이미 잡힌 칸(SelectedFacilityCells) 둘레는 비워 둬서 건물끼리 붙지 않게 한다.
bool UMapFacilityPlanner::PlanFacilities(
	const TMap<FIntPoint, AMapTile*>& TileByCell,
	const TMap<FIntPoint, AMapTile*>& WarZoneByCell,
	int32 AllTileCount)
{
	// 1) 워존 칸 중 2×2 마당이 들어갈 수 있는 자리(Candidates)와 공장 단지(3×5)가 들어갈 자리(CompoundCandidates)를 모은다.
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
		// 이 함수는 Tick 에서 불리는데 예전엔 조용히 return 했다. 그래서 WarZone 이 너무 작아
		// 중심 시설이 안 들어가면 텅 빈 세계만 나오고 이유는 한 줄도 안 남았다.
		// 이제는 매 프레임이 아니라 한 번만 알린다.
		if (!bLoggedMissingWarZoneFootprint)
		{
			bLoggedMissingWarZoneFootprint = true;
			UE_LOG(LogTemp, Error,
				TEXT("WarZone footprint unavailable: tiles=%d warzone_cells=%d fits_2x2=%d fits_core=%d ")
				TEXT("grid_step=%.1f - generation cannot proceed without a %dx%d WarZone block"),
				AllTileCount, WarZoneByCell.Num(), Candidates.Num(), CompoundCandidates.Num(), Map->GridStep,
				WarZoneCoreFootprint.X, WarZoneCoreFootprint.Y);
		}
		return false;
	}

	// 2) 맵 가운데에 가까운 순으로 줄 세운다. 공장 단지는 워존 한가운데에 서야 하니까.
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
		// 기준점(anchor)이 아니라 차지 칸의 중심끼리 비교한다. 그래야 가장 큰 시설이
		// 실제로 원본 WarZone 한가운데를 차지한다.
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

	// 3x3 중심 시설 하나를 먼저 잡고, 서로 충분히 떨어진 2x2 주변 거점 여러 개를 잡는다.
	// AMapTile 이 계속 논리 원본이다. 나중 서버 manifest 에는 기준점, 차지 칸,
	// 회전, 모양 종류, 고정 시드만 있으면 된다.
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
	// 3) 마당 WarZoneYardCount 곳(기본 3): 이미 고른 곳에서 가장 멀리 떨어진 후보를 하나씩 고른다(마당끼리 뭉치지 않게).
	while (SelectedFacilityAnchors.Num() < Map->WarZoneYardCount)
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
		return false;

	Map->AnchorCell = CompoundAnchor;
	Map->ReservedTiles.Reset();
	Map->FacilityPlacements.Reset();
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

	// 기획상 정사각형 구역만이 아니라 진짜로 여러 칸에 걸친 모양도 필요하다.
	// 2x1 실내 두 개와 4x1 일자 교전 장소 하나를 잡는다. 어디에 놓을 수 있는지는
	// 팀원의 WarZone 칸이 계속 원본 기준이다.
	// 4) 막사(2×1, 1×2)와 참호(4×1): 가운데에 가까운 워존 빈자리 중 처음 맞는 곳.
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

	// 5) 검문소(1×2): 장애물 칸과 도로 칸이 붙어 있는 곳 중 맵 가운데에 가장 가까운 곳. 게임에서: 길을 막는 검문소.
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

	// 전투 중심 밖에 테마 구역 세 개를 잡는다. 일부러 보통 None 칸을 쓴다:
	// 팀원의 metaball/도로 결과가 원본으로 남고, 디자인 manifest 는
	// 비어 있는 화면용 칸만 여러 칸짜리 POI 로 바꾼다.
	// 각 입구까지 가는 길은 BuildTileDesignPlacements 가 붙인다.
	// 6) 바깥 동네 3곳(다운타운 6×6, 공장 2×2, 호숫가 마을 2×2)의 목표 지점을 정하고, 도로에서 너무 멀지 않은 빈 칸을 고른다.
	//    맵 크기(GridCellSpan)도 여기서 재서 맵에 적어 둔다(길찾기 바닥판 크기에 씀).
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
	Map->GridCellSpan = FMath::Max(MaxCellX - MinCellX + 1, MaxCellY - MinCellY + 1);

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
	// 시골 마을은 호숫가 작은 마을이다 - 오두막, 부두, 노 젓는 보트 두 척.
	// 마른 들판 한가운데 두면 누가 갖다 놓은 디오라마처럼 보인다 - 첫 배치가 딱 그랬다.
	// 그래서 가장자리 호수 물가 선 바로 안쪽 땅을 노려서, 부두가 물에 닿고
	// 보트가 떠 있을 곳이 있게 한다. ReserveThemedDistrict 는 이 점에서 가장 가까운
	// 놓을 수 있는 2x2 를 고르고, 물 채우기는 예약된 칸을 건드리지 않으므로
	// 마을 자체는 항상 마른 땅 위에 놓인다.
	const int64 LakeRaidSeed = Map->GetRaidSeed(); // 서버 = 게임모드 시드, 들어온 사람 = 설계도로 받은 시드
	const FBorderLake RuralLake = GetBorderLake(
		LakeRaidSeed, FIntPoint(MinCellX, MinCellY), FIntPoint(MaxCellX, MaxCellY),
		Map->BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	const FVector2D InlandDirection =
		(FVector2D((MinCellX + MaxCellX) * 0.5f, (MinCellY + MaxCellY) * 0.5f)
			- RuralLake.CentreCell).GetSafeNormal();
	const FVector2D RuralShorePoint =
		RuralLake.CentreCell + InlandDirection * (RuralLake.Radius + 1.5f);
	const FIntPoint RuralTarget(
		FMath::Clamp(FMath::RoundToInt(RuralShorePoint.X), MinCellX, MaxCellX),
		FMath::Clamp(FMath::RoundToInt(RuralShorePoint.Y), MinCellY, MaxCellY));
	// 모든 칸의 '가장 가까운 도로까지 칸 수'. 도로망 전체에서 동시에 퍼지는 너비 우선 탐색 한 번으로 구한다.
	// 예전 시설 배치는 테마 목표 위치까지 거리만 점수로 봐서, 시설들이 도로에서 평균 13칸
	// 떨어진 곳에 놓였다. 그러면 진입로 단계가 시설마다 260 m 짜리 길을 끌어와야 했고
	// - 아니면 포기했는데, 시설 11개 중 7~10개에서 포기했다.
	// 길을 나중에 놓는 건 순서가 틀렸다. 처음부터 도로 근처에 앉히면 공짜다.
	TMap<FIntPoint, int32> RoadDistanceByCell;
	{
		TArray<FIntPoint> Frontier;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
		{
			if (!IsValid(Pair.Value))
				continue;
			// WarZone 은 지나갈 수 있는 곳이지만 일부러 뺀다: 넓은 지역이라
			// 도로로 치면 맵 가운데 대부분이 '도로 옆'이 되어
			// 이 점수 항목 자체가 의미 없어진다.
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
			// 갈래길이 닿지 못하는 만큼의 도로 거리만 점수에서 깎는다.
			// 갈래길 거리 안이면 어차피 포장 진입로가 공짜로 생기므로, 거기에 벌점을 주면
			// 괜히 구역만 밀려난다. 거리를 넘으면 비용이 제곱으로 커진다 - 목표 위치 항목과 같은
			// 모양이라 둘을 서로 비교할 수 있다. DX/DY 는 반 칸 단위라서 *4 를 한다.
			const int32 RoadCells = RoadDistanceByCell.Contains(Candidate)
				? RoadDistanceByCell[Candidate]
				: Map->MaxFacilitySpurSearchCells;
			const int64 RoadExcess = FMath::Max(0, RoadCells - Map->MaxFacilitySpurCells);
			const int64 Score = DX * DX + DY * DY
				+ RoadExcess * RoadExcess * 4 * Map->FacilityRoadProximityWeight;
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
		// road_cells 는 진입로 단계가 이어야 할 거리다. MaxFacilitySpurCells 이하면
		// 포장되고, 넘으면 그 구역은 길 없이 고립된다
		// - 이 점수 항목이 막으려는 바로 그 상황이다.
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

	// 마지막 로그("Reserved facility set")에 쓸 값을 적어 둔다. 맵이 공사를 다 시킨 뒤 찍는다.
	CompoundAnchorResult = CompoundAnchor;
	CampCountResult = SelectedFacilityAnchors.Num();
	bFoundCheckpointResult = bFoundCheckpoint;
	return true;
}

// 건물 하나를 목록에 올린다. 게임에서: "여기 창고, 입구는 도로 쪽" 을 정하는 일.
// 이름표(FacilityId), 높이(언덕 위/아래), 입구 방향, 쓸 레벨 파일을 정하고 차지한 칸 쪽지에 "예약됨" 태그를 붙인다.
void UMapFacilityPlanner::ReserveFacility(
	EFacilityVisualSet VisualSet,
	const FIntPoint& Anchor,
	const FIntPoint& Footprint,
	int32 RotationQuarterTurns,
	const TArray<FIntPoint>& OccupiedCells,
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	FFacilityPlacement& Placement = Map->FacilityPlacements.AddDefaulted_GetRef();
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
	const int64 RaidSeed = Map->GetRaidSeed();
	Placement.LocalSeed = static_cast<int64>(HashCombine(
		GetTypeHash(RaidSeed),
		HashCombine(GetTypeHash(Anchor.X), HashCombine(GetTypeHash(Anchor.Y), GetTypeHash(static_cast<uint8>(VisualSet))))));
	const bool bLoweredVariant = (Placement.LocalSeed & 1ll) != 0;

	if (VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint)
	{
		// WarZoneStronghold 가 아니라 Ground: +220 단상과 경사로 네 개는 코드로 만든 중심 시설의
		// 윤곽과 진입로를 위한 것이었다. 가져온 공장 구역은 평평하게 만들어져 있어서, 들어 올리면
		// 마당 울타리와 드럼통 줄이 단상 가장자리 밖으로 떠 버린다
		// - 예전에 시골 섬이 자기 단상 밖으로 삐져나왔던 것처럼.
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
		// 손작업 블록은 공통 높이 바로 위 자기 포장면에 서 있으므로,
		// 그 아래 바닥판도 그 높이에 있어야 한다. 160 cm 올리면 카페 테라스가
		// 받침대 위에 올라가고 연석이 가장자리 밖으로 삐져나온다.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}
	else if (VisualSet == EFacilityVisualSet::FactoryConstruction)
	{
		// 손작업 공장 레벨은 자체 바닥판을 지우고 건물 밑면을 로컬 Z=0 에 맞춰 다듬었다.
		// 그 아래 바닥판을 올리거나 내리면 건물이 주변 땅이 아니라
		// 턱 위에 서 있게 된다.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}
	else if (VisualSet == EFacilityVisualSet::RuralHideout)
	{
		// 자체 땅을 지웠으므로 오두막들은 공통 지형 위에 선다.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}

	// 생성기가 만든 '지나갈 수 있는 칸' 중 가장 가까운 쪽을 향한 가장자리를 고른다.
	// 매번 똑같이 정해지고 시설과 함께 저장되므로, 보이는 경사로,
	// 진입로 끝점, 나중 서버 manifest 가 모두 같은 답을 갖는다.
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
		// 코드로 만든 시설은 로컬 기준 북/남 입구를 갖는다. 로컬 남쪽을 도로를 향한
		// 가장자리로 돌려서 경사로가 벽에 막히지 않게 한다.
		if (Placement.EntranceDirection == FIntPoint(0, -1)) Placement.RotationQuarterTurns = 0;
		else if (Placement.EntranceDirection == FIntPoint(1, 0)) Placement.RotationQuarterTurns = 1;
		else if (Placement.EntranceDirection == FIntPoint(0, 1)) Placement.RotationQuarterTurns = 2;
		else if (Placement.EntranceDirection == FIntPoint(-1, 0)) Placement.RotationQuarterTurns = 3;
	}

	// 시설 레벨은 DA_MapAssets(보이는 것 목록)에서 고른다.
	const UMapAssetSet& Assets = Map->GetMapAssets();
	FName FacilityTag = WarehouseTag;
	Placement.FacilityLevel =
		VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint
			? Assets.WarZoneCoreLevel : Assets.WarehouseLevel;
	if (VisualSet == EFacilityVisualSet::Yard)
	{
		FacilityTag = YardTag;
		Placement.FacilityLevel = Assets.YardLevel;
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
		Placement.FacilityLevel = Assets.CheckpointLevel;
	}
	else if (VisualSet == EFacilityVisualSet::DowntownBlock)
	{
		FacilityTag = DowntownTag;
		Placement.FacilityLevel = Assets.DowntownLevel;
	}
	else if (VisualSet == EFacilityVisualSet::FactoryConstruction)
	{
		FacilityTag = FactoryConstructionTag;
		Placement.FacilityLevel = Assets.FactoryLevel;
	}
	else if (VisualSet == EFacilityVisualSet::RuralHideout)
	{
		FacilityTag = RuralHideoutTag;
		Placement.FacilityLevel = Assets.RuralHideoutLevel;
	}
	if (Map->bUseRuntimeBlueprintTiles && !FacilityUsesAuthoredLevel(VisualSet, Footprint))
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
		Map->ReservedTiles.Add(Tile);
		Placement.OccupiedCells.Add(Cell);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Facility mapping: %s -> %s (Anchor=%d,%d Footprint=%d,%d Rotation=%d)"),
		*FacilityTag.ToString(),
		*Placement.FacilityLevel.ToSoftObjectPath().ToString(),
		Anchor.X, Anchor.Y, Footprint.X, Footprint.Y,
		RotationQuarterTurns * 90);
}

