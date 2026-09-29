// AWarZoneFootprintPreview — 여러 시작 지역(멀티 최대 4명)과 들어온 플레이어 배정.
//
// 무엇: 논리 격자(팀원 UMapGeneratorComponent)는 시작 칸을 딱 1개 만든다. 멀티(최대 4명, 각자 적 — 타르코프식)에서
//   모두가 한 칸에서 출발하면 시작하자마자 싸움이 난다. 그래서 우리 층에서 시작 지역을 더 고르고, 서버가 들어온 사람마다 나눠 준다.
// 왜 생성기를 안 고치나: 팀원 코드는 고치지 않는다(작업 규칙). 생성기가 정한 칸 종류(길·출구·워존)는 그대로 두고,
//   그 결과로 만든 설계도(TileDesignPlacements)만 읽어 "시작 지역" 게임 지점을 더한다. 출구 칸을 가져다 쓰지 않으므로 탈출구가 줄지 않는다.
// 고르는 규칙 (BuildSpawnRegions) — 9/28 다시 짬(사용자 PIE: "스폰 지역이 길 생성이 안 되고, 에픽템 장소 바로 뒤에 생긴다"):
//   고르는 때: 설계도의 길을 깔기 "전" (BuildTileDesignPlacements 안, 시설 진입로 다음). 그래야 고른 칸에서 워존까지 길을
//     기존 "시작·출구 → 워존 길 보장" 과 같은 방법으로 깔 수 있다. 그래서 겉모습이 아니라 논리 칸 종류로 고른다.
//   ① 0번 지역 = 논리 격자의 시작 칸.
//   ② 후보 = 맵 가장자리 SpawnRegionEdgeBandCells 칸 안(맨 바깥 줄 제외)의 빈 땅(논리 None) 칸. "출발은 바깥, 워존은 가운데".
//   ③ 빼는 칸 = 출구에서 MinSpawnToExitCells 칸 안(태어나자마자 탈출 금지), 시설에서 3칸 안, 지형 굴곡 칸, 이미 진입로가 지나는 칸,
//      호수와 그 둘레 2칸, 그리고 **빈 모서리와 그 둘레 6칸** — 빈 모서리(길·시작·출구·워존이 없는 모서리 상자)는
//      외진 보상 거점(에픽템) 자리다. 시작 지역이 거기 붙으면 나오자마자 에픽템을 줍는다(PlaceRemoteOutpost 주석).
//   ④ 이미 고른 지역들과의 "가장 가까운 거리" 가 가장 큰 후보를 하나씩 더한다(가장 먼 점 고르기).
//      그 거리가 MinSpawnRegionSpacingCells 보다 작아지면 멈춘다 — 억지로 붙여 놓느니 한 지역에 자리를 나눠 쓰는 게 낫다.
//   ⑤ 동점은 판 시드 해시로 가른다 → 서버·클라가 같은 설계도로 같은 칸(시간·로컬 난수 안 씀).
//   고른 뒤: 추가 지역마다 워존까지 길을 깐다(설계도 쪽). 담장 입구는 그 길이 나가는 쪽을 본다.
// 배정 (PlaceJoinedPlayers, 서버만):
//   들어온 순서 k 번째 사람 → 지역 k % 지역수, 그 지역의 자리 (k / 지역수) 번째. bSquadSharesSpawnRegion 이면 모두 0번 지역.
//   전용 서버에는 화면 앞 플레이어가 없다. 그래서 "첫 번째 플레이어" 가 아니라 모든 PlayerController 를 돈다.
//   게임모드가 진짜 캐릭터를 주기 전(기본 폰)에는 기다렸다가 다음 틱에 세운다 — 늦게 들어온 사람도 같은 길로 처리된다.

#include "WarZoneFootprintPreviewInternal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerState.h"
#include "Common/PGPlayerMessageComponent.h"
#include "Finale/PGHelmControlComponent.h"

namespace
{
	int32 WZFP_ChebyshevCells(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	}
}

void AWarZoneFootprintPreview::BuildSpawnRegions(const TMap<FIntPoint, AMapTile*>& TileByCell, const TSet<FIntPoint>& BlockedCells,
	const FVector2D& LakeCentreCell, float LakeRadiusCells)
{
	SpawnRegionCells.Reset();

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
		if (Type == ETileType::Spawn && SpawnRegionCells.IsEmpty())
			SpawnRegionCells.Add(Cell); // ① 0번 = 논리 시작 칸
		else if (Type == ETileType::Exit)
			ExitCells.Add(Cell);
	}
	if (SpawnRegionCells.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Spawn regions: the logical grid has no spawn cell - nothing to build"));
		return;
	}

	// ③ 빈 모서리(외진 보상 거점 후보) — 원격 거점과 같은 모서리 상자(생성기의 시작 구역 크기). 호수 모서리는 거점이 안 서니 뺀다.
	const int32 RangeSize = FMath::Max(1, GridManifest.StartRangeSize);
	const FVector2D MapCentreCell((GridMin.X + GridMax.X) * 0.5f, (GridMin.Y + GridMax.Y) * 0.5f);
	TArray<FIntRect> OutpostCorners;
	FString CornerText;
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const bool bMaxX = (Corner & 1) != 0;
		const bool bMaxY = (Corner & 2) != 0;
		if (LakeRadiusCells > 0.0f && (LakeCentreCell.X > MapCentreCell.X) == bMaxX && (LakeCentreCell.Y > MapCentreCell.Y) == bMaxY)
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
			OutpostCorners.Add(FIntRect(BoxMin - FIntPoint(6, 6), BoxMax + FIntPoint(6, 6))); // 둘레 6칸: 거점(상자 가운데)까지 최소 8칸 ≈ 160m
			CornerText += FString::Printf(TEXT("%s%sX%sY"), CornerText.IsEmpty() ? TEXT("") : TEXT(","), bMaxX ? TEXT("+") : TEXT("-"), bMaxY ? TEXT("+") : TEXT("-"));
		}
	}

	TArray<FIntPoint> Candidates;
	int32 RejectedByOutpost = 0, RejectedByLake = 0;
	for (const FIntPoint& Cell : SortedCells)
	{
		ETileType Type;
		if (!TypeAt(Cell, Type) || Type != ETileType::None || BlockedCells.Contains(Cell))
			continue;
		// ② 가장자리 띠 안만(맨 바깥 줄은 산자락·경계 벽 바로 앞이라 뺀다)
		const int32 EdgeDistance = FMath::Min(
			FMath::Min(Cell.X - GridMin.X, GridMax.X - Cell.X),
			FMath::Min(Cell.Y - GridMin.Y, GridMax.Y - Cell.Y));
		if (EdgeDistance >= SpawnRegionEdgeBandCells || EdgeDistance < 1)
			continue;
		// ③ 출구·시설 근처 빼기
		if (ExitCells.ContainsByPredicate([&Cell, this](const FIntPoint& Exit)
			{
				return FMath::Abs(Exit.X - Cell.X) + FMath::Abs(Exit.Y - Cell.Y) < MinSpawnToExitCells;
			}))
			continue;
		if (FacilityPlacements.ContainsByPredicate([&Cell](const FFacilityPlacement& Facility)
			{
				return Facility.OccupiedCells.ContainsByPredicate([&Cell](const FIntPoint& Occupied)
				{
					return WZFP_ChebyshevCells(Occupied, Cell) < 3;
				});
			}))
			continue;
		if (LakeRadiusCells > 0.0f && FVector2D::Distance(FVector2D(Cell.X, Cell.Y), LakeCentreCell) <= LakeRadiusCells + 2.0f)
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

	// ④ 가장 먼 점 고르기
	const int32 WantedRegions = FMath::Clamp(SpawnRegionCount, 1, 8);
	int32 SmallestChosenSpacing = TNumericLimits<int32>::Max();
	const uint32 SeedHash = GetTypeHash(GetRaidSeed());
	while (SpawnRegionCells.Num() < WantedRegions && !Candidates.IsEmpty())
	{
		int32 BestIndex = INDEX_NONE;
		int32 BestSpacing = -1;
		uint32 BestTieBreak = 0;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			int32 Spacing = TNumericLimits<int32>::Max();
			for (const FIntPoint& Chosen : SpawnRegionCells)
				Spacing = FMath::Min(Spacing, WZFP_ChebyshevCells(Chosen, Candidates[Index]));
			const uint32 TieBreak = HashCombine(SeedHash, GetTypeHash(Candidates[Index])); // ⑤
			if (Spacing > BestSpacing || (Spacing == BestSpacing && TieBreak > BestTieBreak))
			{
				BestIndex = Index;
				BestSpacing = Spacing;
				BestTieBreak = TieBreak;
			}
		}
		if (BestIndex == INDEX_NONE || BestSpacing < MinSpawnRegionSpacingCells)
			break;
		SpawnRegionCells.Add(Candidates[BestIndex]);
		SmallestChosenSpacing = FMath::Min(SmallestChosenSpacing, BestSpacing);
		Candidates.RemoveAtSwap(BestIndex);
	}

	FString CellList;
	for (const FIntPoint& Cell : SpawnRegionCells)
		CellList += FString::Printf(TEXT("(%d,%d) "), Cell.X, Cell.Y);
	UE_LOG(LogTemp, Display,
		TEXT("Spawn regions: wanted=%d built=%d cells=%smin_spacing_cells=%d exits=%d shared_squad=%s outpost_corners_kept_clear=[%s] rejected(outpost=%d lake=%d)"),
		WantedRegions, SpawnRegionCells.Num(), *CellList,
		SpawnRegionCells.Num() > 1 ? SmallestChosenSpacing : 0,
		ExitCells.Num(), bSquadSharesSpawnRegion ? TEXT("true") : TEXT("false"), *CornerText, RejectedByOutpost, RejectedByLake);
}

void AWarZoneFootprintPreview::PlaceJoinedPlayers()
{
	UWorld* World = GetWorld();
	// 맵 검증 출발(StartSinglePlayerValidation)이 끝난 뒤에만 — 그 전에는 시작 지점이 아직 안전 검사 전이다.
	if (!HasAuthority() || !IsValid(World) || !bStartedSinglePlayerValidation || SpawnRegionCells.IsEmpty())
		return;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		if (!IsValid(PlayerController))
			continue;
		// 그 사람 화면 안내용 부품을 미리 붙여 둔다(탈출 카운트다운·피날레 경고 등이 쓴다, Common/PGPlayerMessageComponent.h).
		UPGPlayerMessageComponent::Ensure(PlayerController);
		UPGHelmControlComponent::Ensure(PlayerController); // 전함 조종석 입력을 서버로 보내는 길(Finale/PGHelmControlComponent.h)
		if (PlacedPlayers.Contains(PlayerController))
			continue;
		APawn* Pawn = PlayerController->GetPawn();
		// 게임모드가 진짜 캐릭터를 주기 전(기본 폰)이면 다음 틱에 다시 본다.
		if (IsValid(Pawn) && Pawn->IsA<ADefaultPawn>())
			continue;
		// 캐릭터가 아예 없는 사람 = 게임모드가 캐릭터를 못 만든 경우.
		// 9/27 PIE(전용 서버 + 2명): 레벨에 쓸 수 있는 PlayerStart 가 없어 두 사람 다 (0,0,0)에 태어나려 했고, 두 번째 사람은
		//   앞사람과 겹쳐 "SpawnActor failed because of collision" — 캐릭터 없이 맵 밑(워존 창고 바닥 아래)을 보고 있었다.
		// 레벨(사용자 자산)과 게임모드(팀 코드)는 안 고치고, 여기서 정한 자리에 게임모드가 그 사람 캐릭터를 만들게 한다
		//   (RestartPlayerAtTransform — 게임모드의 공개 함수라 팀 게임모드의 캐릭터 종류·설정이 그대로 쓰인다).
		// 막 들어온 사람은 게임모드가 곧 만들 수도 있으니 2초 기다린 뒤에만. 관전 전용인 사람은 세우지 않는다.
		if (!IsValid(Pawn))
		{
			if (PlayerController->PlayerState && PlayerController->PlayerState->IsOnlyASpectator())
				continue;
			const double Now = World->GetTimeSeconds();
			if (Now - PawnlessSinceSeconds.FindOrAdd(PlayerController, Now) < 2.0)
				continue;
		}

		const int32 JoinIndex = NextPlayerJoinIndex;
		const int32 UsableRegions = bSquadSharesSpawnRegion ? 1 : SpawnRegionCells.Num();
		const int32 RegionIndex = JoinIndex % UsableRegions;
		const int32 SeatIndex = JoinIndex / UsableRegions;
		const FIntPoint RegionCell = SpawnRegionCells[RegionIndex];

		TArray<const FLevelDesignPoint*> Seats;
		for (const FLevelDesignPoint& Point : LevelDesignPoints)
			if (Point.Type == ELevelDesignPointType::Spawn && Point.GridCell == RegionCell)
				Seats.Add(&Point);
		if (Seats.IsEmpty())
		{
			++NextPlayerJoinIndex;
			PlacedPlayers.Add(PlayerController);
			UE_LOG(LogTemp, Warning, TEXT("Spawn assign: region %d at (%d,%d) has no seats - %s left where the game mode put it"),
				RegionIndex, RegionCell.X, RegionCell.Y, *GetNameSafe(PlayerController));
			continue;
		}

		const FLevelDesignPoint& Seat = *Seats[SeatIndex % Seats.Num()];
		// 출발 구역 담장의 출구 쪽을 보고 출발한다. 출구는 워존으로 가는 길을 향하게 돌려 놓았다(Layout 의 Spawn 회전).
		// 9/28: 전에는 맵 가운데를 봤는데, 출구가 다른 면이면 바로 앞이 담장이었다(사용자 PIE: 벽을 보고 시작).
		//   출구를 모르면(시작 칸이 아니면) 예전처럼 맵 가운데.
		const FVector ToCentre = (GetActorLocation() - Seat.WorldLocation).GetSafeNormal2D();
		float Yaw = ToCentre.IsNearlyZero() ? (IsValid(Pawn) ? Pawn->GetActorRotation().Yaw : 0.0f) : ToCentre.Rotation().Yaw;
		GetSpawnDoorwayYaw(RegionCell, Seat.WorldLocation, Yaw, Pawn);
		const FVector SeatLocation = Seat.WorldLocation + FVector(0.0f, 0.0f, 100.0f);
		bool bSpawnedHere = false;
		if (!IsValid(Pawn))
		{
			AGameModeBase* GameMode = World->GetAuthGameMode();
			if (IsValid(GameMode))
				GameMode->RestartPlayerAtTransform(PlayerController, FTransform(FRotator(0.0f, Yaw, 0.0f), SeatLocation));
			Pawn = PlayerController->GetPawn();
			if (!IsValid(Pawn))
			{
				// 이 자리도 막혀 있으면 2초 뒤 다시(같은 번호·같은 자리). 로그가 쏟아지지 않게 기다리는 시각을 새로 잡는다.
				PawnlessSinceSeconds.Add(PlayerController, World->GetTimeSeconds());
				UE_LOG(LogTemp, Warning, TEXT("Spawn assign: game mode could not spawn a character for %s at seat %s - retrying in 2s"),
					*GetNameSafe(PlayerController), *Seat.PointId.ToString());
				continue;
			}
			bSpawnedHere = true;
		}
		++NextPlayerJoinIndex;
		PlacedPlayers.Add(PlayerController);
		PawnlessSinceSeconds.Remove(PlayerController);
		if (!bSpawnedHere)
			Pawn->SetActorLocationAndRotation(SeatLocation, FRotator(0.0f, Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		// 원격 클라이언트의 시선은 클라가 가진다 — 서버에서 SetControlRotation 만 하면 그 사람 화면은 안 돈다. 클라 RPC 로 보낸다.
		PlayerController->ClientSetRotation(FRotator(-10.0f, Yaw, 0.0f));

		FString Blocker = TEXT("nothing");
		const float OpenAheadCm = MeasureOpenAhead(Seat.WorldLocation, Yaw, 4000.0f, Pawn, &Blocker) /* 자리 점은 땅+120cm, 재는 선은 그 위 60cm = 눈높이 */;
		UE_LOG(LogTemp, Display, TEXT("Spawn assign: player=%s join=%d region=%d/%d cell=(%d,%d) seat=%s yaw=%.0f open_ahead=%.0fm (then %s)%s"),
			*GetNameSafe(PlayerController), JoinIndex, RegionIndex, UsableRegions,
			RegionCell.X, RegionCell.Y, *Seat.PointId.ToString(), Yaw, OpenAheadCm * 0.01f, *Blocker,
			bSpawnedHere ? TEXT(" (character spawned here - game mode had none)") : TEXT(""));
	}
}

// 출발 구역 타일(LD_Tile_Spawn_Staging)은 담장 한 면(원래 동쪽 +X)만 뚫려 있고, Layout 이 그 출구가 워존 가는 길을 보게
// RotationQuarterTurns 만큼(양의 방향) 돌려 세운다. 그러니 출구 방향 = 동쪽(0도) + 회전 × 90도.
bool AWarZoneFootprintPreview::GetSpawnOpeningYaw(const FIntPoint& Cell, float& OutYaw) const
{
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		if (Placement.GridCell == Cell && Placement.Visual == ETileDesignVisual::Spawn)
		{
			OutYaw = FRotator::NormalizeAxis(Placement.RotationQuarterTurns * 90.0f);
			return true;
		}
	}
	return false;
}

// 출구 "쪽" 으로 똑바로가 아니라, 출구 방향 ±45도 안에서 눈높이로 가장 멀리 트인 방향을 본다(5도 간격, 시작할 때 한 번).
// 왜(9/28 시험): 시작 자리는 문에서 옆으로 4m 남짓 떨어진 곳도 있고, 출발 구역 모양에 따라 문이 담장 가운데가 아니라
//   옆으로 치우쳐 있어서, 출구 방향이나 "가장자리 가운데" 를 보면 문 옆 벽을 보고 섰다(8자리 중 1~3자리가 13m 앞 벽).
//   거의 같게 트였으면(50cm 안) 출구 방향에 가까운 쪽을 고른다.
bool AWarZoneFootprintPreview::GetSpawnDoorwayYaw(const FIntPoint& Cell, const FVector& From, float& OutYaw, const AActor* Ignore) const
{
	float OpeningYaw = 0.0f;
	if (!GetSpawnOpeningYaw(Cell, OpeningYaw))
		return false;
	float BestYaw = OpeningYaw;
	float BestOpen = MeasureOpenAhead(From, OpeningYaw, 4000.0f, Ignore);
	for (int32 Step = 1; Step <= 9; ++Step)
	{
		for (const float Sign : { -1.0f, 1.0f })
		{
			const float Yaw = OpeningYaw + Sign * Step * 5.0f;
			const float Open = MeasureOpenAhead(From, Yaw, 4000.0f, Ignore);
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

float AWarZoneFootprintPreview::MeasureOpenAhead(const FVector& From, const float Yaw, const float MaxCm, const AActor* Ignore, FString* OutBlocker) const
{
	const UWorld* World = GetWorld();
	if (!World)
		return 0.0f;
	const FVector Eye = From + FVector(0.0f, 0.0f, 60.0f);
	const FVector To = Eye + FRotator(0.0f, Yaw, 0.0f).Vector() * MaxCm;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSpawnOpenAhead), false, Ignore);
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
