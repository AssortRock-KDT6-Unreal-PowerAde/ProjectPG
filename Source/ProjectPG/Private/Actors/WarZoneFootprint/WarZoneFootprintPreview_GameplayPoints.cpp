// AWarZoneFootprintPreview — 게임 지점 — 시작·탈출·괴물·전리품 후보 자리 만들기와 안전 검사.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"
#include "PGMapTileSpawner.h"
#include "PGGameplayPointBuilder.h"

void UPGGameplayPointBuilder::BuildGameplayPointMarkers()
{
	// 9/28: 468줄이던 것을 단계 함수로 나눴다(동작·순서 그대로). 타일 칸 → 추가 시작 지역 → 가장자리 추가 출구 → 시설 안.
	Map->LevelDesignPoints.Reset();
	// 멀티용 시작 지역(SpawnRegionCells)은 설계도를 만들 때(BuildTileDesignPlacements 끝) 이미 정했다 — 추가 지역 칸도 출발 구역 타일로
	// 바뀌어 있어야 해서 타일을 세우기 전에 골랐다. 여기서 다시 고르면 안 된다: 이제 Spawn 칸이 여러 개라 "첫 Spawn 칸" 이 0번이 아닐 수 있다.
	TMap<ELevelDesignPointType, int32> Counts;
	AddTileDesignPoints(Counts);

	// 추가 시작 지역(1번부터)의 자리 4개. 논리 시작 칸(0번) 자리보다 뒤에 넣는다 —
	// StartSinglePlayerValidation 과 스타터 꾸러미가 "첫 Spawn 지점" 을 0번 지역 첫 자리로 보고 있어서, 순서가 바뀌면 첫 플레이어가 두 곳으로 옮겨진다.
	for (int32 RegionIndex = 1; RegionIndex < Map->SpawnRegionCells.Num(); ++RegionIndex)
	{
		const FIntPoint& RegionCell = Map->SpawnRegionCells[RegionIndex];
		AddDesignPoint(Counts, ELevelDesignPointType::Spawn, RegionCell, FVector(0, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		AddDesignPoint(Counts, ELevelDesignPointType::Spawn, RegionCell, FVector(0, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		AddDesignPoint(Counts, ELevelDesignPointType::Spawn, RegionCell, FVector(360, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		AddDesignPoint(Counts, ELevelDesignPointType::Spawn, RegionCell, FVector(360, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
	}

	AddExtraEdgeExitPoints(Counts);
	AddFacilityDesignPoints(Counts);

	RebuildGameplayPointHash();

	// 여기서 바로 알리지 않는다. 시설 레벨은 비동기로 1초 넘게 걸려 뜨는데, 그 전에 알렸더니 몬스터가 빈 터에 먼저 스폰됐고
	// 나중에 공장이 그 자리에 나타나 벽·지붕 속에 끼었다. 알림은 BroadcastLevelDesignPointsWhenReady(Tick)가 한다.
	if (!Map->bLevelDesignPointsGenerated)
	{
		Map->bLevelDesignPointsGenerated = true;
		Map->LevelDesignPointsGeneratedTime = GetWorld()->GetTimeSeconds();
	}

	UE_LOG(LogTemp, Display,
		TEXT("Design points: total=%d spawn=%d loot=%d ai=%d exit=%d quest=%d point_hash=%08X"),
		Map->LevelDesignPoints.Num(),
		Counts.FindRef(ELevelDesignPointType::Spawn),
		Counts.FindRef(ELevelDesignPointType::Loot),
		Counts.FindRef(ELevelDesignPointType::AISpawn),
		Counts.FindRef(ELevelDesignPointType::Exit),
		Counts.FindRef(ELevelDesignPointType::Quest),
		Map->GameplayPointHash);
}

// 이 칸 둘레에 몬스터 스폰을 두면 안 되나(시작 지역 안전 띠). 9/28 BuildGameplayPointMarkers 에서 떼어 냄(동작 그대로).
bool UPGGameplayPointBuilder::IsCellProtectedFromAI(const FIntPoint& Cell) const
{
	// 추가 시작 지역도 논리 시작 칸과 같은 60m 안전 띠를 둔다(아래 Spawn 판정은 논리 시작 칸만 본다).
	for (const FIntPoint& RegionCell : Map->SpawnRegionCells)
		if (FMath::Abs(Cell.X - RegionCell.X) + FMath::Abs(Cell.Y - RegionCell.Y) < 4)
			return true;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
			+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
		if (Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
			return true; // 60m minimum spawn safe band
		if (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2)
			return true; // do not camp directly on the extraction stencil
	}
	// The boat landing is a spawn too, but it is a gameplay point on the rural
	// hamlet, not a Spawn tile, so the band above cannot see it. Without this
	// the natural encounters crept to 49.5 m of the landing and the hamlet
	// itself was tallied as a facility missing its resident AI.
	if (Map->bHasBorderLake)
	{
		for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
		{
			if (Facility.VisualSet != EFacilityVisualSet::RuralHideout)
				continue;
			for (const FIntPoint& Occupied : Facility.OccupiedCells)
				if (FMath::Abs(Cell.X - Occupied.X) + FMath::Abs(Cell.Y - Occupied.Y) < 3)
					return true;
		}
	}
	return false;
}

// 게임 지점 하나를 목록에 넣는다(종류별 번호는 Counts 로 센다). 9/28 BuildGameplayPointMarkers 에서 떼어 냄(동작 그대로).
void UPGGameplayPointBuilder::AddDesignPoint(TMap<ELevelDesignPointType, int32>& Counts, ELevelDesignPointType Type, const FIntPoint& Cell,
	const FVector& Offset, const TCHAR* Prefix, const TCHAR* Archetype, uint8 Tier, float RadiusCm, int32 Capacity)
{
	const int32 Index = Counts.FindOrAdd(Type)++;
	FLevelDesignPoint& Point = Map->LevelDesignPoints.AddDefaulted_GetRef();
	Point.Type = Type;
	Point.GridCell = Cell;
	FVector TacticalOffset = Offset;
	if (const FTileDesignPlacement* Placement = Map->TileDesignPlacements.FindByPredicate(
		[Cell](const FTileDesignPlacement& Candidate) { return Candidate.GridCell == Cell; }))
	{
		const FRotator Rotation(0.0f, Placement->RotationQuarterTurns * 90.0f, 0.0f);
		TacticalOffset = Rotation.RotateVector(Offset);
		switch (Type)
		{
		case ELevelDesignPointType::Spawn:
			TacticalOffset += Rotation.RotateVector(FVector(-600.0f, 0.0f, 0.0f)); break;
		case ELevelDesignPointType::Exit:
			TacticalOffset += Rotation.RotateVector(FVector(550.0f, 0.0f, 0.0f)); break;
		case ELevelDesignPointType::Loot:
			TacticalOffset += FVector(-320.0f, 480.0f, 0.0f); break;
		case ELevelDesignPointType::AISpawn:
			TacticalOffset += FVector(420.0f, -420.0f, 0.0f); break;
		case ELevelDesignPointType::Quest:
			TacticalOffset += FVector(0.0f, 0.0f, 0.0f); break;
		}
	}
	Point.WorldLocation = FVector(
		Cell.X * DesignCellSize,
		Cell.Y * DesignCellSize,
		120.0f + Map->GetSurfaceElevationForCell(Cell)) + TacticalOffset;
	// The authored offsets are preferred, but random tactical variants can place
	// cover there. Search a deterministic 3x3 pocket so every emitted point is
	// actually spawnable without changing the selected tile or layout hash.
	const FVector CandidateOffsets[] = {
		FVector::ZeroVector,
		FVector(320, 0, 0), FVector(-320, 0, 0), FVector(0, 320, 0), FVector(0, -320, 0),
		FVector(320, 320, 0), FVector(-320, 320, 0), FVector(320, -320, 0), FVector(-320, -320, 0)
	};
	FCollisionQueryParams PointQuery(SCENE_QUERY_STAT(LevelDesignPointPlacement), false);
	const FCollisionShape PointCapsule = FCollisionShape::MakeCapsule(55.0f, 95.0f);
	for (const FVector& CandidateOffset : CandidateOffsets)
	{
		const FVector Candidate = Point.WorldLocation + CandidateOffset;
		TArray<FOverlapResult> Overlaps;
		const bool bOverlap = GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Candidate + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			PointCapsule,
			PointQuery);
		const bool bBlocked = bOverlap && Overlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != Map.Get()
					&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
					&& !(Map->bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
					&& IsValid(HitComponent)
					&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			});
		const bool bTooCloseToSpawn = Type == ELevelDesignPointType::Spawn
			&& Map->LevelDesignPoints.ContainsByPredicate(
				[&Point, &Candidate](const FLevelDesignPoint& Existing)
				{
					return &Existing != &Point
						&& Existing.Type == ELevelDesignPointType::Spawn
						&& FVector::DistSquared2D(Existing.WorldLocation, Candidate) < FMath::Square(250.0f);
				});
		if (!bBlocked && !bTooCloseToSpawn)
		{
			Point.WorldLocation = Candidate;
			break;
		}
	}
	Point.PointId = FName(*FString::Printf(TEXT("%s_%02d"), Prefix, Index));
	Point.ArchetypeId = FName(Archetype);
	Point.Tier = FMath::Clamp<uint8>(Tier, 1, 3);
	Point.RadiusCm = FMath::Max(50.0f, RadiusCm);
	Point.Capacity = FMath::Max(1, Capacity);
	Point.PointSeed = static_cast<int64>(HashCombine(
		Map->LayoutHash,
		HashCombine(
			GetTypeHash(static_cast<uint8>(Type)),
			HashCombine(GetTypeHash(Cell.X), HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Index))))));

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = Map.Get();
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ATargetPoint* Marker = GetWorld()->SpawnActor<ATargetPoint>(
		ATargetPoint::StaticClass(),
		Point.WorldLocation,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (IsValid(Marker))
	{
		// 라벨·폴더는 에디터 전용 함수다 — 가드가 없으면 전용 서버(에디터 없는 실행 파일) 빌드가 실패한다(9/27 멀티 점검).
#if WITH_EDITOR
		Marker->SetActorLabel(Point.PointId.ToString());
		Marker->SetFolderPath(TEXT("RuntimeDesign/Points"));
#endif
		Marker->Tags.AddUnique(TEXT("LevelDesignPoint"));
		Marker->Tags.AddUnique(FName(Prefix));
		Marker->Tags.AddUnique(Point.PointId);
	}
}

// 타일 칸마다의 지점: 시작·출구·몬스터·루팅. 9/28 떼어 냄(동작 그대로).
void UPGGameplayPointBuilder::AddTileDesignPoints(TMap<ELevelDesignPointType, int32>& Counts)
{
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		// Nothing gameplay-facing belongs in the lake.
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (Placement.Visual == ETileDesignVisual::Spawn)
		{
			// 추가 시작 지역(1번부터) 칸의 자리는 아래에서 0번 뒤에 넣는다(순서 이유는 그쪽 주석).
			if (Map->SpawnRegionCells.IndexOfByKey(Placement.GridCell) > 0)
				continue;
			// Four candidates prevent a future squad from stacking into one capsule.
			AddDesignPoint(Counts, ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddDesignPoint(Counts, ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddDesignPoint(Counts, ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddDesignPoint(Counts, ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		}
		else if (Placement.Visual == ETileDesignVisual::Exit)
			AddDesignPoint(Counts, ELevelDesignPointType::Exit, Placement.GridCell, FVector::ZeroVector, TEXT("ExitPoint"), TEXT("Extraction"), 1, 600, 8);
		else if (((Placement.Visual == ETileDesignVisual::WarZoneGround && Placement.LocalSeed % 17 == 0)
				|| ((Placement.Visual == ETileDesignVisual::OpenGround
						|| Placement.Visual == ETileDesignVisual::Ruins)
					&& Placement.LocalSeed % 31 == 0))
			&& !IsCellProtectedFromAI(Placement.GridCell))
		{
			const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::WarZoneGround
				? TEXT("ScavPatrol") : TEXT("PerimeterPatrol");
			AddDesignPoint(Counts, ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector, TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
		}
		else if (Placement.Visual == ETileDesignVisual::Ruins
			&& Placement.LocalSeed % 3 == 0)
		{
			const uint8 LootTier = Placement.LocalSeed % 19 == 0 ? 3 : (Placement.LocalSeed % 5 == 0 ? 2 : 1);
			AddDesignPoint(Counts, ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector, TEXT("LootPoint"), TEXT("RuinsLooseLoot"), LootTier, 100, 1);
		}

		const bool bNaturalTile = Placement.Visual == ETileDesignVisual::NatureMeadow
			|| Placement.Visual == ETileDesignVisual::NatureForestSparse
			|| Placement.Visual == ETileDesignVisual::NatureForestDense
			|| Placement.Visual == ETileDesignVisual::NatureRocky
			|| Placement.Visual == ETileDesignVisual::NatureScrub
			|| Placement.Visual == ETileDesignVisual::NatureAmbush
			|| Placement.Visual == ETileDesignVisual::NatureServiceCamp
			|| Placement.Visual == ETileDesignVisual::NatureDitch;
		if (bNaturalTile)
		{
			// A deterministic low-density lattice makes the entire 900m field useful
			// for a loot-shooter. The server can serialize these markers in the future
			// manifest; no client-side random choice is involved.
			const int32 StableX = Placement.GridCell.X + 64;
			const int32 StableY = Placement.GridCell.Y + 64;
			const bool bNaturalLoot = (StableX % 7 == 0 && StableY % 7 == 0)
				|| (Placement.Visual == ETileDesignVisual::NatureServiceCamp && Placement.LocalSeed % 3 == 0);
			if (bNaturalLoot)
			{
				const bool bHighValue = Placement.Visual == ETileDesignVisual::NatureServiceCamp
					|| Placement.Visual == ETileDesignVisual::NatureRocky;
				AddDesignPoint(Counts, ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector,
					TEXT("LootPoint"), bHighValue ? TEXT("FieldCache") : TEXT("HiddenStash"),
					bHighValue ? 2 : 1, 120, 1);
			}

			const bool bNaturalEncounter = (StableX % 8 == 4 && StableY % 8 == 4)
				|| (Placement.Visual == ETileDesignVisual::NatureAmbush && Placement.LocalSeed % 11 == 0);
			if (bNaturalEncounter && !IsCellProtectedFromAI(Placement.GridCell))
			{
				const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::NatureAmbush
					? TEXT("ForestAmbush") : TEXT("WildernessPatrol");
				AddDesignPoint(Counts, ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector,
					TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
			}
		}
	}
}

// 맵 가장자리 길 칸에 추가 출구. 9/28 떼어 냄(동작 그대로) — 아래 주석은 원래 자리의 것.
void UPGGameplayPointBuilder::AddExtraEdgeExitPoints(TMap<ELevelDesignPointType, int32>& Counts)
{
	// 추가 출구: 맵 가장자리(바깥 두 줄) 길 칸 가운데, 기존 출구·시작 지점에서 가장 먼 곳부터 ExtraExitCount 개.
	// 왜: 논리 격자가 주는 출구는 판마다 2곳뿐이라, 드래곤 등장 붕괴가 그중 하나를 삼키면 남는 길이 거의 없었다
	//   (9/22 사용자: "파괴 타일 때문에 출구를 더 만들거나 스폰 지역을 늘려야"). 맵을 키우는 대신 출구를 늘렸다 — 성능·조정값이 30칸 기준이다.
	// 길 칸만 고르는 이유: 검문소 꾸미기(PGExitDressingActor)가 길을 따라 문틀·펜스를 세우고, 차로도 나갈 수 있어야 한다.
	// 같은 판 시드면 같은 자리(배치 목록 순서와 거리만 쓴다).
	FIntPoint GridMin(TNumericLimits<int32>::Max()), GridMax(TNumericLimits<int32>::Lowest());
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		GridMin = FIntPoint(FMath::Min(GridMin.X, Placement.GridCell.X), FMath::Min(GridMin.Y, Placement.GridCell.Y));
		GridMax = FIntPoint(FMath::Max(GridMax.X, Placement.GridCell.X), FMath::Max(GridMax.Y, Placement.GridCell.Y));
	}
	TSet<FIntPoint> FacilityCellSet;
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
		for (const FIntPoint& Cell : Facility.OccupiedCells)
			FacilityCellSet.Add(Cell);
	TArray<FIntPoint> Anchors; // 멀리 떨어져야 할 자리: 기존 출구·시작 칸
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Exit || Placement.Visual == ETileDesignVisual::Spawn)
			Anchors.Add(Placement.GridCell);
	for (const FIntPoint& RegionCell : Map->SpawnRegionCells)
		Anchors.AddUnique(RegionCell); // 추가 시작 지역 옆에 출구를 세우지 않는다
	int32 Added = 0;
	for (int32 Pick = 0; Pick < Map->ExtraExitCount; ++Pick)
	{
		const FTileDesignPlacement* Best = nullptr;
		int32 BestScore = -1;
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		{
			// 길 칸이 제일 좋고, 없으면 트인 땅(들판·풀밭·덤불). 첫 시도는 길만 봤는데 이 맵은 길이 가장자리까지 거의 안 나와
			// (가장자리 두 줄 안 길 칸 1개) 출구가 하나도 안 생겼다. 숲·바위·폐허 칸은 검문소가 들어갈 자리가 없어 뺀다.
			const bool bRoad = Placement.Visual == ETileDesignVisual::RoadStraight || Placement.Visual == ETileDesignVisual::RoadDeadEnd;
			// 두 번째 시도도 0곳이었다(가장자리 칸이 대부분 숲). 물·장애물·워존·기존 시작/출구만 빼고 모두 받는다 —
			// 검문소 꾸미기는 나무·바위와 겹치는 부품을 알아서 건너뛰고, 문틀·영역은 어느 땅에나 선다.
			const bool bOpen = Placement.Visual != ETileDesignVisual::Water && Placement.Visual != ETileDesignVisual::Obstacle
				&& Placement.Visual != ETileDesignVisual::WarZoneGround && Placement.Visual != ETileDesignVisual::Spawn
				&& Placement.Visual != ETileDesignVisual::Exit;
			if ((!bRoad && !bOpen) || FacilityCellSet.Contains(Placement.GridCell))
				continue;
			const FIntPoint C = Placement.GridCell;
			const int32 EdgeDistance = FMath::Min(FMath::Min(C.X - GridMin.X, GridMax.X - C.X), FMath::Min(C.Y - GridMin.Y, GridMax.Y - C.Y));
			if (EdgeDistance > 2)
				continue;
			int32 Nearest = TNumericLimits<int32>::Max();
			for (const FIntPoint& Anchor : Anchors)
				Nearest = FMath::Min(Nearest, FMath::Abs(Anchor.X - C.X) + FMath::Abs(Anchor.Y - C.Y));
			// 기존 출구·시작 지점과 8칸(160m) 안이면 몰려 있는 것 — 건너뛴다.
			if (Nearest < 8)
				continue;
			// 길 칸은 거리 +6칸 가산점 — 비슷하게 멀면 길 위를 고른다(차로 나가기 좋다).
			const int32 Score = Nearest + (bRoad ? 6 : 0);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = &Placement;
			}
		}
		if (!Best)
			break;
		AddDesignPoint(Counts, ELevelDesignPointType::Exit, Best->GridCell, FVector::ZeroVector, TEXT("ExitPoint"), TEXT("Extraction"), 1, 600, 8);
		// 지점을 "칸 가운데에서 맵 바깥쪽으로 5.5m" 에 다시 놓는다. 검문소는 "칸 가운데 → 지점" 방향을 길 방향(바깥)으로 읽는데,
		// AddPoint 는 타일 회전 방향으로 5.5m 를 밀어서 길이 없는 땅 칸에서는 가장자리를 따라 옆으로 선 검문소가 나왔다(첫 결과: 북쪽 끝 출구가 옆을 봄).
		{
			const FIntPoint C = Best->GridCell;
			const int32 ToMinX = C.X - GridMin.X, ToMaxX = GridMax.X - C.X, ToMinY = C.Y - GridMin.Y, ToMaxY = GridMax.Y - C.Y;
			const int32 Closest = FMath::Min(FMath::Min(ToMinX, ToMaxX), FMath::Min(ToMinY, ToMaxY));
			const FVector Out = Closest == ToMinX ? FVector(-1, 0, 0) : Closest == ToMaxX ? FVector(1, 0, 0) : Closest == ToMinY ? FVector(0, -1, 0) : FVector(0, 1, 0);
			FLevelDesignPoint& Point = Map->LevelDesignPoints.Last();
			Point.WorldLocation = FVector(Best->WorldLocation.X, Best->WorldLocation.Y, Point.WorldLocation.Z) + Out * 550.0f;
		}
		Anchors.Add(Best->GridCell);
		++Added;
		UE_LOG(LogTemp, Display, TEXT("Extra exit %d: cell %d,%d on the map edge (score %d = distance to nearest exit/spawn + road bonus)"),
			Added, Best->GridCell.X, Best->GridCell.Y, BestScore);
	}
	if (Added < Map->ExtraExitCount)
	{
		// 진단: 가장자리에서 몇 칸 안쪽까지 길 칸이 있는지.
		TMap<int32, int32> RoadByEdge;
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		{
			if (Placement.Visual < ETileDesignVisual::RoadStraight || Placement.Visual > ETileDesignVisual::RoadDeadEnd)
				continue;
			const FIntPoint C = Placement.GridCell;
			++RoadByEdge.FindOrAdd(FMath::Min(FMath::Min(C.X - GridMin.X, GridMax.X - C.X), FMath::Min(C.Y - GridMin.Y, GridMax.Y - C.Y)));
		}
		FString Hist;
		for (int32 D = 0; D <= 6; ++D)
			Hist += FString::Printf(TEXT(" d%d=%d"), D, RoadByEdge.FindRef(D));
		UE_LOG(LogTemp, Display, TEXT("Extra exits: only %d/%d — grid %s..%s, road cells by edge distance:%s, anchors %d"),
			Added, Map->ExtraExitCount, *GridMin.ToString(), *GridMax.ToString(), *Hist, Anchors.Num());
	}
}

// 시설 안 지점: 시설 종류별로 정해 둔 자리(시설 기준 좌표)에 루팅·몬스터·퀘스트·시작 지점. 9/28 떼어 냄(동작 그대로).
void UPGGameplayPointBuilder::AddFacilityDesignPoints(TMap<ELevelDesignPointType, int32>& Counts)
{
	auto AddFacilityPoint = [this, &Counts](
		const FFacilityPlacement& Facility,
		ELevelDesignPointType Type,
		const FVector& LocalSocket,
		const TCHAR* Prefix,
		const TCHAR* Archetype,
		uint8 Tier,
		float RadiusCm,
		int32 Capacity)
	{
		if (Facility.OccupiedCells.IsEmpty())
			return;
		const FVector FacilityCenter = Map->GetDesignFootprintCenter(Facility);
		const FRotator FacilityRotation(0.0f, Facility.RotationQuarterTurns * 90.0f, 0.0f);
		const FVector DesiredWorld = FacilityCenter + FacilityRotation.RotateVector(LocalSocket);
		const FIntPoint SocketCell = *Algo::MinElementBy(
			Facility.OccupiedCells,
			[&DesiredWorld](const FIntPoint& Cell)
			{
				return FVector2D(
					Cell.X * DesignCellSize - DesiredWorld.X,
					Cell.Y * DesignCellSize - DesiredWorld.Y).SizeSquared();
			});
		const FVector CellBase(
			SocketCell.X * DesignCellSize,
			SocketCell.Y * DesignCellSize,
			120.0f + Facility.BaseElevationCm);
		AddDesignPoint(Counts, Type, SocketCell, DesiredWorld - CellBase, Prefix, Archetype, Tier, RadiusCm, Capacity);
	};

	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		if (Facility.VisualSet == EFacilityVisualSet::Warehouse)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1850, -830, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 3, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(2100, 1850, 580), TEXT("LootPoint"), TEXT("WarehouseContainer"), 3, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-500, 1500, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1400, -1200, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			// The 3x5 compound's outer hall rows. Growing the core ate WarZone_Mid
			// band cells and their tile loot with them - the map-wide count fell
			// under the verifier's 40 - so the halls that replaced those cells carry
			// the loot instead.
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(0, -3900, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(0, 3800, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1400, 2600, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-900, -1650, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(900, 650, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(2100, 1850, 580), TEXT("QuestPoint"), TEXT("WarZonePrimaryObjective"), 3, 160, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::Yard)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, -700, 120), TEXT("LootPoint"), TEXT("YardStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1050, -950, 360), TEXT("LootPoint"), TEXT("YardStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(1200, 900, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-1200, -900, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::LongBarracks)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1100, 350, 120), TEXT("LootPoint"), TEXT("BarracksLocker"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1100, -350, 120), TEXT("LootPoint"), TEXT("BarracksLocker"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-1500, -450, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			if ((Facility.LocalSeed & 1u) == 0u)
				AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(1100, -350, 120), TEXT("QuestPoint"), TEXT("OptionalFacilityObjective"), 2, 140, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::LinearTrench)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-2600, 0, 120), TEXT("LootPoint"), TEXT("TrenchCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(2600, 0, 120), TEXT("LootPoint"), TEXT("TrenchCache"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-3400, 0, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::DowntownBlock)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1600, 700, 120), TEXT("LootPoint"), TEXT("DowntownShopCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1450, -750, 120), TEXT("LootPoint"), TEXT("DowntownBackroom"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(0, 1400, 120), TEXT("AISpawnPoint"), TEXT("DowntownPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(1500, 850, 120), TEXT("QuestPoint"), TEXT("DowntownObjective"), 2, 140, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::FactoryConstruction)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-800, -700, 120), TEXT("LootPoint"), TEXT("FactoryToolCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, 650, 120), TEXT("LootPoint"), TEXT("FactoryOfficeCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(900, -900, 120), TEXT("AISpawnPoint"), TEXT("FactoryPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::RuralHideout)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-900, 250, 120), TEXT("LootPoint"), TEXT("RuralCabinStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, -250, 120), TEXT("LootPoint"), TEXT("RuralCaravanStash"), 1, 100, 1);
			// The hamlet hosts the boat-landing spawn whenever the lake exists, and
			// a facility that spawns players keeps no resident AI - the same rule
			// the spawn safe band applies everywhere else. A 40 m hamlet cannot hold
			// both a spawn and an ambush 50 m apart (measured 26 m: pass=false).
			if (!Map->bHasBorderLake)
				AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(0, -900, 120), TEXT("AISpawnPoint"), TEXT("RuralAmbush"), 1, 260, 2);
			if (Map->bHasBorderLake)
			{
				// "Arrived by boat": a spawn on the hamlet's water-facing edge. The
				// lake corner moves per seed, so aim at the recorded lake centre and
				// undo the facility's yaw - a fixed socket would face the water only
				// on the seeds that happen to rotate the hamlet the authored way.
				const FVector FacilityCentre = Map->GetDesignFootprintCenter(Facility);
				const FVector2D ToLake = (Map->BorderLakeCentreCell * DesignCellSize
					- FVector2D(FacilityCentre.X, FacilityCentre.Y)).GetSafeNormal();
				const FRotator FacilityYaw(0.0f, Facility.RotationQuarterTurns * 90.0f, 0.0f);
				const FVector LakeSocket = FacilityYaw.UnrotateVector(
					FVector(ToLake.X, ToLake.Y, 0.0f) * 1900.0f) + FVector(0.0f, 0.0f, 120.0f);
				AddFacilityPoint(Facility, ELevelDesignPointType::Spawn, LakeSocket,
					TEXT("SpawnPoint"), TEXT("BoatLanding"), 2, 150, 2);
			}
		}
		else
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector::ZeroVector, TEXT("LootPoint"), TEXT("CheckpointCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-500, 500, 120), TEXT("AISpawnPoint"), TEXT("CheckpointGuard"), 2, 300, 2);
		}
	}
}

void UPGGameplayPointBuilder::RebuildGameplayPointHash()
{
	Map->GameplayPointHash = 0;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		// FName's runtime comparison index is process-local. Hash the serialized
		// string contents so server and clients agree after independent launches.
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, FCrc::StrCrc32(*Point.PointId.ToString()));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, FCrc::StrCrc32(*Point.ArchetypeId.ToString()));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(Point.GridCell.X));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(Point.GridCell.Y));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(Point.Tier));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.X)));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Y)));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Z)));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.RadiusCm)));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(Point.Capacity));
		Map->GameplayPointHash = HashCombine(Map->GameplayPointHash, GetTypeHash(Point.PointSeed));
	}
}

void UPGGameplayPointBuilder::BroadcastLevelDesignPointsWhenReady()
{
	if (Map->bLevelDesignPointsBuilt || !Map->bLevelDesignPointsGenerated)
		return;
	// 보통은 시설이 다 뜨고 벽 속 지점을 옮긴 직후(ResolveGameplayPointSafety)에 넘긴다.
	// 시설 하나가 끝내 안 뜨면(에셋 누락 등) 몬스터·상자가 영영 안 나오므로 20초 뒤에는 그대로 넘긴다.
	const float Waited = GetWorld()->GetTimeSeconds() - Map->LevelDesignPointsGeneratedTime;
	const bool bTimedOut = Waited >= 20.0f;
	const bool bReady = Map->bResolvedGameplayPointSafety || bTimedOut;

	// 로딩 가림막: 준비될 때까지 내 화면(로컬 플레이어)을 검게 둔다. 빈 터를 걷다가 공장이 눈앞에 툭 생기는 걸 안 보이게.
	// UI 위젯이 아니라 카메라 페이드라 팀원 UI 와 안 겹친다. 매 Tick 다시 거는 건 늦게 들어온 플레이어 컨트롤러도 가리려고.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (!IsValid(Controller) || !Controller->IsLocalController() || !IsValid(Controller->PlayerCameraManager))
			continue;
		if (bReady)
			Controller->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 1.0f, FLinearColor::Black, false, false);
		else
			Controller->PlayerCameraManager->SetManualCameraFade(1.0f, FLinearColor::Black, false);
	}
	if (!bReady)
		return;
	Map->bLevelDesignPointsBuilt = true;
	UE_LOG(LogTemp, Display, TEXT("Design points ready for spawners: waited=%.1fs facilities_ready=%s"),
		Waited, Map->bResolvedGameplayPointSafety ? TEXT("true") : TEXT("false(timeout)"));
	Map->OnLevelDesignPointsBuilt.Broadcast(Map->LevelDesignPoints);
}

void UPGGameplayPointBuilder::ResolveGameplayPointSafety()
{
	if (Map->bResolvedGameplayPointSafety || !Map->TileSpawner->AreAllFacilityLevelsLoaded() || Map->LevelDesignPoints.IsEmpty())
		return;

	TArray<FVector> CandidateOffsets;
	for (int32 X = -4; X <= 4; ++X)
		for (int32 Y = -4; Y <= 4; ++Y)
			CandidateOffsets.Add(FVector(X * 225.0f, Y * 225.0f, 0.0f));
	CandidateOffsets.Sort([](const FVector& A, const FVector& B)
	{
		const float ADistance = A.SizeSquared2D();
		const float BDistance = B.SizeSquared2D();
		if (!FMath::IsNearlyEqual(ADistance, BDistance)) return ADistance < BDistance;
		return !FMath::IsNearlyEqual(A.X, B.X) ? A.X < B.X : A.Y < B.Y;
	});

	auto IsBlocked = [this](const FVector& Location)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ResolveGameplayPointSafety), false);
		const bool bOverlap = GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Location + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(55.0f, 95.0f),
			Query);
		return bOverlap && Overlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != Map.Get()
					&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
					&& !(Map->bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
					&& IsValid(HitComponent)
					&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			});
	};

	int32 RelocatedCount = 0;
	int32 UnresolvedCount = 0;
	TArray<FString> UnresolvedIds;
	TArray<FVector> ResolvedSpawnLocations;
	for (FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		const bool bSpawnTooClose = Point.Type == ELevelDesignPointType::Spawn
			&& ResolvedSpawnLocations.ContainsByPredicate(
				[&Point](const FVector& Existing)
				{
					return FVector::DistSquared2D(Existing, Point.WorldLocation) < FMath::Square(250.0f);
				});
		if (!IsBlocked(Point.WorldLocation) && !bSpawnTooClose)
		{
			if (Point.Type == ELevelDesignPointType::Spawn)
				ResolvedSpawnLocations.Add(Point.WorldLocation);
			continue;
		}

		const FVector CellCenter(
			Point.GridCell.X * DesignCellSize,
			Point.GridCell.Y * DesignCellSize,
			120.0f + Map->GetSurfaceElevationForCell(Point.GridCell));
		bool bFound = false;
		for (const FVector& Offset : CandidateOffsets)
		{
			const FVector Candidate = CellCenter + Offset;
			if (IsBlocked(Candidate))
				continue;
			if (Point.Type == ELevelDesignPointType::Spawn
				&& ResolvedSpawnLocations.ContainsByPredicate(
					[&Candidate](const FVector& Existing)
					{
						return FVector::DistSquared2D(Existing, Candidate) < FMath::Square(250.0f);
					}))
			{
				continue;
			}
			Point.WorldLocation = Candidate;
			if (Point.Type == ELevelDesignPointType::Spawn)
				ResolvedSpawnLocations.Add(Point.WorldLocation);
			bFound = true;
			++RelocatedCount;
			for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
			{
				if (It->Tags.Contains(Point.PointId))
				{
					It->SetActorLocation(Point.WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
					break;
				}
			}
			break;
		}
		if (!bFound)
		{
			++UnresolvedCount;
			if (UnresolvedIds.Num() < 8)
				UnresolvedIds.Add(Point.PointId.ToString());
		}
	}

	Map->bResolvedGameplayPointSafety = true;
	RebuildGameplayPointHash();
	UE_LOG(LogTemp, Display,
		TEXT("Gameplay point safety resolution: relocated=%d unresolved=%d final_point_hash=%08X pass=%s sample=[%s]"),
		RelocatedCount,
		UnresolvedCount,
		Map->GameplayPointHash,
		UnresolvedCount == 0 ? TEXT("true") : TEXT("false"),
		*FString::Join(UnresolvedIds, TEXT(",")));
}
