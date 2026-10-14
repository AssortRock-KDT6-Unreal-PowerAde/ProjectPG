#include "Actors/MapBuilder/MapSpawnRegionPlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapTile.h"
#include "Components/MapGeneratorComponent.h"
#include "GameModes/GameModePG.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Components/StaticMeshComponent.h"

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

	// 형님 생성기의 시작 구역 상자 크기(서버 = 생성기, 들어온 사람 = 설계도). 로그에 값을 남긴다.
	const int32 RangeSize = Map->GetStartRangeSize();

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

// 플레이어를 시작 구역에 세우기 (ProjectTest2 PlaceJoinedPlayers 를 옮김, 그쪽 전용 부품 줄은 뺐다).
// 언제부터: 시작 자리 끼임 정리(ResolveGameplayPointSafety)가 끝난 뒤 — 그 전에는 자리가 벽 속일 수 있다.
// ProjectTest2 와 다른 점(10/2): 거기는 "기본 폰(날아다니는 카메라)이면 진짜 캐릭터가 올 때까지 기다림" 이었다.
//   ProjectPG 의 맵 테스트 게임모드(AGameModePG)는 캐릭터를 끝까지 안 줘서 영원히 기다렸다(리슨 서버 + 2명 시험).
//   그래서 폰 종류와 상관없이 먼저 세우고, 나중에 게임모드가 캐릭터를 새로 주면(폰이 바뀌면) 같은 자리에 다시 세운다.
void UMapSpawnRegionPlanner::PlaceJoinedPlayers()
{
	UWorld* World = GetWorld();
	// 서버만. 맵 액터는 복제를 안 해서 클라에서도 HasAuthority() 가 참이라, 넷 모드로 거른다.
	if (!IsValid(World) || World->GetNetMode() == NM_Client
		|| !Map->bResolvedGameplayPointSafety || Map->SpawnRegionCells.IsEmpty())
		return;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!IsValid(PlayerController))
			continue;
		APawn* Pawn = PlayerController->GetPawn();

		// 이미 번호를 받은 사람: 폰이 바뀌었을 때만 같은 자리에 다시 세운다.
		if (const int32* Assigned = JoinIndexByPlayer.Find(PlayerController))
		{
			if (IsValid(Pawn) && PlacedPawnByPlayer.FindRef(PlayerController).Get() != Pawn)
				PlaceAtSeat(PlayerController, Pawn, *Assigned);
			continue;
		}

		// 캐릭터가 아예 없는 사람 = 게임모드가 캐릭터를 못 만든 경우. 막 들어온 사람은 곧 만들 수도 있으니 2초 기다린다.
		// 관전만 하는 사람은 세우지 않는다.
		if (!IsValid(Pawn))
		{
			if (PlayerController->PlayerState && PlayerController->PlayerState->IsOnlyASpectator())
				continue;
			const double Now = World->GetTimeSeconds();
			if (Now - PawnlessSinceSeconds.FindOrAdd(PlayerController, Now) < 2.0)
				continue;
		}

		const int32 JoinIndex = NextPlayerJoinIndex;
		if (!PlaceAtSeat(PlayerController, Pawn, JoinIndex))
		{
			// 이 자리에 캐릭터를 못 만들었으면 2초 뒤 다시(같은 번호·같은 자리).
			PawnlessSinceSeconds.Add(PlayerController, World->GetTimeSeconds());
			continue;
		}
		++NextPlayerJoinIndex;
		JoinIndexByPlayer.Add(PlayerController, JoinIndex);
		PawnlessSinceSeconds.Remove(PlayerController);
	}
}

// 번호 → 구역·자리: 구역 = 번호 % 구역 수, 자리 = 번호 / 구역 수. 4명이면 사람마다 다른 구역, 5번째부터 1구역의 다음 자리.
bool UMapSpawnRegionPlanner::PlaceAtSeat(APlayerController* PlayerController, APawn* Pawn, int32 JoinIndex)
{
	UWorld* World = GetWorld();
	const int32 RegionCount = Map->SpawnRegionCells.Num();
	const int32 RegionIndex = JoinIndex % RegionCount;
	const int32 SeatIndex = JoinIndex / RegionCount;
	const FIntPoint RegionCell = Map->SpawnRegionCells[RegionIndex];

	// 그 구역의 자리(시작 지점 4개).
	TArray<const FLevelDesignPoint*> Seats;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
		if (Point.Type == ELevelDesignPointType::Spawn && Point.GridCell == RegionCell)
			Seats.Add(&Point);
	if (Seats.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Spawn assign: region %d at (%d,%d) has no seats - %s left where the game mode put it"),
			RegionIndex, RegionCell.X, RegionCell.Y, *GetNameSafe(PlayerController));
		PlacedPawnByPlayer.Add(PlayerController, Pawn);
		return true;
	}

	const FLevelDesignPoint& Seat = *Seats[SeatIndex % Seats.Num()];
	// 기본은 맵 가운데를 본다. 시작 대기소면 입구 쪽 트인 방향으로 바꾼다(벽 보고 시작 금지).
	const FVector ToCentre = (Map->GetActorLocation() - Seat.WorldLocation).GetSafeNormal2D();
	float Yaw = ToCentre.IsNearlyZero() ? (IsValid(Pawn) ? Pawn->GetActorRotation().Yaw : 0.0f) : ToCentre.Rotation().Yaw;
	GetSpawnDoorwayYaw(RegionCell, Seat.WorldLocation, Yaw, Pawn);
	const FVector SeatLocation = Seat.WorldLocation + FVector(0.0f, 0.0f, 100.0f);
	bool bSpawnedHere = false;
	if (!IsValid(Pawn))
	{
		// 형님 게임모드는 안 고치고, 게임모드의 공개 함수로 우리가 정한 자리에 그 사람 캐릭터를 만들게 한다.
		if (AGameModeBase* GameMode = World->GetAuthGameMode())
			GameMode->RestartPlayerAtTransform(PlayerController, FTransform(FRotator(0.0f, Yaw, 0.0f), SeatLocation));
		Pawn = PlayerController->GetPawn();
		if (!IsValid(Pawn))
		{
			UE_LOG(LogTemp, Warning, TEXT("Spawn assign: game mode could not spawn a character for %s at seat %s - retrying in 2s"),
				*GetNameSafe(PlayerController), *Seat.PointId.ToString());
			return false;
		}
		bSpawnedHere = true;
	}
	if (!bSpawnedHere)
		Pawn->SetActorLocationAndRotation(SeatLocation, FRotator(0.0f, Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
	// 원격 플레이어의 시선은 그 사람 컴퓨터가 가진다 — 서버에서 돌리기만 하면 그 사람 화면은 안 돈다. 클라 RPC 로 보낸다.
	PlayerController->ClientSetRotation(FRotator(-10.0f, Yaw, 0.0f));
	PlacedPawnByPlayer.Add(PlayerController, Pawn);

	FString Blocker = TEXT("nothing");
	const float OpenAheadCm = MeasureOpenAhead(Seat.WorldLocation, Yaw, 4000.0f, Pawn, &Blocker);
	UE_LOG(LogTemp, Display, TEXT("Spawn assign: player=%s pawn=%s join=%d region=%d/%d cell=(%d,%d) seat=%s yaw=%.0f open_ahead=%.0fm (then %s)%s"),
		*GetNameSafe(PlayerController), *GetNameSafe(Pawn->GetClass()), JoinIndex, RegionIndex, RegionCount,
		RegionCell.X, RegionCell.Y, *Seat.PointId.ToString(), Yaw, OpenAheadCm * 0.01f, *Blocker,
		bSpawnedHere ? TEXT(" (character spawned here - game mode had none)") : TEXT(""));
	return true;
}

// 시작 대기소 타일은 담장 한 면(원래 동쪽 +X)만 뚫려 있고, 입구가 흙길을 보게 RotationQuarterTurns 만큼 돌려 세웠다.
// 그래서 입구 방향 = 0도 + 회전 × 90도. 그 방향 ±45도 안에서 가장 멀리 트인 쪽을 본다(자리가 문 옆으로 치우쳐 있어서).
bool UMapSpawnRegionPlanner::GetSpawnDoorwayYaw(const FIntPoint& Cell, const FVector& From, float& OutYaw, const AActor* Ignore) const
{
	const FTileDesignPlacement* Placement = Map->TileDesignPlacements.FindByPredicate(
		[&Cell](const FTileDesignPlacement& Candidate) { return Candidate.GridCell == Cell && Candidate.Visual == ETileDesignVisual::Spawn; });
	if (!Placement)
		return false;
	const float OpeningYaw = FRotator::NormalizeAxis(Placement->RotationQuarterTurns * 90.0f);
	float BestYaw = OpeningYaw;
	float BestOpen = MeasureOpenAhead(From, OpeningYaw, 4000.0f, Ignore);
	for (int32 Step = 1; Step <= 9; ++Step)
	{
		for (const float Sign : { -1.0f, 1.0f })
		{
			const float Yaw = OpeningYaw + Sign * Step * 5.0f;
			const float Open = MeasureOpenAhead(From, Yaw, 4000.0f, Ignore);
			// 거의 같게 트였으면(50cm 안) 입구 방향에 가까운 쪽을 남긴다.
			if (Open > BestOpen + 50.0f)
			{
				BestOpen = Open;
				BestYaw = Yaw;
			}
		}
	}
	OutYaw = FRotator::NormalizeAxis(BestYaw);
	return true;
}

float UMapSpawnRegionPlanner::MeasureOpenAhead(const FVector& From, const float Yaw, const float MaxCm, const AActor* Ignore, FString* OutBlocker) const
{
	const UWorld* World = GetWorld();
	if (!World)
		return 0.0f;
	// 자리 점은 땅 +120cm, 재는 선은 그 위 60cm = 눈높이.
	const FVector Eye = From + FVector(0.0f, 0.0f, 60.0f);
	const FVector To = Eye + FRotator(0.0f, Yaw, 0.0f).Vector() * MaxCm;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MapSpawnOpenAhead), false, Ignore);
	if (!World->LineTraceSingleByChannel(Hit, Eye, To, ECC_Visibility, Params))
		return MaxCm;
	if (OutBlocker)
	{
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Hit.GetComponent());
		*OutBlocker = FString::Printf(TEXT("%s/%s mesh=%s at %s"), *GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()),
			Mesh ? *GetNameSafe(Mesh->GetStaticMesh()) : TEXT("-"), *Hit.ImpactPoint.ToCompactString());
	}
	return Hit.Distance;
}
