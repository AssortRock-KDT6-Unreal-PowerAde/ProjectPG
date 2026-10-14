#include "Actors/MapBuilder/MapPointPlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapAssetSet.h"
#include "Algo/MinElement.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "LandscapeProxy.h"
#include "GameFramework/Pawn.h"

using namespace MapBuild;

void UMapPointPlanner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapPointPlanner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

const UDataTable* UMapPointPlanner::LoadFacilityPointTable() const
{
	const UDataTable* Table = Map->GetMapAssets().FacilityPointTable.LoadSynchronous();
	if (!Table)
		UE_LOG(LogTemp, Error, TEXT("Map points: facility point table missing (%s) - facilities get no points"),
			*Map->GetMapAssets().FacilityPointTable.ToString());
	return Table;
}

// 지점 찍기.
// ① 칸마다: 시작 칸 → 시작 자리 4개, 출구 칸 → 출구 자리, 폐허·숲 칸 → 규칙에 맞으면 상자·몬스터 자리.
// ② 추가 시작 구역마다 시작 자리 4개.
// ③ 시설마다: 시설 지점 표(DT_FacilityPoints)에서 그 시설 줄을 순서대로 찍는다. 호숫가 마을의 보트 시작 자리만 코드로 계산.
// 같은 시드 = 같은 칸·같은 씨앗 → 같은 자리. 판마다 시각으로 뽑는 값은 없다.
void UMapPointPlanner::BuildPoints()
{
	Map->LevelDesignPoints.Reset();
	TMap<ELevelDesignPointType, int32> Counts;
	auto IsProtectedFromAI = [this](const FIntPoint& Cell)
	{
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		{
			const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
				+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
			if (Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
				return true; // 시작 지점 주변 최소 60m 안전 띠
			if (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2)
				return true; // 탈출 지점 표시 위에 바로 진 치지 않게
		}
		// 보트 상륙 지점도 시작 지점이지만, Spawn 타일이 아니라 시골 마을 위의 게임플레이 지점이라
		// 위의 띠 검사가 못 본다. 이게 없으면 들판 교전 지점이 상륙 지점 49.5 m 까지 다가왔고,
		// 마을 자체가 '상주 AI 가 없는 시설'로 집계됐다.
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
	};

	auto AddPoint = [this, &Counts](
		ELevelDesignPointType Type,
		const FIntPoint& Cell,
		const FVector& Offset,
		const TCHAR* Prefix,
		FName Archetype,
		uint8 Tier,
		float RadiusCm,
		int32 Capacity)
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
		// 손으로 정한 위치를 먼저 쓰지만, 무작위 전술 변형이 그 자리에 엄폐물을 놓을 수 있다.
		// 매번 똑같은 3x3 주변을 찾아서, 내보내는 모든 지점이 실제로 스폰 가능하게 한다.
		// 고른 타일이나 layout hash 는 바뀌지 않는다.
		const FVector CandidateOffsets[] = {
			FVector::ZeroVector,
			FVector(320, 0, 0), FVector(-320, 0, 0), FVector(0, 320, 0), FVector(0, -320, 0),
			FVector(320, 320, 0), FVector(-320, 320, 0), FVector(320, -320, 0), FVector(-320, -320, 0)
		};
		for (const FVector& CandidateOffset : CandidateOffsets)
		{
			const FVector Candidate = Point.WorldLocation + CandidateOffset;
			const bool bBlocked = IsSpotBlocked(Candidate);
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
		Point.ArchetypeId = Archetype;
		Point.Tier = FMath::Clamp<uint8>(Tier, 1, 3);
		Point.RadiusCm = FMath::Max(50.0f, RadiusCm);
		Point.Capacity = FMath::Max(1, Capacity);
		Point.PointSeed = static_cast<int64>(HashCombine(
			Map->LayoutHash,
			HashCombine(
				GetTypeHash(static_cast<uint8>(Type)),
				HashCombine(GetTypeHash(Cell.X), HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Index))))));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Map;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATargetPoint* Marker = GetWorld()->SpawnActor<ATargetPoint>(
			ATargetPoint::StaticClass(),
			Point.WorldLocation,
			FRotator::ZeroRotator,
			SpawnParameters);
		if (IsValid(Marker))
		{
#if WITH_EDITOR
			Marker->SetActorLabel(Point.PointId.ToString());
			Marker->SetFolderPath(TEXT("RuntimeDesign/Points"));
#endif
			Marker->Tags.AddUnique(TEXT("LevelDesignPoint"));
			Marker->Tags.AddUnique(FName(Prefix));
			Marker->Tags.AddUnique(Point.PointId);
		}
	};

	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		// 호수 안에는 게임플레이용 지점을 두지 않는다.
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (Placement.Visual == ETileDesignVisual::Spawn)
		{
			// 추가 시작 구역(1번부터)의 자리는 아래(칸 돌기가 끝난 뒤)에서 넣는다.
			// 왜: 검사기가 "첫 번째 시작 지점" 을 출발점으로 쓴다. 칸 순서대로 넣으면 첫 자리가 다른 구역으로 바뀐다.
			if (Map->SpawnRegionCells.IndexOfByKey(Placement.GridCell) > 0)
				continue;
			// 후보를 네 개 둬서, 나중에 분대가 캡슐 하나에 겹쳐 나오지 않게 한다.
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		}
		else if (Placement.Visual == ETileDesignVisual::Exit)
			AddPoint(ELevelDesignPointType::Exit, Placement.GridCell, FVector::ZeroVector, TEXT("ExitPoint"), TEXT("Extraction"), 1, 600, 8);
		else if (((Placement.Visual == ETileDesignVisual::WarZoneGround && Placement.LocalSeed % 17 == 0)
				|| ((Placement.Visual == ETileDesignVisual::OpenGround
						|| Placement.Visual == ETileDesignVisual::Ruins)
					&& Placement.LocalSeed % 31 == 0))
			&& !IsProtectedFromAI(Placement.GridCell))
		{
			const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::WarZoneGround
				? TEXT("ScavPatrol") : TEXT("PerimeterPatrol");
			AddPoint(ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector, TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
		}
		else if (Placement.Visual == ETileDesignVisual::Ruins
			&& Placement.LocalSeed % 3 == 0)
		{
			const uint8 LootTier = Placement.LocalSeed % 19 == 0 ? 3 : (Placement.LocalSeed % 5 == 0 ? 2 : 1);
			AddPoint(ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector, TEXT("LootPoint"), TEXT("RuinsLooseLoot"), LootTier, 100, 1);
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
			// 매번 똑같이 나오는 낮은 밀도의 격자로 900m 들판 전체를 루트 슈터에 쓸모 있게 만든다.
			// 서버가 나중에 이 표시들을 manifest 에 담아 보낼 수 있다.
			// 클라이언트 쪽 무작위 선택은 없다.
			const int32 StableX = Placement.GridCell.X + 64;
			const int32 StableY = Placement.GridCell.Y + 64;
			const bool bNaturalLoot = (StableX % 7 == 0 && StableY % 7 == 0)
				|| (Placement.Visual == ETileDesignVisual::NatureServiceCamp && Placement.LocalSeed % 3 == 0);
			if (bNaturalLoot)
			{
				const bool bHighValue = Placement.Visual == ETileDesignVisual::NatureServiceCamp
					|| Placement.Visual == ETileDesignVisual::NatureRocky;
				AddPoint(ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector,
					TEXT("LootPoint"), bHighValue ? TEXT("FieldCache") : TEXT("HiddenStash"),
					bHighValue ? 2 : 1, 120, 1);
			}

			const bool bNaturalEncounter = (StableX % 8 == 4 && StableY % 8 == 4)
				|| (Placement.Visual == ETileDesignVisual::NatureAmbush && Placement.LocalSeed % 11 == 0);
			if (bNaturalEncounter && !IsProtectedFromAI(Placement.GridCell))
			{
				const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::NatureAmbush
					? TEXT("ForestAmbush") : TEXT("WildernessPatrol");
				AddPoint(ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector,
					TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
			}
		}
	}

	// 추가 시작 구역(1번부터)마다 자리 4개. 0번(형님 시작 칸) 자리보다 뒤에 넣는다(위 주석).
	for (int32 RegionIndex = 1; RegionIndex < Map->SpawnRegionCells.Num(); ++RegionIndex)
	{
		const FIntPoint& RegionCell = Map->SpawnRegionCells[RegionIndex];
		AddPoint(ELevelDesignPointType::Spawn, RegionCell, FVector(0, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		AddPoint(ELevelDesignPointType::Spawn, RegionCell, FVector(0, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		AddPoint(ELevelDesignPointType::Spawn, RegionCell, FVector(360, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		AddPoint(ELevelDesignPointType::Spawn, RegionCell, FVector(360, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
	}

	// 시설 기준 위치(LocalSocket)를 시설 방향대로 돌린 뒤, 그 위치에서 가장 가까운 시설 칸에 붙인다.
	auto AddFacilityPoint = [this, &AddPoint](
		const FFacilityPlacement& Facility,
		ELevelDesignPointType Type,
		const FVector& LocalSocket,
		const TCHAR* Prefix,
		FName Archetype,
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
		AddPoint(Type, SocketCell, DesiredWorld - CellBase, Prefix, Archetype, Tier, RadiusCm, Capacity);
	};

	// 지점 이름 앞부분(LootPoint_03 의 LootPoint). 종류로 정해진다.
	auto PrefixFor = [](ELevelDesignPointType Type) -> const TCHAR*
	{
		switch (Type)
		{
		case ELevelDesignPointType::Spawn:   return TEXT("SpawnPoint");
		case ELevelDesignPointType::Loot:    return TEXT("LootPoint");
		case ELevelDesignPointType::AISpawn: return TEXT("AISpawnPoint");
		case ELevelDesignPointType::Exit:    return TEXT("ExitPoint");
		default:                             return TEXT("QuestPoint");
		}
	};

	// 표의 줄을 한 번만 꺼내 둔다(시설마다 표 전체를 다시 읽지 않게). 순서 = 표의 줄 순서.
	TArray<FFacilityPointRow*> FacilityRows;
	if (const UDataTable* Table = LoadFacilityPointTable())
		Table->GetAllRows<FFacilityPointRow>(TEXT("MapPointPlanner"), FacilityRows);

	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		for (const FFacilityPointRow* Row : FacilityRows)
		{
			if (!Row || Row->Facility != Facility.VisualSet)
				continue;
			if (Row->Condition == EFacilityPointCondition::EvenSeedOnly && (Facility.LocalSeed & 1u) != 0u)
				continue;
			if (Row->Condition == EFacilityPointCondition::NoLakeOnly && Map->bHasBorderLake)
				continue;
			AddFacilityPoint(Facility, Row->Type, Row->LocalSocket, PrefixFor(Row->Type), Row->Archetype,
				static_cast<uint8>(Row->Tier), Row->RadiusCm, Row->Capacity);
		}

		// 호숫가 마을의 "보트 타고 도착" 시작 자리. 호수 위치가 판마다 달라서 표의 고정 좌표로는 못 적는다.
		if (Facility.VisualSet == EFacilityVisualSet::RuralHideout && Map->bHasBorderLake)
		{
			// "보트로 도착": 마을의 물 쪽 가장자리에 있는 시작 지점. 호수 모서리는 시드마다 바뀌므로,
			// 기록된 호수 중심을 향하게 하고 시설의 회전(yaw)을 되돌린다. 위치를 고정해 두면
			// 마을이 우연히 원래 방향으로 돌아간 시드에서만 물을 바라본다.
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

	RebuildHash();

	// 다른 담당에게 알린다. 재시도로 다시 만들어져도 첫 확정만 알린다.
	if (!Map->bLevelDesignPointsBuilt)
	{
		Map->bLevelDesignPointsBuilt = true;
		Map->OnLevelDesignPointsBuilt.Broadcast(Map->LevelDesignPoints);
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

// 사람 한 명(캡슐 반지름 55·반높이 95)이 Location 바닥에 설 수 있나. 맵 바닥·지점 표시·사람은 막힘으로 안 친다.
// 지점 찍기와 끼임 정리가 같은 기준을 써야 해서 한 곳에 둔다(예전엔 두 군데에 같은 코드가 있었다).
bool UMapPointPlanner::IsSpotBlocked(const FVector& Location) const
{
	TArray<FOverlapResult> Overlaps;
	const FCollisionQueryParams Query(SCENE_QUERY_STAT(MapPointSpot), false);
	const bool bOverlap = GetWorld()->OverlapMultiByChannel(
		Overlaps, Location + FVector(0, 0, 95.0f), FQuat::Identity, ECC_Pawn,
		FCollisionShape::MakeCapsule(55.0f, 95.0f), Query);
	return bOverlap && Overlaps.ContainsByPredicate([this](const FOverlapResult& Result)
	{
		const AActor* HitActor = Result.GetActor();
		const UPrimitiveComponent* HitComponent = Result.GetComponent();
		return IsValid(HitActor) && HitActor != Map
			&& !HitActor->IsA<APawn>() // 사람·몬스터는 맵이 아니다(리슨 서버에서 들어온 사람 쪽엔 이미 캐릭터가 서 있어 자리가 달라졌음)
			&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
			&& !(Map->bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
			&& IsValid(HitComponent)
			&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
	});
}

void UMapPointPlanner::RebuildHash()
{
	uint32 Hash = 0;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		// FName 의 실행 중 비교 번호는 그 프로세스 안에서만 유효하다. 저장되는 문자열 내용으로
		// 해시해야 서버와 클라이언트가 따로 켜져도 같은 값을 낸다.
		Hash = HashCombine(Hash, FCrc::StrCrc32(*Point.PointId.ToString()));
		Hash = HashCombine(Hash, FCrc::StrCrc32(*Point.ArchetypeId.ToString()));
		Hash = HashCombine(Hash, GetTypeHash(Point.GridCell.X));
		Hash = HashCombine(Hash, GetTypeHash(Point.GridCell.Y));
		Hash = HashCombine(Hash, GetTypeHash(Point.Tier));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.X)));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Y)));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Z)));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Point.RadiusCm)));
		Hash = HashCombine(Hash, GetTypeHash(Point.Capacity));
		Hash = HashCombine(Hash, GetTypeHash(Point.PointSeed));
	}
	Map->GameplayPointHash = Hash;
}

// 끼임 정리. 시설 레벨이 다 보인 뒤에야 벽 위치를 알 수 있어서, 지점 찍기와 따로 나중에 한다.
// 막힌 자리는 칸 가운데 둘레 9×9(225cm 간격)를 가까운 순서로 훑어 첫 빈 곳으로 옮긴다.
void UMapPointPlanner::ResolveSafety()
{
	if (Map->bResolvedGameplayPointSafety || !Map->AreAllFacilityLevelsLoaded() || Map->LevelDesignPoints.IsEmpty())
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
		if (!IsSpotBlocked(Point.WorldLocation) && !bSpawnTooClose)
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
			if (IsSpotBlocked(Candidate))
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
	RebuildHash();
	UE_LOG(LogTemp, Display,
		TEXT("Gameplay point safety resolution: relocated=%d unresolved=%d final_point_hash=%08X pass=%s sample=[%s]"),
		RelocatedCount,
		UnresolvedCount,
		Map->GameplayPointHash,
		UnresolvedCount == 0 ? TEXT("true") : TEXT("false"),
		*FString::Join(UnresolvedIds, TEXT(",")));
}
