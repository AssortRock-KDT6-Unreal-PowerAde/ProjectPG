// AWarZoneFootprintPreview — 맵 위 특수 장소 — 호수 배와 배 탈출구, 원격 기지.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"

// 호수 마을(RuralDiorama)의 나룻배를 연료통 탈출구로 만든다(기획서 3.3.2 "배 + 연료통").
// 왜 지금 다시 넣나: 9/18 에 "배는 필요 없음" 으로 뺐는데, 9/22 사용자가 "외곽 호수도 배 띄워 놓고 똑같이 연료통으로 탈출" 을
//   원했다. 드래곤을 잡은 사람에게만 특전 탈출을 주는 대신, 누구나 연료통만 있으면 쓰는 탈출구를 하나 더 두는 쪽이 공평하다.
// 왜 "올라타서 10초" 인가(F 가 아니라 겹침): 배는 카탈로그 메시가 아니라 레벨에 이미 놓인 소품이라 F 로 겨누는 대상이 아니다.
//   배 둘레 상자에 들어가 연료통을 가진 채 10초 버티면 출항 — 헬기의 "F 10초" 와 같은 무게.
// 배 자리는 레벨 파일에 있어서 이름(Wooden_Boat*)으로 찾는다. 찾은 수를 로그로 남긴다 — 0 이면 레벨이 바뀐 것이다.
void AWarZoneFootprintPreview::AttachBoatExits(const ULevel* Level)
{
	UWorld* World = GetWorld();
	if (!IsValid(Level) || !World || !HasAuthority())
	{
		UE_LOG(LogTemp, Display, TEXT("Boat exits: skipped (level=%s, authority=%d)"), *GetNameSafe(Level), HasAuthority() ? 1 : 0);
		return;
	}
	int32 Boats = 0;
	for (AActor* Actor : Level->Actors)
	{
		if (!IsValid(Actor))
			continue;
		TInlineComponentArray<UStaticMeshComponent*> Meshes(Actor);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (!IsValid(Mesh) || !Mesh->GetStaticMesh() || !Mesh->GetStaticMesh()->GetName().StartsWith(TEXT("Wooden_Boat")))
				continue;
			const FBox Bounds = Mesh->Bounds.GetBox();
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			APGExtractionZoneActor* Exit = World->SpawnActor<APGExtractionZoneActor>(APGExtractionZoneActor::StaticClass(),
				FTransform(Mesh->GetComponentRotation(), Bounds.GetCenter()), Params);
			if (!IsValid(Exit))
				continue;
			// 겹침으로 10초, 연료통 1개를 쓰고, 횟수 제한 없음(0) — 누구나 연료통만 있으면 탄다.
			Exit->Configure(EPGExtractionTrigger::Overlap, 5.0f, TEXT("Fuel"), true, 0);
			Exit->SetHoldStill(true); // 9/22: 모든 탈출구가 같은 방식 — 멈춰 5초 버티기, 움직이면 취소
			Exit->SetPollOccupants(true);
			// 상자는 배보다 사방 1.5m 넓게 — 배 위에 올라서거나 바로 옆 물가에 서도 잡힌다.
			const FVector Extent = Bounds.GetExtent() + FVector(150.0f, 150.0f, 150.0f);
			Exit->SetZoneBox(FVector::ZeroVector, Extent);
			Exit->SetExitDisplayName(NSLOCTEXT("Extraction", "BoatExit", "나룻배"));
			++Boats;
			UE_LOG(LogTemp, Display, TEXT("Boat exits: %s on %s at %s, box %s — needs Fuel, hold still 5s"),
				*GetNameSafe(Exit), *Mesh->GetStaticMesh()->GetName(), *Bounds.GetCenter().ToCompactString(), *Extent.ToCompactString());
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Boat exits: %d boat(s) in %s turned into fuel exits%s"),
		Boats, *GetNameSafe(Level->GetOuter()), Boats == 0 ? TEXT(" — NO Wooden_Boat mesh found (level changed?)") : TEXT(""));
}

// 외진 보상 거점(추락 헬기 캠프) 자리 고르기.
//
// 왜: 맵 생성기(팀원 코드, 손대지 않는다)는 가장자리 방향 8개 중 3~5개를 매 판 버리고, 남은 것에 시작·출구와 길을 낸다.
//   그래서 모서리 한두 곳은 길도 시작도 출구도 없는 빈 땅이 된다(호수가 그중 하나를 가져간다). 사용자 9/22: "멀리 갈 이유를 만들자".
// 어디를 "빈 모서리" 로 보나 — 생성기가 시작·출구를 찍는 모서리 상자(_startPositionRangeSize 칸, 기본 4 → 테두리 포함 5x5)마다:
//   ① 호수 모서리가 아니다  ② 논리 타일 중 길·시작·출구·장애물(길 위 장애물)·워존이 없다  ③ 보이는 길(시설 진입로 포함)이 없다
//   ④ 무너진 곳(CollapsedAreas)이 아니다  ⑤ 그 안에 거점이 들어갈 2x2 칸이 있다 — 칸마다: 논리 빈 땅, 시설 발자국과 그 둘레 1칸 밖,
//      물·물가(호수와 맞닿은 칸) 아님, 지형 굴곡(언덕·구덩이) 아님(그런 칸은 TileDesignPlacements 에 없다), 땅 높이 = 기준 20cm.
//   맵 가장 바깥 줄은 쓰지 않는다 — 경계 벽·산자락 바로 앞이라 캠프가 절벽에 붙어 보인다.
// 여럿이면: 시작 지점에서 가장 먼 모서리(멀리까지 갈 이유가 되라고). 하나도 없으면 아무것도 안 세우고 이유를 로그에 남긴다.
// 세운 뒤: 그 2x2 칸(40m 네모) 안의 나무·바위·담을 걷어 내고(RemoveTallDressingInZones), 상자·바닥 아이템은 월드 루팅이 놓는다.
void AWarZoneFootprintPreview::PlaceRemoteOutpost(const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	UWorld* World = GetWorld();
	// 멀티(9/27): 자리 고르기·주변 소품 걷기는 클라이언트도 한다(안 하면 클라 화면에만 나무·바위가 기지와 겹친다). 기지 액터는 서버만 세운다(복제).
	if (!bPlaceRemoteOutpost || !World || !World->IsGameWorld() || IsValid(RemoteOutpost) || TileByCell.IsEmpty())
		return;

	// 모서리 상자 크기는 생성기 값을 그대로 읽는다(리플렉션 — 팀원 코드에 getter 를 추가하지 않으려고). 못 읽으면 기본값 4.
	// 서버는 설계도를 채울 때(FillGridManifest) 생성기에서 읽어 두었고, 클라이언트는 받은 설계도에서 읽는다 — 양쪽이 같은 모서리를 고른다.
	const int32 RangeSize = FMath::Max(1, GridManifest.StartRangeSize);

	FIntPoint MinCell(MAX_int32, MAX_int32);
	FIntPoint MaxCell(MIN_int32, MIN_int32);
	FVector2D SpawnSum = FVector2D::ZeroVector;
	int32 SpawnCount = 0;
	for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
	{
		MinCell = FIntPoint(FMath::Min(MinCell.X, Pair.Key.X), FMath::Min(MinCell.Y, Pair.Key.Y));
		MaxCell = FIntPoint(FMath::Max(MaxCell.X, Pair.Key.X), FMath::Max(MaxCell.Y, Pair.Key.Y));
		if (IsValid(Pair.Value) && Pair.Value->GetType() == ETileType::Spawn)
		{
			SpawnSum += FVector2D(Pair.Key.X, Pair.Key.Y);
			++SpawnCount;
		}
	}
	const FVector2D SpawnCell = SpawnCount > 0 ? SpawnSum / SpawnCount : FVector2D((MinCell.X + MaxCell.X) * 0.5f, (MinCell.Y + MaxCell.Y) * 0.5f);

	TMap<FIntPoint, const FTileDesignPlacement*> PlacementByCell;
	TSet<FIntPoint> WaterCells;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		PlacementByCell.Add(Placement.GridCell, &Placement);
		if (Placement.Visual == ETileDesignVisual::Water)
			WaterCells.Add(Placement.GridCell);
	}
	// 시설 발자국 + 둘레 1칸. 시설 진입로·경사로가 이 둘레에 온다.
	TSet<FIntPoint> FacilityCells;
	for (const FFacilityPlacement& Facility : FacilityPlacements)
		for (const FIntPoint& Cell : Facility.OccupiedCells)
			for (int32 DX = -1; DX <= 1; ++DX)
				for (int32 DY = -1; DY <= 1; ++DY)
					FacilityCells.Add(Cell + FIntPoint(DX, DY));

	auto IsRoadVisual = [](ETileDesignVisual Visual)
	{
		return Visual == ETileDesignVisual::RoadStraight || Visual == ETileDesignVisual::RoadCorner
			|| Visual == ETileDesignVisual::RoadTJunction || Visual == ETileDesignVisual::RoadCross
			|| Visual == ETileDesignVisual::RoadDeadEnd;
	};
	// 거점이 설 수 있는 겉모습. 폐허(담 조각)·도랑(낮은 땅)은 뺀다. 숲·바위 칸은 걷어 내면 되므로 받는다.
	auto IsBuildableVisual = [](ETileDesignVisual Visual)
	{
		return GetTileVisualTraits(Visual).bOutpostBuildable; // 공용 헤더의 겉모습 표
	};
	auto IsCollapsed = [this](const FIntPoint& Cell)
	{
		const FVector2D At(Cell.X * DesignCellSize, Cell.Y * DesignCellSize);
		for (const FVector4& Area : CollapsedAreas)
			if (FVector2D::Distance(At, FVector2D(Area.X, Area.Y)) <= Area.W + DesignCellSize)
				return true;
		return false;
	};

	struct FCornerResult
	{
		FString Label;
		FString Reason;
		bool bOk = false;
		FIntPoint BlockAnchor = FIntPoint::ZeroValue;
		float SpawnDistanceCells = 0.0f;
	};
	TArray<FCornerResult> Results;
	const FVector2D MapCentreCell((MinCell.X + MaxCell.X) * 0.5f, (MinCell.Y + MaxCell.Y) * 0.5f);
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const bool bMaxX = (Corner & 1) != 0;
		const bool bMaxY = (Corner & 2) != 0;
		FCornerResult& Result = Results.AddDefaulted_GetRef();
		Result.Label = FString::Printf(TEXT("%sX%sY"), bMaxX ? TEXT("+") : TEXT("-"), bMaxY ? TEXT("+") : TEXT("-"));
		const FIntPoint BoxMin(bMaxX ? MaxCell.X - RangeSize : MinCell.X, bMaxY ? MaxCell.Y - RangeSize : MinCell.Y);
		const FIntPoint BoxMax(bMaxX ? MaxCell.X : MinCell.X + RangeSize, bMaxY ? MaxCell.Y : MinCell.Y + RangeSize);

		// ① 호수 모서리. 호수 가운데는 모서리 바깥 3칸에 찍혀 있으므로 부호만 보면 된다(GetBorderLake).
		if (bHasBorderLake && (BorderLakeCentreCell.X > MapCentreCell.X) == bMaxX && (BorderLakeCentreCell.Y > MapCentreCell.Y) == bMaxY)
		{
			Result.Reason = TEXT("lake");
			continue;
		}
		// ②③④ 모서리 상자 안을 훑는다.
		int32 Endpoints = 0, LogicalRoads = 0, VisualRoads = 0, Collapsed = 0;
		for (int32 X = BoxMin.X; X <= BoxMax.X; ++X)
		{
			for (int32 Y = BoxMin.Y; Y <= BoxMax.Y; ++Y)
			{
				const FIntPoint Cell(X, Y);
				if (AMapTile* const* Tile = TileByCell.Find(Cell); Tile && IsValid(*Tile))
				{
					const ETileType Type = (*Tile)->GetType();
					if (Type == ETileType::Spawn || Type == ETileType::Exit)
						++Endpoints;
					else if (Type != ETileType::None)
						++LogicalRoads; // 길·장애물(길 위)·워존
				}
				if (const FTileDesignPlacement* const* Placement = PlacementByCell.Find(Cell); Placement && IsRoadVisual((*Placement)->Visual))
					++VisualRoads;
				if (IsCollapsed(Cell))
					++Collapsed;
			}
		}
		// 멀티 추가 시작 지역(9/28): 논리 칸은 빈 땅이라 위에서 안 잡힌다. 상자와 그 둘레 6칸 안에 있으면 이 모서리는 쓰지 않는다
		//   — 시작 지역 바로 뒤에 에픽템이 있으면 나오자마자 줍는다(9/28 사용자 PIE).
		for (int32 RegionIndex = 1; RegionIndex < SpawnRegionCells.Num(); ++RegionIndex)
		{
			const FIntPoint& Region = SpawnRegionCells[RegionIndex];
			if (Region.X >= BoxMin.X - 6 && Region.X <= BoxMax.X + 6 && Region.Y >= BoxMin.Y - 6 && Region.Y <= BoxMax.Y + 6)
				++Endpoints;
		}
		if (Endpoints > 0 || LogicalRoads > 0 || VisualRoads > 0 || Collapsed > 0)
		{
			Result.Reason = FString::Printf(TEXT("busy(spawn_exit=%d road=%d visual_road=%d collapsed=%d)"), Endpoints, LogicalRoads, VisualRoads, Collapsed);
			continue;
		}
		// ⑤ 2x2 칸 찾기. 맵 가장 바깥 줄은 뺀다.
		const int32 InnerMinX = FMath::Max(BoxMin.X, MinCell.X + 1);
		const int32 InnerMaxX = FMath::Min(BoxMax.X, MaxCell.X - 1);
		const int32 InnerMinY = FMath::Max(BoxMin.Y, MinCell.Y + 1);
		const int32 InnerMaxY = FMath::Min(BoxMax.Y, MaxCell.Y - 1);
		int32 RejectFacility = 0, RejectWater = 0, RejectSurface = 0;
		auto CellUsable = [&](const FIntPoint& Cell)
		{
			AMapTile* const* Tile = TileByCell.Find(Cell);
			const FTileDesignPlacement* const* Placement = PlacementByCell.Find(Cell);
			if (FacilityCells.Contains(Cell))
			{
				++RejectFacility;
				return false;
			}
			for (int32 DX = -1; DX <= 1; ++DX)
				for (int32 DY = -1; DY <= 1; ++DY)
					if (WaterCells.Contains(Cell + FIntPoint(DX, DY)))
					{
						++RejectWater;
						return false;
					}
			if (!Tile || !IsValid(*Tile) || (*Tile)->GetType() != ETileType::None || !Placement
				|| !IsBuildableVisual((*Placement)->Visual)
				|| !FMath::IsNearlyEqual(GetSurfaceElevationForCell(Cell), BaseGroundSurfaceZ, 1.0f))
			{
				++RejectSurface; // 지형 굴곡·폐허·도랑 칸(겉모습 목록에 없거나 칸 자체가 없다)
				return false;
			}
			return true;
		};
		// 모서리 상자 가운데에 가장 가까운 2x2. 너무 구석이면 산자락에 붙고, 너무 안쪽이면 모서리가 아니다.
		const FVector2D BoxCentre((BoxMin.X + BoxMax.X) * 0.5f, (BoxMin.Y + BoxMax.Y) * 0.5f);
		float BestScore = TNumericLimits<float>::Max();
		for (int32 X = InnerMinX; X + 1 <= InnerMaxX; ++X)
		{
			for (int32 Y = InnerMinY; Y + 1 <= InnerMaxY; ++Y)
			{
				const FIntPoint Anchor(X, Y);
				if (!CellUsable(Anchor) || !CellUsable(Anchor + FIntPoint(1, 0)) || !CellUsable(Anchor + FIntPoint(0, 1)) || !CellUsable(Anchor + FIntPoint(1, 1)))
					continue;
				const float Score = FVector2D::DistSquared(FVector2D(X + 0.5f, Y + 0.5f), BoxCentre);
				if (Score < BestScore)
				{
					BestScore = Score;
					Result.BlockAnchor = Anchor;
					Result.bOk = true;
				}
			}
		}
		if (!Result.bOk)
		{
			Result.Reason = FString::Printf(TEXT("no_free_2x2(facility=%d water=%d terrain_or_visual=%d)"), RejectFacility, RejectWater, RejectSurface);
			continue;
		}
		// 가장 가까운 시작 지역까지(멀티 9/28: 모든 시작 지역. 예전에는 논리 시작 칸 하나만 봤다).
		Result.SpawnDistanceCells = FVector2D::Distance(FVector2D(Result.BlockAnchor.X + 0.5f, Result.BlockAnchor.Y + 0.5f), SpawnCell);
		for (const FIntPoint& Region : SpawnRegionCells)
			Result.SpawnDistanceCells = FMath::Min(Result.SpawnDistanceCells,
				static_cast<float>(FVector2D::Distance(FVector2D(Result.BlockAnchor.X + 0.5f, Result.BlockAnchor.Y + 0.5f), FVector2D(Region.X, Region.Y))));
		Result.Reason = FString::Printf(TEXT("ok(spawn_dist=%.1f)"), Result.SpawnDistanceCells);
	}

	FString Summary;
	const FCornerResult* Best = nullptr;
	for (const FCornerResult& Result : Results)
	{
		Summary += FString::Printf(TEXT("%s%s=%s"), Summary.IsEmpty() ? TEXT("") : TEXT(" "), *Result.Label, *Result.Reason);
		if (Result.bOk && (!Best || Result.SpawnDistanceCells > Best->SpawnDistanceCells))
			Best = &Result;
	}
	if (!Best)
	{
		UE_LOG(LogTemp, Display, TEXT("RemoteOutpost skipped reason=no_empty_corner range=%d corners: %s"), RangeSize, *Summary);
		return;
	}

	// 2x2 칸 네 개의 가운데(월드). 칸 번호 × 20m 가 이 맵의 좌표 규칙이다(타일 배치와 같다).
	const FIntPoint Anchor = Best->BlockAnchor;
	const FVector Centre((Anchor.X + 0.5f) * DesignCellSize, (Anchor.Y + 0.5f) * DesignCellSize, BaseGroundSurfaceZ);
	const FVector MapCentreWorld(MapCentreCell.X * DesignCellSize, MapCentreCell.Y * DesignCellSize, BaseGroundSurfaceZ);
	const float InwardYaw = (MapCentreWorld - Centre).GetSafeNormal2D().Rotation().Yaw;

	// 먼저 걷어 낸다. 부품을 세운 뒤에 걷으면 같은 자리 나무가 헬기를 뚫고 서 있는 판이 한 프레임이라도 보인다.
	const FBox2D ClearZone(FVector2D(Centre.X, Centre.Y) - FVector2D(DesignCellSize), FVector2D(Centre.X, Centre.Y) + FVector2D(DesignCellSize));
	int32 TouchedComponents = 0;
	const int32 Cleared = RemoveTallDressingInZones({ ClearZone }, TouchedComponents);

	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Display, TEXT("RemoteOutpost (client): corner=%s cell=(%d,%d) cleared_dressing=%d — the camp itself comes from the server"),
			*Best->Label, Anchor.X, Anchor.Y, Cleared);
		return;
	}
	const int64 RaidSeed = GetRaidSeed();
	const int64 OutpostSeed = static_cast<int64>(HashCombine(GetTypeHash(RaidSeed), HashCombine(GetTypeHash(Anchor.X), GetTypeHash(Anchor.Y))));
	RemoteOutpost = APGRemoteOutpostActor::SpawnOutpost(World, Centre, InwardYaw, OutpostSeed, Best->Label);
	if (!IsValid(RemoteOutpost))
	{
		UE_LOG(LogTemp, Warning, TEXT("RemoteOutpost skipped reason=spawn_failed corner=%s cell=(%d,%d)"), *Best->Label, Anchor.X, Anchor.Y);
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("RemoteOutpost placed corner=%s cell=(%d,%d) block=2x2 centre=%s yaw=%.0f spawn_dist_cells=%.1f containers=%d pieces=%d cleared_dressing=%d range=%d corners: %s"),
		*Best->Label, Anchor.X, Anchor.Y, *Centre.ToCompactString(), InwardYaw, Best->SpawnDistanceCells,
		RemoteOutpost->GetLootSlots().Num(), RemoteOutpost->GetPieceCount(), Cleared, RangeSize, *Summary);
}

// 바깥 호숫가 나룻배 탈출구.
//
// 왜: 9/22 에 배 탈출구를 넣었지만 호수 마을(RuralDiorama) 레벨 안 작은 연못의 배 두 척에만 붙었다. 사용자가 원한 것은
//   "외곽 호수에 배를 띄워 놓고 똑같이 연료통으로 탈출" 이었다(9/22 "호수에 배가 없는데?"). 그래서 큰 호수 물가에 배를 직접 띄운다.
// 어디에: 호수 칸과 맞닿은 땅 칸마다 후보를 만든다. 땅 칸 가운데에서 호수 쪽으로 50cm 씩 걸어가며 바닥을 쏴서
//   처음으로 물 높이(-20cm 아래)가 나오는 곳 = 물가. 배는 그 물가에서 물 쪽으로 배 반 폭 + 60cm, 물가와 나란히 눕힌다.
//   물가 모양은 마칭 스퀘어 메시라 칸 경계와 딱 맞지 않아서, 계산하지 않고 쏴서 찾는다.
// 몇 척: LakeBoatCount(기본 3), 서로 80m 넘게. 같은 맵이면 같은 자리(호수 칸 수로 시드를 만든다).
// 조건: 마을 배와 같다 — 배 둘레 상자에 들어가 연료통을 가진 채 10초. 물가에 서 있어도 잡히게 상자를 땅 쪽으로 2.5m 넓힌다.
void AWarZoneFootprintPreview::SpawnLakeBoats()
{
	UWorld* World = GetWorld();
	// 멀티(9/27): 배 모형은 클라이언트도 스스로 놓는다(복제하지 않는 장식). 탈출구·연료통은 서버만(복제되는 게임 물건).
	if (!World || !World->IsGameWorld() || LakeBoatCount <= 0)
		return;
	const TSoftObjectPtr<UStaticMesh>& BoatAsset = UPGMapVisualSet::GetActive()->LakeBoat;
	UStaticMesh* BoatMesh = BoatAsset.IsNull() ? nullptr : BoatAsset.LoadSynchronous();
	if (!BoatMesh)
	{
		UE_LOG(LogTemp, Warning, TEXT("Lake boats: Wooden_Boat mesh missing — no lake boat exits"));
		return;
	}
	TSet<FIntPoint> Water;
	TMap<FIntPoint, FVector> CellWorld; // 칸 번호 → 칸 가운데 월드 좌표(맵이 쓰는 그 값)
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		CellWorld.Add(Placement.GridCell, Placement.WorldLocation);
		if (Placement.Visual == ETileDesignVisual::Water)
			Water.Add(Placement.GridCell);
	}
	if (Water.IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("Lake boats: no lake on this map"));
		return;
	}
	struct FBoatCandidate { FIntPoint Land; FIntPoint Dir; };
	TArray<FBoatCandidate> Candidates;
	const FIntPoint Dirs[] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	for (const FIntPoint& Cell : Water)
		for (const FIntPoint& Dir : Dirs)
		{
			const FIntPoint Land = Cell - Dir; // 땅 → 물 방향이 Dir
			if (CellWorld.Contains(Land) && !Water.Contains(Land))
				Candidates.Add({ Land, Dir });
		}
	Candidates.Sort([](const FBoatCandidate& A, const FBoatCandidate& B)
	{
		return A.Land.X != B.Land.X ? A.Land.X < B.Land.X : (A.Land.Y != B.Land.Y ? A.Land.Y < B.Land.Y : A.Dir.X * 3 + A.Dir.Y < B.Dir.X * 3 + B.Dir.Y);
	});
	FRandomStream Stream(static_cast<int32>(Water.Num() * 7919 + Candidates.Num() * 104729));
	for (int32 Index = Candidates.Num() - 1; Index > 0; --Index)
		Candidates.Swap(Index, Stream.RandRange(0, Index));

	const FTransform ToWorld = GetActorTransform();
	const FBoxSphereBounds MeshBounds = BoatMesh->GetBounds();
	const bool bLongX = MeshBounds.BoxExtent.X >= MeshBounds.BoxExtent.Y;
	const float HalfWidth = bLongX ? MeshBounds.BoxExtent.Y : MeshBounds.BoxExtent.X;
	FCollisionQueryParams Query(SCENE_QUERY_STAT(PGLakeBoat), false);
	// 길찾기용 투명 바닥판(맵 전체, 윗면 Z≈-10)은 물 위도 덮는다. 무시하지 않으면 물가를 영영 못 찾는다(첫 실행: 후보 11곳 모두 실패).
	Query.AddIgnoredComponent(NavigationFloor.Get());
	TArray<FVector> Placed;
	int32 NoShore = 0;
	int32 NotLand = 0;
	for (const FBoatCandidate& Candidate : Candidates)
	{
		if (Placed.Num() >= LakeBoatCount)
			break;
		// 칸 좌표는 격자 번호 × 20m 로 계산하지 않고 배치가 가진 월드 좌표를 쓴다(첫 시도는 그렇게 계산해서 11곳 모두 엉뚱한 자리를 봤다).
		const FVector LandCentre = CellWorld[Candidate.Land] * FVector(1.0f, 1.0f, 0.0f);
		const FVector Dir = (CellWorld[Candidate.Land + Candidate.Dir] - CellWorld[Candidate.Land]).GetSafeNormal2D();
		if (Dir.IsNearlyZero())
			continue;
		if (Placed.ContainsByPredicate([&LandCentre](const FVector& P) { return FVector::DistSquared2D(P, LandCentre) < FMath::Square(8000.0f); }))
			continue;
		// 물가 찾기: 땅 칸 한 칸 안쪽에서 출발해 50cm 씩 호수 쪽으로 걸으며 "마른 땅 → 물" 로 바뀌는 첫 곳.
		// 물가 칸은 비탈 메시라 칸 가운데가 이미 물 밑일 수 있어서 한 칸 더 안쪽부터 본다.
		FHitResult Hit;
		FVector Shore = FVector::ZeroVector;
		bool bFound = false;
		bool bSawDry = false;
		for (float Step = -DesignCellSize; Step <= DesignCellSize * 1.5f; Step += 50.0f)
		{
			const FVector P = LandCentre + Dir * Step;
			const bool bHit = World->LineTraceSingleByChannel(Hit, P + FVector(0, 0, 600), P - FVector(0, 0, 400), ECC_Visibility, Query);
			const bool bWet = !bHit || Hit.ImpactPoint.Z < LakeSurfaceZ + 15.0f;
			if (!bWet && Hit.ImpactPoint.Z < BaseGroundSurfaceZ + 400.0f)
				bSawDry = true;
			else if (bWet && bSawDry)
			{
				Shore = FVector(P.X, P.Y, LakeSurfaceZ);
				bFound = true;
				break;
			}
		}
		if (!bFound)
		{
			if (bSawDry)
				++NoShore;
			else
				++NotLand;
			continue;
		}
		const FVector BoatCentre = Shore + Dir * (HalfWidth + 60.0f);
		// 배의 긴 쪽을 물가와 나란히(Dir 에 수직).
		const float ShoreYaw = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X)) + 90.0f;
		const float Yaw = ShoreYaw + (bLongX ? 0.0f : 90.0f) + Stream.FRandRange(-12.0f, 12.0f);
		// 바닥이 물 밑 15cm 에 잠기게(흘수). 메시 원점이 바닥이 아닐 수 있어 바운드로 맞춘다.
		const float MeshBottom = MeshBounds.Origin.Z - MeshBounds.BoxExtent.Z;
		const FVector BoatLocation(BoatCentre.X, BoatCentre.Y, LakeSurfaceZ - 15.0f - MeshBottom);
		const FRotator BoatRotation(0.0f, Yaw, Stream.FRandRange(-2.0f, 2.0f));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Boat = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), FTransform(BoatRotation, BoatLocation), Params);
		if (!IsValid(Boat))
			continue;
		Boat->SetReplicates(false); // 장식: 서버·클라가 같은 자리에 각자 놓는다
		Boat->GetStaticMeshComponent()->SetMobility(EComponentMobility::Movable);
		Boat->GetStaticMeshComponent()->SetStaticMesh(BoatMesh);
		// 차에 치여 날아가면 탈출구가 사라진다 → 보호. 땅이 꺼지면 검문소처럼 같이 사라진다(CollapseRegion 의 예외 표).
		Boat->Tags.AddUnique(PGPhysicsUtil::ProtectedTag);
		Boat->Tags.AddUnique(TEXT("PGExitDressing"));

		FActorSpawnParameters ZoneParams;
		ZoneParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ZoneParams.bDeferConstruction = true;
		// 상자는 배와 물가 사이 가운데에 두고 물가 쪽으로 넓힌다(배 안이든 바로 옆 물가든 잡힌다).
		const FVector ZoneCentre = (BoatCentre + Shore) * 0.5f;
		const FTransform ZoneTransform(FRotator(0.0f, ShoreYaw, 0.0f), FVector(ZoneCentre.X, ZoneCentre.Y, LakeSurfaceZ));
		// 탈출구(복제되는 게임 물건)는 서버만 만든다.
		if (APGExtractionZoneActor* Exit = HasAuthority() ? World->SpawnActor<APGExtractionZoneActor>(APGExtractionZoneActor::StaticClass(), ZoneTransform, ZoneParams) : nullptr)
		{
			const float BoatLong = bLongX ? MeshBounds.BoxExtent.X : MeshBounds.BoxExtent.Y;
			Exit->Configure(EPGExtractionTrigger::Overlap, 5.0f, TEXT("Fuel"), true, 0);
			Exit->SetHoldStill(true); // 9/22: 모든 탈출구가 같은 방식 — 멈춰 5초 버티기, 움직이면 취소
			Exit->SetPollOccupants(true);
			// 상자 X = 물가 따라(배 길이 + 1m), Y = 물가 ↔ 배 방향(배 폭 + 물가 쪽 2.5m).
			Exit->SetZoneBox(FVector(0.0f, 0.0f, 150.0f), FVector(BoatLong + 100.0f, (HalfWidth + 60.0f) * 0.5f + HalfWidth + 250.0f, 250.0f));
			Exit->SetExitDisplayName(NSLOCTEXT("Extraction", "LakeBoatExit", "호숫가 나룻배"));
			Exit->SetPollOccupants(true);
			Exit->FinishSpawning(ZoneTransform);
		}
		// 배 바로 옆 물가(마른 땅 쪽 1.5m)에 연료통 하나. 9/22 사용자: "나룻배 바로 옆에 연료통 좀". 검문소마다 하나 두는 것과 같은 몫이다.
		if (HasAuthority()) // 연료통(복제되는 게임 물건)은 서버만
		{
			const FVector FuelXY = Shore - Dir * 150.0f;
			FHitResult FuelGround;
			if (World->LineTraceSingleByChannel(FuelGround, FuelXY + FVector(0, 0, 400), FuelXY - FVector(0, 0, 400), ECC_Visibility, Query)
				&& FuelGround.ImpactPoint.Z > LakeSurfaceZ + 15.0f)
			{
				APGFloorItemActor* Fuel = APGFloorItemActor::SpawnDrop(World, TEXT("Fuel"), 1,
					FTransform(FRotator(0.0f, ShoreYaw, 0.0f), FuelGround.ImpactPoint + FVector(0.0f, 0.0f, 30.0f)));
				UE_LOG(LogTemp, Display, TEXT("Lake boats: fuel %s beside boat %d at %s"), *GetNameSafe(Fuel), Placed.Num() + 1, *FuelGround.ImpactPoint.ToCompactString());
			}
		}
		Placed.Add(LandCentre);
		UE_LOG(LogTemp, Display, TEXT("Lake boats: boat %d at %s (land cell %d,%d -> dir %d,%d, shore %s) — needs Fuel, hold still 5s"),
			Placed.Num(), *BoatLocation.ToCompactString(), Candidate.Land.X, Candidate.Land.Y, Candidate.Dir.X, Candidate.Dir.Y, *Shore.ToCompactString());
	}
	UE_LOG(LogTemp, Display, TEXT("Lake boats: %d/%d placed from %d shore candidates (%d without a shoreline, %d not on land)"),
		Placed.Num(), LakeBoatCount, Candidates.Num(), NoShore, NotLand);
}
