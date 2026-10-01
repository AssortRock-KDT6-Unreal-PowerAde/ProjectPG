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
		// This runs from Tick and used to return silently, so a WarZone that came
		// out too small to hold the compound produced an empty world and not one
		// line of explanation. Report it once instead of every frame.
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
	// 3) 마당 3곳: 이미 고른 곳에서 가장 멀리 떨어진 후보를 하나씩 고른다(마당끼리 뭉치지 않게).
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

	// The design contract also needs genuinely multi-cell shapes, not only square
	// compounds. Reserve two 2x1 interiors and one 4x1 linear encounter while the
	// brother's WarZone cells remain the authoritative eligibility mask.
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

	// Reserve three themed districts outside the combat core.  They deliberately
	// consume ordinary None cells: the brother's metaball/road result stays the
	// authority, while the design manifest replaces only open visual cells with a
	// multi-cell POI.  Roads to each entrance are added by BuildTileDesignPlacements.
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
		Map->BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
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

