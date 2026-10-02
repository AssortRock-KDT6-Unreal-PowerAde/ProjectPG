#include "Actors/MapBuilder/MapSpawnRegionPlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapTile.h"
#include "Components/MapGeneratorComponent.h"
#include "GameModes/GameModePG.h"
#include "Engine/World.h"

using namespace MapBuild;

void UMapSpawnRegionPlanner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapSpawnRegionPlanner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 시작 구역 고르기.
// ① 0번 = 형님 생성기가 만든 시작 칸.
// ② 후보 = 맵 가장자리 SpawnRegionEdgeBandCells 칸 안(맨 바깥 줄 제외)의 빈 땅(형님 쪽지 None) 칸. "출발은 바깥, 워존은 가운데".
// ③ 빼는 칸 = 출구에서 MinSpawnToExitCells 칸 안(나오자마자 탈출 금지), 건물에서 3칸 안, 호수와 그 둘레 2칸,
//    빈 모서리 상자와 그 둘레 6칸(나중에 보상 거점을 둘 자리 — ProjectTest2 와 같은 칸이 나오게 규칙을 그대로 둔다).
// ④ 이미 고른 구역들과 "가장 가까운 거리" 가 가장 큰 후보를 하나씩 더한다(가장 먼 점 고르기).
//    그 거리가 MinSpawnRegionSpacingCells 보다 작아지면 멈춘다 — 억지로 붙이느니 한 구역 자리를 나눠 쓰는 게 낫다.
// ⑤ 동점은 판 시드 해시로 가른다(같은 시드 = 같은 칸).
void UMapSpawnRegionPlanner::PickSpawnRegions(
	const TMap<FIntPoint, AMapTile*>& TileByCell,
	const TSet<FIntPoint>& BlockedCells,
	int64 RaidSeed)
{
	// 맵이 다시 지어질 때(TryReserveFootprint 재시도) 지난 결과가 쌓이지 않게.
	Map->SpawnRegionCells.Reset();

	// 두 가지 거리: 구역끼리 간격은 체비쇼프(가로·세로 중 큰 쪽 = 대각선도 1칸), 출구까지는 맨해튼(가로+세로).
	auto ChebyshevCells = [](const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	};

	FIntPoint GridMin(TNumericLimits<int32>::Max()), GridMax(TNumericLimits<int32>::Lowest());
	TArray<FIntPoint> SortedCells;
	TileByCell.GetKeys(SortedCells);
	SortedCells.Sort([](const FIntPoint& A, const FIntPoint& B) { return A.X != B.X ? A.X < B.X : A.Y < B.Y; });
	TArray<FIntPoint> ExitCells;
	auto TypeAt = [&TileByCell](const FIntPoint& Cell, ETileType& Out)
	{
		AMapTile* const* Tile = TileByCell.Find(Cell);
		if (!Tile || !IsValid(*Tile))
			return false;
		Out = (*Tile)->GetType();
		return true;
	};
	for (const FIntPoint& Cell : SortedCells)
	{
		GridMin = FIntPoint(FMath::Min(GridMin.X, Cell.X), FMath::Min(GridMin.Y, Cell.Y));
		GridMax = FIntPoint(FMath::Max(GridMax.X, Cell.X), FMath::Max(GridMax.Y, Cell.Y));
		ETileType Type;
		if (!TypeAt(Cell, Type))
			continue;
		if (Type == ETileType::Spawn && Map->SpawnRegionCells.IsEmpty())
			Map->SpawnRegionCells.Add(Cell); // ① 0번 = 형님 시작 칸
		else if (Type == ETileType::Exit)
			ExitCells.Add(Cell);
	}
	if (Map->SpawnRegionCells.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Spawn regions: the logical grid has no spawn cell - nothing to build"));
		return;
	}

	// 호수 위치. 칸 모양 담당과 같은 계산(같은 입력 → 같은 모서리)이라 따로 계산해도 어긋나지 않는다.
	const float LakeRadiusCells = Map->BorderLakeRadiusCells;
	const FBorderLake Lake = GetBorderLake(RaidSeed, GridMin, GridMax, LakeRadiusCells, CollectTraversalCells(TileByCell));

	// 형님 생성기의 시작 구역 상자 크기(_startPositionRangeSize). 형님 코드에 getter 를 더하지 않으려고 이름으로 찾아 읽는다(리플렉션).
	// 이름이 바뀌면 4 로 돌아가니 로그에 값을 남긴다.
	int32 RangeSize = 4;
	if (const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode()))
	{
		const UMapGeneratorComponent* Generator = GameMode->FindComponentByClass<UMapGeneratorComponent>();
		const FIntProperty* RangeProperty = FindFProperty<FIntProperty>(UMapGeneratorComponent::StaticClass(), TEXT("_startPositionRangeSize"));
		if (Generator && RangeProperty)
			RangeSize = FMath::Max(1, RangeProperty->GetPropertyValue_InContainer(Generator));
	}

	// ③ 빈 모서리 상자(길·시작·출구·워존이 하나도 없는 모서리). 호수 모서리는 뺀다.
	const FVector2D MapCentreCell((GridMin.X + GridMax.X) * 0.5f, (GridMin.Y + GridMax.Y) * 0.5f);
	TArray<FIntRect> OutpostCorners;
	FString CornerText;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const bool bMaxX = (Corner & 1) != 0;
		const bool bMaxY = (Corner & 2) != 0;
		if (LakeRadiusCells > 0.0f && (Lake.CentreCell.X > MapCentreCell.X) == bMaxX && (Lake.CentreCell.Y > MapCentreCell.Y) == bMaxY)
			continue;
		const FIntPoint BoxMin(bMaxX ? GridMax.X - RangeSize : GridMin.X, bMaxY ? GridMax.Y - RangeSize : GridMin.Y);
		const FIntPoint BoxMax(bMaxX ? GridMax.X : GridMin.X + RangeSize, bMaxY ? GridMax.Y : GridMin.Y + RangeSize);
		bool bEmpty = true;
		for (int32 X = BoxMin.X; X <= BoxMax.X && bEmpty; ++X)
			for (int32 Y = BoxMin.Y; Y <= BoxMax.Y && bEmpty; ++Y)
			{
				ETileType Type;
				if (TypeAt(FIntPoint(X, Y), Type) && Type != ETileType::None)
					bEmpty = false;
			}
		if (bEmpty)
		{
			OutpostCorners.Add(FIntRect(BoxMin - FIntPoint(6, 6), BoxMax + FIntPoint(6, 6)));
			CornerText += FString::Printf(TEXT("%s%sX%sY"), CornerText.IsEmpty() ? TEXT("") : TEXT(","), bMaxX ? TEXT("+") : TEXT("-"), bMaxY ? TEXT("+") : TEXT("-"));
		}
	}

	// ②③ 후보 모으기
	TArray<FIntPoint> Candidates;
	int32 RejectedByOutpost = 0, RejectedByLake = 0;
	for (const FIntPoint& Cell : SortedCells)
	{
		ETileType Type;
		if (!TypeAt(Cell, Type) || Type != ETileType::None || BlockedCells.Contains(Cell))
			continue;
		const int32 EdgeDistance = FMath::Min(
			FMath::Min(Cell.X - GridMin.X, GridMax.X - Cell.X),
			FMath::Min(Cell.Y - GridMin.Y, GridMax.Y - Cell.Y));
		if (EdgeDistance >= Map->SpawnRegionEdgeBandCells || EdgeDistance < 1)
			continue;
		if (ExitCells.ContainsByPredicate([&Cell, this](const FIntPoint& Exit)
			{
				return FMath::Abs(Exit.X - Cell.X) + FMath::Abs(Exit.Y - Cell.Y) < Map->MinSpawnToExitCells;
			}))
			continue;
		if (Map->FacilityPlacements.ContainsByPredicate([&Cell, &ChebyshevCells](const FFacilityPlacement& Facility)
			{
				return Facility.OccupiedCells.ContainsByPredicate([&Cell, &ChebyshevCells](const FIntPoint& Occupied)
				{
					return ChebyshevCells(Occupied, Cell) < 3;
				});
			}))
			continue;
		if (LakeRadiusCells > 0.0f && FVector2D::Distance(FVector2D(Cell.X, Cell.Y), Lake.CentreCell) <= Lake.Radius + 2.0f)
		{
			++RejectedByLake;
			continue;
		}
		if (OutpostCorners.ContainsByPredicate([&Cell](const FIntRect& Box)
			{
				return Cell.X >= Box.Min.X && Cell.X <= Box.Max.X && Cell.Y >= Box.Min.Y && Cell.Y <= Box.Max.Y;
			}))
		{
			++RejectedByOutpost;
			continue;
		}
		Candidates.Add(Cell);
	}

	// ④ 가장 먼 점 고르기, ⑤ 동점은 시드 해시
	const int32 WantedRegions = FMath::Clamp(Map->SpawnRegionCount, 1, 8);
	int32 SmallestChosenSpacing = TNumericLimits<int32>::Max();
	const uint32 SeedHash = GetTypeHash(RaidSeed);
	while (Map->SpawnRegionCells.Num() < WantedRegions && !Candidates.IsEmpty())
	{
		int32 BestIndex = INDEX_NONE;
		int32 BestSpacing = -1;
		uint32 BestTieBreak = 0;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			int32 Spacing = TNumericLimits<int32>::Max();
			for (const FIntPoint& Chosen : Map->SpawnRegionCells)
				Spacing = FMath::Min(Spacing, ChebyshevCells(Chosen, Candidates[Index]));
			const uint32 TieBreak = HashCombine(SeedHash, GetTypeHash(Candidates[Index]));
			if (Spacing > BestSpacing || (Spacing == BestSpacing && TieBreak > BestTieBreak))
			{
				BestIndex = Index;
				BestSpacing = Spacing;
				BestTieBreak = TieBreak;
			}
		}
		if (BestIndex == INDEX_NONE || BestSpacing < Map->MinSpawnRegionSpacingCells)
			break;
		Map->SpawnRegionCells.Add(Candidates[BestIndex]);
		SmallestChosenSpacing = FMath::Min(SmallestChosenSpacing, BestSpacing);
		Candidates.RemoveAtSwap(BestIndex);
	}

	FString CellList;
	for (const FIntPoint& Cell : Map->SpawnRegionCells)
		CellList += FString::Printf(TEXT("(%d,%d) "), Cell.X, Cell.Y);
	UE_LOG(LogTemp, Display,
		TEXT("Spawn regions: wanted=%d built=%d cells=%smin_spacing_cells=%d exits=%d start_range=%d outpost_corners_kept_clear=[%s] rejected(outpost=%d lake=%d)"),
		WantedRegions, Map->SpawnRegionCells.Num(), *CellList,
		Map->SpawnRegionCells.Num() > 1 ? SmallestChosenSpacing : 0,
		ExitCells.Num(), RangeSize, *CornerText, RejectedByOutpost, RejectedByLake);
}

TSet<FIntPoint> UMapSpawnRegionPlanner::GetExtraSpawnCells() const
{
	TSet<FIntPoint> Extra;
	for (int32 RegionIndex = 1; RegionIndex < Map->SpawnRegionCells.Num(); ++RegionIndex)
		Extra.Add(Map->SpawnRegionCells[RegionIndex]);
	return Extra;
}

// 고른 칸을 "시작 대기소" 로. 게임에서: 두 번째·세 번째·네 번째 플레이어도 풀숲이 아니라 벽 친 대기소에서 나온다.
// 대기소 타일은 입구가 하나(원래 동쪽 면)라서, 워존 가는 흙길이 나가는 방향으로 돌린다. 길이 없으면 맵 가운데 쪽.
// 칸 모양 담당이 지문·길 연결 확인을 끝낸 뒤 부른다(그 확인은 길 타일 기준이라 대기소로 바꾼 칸을 보면 틀렸다고 센다).
void UMapSpawnRegionPlanner::DressExtraSpawnRegions(const FIntPoint& MinCell, const FIntPoint& MaxCell)
{
	for (int32 RegionIndex = 1; RegionIndex < Map->SpawnRegionCells.Num(); ++RegionIndex)
	{
		const FIntPoint RegionCell = Map->SpawnRegionCells[RegionIndex];
		FTileDesignPlacement* Placement = Map->TileDesignPlacements.FindByPredicate(
			[&RegionCell](const FTileDesignPlacement& Candidate) { return Candidate.GridCell == RegionCell; });
		if (!Placement)
			continue;
		Placement->Visual = ETileDesignVisual::Spawn;
		Placement->VisualLevel = TSoftObjectPtr<UWorld>(SpawnLevelPath);
		const uint8 RouteMask = Placement->ConnectionMask;
		uint8 FacingMask = 0;
		for (const uint8 Bit : { NorthConnection, EastConnection, SouthConnection, WestConnection })
			if ((RouteMask & Bit) != 0 && FacingMask == 0)
				FacingMask = Bit;
		if (FacingMask == 0)
		{
			const FIntPoint ToCentre = (MinCell + MaxCell) / 2 - RegionCell;
			FacingMask = FMath::Abs(ToCentre.X) >= FMath::Abs(ToCentre.Y)
				? (ToCentre.X >= 0 ? EastConnection : WestConnection)
				: (ToCentre.Y >= 0 ? NorthConnection : SouthConnection);
		}
		Placement->ConnectionMask = FacingMask;
		Placement->RotationQuarterTurns = FindPositiveYawRotation(EastConnection, FacingMask);
		UE_LOG(LogTemp, Display, TEXT("Spawn region %d at (%d,%d): road mask %d, opening faces %d"),
			RegionIndex, RegionCell.X, RegionCell.Y, RouteMask, FacingMask);
		// 같은 시드면 같은 맵인지 비교하는 지문에도 넣는다.
		Map->LayoutHash = HashCombine(Map->LayoutHash, HashCombine(GetTypeHash(RegionCell), GetTypeHash(Placement->RotationQuarterTurns)));
	}
}
